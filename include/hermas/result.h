#ifndef HERMAS_RESULT_H
#define HERMAS_RESULT_H

#include "hermas/protocol.h"

#include <stddef.h>
#include <stdint.h>

#define HERMAS_RESULT_VERSION 1u
#define HERMAS_RESULT_HEADER_SIZE 64u

typedef enum hermas_result_store_result {
    HERMAS_RESULT_STORE_OK = 0,
    HERMAS_RESULT_STORE_INVALID_ARGUMENT,
    HERMAS_RESULT_STORE_BUFFER_TOO_SMALL,
    HERMAS_RESULT_STORE_INVALID_RECORD,
    HERMAS_RESULT_STORE_CHECKSUM_MISMATCH,
    HERMAS_RESULT_STORE_INVALID_SEQUENCE,
    HERMAS_RESULT_STORE_DUPLICATE_RESULT,
    HERMAS_RESULT_STORE_WRITE_ERROR
} hermas_result_store_result;

typedef struct hermas_result_key {
    uint64_t execution_id;
    uint32_t workflow_id;
    uint64_t image_fingerprint;
} hermas_result_key;

typedef struct hermas_result_record {
    uint64_t sequence;
    hermas_result_key key;
    uint16_t outcome;
    uint16_t source_type;
    uint16_t destination_type;
    const uint8_t *value;
    uint32_t value_length;
} hermas_result_record;

typedef struct hermas_result_summary {
    uint64_t record_count;
    uint64_t next_sequence;
} hermas_result_summary;

typedef hermas_result_store_result (*hermas_result_lookup)(
    void *context,
    hermas_result_key key,
    hermas_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

typedef hermas_result_store_result (*hermas_result_visitor)(
    void *context,
    const hermas_result_record *record);

typedef hermas_result_store_result (*hermas_result_write)(
    void *context,
    const uint8_t *record,
    size_t record_size);

typedef struct hermas_result_writer {
    hermas_result_write write;
    void *context;
    uint64_t next_sequence;
} hermas_result_writer;

hermas_result_store_result hermas_result_encode(
    const hermas_result_record *record,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size);

hermas_result_store_result hermas_result_decode(
    const uint8_t *source,
    size_t source_size,
    hermas_result_record *record,
    size_t *record_size);

hermas_result_store_result hermas_result_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_result_visitor visitor,
    void *visitor_context,
    hermas_result_summary *summary);

hermas_result_store_result hermas_result_find(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_result_key key,
    hermas_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

hermas_result_store_result hermas_result_writer_init(
    hermas_result_writer *writer,
    hermas_result_write write,
    void *context,
    uint64_t next_sequence);

hermas_result_store_result hermas_result_writer_append(
    hermas_result_writer *writer,
    hermas_result_record record,
    uint8_t *scratch,
    size_t scratch_capacity);

const char *hermas_result_store_result_name(
    hermas_result_store_result result);

#endif
