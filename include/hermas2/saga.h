#ifndef HERMAS2_SAGA_H
#define HERMAS2_SAGA_H

#include "hermas2/compensation.h"
#include "hermas2/journal.h"
#include "hermas2/protocol.h"
#include "hermas2/saga_log.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS2_SAGA_MAX_STEPS 16u

typedef enum hermas2_saga_result {
    HERMAS2_SAGA_OK = 0,
    HERMAS2_SAGA_INVALID_ARGUMENT,
    HERMAS2_SAGA_INVALID_IMAGE,
    HERMAS2_SAGA_NOT_SAGA,
    HERMAS2_SAGA_INCONSISTENT_HISTORY,
    HERMAS2_SAGA_UNSAFE_HISTORY,
    HERMAS2_SAGA_MISSING_TOKEN,
    HERMAS2_SAGA_DUPLICATE_TOKEN,
    HERMAS2_SAGA_INVALID_TOKEN,
    HERMAS2_SAGA_BUFFER_TOO_SMALL,
    HERMAS2_SAGA_INVALID_STATE,
    HERMAS2_SAGA_UNEXPECTED_RESULT,
    HERMAS2_SAGA_REQUEST_ID_EXHAUSTED,
    HERMAS2_SAGA_LOG_ERROR
} hermas2_saga_result;

typedef enum hermas2_saga_state {
    HERMAS2_SAGA_EMPTY = 0,
    HERMAS2_SAGA_READY,
    HERMAS2_SAGA_PREPARED,
    HERMAS2_SAGA_SENT,
    HERMAS2_SAGA_COMPLETE,
    HERMAS2_SAGA_BLOCKED
} hermas2_saga_state;

typedef struct hermas2_saga_step {
    uint64_t forward_request_id;
    uint16_t forward_node;
    uint16_t compensation_app_id;
    uint16_t compensation_action_id;
    uint16_t source_type;
    uint16_t destination_type;
    uint16_t success_type;
    uint16_t error_type;
    uint16_t ordinal;
} hermas2_saga_step;

typedef struct hermas2_saga_execution {
    const uint8_t *image;
    size_t image_size;
    const uint8_t *tokens;
    size_t token_bytes;
    hermas2_compensation_lookup token_lookup;
    void *token_lookup_context;
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t image_fingerprint;
    uint64_t next_request_id;
    uint64_t current_request_id;
    uint16_t original_outcome;
    uint16_t compensation_outcome;
    uint8_t step_count;
    uint8_t completed_steps;
    uint8_t remaining;
    hermas2_saga_state state;
    hermas2_saga_step steps[HERMAS2_SAGA_MAX_STEPS];
} hermas2_saga_execution;

typedef struct hermas2_saga_driver {
    hermas2_saga_execution execution;
    hermas2_saga_log_writer *log;
    uint8_t started;
} hermas2_saga_driver;

hermas2_saga_result hermas2_saga_recover(
    hermas2_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    const uint8_t *journal,
    size_t journal_bytes,
    const uint8_t *tokens,
    size_t token_bytes,
    uint64_t execution_id,
    uint32_t workflow_id);

hermas2_saga_result hermas2_saga_begin_live(
    hermas2_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint64_t execution_id,
    uint32_t workflow_id,
    uint16_t original_outcome,
    const uint64_t *forward_request_ids,
    uint8_t completed_steps,
    uint64_t next_request_id,
    hermas2_compensation_lookup token_lookup,
    void *token_lookup_context);

hermas2_saga_result hermas2_saga_prepare(
    hermas2_saga_execution *execution,
    uint8_t *token_buffer,
    size_t token_capacity,
    hermas2_frame *invocation);

hermas2_saga_result hermas2_saga_reconcile(
    hermas2_saga_execution *execution,
    const uint8_t *saga_log,
    size_t saga_log_bytes);

hermas2_saga_result hermas2_saga_mark_sent(
    hermas2_saga_execution *execution);

hermas2_saga_result hermas2_saga_mark_not_sent(
    hermas2_saga_execution *execution);

hermas2_saga_result hermas2_saga_mark_unknown(
    hermas2_saga_execution *execution);

hermas2_saga_result hermas2_saga_accept_result(
    hermas2_saga_execution *execution,
    const hermas2_frame *result);

hermas2_saga_result hermas2_saga_driver_begin(
    hermas2_saga_driver *driver,
    const hermas2_saga_execution *execution,
    hermas2_saga_log_writer *log,
    int resume_existing);

hermas2_saga_result hermas2_saga_driver_prepare(
    hermas2_saga_driver *driver,
    uint8_t *token_buffer,
    size_t token_capacity,
    hermas2_frame *invocation);

hermas2_saga_result hermas2_saga_driver_mark_sent(
    hermas2_saga_driver *driver);

hermas2_saga_result hermas2_saga_driver_mark_not_sent(
    hermas2_saga_driver *driver);

hermas2_saga_result hermas2_saga_driver_mark_unknown(
    hermas2_saga_driver *driver);

hermas2_saga_result hermas2_saga_driver_accept_result(
    hermas2_saga_driver *driver,
    const hermas2_frame *result);

const char *hermas2_saga_result_name(hermas2_saga_result result);

#endif
