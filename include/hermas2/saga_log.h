#ifndef HERMAS2_SAGA_LOG_H
#define HERMAS2_SAGA_LOG_H

#include "hermas2/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS2_SAGA_LOG_VERSION 1u
#define HERMAS2_SAGA_LOG_RECORD_SIZE 64u
#define HERMAS2_SAGA_LOG_MAX_ACTIVE 16u
#define HERMAS2_SAGA_LOG_MAX_STEPS 16u

typedef enum hermas2_saga_log_result {
    HERMAS2_SAGA_LOG_OK = 0,
    HERMAS2_SAGA_LOG_INVALID_ARGUMENT,
    HERMAS2_SAGA_LOG_BUFFER_TOO_SMALL,
    HERMAS2_SAGA_LOG_INVALID_RECORD,
    HERMAS2_SAGA_LOG_CHECKSUM_MISMATCH,
    HERMAS2_SAGA_LOG_INVALID_SEQUENCE,
    HERMAS2_SAGA_LOG_INVALID_TRANSITION,
    HERMAS2_SAGA_LOG_CAPACITY_EXHAUSTED,
    HERMAS2_SAGA_LOG_WRITE_ERROR
} hermas2_saga_log_result;

typedef enum hermas2_saga_log_kind {
    HERMAS2_SAGA_LOG_STARTED = 1,
    HERMAS2_SAGA_LOG_DELIVERY_PREPARED,
    HERMAS2_SAGA_LOG_DELIVERY_SENT,
    HERMAS2_SAGA_LOG_STEP_SUCCEEDED,
    HERMAS2_SAGA_LOG_STEP_FAILED,
    HERMAS2_SAGA_LOG_STEP_UNKNOWN,
    HERMAS2_SAGA_LOG_FINISHED
} hermas2_saga_log_kind;

typedef struct hermas2_saga_log_record {
    hermas2_saga_log_kind kind;
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
} hermas2_saga_log_record;

typedef struct hermas2_saga_log_active {
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
} hermas2_saga_log_active;

typedef struct hermas2_saga_log_summary {
    uint64_t record_count;
    uint64_t next_sequence;
    uint8_t active_count;
    hermas2_saga_log_active active[HERMAS2_SAGA_LOG_MAX_ACTIVE];
} hermas2_saga_log_summary;

typedef hermas2_saga_log_result (*hermas2_saga_log_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas2_saga_log_writer {
    hermas2_saga_log_write write;
    void *context;
    uint64_t next_sequence;
} hermas2_saga_log_writer;

hermas2_saga_log_result hermas2_saga_log_encode(
    const hermas2_saga_log_record *record,
    uint8_t *destination,
    size_t destination_size);

hermas2_saga_log_result hermas2_saga_log_decode(
    const uint8_t *source,
    size_t source_size,
    hermas2_saga_log_record *record);

hermas2_saga_log_result hermas2_saga_log_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_saga_log_summary *summary);

hermas2_saga_log_result hermas2_saga_log_writer_init(
    hermas2_saga_log_writer *writer,
    hermas2_saga_log_write write,
    void *context,
    uint64_t next_sequence);

hermas2_saga_log_result hermas2_saga_log_writer_append(
    hermas2_saga_log_writer *writer,
    hermas2_saga_log_record record);

const char *hermas2_saga_log_result_name(
    hermas2_saga_log_result result);

#endif
