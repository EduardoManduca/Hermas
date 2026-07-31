#define _GNU_SOURCE

#include "hermas/host_linux.h"

#include "hermas/image.h"
#include "hermas/saga_linux.h"

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

typedef struct hermas_mapped_image {
    int descriptor;
    const uint8_t *bytes;
    size_t size;
} hermas_mapped_image;

static void initialize_empty(hermas_host *host) {
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

static hermas_host_result load_image(
    hermas_mapped_image *image,
    const char *path) {
    image->descriptor = -1;
    image->bytes = NULL;
    image->size = 0u;
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat status;
    if (descriptor < 0 || fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        (uint64_t)status.st_size > SIZE_MAX ||
        (uint64_t)status.st_size > HERMAS_IMAGE_MAX_SIZE ||
        (status.st_mode & 022u) != 0u) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return HERMAS_HOST_IMAGE_ERROR;
    }
    size_t size = (size_t)status.st_size;
    void *mapping =
        mmap(NULL, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (mapping == MAP_FAILED ||
        hermas_image_validate(mapping, size, NULL) !=
            HERMAS_IMAGE_OK) {
        if (mapping != MAP_FAILED) {
            (void)munmap(mapping, size);
        }
        close(descriptor);
        return HERMAS_HOST_IMAGE_ERROR;
    }
    image->descriptor = descriptor;
    image->bytes = mapping;
    image->size = size;
    return HERMAS_HOST_OK;
}

static void unload_image(hermas_mapped_image *image) {
    if (image->bytes != NULL) {
        (void)munmap((void *)image->bytes, image->size);
    }
    if (image->descriptor >= 0) {
        close(image->descriptor);
    }
    image->descriptor = -1;
    image->bytes = NULL;
    image->size = 0u;
}

hermas_host_result hermas_host_check_image(const char *image_path) {
    if (!valid_text(image_path)) {
        return HERMAS_HOST_INVALID_ARGUMENT;
    }
    hermas_mapped_image image;
    hermas_host_result loaded = load_image(&image, image_path);
    if (loaded != HERMAS_HOST_OK) {
        return loaded;
    }
    hermas_loop_result checked =
        hermas_daemon_image_check(image.bytes, image.size);
    unload_image(&image);
    if (checked == HERMAS_LOOP_OK) {
        return HERMAS_HOST_OK;
    }
    return checked == HERMAS_LOOP_UNSUPPORTED_GRAPH
               ? HERMAS_HOST_UNSUPPORTED_GRAPH
               : HERMAS_HOST_IMAGE_ERROR;
}

static hermas_host_result validate_state_directory(const char *path) {
    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        return HERMAS_HOST_STATE_ERROR;
    }
    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != geteuid() ||
        (status.st_mode & 077u) != 0u ||
        access(path, R_OK | W_OK | X_OK) != 0) {
        return HERMAS_HOST_STATE_ERROR;
    }
    return HERMAS_HOST_OK;
}

static bool state_path(
    char destination[PATH_MAX],
    const char *directory,
    const char *name) {
    int length = snprintf(
        destination, PATH_MAX, "%s/%s", directory, name);
    return length > 0 && length < PATH_MAX;
}

static hermas_host_result open_state(
    hermas_host *host,
    const hermas_host_config *config,
    hermas_journal_summary *journal_summary,
    hermas_saga_log_summary *saga_summary) {
    if (validate_state_directory(config->state_directory) !=
        HERMAS_HOST_OK) {
        return HERMAS_HOST_STATE_ERROR;
    }
    char journal_path[PATH_MAX];
    char result_path[PATH_MAX];
    char compensation_path[PATH_MAX];
    char saga_path[PATH_MAX];
    if (!state_path(
            journal_path, config->state_directory, "journal.hj") ||
        !state_path(
            result_path, config->state_directory, "results.hr") ||
        !state_path(
            compensation_path, config->state_directory,
            "compensation.hc") ||
        !state_path(
            saga_path, config->state_directory, "saga.hs")) {
        return HERMAS_HOST_STATE_ERROR;
    }
    hermas_result_summary result_summary;
    hermas_compensation_summary compensation_summary;
    if (hermas_journal_file_open(
            &host->journal, journal_path, journal_summary) !=
            HERMAS_JOURNAL_OK) {
        return HERMAS_HOST_STATE_ERROR;
    }
    host->journal_open = true;
    if (hermas_result_file_open(
            &host->results, result_path, &result_summary) !=
            HERMAS_RESULT_STORE_OK) {
        return HERMAS_HOST_STATE_ERROR;
    }
    host->result_open = true;
    if (hermas_compensation_file_open(
            &host->compensation, compensation_path,
            &compensation_summary) != HERMAS_COMPENSATION_OK) {
        return HERMAS_HOST_STATE_ERROR;
    }
    host->compensation_open = true;
    if (hermas_saga_log_file_open(
            &host->saga_log, saga_path, saga_summary) !=
            HERMAS_SAGA_LOG_OK) {
        return HERMAS_HOST_STATE_ERROR;
    }
    host->saga_log_open = true;
    host->next_execution_id = journal_summary->next_execution_id;
    return HERMAS_HOST_OK;
}

