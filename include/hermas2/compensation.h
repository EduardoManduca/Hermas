#ifndef HERMAS2_COMPENSATION_H
#define HERMAS2_COMPENSATION_H

#include "hermas2/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS2_COMPENSATION_VERSION 1u
#define HERMAS2_COMPENSATION_HEADER_SIZE 72u

typedef enum hermas2_compensation_result {
    HERMAS2_COMPENSATION_OK = 0,
    HERMAS2_COMPENSATION_INVALID_ARGUMENT,
    HERMAS2_COMPENSATION_BUFFER_TOO_SMALL,
    HERMAS2_COMPENSATION_INVALID_RECORD,
    HERMAS2_COMPENSATION_CHECKSUM_MISMATCH,
    HERMAS2_COMPENSATION_INVALID_SEQUENCE,
    HERMAS2_COMPENSATION_DUPLICATE_TOKEN,
    HERMAS2_COMPENSATION_WRITE_ERROR
} hermas2_compensation_result;

typedef struct hermas2_compensation_key {
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t request_id;
    uint16_t node_id;
    uint64_t image_fingerprint;
} hermas2_compensation_key;

typedef struct hermas2_compensation_record {
    uint64_t sequence;
    hermas2_compensation_key key;
    uint16_t compensation_app_id;
    uint16_t compensation_action_id;
    uint16_t source_type;
    uint16_t destination_type;
    const uint8_t *token;
    uint32_t token_length;
} hermas2_compensation_record;

typedef struct hermas2_compensation_summary {
    uint64_t record_count;
    uint64_t next_sequence;
} hermas2_compensation_summary;

typedef hermas2_compensation_result (*hermas2_compensation_visitor)(
    void *context,
    const hermas2_compensation_record *record);

typedef hermas2_compensation_result (*hermas2_compensation_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas2_compensation_writer {
    hermas2_compensation_write write;
    void *context;
    uint64_t next_sequence;
} hermas2_compensation_writer;

hermas2_compensation_result hermas2_compensation_encode(
    const hermas2_compensation_record *record,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size);

hermas2_compensation_result hermas2_compensation_decode(
    const uint8_t *source,
    size_t source_size,
    hermas2_compensation_record *record,
    size_t *record_size);

hermas2_compensation_result hermas2_compensation_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_compensation_visitor visitor,
    void *visitor_context,
    hermas2_compensation_summary *summary);

hermas2_compensation_result hermas2_compensation_find(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_compensation_key key,
    hermas2_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found);

hermas2_compensation_result hermas2_compensation_writer_init(
    hermas2_compensation_writer *writer,
    hermas2_compensation_write write,
    void *context,
    uint64_t next_sequence);

hermas2_compensation_result hermas2_compensation_writer_append(
    hermas2_compensation_writer *writer,
    hermas2_compensation_record record,
    uint8_t *scratch,
    size_t scratch_capacity);

const char *hermas2_compensation_result_name(
    hermas2_compensation_result result);

#endif
