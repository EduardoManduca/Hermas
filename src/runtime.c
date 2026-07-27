#include "hermas2/runtime.h"

#include "hermas2/image.h"

#include <stdbool.h>
#include <string.h>

#define HERMAS2_HEADER_INPUT_TYPE 22u
#define HERMAS2_HEADER_SUCCESS_TYPE 24u
#define HERMAS2_HEADER_NODE_COUNT 30u
#define HERMAS2_HEADER_EDGE_COUNT 32u
#define HERMAS2_HEADER_NODES_OFFSET \
    HERMAS2_IMAGE_HEADER_NODES_OFFSET
#define HERMAS2_HEADER_EDGES_OFFSET \
    HERMAS2_IMAGE_HEADER_EDGES_OFFSET
#define HERMAS2_NODE_RECORD_SIZE HERMAS2_IMAGE_NODE_RECORD_SIZE
#define HERMAS2_EDGE_RECORD_SIZE HERMAS2_IMAGE_EDGE_RECORD_SIZE

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static void write_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)(value & 0xffu);
    bytes[offset + 1u] = (uint8_t)((value >> 8u) & 0xffu);
    bytes[offset + 2u] = (uint8_t)((value >> 16u) & 0xffu);
    bytes[offset + 3u] = (uint8_t)((value >> 24u) & 0xffu);
}

static size_t node_offset(const hermas2_execution *execution, uint16_t node) {
    return (size_t)read_u32(execution->image, HERMAS2_HEADER_NODES_OFFSET) +
           ((size_t)node - 1u) * HERMAS2_NODE_RECORD_SIZE;
}

static bool find_edge(
    const hermas2_execution *execution,
    uint8_t source_kind,
    uint16_t source_node,
    size_t *edge_offset) {
    size_t edges = read_u16(execution->image, HERMAS2_HEADER_EDGE_COUNT);
    size_t base = read_u32(execution->image, HERMAS2_HEADER_EDGES_OFFSET);
    for (size_t index = 0u; index < edges; ++index) {
        size_t offset = base + index * HERMAS2_EDGE_RECORD_SIZE;
        if (execution->image[offset] == source_kind &&
            read_u16(execution->image, offset + 4u) == source_node) {
            *edge_offset = offset;
            return true;
        }
    }
    return false;
}

static bool find_case_edge(
    const hermas2_execution *execution,
    uint16_t source_node,
    uint8_t case_tag,
    size_t *edge_offset) {
    size_t edges = read_u16(execution->image, HERMAS2_HEADER_EDGE_COUNT);
    size_t base = read_u32(execution->image, HERMAS2_HEADER_EDGES_OFFSET);
    for (size_t index = 0u; index < edges; ++index) {
        size_t offset = base + index * HERMAS2_EDGE_RECORD_SIZE;
        if (execution->image[offset] == 5u &&
            read_u16(execution->image, offset + 4u) == source_node &&
            execution->image[offset + 3u] == case_tag) {
            *edge_offset = offset;
            return true;
        }
    }
    return false;
}

static hermas2_runtime_result copy_value(
    hermas2_execution *execution,
    const uint8_t *value,
    size_t value_length) {
    if (value_length > execution->value_capacity) {
        return HERMAS2_RUNTIME_BUFFER_TOO_SMALL;
    }
    if (value_length != 0u) {
        if (value == NULL || execution->value_buffer == NULL) {
            return HERMAS2_RUNTIME_INVALID_ARGUMENT;
        }
        memmove(execution->value_buffer, value, value_length);
    }
    execution->value_length = value_length;
    return HERMAS2_RUNTIME_OK;
}

static hermas2_runtime_result enter_edge(
    hermas2_execution *execution,
    size_t edge_offset,
    uint16_t terminal_outcome);

