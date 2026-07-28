#ifndef HERMAS_SAGA_LOG_H
#define HERMAS_SAGA_LOG_H

#include "hermas/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS_SAGA_LOG_VERSION 1u
#define HERMAS_SAGA_LOG_RECORD_SIZE 64u
#define HERMAS_SAGA_LOG_MAX_ACTIVE 16u
#define HERMAS_SAGA_LOG_MAX_STEPS 16u

typedef enum hermas_saga_log_result {
    HERMAS_SAGA_LOG_OK = 0,
    HERMAS_SAGA_LOG_INVALID_ARGUMENT,
    HERMAS_SAGA_LOG_BUFFER_TOO_SMALL,
    HERMAS_SAGA_LOG_INVALID_RECORD,
    HERMAS_SAGA_LOG_CHECKSUM_MISMATCH,
    HERMAS_SAGA_LOG_INVALID_SEQUENCE,
    HERMAS_SAGA_LOG_INVALID_TRANSITION,
    HERMAS_SAGA_LOG_CAPACITY_EXHAUSTED,
    HERMAS_SAGA_LOG_WRITE_ERROR
} hermas_saga_log_result;

typedef enum hermas_saga_log_kind {
    HERMAS_SAGA_LOG_STARTED = 1,
    HERMAS_SAGA_LOG_DELIVERY_PREPARED,
    HERMAS_SAGA_LOG_DELIVERY_SENT,
    HERMAS_SAGA_LOG_STEP_SUCCEEDED,
    HERMAS_SAGA_LOG_STEP_FAILED,
    HERMAS_SAGA_LOG_STEP_UNKNOWN,
    HERMAS_SAGA_LOG_FINISHED
} hermas_saga_log_kind;

typedef struct hermas_saga_log_record {
    hermas_saga_log_kind kind;
    uint16_t outcome;
    uint64_t sequence;
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t request_id;
    uint16_t forward_node;
    uint16_t app_id;
    uint16_t action_id;
    uint16_t ordinal;
    uint64_t image_fingerprint;
} hermas_saga_log_record;

typedef struct hermas_saga_log_active {
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t image_fingerprint;
    uint64_t request_id;
    uint16_t forward_node;
    uint16_t app_id;
    uint16_t action_id;
    uint16_t next_ordinal;
    uint16_t terminal_outcome;
    uint8_t has_open_delivery;
    uint8_t delivery_was_sent;
} hermas_saga_log_active;

typedef struct hermas_saga_log_summary {
    uint64_t record_count;
    uint64_t next_sequence;
    uint8_t active_count;
    hermas_saga_log_active active[HERMAS_SAGA_LOG_MAX_ACTIVE];
} hermas_saga_log_summary;

typedef hermas_saga_log_result (*hermas_saga_log_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas_saga_log_writer {
    hermas_saga_log_write write;
    void *context;
    uint64_t next_sequence;
} hermas_saga_log_writer;

hermas_saga_log_result hermas_saga_log_encode(
    const hermas_saga_log_record *record,
    uint8_t *destination,
    size_t destination_size);

hermas_saga_log_result hermas_saga_log_decode(
    const uint8_t *source,
    size_t source_size,
    hermas_saga_log_record *record);

hermas_saga_log_result hermas_saga_log_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_saga_log_summary *summary);

hermas_saga_log_result hermas_saga_log_writer_init(
    hermas_saga_log_writer *writer,
    hermas_saga_log_write write,
    void *context,
    uint64_t next_sequence);

hermas_saga_log_result hermas_saga_log_writer_append(
    hermas_saga_log_writer *writer,
    hermas_saga_log_record record);

const char *hermas_saga_log_result_name(
    hermas_saga_log_result result);

#endif
