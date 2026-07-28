#ifndef HERMAS_JOURNAL_H
#define HERMAS_JOURNAL_H

#include "hermas/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS_JOURNAL_VERSION 1u
#define HERMAS_JOURNAL_RECORD_SIZE 64u
#define HERMAS_JOURNAL_MAX_INTERRUPTED 16u

typedef enum hermas_journal_result {
    HERMAS_JOURNAL_OK = 0,
    HERMAS_JOURNAL_INVALID_ARGUMENT,
    HERMAS_JOURNAL_BUFFER_TOO_SMALL,
    HERMAS_JOURNAL_INVALID_MAGIC,
    HERMAS_JOURNAL_INVALID_VERSION,
    HERMAS_JOURNAL_INVALID_SIZE,
    HERMAS_JOURNAL_INVALID_KIND,
    HERMAS_JOURNAL_INVALID_OUTCOME,
    HERMAS_JOURNAL_INVALID_FIELD,
    HERMAS_JOURNAL_NONZERO_RESERVED,
    HERMAS_JOURNAL_CHECKSUM_MISMATCH,
    HERMAS_JOURNAL_INVALID_SEQUENCE,
    HERMAS_JOURNAL_INVALID_TRANSITION,
    HERMAS_JOURNAL_CAPACITY_EXHAUSTED,
    HERMAS_JOURNAL_WRITE_ERROR
} hermas_journal_result;

typedef enum hermas_journal_kind {
    HERMAS_JOURNAL_EXECUTION_STARTED = 1,
    HERMAS_JOURNAL_DELIVERY_PREPARED,
    HERMAS_JOURNAL_DELIVERY_SENT,
    HERMAS_JOURNAL_ACTION_SUCCEEDED,
    HERMAS_JOURNAL_ACTION_FAILED,
    HERMAS_JOURNAL_ACTION_UNKNOWN,
    HERMAS_JOURNAL_EXECUTION_FINISHED
} hermas_journal_kind;

typedef struct hermas_journal_record {
    hermas_journal_kind kind;
    uint16_t outcome;
    uint64_t sequence;
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t request_id;
    uint16_t node_id;
    uint16_t app_id;
    uint16_t action_id;
    uint64_t image_fingerprint;
} hermas_journal_record;

typedef struct hermas_journal_interrupted {
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t image_fingerprint;
    uint8_t has_open_delivery;
    uint8_t delivery_was_sent;
    uint64_t request_id;
    uint16_t node_id;
    uint16_t app_id;
    uint16_t action_id;
} hermas_journal_interrupted;

typedef struct hermas_journal_summary {
    uint64_t record_count;
    uint64_t next_sequence;
    uint64_t next_execution_id;
    uint8_t interrupted_count;
    hermas_journal_interrupted
        interrupted[HERMAS_JOURNAL_MAX_INTERRUPTED];
} hermas_journal_summary;

typedef hermas_journal_result (*hermas_journal_visitor)(
    void *context,
    const hermas_journal_record *record);

typedef hermas_journal_result (*hermas_journal_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas_journal_writer {
    hermas_journal_write write;
    void *context;
    uint64_t next_sequence;
} hermas_journal_writer;

hermas_journal_result hermas_journal_encode(
    const hermas_journal_record *record,
    uint8_t *destination,
    size_t destination_size);

hermas_journal_result hermas_journal_decode(
    const uint8_t *source,
    size_t source_size,
    hermas_journal_record *record);

hermas_journal_result hermas_journal_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_journal_visitor visitor,
    void *visitor_context,
    hermas_journal_summary *summary);

hermas_journal_result hermas_journal_writer_init(
    hermas_journal_writer *writer,
    hermas_journal_write write,
    void *context,
    uint64_t next_sequence);

hermas_journal_result hermas_journal_writer_append(
    hermas_journal_writer *writer,
    hermas_journal_record record);

uint64_t hermas_journal_image_fingerprint(
    const uint8_t *image,
    size_t image_size);

const char *hermas_journal_result_name(
    hermas_journal_result result);

#endif