static hermas2_runtime_result route_dispatch(
    hermas2_execution *execution,
    uint16_t terminal_outcome) {
    if (execution->value_length < 8u) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    uint32_t tag = read_u32(execution->value_buffer, 0u);
    uint32_t reserved = read_u32(execution->value_buffer, 4u);
    if (reserved != 0u || tag > UINT8_MAX) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    size_t edge = 0u;
    if (!find_case_edge(execution, execution->current_node,
                        (uint8_t)tag, &edge)) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    execution->value_length -= 8u;
    if (execution->value_length != 0u) {
        memmove(execution->value_buffer, execution->value_buffer + 8u,
                execution->value_length);
    }
    uint16_t payload_type = read_u16(execution->image, edge + 8u);
    if (hermas2_image_validate_value(
            execution->image, execution->image_size, payload_type,
            execution->value_buffer, execution->value_length) !=
        HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    return enter_edge(execution, edge, terminal_outcome);
}

static hermas2_runtime_result enter_edge(
    hermas2_execution *execution,
    size_t edge_offset,
    uint16_t terminal_outcome) {
    uint8_t target_kind = execution->image[edge_offset + 1u];
    uint16_t target_node = read_u16(execution->image, edge_offset + 6u);
    execution->value_source_type =
        read_u16(execution->image, edge_offset + 8u);
    execution->value_destination_type =
        read_u16(execution->image, edge_offset + 10u);
    execution->current_node = target_node;
    if (target_kind == 1u) {
        execution->state = HERMAS2_EXECUTION_READY;
        return HERMAS2_RUNTIME_OK;
    }
    if (target_kind == 3u) {
        return route_dispatch(execution, terminal_outcome);
    }
    execution->terminal_outcome = terminal_outcome;
    execution->state = HERMAS2_EXECUTION_COMPLETE;
    return HERMAS2_RUNTIME_OK;
}

static hermas2_runtime_result finish_delivery_outcome(
    hermas2_execution *execution,
    hermas2_execution_state required_state,
    uint8_t source_kind,
    uint16_t outcome) {
    if (execution == NULL || execution->state != required_state) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    size_t edge_offset = 0u;
    if (!find_edge(execution, source_kind, execution->current_node,
                   &edge_offset)) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    execution->value_length = 0u;
    return enter_edge(execution, edge_offset, outcome);
}

hermas2_runtime_result hermas2_execution_start(
    hermas2_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint64_t execution_id,
    uint8_t *value_buffer,
    size_t value_capacity,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length) {
    if (execution == NULL || image == NULL || execution_id == 0u ||
        input_type == 0u || (input_length != 0u && input == NULL) ||
        (value_capacity != 0u && value_buffer == NULL) ||
        input_length > HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    hermas2_image_summary summary;
    if (hermas2_image_validate(image, image_size, &summary) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    if (input_type != summary.input_type ||
        hermas2_image_validate_value(image, image_size, input_type,
                                     input, input_length) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    hermas2_execution fresh = {
        .image = image,
        .image_size = image_size,
        .value_buffer = value_buffer,
        .value_capacity = value_capacity,
        .execution_id = execution_id,
        .request_id = 1u,
        .state = HERMAS2_EXECUTION_EMPTY
    };
    hermas2_runtime_result copied = copy_value(&fresh, input, input_length);
    if (copied != HERMAS2_RUNTIME_OK) {
        return copied;
    }
    size_t root = 0u;
    if (!find_edge(&fresh, 0u, 0u, &root) ||
        fresh.image[root + 1u] != 1u) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    enter_edge(&fresh, root, HERMAS2_OUTCOME_NONE);
    *execution = fresh;
    return HERMAS2_RUNTIME_OK;
}

hermas2_runtime_result hermas2_execution_prepare(
    hermas2_execution *execution,
    hermas2_frame *invocation) {
    if (execution == NULL || invocation == NULL) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    if (execution->state != HERMAS2_EXECUTION_READY) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    size_t offset = node_offset(execution, execution->current_node);
    if (execution->image[offset] != 1u) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    *invocation = (hermas2_frame){
        .kind = HERMAS2_FRAME_INVOKE,
        .execution_id = execution->execution_id,
        .request_id = execution->request_id,
        .app_id = read_u16(execution->image, offset + 4u),
        .action_id = read_u16(execution->image, offset + 2u),
        .source_type = execution->value_source_type,
        .destination_type = execution->value_destination_type,
        .outcome = HERMAS2_OUTCOME_NONE,
        .payload = execution->value_buffer,
        .payload_length = (uint32_t)execution->value_length
    };
    execution->state = HERMAS2_EXECUTION_PREPARED;
    return HERMAS2_RUNTIME_OK;
}

hermas2_runtime_result hermas2_execution_mark_sent(
    hermas2_execution *execution) {
    if (execution == NULL || execution->state != HERMAS2_EXECUTION_PREPARED) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    execution->state = HERMAS2_EXECUTION_SENT;
    return HERMAS2_RUNTIME_OK;
}

hermas2_runtime_result hermas2_execution_mark_not_sent(
    hermas2_execution *execution) {
    return finish_delivery_outcome(execution, HERMAS2_EXECUTION_PREPARED,
                                   3u, HERMAS2_OUTCOME_NOT_SENT);
}

hermas2_runtime_result hermas2_execution_mark_unknown(
    hermas2_execution *execution) {
    return finish_delivery_outcome(execution, HERMAS2_EXECUTION_SENT,
                                   4u, HERMAS2_OUTCOME_UNKNOWN);
}

hermas2_runtime_result hermas2_execution_accept_result(
    hermas2_execution *execution,
    const hermas2_frame *result) {
    if (execution == NULL || result == NULL) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    if (execution->state != HERMAS2_EXECUTION_SENT) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    size_t node = node_offset(execution, execution->current_node);
    if (result->kind != HERMAS2_FRAME_RESULT ||
        result->execution_id != execution->execution_id ||
        result->request_id != execution->request_id ||
        result->app_id != read_u16(execution->image, node + 4u) ||
        result->action_id != read_u16(execution->image, node + 2u) ||
        result->source_type != result->destination_type ||
        (result->outcome != HERMAS2_OUTCOME_SUCCESS &&
         result->outcome != HERMAS2_OUTCOME_APP_ERROR)) {
        return HERMAS2_RUNTIME_UNEXPECTED_RESULT;
    }
    uint8_t source_kind =
        result->outcome == HERMAS2_OUTCOME_SUCCESS ? 1u : 2u;
    size_t edge = 0u;
    if (!find_edge(execution, source_kind, execution->current_node, &edge) ||
        result->source_type != read_u16(execution->image, edge + 8u)) {
        return HERMAS2_RUNTIME_UNEXPECTED_RESULT;
    }
    if (result->payload_length > HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    if (hermas2_image_validate_value(
            execution->image, execution->image_size, result->source_type,
            result->payload, result->payload_length) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    uint8_t target_kind = execution->image[edge + 1u];
    if ((target_kind == 1u || target_kind == 3u) &&
        execution->request_id == UINT64_MAX) {
        return HERMAS2_RUNTIME_REQUEST_ID_EXHAUSTED;
    }
    hermas2_runtime_result copied =
        copy_value(execution, result->payload, result->payload_length);
    if (copied != HERMAS2_RUNTIME_OK) {
        return copied;
    }
    uint16_t outcome = result->outcome;
    if (target_kind == 1u || target_kind == 3u) {
        ++execution->request_id;
    }
    return enter_edge(execution, edge, outcome);
}

hermas2_runtime_result hermas2_execution_get_result(
    const hermas2_execution *execution,
    hermas2_frame *result) {
    if (execution == NULL || result == NULL) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    if (execution->state != HERMAS2_EXECUTION_COMPLETE) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    uint16_t source_type = execution->terminal_outcome ==
                                   HERMAS2_OUTCOME_SUCCESS ||
                               execution->terminal_outcome ==
                                   HERMAS2_OUTCOME_APP_ERROR
                           ? execution->value_source_type
                           : 0u;
    uint16_t destination_type = execution->terminal_outcome ==
                                        HERMAS2_OUTCOME_SUCCESS ||
                                    execution->terminal_outcome ==
                                        HERMAS2_OUTCOME_APP_ERROR
                                ? execution->value_destination_type
                                : 0u;
    *result = (hermas2_frame){
        .kind = HERMAS2_FRAME_EXECUTION_RESULT,
        .execution_id = execution->execution_id,
        .source_type = source_type,
        .destination_type = destination_type,
        .outcome = execution->terminal_outcome,
        .payload = source_type == 0u ? NULL : execution->value_buffer,
        .payload_length =
            source_type == 0u ? 0u : (uint32_t)execution->value_length
    };
    return HERMAS2_RUNTIME_OK;
}

static bool group_find_edge(
    const hermas2_group_execution *execution,
    uint8_t source_kind,
    uint16_t source_node,
    uint8_t tag,
    size_t *edge_offset) {
    size_t edges = read_u16(execution->image, HERMAS2_HEADER_EDGE_COUNT);
    size_t base = read_u32(execution->image, HERMAS2_HEADER_EDGES_OFFSET);
    for (size_t index = 0u; index < edges; ++index) {
        size_t offset = base + index * HERMAS2_EDGE_RECORD_SIZE;
        uint8_t record_tag = execution->image[offset + 3u];
        if (execution->image[offset] == source_kind &&
            read_u16(execution->image, offset + 4u) == source_node &&
            (source_kind < 5u || record_tag == tag)) {
            *edge_offset = offset;
            return true;
        }
    }
    return false;
}

static hermas2_runtime_result group_copy_value(
    hermas2_flow *flow,
    const uint8_t *value,
    size_t value_length) {
    if (value_length > flow->value_capacity) {
        return HERMAS2_RUNTIME_BUFFER_TOO_SMALL;
    }
    if (value_length != 0u) {
        if (value == NULL || flow->value_buffer == NULL) {
            return HERMAS2_RUNTIME_INVALID_ARGUMENT;
        }
        memmove(flow->value_buffer, value, value_length);
    }
    flow->value_length = value_length;
    return HERMAS2_RUNTIME_OK;
}

static unsigned outcome_precedence(uint16_t outcome) {
    if (outcome == HERMAS2_OUTCOME_UNKNOWN) {
        return 4u;
    }
    if (outcome == HERMAS2_OUTCOME_APP_ERROR) {
        return 3u;
    }
    if (outcome == HERMAS2_OUTCOME_NOT_SENT) {
        return 2u;
    }
    return outcome == HERMAS2_OUTCOME_SUCCESS ? 1u : 0u;
}

static bool group_has_sent(const hermas2_group_execution *execution) {
    for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
        if (execution->flows[index].active != 0u &&
            execution->flows[index].state == HERMAS2_EXECUTION_SENT) {
            return true;
        }
    }
    return false;
}

static bool group_has_active(const hermas2_group_execution *execution) {
    for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
        if (execution->flows[index].active != 0u) {
            return true;
        }
    }
    return false;
}

static void group_maybe_complete(hermas2_group_execution *execution) {
    if (execution->pending_outcome == HERMAS2_OUTCOME_SUCCESS &&
        !group_has_active(execution)) {
        execution->complete = 1u;
    } else if (execution->pending_outcome != HERMAS2_OUTCOME_NONE &&
               execution->pending_outcome != HERMAS2_OUTCOME_SUCCESS &&
               !group_has_sent(execution)) {
        execution->complete = 1u;
    }
}

static void group_record_terminal(
    hermas2_group_execution *execution,
    size_t flow_index,
    uint16_t outcome) {
    if (outcome_precedence(outcome) >
        outcome_precedence(execution->pending_outcome)) {
        execution->pending_outcome = outcome;
        execution->pending_flow = (uint8_t)flow_index;
    }
    execution->flows[flow_index].active = 0u;
    if (outcome != HERMAS2_OUTCOME_SUCCESS) {
        for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
            hermas2_flow *flow = &execution->flows[index];
            if (flow->active != 0u &&
                flow->state != HERMAS2_EXECUTION_SENT) {
                flow->active = 0u;
            }
        }
    }
    group_maybe_complete(execution);
}

static hermas2_runtime_result group_enter_edge(
    hermas2_group_execution *execution,
    size_t flow_index,
    size_t edge_offset,
    uint16_t terminal_outcome);

static hermas2_runtime_result group_each_admit(
    hermas2_group_execution *execution);

static hermas2_runtime_result group_each_finish(
    hermas2_group_execution *execution) {
    uint8_t region_id = execution->active_each_region;
    if (region_id == 0u || region_id > execution->each_region_count) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    size_t coordinator = execution->each_coordinator_flow;
    hermas2_flow *flow = &execution->flows[coordinator];
    hermas2_runtime_result copied = group_copy_value(
        flow, execution->each_collection,
        execution->each_collection_length);
    if (copied != HERMAS2_RUNTIME_OK) {
        return copied;
    }
    uint8_t region = (uint8_t)(region_id - 1u);
    if (hermas2_image_validate_value(
            execution->image, execution->image_size,
            execution->each_collected_types[region], flow->value_buffer,
            flow->value_length) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    flow->active = 1u;
    flow->each_region = 0u;
    execution->active_each_region = 0u;
    size_t edge = 0u;
    if (!group_find_edge(execution, 9u, region_id, 0u, &edge)) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    return group_enter_edge(
        execution, coordinator, edge, HERMAS2_OUTCOME_SUCCESS);
}

static hermas2_runtime_result group_each_collect(
    hermas2_group_execution *execution,
    size_t flow_index,
    uint8_t region_id) {
    if (region_id == 0u ||
        region_id != execution->active_each_region ||
        flow_index >= HERMAS2_RUNTIME_MAX_FLOWS) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    hermas2_flow *flow = &execution->flows[flow_index];
    if (flow->each_region != region_id ||
        flow->item_index >= execution->each_item_count) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    flow->state = HERMAS2_EXECUTION_WAITING;
    ++execution->each_completed_items;

    for (;;) {
        size_t ready = HERMAS2_RUNTIME_MAX_FLOWS;
        for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
            hermas2_flow *candidate = &execution->flows[index];
            if (candidate->active != 0u &&
                candidate->each_region == region_id &&
                candidate->state == HERMAS2_EXECUTION_WAITING &&
                candidate->item_index == execution->each_flush_item) {
                ready = index;
                break;
            }
        }
        if (ready == HERMAS2_RUNTIME_MAX_FLOWS) {
            break;
        }
        hermas2_flow *item = &execution->flows[ready];
        if (item->value_length >
            sizeof(execution->each_collection) -
                execution->each_collection_length) {
            return HERMAS2_RUNTIME_BUFFER_TOO_SMALL;
        }
        memmove(execution->each_collection +
                    execution->each_collection_length,
                item->value_buffer, item->value_length);
        execution->each_collection_length += item->value_length;
        item->active = 0u;
        item->each_region = 0u;
        ++execution->each_flush_item;
    }
    if (execution->each_flush_item == execution->each_item_count) {
        return group_each_finish(execution);
    }
    return group_each_admit(execution);
}

static hermas2_runtime_result group_each_admit(
    hermas2_group_execution *execution) {
    uint8_t region_id = execution->active_each_region;
    if (region_id == 0u || region_id > execution->each_region_count) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    uint8_t region = (uint8_t)(region_id - 1u);
    for (;;) {
        size_t active = 0u;
        for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
            const hermas2_flow *flow = &execution->flows[index];
            if (flow->active != 0u && flow->each_region == region_id) {
                ++active;
            }
        }
        if (active >= execution->each_concurrency[region] ||
            execution->each_next_item >= execution->each_item_count) {
            return HERMAS2_RUNTIME_OK;
        }
        size_t slot = HERMAS2_RUNTIME_MAX_FLOWS;
        for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
            if (execution->flows[index].active == 0u) {
                slot = index;
                break;
            }
        }
        if (slot == HERMAS2_RUNTIME_MAX_FLOWS) {
            return HERMAS2_RUNTIME_INVALID_STATE;
        }
        uint16_t item_index = execution->each_next_item++;
        hermas2_flow *flow = &execution->flows[slot];
        flow->active = 1u;
        flow->each_region = region_id;
        flow->item_index = item_index;
        hermas2_runtime_result copied = group_copy_value(
            flow,
            execution->each_source_snapshot +
                execution->each_item_offsets[item_index],
            execution->each_item_lengths[item_index]);
        if (copied != HERMAS2_RUNTIME_OK) {
            return copied;
        }
        size_t edge = 0u;
        if (!group_find_edge(execution, 8u, region_id, 0u, &edge)) {
            return HERMAS2_RUNTIME_INVALID_IMAGE;
        }
        hermas2_runtime_result entered = group_enter_edge(
            execution, slot, edge, HERMAS2_OUTCOME_NONE);
        if (entered != HERMAS2_RUNTIME_OK) {
            return entered;
        }
    }
}

static hermas2_runtime_result group_each_start(
    hermas2_group_execution *execution,
    size_t flow_index,
    uint8_t region_id) {
    if (region_id == 0u || region_id > execution->each_region_count ||
        execution->active_each_region != 0u) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    uint8_t region = (uint8_t)(region_id - 1u);
    hermas2_flow *source = &execution->flows[flow_index];
    if (source->value_length > sizeof(execution->each_source_snapshot)) {
        return HERMAS2_RUNTIME_BUFFER_TOO_SMALL;
    }
    memmove(execution->each_source_snapshot, source->value_buffer,
            source->value_length);
    uint16_t item_count = 0u;
    if (hermas2_image_list_items(
            execution->image, execution->image_size,
            execution->each_source_types[region],
            execution->each_source_snapshot, source->value_length,
            &item_count, execution->each_item_offsets,
            execution->each_item_lengths,
            HERMAS2_RUNTIME_MAX_EACH_ITEMS) != HERMAS2_IMAGE_OK ||
        item_count > execution->each_bounds[region]) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    execution->active_each_region = region_id;
    execution->each_coordinator_flow = (uint8_t)flow_index;
    execution->each_item_count = item_count;
    execution->each_next_item = 0u;
    execution->each_completed_items = 0u;
    execution->each_flush_item = 0u;
    execution->each_collection_length = 8u;
    write_u32(execution->each_collection, 0u, item_count);
    write_u32(execution->each_collection, 4u, 0u);
    source->active = 0u;
    source->each_region = 0u;
    if (item_count == 0u) {
        return group_each_finish(execution);
    }
    return group_each_admit(execution);
}

static hermas2_runtime_result group_route_dispatch(
    hermas2_group_execution *execution,
    size_t flow_index,
    uint16_t dispatch_node,
    uint16_t terminal_outcome) {
    hermas2_flow *flow = &execution->flows[flow_index];
    if (flow->value_length < 8u) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    uint32_t tag = read_u32(flow->value_buffer, 0u);
    if (read_u32(flow->value_buffer, 4u) != 0u || tag > UINT8_MAX) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    size_t edge = 0u;
    if (!group_find_edge(
            execution, 5u, dispatch_node, (uint8_t)tag, &edge)) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    flow->value_length -= 8u;
    if (flow->value_length != 0u) {
        memmove(flow->value_buffer, flow->value_buffer + 8u,
                flow->value_length);
    }
    uint16_t payload_type = read_u16(execution->image, edge + 8u);
    if (hermas2_image_validate_value(
            execution->image, execution->image_size, payload_type,
            flow->value_buffer, flow->value_length) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    return group_enter_edge(
        execution, flow_index, edge, terminal_outcome);
}

static hermas2_runtime_result group_resolve_join(
    hermas2_group_execution *execution,
    uint16_t join_node) {
    size_t offset = (size_t)read_u32(
                        execution->image, HERMAS2_HEADER_NODES_OFFSET) +
                    ((size_t)join_node - 1u) * HERMAS2_NODE_RECORD_SIZE;
    if (execution->image[offset] != 5u) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    uint8_t branch_count = execution->image[offset + 1u];
    size_t branch_flows[HERMAS2_RUNTIME_MAX_FLOWS];
    for (uint8_t tag = 0u; tag < branch_count; ++tag) {
        bool found = false;
        for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
            hermas2_flow *flow = &execution->flows[index];
            if (flow->active != 0u &&
                flow->state == HERMAS2_EXECUTION_WAITING &&
                flow->join_node == join_node && flow->join_tag == tag) {
                branch_flows[tag] = index;
                found = true;
                break;
            }
        }
        if (!found) {
            return HERMAS2_RUNTIME_OK;
        }
    }
    for (uint8_t tag = 0u; tag < branch_count; ++tag) {
        size_t index = branch_flows[tag];
        size_t edge = 0u;
        if (group_find_edge(execution, 7u, join_node, tag, &edge)) {
            hermas2_runtime_result entered = group_enter_edge(
                execution, index, edge, HERMAS2_OUTCOME_SUCCESS);
            if (entered != HERMAS2_RUNTIME_OK) {
                return entered;
            }
        } else {
            execution->flows[index].active = 0u;
        }
    }
    group_maybe_complete(execution);
    return HERMAS2_RUNTIME_OK;
}

static hermas2_runtime_result group_enter_fork(
    hermas2_group_execution *execution,
    size_t source_flow,
    uint16_t fork_node) {
    size_t offset = (size_t)read_u32(
                        execution->image, HERMAS2_HEADER_NODES_OFFSET) +
                    ((size_t)fork_node - 1u) * HERMAS2_NODE_RECORD_SIZE;
    if (execution->image[offset] != 4u) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    uint8_t branch_count = execution->image[offset + 1u];
    size_t branch_flows[HERMAS2_RUNTIME_MAX_FLOWS];
    branch_flows[0] = source_flow;
    for (uint8_t branch = 1u; branch < branch_count; ++branch) {
        bool found = false;
        for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
            if (execution->flows[index].active == 0u && index != source_flow) {
                branch_flows[branch] = index;
                found = true;
                break;
            }
        }
        if (!found) {
            return HERMAS2_RUNTIME_INVALID_STATE;
        }
        hermas2_flow *destination = &execution->flows[branch_flows[branch]];
        destination->active = 1u;
        hermas2_runtime_result copied = group_copy_value(
            destination, execution->flows[source_flow].value_buffer,
            execution->flows[source_flow].value_length);
        if (copied != HERMAS2_RUNTIME_OK) {
            return copied;
        }
    }
    for (uint8_t branch = 0u; branch < branch_count; ++branch) {
        size_t edge = 0u;
        if (!group_find_edge(execution, 6u, fork_node, branch, &edge)) {
            return HERMAS2_RUNTIME_INVALID_IMAGE;
        }
        hermas2_runtime_result entered = group_enter_edge(
            execution, branch_flows[branch], edge, HERMAS2_OUTCOME_NONE);
        if (entered != HERMAS2_RUNTIME_OK) {
            return entered;
        }
    }
    return HERMAS2_RUNTIME_OK;
}

