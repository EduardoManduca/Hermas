#include "hermas/saga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_saga: %s\n", message);
        ++failures;
    }
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    uint32_t value = 0u;
    for (size_t index = 0u; index < 4u; ++index) {
        value |= (uint32_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

typedef struct fixture {
    const uint8_t *image;
    size_t image_size;
    uint64_t fingerprint;
    hermas_saga_step steps[3];
    uint8_t journal[12u * HERMAS_JOURNAL_RECORD_SIZE];
    size_t journal_size;
    uint8_t tokens[2u * (HERMAS_COMPENSATION_HEADER_SIZE + 8u)];
    size_t token_size;
} fixture;

static hermas_compensation_result fixture_lookup(
    void *context,
    hermas_compensation_key key,
    hermas_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found) {
    fixture *value = context;
    return hermas_compensation_find(
        value->tokens, value->token_size, key, record, token,
        token_capacity, found);
}

static void append_journal(
    fixture *value,
    hermas_journal_kind kind,
    uint16_t outcome,
    uint64_t request,
    uint16_t node) {
    hermas_journal_record record = {
        .kind = kind,
        .outcome = outcome,
        .sequence =
            value->journal_size / HERMAS_JOURNAL_RECORD_SIZE + 1u,
        .execution_id = 41u,
        .workflow_id = 7u,
        .image_fingerprint = value->fingerprint
    };
    if (node != 0u) {
        size_t nodes = read_u32(value->image, 48u);
        size_t offset = nodes + ((size_t)node - 1u) * 8u;
        record.request_id = request;
        record.node_id = node;
        record.action_id = read_u16(value->image, offset + 2u);
        record.app_id = read_u16(value->image, offset + 4u);
    }
    require(hermas_journal_encode(
                &record, value->journal + value->journal_size,
                sizeof(value->journal) - value->journal_size) ==
                HERMAS_JOURNAL_OK,
            "could not encode journal fixture");
    value->journal_size += HERMAS_JOURNAL_RECORD_SIZE;
}

static void append_token(
    fixture *value,
    const hermas_saga_step *step,
    uint64_t request,
    uint8_t marker) {
    uint8_t token[8] = {0u};
    token[0] = marker;
    hermas_compensation_record record = {
        .sequence = value->token_size == 0u ? 1u : 2u,
        .key = {
            .execution_id = 41u,
            .workflow_id = 7u,
            .request_id = request,
            .node_id = step->forward_node,
            .image_fingerprint = value->fingerprint
        },
        .compensation_app_id = step->compensation_app_id,
        .compensation_action_id = step->compensation_action_id,
        .source_type = step->source_type,
        .destination_type = step->destination_type,
        .token = token,
        .token_length = sizeof(token)
    };
    size_t encoded = 0u;
    require(hermas_compensation_encode(
                &record, value->tokens + value->token_size,
                sizeof(value->tokens) - value->token_size,
                &encoded) == HERMAS_COMPENSATION_OK,
            "could not encode token fixture");
    value->token_size += encoded;
}

static void initialize_fixture(fixture *value) {
    value->fingerprint = hermas_journal_image_fingerprint(
        value->image, value->image_size);
    size_t regions = read_u32(value->image, 72u);
    uint16_t region_count = read_u16(value->image, 68u);
    size_t found = 0u;
    for (uint16_t index = 0u; index < region_count; ++index) {
        size_t offset = regions + (size_t)index * 16u;
        if (value->image[offset] != 3u) {
            continue;
        }
        hermas_saga_step *step = &value->steps[found++];
        step->forward_node = read_u16(value->image, offset + 2u);
        step->compensation_app_id =
            read_u16(value->image, offset + 4u);
        step->compensation_action_id =
            read_u16(value->image, offset + 6u);
        step->source_type = read_u16(value->image, offset + 8u);
        step->destination_type =
            read_u16(value->image, offset + 10u);
        step->ordinal = read_u16(value->image, offset + 12u);
        step->success_type = read_u16(value->image, offset + 20u);
        step->error_type = read_u16(value->image, offset + 22u);
    }
    require(found == 3u, "fixture does not contain three saga steps");

    append_journal(value, HERMAS_JOURNAL_EXECUTION_STARTED,
                   HERMAS_OUTCOME_NONE, 0u, 0u);
    for (uint16_t node = 1u; node <= 2u; ++node) {
        append_journal(value, HERMAS_JOURNAL_DELIVERY_PREPARED,
                       HERMAS_OUTCOME_NONE, node, node);
        append_journal(value, HERMAS_JOURNAL_DELIVERY_SENT,
                       HERMAS_OUTCOME_NONE, node, node);
        append_journal(value, HERMAS_JOURNAL_ACTION_SUCCEEDED,
                       HERMAS_OUTCOME_SUCCESS, node, node);
        append_token(value, &value->steps[node - 1u], node,
                     (uint8_t)(10u + node));
    }
    append_journal(value, HERMAS_JOURNAL_DELIVERY_PREPARED,
                   HERMAS_OUTCOME_NONE, 3u, 3u);
    append_journal(value, HERMAS_JOURNAL_DELIVERY_SENT,
                   HERMAS_OUTCOME_NONE, 3u, 3u);
    append_journal(value, HERMAS_JOURNAL_ACTION_FAILED,
                   HERMAS_OUTCOME_APP_ERROR, 3u, 3u);
    append_journal(value, HERMAS_JOURNAL_EXECUTION_FINISHED,
                   HERMAS_OUTCOME_APP_ERROR, 0u, 0u);
}

static void accept_success(
    hermas_saga_execution *execution,
    const hermas_frame *invocation,
    uint16_t success_type) {
    hermas_frame result = {
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = invocation->execution_id,
        .request_id = invocation->request_id,
        .app_id = invocation->app_id,
        .action_id = invocation->action_id,
        .source_type = success_type,
        .destination_type = success_type,
        .outcome = HERMAS_OUTCOME_SUCCESS
    };
    require(hermas_saga_mark_sent(execution) == HERMAS_SAGA_OK &&
                hermas_saga_accept_result(execution, &result) ==
                    HERMAS_SAGA_OK,
            "successful compensation was rejected");
}

static void test_reverse_recovery(fixture *value) {
    hermas_saga_execution execution;
    require(hermas_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS_SAGA_OK &&
                execution.state == HERMAS_SAGA_READY &&
                execution.remaining == 2u,
            "known failure did not produce a compensation plan");

    uint8_t token[8];
    hermas_frame invocation;
    require(hermas_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS_SAGA_OK &&
                invocation.app_id ==
                    value->steps[1].compensation_app_id &&
                invocation.action_id ==
                    value->steps[1].compensation_action_id &&
                token[0] == 12u,
            "second completed step was not compensated first");
    accept_success(&execution, &invocation,
                   value->steps[1].success_type);
    require(execution.state == HERMAS_SAGA_READY &&
                execution.remaining == 1u,
            "first compensation did not reveal predecessor");

    require(hermas_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS_SAGA_OK &&
                invocation.app_id ==
                    value->steps[0].compensation_app_id &&
                token[0] == 11u,
            "first forward step was not compensated last");
    accept_success(&execution, &invocation,
                   value->steps[0].success_type);
    require(execution.state == HERMAS_SAGA_COMPLETE &&
                execution.remaining == 0u,
            "saga did not complete after reverse compensation");
}

static void test_refusal_paths(fixture *value) {
    hermas_saga_execution execution;
    require(hermas_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, 0u, 41u, 7u) ==
                HERMAS_SAGA_MISSING_TOKEN,
            "journal success without a token was accepted");

    fixture unsafe = {
        .image = value->image,
        .image_size = value->image_size,
        .fingerprint = value->fingerprint
    };
    append_journal(&unsafe, HERMAS_JOURNAL_EXECUTION_STARTED,
                   HERMAS_OUTCOME_NONE, 0u, 0u);
    append_journal(&unsafe, HERMAS_JOURNAL_DELIVERY_PREPARED,
                   HERMAS_OUTCOME_NONE, 1u, 1u);
    append_journal(&unsafe, HERMAS_JOURNAL_DELIVERY_SENT,
                   HERMAS_OUTCOME_NONE, 1u, 1u);
    append_journal(&unsafe, HERMAS_JOURNAL_ACTION_UNKNOWN,
                   HERMAS_OUTCOME_UNKNOWN, 1u, 1u);
    append_journal(&unsafe, HERMAS_JOURNAL_EXECUTION_FINISHED,
                   HERMAS_OUTCOME_UNKNOWN, 0u, 0u);
    require(hermas_saga_recover(
                &execution, unsafe.image, unsafe.image_size,
                unsafe.journal, unsafe.journal_size,
                NULL, 0u, 41u, 7u) ==
                HERMAS_SAGA_UNSAFE_HISTORY &&
                execution.state == HERMAS_SAGA_BLOCKED,
            "Unknown history authorized compensation");

    fixture successful = {
        .image = value->image,
        .image_size = value->image_size,
        .fingerprint = value->fingerprint
    };
    append_journal(&successful, HERMAS_JOURNAL_EXECUTION_STARTED,
                   HERMAS_OUTCOME_NONE, 0u, 0u);
    for (uint16_t node = 1u; node <= 3u; ++node) {
        append_journal(&successful, HERMAS_JOURNAL_DELIVERY_PREPARED,
                       HERMAS_OUTCOME_NONE, node, node);
        append_journal(&successful, HERMAS_JOURNAL_DELIVERY_SENT,
                       HERMAS_OUTCOME_NONE, node, node);
        append_journal(&successful, HERMAS_JOURNAL_ACTION_SUCCEEDED,
                       HERMAS_OUTCOME_SUCCESS, node, node);
    }
    append_journal(&successful, HERMAS_JOURNAL_EXECUTION_FINISHED,
                   HERMAS_OUTCOME_SUCCESS, 0u, 0u);
    require(hermas_saga_recover(
                &execution, successful.image, successful.image_size,
                successful.journal, successful.journal_size,
                NULL, 0u, 41u, 7u) == HERMAS_SAGA_OK &&
                execution.state == HERMAS_SAGA_COMPLETE &&
                execution.remaining == 0u,
            "successful saga incorrectly requested compensation");

    require(hermas_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS_SAGA_OK,
            "could not rebuild plan for failure test");
    uint8_t token[8];
    hermas_frame invocation;
    execution.next_request_id = UINT64_MAX;
    require(hermas_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS_SAGA_REQUEST_ID_EXHAUSTED,
            "exhausted compensation request ID was reused");
    execution.next_request_id = 4u;
    require(hermas_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS_SAGA_OK &&
                hermas_saga_mark_sent(&execution) ==
                    HERMAS_SAGA_OK,
            "could not send compensation failure fixture");
    hermas_frame failed = {
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = invocation.execution_id,
        .request_id = invocation.request_id,
        .app_id = invocation.app_id,
        .action_id = invocation.action_id,
        .source_type = value->steps[1].error_type,
        .destination_type = value->steps[1].error_type,
        .outcome = HERMAS_OUTCOME_APP_ERROR
    };
    require(hermas_saga_accept_result(&execution, &failed) ==
                HERMAS_SAGA_OK &&
                execution.state == HERMAS_SAGA_BLOCKED &&
                execution.remaining == 2u,
            "compensation app failure did not stop the saga");
}

static size_t append_saga_log(
    uint8_t *bytes,
    size_t size,
    const fixture *value,
    hermas_saga_log_kind kind,
    uint16_t outcome,
    uint16_t ordinal) {
    hermas_saga_log_record record = {
        .kind = kind,
        .outcome = outcome,
        .sequence = size / HERMAS_SAGA_LOG_RECORD_SIZE + 1u,
        .execution_id = 41u,
        .workflow_id = 7u,
        .ordinal = ordinal,
        .image_fingerprint = value->fingerprint
    };
    if (kind >= HERMAS_SAGA_LOG_DELIVERY_PREPARED &&
        kind <= HERMAS_SAGA_LOG_STEP_UNKNOWN) {
        const hermas_saga_step *step =
            &value->steps[ordinal - 1u];
        record.request_id = 10u + ordinal;
        record.forward_node = step->forward_node;
        record.app_id = step->compensation_app_id;
        record.action_id = step->compensation_action_id;
    }
    require(hermas_saga_log_encode(
                &record, bytes + size,
                HERMAS_SAGA_LOG_RECORD_SIZE) ==
                HERMAS_SAGA_LOG_OK,
            "could not encode reconciliation log");
    return size + HERMAS_SAGA_LOG_RECORD_SIZE;
}

static void test_durable_reconciliation(fixture *value) {
    uint8_t log[8u * HERMAS_SAGA_LOG_RECORD_SIZE];
    size_t size = 0u;
    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_STARTED,
        HERMAS_OUTCOME_APP_ERROR, 2u);
    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_DELIVERY_PREPARED,
        HERMAS_OUTCOME_NONE, 2u);
    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_DELIVERY_SENT,
        HERMAS_OUTCOME_NONE, 2u);
    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_STEP_SUCCEEDED,
        HERMAS_OUTCOME_SUCCESS, 2u);

    hermas_saga_execution execution;
    require(hermas_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS_SAGA_OK &&
                hermas_saga_reconcile(&execution, log, size) ==
                    HERMAS_SAGA_OK &&
                execution.completed_steps == 2u &&
                execution.remaining == 1u &&
                execution.state == HERMAS_SAGA_READY,
            "durable reverse success was not skipped");

    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_DELIVERY_PREPARED,
        HERMAS_OUTCOME_NONE, 1u);
    require(hermas_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS_SAGA_OK &&
                hermas_saga_reconcile(&execution, log, size) ==
                    HERMAS_SAGA_UNSAFE_HISTORY &&
                execution.state == HERMAS_SAGA_BLOCKED &&
                execution.compensation_outcome ==
                    HERMAS_OUTCOME_UNKNOWN,
            "open compensation delivery was replayable");

    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_DELIVERY_SENT,
        HERMAS_OUTCOME_NONE, 1u);
    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_STEP_SUCCEEDED,
        HERMAS_OUTCOME_SUCCESS, 1u);
    size = append_saga_log(
        log, size, value, HERMAS_SAGA_LOG_FINISHED,
        HERMAS_OUTCOME_SUCCESS, 0u);
    require(hermas_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS_SAGA_OK &&
                hermas_saga_reconcile(&execution, log, size) ==
                    HERMAS_SAGA_OK &&
                execution.state == HERMAS_SAGA_COMPLETE &&
                execution.remaining == 0u,
            "durably finished saga did not remain complete");
}

