#define _GNU_SOURCE

#include "hermas2/host_linux.h"

#include "hermas2/image.h"
#include "hermas2/saga_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static void initialize_empty(hermas2_host *host) {
    memset(host, 0, sizeof(*host));
    host->image_descriptor = -1;
    host->app_listener = -1;
    host->control_listener = -1;
    host->journal.file_descriptor = -1;
    host->compensation.file_descriptor = -1;
    host->results.file_descriptor = -1;
    host->saga_log.file_descriptor = -1;
}

static bool valid_text(const char *text) {
    return text != NULL && text[0] != '\0';
}

static hermas2_host_result load_image(
    hermas2_host *host,
    const char *path) {
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat status;
    if (descriptor < 0 || fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        (uint64_t)status.st_size > SIZE_MAX ||
        (status.st_mode & 022u) != 0u) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return HERMAS2_HOST_IMAGE_ERROR;
    }
    size_t size = (size_t)status.st_size;
    void *mapping =
        mmap(NULL, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (mapping == MAP_FAILED ||
        hermas2_image_validate(mapping, size, NULL) !=
            HERMAS2_IMAGE_OK) {
        if (mapping != MAP_FAILED) {
            (void)munmap(mapping, size);
        }
        close(descriptor);
        return HERMAS2_HOST_IMAGE_ERROR;
    }
    host->image_descriptor = descriptor;
    host->image = mapping;
    host->image_size = size;
    return HERMAS2_HOST_OK;
}

static hermas2_host_result validate_state_directory(const char *path) {
    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != geteuid() ||
        (status.st_mode & 077u) != 0u ||
        access(path, R_OK | W_OK | X_OK) != 0) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    return HERMAS2_HOST_OK;
}

static bool state_path(
    char destination[PATH_MAX],
    const char *directory,
    const char *name) {
    int length = snprintf(
        destination, PATH_MAX, "%s/%s", directory, name);
    return length > 0 && length < PATH_MAX;
}

static hermas2_host_result open_state(
    hermas2_host *host,
    const hermas2_host_config *config,
    hermas2_journal_summary *journal_summary,
    hermas2_saga_log_summary *saga_summary) {
    if (validate_state_directory(config->state_directory) !=
        HERMAS2_HOST_OK) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    char journal_path[PATH_MAX];
    char result_path[PATH_MAX];
    char compensation_path[PATH_MAX];
    char saga_path[PATH_MAX];
    if (!state_path(
            journal_path, config->state_directory, "journal.h2j") ||
        !state_path(
            result_path, config->state_directory, "results.h2r") ||
        !state_path(
            compensation_path, config->state_directory,
            "compensation.h2c") ||
        !state_path(
            saga_path, config->state_directory, "saga.h2s")) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    hermas2_result_summary result_summary;
    hermas2_compensation_summary compensation_summary;
    if (hermas2_journal_file_open(
            &host->journal, journal_path, journal_summary) !=
            HERMAS2_JOURNAL_OK) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    host->journal_open = true;
    if (hermas2_result_file_open(
            &host->results, result_path, &result_summary) !=
            HERMAS2_RESULT_STORE_OK) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    host->result_open = true;
    if (hermas2_compensation_file_open(
            &host->compensation, compensation_path,
            &compensation_summary) != HERMAS2_COMPENSATION_OK) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    host->compensation_open = true;
    if (hermas2_saga_log_file_open(
            &host->saga_log, saga_path, saga_summary) !=
            HERMAS2_SAGA_LOG_OK) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    host->saga_log_open = true;
    host->next_execution_id = journal_summary->next_execution_id;
    return HERMAS2_HOST_OK;
}

