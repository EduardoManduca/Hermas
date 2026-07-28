#include "hermas/client.h"
#include "hermas/image.h"
#include "hermas/version.h"

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

static int image_input_type(
    const char *path,
    uint16_t *input_type) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }
    long length = ftell(file);
    if (length <= 0 ||
        (uint64_t)length > HERMAS_IMAGE_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) !=
            (size_t)length) {
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    hermas_image_summary summary;
    int valid = hermas_image_validate(
                    bytes, (size_t)length, &summary) ==
                HERMAS_IMAGE_OK;
    if (valid) {
        *input_type = summary.input_type;
    }
    free(bytes);
    return valid;
}

static const char *outcome_name(uint16_t outcome) {
    switch (outcome) {
        case HERMAS_OUTCOME_SUCCESS:
            return "success";
        case HERMAS_OUTCOME_APP_ERROR:
            return "app-error";
        case HERMAS_OUTCOME_NOT_SENT:
            return "not-sent";
        case HERMAS_OUTCOME_UNKNOWN:
            return "unknown";
        default:
            return "invalid";
    }
}

static int outcome_exit_code(uint16_t outcome) {
    switch (outcome) {
        case HERMAS_OUTCOME_SUCCESS:
            return 0;
        case HERMAS_OUTCOME_APP_ERROR:
            return 10;
        case HERMAS_OUTCOME_NOT_SENT:
            return 11;
        case HERMAS_OUTCOME_UNKNOWN:
            return 12;
        default:
            return 1;
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf(
            "Hermas %s (hermas_run; protocol %u)\n",
            HERMAS_VERSION, HERMAS_PROTOCOL_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts(
            "usage:\n"
            "  hermas_run CONTROL_SOCKET EXECUTION_ID "
            "INPUT_TYPE [INPUT_HEX]\n"
            "  hermas_run CONTROL_SOCKET EXECUTION_ID "
            "--image IMAGE [INPUT_HEX]");
        return 0;
    }
    int image_mode =
        argc >= 4 && strcmp(argv[3], "--image") == 0;
    if ((!image_mode && argc != 4 && argc != 5) ||
        (image_mode && argc != 5 && argc != 6)) {
        fprintf(
            stderr,
            "usage: %s CONTROL_SOCKET EXECUTION_ID "
            "INPUT_TYPE [INPUT_HEX]\n"
            "       %s CONTROL_SOCKET EXECUTION_ID "
            "--image IMAGE [INPUT_HEX]\n",
            argv[0],
            argv[0]);
        return 2;
    }
    uint64_t execution_id = 0u;
    uint16_t input_type = 0u;
    uint8_t *input = malloc(HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE);
    uint8_t *packet = malloc(HERMAS_PROTOCOL_MAX_PACKET_SIZE);
    size_t input_size = 0u;
    if (input == NULL || packet == NULL ||
        !parse_u64(argv[2], &execution_id) ||
        (image_mode
             ? !image_input_type(argv[4], &input_type)
             : !parse_u16(argv[3], &input_type)) ||
        ((!image_mode && argc == 5) &&
         !parse_hex(
             argv[4], input, HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE,
             &input_size)) ||
        ((image_mode && argc == 6) &&
         !parse_hex(
             argv[5], input, HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE,
             &input_size))) {
        fputs("hermas_run: invalid argument\n", stderr);
        free(packet);
        free(input);
        return 2;
    }
    hermas_client client;
    hermas_client_result connected =
        hermas_client_connect(&client, argv[1]);
    hermas_frame result;
    hermas_client_result executed = connected;
    if (connected == HERMAS_CLIENT_OK) {
        executed = hermas_client_execute(
            &client, execution_id, input_type,
            input_size == 0u ? NULL : input, input_size,
            packet, HERMAS_PROTOCOL_MAX_PACKET_SIZE, &result);
        hermas_client_close(&client);
    }
    if (executed != HERMAS_CLIENT_OK) {
        fprintf(
            stderr, "hermas_run: execution failed: %s\n",
            hermas_client_result_name(executed));
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
