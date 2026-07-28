#ifndef HERMAS_SAGA_H
#define HERMAS_SAGA_H

#include "hermas/compensation.h"
#include "hermas/journal.h"
#include "hermas/protocol.h"
#include "hermas/saga_log.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS_SAGA_MAX_STEPS 16u

typedef enum hermas_saga_result {
    HERMAS_SAGA_OK = 0,
    HERMAS_SAGA_INVALID_ARGUMENT,
    HERMAS_SAGA_INVALID_IMAGE,
    HERMAS_SAGA_NOT_SAGA,
    HERMAS_SAGA_INCONSISTENT_HISTORY,
    HERMAS_SAGA_UNSAFE_HISTORY,
    HERMAS_SAGA_MISSING_TOKEN,
    HERMAS_SAGA_DUPLICATE_TOKEN,
    HERMAS_SAGA_INVALID_TOKEN,
    HERMAS_SAGA_BUFFER_TOO_SMALL,
    HERMAS_SAGA_INVALID_STATE,
    HERMAS_SAGA_UNEXPECTED_RESULT,
    HERMAS_SAGA_REQUEST_ID_EXHAUSTED,
    HERMAS_SAGA_LOG_ERROR
} hermas_saga_result;

typedef enum hermas_saga_state {
    HERMAS_SAGA_EMPTY = 0,
    HERMAS_SAGA_READY,
    HERMAS_SAGA_PREPARED,
    HERMAS_SAGA_SENT,
    HERMAS_SAGA_COMPLETE,
    HERMAS_SAGA_BLOCKED
} hermas_saga_state;

typedef struct hermas_saga_step {
    uint64_t forward_request_id;
    uint16_t forward_node;
    uint16_t compensation_app_id;
    uint16_t compensation_action_id;
    uint16_t source_type;
    uint16_t destination_type;
    uint16_t success_type;
    uint16_t error_type;
    uint16_t ordinal;
} hermas_saga_step;

typedef struct hermas_saga_execution {
    const uint8_t *image;
    size_t image_size;
    const uint8_t *tokens;
    size_t token_bytes;
    hermas_compensation_lookup token_lookup;
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
    hermas_saga_state state;
    hermas_saga_step steps[HERMAS_SAGA_MAX_STEPS];
} hermas_saga_execution;

typedef struct hermas_saga_driver {
    hermas_saga_execution execution;
    hermas_saga_log_writer *log;
    uint8_t started;
} hermas_saga_driver;

hermas_saga_result hermas_saga_recover(
    hermas_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    const uint8_t *journal,
    size_t journal_bytes,
    const uint8_t *tokens,
    size_t token_bytes,
    uint64_t execution_id,
    uint32_t workflow_id);

hermas_saga_result hermas_saga_begin_live(
    hermas_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint64_t execution_id,
    uint32_t workflow_id,
    uint16_t original_outcome,
    const uint64_t *forward_request_ids,
    uint8_t completed_steps,
    uint64_t next_request_id,
    hermas_compensation_lookup token_lookup,
    void *token_lookup_context);

hermas_saga_result hermas_saga_prepare(
    hermas_saga_execution *execution,
    uint8_t *token_buffer,
    size_t token_capacity,
    hermas_frame *invocation);

hermas_saga_result hermas_saga_reconcile(
    hermas_saga_execution *execution,
    const uint8_t *saga_log,
    size_t saga_log_bytes);

hermas_saga_result hermas_saga_mark_sent(
    hermas_saga_execution *execution);

hermas_saga_result hermas_saga_mark_not_sent(
    hermas_saga_execution *execution);

hermas_saga_result hermas_saga_mark_unknown(
    hermas_saga_execution *execution);

hermas_saga_result hermas_saga_accept_result(
    hermas_saga_execution *execution,
    const hermas_frame *result);

hermas_saga_result hermas_saga_driver_begin(
    hermas_saga_driver *driver,
    const hermas_saga_execution *execution,
    hermas_saga_log_writer *log,
    int resume_existing);

hermas_saga_result hermas_saga_driver_prepare(
    hermas_saga_driver *driver,
    uint8_t *token_buffer,
    size_t token_capacity,
    hermas_frame *invocation);

hermas_saga_result hermas_saga_driver_mark_sent(
    hermas_saga_driver *driver);

hermas_saga_result hermas_saga_driver_mark_not_sent(
    hermas_saga_driver *driver);

hermas_saga_result hermas_saga_driver_mark_unknown(
    hermas_saga_driver *driver);

hermas_saga_result hermas_saga_driver_accept_result(
    hermas_saga_driver *driver,
    const hermas_frame *result);

const char *hermas_saga_result_name(hermas_saga_result result);

#endif
