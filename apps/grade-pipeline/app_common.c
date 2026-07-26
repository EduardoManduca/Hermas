#include "app_common.h"

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

int hermas2_example_app_run_once(
    int argc,
    char **argv,
    uint16_t app_id,
    hermas2_action_handler handler,
    uint8_t *result,
    size_t result_capacity) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET CONTRACT_SHA256\n", argv[0]);
        return 2;
    }
    uint8_t fingerprint[32];
    if (!parse_fingerprint(argv[2], fingerprint)) {
        fputs("invalid contract fingerprint\n", stderr);
        return 2;
    }
    hermas2_edge edge;
    if (hermas2_edge_connect(
            &edge, argv[1], app_id, fingerprint) != HERMAS2_EDGE_OK) {
        return 1;
    }
    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    hermas2_edge_result served = hermas2_edge_serve_once(
        &edge, packet, sizeof(packet), result, result_capacity,
        handler, NULL);
    int succeeded =
        served == HERMAS2_EDGE_OK &&
        edge.delivered_invocations == 1u;
    hermas2_edge_disconnect(&edge);
    return succeeded ? 0 : 1;
}