static hermas2_runtime_result group_enter_edge(
    hermas2_group_execution *execution,
    size_t flow_index,
    size_t edge_offset,
    uint16_t terminal_outcome) {
    hermas2_flow *flow = &execution->flows[flow_index];
    uint8_t target_kind = execution->image[edge_offset + 1u];
    uint16_t target_node = read_u16(execution->image, edge_offset + 6u);
    flow->value_source_type = read_u16(execution->image, edge_offset + 8u);
    flow->value_destination_type =
        read_u16(execution->image, edge_offset + 10u);
    flow->current_node = target_node;
    if (target_kind == 1u) {
        flow->state = HERMAS2_EXECUTION_READY;
        flow->active = 1u;
        return HERMAS2_RUNTIME_OK;
    }
    if (target_kind == 4u) {
        return group_enter_fork(execution, flow_index, target_node);
    }
    if (target_kind == 3u) {
        return group_route_dispatch(
            execution, flow_index, target_node, terminal_outcome);
    }
    if (target_kind == 5u) {
        flow->join_node = target_node;
        flow->join_tag = execution->image[edge_offset + 3u];
        flow->state = HERMAS2_EXECUTION_WAITING;
        return group_resolve_join(execution, target_node);
    }
    if (target_kind == 6u) {
        return group_each_start(
            execution, flow_index, (uint8_t)target_node);
    }
    if (target_kind == 7u) {
        return group_each_collect(
            execution, flow_index, (uint8_t)target_node);
    }
    if (target_kind == 2u) {
        group_record_terminal(execution, flow_index, terminal_outcome);
        return HERMAS2_RUNTIME_OK;
    }
    return HERMAS2_RUNTIME_INVALID_IMAGE;
}

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
    size_t input_length) {
    if (execution == NULL || image == NULL || execution_id == 0u ||
        value_storage == NULL || value_stride == 0u ||
        value_stride > HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE ||
        value_storage_capacity / HERMAS2_RUNTIME_MAX_FLOWS < value_stride ||
        input_length > value_stride ||
        (input_length != 0u && input == NULL)) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    hermas2_image_summary summary;
    if (hermas2_image_validate(image, image_size, &summary) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    if (input_type != summary.input_type ||
        hermas2_image_validate_value(image, image_size, input_type,
                                     input, input_length) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    memset(execution, 0, sizeof(*execution));
    execution->image = image;
    execution->image_size = image_size;
    execution->execution_id = execution_id;
    execution->next_request_id = 1u;
    execution->value_stride = value_stride;
    uint16_t image_region_count = read_u16(
        image, HERMAS2_IMAGE_HEADER_REGION_COUNT_OFFSET);
    size_t regions =
        read_u32(image, HERMAS2_IMAGE_HEADER_REGIONS_OFFSET);
    for (uint16_t index = 0u; index < image_region_count; ++index) {
        size_t region =
            regions + (size_t)index *
                          HERMAS2_IMAGE_REGION_RECORD_SIZE;
        if (image[region] == 1u) {
            uint8_t deadline = execution->region_count++;
            execution->region_deadlines_ms[deadline] =
                read_u64(image, region + 8u);
            execution->region_first_nodes[deadline] =
                read_u16(image, region + 2u);
            execution->region_last_nodes[deadline] =
                (uint16_t)(execution->region_first_nodes[deadline] +
                           read_u16(image, region + 4u) - 1u);
        } else if (image[region] == 2u) {
            uint8_t each = execution->each_region_count++;
            execution->each_concurrency[each] = image[region + 1u];
            execution->each_template_nodes[each] =
                read_u16(image, region + 2u);
            execution->each_source_types[each] =
                read_u16(image, region + 4u);
            execution->each_item_input_types[each] =
                read_u16(image, region + 6u);
            execution->each_item_output_types[each] =
                read_u16(image, region + 8u);
            execution->each_collected_types[each] =
                read_u16(image, region + 10u);
            execution->each_bounds[each] =
                read_u16(image, region + 12u);
        }
    }
    execution->deadline_ms = execution->region_count == 0u
                                 ? 0u
                                 : execution->region_deadlines_ms[0];
    for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
        execution->flows[index].value_buffer =
            value_storage + index * value_stride;
        execution->flows[index].value_capacity = value_stride;
    }
    execution->flows[0].active = 1u;
    hermas2_runtime_result copied =
        group_copy_value(&execution->flows[0], input, input_length);
    if (copied != HERMAS2_RUNTIME_OK) {
        return copied;
    }
    size_t root = 0u;
    if (!group_find_edge(execution, 0u, 0u, 0u, &root)) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    return group_enter_edge(
        execution, 0u, root, HERMAS2_OUTCOME_NONE);
}

hermas2_runtime_result hermas2_group_prepare(
    hermas2_group_execution *execution,
    size_t flow_index,
    hermas2_frame *invocation) {
    if (execution == NULL || invocation == NULL ||
        flow_index >= HERMAS2_RUNTIME_MAX_FLOWS) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    hermas2_flow *flow = &execution->flows[flow_index];
    if (execution->complete != 0u || flow->active == 0u ||
        flow->state != HERMAS2_EXECUTION_READY) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    if (execution->next_request_id == 0u) {
        return HERMAS2_RUNTIME_REQUEST_ID_EXHAUSTED;
    }
    size_t offset = (size_t)read_u32(
                        execution->image, HERMAS2_HEADER_NODES_OFFSET) +
                    ((size_t)flow->current_node - 1u) *
                        HERMAS2_NODE_RECORD_SIZE;
    if (execution->image[offset] != 1u) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    uint16_t app_id = read_u16(execution->image, offset + 4u);
    uint16_t action_id = read_u16(execution->image, offset + 2u);
    for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
        const hermas2_flow *other = &execution->flows[index];
        if (index == flow_index || other->active == 0u ||
            (other->state != HERMAS2_EXECUTION_PREPARED &&
             other->state != HERMAS2_EXECUTION_SENT)) {
            continue;
        }
        size_t other_node =
            (size_t)read_u32(execution->image,
                             HERMAS2_HEADER_NODES_OFFSET) +
            ((size_t)other->current_node - 1u) * HERMAS2_NODE_RECORD_SIZE;
        if (execution->image[other_node] != 1u) {
            return HERMAS2_RUNTIME_INVALID_IMAGE;
        }
        if (read_u16(execution->image, other_node + 4u) == app_id &&
            read_u16(execution->image, other_node + 2u) == action_id) {
            return HERMAS2_RUNTIME_INVALID_STATE;
        }
    }
    flow->request_id = execution->next_request_id++;
    *invocation = (hermas2_frame){
        .kind = HERMAS2_FRAME_INVOKE,
        .execution_id = execution->execution_id,
        .request_id = flow->request_id,
        .app_id = app_id,
        .action_id = action_id,
        .source_type = flow->value_source_type,
        .destination_type = flow->value_destination_type,
        .outcome = HERMAS2_OUTCOME_NONE,
        .payload = flow->value_buffer,
        .payload_length = (uint32_t)flow->value_length
    };
    flow->state = HERMAS2_EXECUTION_PREPARED;
    return HERMAS2_RUNTIME_OK;
}

