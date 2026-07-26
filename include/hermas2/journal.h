#ifndef HERMAS2_JOURNAL_H
#define HERMAS2_JOURNAL_H

#include "hermas2/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS2_JOURNAL_VERSION 1u
#define HERMAS2_JOURNAL_RECORD_SIZE 64u
#define HERMAS2_JOURNAL_MAX_INTERRUPTED 16u

typedef enum hermas2_journal_result {
    HERMAS2_JOURNAL_OK = 0,
    HERMAS2_JOURNAL_INVALID_ARGUMENT,
    HERMAS2_JOURNAL_BUFFER_TOO_SMALL,
    HERMAS2_JOURNAL_INVALID_MAGIC,
    HERMAS2_JOURNAL_INVALID_VERSION,
    HERMAS2_JOURNAL_INVALID_SIZE,
    HERMAS2_JOURNAL_INVALID_KIND,
    HERMAS2_JOURNAL_INVALID_OUTCOME,
    HERMAS2_JOURNAL_INVALID_FIELD,
    HERMAS2_JOURNAL_NONZERO_RESERVED,
    HERMAS2_JOURNAL_CHECKSUM_MISMATCH,
    HERMAS2_JOURNAL_INVALID_SEQUENCE,
    HERMAS2_JOURNAL_INVALID_TRANSITION,
    HERMAS2_JOURNAL_CAPACITY_EXHAUSTED,
    HERMAS2_JOURNAL_WRITE_ERROR
} hermas2_journal_result;

typedef enum hermas2_journal_kind {
    HERMAS2_JOURNAL_EXECUTION_STARTED = 1,
    HERMAS2_JOURNAL_DELIVERY_PREPARED,
    HERMAS2_JOURNAL_DELIVERY_SENT,
    HERMAS2_JOURNAL_ACTION_SUCCEEDED,
    HERMAS2_JOURNAL_ACTION_FAILED,
    HERMAS2_JOURNAL_ACTION_UNKNOWN,
    HERMAS2_JOURNAL_EXECUTION_FINISHED
} hermas2_journal_kind;

typedef struct hermas2_journal_record {
    hermas2_journal_kind kind;
    uint16_t outcome;
    uint64_t sequence;
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t request_id;
    uint16_t node_id;
    uint16_t app_id;
    uint16_t action_id;
    uint64_t image_fingerprint;
} hermas2_journal_record;

typedef struct hermas2_journal_interrupted {
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t image_fingerprint;
    uint8_t has_open_delivery;
    uint8_t delivery_was_sent;
    uint64_t request_id;
    uint16_t node_id;
    uint16_t app_id;
    uint16_t action_id;
} hermas2_journal_interrupted;

typedef struct hermas2_journal_summary {
    uint64_t record_count;
    uint64_t next_sequence;
    uint64_t next_execution_id;
    uint8_t interrupted_count;
    hermas2_journal_interrupted
        interrupted[HERMAS2_JOURNAL_MAX_INTERRUPTED];
} hermas2_journal_summary;

typedef hermas2_journal_result (*hermas2_journal_visitor)(
    void *context,
    const hermas2_journal_record *record);

typedef hermas2_journal_result (*hermas2_journal_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas2_journal_writer {
    hermas2_journal_write write;
    void *context;
    uint64_t next_sequence;
} hermas2_journal_writer;

hermas2_journal_result hermas2_journal_encode(
    const hermas2_journal_record *record,
    uint8_t *destination,
    size_t destination_size);

hermas2_journal_result hermas2_journal_decode(
    const uint8_t *source,
    size_t source_size,
    hermas2_journal_record *record);

hermas2_journal_result hermas2_journal_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_journal_visitor visitor,
    void *visitor_context,
    hermas2_journal_summary *summary);

hermas2_journal_result hermas2_journal_writer_init(
    hermas2_journal_writer *writer,
    hermas2_journal_write write,
    void *context,
    uint64_t next_sequence);

hermas2_journal_result hermas2_journal_writer_append(
    hermas2_journal_writer *writer,
    hermas2_journal_record record);

uint64_t hermas2_journal_image_fingerprint(
    const uint8_t *image,
    size_t image_size);

const char *hermas2_journal_result_name(
    hermas2_journal_result result);

#endif
