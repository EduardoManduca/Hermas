#ifndef HERMAS_RUNTIME_H
#define HERMAS_RUNTIME_H

#include "hermas/protocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas_execution_state {
    HERMAS_EXECUTION_EMPTY = 0,
    HERMAS_EXECUTION_READY,
    HERMAS_EXECUTION_PREPARED,
    HERMAS_EXECUTION_SENT,
    HERMAS_EXECUTION_COMPLETE,
    HERMAS_EXECUTION_WAITING
} hermas_execution_state;

typedef enum hermas_runtime_result {
    HERMAS_RUNTIME_OK = 0,
    HERMAS_RUNTIME_INVALID_ARGUMENT,
    HERMAS_RUNTIME_INVALID_IMAGE,
    HERMAS_RUNTIME_INVALID_VALUE,
    HERMAS_RUNTIME_BUFFER_TOO_SMALL,
    HERMAS_RUNTIME_INVALID_STATE,
    HERMAS_RUNTIME_UNEXPECTED_RESULT,
    HERMAS_RUNTIME_REQUEST_ID_EXHAUSTED
} hermas_runtime_result;

typedef struct hermas_execution {
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
    hermas_execution_state state;
} hermas_execution;

#define HERMAS_RUNTIME_MAX_FLOWS 8u
#define HERMAS_RUNTIME_MAX_EACH_ITEMS 256u

typedef struct hermas_flow {
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
    hermas_execution_state state;
    uint8_t active;
} hermas_flow;

typedef struct hermas_group_execution {
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
    size_t each_item_offsets[HERMAS_RUNTIME_MAX_EACH_ITEMS];
    size_t each_item_lengths[HERMAS_RUNTIME_MAX_EACH_ITEMS];
    uint8_t each_source_snapshot[HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE];
    uint8_t each_collection[HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE];
    size_t each_collection_length;
    hermas_flow flows[HERMAS_RUNTIME_MAX_FLOWS];
    size_t value_stride;
    uint16_t pending_outcome;
    uint8_t pending_flow;
    uint8_t complete;
} hermas_group_execution;

hermas_runtime_result hermas_group_start(
    hermas_group_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint64_t execution_id,
    uint8_t *value_storage,
    size_t value_storage_capacity,
    size_t value_stride,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length);

hermas_runtime_result hermas_group_prepare(
    hermas_group_execution *execution,
    size_t flow_index,
    hermas_frame *invocation);

hermas_runtime_result hermas_group_mark_sent(
    hermas_group_execution *execution,
    size_t flow_index);

hermas_runtime_result hermas_group_mark_not_sent(
    hermas_group_execution *execution,
    size_t flow_index);

hermas_runtime_result hermas_group_mark_unknown(
    hermas_group_execution *execution,
    size_t flow_index);

hermas_runtime_result hermas_group_accept_result(
    hermas_group_execution *execution,
    size_t flow_index,
    const hermas_frame *result);

hermas_runtime_result hermas_group_get_result(
    const hermas_group_execution *execution,
    hermas_frame *result);

uint64_t hermas_group_deadline_ms(
    const hermas_group_execution *execution);

hermas_runtime_result hermas_group_expire(
    hermas_group_execution *execution);

uint64_t hermas_group_region_deadline_ms(
    const hermas_group_execution *execution,
    uint8_t region_id);

hermas_runtime_result hermas_group_expire_region(
    hermas_group_execution *execution,
    uint8_t region_id);

hermas_runtime_result hermas_execution_start(
    hermas_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint64_t execution_id,
    uint8_t *value_buffer,
    size_t value_capacity,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length);

hermas_runtime_result hermas_execution_prepare(
    hermas_execution *execution,
    hermas_frame *invocation);

hermas_runtime_result hermas_execution_mark_sent(
    hermas_execution *execution);

hermas_runtime_result hermas_execution_mark_not_sent(
    hermas_execution *execution);

hermas_runtime_result hermas_execution_mark_unknown(
    hermas_execution *execution);

hermas_runtime_result hermas_execution_accept_result(
    hermas_execution *execution,
    const hermas_frame *result);

hermas_runtime_result hermas_execution_get_result(
    const hermas_execution *execution,
    hermas_frame *result);

const char *hermas_runtime_result_name(hermas_runtime_result result);

#endif
