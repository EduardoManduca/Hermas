#include "hermas/saga_log.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_saga_log: %s\n", message);
        ++failures;
    }
}

static hermas_saga_log_record record(
    hermas_saga_log_kind kind,
    uint16_t outcome,
    uint64_t sequence,
    uint16_t ordinal) {
    hermas_saga_log_record value = {
        .kind = kind,
        .outcome = outcome,
        .sequence = sequence,
        .execution_id = 31u,
        .workflow_id = 7u,
        .ordinal = ordinal,
        .image_fingerprint = UINT64_C(0x1122334455667788)
    };
    if (kind >= HERMAS_SAGA_LOG_DELIVERY_PREPARED &&
        kind <= HERMAS_SAGA_LOG_STEP_UNKNOWN) {
        value.request_id = 19u + ordinal;
        value.forward_node = ordinal;
        value.app_id = 4u;
        value.action_id = 5u;
    }
    return value;
}

static size_t append(
    uint8_t *bytes,
    size_t offset,
    hermas_saga_log_record value) {
    require(hermas_saga_log_encode(
                &value, bytes + offset,
                HERMAS_SAGA_LOG_RECORD_SIZE) ==
                HERMAS_SAGA_LOG_OK,
            "could not encode fixture record");
    return offset + HERMAS_SAGA_LOG_RECORD_SIZE;
}

static void test_codec_and_reverse_progress(void) {
    uint8_t bytes[8u * HERMAS_SAGA_LOG_RECORD_SIZE];
    size_t length = 0u;
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_STARTED, HERMAS_OUTCOME_APP_ERROR,
        1u, 2u));
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_DELIVERY_PREPARED, HERMAS_OUTCOME_NONE,
        2u, 2u));
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_DELIVERY_SENT, HERMAS_OUTCOME_NONE,
        3u, 2u));
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_STEP_SUCCEEDED, HERMAS_OUTCOME_SUCCESS,
        4u, 2u));
    hermas_saga_log_summary summary;
    require(hermas_saga_log_scan(
                bytes, length, &summary) == HERMAS_SAGA_LOG_OK &&
                summary.next_sequence == 5u &&
                summary.active_count == 1u &&
                summary.active[0].next_ordinal == 1u &&
                summary.active[0].has_open_delivery == 0u,
            "durable success did not expose the predecessor");

    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_DELIVERY_PREPARED, HERMAS_OUTCOME_NONE,
        5u, 1u));
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_DELIVERY_SENT, HERMAS_OUTCOME_NONE,
        6u, 1u));
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_STEP_SUCCEEDED, HERMAS_OUTCOME_SUCCESS,
        7u, 1u));
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_FINISHED, HERMAS_OUTCOME_SUCCESS,
        8u, 0u));
    require(hermas_saga_log_scan(
                bytes, length, &summary) == HERMAS_SAGA_LOG_OK &&
                summary.active_count == 0u,
            "finished reverse plan remained active");

    hermas_saga_log_record decoded;
    require(hermas_saga_log_decode(
                bytes, sizeof(bytes), &decoded) ==
                HERMAS_SAGA_LOG_OK &&
                decoded.kind == HERMAS_SAGA_LOG_STARTED &&
                decoded.ordinal == 2u,
            "saga log record did not round trip");
    bytes[60] ^= 1u;
    require(hermas_saga_log_decode(
                bytes, sizeof(bytes), &decoded) ==
                HERMAS_SAGA_LOG_CHECKSUM_MISMATCH,
            "checksum mutation was accepted");
}

static void test_restart_and_invalid_transitions(void) {
    uint8_t bytes[4u * HERMAS_SAGA_LOG_RECORD_SIZE];
    size_t length = 0u;
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_STARTED, HERMAS_OUTCOME_NOT_SENT,
        1u, 2u));
    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_DELIVERY_PREPARED, HERMAS_OUTCOME_NONE,
        2u, 2u));
    hermas_saga_log_summary summary;
    require(hermas_saga_log_scan(
                bytes, length, &summary) == HERMAS_SAGA_LOG_OK &&
                summary.active_count == 1u &&
                summary.active[0].has_open_delivery == 1u &&
                summary.active[0].delivery_was_sent == 0u,
            "prepared crash was not classified");

    length = append(bytes, length, record(
        HERMAS_SAGA_LOG_DELIVERY_SENT, HERMAS_OUTCOME_NONE,
        3u, 2u));
    require(hermas_saga_log_scan(
                bytes, length, &summary) == HERMAS_SAGA_LOG_OK &&
                summary.active[0].delivery_was_sent == 1u,
            "sent crash was not classified");

    hermas_saga_log_record wrong = record(
        HERMAS_SAGA_LOG_STEP_SUCCEEDED, HERMAS_OUTCOME_SUCCESS,
        4u, 1u);
    append(bytes, length, wrong);
    require(hermas_saga_log_scan(
                bytes, sizeof(bytes), &summary) ==
                HERMAS_SAGA_LOG_INVALID_TRANSITION,
            "out-of-order reverse success was accepted");
}

typedef struct sink {
    uint8_t bytes[HERMAS_SAGA_LOG_RECORD_SIZE];
    int fail;
} sink;

static hermas_saga_log_result write_record(
    void *context,
    const uint8_t *bytes,
    size_t size) {
    sink *output = context;
    if (output->fail || size != sizeof(output->bytes)) {
        return HERMAS_SAGA_LOG_WRITE_ERROR;
    }
    memcpy(output->bytes, bytes, size);
    return HERMAS_SAGA_LOG_OK;
}

static void test_writer_boundary(void) {
    sink output;
    memset(&output, 0, sizeof(output));
    hermas_saga_log_writer writer;
    require(hermas_saga_log_writer_init(
                &writer, write_record, &output, 1u) ==
                HERMAS_SAGA_LOG_OK &&
                hermas_saga_log_writer_append(
                    &writer, record(HERMAS_SAGA_LOG_STARTED,
                                    HERMAS_OUTCOME_APP_ERROR,
                                    0u, 1u)) ==
                    HERMAS_SAGA_LOG_OK &&
                writer.next_sequence == 2u,
            "writer did not commit one record");
    output.fail = 1;
    require(hermas_saga_log_writer_append(
                &writer, record(HERMAS_SAGA_LOG_STARTED,
                                HERMAS_OUTCOME_APP_ERROR,
                                0u, 1u)) ==
                HERMAS_SAGA_LOG_WRITE_ERROR &&
                writer.next_sequence == 2u,
            "failed write consumed a sequence");
}

int main(void) {
    test_codec_and_reverse_progress();
    test_restart_and_invalid_transitions();
    test_writer_boundary();
    if (failures != 0) {
        fprintf(stderr, "%d saga log tests failed\n", failures);
        return 1;
    }
    puts("saga log tests passed");
    return 0;
}
