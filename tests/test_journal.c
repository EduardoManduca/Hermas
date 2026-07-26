#include "hermas2/journal.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_journal: %s\n", message);
        ++failures;
    }
}

static hermas2_journal_record action_record(
    hermas2_journal_kind kind,
    uint16_t outcome,
    uint64_t sequence) {
    return (hermas2_journal_record){
        .kind = kind,
        .outcome = outcome,
        .sequence = sequence,
        .execution_id = 17u,
        .workflow_id = 3u,
        .request_id = 9u,
        .node_id = 2u,
        .app_id = 4u,
        .action_id = 5u,
        .image_fingerprint = UINT64_C(0x1122334455667788)
    };
}

static hermas2_journal_record execution_record(
    hermas2_journal_kind kind,
    uint16_t outcome,
    uint64_t sequence) {
    hermas2_journal_record record =
        action_record(kind, outcome, sequence);
    record.request_id = 0u;
    record.node_id = 0u;
    record.app_id = 0u;
    record.action_id = 0u;
    return record;
}

static void test_codec(void) {
    hermas2_journal_record prepared = action_record(
        HERMAS2_JOURNAL_DELIVERY_PREPARED,
        HERMAS2_OUTCOME_NONE, UINT64_C(0x0102030405060708));
    uint8_t encoded[HERMAS2_JOURNAL_RECORD_SIZE];
    hermas2_journal_record decoded;
    require(hermas2_journal_encode(
                &prepared, encoded, sizeof(encoded)) ==
                HERMAS2_JOURNAL_OK,
            "could not encode prepared record");
    require(memcmp(encoded, "H2JR", 4u) == 0 &&
                encoded[4] == HERMAS2_JOURNAL_VERSION &&
                encoded[6] == HERMAS2_JOURNAL_RECORD_SIZE &&
                encoded[16] == 8u && encoded[23] == 1u &&
                encoded[24] == 17u && encoded[32] == 3u &&
                encoded[36] == 9u && encoded[44] == 2u &&
                encoded[46] == 4u && encoded[48] == 5u &&
                encoded[52] == 0x88u && encoded[59] == 0x11u,
            "prepared record bytes differ");
    require(hermas2_journal_decode(
                encoded, sizeof(encoded), &decoded) ==
                HERMAS2_JOURNAL_OK &&
                decoded.kind == prepared.kind &&
                decoded.outcome == prepared.outcome &&
                decoded.sequence == prepared.sequence &&
                decoded.execution_id == prepared.execution_id &&
                decoded.workflow_id == prepared.workflow_id &&
                decoded.request_id == prepared.request_id &&
                decoded.node_id == prepared.node_id &&
                decoded.app_id == prepared.app_id &&
                decoded.action_id == prepared.action_id &&
                decoded.image_fingerprint ==
                    prepared.image_fingerprint,
            "prepared record did not round trip");
    encoded[32] ^= 1u;
    require(hermas2_journal_decode(
                encoded, sizeof(encoded), &decoded) ==
                HERMAS2_JOURNAL_CHECKSUM_MISMATCH,
            "journal corruption was accepted");
    require(hermas2_journal_decode(
                encoded, sizeof(encoded) - 1u, &decoded) ==
                HERMAS2_JOURNAL_BUFFER_TOO_SMALL,
            "truncated record was accepted");
}

static size_t append_encoded(
    uint8_t *bytes,
    size_t offset,
    hermas2_journal_record record) {
    require(hermas2_journal_encode(
                &record, bytes + offset,
                HERMAS2_JOURNAL_RECORD_SIZE) == HERMAS2_JOURNAL_OK,
            "scan fixture record could not encode");
    return offset + HERMAS2_JOURNAL_RECORD_SIZE;
}

