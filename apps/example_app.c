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
    hermas_action_handler handler,
    uint8_t *result,
    size_t result_capacity) {
    int workspace_mode =
        argc == 4 && strcmp(argv[1], "--workspace") == 0;
    if (argc != 3 && !workspace_mode) {
        fprintf(
            stderr,
            "usage: %s SOCKET CONTRACT_SHA256\n"
            "       %s --workspace DIRECTORY CONTRACT_SHA256\n",
            argv[0], argv[0]);
        return 2;
    }
    hermas_workspace_paths workspace;
    const char *socket_path = argv[1];
    const char *fingerprint_text = argv[2];
    if (workspace_mode) {
        hermas_workspace_result opened =
            hermas_workspace_open(argv[2], false, &workspace);
        if (opened != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "workspace error: %s\n",
                hermas_workspace_result_name(opened));
            return 2;
        }
        hermas_workspace_binding binding;
        hermas_workspace_result loaded =
            hermas_workspace_load(&workspace, &binding);
        if (loaded != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "workspace binding failed: %s\n",
                hermas_workspace_result_name(loaded));
            return 2;
        }
        socket_path = workspace.app_socket;
        fingerprint_text = argv[3];
    }
    uint8_t fingerprint[32];
    if (!parse_fingerprint(fingerprint_text, fingerprint)) {
        fputs("invalid contract fingerprint\n", stderr);
        return 2;
    }
    hermas_edge edge;
    if (hermas_edge_connect(
            &edge, socket_path, app_id, action_id, fingerprint) !=
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
