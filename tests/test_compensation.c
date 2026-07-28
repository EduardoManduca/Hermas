#include "hermas/compensation.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_compensation: %s\n", message);
        ++failures;
    }
}

static hermas_compensation_record token_record(
    uint64_t sequence,
    const uint8_t *token,
    uint32_t token_length) {
    return (hermas_compensation_record){
        .sequence = sequence,
        .key = {
            .execution_id = 19u,
            .workflow_id = 7u,
            .request_id = 3u,
            .node_id = 2u,
            .image_fingerprint = UINT64_C(0x1020304050607080)
        },
        .compensation_app_id = 4u,
        .compensation_action_id = 5u,
        .source_type = 6u,
        .destination_type = 7u,
        .token = token,
        .token_length = token_length
    };
}

static void test_codec_scan_and_lookup(void) {
    uint8_t value[8] = {42u};
    hermas_compensation_record input =
        token_record(1u, value, sizeof(value));
    uint8_t bytes[2u * (HERMAS_COMPENSATION_HEADER_SIZE + 8u)];
    size_t size = 0u;
    require(hermas_compensation_encode(
                &input, bytes, sizeof(bytes), &size) ==
                HERMAS_COMPENSATION_OK &&
                size == HERMAS_COMPENSATION_HEADER_SIZE + sizeof(value) &&
                memcmp(bytes, "HCT1", 4u) == 0,
            "token record did not encode");
    hermas_compensation_record decoded;
    size_t decoded_size = 0u;
    require(hermas_compensation_decode(
                bytes, size, &decoded, &decoded_size) ==
                HERMAS_COMPENSATION_OK &&
                decoded_size == size &&
                decoded.sequence == 1u &&
                decoded.compensation_action_id == 5u &&
                decoded.token_length == sizeof(value) &&
                memcmp(decoded.token, value, sizeof(value)) == 0,
            "token record did not round trip");
    hermas_compensation_summary summary;
    require(hermas_compensation_scan(
                bytes, size, NULL, NULL, &summary) ==
                HERMAS_COMPENSATION_OK &&
                summary.record_count == 1u &&
                summary.next_sequence == 2u,
            "token log did not scan");
    uint8_t output[8];
    int found = 0;
    hermas_compensation_record match;
    require(hermas_compensation_find(
                bytes, size, input.key, &match, output,
                sizeof(output), &found) == HERMAS_COMPENSATION_OK &&
                found == 1 && match.token == output &&
                memcmp(output, value, sizeof(value)) == 0,
            "exact compensation token lookup failed");

    input.sequence = 2u;
    size_t second = 0u;
    require(hermas_compensation_encode(
                &input, bytes + size, sizeof(bytes) - size, &second) ==
                HERMAS_COMPENSATION_OK &&
                hermas_compensation_find(
                    bytes, size + second, input.key, &match, output,
                    sizeof(output), &found) ==
                    HERMAS_COMPENSATION_DUPLICATE_TOKEN,
            "ambiguous compensation token was accepted");
    bytes[HERMAS_COMPENSATION_HEADER_SIZE] ^= 1u;
    require(hermas_compensation_decode(
                bytes, size, &decoded, &decoded_size) ==
                HERMAS_COMPENSATION_CHECKSUM_MISMATCH,
            "token corruption was accepted");
}

typedef struct memory_sink {
    uint8_t bytes[HERMAS_COMPENSATION_HEADER_SIZE + 8u];
    size_t length;
} memory_sink;

static hermas_compensation_result memory_write(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    memory_sink *sink = context;
    if (record_size > sizeof(sink->bytes) - sink->length) {
        return HERMAS_COMPENSATION_WRITE_ERROR;
    }
    memcpy(sink->bytes + sink->length, record, record_size);
    sink->length += record_size;
    return HERMAS_COMPENSATION_OK;
}

static void test_writer(void) {
    memory_sink sink;
    memset(&sink, 0, sizeof(sink));
    hermas_compensation_writer writer;
    uint8_t value[8] = {9u};
    uint8_t scratch[HERMAS_COMPENSATION_HEADER_SIZE + 8u];
    hermas_compensation_record record =
        token_record(0u, value, sizeof(value));
    require(hermas_compensation_writer_init(
                &writer, memory_write, &sink, 1u) ==
                HERMAS_COMPENSATION_OK &&
                hermas_compensation_writer_append(
                    &writer, record, scratch, sizeof(scratch)) ==
                    HERMAS_COMPENSATION_OK &&
                writer.next_sequence == 2u,
            "durable token writer did not advance");
    require(hermas_compensation_writer_append(
                &writer, record, scratch, sizeof(scratch)) ==
                HERMAS_COMPENSATION_WRITE_ERROR &&
                writer.next_sequence == 2u,
            "failed token write consumed a sequence");
}

int main(void) {
    test_codec_scan_and_lookup();
    test_writer();
    if (failures != 0) {
        fprintf(stderr, "%d compensation tests failed\n", failures);
        return 1;
    }
    puts("compensation token tests passed");
    return 0;
}