hermas2_runtime_result hermas2_group_mark_sent(
    hermas2_group_execution *execution,
    size_t flow_index) {
    if (execution == NULL || flow_index >= HERMAS2_RUNTIME_MAX_FLOWS ||
        execution->flows[flow_index].active == 0u ||
        execution->flows[flow_index].state != HERMAS2_EXECUTION_PREPARED) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    execution->flows[flow_index].state = HERMAS2_EXECUTION_SENT;
    return HERMAS2_RUNTIME_OK;
}

static hermas2_runtime_result group_delivery_outcome(
    hermas2_group_execution *execution,
    size_t flow_index,
    hermas2_execution_state required,
    uint8_t source_kind,
    uint16_t outcome) {
    if (execution == NULL || flow_index >= HERMAS2_RUNTIME_MAX_FLOWS ||
        execution->flows[flow_index].active == 0u ||
        execution->flows[flow_index].state != required) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    hermas2_flow *flow = &execution->flows[flow_index];
    size_t edge = 0u;
    if (!group_find_edge(execution, source_kind, flow->current_node, 0u,
                         &edge)) {
        return HERMAS2_RUNTIME_INVALID_IMAGE;
    }
    flow->value_length = 0u;
    return group_enter_edge(execution, flow_index, edge, outcome);
}

