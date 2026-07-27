#include "hermas2/result.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_result: %s\n", message);
        ++failures;
    }
}

static hermas2_result_record result_record(
    uint64_t sequence,
    const uint8_t *value,
    uint32_t value_length) {
    return (hermas2_result_record){
        .sequence = sequence,
        .key = {
            .execution_id = 19u,
            .workflow_id = 7u,
            .image_fingerprint = UINT64_C(0x1020304050607080)
        },
        .outcome = HERMAS2_OUTCOME_APP_ERROR,
        .source_type = 4u,
        .destination_type = 5u,
        .value = value,
        .value_length = value_length
    };
}

static void test_codec_scan_and_lookup(void) {
    uint8_t value[8] = {42u};
    hermas2_result_record input =
        result_record(1u, value, sizeof(value));
    uint8_t bytes[2u * (HERMAS2_RESULT_HEADER_SIZE + 8u)];
    size_t size = 0u;
    require(hermas2_result_encode(
                &input, bytes, sizeof(bytes), &size) ==
                HERMAS2_RESULT_STORE_OK &&
                size == HERMAS2_RESULT_HEADER_SIZE + sizeof(value) &&
                memcmp(bytes, "H2RS", 4u) == 0,
            "result record did not encode");
    hermas2_result_record decoded;
    size_t decoded_size = 0u;
    require(hermas2_result_decode(
                bytes, size, &decoded, &decoded_size) ==
                HERMAS2_RESULT_STORE_OK &&
                decoded_size == size &&
                decoded.outcome == HERMAS2_OUTCOME_APP_ERROR &&
                decoded.value_length == sizeof(value) &&
                memcmp(decoded.value, value, sizeof(value)) == 0,
            "result record did not round trip");
    hermas2_result_summary summary;
    require(hermas2_result_scan(
                bytes, size, NULL, NULL, &summary) ==
                HERMAS2_RESULT_STORE_OK &&
                summary.record_count == 1u &&
                summary.next_sequence == 2u,
            "result log did not scan");
    uint8_t output[8];
    int found = 0;
    hermas2_result_record match;
    require(hermas2_result_find(
                bytes, size, input.key, &match, output,
                sizeof(output), &found) ==
                HERMAS2_RESULT_STORE_OK &&
                found == 1 && match.value == output &&
                memcmp(output, value, sizeof(value)) == 0,
            "exact terminal result lookup failed");

    input.sequence = 2u;
    size_t second = 0u;
    require(hermas2_result_encode(
                &input, bytes + size, sizeof(bytes) - size,
                &second) == HERMAS2_RESULT_STORE_OK &&
                hermas2_result_scan(
                    bytes, size + second, NULL, NULL, &summary) ==
                    HERMAS2_RESULT_STORE_DUPLICATE_RESULT &&
                hermas2_result_find(
                    bytes, size + second, input.key, &match,
                    output, sizeof(output), &found) ==
                    HERMAS2_RESULT_STORE_DUPLICATE_RESULT,
            "ambiguous terminal result was accepted");
    bytes[HERMAS2_RESULT_HEADER_SIZE] ^= 1u;
    require(hermas2_result_decode(
                bytes, size, &decoded, &decoded_size) ==
                HERMAS2_RESULT_STORE_CHECKSUM_MISMATCH,
            "result value corruption was accepted");
    bytes[HERMAS2_RESULT_HEADER_SIZE] ^= 1u;
    bytes[60u] = 1u;
    require(hermas2_result_decode(
                bytes, size, &decoded, &decoded_size) ==
                HERMAS2_RESULT_STORE_INVALID_RECORD,
            "nonzero reserved result field was accepted");
}

typedef struct memory_sink {
    uint8_t bytes[HERMAS2_RESULT_HEADER_SIZE + 8u];
    size_t length;
} memory_sink;

static hermas2_result_store_result memory_write(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    memory_sink *sink = context;
    if (record_size > sizeof(sink->bytes) - sink->length) {
        return HERMAS2_RESULT_STORE_WRITE_ERROR;
    }
    memcpy(sink->bytes + sink->length, record, record_size);
    sink->length += record_size;
    return HERMAS2_RESULT_STORE_OK;
}

static void test_writer(void) {
    memory_sink sink;
    memset(&sink, 0, sizeof(sink));
    hermas2_result_writer writer;
    uint8_t value[8] = {9u};
    uint8_t scratch[HERMAS2_RESULT_HEADER_SIZE + 8u];
    hermas2_result_record record =
        result_record(0u, value, sizeof(value));
    require(hermas2_result_writer_init(
                &writer, memory_write, &sink, 1u) ==
                HERMAS2_RESULT_STORE_OK &&
                hermas2_result_writer_append(
                    &writer, record, scratch, sizeof(scratch)) ==
                    HERMAS2_RESULT_STORE_OK &&
                writer.next_sequence == 2u,
            "durable result writer did not advance");
    require(hermas2_result_writer_append(
                &writer, record, scratch, sizeof(scratch)) ==
                HERMAS2_RESULT_STORE_WRITE_ERROR &&
                writer.next_sequence == 2u,
            "failed result write consumed a sequence");
}

int main(void) {
    test_codec_scan_and_lookup();
    test_writer();
    if (failures != 0) {
        fprintf(stderr, "%d terminal result tests failed\n", failures);
        return 1;
    }
    puts("terminal result store tests passed");
    return 0;
}
