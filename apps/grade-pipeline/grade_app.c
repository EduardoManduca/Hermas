#include "hermas2/edge.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef HERMAS2_GRADE_APP
#error "HERMAS2_GRADE_APP must identify the sample app"
#endif

#if HERMAS2_GRADE_APP == 2 || HERMAS2_GRADE_APP == 3
static int64_t read_i64(const uint8_t *bytes) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return (int64_t)value;
}
#endif

#if HERMAS2_GRADE_APP == 2
static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}
#endif

#if HERMAS2_GRADE_APP == 1 || HERMAS2_GRADE_APP == 2
static void write_i64(uint8_t *bytes, int64_t value) {
    uint64_t bits = (uint64_t)value;
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(bits >> (index * 8u));
    }
}
#endif

static int handler(
    void *user_data,
    uint16_t action_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint16_t *result_type,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length) {
    (void)user_data;
    *outcome = HERMAS2_OUTCOME_SUCCESS;
#if HERMAS2_GRADE_APP == 1
    (void)input;
    if (action_id != 1u || input_type != 1u || input_length != 0u ||
        result_capacity < 32u) {
        return 0;
    }
    memset(result, 0, 32u);
    result[0] = 3u;
    write_i64(result + 8u, 70);
    write_i64(result + 16u, 80);
    write_i64(result + 24u, 90);
    *result_type = 4u;
    *result_length = 32u;
    return 1;
#elif HERMAS2_GRADE_APP == 2
    if (action_id != 2u || input_type != 8u || input_length != 32u ||
        read_u32(input, 0u) != 3u || read_u32(input, 4u) != 0u ||
        result_capacity < 8u) {
        return 0;
    }
    int64_t total =
        read_i64(input + 8u) + read_i64(input + 16u) +
        read_i64(input + 24u);
    write_i64(result, total / 3);
    *result_type = 6u;
    *result_length = 8u;
    return 1;
#elif HERMAS2_GRADE_APP == 3
    if (action_id != 3u || input_type != 10u || input_length != 8u ||
        result_capacity < 1u ||
        printf("Mean: %lld\n", (long long)read_i64(input)) < 0 ||
        fflush(stdout) != 0) {
        return 0;
    }
    result[0] = 1u;
    *result_type = 11u;
    *result_length = 1u;
    return 1;
#else
#error "unsupported HERMAS2_GRADE_APP"
#endif
}

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

static int parse_fingerprint(const char *text, uint8_t fingerprint[32]) {
    if (strlen(text) != 64u) {
        return 0;
    }
    for (size_t index = 0u; index < 32u; ++index) {
        int high = hex_digit(text[index * 2u]);
        int low = hex_digit(text[index * 2u + 1u]);
        if (high < 0 || low < 0) {
            return 0;
        }
        fingerprint[index] = (uint8_t)((unsigned)high << 4u | (unsigned)low);
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET CONTRACT_SHA256\n", argv[0]);
        return 2;
    }
    uint8_t fingerprint[32];
    if (!parse_fingerprint(argv[2], fingerprint)) {
        fprintf(stderr, "invalid contract fingerprint\n");
        return 2;
    }
    hermas2_edge edge;
    if (hermas2_edge_connect(&edge, argv[1], HERMAS2_GRADE_APP,
                             fingerprint) != HERMAS2_EDGE_OK) {
        return 1;
    }
    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    uint8_t result[264];
    hermas2_edge_result served =
        hermas2_edge_serve_once(&edge, packet, sizeof(packet),
                                result, sizeof(result), handler, NULL);
    int succeeded =
        served == HERMAS2_EDGE_OK && edge.delivered_invocations == 1u;
    hermas2_edge_disconnect(&edge);
    return succeeded ? 0 : 1;
}