static hermas2_host_result restore_startup_state(
    hermas2_host *host,
    const hermas2_host_config *config,
    const hermas2_journal_summary *journal_summary,
    const hermas2_saga_log_summary *saga_summary) {
    size_t closed_count = 0u;
    if (hermas2_journal_file_close_interrupted(
            &host->journal, journal_summary, &closed_count) !=
            HERMAS2_JOURNAL_OK) {
        return HERMAS2_HOST_STATE_ERROR;
    }
    (void)closed_count;
    for (uint8_t index = 0u;
         index < saga_summary->active_count; ++index) {
        const hermas2_saga_log_active *active =
            &saga_summary->active[index];
        if (active->workflow_id != config->workflow_id ||
            active->image_fingerprint !=
                host->loop.image_fingerprint) {
            return HERMAS2_HOST_STATE_ERROR;
        }
        hermas2_saga_execution recovered;
        hermas2_saga_result result = hermas2_saga_recover_files(
            &recovered, host->image, host->image_size,
            &host->journal, &host->compensation,
            &host->saga_log, active->execution_id,
            config->workflow_id);
        if (result == HERMAS2_SAGA_UNSAFE_HISTORY) {
            return HERMAS2_HOST_RECOVERY_REQUIRED;
        }
        if (result != HERMAS2_SAGA_OK ||
            recovered.state != HERMAS2_SAGA_READY ||
            recovered.remaining == 0u ||
            hermas2_daemon_loop_resume_saga(
                &host->loop, &recovered) != HERMAS2_LOOP_OK) {
            return HERMAS2_HOST_STATE_ERROR;
        }
        host->recovered_execution_ids[
            host->recovered_execution_count++] =
                active->execution_id;
    }
    return HERMAS2_HOST_OK;
}

static hermas2_host_result create_listener(
    const char *path,
    int *listener,
    char stored_path[sizeof(((struct sockaddr_un *)0)->sun_path)],
    bool *bound) {
    size_t path_length = strlen(path);
    if (path_length == 0u ||
        path_length >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        return HERMAS2_HOST_SOCKET_ERROR;
    }
    int descriptor = socket(
        AF_UNIX,
        SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0);
    if (descriptor < 0) {
        return HERMAS2_HOST_SOCKET_ERROR;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, path_length + 1u);
    if (bind(
            descriptor, (const struct sockaddr *)&address,
            sizeof(address)) != 0) {
        close(descriptor);
        return HERMAS2_HOST_SOCKET_ERROR;
    }
    if (chmod(path, 0600) != 0 ||
        listen(descriptor, HERMAS2_DAEMON_MAX_ACTIONS) != 0) {
        close(descriptor);
        (void)unlink(path);
        return HERMAS2_HOST_SOCKET_ERROR;
    }
    memcpy(stored_path, path, path_length + 1u);
    *listener = descriptor;
    *bound = true;
    return HERMAS2_HOST_OK;
}

