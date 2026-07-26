#include "hermas2/image.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "test_image: %s\n", message);
    return 1;
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static int validate_additional_fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return fail("cannot open additional fixture");
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return fail("invalid additional fixture length");
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(bytes);
        return fail("cannot read additional fixture");
    }
    fclose(file);
    int result = 0;
    if (hermas2_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS2_IMAGE_OK) {
        result = fail("additional Rust image was rejected");
    }
    if (result == 0) {
        size_t nodes_offset = read_u32(bytes, 48u);
        uint16_t node_count =
            (uint16_t)bytes[30] | ((uint16_t)bytes[31] << 8u);
        bool tested_fork = false;
        for (size_t index = 0u; index < node_count; ++index) {
            size_t offset = nodes_offset + index * 8u;
            if (bytes[offset] == 4u) {
                uint8_t original = bytes[offset + 1u];
                bytes[offset + 1u] = 1u;
                if (hermas2_image_validate(bytes, (size_t)length, NULL) ==
                    HERMAS2_IMAGE_OK) {
                    result = fail("invalid Fork branch count was accepted");
                }
                bytes[offset + 1u] = original;
                tested_fork = true;
                break;
            }
        }
        if (!tested_fork) {
            result = fail("additional fixture contains no Fork");
        }
    }
    free(bytes);
    return result;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        return fail("expected graph-image fixture path and optional additional fixture");
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return fail("cannot open fixture");
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        return fail("invalid fixture length");
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        return fail("cannot read fixture");
    }
    fclose(file);
    hermas2_image_summary summary;
    if (hermas2_image_validate(bytes, (size_t)length, &summary) != HERMAS2_IMAGE_OK) {
        return fail("valid Rust image was rejected");
    }
    if (summary.node_count != 7u || summary.edge_count != 13u ||
        summary.app_count != 3u || summary.type_count != 9u ||
        summary.workflow_name_length != 14u ||
        memcmp(summary.workflow_name, "grade_pipeline", 14u) != 0) {
        return fail("decoded summary differs");
    }
    uint8_t grades[32] = {0u};
    grades[0] = 3u;
    grades[8] = 70u;
    grades[16] = 80u;
    grades[24] = 90u;
    uint8_t mean[8] = {80u};
    uint8_t printed[1] = {1u};
    if (hermas2_image_validate_value(bytes, (size_t)length, 1u, NULL, 0u) !=
            HERMAS2_IMAGE_OK ||
        hermas2_image_validate_value(bytes, (size_t)length, 4u,
                                     grades, sizeof(grades)) !=
            HERMAS2_IMAGE_OK ||
        hermas2_image_validate_value(bytes, (size_t)length, 6u,
                                     mean, sizeof(mean)) !=
            HERMAS2_IMAGE_OK ||
        hermas2_image_validate_value(bytes, (size_t)length, 11u,
                                     printed, sizeof(printed)) !=
            HERMAS2_IMAGE_OK) {
        return fail("valid canonical payload was rejected");
    }
    grades[0] = 33u;
    printed[0] = 2u;
    if (hermas2_image_validate_value(bytes, (size_t)length, 4u,
                                     grades, sizeof(grades)) ==
            HERMAS2_IMAGE_OK ||
        hermas2_image_validate_value(bytes, (size_t)length, 6u,
                                     mean, sizeof(mean) - 1u) ==
            HERMAS2_IMAGE_OK ||
        hermas2_image_validate_value(bytes, (size_t)length, 11u,
                                     printed, sizeof(printed)) ==
            HERMAS2_IMAGE_OK) {
        return fail("malformed canonical payload was accepted");
    }
    for (size_t prefix = 0u; prefix < (size_t)length; ++prefix) {
        if (hermas2_image_validate(bytes, prefix, NULL) == HERMAS2_IMAGE_OK) {
            return fail("truncated prefix was accepted");
        }
    }
    uint8_t original = bytes[12];
    bytes[12] = 1u;
    if (hermas2_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS2_IMAGE_INVALID_HEADER) {
        return fail("reserved header mutation was accepted");
    }
    bytes[12] = original;
    original = bytes[44];
    bytes[44] = 0u;
    if (hermas2_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS2_IMAGE_INVALID_OFFSET) {
        return fail("node offset mutation was accepted");
    }
    bytes[44] = original;
    size_t apps_offset = read_u32(bytes, 40u);
    size_t nodes_offset = read_u32(bytes, 48u);
    size_t edges_offset = read_u32(bytes, 52u);
    const size_t record_mutations[] = {
        apps_offset + 2u,
        nodes_offset,
        edges_offset,
        edges_offset + 3u,
        edges_offset + 14u
    };
    for (size_t index = 0u;
         index < sizeof(record_mutations) / sizeof(record_mutations[0]);
         ++index) {
        size_t offset = record_mutations[index];
        original = bytes[offset];
        bytes[offset] = 0xffu;
        if (hermas2_image_validate(bytes, (size_t)length, NULL) ==
            HERMAS2_IMAGE_OK) {
            return fail("malformed table record was accepted");
        }
        bytes[offset] = original;
    }
    uint8_t saved_error[2] = {bytes[82], bytes[83]};
    bytes[82] = bytes[80];
    bytes[83] = bytes[81];
    if (hermas2_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS2_IMAGE_DUPLICATE_RECORD) {
        return fail("duplicate workflow error was accepted");
    }
    bytes[82] = saved_error[0];
    bytes[83] = saved_error[1];
    free(bytes);
    return argc == 3 ? validate_additional_fixture(argv[2]) : 0;
}
