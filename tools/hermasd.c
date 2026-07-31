#define _POSIX_C_SOURCE 200809L

#include "hermas/host_linux.h"
#include "hermas/image.h"
#include "hermas/version.h"
#include "hermas/workspace_linux.h"

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

static bool daemon_accepts_image(
    const uint8_t *image,
    size_t image_size,
    void *context) {
    (void)context;
    return hermas_daemon_image_check(image, image_size) ==
           HERMAS_LOOP_OK;
}

static int image_check_exit(hermas_host_result result) {
    return result == HERMAS_HOST_UNSUPPORTED_GRAPH ? 4 : 1;
}

static const char *image_check_status(hermas_host_result result) {
    if (result == HERMAS_HOST_OK) {
        return "supported";
    }
    if (result == HERMAS_HOST_UNSUPPORTED_GRAPH) {
        return "unsupported";
    }
    return "invalid";
}

static void print_image_check_json(hermas_host_result result) {
    printf(
        "{\"format\":\"hermas-image-check-v1\","
        "\"status\":\"%s\",\"reason\":\"%s\"}\n",
        image_check_status(result), hermas_host_result_name(result));
}

static const char *feature_boolean(
    hermas_graph_features supported,
    hermas_graph_features feature) {
    return (supported & feature) != 0u ? "true" : "false";
}

