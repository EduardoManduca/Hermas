#include "hermas2/saga.h"

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
    hermas2_saga_step steps[3];
    uint8_t journal[12u * HERMAS2_JOURNAL_RECORD_SIZE];
    size_t journal_size;
    uint8_t tokens[2u * (HERMAS2_COMPENSATION_HEADER_SIZE + 8u)];
    size_t token_size;
} fixture;

static void append_journal(
    fixture *value,
    hermas2_journal_kind kind,
    uint16_t outcome,
    uint64_t request,
    uint16_t node) {
    hermas2_journal_record record = {
        .kind = kind,
        .outcome = outcome,
        .sequence =
            value->journal_size / HERMAS2_JOURNAL_RECORD_SIZE + 1u,
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
    require(hermas2_journal_encode(
                &record, value->journal + value->journal_size,
                sizeof(value->journal) - value->journal_size) ==
                HERMAS2_JOURNAL_OK,
            "could not encode journal fixture");
    value->journal_size += HERMAS2_JOURNAL_RECORD_SIZE;
}

static void append_token(
    fixture *value,
    const hermas2_saga_step *step,
    uint64_t request,
    uint8_t marker) {
    uint8_t token[8] = {0u};
    token[0] = marker;
    hermas2_compensation_record record = {
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
    require(hermas2_compensation_encode(
                &record, value->tokens + value->token_size,
                sizeof(value->tokens) - value->token_size,
                &encoded) == HERMAS2_COMPENSATION_OK,
            "could not encode token fixture");
    value->token_size += encoded;
}

static void initialize_fixture(fixture *value) {
    value->fingerprint = hermas2_journal_image_fingerprint(
        value->image, value->image_size);
    size_t regions = read_u32(value->image, 72u);
    uint16_t region_count = read_u16(value->image, 68u);
    size_t found = 0u;
    for (uint16_t index = 0u; index < region_count; ++index) {
        size_t offset = regions + (size_t)index * 16u;
        if (value->image[offset] != 3u) {
            continue;
        }
        hermas2_saga_step *step = &value->steps[found++];
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

    append_journal(value, HERMAS2_JOURNAL_EXECUTION_STARTED,
                   HERMAS2_OUTCOME_NONE, 0u, 0u);
    for (uint16_t node = 1u; node <= 2u; ++node) {
        append_journal(value, HERMAS2_JOURNAL_DELIVERY_PREPARED,
                       HERMAS2_OUTCOME_NONE, node, node);
        append_journal(value, HERMAS2_JOURNAL_DELIVERY_SENT,
                       HERMAS2_OUTCOME_NONE, node, node);
        append_journal(value, HERMAS2_JOURNAL_ACTION_SUCCEEDED,
                       HERMAS2_OUTCOME_SUCCESS, node, node);
        append_token(value, &value->steps[node - 1u], node,
                     (uint8_t)(10u + node));
    }
    append_journal(value, HERMAS2_JOURNAL_DELIVERY_PREPARED,
                   HERMAS2_OUTCOME_NONE, 3u, 3u);
    append_journal(value, HERMAS2_JOURNAL_DELIVERY_SENT,
                   HERMAS2_OUTCOME_NONE, 3u, 3u);
    append_journal(value, HERMAS2_JOURNAL_ACTION_FAILED,
                   HERMAS2_OUTCOME_APP_ERROR, 3u, 3u);
    append_journal(value, HERMAS2_JOURNAL_EXECUTION_FINISHED,
                   HERMAS2_OUTCOME_APP_ERROR, 0u, 0u);
}

static void accept_success(
    hermas2_saga_execution *execution,
    const hermas2_frame *invocation,
    uint16_t success_type) {
    hermas2_frame result = {
        .kind = HERMAS2_FRAME_RESULT,
        .execution_id = invocation->execution_id,
        .request_id = invocation->request_id,
        .app_id = invocation->app_id,
        .action_id = invocation->action_id,
        .source_type = success_type,
        .destination_type = success_type,
        .outcome = HERMAS2_OUTCOME_SUCCESS
    };
    require(hermas2_saga_mark_sent(execution) == HERMAS2_SAGA_OK &&
                hermas2_saga_accept_result(execution, &result) ==
                    HERMAS2_SAGA_OK,
            "successful compensation was rejected");
}

static void test_reverse_recovery(fixture *value) {
    hermas2_saga_execution execution;
    require(hermas2_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS2_SAGA_OK &&
                execution.state == HERMAS2_SAGA_READY &&
                execution.remaining == 2u,
            "known failure did not produce a compensation plan");

    uint8_t token[8];
    hermas2_frame invocation;
    require(hermas2_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS2_SAGA_OK &&
                invocation.app_id ==
                    value->steps[1].compensation_app_id &&
                invocation.action_id ==
                    value->steps[1].compensation_action_id &&
                token[0] == 12u,
            "second completed step was not compensated first");
    accept_success(&execution, &invocation,
                   value->steps[1].success_type);
    require(execution.state == HERMAS2_SAGA_READY &&
                execution.remaining == 1u,
            "first compensation did not reveal predecessor");

    require(hermas2_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS2_SAGA_OK &&
                invocation.app_id ==
                    value->steps[0].compensation_app_id &&
                token[0] == 11u,
            "first forward step was not compensated last");
    accept_success(&execution, &invocation,
                   value->steps[0].success_type);
    require(execution.state == HERMAS2_SAGA_COMPLETE &&
                execution.remaining == 0u,
            "saga did not complete after reverse compensation");
}

static void test_refusal_paths(fixture *value) {
    hermas2_saga_execution execution;
    require(hermas2_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, 0u, 41u, 7u) ==
                HERMAS2_SAGA_MISSING_TOKEN,
            "journal success without a token was accepted");

    fixture unsafe = {
        .image = value->image,
        .image_size = value->image_size,
        .fingerprint = value->fingerprint
    };
    append_journal(&unsafe, HERMAS2_JOURNAL_EXECUTION_STARTED,
                   HERMAS2_OUTCOME_NONE, 0u, 0u);
    append_journal(&unsafe, HERMAS2_JOURNAL_DELIVERY_PREPARED,
                   HERMAS2_OUTCOME_NONE, 1u, 1u);
    append_journal(&unsafe, HERMAS2_JOURNAL_DELIVERY_SENT,
                   HERMAS2_OUTCOME_NONE, 1u, 1u);
    append_journal(&unsafe, HERMAS2_JOURNAL_ACTION_UNKNOWN,
                   HERMAS2_OUTCOME_UNKNOWN, 1u, 1u);
    append_journal(&unsafe, HERMAS2_JOURNAL_EXECUTION_FINISHED,
                   HERMAS2_OUTCOME_UNKNOWN, 0u, 0u);
    require(hermas2_saga_recover(
                &execution, unsafe.image, unsafe.image_size,
                unsafe.journal, unsafe.journal_size,
                NULL, 0u, 41u, 7u) ==
                HERMAS2_SAGA_UNSAFE_HISTORY &&
                execution.state == HERMAS2_SAGA_BLOCKED,
            "Unknown history authorized compensation");

    fixture successful = {
        .image = value->image,
        .image_size = value->image_size,
        .fingerprint = value->fingerprint
    };
    append_journal(&successful, HERMAS2_JOURNAL_EXECUTION_STARTED,
                   HERMAS2_OUTCOME_NONE, 0u, 0u);
    for (uint16_t node = 1u; node <= 3u; ++node) {
        append_journal(&successful, HERMAS2_JOURNAL_DELIVERY_PREPARED,
                       HERMAS2_OUTCOME_NONE, node, node);
        append_journal(&successful, HERMAS2_JOURNAL_DELIVERY_SENT,
                       HERMAS2_OUTCOME_NONE, node, node);
        append_journal(&successful, HERMAS2_JOURNAL_ACTION_SUCCEEDED,
                       HERMAS2_OUTCOME_SUCCESS, node, node);
    }
    append_journal(&successful, HERMAS2_JOURNAL_EXECUTION_FINISHED,
                   HERMAS2_OUTCOME_SUCCESS, 0u, 0u);
    require(hermas2_saga_recover(
                &execution, successful.image, successful.image_size,
                successful.journal, successful.journal_size,
                NULL, 0u, 41u, 7u) == HERMAS2_SAGA_OK &&
                execution.state == HERMAS2_SAGA_COMPLETE &&
                execution.remaining == 0u,
            "successful saga incorrectly requested compensation");

    require(hermas2_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS2_SAGA_OK,
            "could not rebuild plan for failure test");
    uint8_t token[8];
    hermas2_frame invocation;
    require(hermas2_saga_prepare(
                &execution, token, sizeof(token), &invocation) ==
                HERMAS2_SAGA_OK &&
                hermas2_saga_mark_sent(&execution) ==
                    HERMAS2_SAGA_OK,
            "could not send compensation failure fixture");
    hermas2_frame failed = {
        .kind = HERMAS2_FRAME_RESULT,
        .execution_id = invocation.execution_id,
        .request_id = invocation.request_id,
        .app_id = invocation.app_id,
        .action_id = invocation.action_id,
        .source_type = value->steps[1].error_type,
        .destination_type = value->steps[1].error_type,
        .outcome = HERMAS2_OUTCOME_APP_ERROR
    };
    require(hermas2_saga_accept_result(&execution, &failed) ==
                HERMAS2_SAGA_OK &&
                execution.state == HERMAS2_SAGA_BLOCKED &&
                execution.remaining == 2u,
            "compensation app failure did not stop the saga");
}

static size_t append_saga_log(
    uint8_t *bytes,
    size_t size,
    const fixture *value,
    hermas2_saga_log_kind kind,
    uint16_t outcome,
    uint16_t ordinal) {
    hermas2_saga_log_record record = {
        .kind = kind,
        .outcome = outcome,
        .sequence = size / HERMAS2_SAGA_LOG_RECORD_SIZE + 1u,
        .execution_id = 41u,
        .workflow_id = 7u,
        .ordinal = ordinal,
        .image_fingerprint = value->fingerprint
    };
    if (kind >= HERMAS2_SAGA_LOG_DELIVERY_PREPARED &&
        kind <= HERMAS2_SAGA_LOG_STEP_UNKNOWN) {
        const hermas2_saga_step *step =
            &value->steps[ordinal - 1u];
        record.request_id = 10u + ordinal;
        record.forward_node = step->forward_node;
        record.app_id = step->compensation_app_id;
        record.action_id = step->compensation_action_id;
    }
    require(hermas2_saga_log_encode(
                &record, bytes + size,
                HERMAS2_SAGA_LOG_RECORD_SIZE) ==
                HERMAS2_SAGA_LOG_OK,
            "could not encode reconciliation log");
    return size + HERMAS2_SAGA_LOG_RECORD_SIZE;
}

static void test_durable_reconciliation(fixture *value) {
    uint8_t log[8u * HERMAS2_SAGA_LOG_RECORD_SIZE];
    size_t size = 0u;
    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_STARTED,
        HERMAS2_OUTCOME_APP_ERROR, 2u);
    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_DELIVERY_PREPARED,
        HERMAS2_OUTCOME_NONE, 2u);
    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_DELIVERY_SENT,
        HERMAS2_OUTCOME_NONE, 2u);
    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_STEP_SUCCEEDED,
        HERMAS2_OUTCOME_SUCCESS, 2u);

    hermas2_saga_execution execution;
    require(hermas2_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS2_SAGA_OK &&
                hermas2_saga_reconcile(&execution, log, size) ==
                    HERMAS2_SAGA_OK &&
                execution.remaining == 1u &&
                execution.state == HERMAS2_SAGA_READY,
            "durable reverse success was not skipped");

    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_DELIVERY_PREPARED,
        HERMAS2_OUTCOME_NONE, 1u);
    require(hermas2_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS2_SAGA_OK &&
                hermas2_saga_reconcile(&execution, log, size) ==
                    HERMAS2_SAGA_UNSAFE_HISTORY &&
                execution.state == HERMAS2_SAGA_BLOCKED &&
                execution.compensation_outcome ==
                    HERMAS2_OUTCOME_UNKNOWN,
            "open compensation delivery was replayable");

    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_DELIVERY_SENT,
        HERMAS2_OUTCOME_NONE, 1u);
    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_STEP_SUCCEEDED,
        HERMAS2_OUTCOME_SUCCESS, 1u);
    size = append_saga_log(
        log, size, value, HERMAS2_SAGA_LOG_FINISHED,
        HERMAS2_OUTCOME_SUCCESS, 0u);
    require(hermas2_saga_recover(
                &execution, value->image, value->image_size,
                value->journal, value->journal_size,
                value->tokens, value->token_size, 41u, 7u) ==
                HERMAS2_SAGA_OK &&
                hermas2_saga_reconcile(&execution, log, size) ==
                    HERMAS2_SAGA_OK &&
                execution.state == HERMAS2_SAGA_COMPLETE &&
                execution.remaining == 0u,
            "durably finished saga did not remain complete");
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
    free(image);
    if (failures != 0) {
        fprintf(stderr, "%d saga tests failed\n", failures);
        return 1;
    }
    puts("saga recovery tests passed");
    return 0;
}
