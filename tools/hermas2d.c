#define _POSIX_C_SOURCE 200809L

#include "hermas2/host_linux.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t stop_requested = 0;

static void request_stop(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static int parse_workflow_id(const char *text, uint32_t *value) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed == 0ul || parsed > UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(
            stderr,
            "usage: %s IMAGE WORKFLOW_ID STATE_DIR "
            "APP_SOCKET CONTROL_SOCKET\n",
            argv[0]);
        return 2;
    }
    uint32_t workflow_id = 0u;
    if (!parse_workflow_id(argv[2], &workflow_id)) {
        fputs("hermas2d: invalid workflow ID\n", stderr);
        return 2;
    }
    struct sigaction action = {
        .sa_handler = request_stop,
        .sa_flags = 0
    };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        fputs("hermas2d: cannot install signal handlers\n", stderr);
        return 1;
    }
    hermas2_host *host = calloc(1u, sizeof(*host));
    if (host == NULL) {
        fputs("hermas2d: cannot allocate bounded host\n", stderr);
        return 1;
    }
    hermas2_host_config config = {
        .image_path = argv[1],
        .workflow_id = workflow_id,
        .state_directory = argv[3],
        .app_socket_path = argv[4],
        .control_socket_path = argv[5]
    };
    hermas2_host_result result = hermas2_host_open(host, &config);
    if (result != HERMAS2_HOST_OK) {
        fprintf(
            stderr, "hermas2d: startup failed: %s\n",
            hermas2_host_result_name(result));
        free(host);
        return result == HERMAS2_HOST_RECOVERY_REQUIRED ? 3 : 1;
    }
    while (!stop_requested) {
        size_t progress = 0u;
        result = hermas2_host_step(host, -1, &progress);
        if (result != HERMAS2_HOST_OK) {
            fprintf(
                stderr, "hermas2d: runtime failed: %s\n",
                hermas2_host_result_name(result));
            break;
        }
    }
    hermas2_host_close(host);
    free(host);
    return result == HERMAS2_HOST_OK ? 0 : 1;
}