hermas2_runtime_result hermas2_group_mark_not_sent(
    hermas2_group_execution *execution,
    size_t flow_index) {
    return group_delivery_outcome(
        execution, flow_index, HERMAS2_EXECUTION_PREPARED, 3u,
        HERMAS2_OUTCOME_NOT_SENT);
}

hermas2_runtime_result hermas2_group_mark_unknown(
    hermas2_group_execution *execution,
    size_t flow_index) {
    return group_delivery_outcome(
        execution, flow_index, HERMAS2_EXECUTION_SENT, 4u,
        HERMAS2_OUTCOME_UNKNOWN);
}

hermas2_runtime_result hermas2_group_accept_result(
    hermas2_group_execution *execution,
    size_t flow_index,
    const hermas2_frame *result) {
    if (execution == NULL || result == NULL ||
        flow_index >= HERMAS2_RUNTIME_MAX_FLOWS) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    hermas2_flow *flow = &execution->flows[flow_index];
    if (flow->active == 0u || flow->state != HERMAS2_EXECUTION_SENT) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    size_t node = (size_t)read_u32(
                      execution->image, HERMAS2_HEADER_NODES_OFFSET) +
                  ((size_t)flow->current_node - 1u) *
                      HERMAS2_NODE_RECORD_SIZE;
    if (result->kind != HERMAS2_FRAME_RESULT ||
        result->execution_id != execution->execution_id ||
        result->request_id != flow->request_id ||
        result->app_id != read_u16(execution->image, node + 4u) ||
        result->action_id != read_u16(execution->image, node + 2u) ||
        result->source_type != result->destination_type ||
        (result->outcome != HERMAS2_OUTCOME_SUCCESS &&
         result->outcome != HERMAS2_OUTCOME_APP_ERROR)) {
        return HERMAS2_RUNTIME_UNEXPECTED_RESULT;
    }
    uint8_t source_kind =
        result->outcome == HERMAS2_OUTCOME_SUCCESS ? 1u : 2u;
    size_t edge = 0u;
    if (!group_find_edge(execution, source_kind, flow->current_node, 0u,
                         &edge) ||
        result->source_type != read_u16(execution->image, edge + 8u)) {
        return HERMAS2_RUNTIME_UNEXPECTED_RESULT;
    }
    if (result->payload_length > flow->value_capacity ||
        hermas2_image_validate_value(
            execution->image, execution->image_size, result->source_type,
            result->payload, result->payload_length) != HERMAS2_IMAGE_OK) {
        return HERMAS2_RUNTIME_INVALID_VALUE;
    }
    hermas2_runtime_result copied =
        group_copy_value(flow, result->payload, result->payload_length);
    if (copied != HERMAS2_RUNTIME_OK) {
        return copied;
    }
    return group_enter_edge(
        execution, flow_index, edge, result->outcome);
}

