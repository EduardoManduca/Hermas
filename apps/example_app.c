#include "example_app.h"
#include "hermas/workspace_linux.h"

#include <stdio.h>
#include <string.h>

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int parse_fingerprint(
    const char *text,
    uint8_t fingerprint[32]) {
    if (strlen(text) != 64u) {
        return 0;
    }
    for (size_t index = 0u; index < 32u; ++index) {
        int high = hex_digit(text[index * 2u]);
        int low = hex_digit(text[index * 2u + 1u]);
        if (high < 0 || low < 0) {
            return 0;
        }
        fingerprint[index] =
            (uint8_t)((unsigned)high << 4u | (unsigned)low);
    }
    return 1;
}

int hermas_example_app_run_once(
    int argc,
    char **argv,
    uint16_t app_id,
    uint16_t action_id,
    const uint8_t expected_fingerprint[32],
    hermas_action_handler handler,
    uint8_t *result,
    size_t result_capacity) {
    int workspace_mode =
        argc >= 2 && strcmp(argv[1], "--workspace") == 0;
    if ((!workspace_mode && argc != 3) ||
        (workspace_mode && argc != 3) ||
        expected_fingerprint == NULL) {
        fprintf(
            stderr,
            "usage: %s SOCKET CONTRACT_SHA256\n"
            "       %s --workspace DIRECTORY\n",
            argv[0], argv[0]);
        return 2;
    }
    hermas_workspace_paths workspace;
    const char *socket_path = argv[1];
    uint8_t fingerprint[32];
    if (workspace_mode) {
        hermas_workspace_result opened =
            hermas_workspace_open(argv[2], false, &workspace);
        if (opened != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "workspace error: %s\n",
                hermas_workspace_result_name(opened));
            return 2;
        }
        hermas_workspace_result resolved =
            hermas_workspace_action_fingerprint(
                &workspace, app_id, action_id, fingerprint);
        if (resolved != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "workspace Action identity failed: %s\n",
                hermas_workspace_result_name(resolved));
            return 2;
        }
        socket_path = workspace.app_socket;
        if (memcmp(
                expected_fingerprint, fingerprint,
                sizeof(fingerprint)) != 0) {
            fputs(
                "workspace Action contract does not match this binary\n",
                stderr);
            return 2;
        }
    } else if (!parse_fingerprint(argv[2], fingerprint)) {
        fputs("invalid contract fingerprint\n", stderr);
        return 2;
    } else if (memcmp(
                   expected_fingerprint, fingerprint,
                   sizeof(fingerprint)) != 0) {
        fputs(
            "supplied Action contract does not match this binary\n",
            stderr);
        return 2;
    }
    hermas_edge edge;
    if (hermas_edge_connect(
            &edge, socket_path, app_id, action_id,
            expected_fingerprint) !=
        HERMAS_EDGE_OK) {
        return 1;
    }
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    hermas_edge_result served = hermas_edge_serve_once(
        &edge, packet, sizeof(packet), result, result_capacity,
        handler, NULL);
    int succeeded =
        served == HERMAS_EDGE_OK &&
        edge.delivered_invocations == 1u;
    hermas_edge_disconnect(&edge);
    return succeeded ? 0 : 1;
}