static hermas_host_result restore_startup_state(
    hermas_host *host,
    const hermas_host_config *config,
    const hermas_journal_summary *journal_summary,
    const hermas_saga_log_summary *saga_summary) {
    size_t closed_count = 0u;
    if (hermas_journal_file_close_interrupted(
            &host->journal, journal_summary, &closed_count) !=
            HERMAS_JOURNAL_OK) {
        return HERMAS_HOST_STATE_ERROR;
    }
    (void)closed_count;
    for (uint8_t index = 0u;
         index < saga_summary->active_count; ++index) {
        const hermas_saga_log_active *active =
            &saga_summary->active[index];
        if (active->workflow_id != config->workflow_id ||
            active->image_fingerprint !=
                host->loop.image_fingerprint) {
            return HERMAS_HOST_STATE_ERROR;
        }
        hermas_saga_execution recovered;
        hermas_saga_result result = hermas_saga_recover_files(
            &recovered, host->image, host->image_size,
            &host->journal, &host->compensation,
            &host->saga_log, active->execution_id,
            config->workflow_id);
        if (result == HERMAS_SAGA_UNSAFE_HISTORY) {
            return HERMAS_HOST_RECOVERY_REQUIRED;
        }
        if (result != HERMAS_SAGA_OK ||
            recovered.state != HERMAS_SAGA_READY ||
            recovered.remaining == 0u ||
            hermas_daemon_loop_resume_saga(
                &host->loop, &recovered) != HERMAS_LOOP_OK) {
            return HERMAS_HOST_STATE_ERROR;
        }
        host->recovered_execution_ids[
            host->recovered_execution_count++] =
                active->execution_id;
    }
    return HERMAS_HOST_OK;
}

static hermas_host_result create_listener(
    const char *path,
    int *listener,
    char stored_path[sizeof(((struct sockaddr_un *)0)->sun_path)],
    bool *bound) {
    size_t path_length = strlen(path);
    if (path_length == 0u ||
        path_length >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        return HERMAS_HOST_SOCKET_ERROR;
    }
    int descriptor = socket(
        AF_UNIX,
        SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0);
    if (descriptor < 0) {
        return HERMAS_HOST_SOCKET_ERROR;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, path_length + 1u);
    if (bind(
            descriptor, (const struct sockaddr *)&address,
            sizeof(address)) != 0) {
        close(descriptor);
        return HERMAS_HOST_SOCKET_ERROR;
    }
    if (chmod(path, 0600) != 0 ||
        listen(descriptor, HERMAS_DAEMON_MAX_ACTIONS) != 0) {
        close(descriptor);
        (void)unlink(path);
        return HERMAS_HOST_SOCKET_ERROR;
    }
    memcpy(stored_path, path, path_length + 1u);
    *listener = descriptor;
    *bound = true;
    return HERMAS_HOST_OK;
}

