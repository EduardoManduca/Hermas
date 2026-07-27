#ifndef HERMAS2_RESULT_H
#define HERMAS2_RESULT_H

#include "hermas2/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS2_RESULT_VERSION 1u
#define HERMAS2_RESULT_HEADER_SIZE 64u

typedef enum hermas2_result_store_result {
    HERMAS2_RESULT_STORE_OK = 0,
    HERMAS2_RESULT_STORE_INVALID_ARGUMENT,
    HERMAS2_RESULT_STORE_BUFFER_TOO_SMALL,
    HERMAS2_RESULT_STORE_INVALID_RECORD,
    HERMAS2_RESULT_STORE_CHECKSUM_MISMATCH,
    HERMAS2_RESULT_STORE_INVALID_SEQUENCE,
    HERMAS2_RESULT_STORE_DUPLICATE_RESULT,
    HERMAS2_RESULT_STORE_WRITE_ERROR
} hermas2_result_store_result;

typedef struct hermas2_result_key {
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t image_fingerprint;
} hermas2_result_key;

typedef struct hermas2_result_record {
    uint64_t sequence;
    hermas2_result_key key;
    uint16_t outcome;
    uint16_t source_type;
    uint16_t destination_type;
    const uint8_t *value;
    uint32_t value_length;
} hermas2_result_record;

typedef struct hermas2_result_summary {
    uint64_t record_count;
    uint64_t next_sequence;
} hermas2_result_summary;

typedef hermas2_result_store_result (*hermas2_result_lookup)(
    void *context,
    hermas2_result_key key,
    hermas2_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

typedef hermas2_result_store_result (*hermas2_result_visitor)(
    void *context,
    const hermas2_result_record *record);

typedef hermas2_result_store_result (*hermas2_result_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas2_result_writer {
    hermas2_result_write write;
    void *context;
    uint64_t next_sequence;
} hermas2_result_writer;

hermas2_result_store_result hermas2_result_encode(
    const hermas2_result_record *record,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size);

hermas2_result_store_result hermas2_result_decode(
    const uint8_t *source,
    size_t source_size,
    hermas2_result_record *record,
    size_t *record_size);

hermas2_result_store_result hermas2_result_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_result_visitor visitor,
    void *visitor_context,
    hermas2_result_summary *summary);

hermas2_result_store_result hermas2_result_find(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_result_key key,
    hermas2_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

hermas2_result_store_result hermas2_result_writer_init(
    hermas2_result_writer *writer,
    hermas2_result_write write,
    void *context,
    uint64_t next_sequence);

hermas2_result_store_result hermas2_result_writer_append(
    hermas2_result_writer *writer,
    hermas2_result_record record,
    uint8_t *scratch,
    size_t scratch_capacity);

const char *hermas2_result_store_result_name(
    hermas2_result_store_result result);

#endif
