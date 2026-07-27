#include "hermas2/image.h"
#include "hermas2/version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf(
            "Hermas %s (hermas2_image_check; graph-image %u)\n",
            HERMAS_VERSION, HERMAS2_GRAPH_IMAGE_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts(
            "usage: hermas2_image_check [--describe] IMAGE\n\n"
            "Validate a graph image; --describe prints its runtime "
            "contract metadata.");
        return 0;
    }
    int describe =
        argc == 3 && strcmp(argv[1], "--describe") == 0;
    if ((!describe && argc != 2) || (describe && argc != 3)) {
        fputs(
            "usage: hermas2_image_check [--describe] IMAGE\n",
            stderr);
        return 2;
    }
    const char *path = argv[describe ? 2 : 1];
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) !=
            (size_t)length) {
        free(bytes);
        fclose(file);
        return 2;
    }
    fclose(file);
    hermas2_image_summary summary;
    hermas2_image_result result = hermas2_image_validate(
        bytes, (size_t)length, &summary);
    if (result != HERMAS2_IMAGE_OK) {
        fprintf(stderr, "%s\n", hermas2_image_result_name(result));
        free(bytes);
        return 1;
    }
    if (describe) {
        printf(
            "workflow=%.*s\ninput-type=%u\nsuccess-type=%u\n",
            (int)summary.workflow_name_length,
            (const char *)summary.workflow_name,
            (unsigned)summary.input_type,
            (unsigned)summary.success_type);
        size_t contracts = read_u32(
            bytes,
            HERMAS2_IMAGE_HEADER_ACTION_CONTRACTS_OFFSET);
        uint16_t count = read_u16(
            bytes,
            HERMAS2_IMAGE_HEADER_ACTION_CONTRACT_COUNT_OFFSET);
        for (uint16_t index = 0u; index < count; ++index) {
            size_t record =
                contracts + (size_t)index *
                                HERMAS2_IMAGE_ACTION_CONTRACT_RECORD_SIZE;
            printf(
                "action app=%u id=%u fingerprint=",
                (unsigned)read_u16(bytes, record),
                (unsigned)read_u16(bytes, record + 2u));
            for (size_t byte = 0u; byte < 32u; ++byte) {
                printf("%02x", (unsigned)bytes[record + 4u + byte]);
            }
            putchar('\n');
        }
    }
    free(bytes);
    return 0;
}