typedef struct saga_sink {
    uint8_t bytes[8u * HERMAS_SAGA_LOG_RECORD_SIZE];
    size_t size;
} saga_sink;

static hermas_saga_log_result write_saga_record(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    saga_sink *sink = context;
    if (record_size > sizeof(sink->bytes) - sink->size) {
        return HERMAS_SAGA_LOG_WRITE_ERROR;
    }
    memcpy(sink->bytes + sink->size, record, record_size);
    sink->size += record_size;
    return HERMAS_SAGA_LOG_OK;
}

static void test_write_ahead_driver(fixture *value) {
    hermas_saga_execution recovered;
    require(hermas_saga_recover(
                &recovered, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS_SAGA_OK,
            "could not recover driver fixture");
    saga_sink sink;
    memset(&sink, 0, sizeof(sink));
    hermas_saga_log_writer writer;
    hermas_saga_driver driver;
    require(hermas_saga_log_writer_init(
                &writer, write_saga_record, &sink, 1u) ==
                HERMAS_SAGA_LOG_OK &&
                hermas_saga_driver_begin(
                    &driver, &recovered, &writer, 0) ==
                    HERMAS_SAGA_OK,
            "could not begin durable saga driver");
    for (uint8_t remaining = 2u; remaining > 0u; --remaining) {
        uint8_t token[8];
        hermas_frame invocation;
        require(hermas_saga_driver_prepare(
                    &driver, token, sizeof(token), &invocation) ==
                    HERMAS_SAGA_OK &&
                    hermas_saga_driver_mark_sent(&driver) ==
                    HERMAS_SAGA_OK,
                "driver did not record delivery boundary");
        const hermas_saga_step *step =
            &value->steps[remaining - 1u];
        hermas_frame result = {
            .kind = HERMAS_FRAME_RESULT,
            .execution_id = invocation.execution_id,
            .request_id = invocation.request_id,
            .app_id = invocation.app_id,
            .action_id = invocation.action_id,
            .source_type = step->success_type,
            .destination_type = step->success_type,
            .outcome = HERMAS_OUTCOME_SUCCESS
        };
        require(hermas_saga_driver_accept_result(
                    &driver, &result) == HERMAS_SAGA_OK,
                "driver did not record successful compensation");
    }
    hermas_saga_log_summary summary;
    require(driver.execution.state == HERMAS_SAGA_COMPLETE &&
                sink.size == 8u * HERMAS_SAGA_LOG_RECORD_SIZE &&
                hermas_saga_log_scan(
                    sink.bytes, sink.size, &summary) ==
                    HERMAS_SAGA_LOG_OK &&
                summary.active_count == 0u,
            "driver log is not a complete durable reverse history");
}

static void test_live_plan(fixture *value) {
    uint64_t requests[2] = {1u, 2u};
    hermas_saga_execution execution;
    require(hermas_saga_begin_live(
                &execution, value->image, value->image_size, 41u, 7u,
                HERMAS_OUTCOME_APP_ERROR, requests, 2u, 4u,
                fixture_lookup, value) == HERMAS_SAGA_OK &&
                execution.state == HERMAS_SAGA_READY &&
                execution.remaining == 2u &&
                execution.next_request_id == 4u,
            "durable live enrollments did not form a saga plan");
    uint8_t token[8];
    hermas_frame invocation;
    require(hermas_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS_SAGA_OK &&
                invocation.request_id == 4u &&
                invocation.app_id ==
                    value->steps[1].compensation_app_id &&
                token[0] == 12u,
            "live plan did not select the latest enrollment");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 2;
    }
    uint8_t *image = malloc((size_t)length);
    if (image == NULL ||
        fread(image, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(image);
        return 2;
    }
    fclose(file);
    fixture value;
    memset(&value, 0, sizeof(value));
    value.image = image;
    value.image_size = (size_t)length;
    initialize_fixture(&value);
    test_reverse_recovery(&value);
    test_refusal_paths(&value);
    test_durable_reconciliation(&value);
    test_write_ahead_driver(&value);
    test_live_plan(&value);
    free(image);
    if (failures != 0) {
        fprintf(stderr, "%d saga tests failed\n", failures);
        return 1;
    }
    puts("saga recovery tests passed");
    return 0;
}