hermas2_host_result hermas2_host_open(
    hermas2_host *host,
    const hermas2_host_config *config) {
    if (host == NULL || config == NULL ||
        !valid_text(config->image_path) ||
        !valid_text(config->state_directory) ||
        !valid_text(config->app_socket_path) ||
        !valid_text(config->control_socket_path) ||
        config->workflow_id == 0u ||
        strcmp(
            config->app_socket_path,
            config->control_socket_path) == 0) {
        return HERMAS2_HOST_INVALID_ARGUMENT;
    }
    initialize_empty(host);
    hermas2_host_result result =
        load_image(host, config->image_path);
    if (result != HERMAS2_HOST_OK) {
        hermas2_host_close(host);
        return result;
    }
    if (hermas2_daemon_registry_init(
            &host->registry, host->image, host->image_size) !=
            HERMAS2_DAEMON_OK) {
        hermas2_host_close(host);
        return HERMAS2_HOST_IMAGE_ERROR;
    }
    host->registry_initialized = true;
    if (hermas2_registration_server_init(
            &host->registration, &host->registry) !=
            HERMAS2_REGISTRATION_SERVER_OK) {
        hermas2_host_close(host);
        return HERMAS2_HOST_REGISTRATION_ERROR;
    }
    host->registration_initialized = true;
    if (hermas2_daemon_loop_init(
            &host->loop, &host->registry,
            host->image, host->image_size) != HERMAS2_LOOP_OK) {
        hermas2_host_close(host);
        return HERMAS2_HOST_IMAGE_ERROR;
    }
    hermas2_journal_summary journal_summary;
    hermas2_saga_log_summary saga_summary;
    result = open_state(
        host, config, &journal_summary, &saga_summary);
    if (result != HERMAS2_HOST_OK) {
        hermas2_host_close(host);
        return result;
    }
    if (hermas2_daemon_loop_attach_journal(
            &host->loop, &host->journal.writer,
            config->workflow_id) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_set_execution_floor(
            &host->loop, host->next_execution_id) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_results(
            &host->loop, &host->results.writer,
            hermas2_result_file_lookup, &host->results) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_saga(
            &host->loop, &host->compensation.writer,
            hermas2_compensation_file_lookup,
            &host->compensation, &host->saga_log.writer) !=
            HERMAS2_LOOP_OK) {
        hermas2_host_close(host);
        return HERMAS2_HOST_STATE_ERROR;
    }
    result = restore_startup_state(
        host, config, &journal_summary, &saga_summary);
    if (result != HERMAS2_HOST_OK) {
        hermas2_host_close(host);
        return result;
    }
    if (hermas2_control_server_init(
            &host->control, &host->loop) !=
            HERMAS2_CONTROL_SERVER_OK) {
        hermas2_host_close(host);
        return HERMAS2_HOST_CONTROL_ERROR;
    }
    host->control_initialized = true;
    result = create_listener(
        config->app_socket_path, &host->app_listener,
        host->app_socket_path, &host->app_socket_bound);
    if (result == HERMAS2_HOST_OK) {
        result = create_listener(
            config->control_socket_path, &host->control_listener,
            host->control_socket_path, &host->control_socket_bound);
    }
    if (result != HERMAS2_HOST_OK) {
        hermas2_host_close(host);
    }
    return result;
}

static bool all_actions_registered(const hermas2_host *host) {
    for (size_t index = 0u;
         index < host->registry.action_count; ++index) {
        if (host->registry.actions[index].file_descriptor < 0) {
            return false;
        }
    }
    return true;
}

static hermas2_host_result reap_recovered(
    hermas2_host *host,
    size_t *progress_count) {
    uint8_t index = 0u;
    while (index < host->recovered_execution_count) {
        uint64_t execution_id =
            host->recovered_execution_ids[index];
        hermas2_frame result;
        hermas2_loop_result available =
            hermas2_daemon_loop_result(
                &host->loop, execution_id, &result);
        if (available == HERMAS2_LOOP_EXECUTION_ACTIVE) {
            ++index;
            continue;
        }
        if (available != HERMAS2_LOOP_OK ||
            hermas2_daemon_loop_release(
                &host->loop, execution_id) != HERMAS2_LOOP_OK) {
            return HERMAS2_HOST_STATE_ERROR;
        }
        --host->recovered_execution_count;
        host->recovered_execution_ids[index] =
            host->recovered_execution_ids[
                host->recovered_execution_count];
        host->recovered_execution_ids[
            host->recovered_execution_count] = 0u;
        ++*progress_count;
    }
    return HERMAS2_HOST_OK;
}

static hermas2_host_result advance_servers(
    hermas2_host *host,
    size_t *progress_count) {
    size_t progressed = 0u;
    if (hermas2_registration_server_step(
            &host->registration, 0, &progressed) !=
            HERMAS2_REGISTRATION_SERVER_OK) {
        return HERMAS2_HOST_REGISTRATION_ERROR;
    }
    *progress_count += progressed;
    if (!all_actions_registered(host)) {
        return HERMAS2_HOST_OK;
    }
    progressed = 0u;
    if (hermas2_control_server_step(
            &host->control, 0, &progressed) !=
            HERMAS2_CONTROL_SERVER_OK) {
        return HERMAS2_HOST_CONTROL_ERROR;
    }
    host->next_execution_id = host->loop.minimum_execution_id;
    *progress_count += progressed;
    return reap_recovered(host, progress_count);
}