hermas2_runtime_result hermas2_group_get_result(
    const hermas2_group_execution *execution,
    hermas2_frame *result) {
    if (execution == NULL || result == NULL) {
        return HERMAS2_RUNTIME_INVALID_ARGUMENT;
    }
    if (execution->complete == 0u ||
        execution->pending_flow >= HERMAS2_RUNTIME_MAX_FLOWS) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    const hermas2_flow *flow =
        &execution->flows[execution->pending_flow];
    bool typed = execution->pending_outcome == HERMAS2_OUTCOME_SUCCESS ||
                 execution->pending_outcome == HERMAS2_OUTCOME_APP_ERROR;
    *result = (hermas2_frame){
        .kind = HERMAS2_FRAME_EXECUTION_RESULT,
        .execution_id = execution->execution_id,
        .source_type = typed ? flow->value_source_type : 0u,
        .destination_type = typed ? flow->value_destination_type : 0u,
        .outcome = execution->pending_outcome,
        .payload = typed ? flow->value_buffer : NULL,
        .payload_length = typed ? (uint32_t)flow->value_length : 0u
    };
    return HERMAS2_RUNTIME_OK;
}

uint64_t hermas2_group_deadline_ms(
    const hermas2_group_execution *execution) {
    return execution == NULL ? 0u : execution->deadline_ms;
}

