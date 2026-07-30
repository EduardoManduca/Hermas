#ifndef HERMAS_IMAGE_H
#define HERMAS_IMAGE_H

#include "hermas/image_format.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas_image_result {
    HERMAS_IMAGE_OK = 0,
    HERMAS_IMAGE_TRUNCATED,
    HERMAS_IMAGE_BAD_MAGIC,
    HERMAS_IMAGE_UNSUPPORTED_VERSION,
    HERMAS_IMAGE_INVALID_HEADER,
    HERMAS_IMAGE_INVALID_OFFSET,
    HERMAS_IMAGE_INVALID_COUNT,
    HERMAS_IMAGE_INVALID_STRING,
    HERMAS_IMAGE_INVALID_RECORD,
    HERMAS_IMAGE_DUPLICATE_RECORD,
    HERMAS_IMAGE_INVALID_TOPOLOGY,
    HERMAS_IMAGE_INVALID_VALUE,
    HERMAS_IMAGE_ACTION_NOT_FOUND
} hermas_image_result;

typedef struct hermas_image_summary {
    const uint8_t *workflow_name;
    uint16_t workflow_name_length;
    uint16_t input_type;
    uint16_t success_type;
    uint16_t error_count;
    uint16_t action_contract_count;
    uint16_t type_count;
    uint16_t node_count;
    uint16_t edge_count;
} hermas_image_summary;

typedef enum hermas_image_value_kind {
    HERMAS_IMAGE_VALUE_UNIT = 1,
    HERMAS_IMAGE_VALUE_INTEGER = 2,
    HERMAS_IMAGE_VALUE_BOOLEAN = 3,
    HERMAS_IMAGE_VALUE_STRING = 4,
    HERMAS_IMAGE_VALUE_BYTES = 5,
    HERMAS_IMAGE_VALUE_RECORD = 6,
    HERMAS_IMAGE_VALUE_LIST = 7,
    HERMAS_IMAGE_VALUE_VARIANT = 8
} hermas_image_value_kind;

typedef struct hermas_image_type_summary {
    hermas_image_value_kind kind;
    uint32_t bound;
} hermas_image_type_summary;

typedef struct hermas_image_action_contract {
    uint16_t app_id;
    uint16_t action_id;
    uint16_t input_type;
    uint16_t success_type;
    uint16_t error_type;
    uint8_t fingerprint[32];
} hermas_image_action_contract;

hermas_image_result hermas_image_validate(
    const uint8_t *bytes,
    size_t size,
    hermas_image_summary *summary);

/*
 * Describes the outer canonical representation of one nominal Type.
 * `bound` is meaningful for String, Bytes, and List; it is zero otherwise.
 */
hermas_image_result hermas_image_describe_type(
    const uint8_t *image,
    size_t image_size,
    uint16_t type_id,
    hermas_image_type_summary *summary);

/*
 * Copies the exact semantic fingerprint for one installed Action contract.
 * The complete image is validated before lookup.
 */
hermas_image_result hermas_image_action_fingerprint(
    const uint8_t *image,
    size_t image_size,
    uint16_t app_id,
    uint16_t action_id,
    uint8_t fingerprint[32]);

/*
 * Resolves graph-local IDs and port Types from an independently compiled
 * semantic Action fingerprint. The complete image is validated first.
 */
hermas_image_result hermas_image_find_action_contract(
    const uint8_t *image,
    size_t image_size,
    const uint8_t fingerprint[32],
    hermas_image_action_contract *contract);

hermas_image_result hermas_image_validate_value(
    const uint8_t *image,
    size_t image_size,
    uint16_t type_id,
    const uint8_t *payload,
    size_t payload_size);

hermas_image_result hermas_image_list_items(
    const uint8_t *image,
    size_t image_size,
    uint16_t type_id,
    const uint8_t *payload,
    size_t payload_size,
    uint16_t *item_count,
    size_t *item_offsets,
    size_t *item_lengths,
    size_t item_capacity);

const char *hermas_image_result_name(hermas_image_result result);

#endif
