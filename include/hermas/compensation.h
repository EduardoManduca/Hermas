#ifndef HERMAS_COMPENSATION_H
#define HERMAS_COMPENSATION_H

#include "hermas/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS_COMPENSATION_VERSION 1u
#define HERMAS_COMPENSATION_HEADER_SIZE 72u

typedef enum hermas_compensation_result {
    HERMAS_COMPENSATION_OK = 0,
    HERMAS_COMPENSATION_INVALID_ARGUMENT,
    HERMAS_COMPENSATION_BUFFER_TOO_SMALL,
    HERMAS_COMPENSATION_INVALID_RECORD,
    HERMAS_COMPENSATION_CHECKSUM_MISMATCH,
    HERMAS_COMPENSATION_INVALID_SEQUENCE,
    HERMAS_COMPENSATION_DUPLICATE_TOKEN,
    HERMAS_COMPENSATION_WRITE_ERROR
} hermas_compensation_result;

typedef struct hermas_compensation_key {
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t request_id;
    uint16_t node_id;
    uint64_t image_fingerprint;
} hermas_compensation_key;

typedef struct hermas_compensation_record {
    uint64_t sequence;
    hermas_compensation_key key;
    uint16_t compensation_app_id;
    uint16_t compensation_action_id;
    uint16_t source_type;
    uint16_t destination_type;
    const uint8_t *token;
    uint32_t token_length;
} hermas_compensation_record;

typedef struct hermas_compensation_summary {
    uint64_t record_count;
    uint64_t next_sequence;
} hermas_compensation_summary;

typedef hermas_compensation_result (*hermas_compensation_lookup)(
    void *context,
    hermas_compensation_key key,
    hermas_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found);

typedef hermas_compensation_result (*hermas_compensation_visitor)(
    void *context,
    const hermas_compensation_record *record);

typedef hermas_compensation_result (*hermas_compensation_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas_compensation_writer {
    hermas_compensation_write write;
    void *context;
    uint64_t next_sequence;
} hermas_compensation_writer;

hermas_compensation_result hermas_compensation_encode(
    const hermas_compensation_record *record,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size);

hermas_compensation_result hermas_compensation_decode(
    const uint8_t *source,
    size_t source_size,
    hermas_compensation_record *record,
    size_t *record_size);

hermas_compensation_result hermas_compensation_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_compensation_visitor visitor,
    void *visitor_context,
    hermas_compensation_summary *summary);

hermas_compensation_result hermas_compensation_find(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_compensation_key key,
    hermas_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found);

hermas_compensation_result hermas_compensation_writer_init(
    hermas_compensation_writer *writer,
    hermas_compensation_write write,
    void *context,
    uint64_t next_sequence);

hermas_compensation_result hermas_compensation_writer_append(
    hermas_compensation_writer *writer,
    hermas_compensation_record record,
    uint8_t *scratch,
    size_t scratch_capacity);

const char *hermas_compensation_result_name(
    hermas_compensation_result result);

#endif
