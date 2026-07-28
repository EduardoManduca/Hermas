#include "hermas/client.h"
#include "hermas/image.h"
#include "hermas/version.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct loaded_image {
    uint8_t *bytes;
    size_t size;
    hermas_image_summary summary;
} loaded_image;

static void write_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
    bytes[offset + 2u] = (uint8_t)(value >> 16u);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *bytes) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
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

static int load_image(
    const char *path,
    uint8_t *storage,
    size_t capacity,
    loaded_image *image) {
    memset(image, 0, sizeof(*image));
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
        (uint64_t)length > capacity ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    if (storage == NULL ||
        fread(storage, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        return 0;
    }
    fclose(file);
    image->bytes = storage;
    image->size = (size_t)length;
    if (hermas_image_validate(
            image->bytes, image->size, &image->summary) !=
        HERMAS_IMAGE_OK) {
        memset(image, 0, sizeof(*image));
        return 0;
    }
    return 1;
}

static void unload_image(loaded_image *image) {
    memset(image, 0, sizeof(*image));
}

static int parse_scalar(
    const loaded_image *image,
    uint16_t type_id,
    const char *text,
    uint8_t *bytes,
    size_t capacity,
    size_t *size) {
    if (image == NULL || text == NULL || bytes == NULL ||
        size == NULL || capacity < 8u) {
        return 0;
    }
    hermas_image_type_summary type;
    if (hermas_image_describe_type(
            image->bytes, image->size, type_id, &type) !=
        HERMAS_IMAGE_OK) {
        return 0;
    }
    *size = 0u;
    if (type.kind == HERMAS_IMAGE_VALUE_UNIT) {
        return strcmp(text, "unit") == 0;
    }
    if (type.kind == HERMAS_IMAGE_VALUE_INTEGER) {
        char *end = NULL;
        errno = 0;
        intmax_t parsed = strtoimax(text, &end, 10);
        if (errno != 0 || end == text || *end != '\0' ||
            parsed < INT64_MIN || parsed > INT64_MAX ||
            capacity < 8u) {
            return 0;
        }
        uint64_t encoded = (uint64_t)(int64_t)parsed;
        for (size_t index = 0u; index < 8u; ++index) {
            bytes[index] = (uint8_t)(encoded >> (index * 8u));
        }
        *size = 8u;
        return 1;
    }
    if (type.kind == HERMAS_IMAGE_VALUE_BOOLEAN) {
        if (strcmp(text, "true") == 0) {
            bytes[0] = 1u;
        } else if (strcmp(text, "false") == 0) {
            bytes[0] = 0u;
        } else {
            return 0;
        }
        *size = 1u;
        return 1;
    }
    if (type.kind == HERMAS_IMAGE_VALUE_STRING) {
        size_t length = strlen(text);
        if (length > type.bound || length > UINT32_MAX ||
            length > capacity - 8u) {
            return 0;
        }
        write_u32(bytes, 0u, (uint32_t)length);
        write_u32(bytes, 4u, 0u);
        memcpy(bytes + 8u, text, length);
        *size = 8u + length;
        return 1;
    }
    if (type.kind == HERMAS_IMAGE_VALUE_BYTES) {
        size_t length = strlen(text);
        if (length < 2u || text[0] != '0' || text[1] != 'x') {
            return 0;
        }
        size_t data_size = 0u;
        if (!parse_hex(
                text + 2u, bytes + 8u,
                capacity - 8u, &data_size) ||
            data_size > type.bound || data_size > UINT32_MAX) {
            return 0;
        }
        write_u32(bytes, 0u, (uint32_t)data_size);
        write_u32(bytes, 4u, 0u);
        *size = 8u + data_size;
        return 1;
    }
    return -1;
}

static void print_escaped_string(
    const uint8_t *bytes,
    size_t size) {
    putchar('"');
    for (size_t index = 0u; index < size; ++index) {
        unsigned value = bytes[index];
        if (value == '"' || value == '\\') {
            putchar('\\');
            putchar((int)value);
        } else if (value == '\n') {
            fputs("\\n", stdout);
        } else if (value == '\r') {
            fputs("\\r", stdout);
        } else if (value == '\t') {
            fputs("\\t", stdout);
        } else if (value < 0x20u) {
            printf("\\u%04x", value);
        } else {
            putchar((int)value);
        }
    }
    putchar('"');
}

static void print_scalar(
    const loaded_image *image,
    uint16_t type_id,
    const uint8_t *bytes,
    size_t size) {
    hermas_image_type_summary type;
    if (hermas_image_describe_type(
            image->bytes, image->size, type_id, &type) !=
            HERMAS_IMAGE_OK ||
        hermas_image_validate_value(
            image->bytes, image->size, type_id, bytes, size) !=
            HERMAS_IMAGE_OK) {
        return;
    }
    if (type.kind == HERMAS_IMAGE_VALUE_UNIT) {
        fputs(" display=unit", stdout);
    } else if (type.kind == HERMAS_IMAGE_VALUE_INTEGER && size == 8u) {
        uint64_t value = read_u64(bytes);
        if (value <= INT64_MAX) {
            printf(" display=%" PRIu64, value);
        } else {
            printf(" display=-%" PRIu64, (~value) + 1u);
        }
    } else if (type.kind == HERMAS_IMAGE_VALUE_BOOLEAN && size == 1u) {
        printf(" display=%s", bytes[0] == 0u ? "false" : "true");
    } else if ((type.kind == HERMAS_IMAGE_VALUE_STRING ||
                type.kind == HERMAS_IMAGE_VALUE_BYTES) &&
               size >= 8u) {
        uint32_t length = read_u32(bytes, 0u);
        fputs(" display=", stdout);
        if (type.kind == HERMAS_IMAGE_VALUE_STRING) {
            print_escaped_string(bytes + 8u, length);
        } else {
            fputs("0x", stdout);
            for (uint32_t index = 0u; index < length; ++index) {
                printf("%02x", (unsigned)bytes[8u + index]);
            }
        }
    }
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
            "--image IMAGE [INPUT_HEX]\n"
            "  hermas_run CONTROL_SOCKET EXECUTION_ID "
            "--image IMAGE --value VALUE\n"
            "  hermas_run CONTROL_SOCKET EXECUTION_ID "
            "--image IMAGE --hex INPUT_HEX\n\n"
            "--value accepts unit, decimal Integer, true/false, "
            "String text, or 0x-prefixed Bytes.");
        return 0;
    }
    int image_mode =
        argc >= 4 && strcmp(argv[3], "--image") == 0;
    if ((!image_mode && argc != 4 && argc != 5) ||
        (image_mode && argc != 5 && argc != 6 && argc != 7) ||
        (image_mode && argc == 7 &&
         strcmp(argv[5], "--value") != 0 &&
         strcmp(argv[5], "--hex") != 0)) {
        fprintf(
            stderr,
            "usage: %s CONTROL_SOCKET EXECUTION_ID "
            "INPUT_TYPE [INPUT_HEX]\n"
            "       %s CONTROL_SOCKET EXECUTION_ID "
            "--image IMAGE [INPUT_HEX]\n"
            "       %s CONTROL_SOCKET EXECUTION_ID "
            "--image IMAGE --value VALUE\n"
            "       %s CONTROL_SOCKET EXECUTION_ID "
            "--image IMAGE --hex INPUT_HEX\n",
            argv[0],
            argv[0],
            argv[0],
            argv[0]);
        return 2;
    }
    uint64_t execution_id = 0u;
    uint16_t input_type = 0u;
    uint8_t *input = malloc(HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE);
    uint8_t *packet = malloc(HERMAS_PROTOCOL_MAX_PACKET_SIZE);
    uint8_t *image_storage =
        image_mode ? malloc(HERMAS_IMAGE_MAX_SIZE) : NULL;
    size_t input_size = 0u;
    loaded_image image = {0};
    int image_loaded =
        image_mode && image_storage != NULL &&
        load_image(
            argv[4], image_storage, HERMAS_IMAGE_MAX_SIZE, &image);
    int scalar_result = 1;
    if (image_loaded && input != NULL) {
        input_type = image.summary.input_type;
        if (argc == 6) {
            scalar_result = parse_hex(
                argv[5], input, HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE,
                &input_size);
        } else if (argc == 7 && strcmp(argv[5], "--hex") == 0) {
            scalar_result = parse_hex(
                argv[6], input, HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE,
                &input_size);
        } else if (argc == 7) {
            scalar_result = parse_scalar(
                &image, input_type, argv[6], input,
                HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE, &input_size);
        }
    } else if (image_loaded) {
        scalar_result = 0;
    }
    int canonical_input =
        !image_loaded ||
        (input != NULL &&
         hermas_image_validate_value(
             image.bytes, image.size, input_type,
             input_size == 0u ? NULL : input, input_size) ==
             HERMAS_IMAGE_OK);
    if (input == NULL || packet == NULL ||
        !parse_u64(argv[2], &execution_id) ||
        (image_mode ? !image_loaded
                    : !parse_u16(argv[3], &input_type)) ||
        ((!image_mode && argc == 5) &&
         !parse_hex(
             argv[4], input, HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE,
             &input_size)) ||
        scalar_result <= 0 || !canonical_input) {
        if (scalar_result < 0) {
            fputs(
                "hermas_run: --value supports only scalar input "
                "Types; use --hex for composite canonical values\n",
                stderr);
        } else {
            fputs("hermas_run: invalid argument or value\n", stderr);
        }
        unload_image(&image);
        free(image_storage);
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
        unload_image(&image);
        free(image_storage);
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
    if (image_loaded) {
        print_scalar(
            &image, result.source_type,
            result.payload, result.payload_length);
    }
    putchar('\n');
    int exit_code = outcome_exit_code(result.outcome);
    unload_image(&image);
    free(image_storage);
    free(packet);
    free(input);
    return exit_code;
}
