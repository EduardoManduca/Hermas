#define _POSIX_C_SOURCE 200809L

#include "hermas/host_linux.h"
#include "hermas/image.h"
#include "hermas/version.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf(
            "Hermas %s (hermasd; graph-image %u, protocol %u, "
            "journal %u, result %u, compensation %u, saga-log %u)\n",
            HERMAS_VERSION, HERMAS_GRAPH_IMAGE_VERSION,
            HERMAS_PROTOCOL_VERSION, HERMAS_JOURNAL_VERSION,
            HERMAS_RESULT_VERSION, HERMAS_COMPENSATION_VERSION,
            HERMAS_SAGA_LOG_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts(
            "usage: hermasd IMAGE WORKFLOW_ID STATE_DIR "
            "APP_SOCKET CONTROL_SOCKET\n\n"
            "Run one verified graph image with private durable state.");
        return 0;
    }
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
        fputs("hermasd: invalid workflow ID\n", stderr);
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
        fputs("hermasd: cannot install signal handlers\n", stderr);
        return 1;
    }
    hermas_host *host = calloc(1u, sizeof(*host));
    if (host == NULL) {
        fputs("hermasd: cannot allocate bounded host\n", stderr);
        return 1;
    }
    hermas_host_config config = {
        .image_path = argv[1],
        .workflow_id = workflow_id,
        .state_directory = argv[3],
        .app_socket_path = argv[4],
        .control_socket_path = argv[5]
    };
    hermas_host_result result = hermas_host_open(host, &config);
    if (result != HERMAS_HOST_OK) {
        fprintf(
            stderr, "hermasd: startup failed: %s\n",
            hermas_host_result_name(result));
        free(host);
        return result == HERMAS_HOST_RECOVERY_REQUIRED ? 3 : 1;
    }
    while (!stop_requested) {
        size_t progress = 0u;
        result = hermas_host_step(host, -1, &progress);
        if (result != HERMAS_HOST_OK) {
            fprintf(
                stderr, "hermasd: runtime failed: %s\n",
                hermas_host_result_name(result));
            break;
        }
    }
    hermas_host_close(host);
    free(host);
    return result == HERMAS_HOST_OK ? 0 : 1;
}
