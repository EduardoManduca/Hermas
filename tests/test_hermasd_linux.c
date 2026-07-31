#define _POSIX_C_SOURCE 200809L

#include "hermas/client.h"
#include "hermas/host_linux.h"
#include "hermas/journal_linux.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_hermasd: %s\n", message);
    return 1;
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static int write_all(int descriptor, const uint8_t *bytes, size_t size) {
    size_t written = 0u;
    while (written < size) {
        ssize_t result = write(
            descriptor, bytes + written, size - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return 0;
        }
        written += (size_t)result;
    }
    return 1;
}

static pid_t spawn_daemon(
    const char *daemon,
    const char *image,
    const char *state,
    const char *app_socket,
    const char *control_socket) {
    pid_t child = fork();
    if (child == 0) {
        execl(
            daemon, daemon, image, "7", state,
            app_socket, control_socket, (char *)NULL);
        _exit(127);
    }
    return child;
}

static pid_t spawn_app(
    const char *executable,
    const char *socket_path,
    const char *image) {
    pid_t child = fork();
    if (child == 0) {
        execl(
            executable, executable, socket_path, image,
            (char *)NULL);
        _exit(127);
    }
    return child;
}

static int wait_for_socket(const char *path, pid_t daemon) {
    struct timespec pause = {
        .tv_sec = 0,
        .tv_nsec = 10000000L
    };
    for (size_t attempt = 0u; attempt < 500u; ++attempt) {
        struct stat status;
        if (lstat(path, &status) == 0 &&
            S_ISSOCK(status.st_mode)) {
            return 1;
        }
        int child_status = 0;
        if (waitpid(daemon, &child_status, WNOHANG) == daemon) {
            return 0;
        }
        (void)nanosleep(&pause, NULL);
    }
    return 0;
}

