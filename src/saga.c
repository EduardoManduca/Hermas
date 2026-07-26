#include "hermas2/saga.h"

#include "hermas2/image.h"

#include <stdbool.h>
#include <string.h>

#define HEADER_NODE_COUNT 30u
#define HEADER_REGION_COUNT 68u
#define HEADER_NODES_OFFSET 48u
#define HEADER_REGIONS_OFFSET 72u
#define NODE_RECORD_SIZE 8u
#define REGION_RECORD_SIZE 16u

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    uint32_t value = 0u;
    for (size_t index = 0u; index < 4u; ++index) {
        value |= (uint32_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static bool load_steps(hermas2_saga_execution *execution) {
    uint16_t regions = read_u16(execution->image, HEADER_REGION_COUNT);
    size_t base = read_u32(execution->image, HEADER_REGIONS_OFFSET);
    for (uint16_t index = 0u; index < regions; ++index) {
        size_t offset = base + (size_t)index * REGION_RECORD_SIZE;
        if (execution->image[offset] != 3u) {
            continue;
        }
        uint16_t ordinal = read_u16(execution->image, offset + 12u);
        if (ordinal == 0u || ordinal > HERMAS2_SAGA_MAX_STEPS ||
            index + 1u >= regions) {
            return false;
        }
        size_t outcome = offset + REGION_RECORD_SIZE;
        if (execution->image[outcome] != 4u ||
            read_u16(execution->image, outcome + 2u) !=
                read_u16(execution->image, offset + 2u)) {
            return false;
        }
        hermas2_saga_step *step = &execution->steps[ordinal - 1u];
        step->forward_node = read_u16(execution->image, offset + 2u);
        step->compensation_app_id =
            read_u16(execution->image, offset + 4u);
        step->compensation_action_id =
            read_u16(execution->image, offset + 6u);
        step->source_type = read_u16(execution->image, offset + 8u);
        step->destination_type =
            read_u16(execution->image, offset + 10u);
        step->ordinal = ordinal;
        step->success_type = read_u16(execution->image, outcome + 4u);
        step->error_type = read_u16(execution->image, outcome + 6u);
        ++execution->step_count;
    }
    return execution->step_count != 0u;
}

static hermas2_saga_step *find_step(
    hermas2_saga_execution *execution,
    uint16_t node) {
    for (uint8_t index = 0u; index < execution->step_count; ++index) {
        if (execution->steps[index].forward_node == node) {
            return &execution->steps[index];
        }
    }
    return NULL;
}

static bool forward_route_matches(
    const hermas2_saga_execution *execution,
    const hermas2_saga_step *step,
    const hermas2_journal_record *record) {
    size_t nodes = read_u32(execution->image, HEADER_NODES_OFFSET);
    size_t node = nodes +
                  ((size_t)step->forward_node - 1u) * NODE_RECORD_SIZE;
    return record->app_id == read_u16(execution->image, node + 4u) &&
           record->action_id == read_u16(execution->image, node + 2u);
}

static hermas2_saga_result validate_token(
    const hermas2_saga_execution *execution,
    const hermas2_saga_step *step,
    uint8_t *buffer,
    size_t capacity,
    hermas2_compensation_record *record) {
    hermas2_compensation_key key = {
        .execution_id = execution->execution_id,
        .workflow_id = execution->workflow_id,
        .request_id = step->forward_request_id,
        .node_id = step->forward_node,
        .image_fingerprint = execution->image_fingerprint
    };
    int found = 0;
    hermas2_compensation_result found_result = hermas2_compensation_find(
        execution->tokens, execution->token_bytes, key, record,
        buffer, capacity, &found);
    if (found_result == HERMAS2_COMPENSATION_DUPLICATE_TOKEN) {
        return HERMAS2_SAGA_DUPLICATE_TOKEN;
    }
    if (found_result == HERMAS2_COMPENSATION_BUFFER_TOO_SMALL) {
        return HERMAS2_SAGA_BUFFER_TOO_SMALL;
    }
    if (found_result != HERMAS2_COMPENSATION_OK) {
        return HERMAS2_SAGA_INVALID_TOKEN;
    }
    if (found == 0) {
        return HERMAS2_SAGA_MISSING_TOKEN;
    }
    if (record->compensation_app_id != step->compensation_app_id ||
        record->compensation_action_id != step->compensation_action_id ||
        record->source_type != step->source_type ||
        record->destination_type != step->destination_type ||
        hermas2_image_validate_value(
            execution->image, execution->image_size,
            step->destination_type, record->token,
            record->token_length) != HERMAS2_IMAGE_OK) {
        return HERMAS2_SAGA_INVALID_TOKEN;
    }
    return HERMAS2_SAGA_OK;
}

hermas2_saga_result hermas2_saga_recover(
    hermas2_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    const uint8_t *journal,
    size_t journal_bytes,
    const uint8_t *tokens,
    size_t token_bytes,
    uint64_t execution_id,
    uint32_t workflow_id) {
    if (execution == NULL || image == NULL ||
        (journal == NULL && journal_bytes != 0u) ||
        (tokens == NULL && token_bytes != 0u) ||
        execution_id == 0u || workflow_id == 0u) {
        return HERMAS2_SAGA_INVALID_ARGUMENT;
    }
    if (hermas2_image_validate(image, image_size, NULL) !=
        HERMAS2_IMAGE_OK) {
        return HERMAS2_SAGA_INVALID_IMAGE;
    }
    hermas2_journal_summary summary;
    if (hermas2_journal_scan(journal, journal_bytes, NULL, NULL,
                             &summary) != HERMAS2_JOURNAL_OK) {
        return HERMAS2_SAGA_INCONSISTENT_HISTORY;
    }
    hermas2_compensation_summary token_summary;
    if (hermas2_compensation_scan(tokens, token_bytes, NULL, NULL,
                                  &token_summary) !=
        HERMAS2_COMPENSATION_OK) {
        return HERMAS2_SAGA_INVALID_TOKEN;
    }
    memset(execution, 0, sizeof(*execution));
    execution->image = image;
    execution->image_size = image_size;
    execution->tokens = tokens;
    execution->token_bytes = token_bytes;
    execution->execution_id = execution_id;
    execution->workflow_id = workflow_id;
    execution->image_fingerprint =
        hermas2_journal_image_fingerprint(image, image_size);
    if (!load_steps(execution)) {
        return HERMAS2_SAGA_NOT_SAGA;
    }

    bool started = false;
    bool finished = false;
    bool terminal_action = false;
    uint64_t largest_request = 0u;
    size_t record_count =
        journal_bytes / HERMAS2_JOURNAL_RECORD_SIZE;
    for (size_t index = 0u; index < record_count; ++index) {
        hermas2_journal_record record;
        if (hermas2_journal_decode(
                journal + index * HERMAS2_JOURNAL_RECORD_SIZE,
                HERMAS2_JOURNAL_RECORD_SIZE, &record) !=
            HERMAS2_JOURNAL_OK ||
            record.execution_id != execution_id) {
            continue;
        }
        if (record.workflow_id != workflow_id ||
            record.image_fingerprint != execution->image_fingerprint) {
            return HERMAS2_SAGA_INCONSISTENT_HISTORY;
        }
        if (record.kind == HERMAS2_JOURNAL_EXECUTION_STARTED) {
            if (started) {
                return HERMAS2_SAGA_INCONSISTENT_HISTORY;
            }
            started = true;
            continue;
        }
        if (!started || finished) {
            return HERMAS2_SAGA_INCONSISTENT_HISTORY;
        }
        if (record.request_id > largest_request) {
            largest_request = record.request_id;
        }
        if (record.kind == HERMAS2_JOURNAL_ACTION_SUCCEEDED) {
            hermas2_saga_step *step =
                find_step(execution, record.node_id);
            if (terminal_action || step == NULL ||
                step->ordinal != execution->remaining + 1u ||
                step->forward_request_id != 0u ||
                !forward_route_matches(execution, step, &record)) {
                return HERMAS2_SAGA_INCONSISTENT_HISTORY;
            }
            step->forward_request_id = record.request_id;
            ++execution->remaining;
        } else if (record.kind == HERMAS2_JOURNAL_ACTION_FAILED ||
                   record.kind == HERMAS2_JOURNAL_ACTION_UNKNOWN) {
            hermas2_saga_step *step =
                find_step(execution, record.node_id);
            if (terminal_action || step == NULL ||
                step->ordinal != execution->remaining + 1u ||
                !forward_route_matches(execution, step, &record)) {
                return HERMAS2_SAGA_INCONSISTENT_HISTORY;
            }
            terminal_action = true;
            execution->original_outcome = record.outcome;
        } else if (record.kind ==
                   HERMAS2_JOURNAL_EXECUTION_FINISHED) {
            bool successful =
                !terminal_action &&
                record.outcome == HERMAS2_OUTCOME_SUCCESS &&
                execution->remaining == execution->step_count;
            if (!successful &&
                (!terminal_action ||
                 execution->original_outcome != record.outcome)) {
                return HERMAS2_SAGA_INCONSISTENT_HISTORY;
            }
            execution->original_outcome = record.outcome;
            finished = true;
        }
    }
    if (!started || !finished || largest_request == UINT64_MAX) {
        return HERMAS2_SAGA_INCONSISTENT_HISTORY;
    }
    if (execution->original_outcome == HERMAS2_OUTCOME_UNKNOWN) {
        execution->state = HERMAS2_SAGA_BLOCKED;
        return HERMAS2_SAGA_UNSAFE_HISTORY;
    }
    if (execution->original_outcome == HERMAS2_OUTCOME_SUCCESS) {
        execution->remaining = 0u;
        execution->state = HERMAS2_SAGA_COMPLETE;
        return HERMAS2_SAGA_OK;
    }
    if (execution->original_outcome != HERMAS2_OUTCOME_APP_ERROR &&
        execution->original_outcome != HERMAS2_OUTCOME_NOT_SENT) {
        return HERMAS2_SAGA_INCONSISTENT_HISTORY;
    }
    execution->next_request_id = largest_request + 1u;
    for (uint8_t index = 0u; index < execution->remaining; ++index) {
        uint8_t scratch[HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
        hermas2_compensation_record token;
        hermas2_saga_result valid = validate_token(
            execution, &execution->steps[index], scratch,
            sizeof(scratch), &token);
        if (valid != HERMAS2_SAGA_OK) {
            return valid;
        }
    }
    execution->state = execution->remaining == 0u
                           ? HERMAS2_SAGA_COMPLETE
                           : HERMAS2_SAGA_READY;
    return HERMAS2_SAGA_OK;
}

hermas2_saga_result hermas2_saga_prepare(
    hermas2_saga_execution *execution,
    uint8_t *token_buffer,
    size_t token_capacity,
    hermas2_frame *invocation) {
    if (execution == NULL || invocation == NULL ||
        (token_buffer == NULL && token_capacity != 0u)) {
        return HERMAS2_SAGA_INVALID_ARGUMENT;
    }
    if (execution->state != HERMAS2_SAGA_READY ||
        execution->remaining == 0u) {
        return HERMAS2_SAGA_INVALID_STATE;
    }
    if (execution->next_request_id == 0u ||
        execution->next_request_id == UINT64_MAX) {
        return HERMAS2_SAGA_REQUEST_ID_EXHAUSTED;
    }
    hermas2_saga_step *step =
        &execution->steps[execution->remaining - 1u];
    hermas2_compensation_record token;
    hermas2_saga_result valid = validate_token(
        execution, step, token_buffer, token_capacity, &token);
    if (valid != HERMAS2_SAGA_OK) {
        return valid;
    }
    execution->current_request_id = execution->next_request_id++;
    *invocation = (hermas2_frame){
        .kind = HERMAS2_FRAME_INVOKE,
        .execution_id = execution->execution_id,
        .request_id = execution->current_request_id,
        .app_id = step->compensation_app_id,
        .action_id = step->compensation_action_id,
        .source_type = step->source_type,
        .destination_type = step->destination_type,
        .outcome = HERMAS2_OUTCOME_NONE,
        .payload = token_buffer,
        .payload_length = token.token_length
    };
    execution->state = HERMAS2_SAGA_PREPARED;
    return HERMAS2_SAGA_OK;
}

hermas2_saga_result hermas2_saga_mark_sent(
    hermas2_saga_execution *execution) {
    if (execution == NULL ||
        execution->state != HERMAS2_SAGA_PREPARED) {
        return HERMAS2_SAGA_INVALID_STATE;
    }
    execution->state = HERMAS2_SAGA_SENT;
    return HERMAS2_SAGA_OK;
}

hermas2_saga_result hermas2_saga_mark_not_sent(
    hermas2_saga_execution *execution) {
    if (execution == NULL ||
        execution->state != HERMAS2_SAGA_PREPARED) {
        return HERMAS2_SAGA_INVALID_STATE;
    }
    execution->compensation_outcome = HERMAS2_OUTCOME_NOT_SENT;
    execution->state = HERMAS2_SAGA_BLOCKED;
    return HERMAS2_SAGA_OK;
}

hermas2_saga_result hermas2_saga_mark_unknown(
    hermas2_saga_execution *execution) {
    if (execution == NULL ||
        execution->state != HERMAS2_SAGA_SENT) {
        return HERMAS2_SAGA_INVALID_STATE;
    }
    execution->compensation_outcome = HERMAS2_OUTCOME_UNKNOWN;
    execution->state = HERMAS2_SAGA_BLOCKED;
    return HERMAS2_SAGA_OK;
}

hermas2_saga_result hermas2_saga_accept_result(
    hermas2_saga_execution *execution,
    const hermas2_frame *result) {
    if (execution == NULL || result == NULL) {
        return HERMAS2_SAGA_INVALID_ARGUMENT;
    }
    if (execution->state != HERMAS2_SAGA_SENT ||
        execution->remaining == 0u) {
        return HERMAS2_SAGA_INVALID_STATE;
    }
    const hermas2_saga_step *step =
        &execution->steps[execution->remaining - 1u];
    uint16_t expected_type =
        result->outcome == HERMAS2_OUTCOME_SUCCESS
            ? step->success_type
            : step->error_type;
    if (result->kind != HERMAS2_FRAME_RESULT ||
        result->execution_id != execution->execution_id ||
        result->request_id != execution->current_request_id ||
        result->app_id != step->compensation_app_id ||
        result->action_id != step->compensation_action_id ||
        (result->outcome != HERMAS2_OUTCOME_SUCCESS &&
         result->outcome != HERMAS2_OUTCOME_APP_ERROR) ||
        result->source_type != expected_type ||
        result->destination_type != expected_type) {
        return HERMAS2_SAGA_UNEXPECTED_RESULT;
    }
    if (hermas2_image_validate_value(
            execution->image, execution->image_size, expected_type,
            result->payload, result->payload_length) !=
        HERMAS2_IMAGE_OK) {
        return HERMAS2_SAGA_UNEXPECTED_RESULT;
    }
    execution->compensation_outcome = result->outcome;
    if (result->outcome == HERMAS2_OUTCOME_APP_ERROR) {
        execution->state = HERMAS2_SAGA_BLOCKED;
        return HERMAS2_SAGA_OK;
    }
    --execution->remaining;
    execution->state = execution->remaining == 0u
                           ? HERMAS2_SAGA_COMPLETE
                           : HERMAS2_SAGA_READY;
    return HERMAS2_SAGA_OK;
}

const char *hermas2_saga_result_name(hermas2_saga_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "invalid-image", "not-saga",
        "inconsistent-history", "unsafe-history", "missing-token",
        "duplicate-token", "invalid-token", "buffer-too-small",
        "invalid-state", "unexpected-result", "request-id-exhausted"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
