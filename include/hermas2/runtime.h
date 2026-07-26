#ifndef HERMAS2_RUNTIME_H
#define HERMAS2_RUNTIME_H

#include "hermas2/protocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas2_execution_state {
    HERMAS2_EXECUTION_EMPTY = 0,
    HERMAS2_EXECUTION_READY,
    HERMAS2_EXECUTION_PREPARED,
    HERMAS2_EXECUTION_SENT,
    HERMAS2_EXECUTION_COMPLETE,
    HERMAS2_EXECUTION_WAITING
} hermas2_execution_state;

typedef enum hermas2_runtime_result {
    HERMAS2_RUNTIME_OK = 0,
    HERMAS2_RUNTIME_INVALID_ARGUMENT,
    HERMAS2_RUNTIME_INVALID_IMAGE,
    HERMAS2_RUNTIME_INVALID_VALUE,
    HERMAS2_RUNTIME_BUFFER_TOO_SMALL,
    HERMAS2_RUNTIME_INVALID_STATE,
    HERMAS2_RUNTIME_UNEXPECTED_RESULT,
    HERMAS2_RUNTIME_REQUEST_ID_EXHAUSTED
} hermas2_runtime_result;

typedef struct hermas2_execution {
    const uint8_t *image;
    size_t image_size;
    uint8_t *value_buffer;
    size_t value_capacity;
    size_t value_length;
    uint64_t execution_id;
    uint64_t request_id;
    uint16_t current_node;
    uint16_t value_source_type;
    uint16_t value_destination_type;
    uint16_t terminal_outcome;
    hermas2_execution_state state;
} hermas2_execution;

#define HERMAS2_RUNTIME_MAX_FLOWS 8u
#define HERMAS2_RUNTIME_MAX_EACH_ITEMS 256u

typedef struct hermas2_flow {
    uint8_t *value_buffer;
    size_t value_capacity;
    size_t value_length;
    uint64_t request_id;
    uint16_t current_node;
    uint16_t value_source_type;
    uint16_t value_destination_type;
    uint16_t join_node;
    uint8_t join_tag;
    uint8_t each_region;
    uint16_t item_index;
    hermas2_execution_state state;
    uint8_t active;
} hermas2_flow;

typedef struct hermas2_group_execution {
    const uint8_t *image;
    size_t image_size;
    uint64_t execution_id;
    uint64_t next_request_id;
    uint64_t deadline_ms;
    uint64_t region_deadlines_ms[8];
    uint16_t region_first_nodes[8];
    uint16_t region_last_nodes[8];
    uint8_t region_count;
    uint8_t each_region_count;
    uint16_t each_template_nodes[8];
    uint16_t each_source_types[8];
    uint16_t each_item_input_types[8];
    uint16_t each_item_output_types[8];
    uint16_t each_collected_types[8];
    uint16_t each_bounds[8];
    uint8_t each_concurrency[8];
    uint8_t active_each_region;
    uint8_t each_coordinator_flow;
    uint16_t each_item_count;
    uint16_t each_next_item;
    uint16_t each_completed_items;
    uint16_t each_flush_item;
    size_t each_item_offsets[HERMAS2_RUNTIME_MAX_EACH_ITEMS];
    size_t each_item_lengths[HERMAS2_RUNTIME_MAX_EACH_ITEMS];
    uint8_t each_source_snapshot[HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
    uint8_t each_collection[HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
    size_t each_collection_length;
    hermas2_flow flows[HERMAS2_RUNTIME_MAX_FLOWS];
    size_t value_stride;
    uint16_t pending_outcome;
    uint8_t pending_flow;
    uint8_t complete;
} hermas2_group_execution;

hermas2_runtime_result hermas2_group_start(
    hermas2_group_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint64_t execution_id,
    uint8_t *value_storage,
    size_t value_storage_capacity,
    size_t value_stride,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length);

hermas2_runtime_result hermas2_group_prepare(
    hermas2_group_execution *execution,
    size_t flow_index,
    hermas2_frame *invocation);

hermas2_runtime_result hermas2_group_mark_sent(
    hermas2_group_execution *execution,
    size_t flow_index);

hermas2_runtime_result hermas2_group_mark_not_sent(
    hermas2_group_execution *execution,
    size_t flow_index);

hermas2_runtime_result hermas2_group_mark_unknown(
    hermas2_group_execution *execution,
    size_t flow_index);

hermas2_runtime_result hermas2_group_accept_result(
    hermas2_group_execution *execution,
    size_t flow_index,
    const hermas2_frame *result);

hermas2_runtime_result hermas2_group_get_result(
    const hermas2_group_execution *execution,
    hermas2_frame *result);

uint64_t hermas2_group_deadline_ms(
    const hermas2_group_execution *execution);

hermas2_runtime_result hermas2_group_expire(
    hermas2_group_execution *execution);

uint64_t hermas2_group_region_deadline_ms(
    const hermas2_group_execution *execution,
    uint8_t region_id);

hermas2_runtime_result hermas2_group_expire_region(
    hermas2_group_execution *execution,
    uint8_t region_id);

hermas2_runtime_result hermas2_execution_start(
    hermas2_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint64_t execution_id,
    uint8_t *value_buffer,
    size_t value_capacity,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length);

hermas2_runtime_result hermas2_execution_prepare(
    hermas2_execution *execution,
    hermas2_frame *invocation);

hermas2_runtime_result hermas2_execution_mark_sent(
    hermas2_execution *execution);

hermas2_runtime_result hermas2_execution_mark_not_sent(
    hermas2_execution *execution);

hermas2_runtime_result hermas2_execution_mark_unknown(
    hermas2_execution *execution);

hermas2_runtime_result hermas2_execution_accept_result(
    hermas2_execution *execution,
    const hermas2_frame *result);

hermas2_runtime_result hermas2_execution_get_result(
    const hermas2_execution *execution,
    hermas2_frame *result);

const char *hermas2_runtime_result_name(hermas2_runtime_result result);

#endif