static void print_capabilities(void) {
    hermas_graph_features supported =
        hermas_daemon_supported_graph_features();
    printf(
        "{\"format\":\"hermas-daemon-capabilities-v1\","
        "\"hermas_version\":\"%s\","
        "\"graph_image_version\":%u,"
        "\"protocol_version\":%u,"
        "\"formats\":{\"journal\":%u,\"result\":%u,"
        "\"compensation\":%u,\"saga_log\":%u,"
        "\"workspace_manifest\":%u},"
        "\"limits\":{\"actions\":%u,\"active_executions\":%u,"
        "\"active_group_executions\":%u},"
        "\"flows\":{\"action\":%s,\"match\":%s,"
        "\"within\":%s,\"saga\":%s,"
        "\"all\":%s,\"each\":%s}}\n",
        HERMAS_VERSION, HERMAS_GRAPH_IMAGE_VERSION,
        HERMAS_PROTOCOL_VERSION, HERMAS_JOURNAL_VERSION,
        HERMAS_RESULT_VERSION, HERMAS_COMPENSATION_VERSION,
        HERMAS_SAGA_LOG_VERSION, HERMAS_WORKSPACE_MANIFEST_VERSION,
        HERMAS_DAEMON_MAX_ACTIONS,
        HERMAS_DAEMON_MAX_EXECUTIONS,
        HERMAS_DAEMON_MAX_GROUP_EXECUTIONS,
        feature_boolean(supported, HERMAS_GRAPH_FEATURE_ACTION),
        feature_boolean(supported, HERMAS_GRAPH_FEATURE_MATCH),
        feature_boolean(supported, HERMAS_GRAPH_FEATURE_WITHIN),
        feature_boolean(supported, HERMAS_GRAPH_FEATURE_SAGA),
        feature_boolean(supported, HERMAS_GRAPH_FEATURE_ALL),
        feature_boolean(supported, HERMAS_GRAPH_FEATURE_EACH));
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf(
            "Hermas %s (hermasd; graph-image %u, protocol %u, "
            "journal %u, result %u, compensation %u, saga-log %u, "
            "workspace-manifest %u)\n",
            HERMAS_VERSION, HERMAS_GRAPH_IMAGE_VERSION,
            HERMAS_PROTOCOL_VERSION, HERMAS_JOURNAL_VERSION,
            HERMAS_RESULT_VERSION, HERMAS_COMPENSATION_VERSION,
            HERMAS_SAGA_LOG_VERSION,
            HERMAS_WORKSPACE_MANIFEST_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts(
            "usage: hermasd IMAGE WORKFLOW_ID STATE_DIR "
            "APP_SOCKET CONTROL_SOCKET\n"
            "       hermasd --capabilities\n"
            "       hermasd --check-image IMAGE [--json]\n"
            "       hermasd --workspace DIRECTORY IMAGE WORKFLOW_ID\n"
            "       hermasd --workspace DIRECTORY\n\n"
            "--capabilities emits versioned JSON for automation.\n"
            "--check-image verifies file safety, graph format, and daemon "
            "capability without creating state or sockets. --json emits "
            "hermas-image-check-v1.\n\n"
            "The IMAGE form initializes or verifies the managed workspace. "
            "Later starts derive its pinned image and workflow ID.\n\n"
            "Run one verified graph image with private durable state. "
            "This alpha daemon accepts sequential, typed-choice, deadline, "
            "and saga graphs; bounded all/each graphs fail closed.");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--capabilities") == 0) {
        print_capabilities();
        return 0;
    }
    if ((argc == 3 ||
         (argc == 4 && strcmp(argv[3], "--json") == 0)) &&
        strcmp(argv[1], "--check-image") == 0) {
        hermas_host_result checked = hermas_host_check_image(argv[2]);
        if (argc == 4) {
            print_image_check_json(checked);
            return checked == HERMAS_HOST_OK ? 0 : image_check_exit(checked);
        }
        if (checked == HERMAS_HOST_OK) {
            printf("hermasd: supported image: %s\n", argv[2]);
            return 0;
        }
        fprintf(
            stderr, "hermasd: image check failed: %s\n",
            hermas_host_result_name(checked));
        return image_check_exit(checked);
    }
    int workspace_bind_mode =
        argc == 5 && strcmp(argv[1], "--workspace") == 0;
    int workspace_load_mode =
        argc == 3 && strcmp(argv[1], "--workspace") == 0;
    int workspace_mode = workspace_bind_mode || workspace_load_mode;
    if (argc != 6 && !workspace_mode) {
        fprintf(
            stderr,
            "usage: %s IMAGE WORKFLOW_ID STATE_DIR "
            "APP_SOCKET CONTROL_SOCKET\n"
            "       %s --capabilities\n"
            "       %s --check-image IMAGE [--json]\n"
            "       %s --workspace DIRECTORY IMAGE WORKFLOW_ID\n"
            "       %s --workspace DIRECTORY\n",
            argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    uint32_t workflow_id = 0u;
    if (workspace_bind_mode) {
        if (!parse_workflow_id(argv[4], &workflow_id)) {
            fputs("hermasd: invalid workflow ID\n", stderr);
            return 2;
        }
        hermas_host_result checked = hermas_host_check_image(argv[3]);
        if (checked != HERMAS_HOST_OK) {
            fprintf(
                stderr, "hermasd: image check failed: %s\n",
                hermas_host_result_name(checked));
            return image_check_exit(checked);
        }
    }
    hermas_workspace_paths workspace;
    if (workspace_mode) {
        hermas_workspace_result opened =
            hermas_workspace_open(argv[2], true, &workspace);
        if (opened != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "hermasd: workspace error: %s\n",
                hermas_workspace_result_name(opened));
            return 2;
        }
    }
    hermas_workspace_binding binding;
    if (workspace_bind_mode) {
        hermas_workspace_result bound = hermas_workspace_bind_checked(
            &workspace, argv[3], workflow_id,
            daemon_accepts_image, NULL, &binding);
        if (bound != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "hermasd: workspace binding failed: %s\n",
                hermas_workspace_result_name(bound));
            return 2;
        }
    } else if (workspace_load_mode) {
        hermas_workspace_result loaded =
            hermas_workspace_load(&workspace, &binding);
        if (loaded != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "hermasd: workspace binding failed: %s\n",
                hermas_workspace_result_name(loaded));
            return 2;
        }
        workflow_id = binding.workflow_id;
    } else if (!parse_workflow_id(argv[2], &workflow_id)) {
        fputs("hermasd: invalid workflow ID\n", stderr);
        return 2;
    }
    const char *image_path =
        workspace_mode ? workspace.image_path : argv[1];
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
        .image_path = image_path,
        .workflow_id = workflow_id,
        .state_directory =
            workspace_mode ? workspace.state_directory : argv[3],
        .app_socket_path =
            workspace_mode ? workspace.app_socket : argv[4],
        .control_socket_path =
            workspace_mode ? workspace.control_socket : argv[5]
    };
    hermas_host_result result = hermas_host_open(host, &config);
    if (result != HERMAS_HOST_OK) {
        fprintf(
            stderr, "hermasd: startup failed: %s\n",
            hermas_host_result_name(result));
        free(host);
        if (result == HERMAS_HOST_RECOVERY_REQUIRED) {
            return 3;
        }
        return image_check_exit(result);
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