static void test_scan_and_restart_classification(void) {
    uint8_t bytes[8u * HERMAS2_JOURNAL_RECORD_SIZE];
    size_t length = 0u;
    length = append_encoded(bytes, length, execution_record(
        HERMAS2_JOURNAL_EXECUTION_STARTED,
        HERMAS2_OUTCOME_NONE, 1u));
    length = append_encoded(bytes, length, action_record(
        HERMAS2_JOURNAL_DELIVERY_PREPARED,
        HERMAS2_OUTCOME_NONE, 2u));
    length = append_encoded(bytes, length, action_record(
        HERMAS2_JOURNAL_DELIVERY_SENT,
        HERMAS2_OUTCOME_NONE, 3u));
    hermas2_journal_summary summary;
    require(hermas2_journal_scan(
                bytes, length, NULL, NULL, &summary) ==
                HERMAS2_JOURNAL_OK &&
                summary.record_count == 3u &&
                summary.next_sequence == 4u &&
                summary.next_execution_id == 18u &&
                summary.interrupted_count == 1u &&
                summary.interrupted[0].has_open_delivery == 1u &&
                summary.interrupted[0].delivery_was_sent == 1u &&
                summary.interrupted[0].request_id == 9u,
            "sent crash was not classified as interrupted");

    length = append_encoded(bytes, length, action_record(
        HERMAS2_JOURNAL_ACTION_UNKNOWN,
        HERMAS2_OUTCOME_UNKNOWN, 4u));
    length = append_encoded(bytes, length, execution_record(
        HERMAS2_JOURNAL_EXECUTION_FINISHED,
        HERMAS2_OUTCOME_UNKNOWN, 5u));
    require(hermas2_journal_scan(
                bytes, length, NULL, NULL, &summary) ==
                HERMAS2_JOURNAL_OK &&
                summary.interrupted_count == 0u &&
                summary.next_sequence == 6u,
            "durably closed Unknown remained interrupted");

    bytes[2u * HERMAS2_JOURNAL_RECORD_SIZE + 16u] = 7u;
    require(hermas2_journal_scan(
                bytes, length, NULL, NULL, &summary) ==
                HERMAS2_JOURNAL_CHECKSUM_MISMATCH,
            "mutated sequence checksum was accepted");

    uint8_t invalid[4u * HERMAS2_JOURNAL_RECORD_SIZE];
    size_t invalid_length = 0u;
    invalid_length = append_encoded(invalid, invalid_length,
        execution_record(HERMAS2_JOURNAL_EXECUTION_STARTED,
                         HERMAS2_OUTCOME_NONE, 1u));
    invalid_length = append_encoded(invalid, invalid_length,
        action_record(HERMAS2_JOURNAL_DELIVERY_PREPARED,
                      HERMAS2_OUTCOME_NONE, 2u));
    invalid_length = append_encoded(invalid, invalid_length,
        action_record(HERMAS2_JOURNAL_ACTION_FAILED,
                      HERMAS2_OUTCOME_NOT_SENT, 3u));
    invalid_length = append_encoded(invalid, invalid_length,
        execution_record(HERMAS2_JOURNAL_EXECUTION_FINISHED,
                         HERMAS2_OUTCOME_SUCCESS, 4u));
    require(hermas2_journal_scan(
                invalid, invalid_length, NULL, NULL, &summary) ==
                HERMAS2_JOURNAL_INVALID_TRANSITION,
            "terminal outcome mismatch was accepted");
}

typedef struct memory_sink {
    uint8_t bytes[2u * HERMAS2_JOURNAL_RECORD_SIZE];
    size_t length;
    int fail;
} memory_sink;

static hermas2_journal_result memory_write(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    memory_sink *sink = context;
    if (sink->fail || record_size > sizeof(sink->bytes) - sink->length) {
        return HERMAS2_JOURNAL_WRITE_ERROR;
    }
    memcpy(sink->bytes + sink->length, record, record_size);
    sink->length += record_size;
    return HERMAS2_JOURNAL_OK;
}

static void test_writer_commit_boundary(void) {
    memory_sink sink;
    memset(&sink, 0, sizeof(sink));
    hermas2_journal_writer writer;
    require(hermas2_journal_writer_init(
                &writer, memory_write, &sink, 4u) ==
                HERMAS2_JOURNAL_OK,
            "writer did not initialize");
    hermas2_journal_record record = execution_record(
        HERMAS2_JOURNAL_EXECUTION_STARTED,
        HERMAS2_OUTCOME_NONE, 0u);
    require(hermas2_journal_writer_append(&writer, record) ==
                HERMAS2_JOURNAL_OK &&
                writer.next_sequence == 5u &&
                sink.length == HERMAS2_JOURNAL_RECORD_SIZE,
            "writer did not commit one record");
    sink.fail = 1;
    require(hermas2_journal_writer_append(&writer, record) ==
                HERMAS2_JOURNAL_WRITE_ERROR &&
                writer.next_sequence == 5u,
            "failed durable write consumed a sequence");
}

int main(void) {
    test_codec();
    test_scan_and_restart_classification();
    test_writer_commit_boundary();
    if (failures != 0) {
        fprintf(stderr, "%d journal tests failed\n", failures);
        return 1;
    }
    puts("journal tests passed");
    return 0;
}
