#ifndef HERMAS2_IMAGE_H
#define HERMAS2_IMAGE_H

#include "hermas2/image_format.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas2_image_result {
    HERMAS2_IMAGE_OK = 0,
    HERMAS2_IMAGE_TRUNCATED,
    HERMAS2_IMAGE_BAD_MAGIC,
    HERMAS2_IMAGE_UNSUPPORTED_VERSION,
    HERMAS2_IMAGE_INVALID_HEADER,
    HERMAS2_IMAGE_INVALID_OFFSET,
    HERMAS2_IMAGE_INVALID_COUNT,
    HERMAS2_IMAGE_INVALID_STRING,
    HERMAS2_IMAGE_INVALID_RECORD,
    HERMAS2_IMAGE_DUPLICATE_RECORD,
    HERMAS2_IMAGE_INVALID_TOPOLOGY,
    HERMAS2_IMAGE_INVALID_VALUE
} hermas2_image_result;

typedef struct hermas2_image_summary {
    const uint8_t *workflow_name;
    uint16_t workflow_name_length;
    uint16_t input_type;
    uint16_t success_type;
    uint16_t error_count;
    uint16_t action_contract_count;
    uint16_t type_count;
    uint16_t node_count;
    uint16_t edge_count;
} hermas2_image_summary;

hermas2_image_result hermas2_image_validate(
    const uint8_t *bytes,
    size_t size,
    hermas2_image_summary *summary);

hermas2_image_result hermas2_image_validate_value(
    const uint8_t *image,
    size_t image_size,
    uint16_t type_id,
    const uint8_t *payload,
    size_t payload_size);

hermas2_image_result hermas2_image_list_items(
    const uint8_t *image,
    size_t image_size,
    uint16_t type_id,
    const uint8_t *payload,
    size_t payload_size,
    uint16_t *item_count,
    size_t *item_offsets,
    size_t *item_lengths,
    size_t item_capacity);

const char *hermas2_image_result_name(hermas2_image_result result);

#endif