hermas_host_result hermas_host_open(
    hermas_host *host,
    const hermas_host_config *config) {
    if (host == NULL || config == NULL ||
        !valid_text(config->image_path) ||
        !valid_text(config->state_directory) ||
        !valid_text(config->app_socket_path) ||
        !valid_text(config->control_socket_path) ||
        config->workflow_id == 0u ||
        strcmp(
            config->app_socket_path,
            config->control_socket_path) == 0) {
        return HERMAS_HOST_INVALID_ARGUMENT;
    }
    initialize_empty(host);
    hermas_mapped_image image;
    hermas_host_result result = load_image(&image, config->image_path);
    if (result != HERMAS_HOST_OK) {
        hermas_host_close(host);
        return result;
    }
    host->image_descriptor = image.descriptor;
    host->image = image.bytes;
    host->image_size = image.size;
    if (hermas_daemon_registry_init(
            &host->registry, host->image, host->image_size) !=
            HERMAS_DAEMON_OK) {
        hermas_host_close(host);
        return HERMAS_HOST_IMAGE_ERROR;
    }
    host->registry_initialized = true;
    if (hermas_registration_server_init(
            &host->registration, &host->registry) !=
            HERMAS_REGISTRATION_SERVER_OK) {
        hermas_host_close(host);
        return HERMAS_HOST_REGISTRATION_ERROR;
    }
    host->registration_initialized = true;
    hermas_loop_result initialized = hermas_daemon_loop_init(
        &host->loop, &host->registry,
        host->image, host->image_size);
    if (initialized != HERMAS_LOOP_OK) {
        hermas_host_close(host);
        return initialized == HERMAS_LOOP_UNSUPPORTED_GRAPH
            ? HERMAS_HOST_UNSUPPORTED_GRAPH
            : HERMAS_HOST_IMAGE_ERROR;
    }
    hermas_journal_summary journal_summary;
    hermas_saga_log_summary saga_summary;
    result = open_state(
        host, config, &journal_summary, &saga_summary);
    if (result != HERMAS_HOST_OK) {
        hermas_host_close(host);
        return result;
    }
    if (hermas_daemon_loop_attach_journal(
            &host->loop, &host->journal.writer,
            config->workflow_id) != HERMAS_LOOP_OK ||
        hermas_daemon_loop_set_execution_floor(
            &host->loop, host->next_execution_id) != HERMAS_LOOP_OK ||
        hermas_daemon_loop_attach_results(
            &host->loop, &host->results.writer,
            hermas_result_file_lookup, &host->results) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_attach_saga(
            &host->loop, &host->compensation.writer,
            hermas_compensation_file_lookup,
            &host->compensation, &host->saga_log.writer) !=
            HERMAS_LOOP_OK) {
        hermas_host_close(host);
        return HERMAS_HOST_STATE_ERROR;
    }
    result = restore_startup_state(
        host, config, &journal_summary, &saga_summary);
    if (result != HERMAS_HOST_OK) {
        hermas_host_close(host);
        return result;
    }
    if (hermas_control_server_init(
            &host->control, &host->loop) !=
            HERMAS_CONTROL_SERVER_OK) {
        hermas_host_close(host);
        return HERMAS_HOST_CONTROL_ERROR;
    }
    host->control_initialized = true;
    result = create_listener(
        config->app_socket_path, &host->app_listener,
        host->app_socket_path, &host->app_socket_bound);
    if (result == HERMAS_HOST_OK) {
        result = create_listener(
            config->control_socket_path, &host->control_listener,
            host->control_socket_path, &host->control_socket_bound);
    }
    if (result != HERMAS_HOST_OK) {
        hermas_host_close(host);
    }
    return result;
}

static bool all_actions_registered(const hermas_host *host) {
    for (size_t index = 0u;
         index < host->registry.action_count; ++index) {
        if (host->registry.actions[index].file_descriptor < 0) {
            return false;
        }
    }
    return true;
}

static hermas_host_result reap_recovered(
    hermas_host *host,
    size_t *progress_count) {
    uint8_t index = 0u;
    while (index < host->recovered_execution_count) {
        uint64_t execution_id =
            host->recovered_execution_ids[index];
        hermas_frame result;
        hermas_loop_result available =
            hermas_daemon_loop_result(
                &host->loop, execution_id, &result);
        if (available == HERMAS_LOOP_EXECUTION_ACTIVE) {
            ++index;
            continue;
        }
        if (available != HERMAS_LOOP_OK ||
            hermas_daemon_loop_release(
                &host->loop, execution_id) != HERMAS_LOOP_OK) {
            return HERMAS_HOST_STATE_ERROR;
        }
        --host->recovered_execution_count;
        host->recovered_execution_ids[index] =
            host->recovered_execution_ids[
                host->recovered_execution_count];
        host->recovered_execution_ids[
            host->recovered_execution_count] = 0u;
        ++*progress_count;
    }
    return HERMAS_HOST_OK;
}

