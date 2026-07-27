#include "hermas2/client.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static int parse_u64(const char *text, uint64_t *value) {
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed == 0ull || parsed == UINT64_MAX) {
        return 0;
    }
    *value = (uint64_t)parsed;
    return 1;
}

static int parse_u16(const char *text, uint16_t *value) {
    uint64_t parsed = 0u;
    if (!parse_u64(text, &parsed) || parsed > UINT16_MAX) {
        return 0;
    }
    *value = (uint16_t)parsed;
    return 1;
}

static int parse_hex(
    const char *text,
    uint8_t *bytes,
    size_t capacity,
    size_t *size) {
    size_t length = strlen(text);
    if (length % 2u != 0u || length / 2u > capacity) {
        return 0;
    }
    *size = length / 2u;
    for (size_t index = 0u; index < *size; ++index) {
        int high = hex_digit(text[index * 2u]);
        int low = hex_digit(text[index * 2u + 1u]);
        if (high < 0 || low < 0) {
            return 0;
        }
        bytes[index] =
            (uint8_t)((unsigned)high << 4u | (unsigned)low);
    }
    return 1;
}

static const char *outcome_name(uint16_t outcome) {
    switch (outcome) {
        case HERMAS2_OUTCOME_SUCCESS:
            return "success";
        case HERMAS2_OUTCOME_APP_ERROR:
            return "app-error";
        case HERMAS2_OUTCOME_NOT_SENT:
            return "not-sent";
        case HERMAS2_OUTCOME_UNKNOWN:
            return "unknown";
        default:
            return "invalid";
    }
}

static int outcome_exit_code(uint16_t outcome) {
    switch (outcome) {
        case HERMAS2_OUTCOME_SUCCESS:
            return 0;
        case HERMAS2_OUTCOME_APP_ERROR:
            return 10;
        case HERMAS2_OUTCOME_NOT_SENT:
            return 11;
        case HERMAS2_OUTCOME_UNKNOWN:
            return 12;
        default:
            return 1;
    }
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        fprintf(
            stderr,
            "usage: %s CONTROL_SOCKET EXECUTION_ID "
            "INPUT_TYPE [INPUT_HEX]\n",
            argv[0]);
        return 2;
    }
    uint64_t execution_id = 0u;
    uint16_t input_type = 0u;
    uint8_t *input = malloc(HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE);
    uint8_t *packet = malloc(HERMAS2_PROTOCOL_MAX_PACKET_SIZE);
    size_t input_size = 0u;
    if (input == NULL || packet == NULL ||
        !parse_u64(argv[2], &execution_id) ||
        !parse_u16(argv[3], &input_type) ||
        (argc == 5 &&
         !parse_hex(
             argv[4], input, HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE,
             &input_size))) {
        fputs("hermas2_run: invalid argument\n", stderr);
        free(packet);
        free(input);
        return 2;
    }
    hermas2_client client;
    hermas2_client_result connected =
        hermas2_client_connect(&client, argv[1]);
    hermas2_frame result;
    hermas2_client_result executed = connected;
    if (connected == HERMAS2_CLIENT_OK) {
        executed = hermas2_client_execute(
            &client, execution_id, input_type,
            input_size == 0u ? NULL : input, input_size,
            packet, HERMAS2_PROTOCOL_MAX_PACKET_SIZE, &result);
        hermas2_client_close(&client);
    }
    if (executed != HERMAS2_CLIENT_OK) {
        fprintf(
            stderr, "hermas2_run: execution failed: %s\n",
            hermas2_client_result_name(executed));
        free(packet);
        free(input);
        return 1;
    }
    printf(
        "execution=%" PRIu64 " outcome=%s source_type=%u "
        "destination_type=%u value=",
        result.execution_id, outcome_name(result.outcome),
        (unsigned)result.source_type,
        (unsigned)result.destination_type);
    for (uint32_t index = 0u;
         index < result.payload_length; ++index) {
        printf("%02x", (unsigned)result.payload[index]);
    }
    putchar('\n');
    int exit_code = outcome_exit_code(result.outcome);
    free(packet);
    free(input);
    return exit_code;
}
