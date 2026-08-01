#include "hermas/journal.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_journal: %s\n", message);
        ++failures;
    }
}

static hermas_journal_record action_record(
    hermas_journal_kind kind,
    uint16_t outcome,
    uint64_t sequence) {
    return (hermas_journal_record){
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

static hermas_journal_record execution_record(
    hermas_journal_kind kind,
    uint16_t outcome,
    uint64_t sequence) {
    hermas_journal_record record =
        action_record(kind, outcome, sequence);
    record.request_id = 0u;
    record.node_id = 0u;
    record.app_id = 0u;
    record.action_id = 0u;
    return record;
}

static void test_codec(void) {
    hermas_journal_record prepared = action_record(
        HERMAS_JOURNAL_DELIVERY_PREPARED,
        HERMAS_OUTCOME_NONE, UINT64_C(0x0102030405060708));
    uint8_t encoded[HERMAS_JOURNAL_RECORD_SIZE];
    hermas_journal_record decoded;
    require(hermas_journal_encode(
                &prepared, encoded, sizeof(encoded)) ==
                HERMAS_JOURNAL_OK,
            "could not encode prepared record");
    require(memcmp(encoded, "HJR1", 4u) == 0 &&
                encoded[4] == HERMAS_JOURNAL_VERSION &&
                encoded[6] == HERMAS_JOURNAL_RECORD_SIZE &&
                encoded[16] == 8u && encoded[23] == 1u &&
                encoded[24] == 17u && encoded[32] == 3u &&
                encoded[36] == 9u && encoded[44] == 2u &&
                encoded[46] == 4u && encoded[48] == 5u &&
                encoded[52] == 0x88u && encoded[59] == 0x11u,
            "prepared record bytes differ");
    require(hermas_journal_decode(
                encoded, sizeof(encoded), &decoded) ==
                HERMAS_JOURNAL_OK &&
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
    encoded[4] = 1u;
    require(hermas_journal_decode(
                encoded, sizeof(encoded), &decoded) ==
                HERMAS_JOURNAL_INVALID_VERSION,
            "journal v1 was silently reinterpreted");
    encoded[4] = HERMAS_JOURNAL_VERSION;
    encoded[32] ^= 1u;
    require(hermas_journal_decode(
                encoded, sizeof(encoded), &decoded) ==
                HERMAS_JOURNAL_CHECKSUM_MISMATCH,
            "journal corruption was accepted");
    require(hermas_journal_decode(
                encoded, sizeof(encoded) - 1u, &decoded) ==
                HERMAS_JOURNAL_BUFFER_TOO_SMALL,
            "truncated record was accepted");
}

static size_t append_encoded(
    uint8_t *bytes,
    size_t offset,
    hermas_journal_record record) {
    require(hermas_journal_encode(
                &record, bytes + offset,
                HERMAS_JOURNAL_RECORD_SIZE) == HERMAS_JOURNAL_OK,
            "scan fixture record could not encode");
    return offset + HERMAS_JOURNAL_RECORD_SIZE;
}

static void test_scan_and_restart_classification(void) {
    uint8_t bytes[8u * HERMAS_JOURNAL_RECORD_SIZE];
    size_t length = 0u;
    length = append_encoded(bytes, length, execution_record(
        HERMAS_JOURNAL_EXECUTION_STARTED,
        HERMAS_OUTCOME_NONE, 1u));
    length = append_encoded(bytes, length, action_record(
        HERMAS_JOURNAL_DELIVERY_PREPARED,
        HERMAS_OUTCOME_NONE, 2u));
    length = append_encoded(bytes, length, action_record(
        HERMAS_JOURNAL_DELIVERY_SENT,
        HERMAS_OUTCOME_NONE, 3u));
    hermas_journal_record second = action_record(
        HERMAS_JOURNAL_DELIVERY_PREPARED,
        HERMAS_OUTCOME_NONE, 4u);
    second.request_id = 10u;
    second.node_id = 3u;
    second.app_id = 6u;
    second.action_id = 7u;
    length = append_encoded(bytes, length, second);
    hermas_journal_summary summary;
    require(hermas_journal_scan(
                bytes, length, NULL, NULL, &summary) ==
                HERMAS_JOURNAL_OK &&
                summary.record_count == 4u &&
                summary.next_sequence == 5u &&
                summary.next_execution_id == 18u &&
                summary.interrupted_count == 1u &&
                summary.interrupted[0].open_delivery_count == 2u &&
                summary.interrupted[0].open_deliveries[0]
                        .delivery_was_sent == 1u &&
                summary.interrupted[0].open_deliveries[0]
                        .request_id == 9u &&
                summary.interrupted[0].open_deliveries[1]
                        .delivery_was_sent == 0u &&
                summary.interrupted[0].open_deliveries[1]
                        .request_id == 10u,
            "overlapping crash was not classified per delivery");

    second.kind = HERMAS_JOURNAL_ACTION_UNKNOWN;
    second.outcome = HERMAS_OUTCOME_UNKNOWN;
    second.sequence = 5u;
    length = append_encoded(bytes, length, second);
    length = append_encoded(bytes, length, action_record(
        HERMAS_JOURNAL_ACTION_UNKNOWN,
        HERMAS_OUTCOME_UNKNOWN, 6u));
    length = append_encoded(bytes, length, execution_record(
        HERMAS_JOURNAL_EXECUTION_FINISHED,
        HERMAS_OUTCOME_UNKNOWN, 7u));
    require(hermas_journal_scan(
                bytes, length, NULL, NULL, &summary) ==
                HERMAS_JOURNAL_OK &&
                summary.interrupted_count == 0u &&
                summary.next_sequence == 8u,
            "durably closed Unknown remained interrupted");

    bytes[2u * HERMAS_JOURNAL_RECORD_SIZE + 16u] = 7u;
    require(hermas_journal_scan(
                bytes, length, NULL, NULL, &summary) ==
                HERMAS_JOURNAL_CHECKSUM_MISMATCH,
            "mutated sequence checksum was accepted");

    uint8_t invalid[4u * HERMAS_JOURNAL_RECORD_SIZE];
    size_t invalid_length = 0u;
    invalid_length = append_encoded(invalid, invalid_length,
        execution_record(HERMAS_JOURNAL_EXECUTION_STARTED,
                         HERMAS_OUTCOME_NONE, 1u));
    invalid_length = append_encoded(invalid, invalid_length,
        action_record(HERMAS_JOURNAL_DELIVERY_PREPARED,
                      HERMAS_OUTCOME_NONE, 2u));
    invalid_length = append_encoded(invalid, invalid_length,
        action_record(HERMAS_JOURNAL_ACTION_FAILED,
                      HERMAS_OUTCOME_NOT_SENT, 3u));
    invalid_length = append_encoded(invalid, invalid_length,
        execution_record(HERMAS_JOURNAL_EXECUTION_FINISHED,
                         HERMAS_OUTCOME_SUCCESS, 4u));
    require(hermas_journal_scan(
                invalid, invalid_length, NULL, NULL, &summary) ==
                HERMAS_JOURNAL_INVALID_TRANSITION,
            "terminal outcome mismatch was accepted");

    uint8_t precedence[8u * HERMAS_JOURNAL_RECORD_SIZE];
    size_t precedence_length = 0u;
    hermas_journal_record left = action_record(
        HERMAS_JOURNAL_DELIVERY_PREPARED,
        HERMAS_OUTCOME_NONE, 2u);
    hermas_journal_record right = left;
    right.request_id = 10u;
    right.node_id = 3u;
    right.app_id = 6u;
    right.action_id = 7u;
    precedence_length = append_encoded(
        precedence, precedence_length,
        execution_record(HERMAS_JOURNAL_EXECUTION_STARTED,
                         HERMAS_OUTCOME_NONE, 1u));
    precedence_length = append_encoded(
        precedence, precedence_length, left);
    left.kind = HERMAS_JOURNAL_DELIVERY_SENT;
    left.sequence = 3u;
    precedence_length = append_encoded(
        precedence, precedence_length, left);
    right.sequence = 4u;
    precedence_length = append_encoded(
        precedence, precedence_length, right);
    right.kind = HERMAS_JOURNAL_DELIVERY_SENT;
    right.sequence = 5u;
    precedence_length = append_encoded(
        precedence, precedence_length, right);
    left.kind = HERMAS_JOURNAL_ACTION_FAILED;
    left.outcome = HERMAS_OUTCOME_APP_ERROR;
    left.sequence = 6u;
    precedence_length = append_encoded(
        precedence, precedence_length, left);
    right.kind = HERMAS_JOURNAL_ACTION_UNKNOWN;
    right.outcome = HERMAS_OUTCOME_UNKNOWN;
    right.sequence = 7u;
    precedence_length = append_encoded(
        precedence, precedence_length, right);
    precedence_length = append_encoded(
        precedence, precedence_length,
        execution_record(HERMAS_JOURNAL_EXECUTION_FINISHED,
                         HERMAS_OUTCOME_UNKNOWN, 8u));
    require(hermas_journal_scan(
                precedence, precedence_length, NULL, NULL, &summary) ==
                HERMAS_JOURNAL_OK && summary.interrupted_count == 0u,
            "Unknown did not dominate a concurrent app error");
}

typedef struct memory_sink {
    uint8_t bytes[2u * HERMAS_JOURNAL_RECORD_SIZE];
    size_t length;
    int fail;
} memory_sink;

static hermas_journal_result memory_write(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    memory_sink *sink = context;
    if (sink->fail || record_size > sizeof(sink->bytes) - sink->length) {
        return HERMAS_JOURNAL_WRITE_ERROR;
    }
    memcpy(sink->bytes + sink->length, record, record_size);
    sink->length += record_size;
    return HERMAS_JOURNAL_OK;
}

static void test_writer_commit_boundary(void) {
    memory_sink sink;
    memset(&sink, 0, sizeof(sink));
    hermas_journal_writer writer;
    require(hermas_journal_writer_init(
                &writer, memory_write, &sink, 4u) ==
                HERMAS_JOURNAL_OK,
            "writer did not initialize");
    hermas_journal_record record = execution_record(
        HERMAS_JOURNAL_EXECUTION_STARTED,
        HERMAS_OUTCOME_NONE, 0u);
    require(hermas_journal_writer_append(&writer, record) ==
                HERMAS_JOURNAL_OK &&
                writer.next_sequence == 5u &&
                sink.length == HERMAS_JOURNAL_RECORD_SIZE,
            "writer did not commit one record");
    sink.fail = 1;
    require(hermas_journal_writer_append(&writer, record) ==
                HERMAS_JOURNAL_WRITE_ERROR &&
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