hermas2_host_result hermas2_host_step(
    hermas2_host *host,
    int timeout_milliseconds,
    size_t *progress_count) {
    if (host == NULL || progress_count == NULL ||
        timeout_milliseconds < -1 || !host->control_initialized ||
        !host->registration_initialized ||
        host->app_listener < 0 || host->control_listener < 0) {
        return HERMAS2_HOST_INVALID_ARGUMENT;
    }
    *progress_count = 0u;
    hermas2_host_result advanced =
        advance_servers(host, progress_count);
    if (advanced != HERMAS2_HOST_OK) {
        return advanced;
    }
    int wait = *progress_count == 0u ? timeout_milliseconds : 0;
    bool active =
        hermas2_registration_server_pending(&host->registration) !=
            0u ||
        hermas2_control_server_active(&host->control) != 0u ||
        hermas2_daemon_loop_active(&host->loop) != 0u;
    if (active &&
        (wait < 0 || wait > HERMAS2_CONTROL_ACTIVE_QUANTUM_MS)) {
        wait = HERMAS2_CONTROL_ACTIVE_QUANTUM_MS;
    }
    bool ready = all_actions_registered(host);
    struct pollfd listeners[2] = {
        {.fd = host->app_listener, .events = POLLIN},
        {
            .fd = ready ? host->control_listener : -1,
            .events = POLLIN
        }
    };
    int polled = poll(listeners, 2u, wait);
    if (polled < 0) {
        return errno == EINTR ? HERMAS2_HOST_OK
                             : HERMAS2_HOST_POLL_ERROR;
    }
    if ((listeners[0].revents & POLLIN) != 0) {
        hermas2_registration_server_result accepted =
            hermas2_registration_server_accept(
                &host->registration, host->app_listener);
        if (accepted != HERMAS2_REGISTRATION_SERVER_OK &&
            accepted !=
                HERMAS2_REGISTRATION_SERVER_CAPACITY_EXHAUSTED) {
            return HERMAS2_HOST_REGISTRATION_ERROR;
        }
        ++*progress_count;
    }
    if (ready && (listeners[1].revents & POLLIN) != 0) {
        hermas2_control_server_result accepted =
            hermas2_control_server_accept(
                &host->control, host->control_listener);
        if (accepted != HERMAS2_CONTROL_SERVER_OK &&
            accepted != HERMAS2_CONTROL_SERVER_CAPACITY_EXHAUSTED) {
            return HERMAS2_HOST_CONTROL_ERROR;
        }
        ++*progress_count;
    }
    if ((listeners[0].revents &
         (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
        (listeners[1].revents &
         (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return HERMAS2_HOST_POLL_ERROR;
    }
    return advance_servers(host, progress_count);
}

void hermas2_host_close(hermas2_host *host) {
    if (host == NULL) {
        return;
    }
    if (host->control_initialized) {
        hermas2_control_server_close(&host->control);
    }
    if (host->registration_initialized) {
        hermas2_registration_server_close(&host->registration);
    }
    if (host->registry_initialized) {
        hermas2_daemon_registry_close(&host->registry);
    }
    if (host->app_listener >= 0) {
        close(host->app_listener);
    }
    if (host->control_listener >= 0) {
        close(host->control_listener);
    }
    if (host->app_socket_bound) {
        (void)unlink(host->app_socket_path);
    }
    if (host->control_socket_bound) {
        (void)unlink(host->control_socket_path);
    }
    if (host->saga_log_open) {
        hermas2_saga_log_file_close(&host->saga_log);
    }
    if (host->compensation_open) {
        hermas2_compensation_file_close(&host->compensation);
    }
    if (host->result_open) {
        hermas2_result_file_close(&host->results);
    }
    if (host->journal_open) {
        hermas2_journal_file_close(&host->journal);
    }
    if (host->image != NULL) {
        (void)munmap((void *)host->image, host->image_size);
    }
    if (host->image_descriptor >= 0) {
        close(host->image_descriptor);
    }
    initialize_empty(host);
}

const char *hermas2_host_result_name(hermas2_host_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "image-error", "state-error",
        "recovery-required", "socket-error", "registration-error",
        "control-error", "poll-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