static int wait_success(pid_t child) {
    int status = 0;
    return child > 0 && waitpid(child, &status, 0) == child &&
           WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int run_cli(
    const char *runner,
    const char *control_socket,
    uint64_t execution_id,
    uint16_t input_type) {
    int output[2];
    if (pipe(output) != 0) {
        return 0;
    }
    char execution_text[32];
    char type_text[16];
    (void)snprintf(
        execution_text, sizeof(execution_text), "%llu",
        (unsigned long long)execution_id);
    (void)snprintf(
        type_text, sizeof(type_text), "%u", (unsigned)input_type);
    pid_t child = fork();
    if (child == 0) {
        close(output[0]);
        if (dup2(output[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(output[1]);
        execl(
            runner, runner, control_socket,
            execution_text, type_text, (char *)NULL);
        _exit(127);
    }
    close(output[1]);
    char text[512];
    size_t length = 0u;
    while (length + 1u < sizeof(text)) {
        ssize_t received = read(
            output[0], text + length,
            sizeof(text) - length - 1u);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            break;
        }
        length += (size_t)received;
    }
    close(output[0]);
    text[length] = '\0';
    char expected[64];
    (void)snprintf(
        expected, sizeof(expected), "execution=%llu outcome=success",
        (unsigned long long)execution_id);
    return wait_success(child) &&
           strstr(text, expected) != NULL &&
           strstr(text, "source_type=11") != NULL &&
           strstr(text, "value=01") != NULL;
}

static int run_cycle(
    const char *daemon_path,
    const char *runner_path,
    const char *image_path,
    const char *state_path,
    const char *app_socket,
    const char *control_socket,
    const char *const app_paths[3],
    uint16_t input_type,
    uint64_t execution_id,
    int use_runner) {
    pid_t daemon = spawn_daemon(
        daemon_path, image_path, state_path,
        app_socket, control_socket);
    if (daemon <= 0 || !wait_for_socket(app_socket, daemon) ||
        !wait_for_socket(control_socket, daemon)) {
        return 0;
    }
    hermas_client client = {.file_descriptor = -1};
    if (!use_runner &&
        hermas_client_connect(&client, control_socket) !=
            HERMAS_CLIENT_OK) {
        (void)kill(daemon, SIGTERM);
        (void)waitpid(daemon, NULL, 0);
        return 0;
    }
    pid_t apps[3];
    for (size_t index = 0u; index < 3u; ++index) {
        apps[index] = spawn_app(
            app_paths[index], app_socket, image_path);
        if (apps[index] <= 0) {
            hermas_client_close(&client);
            (void)kill(daemon, SIGTERM);
            (void)waitpid(daemon, NULL, 0);
            return 0;
        }
    }
    int succeeded = 0;
    if (use_runner) {
        succeeded = run_cli(
            runner_path, control_socket, execution_id, input_type);
    } else {
        uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
        hermas_frame result;
        succeeded =
            hermas_client_execute(
                &client, execution_id, input_type, NULL, 0u,
                packet, sizeof(packet), &result) ==
                HERMAS_CLIENT_OK &&
            result.outcome == HERMAS_OUTCOME_SUCCESS &&
            result.source_type == 11u &&
            result.payload_length == 1u &&
            result.payload[0] == 1u;
        hermas_client_close(&client);
    }
    for (size_t index = 0u; index < 3u; ++index) {
        succeeded = wait_success(apps[index]) && succeeded;
    }
    if (kill(daemon, SIGTERM) != 0 || !wait_success(daemon)) {
        succeeded = 0;
    }
    if (access(app_socket, F_OK) == 0 ||
        access(control_socket, F_OK) == 0) {
        succeeded = 0;
    }
    return succeeded;
}

static void remove_state(const char *directory) {
    static const char *const files[] = {
        "journal.hj", "results.hr",
        "compensation.hc", "saga.hs"
    };
    char path[512];
    for (size_t index = 0u;
         index < sizeof(files) / sizeof(files[0]); ++index) {
        int length = snprintf(
            path, sizeof(path), "%s/%s", directory, files[index]);
        if (length > 0 && (size_t)length < sizeof(path)) {
            (void)unlink(path);
        }
    }
    (void)rmdir(directory);
}

static int run_unsupported_command(
    const char *daemon,
    const char *image,
    const char *workspace) {
    int errors[2];
    if (pipe(errors) != 0) {
        return 0;
    }
    pid_t child = fork();
    if (child == 0) {
        close(errors[0]);
        if (dup2(errors[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(errors[1]);
        if (workspace == NULL) {
            execl(
                daemon, daemon, "--check-image", image,
                (char *)NULL);
        } else {
            execl(
                daemon, daemon, "--workspace", workspace,
                image, "7", (char *)NULL);
        }
        _exit(127);
    }
    close(errors[1]);
    char text[512];
    size_t length = 0u;
    while (length + 1u < sizeof(text)) {
        ssize_t received = read(
            errors[0], text + length,
            sizeof(text) - length - 1u);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            break;
        }
        length += (size_t)received;
    }
    close(errors[0]);
    text[length] = '\0';
    int status = 0;
    return waitpid(child, &status, 0) == child &&
           WIFEXITED(status) && WEXITSTATUS(status) == 4 &&
           strstr(text, "unsupported-graph") != NULL;
}

static int test_unsupported_graph(
    const char *daemon,
    const char *image_path) {
    size_t image_size = 0u;
    uint8_t *image = read_file(image_path, &image_size);
    char secure_image[] = "/tmp/hermasd-unsupported-image-XXXXXX";
    int descriptor = mkstemp(secure_image);
    int prepared = 0;
    if (image != NULL && descriptor >= 0) {
        int written = write_all(descriptor, image, image_size);
        int closed = close(descriptor) == 0;
        descriptor = -1;
        prepared =
            written && closed &&
            chmod(secure_image, 0600) == 0;
    }
    free(image);
    if (!prepared) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        (void)unlink(secure_image);
        return 0;
    }
    char state[] = "/tmp/hermasd-unsupported-state-XXXXXX";
    if (mkdtemp(state) == NULL) {
        (void)unlink(secure_image);
        return 0;
    }
    char app_socket[512];
    char control_socket[512];
    (void)snprintf(
        app_socket, sizeof(app_socket), "%s/apps.sock", state);
    (void)snprintf(
        control_socket, sizeof(control_socket),
        "%s/control.sock", state);
    hermas_host *host = calloc(1u, sizeof(*host));
    if (host == NULL) {
        (void)rmdir(state);
        (void)unlink(secure_image);
        return 0;
    }
    hermas_host_result result = hermas_host_open(
        host,
        &(hermas_host_config){
            .image_path = secure_image,
            .state_directory = state,
            .app_socket_path = app_socket,
            .control_socket_path = control_socket,
            .workflow_id = 7u,
        });
    int rejected =
        result == HERMAS_HOST_UNSUPPORTED_GRAPH &&
        strcmp(
            hermas_host_result_name(result),
            "unsupported-graph") == 0;
    hermas_host_close(host);
    free(host);
    char workspace[] = "/tmp/hermasd-unbound-workspace-XXXXXX";
    int workspace_ready =
        mkdtemp(workspace) != NULL && rmdir(workspace) == 0;
    rejected =
        rejected &&
        hermas_host_check_image(secure_image) ==
            HERMAS_HOST_UNSUPPORTED_GRAPH &&
        run_unsupported_command(daemon, secure_image, NULL) &&
        workspace_ready &&
        run_unsupported_command(
            daemon, secure_image, workspace) &&
        access(workspace, F_OK) != 0 && errno == ENOENT;
    (void)rmdir(state);
    (void)unlink(secure_image);
    return rejected;
}

int main(int argc, char **argv) {
    if (argc != 8) {
        return fail(
            "expected image, daemon, runner, three apps, and parallel image");
    }
    size_t image_size = 0u;
    uint8_t *image = read_file(argv[1], &image_size);
    if (image == NULL || image_size < 72u) {
        free(image);
        return fail("cannot load graph image");
    }
    uint32_t apps_offset = read_u32(image, 40u);
    uint16_t action_contract_count = read_u16(image, 28u);
    uint16_t input_type = read_u16(image, 22u);
    if (action_contract_count != 3u || input_type == 0u ||
        apps_offset > image_size ||
        image_size - apps_offset < 3u * 36u) {
        free(image);
        return fail("unexpected grade graph layout");
    }
    const char *app_paths[3] = {argv[4], argv[5], argv[6]};
    if (!test_unsupported_graph(argv[2], argv[7])) {
        free(image);
        return fail("bounded-flow graph did not report unsupported-graph");
    }
    char secure_image[] = "/tmp/hermasd-image-XXXXXX";
    int secure_descriptor = mkstemp(secure_image);
    int image_written =
        secure_descriptor >= 0 &&
        write_all(secure_descriptor, image, image_size);
    int image_closed =
        secure_descriptor >= 0 && close(secure_descriptor) == 0;
    secure_descriptor = -1;
    if (!image_written || !image_closed ||
        chmod(secure_image, 0600) != 0) {
        if (secure_descriptor >= 0) {
            close(secure_descriptor);
        }
        unlink(secure_image);
        free(image);
        return fail("cannot create secure image fixture");
    }
    char state[] = "/tmp/hermasd-state-XXXXXX";
    if (mkdtemp(state) == NULL) {
        unlink(secure_image);
        free(image);
        return fail("cannot create state directory");
    }
    char app_socket[512];
    char control_socket[512];
    (void)snprintf(
        app_socket, sizeof(app_socket), "%s/apps.sock", state);
    (void)snprintf(
        control_socket, sizeof(control_socket),
        "%s/control.sock", state);
    if (!run_cycle(
            argv[2], argv[3], secure_image, state,
            app_socket, control_socket, app_paths,
            input_type, 1u, 0) ||
        !run_cycle(
            argv[2], argv[3], secure_image, state,
            app_socket, control_socket, app_paths,
            input_type, 2u, 1)) {
        remove_state(state);
        unlink(secure_image);
        free(image);
        return fail("runnable daemon cycle failed");
    }

    char recovery[] = "/tmp/hermasd-recovery-XXXXXX";
    if (mkdtemp(recovery) == NULL) {
        remove_state(state);
        unlink(secure_image);
        free(image);
        return fail("cannot create recovery fixture");
    }
    char journal_path[512];
    (void)snprintf(
        journal_path, sizeof(journal_path),
        "%s/journal.hj", recovery);
    hermas_journal_file journal;
    hermas_journal_summary summary;
    hermas_journal_record started = {
        .kind = HERMAS_JOURNAL_EXECUTION_STARTED,
        .outcome = HERMAS_OUTCOME_NONE,
        .execution_id = 9u,
        .workflow_id = 7u,
        .image_fingerprint =
            hermas_journal_image_fingerprint(image, image_size)
    };
    if (hermas_journal_file_open(
            &journal, journal_path, &summary) != HERMAS_JOURNAL_OK ||
        hermas_journal_writer_append(
            &journal.writer, started) != HERMAS_JOURNAL_OK) {
        remove_state(recovery);
        remove_state(state);
        unlink(secure_image);
        free(image);
        return fail("cannot create interrupted history");
    }
    hermas_journal_file_close(&journal);
    (void)snprintf(
        app_socket, sizeof(app_socket), "%s/apps.sock", recovery);
    (void)snprintf(
        control_socket, sizeof(control_socket),
        "%s/control.sock", recovery);
    pid_t recovered_forward = spawn_daemon(
        argv[2], secure_image, recovery, app_socket, control_socket);
    if (recovered_forward <= 0 ||
        !wait_for_socket(app_socket, recovered_forward) ||
        !wait_for_socket(control_socket, recovered_forward) ||
        kill(recovered_forward, SIGTERM) != 0 ||
        !wait_success(recovered_forward) ||
        hermas_journal_file_open(
            &journal, journal_path, &summary) !=
            HERMAS_JOURNAL_OK ||
        summary.interrupted_count != 0u ||
        summary.record_count != 2u) {
        remove_state(recovery);
        remove_state(state);
        unlink(secure_image);
        free(image);
        return fail("interrupted forward history was not closed Unknown");
    }
    hermas_journal_file_close(&journal);

    char collision[] = "/tmp/hermasd-collision-XXXXXX";
    if (mkdtemp(collision) == NULL) {
        remove_state(recovery);
        remove_state(state);
        unlink(secure_image);
        free(image);
        return fail("cannot create socket collision fixture");
    }
    (void)snprintf(
        app_socket, sizeof(app_socket), "%s/apps.sock", collision);
    (void)snprintf(
        control_socket, sizeof(control_socket),
        "%s/control.sock", collision);
    FILE *sentinel = fopen(app_socket, "wb");
    if (sentinel == NULL || fputs("owned", sentinel) == EOF ||
        fclose(sentinel) != 0) {
        unlink(app_socket);
        remove_state(collision);
        remove_state(recovery);
        remove_state(state);
        unlink(secure_image);
        free(image);
        return fail("cannot create existing socket-path sentinel");
    }
    pid_t collided = spawn_daemon(
        argv[2], secure_image, collision, app_socket, control_socket);
    int collided_status = 0;
    struct stat sentinel_status;
    if (collided <= 0 ||
        waitpid(collided, &collided_status, 0) != collided ||
        !WIFEXITED(collided_status) ||
        WEXITSTATUS(collided_status) != 1 ||
        lstat(app_socket, &sentinel_status) != 0 ||
        !S_ISREG(sentinel_status.st_mode)) {
        unlink(app_socket);
        remove_state(collision);
        remove_state(recovery);
        remove_state(state);
        unlink(secure_image);
        free(image);
        return fail("existing socket path was replaced or removed");
    }
    unlink(app_socket);
    remove_state(collision);
    remove_state(recovery);
    remove_state(state);
    unlink(secure_image);
    free(image);
    puts("runnable hermasd host tests passed");
    return 0;
}