hermas2_runtime_result hermas2_group_expire(
    hermas2_group_execution *execution) {
    return hermas2_group_expire_region(execution, 1u);
}

uint64_t hermas2_group_region_deadline_ms(
    const hermas2_group_execution *execution,
    uint8_t region_id) {
    if (execution == NULL || region_id == 0u ||
        region_id > execution->region_count) {
        return 0u;
    }
    return execution->region_deadlines_ms[region_id - 1u];
}

static bool flow_in_region(
    const hermas2_group_execution *execution,
    size_t flow_index,
    uint8_t region_id) {
    const hermas2_flow *flow = &execution->flows[flow_index];
    return flow->active != 0u &&
           flow->current_node >=
               execution->region_first_nodes[region_id - 1u] &&
           flow->current_node <=
               execution->region_last_nodes[region_id - 1u];
}

hermas2_runtime_result hermas2_group_expire_region(
    hermas2_group_execution *execution,
    uint8_t region_id) {
    if (execution == NULL || region_id == 0u ||
        region_id > execution->region_count ||
        execution->complete != 0u) {
        return HERMAS2_RUNTIME_INVALID_STATE;
    }
    bool has_sent = false;
    for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
        has_sent = has_sent ||
                   (flow_in_region(execution, index, region_id) &&
                    execution->flows[index].state ==
                        HERMAS2_EXECUTION_SENT);
    }
    if (has_sent) {
        for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
            if (flow_in_region(execution, index, region_id) &&
                execution->flows[index].state == HERMAS2_EXECUTION_SENT) {
                hermas2_runtime_result result =
                    hermas2_group_mark_unknown(execution, index);
                if (result != HERMAS2_RUNTIME_OK) {
                    return result;
                }
            }
        }
        return HERMAS2_RUNTIME_OK;
    }
    for (size_t index = 0u; index < HERMAS2_RUNTIME_MAX_FLOWS; ++index) {
        hermas2_flow *flow = &execution->flows[index];
        if (!flow_in_region(execution, index, region_id)) {
            continue;
        }
        if (flow->state == HERMAS2_EXECUTION_PREPARED) {
            return hermas2_group_mark_not_sent(execution, index);
        }
        size_t edge = 0u;
        if (flow->state == HERMAS2_EXECUTION_READY &&
            group_find_edge(execution, 3u, flow->current_node, 0u, &edge)) {
            flow->value_length = 0u;
            return group_enter_edge(
                execution, index, edge, HERMAS2_OUTCOME_NOT_SENT);
        }
        group_record_terminal(
            execution, index, HERMAS2_OUTCOME_NOT_SENT);
        return HERMAS2_RUNTIME_OK;
    }
    return HERMAS2_RUNTIME_INVALID_STATE;
}

const char *hermas2_runtime_result_name(hermas2_runtime_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "invalid-image", "invalid-value",
        "buffer-too-small", "invalid-state", "unexpected-result",
        "request-id-exhausted"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