static hermas_host_result advance_servers(
    hermas_host *host,
    size_t *progress_count) {
    size_t progressed = 0u;
    if (hermas_registration_server_step(
            &host->registration, 0, &progressed) !=
            HERMAS_REGISTRATION_SERVER_OK) {
        return HERMAS_HOST_REGISTRATION_ERROR;
    }
    *progress_count += progressed;
    if (!all_actions_registered(host)) {
        return HERMAS_HOST_OK;
    }
    progressed = 0u;
    if (hermas_control_server_step(
            &host->control, 0, &progressed) !=
            HERMAS_CONTROL_SERVER_OK) {
        return HERMAS_HOST_CONTROL_ERROR;
    }
    host->next_execution_id = host->loop.minimum_execution_id;
    *progress_count += progressed;
    return reap_recovered(host, progress_count);
}

hermas_host_result hermas_host_step(
    hermas_host *host,
    int timeout_milliseconds,
    size_t *progress_count) {
    if (host == NULL || progress_count == NULL ||
        timeout_milliseconds < -1 || !host->control_initialized ||
        !host->registration_initialized ||
        host->app_listener < 0 || host->control_listener < 0) {
        return HERMAS_HOST_INVALID_ARGUMENT;
    }
    *progress_count = 0u;
    hermas_host_result advanced =
        advance_servers(host, progress_count);
    if (advanced != HERMAS_HOST_OK) {
        return advanced;
    }
    int wait = *progress_count == 0u ? timeout_milliseconds : 0;
    bool active =
        hermas_registration_server_pending(&host->registration) !=
            0u ||
        hermas_control_server_active(&host->control) != 0u ||
        hermas_daemon_loop_active(&host->loop) != 0u;
    if (active &&
        (wait < 0 || wait > HERMAS_CONTROL_ACTIVE_QUANTUM_MS)) {
        wait = HERMAS_CONTROL_ACTIVE_QUANTUM_MS;
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
        return errno == EINTR ? HERMAS_HOST_OK
                             : HERMAS_HOST_POLL_ERROR;
    }
    if ((listeners[0].revents & POLLIN) != 0) {
        hermas_registration_server_result accepted =
            hermas_registration_server_accept(
                &host->registration, host->app_listener);
        if (accepted != HERMAS_REGISTRATION_SERVER_OK &&
            accepted !=
                HERMAS_REGISTRATION_SERVER_CAPACITY_EXHAUSTED) {
            return HERMAS_HOST_REGISTRATION_ERROR;
        }
        ++*progress_count;
    }
    if (ready && (listeners[1].revents & POLLIN) != 0) {
        hermas_control_server_result accepted =
            hermas_control_server_accept(
                &host->control, host->control_listener);
        if (accepted != HERMAS_CONTROL_SERVER_OK &&
            accepted != HERMAS_CONTROL_SERVER_CAPACITY_EXHAUSTED) {
            return HERMAS_HOST_CONTROL_ERROR;
        }
        ++*progress_count;
    }
    if ((listeners[0].revents &
         (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
        (listeners[1].revents &
         (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return HERMAS_HOST_POLL_ERROR;
    }
    return advance_servers(host, progress_count);
}

void hermas_host_close(hermas_host *host) {
    if (host == NULL) {
        return;
    }
    if (host->control_initialized) {
        hermas_control_server_close(&host->control);
    }
    if (host->registration_initialized) {
        hermas_registration_server_close(&host->registration);
    }
    if (host->registry_initialized) {
        hermas_daemon_registry_close(&host->registry);
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
        hermas_saga_log_file_close(&host->saga_log);
    }
    if (host->compensation_open) {
        hermas_compensation_file_close(&host->compensation);
    }
    if (host->result_open) {
        hermas_result_file_close(&host->results);
    }
    if (host->journal_open) {
        hermas_journal_file_close(&host->journal);
    }
    if (host->image != NULL) {
        (void)munmap((void *)host->image, host->image_size);
    }
    if (host->image_descriptor >= 0) {
        close(host->image_descriptor);
    }
    initialize_empty(host);
}

const char *hermas_host_result_name(hermas_host_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "image-error", "state-error",
        "recovery-required", "socket-error", "registration-error",
        "control-error", "poll-error", "unsupported-graph"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
