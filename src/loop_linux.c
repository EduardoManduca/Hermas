#include "hermas2/daemon.h"

#include "hermas2/image.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static hermas2_loop_slot *find_execution(
    hermas2_daemon_loop *loop,
    uint64_t execution_id) {
    for (size_t index = 0u; index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas2_loop_slot *slot = &loop->executions[index];
        if (slot->active && slot->execution.execution_id == execution_id) {
            return slot;
        }
    }
    return NULL;
}

static const hermas2_loop_slot *find_const_execution(
    const hermas2_daemon_loop *loop,
    uint64_t execution_id) {
    for (size_t index = 0u; index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        const hermas2_loop_slot *slot = &loop->executions[index];
        if (slot->active && slot->execution.execution_id == execution_id) {
            return slot;
        }
    }
    return NULL;
}

static void drop_app(hermas2_daemon_registry *registry, uint16_t app_id) {
    for (size_t index = 0u; index < registry->app_count; ++index) {
        hermas2_daemon_app *app = &registry->apps[index];
        if (app->app_id == app_id) {
            if (app->file_descriptor >= 0) {
                close(app->file_descriptor);
                app->file_descriptor = -1;
            }
            return;
        }
    }
}

static bool app_owned_by_other(
    const hermas2_daemon_loop *loop,
    const hermas2_loop_slot *candidate) {
    for (size_t index = 0u; index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        const hermas2_loop_slot *slot = &loop->executions[index];
        if (slot != candidate && slot->active && slot->owns_app &&
            slot->app_id == candidate->app_id) {
            return true;
        }
    }
    return false;
}

static hermas2_loop_result append_record(
    hermas2_daemon_loop *loop,
    const hermas2_loop_slot *slot,
    hermas2_journal_kind kind,
    uint16_t outcome,
    bool action) {
    if (loop->journal == NULL) {
        return HERMAS2_LOOP_OK;
    }
    hermas2_journal_record record = {
        .kind = kind,
        .outcome = outcome,
        .execution_id = slot->execution.execution_id,
        .workflow_id = loop->workflow_id,
        .image_fingerprint = loop->image_fingerprint
    };
    if (action) {
        record.request_id = slot->request_id;
        record.node_id = slot->node_id;
        record.app_id = slot->app_id;
        record.action_id = slot->action_id;
    }
    return hermas2_journal_writer_append(loop->journal, record) ==
                   HERMAS2_JOURNAL_OK
               ? HERMAS2_LOOP_OK
               : HERMAS2_LOOP_JOURNAL_ERROR;
}

static hermas2_loop_result append_finished_if_complete(
    hermas2_daemon_loop *loop,
    hermas2_loop_slot *slot) {
    if (slot->execution.state != HERMAS2_EXECUTION_COMPLETE ||
        slot->journal_finished) {
        return HERMAS2_LOOP_OK;
    }
    hermas2_loop_result result = append_record(
        loop, slot, HERMAS2_JOURNAL_EXECUTION_FINISHED,
        slot->execution.terminal_outcome, false);
    if (result == HERMAS2_LOOP_OK) {
        slot->journal_finished = true;
    }
    return result;
}

typedef struct saga_route {
    uint16_t compensation_app;
    uint16_t compensation_action;
    uint16_t source_type;
    uint16_t destination_type;
    uint16_t ordinal;
} saga_route;

static bool find_saga_route(
    const hermas2_daemon_loop *loop,
    uint16_t node,
    saga_route *route) {
    uint16_t regions = (uint16_t)loop->image[68] |
                       ((uint16_t)loop->image[69] << 8u);
    size_t base =
        (size_t)loop->image[72] |
        ((size_t)loop->image[73] << 8u) |
        ((size_t)loop->image[74] << 16u) |
        ((size_t)loop->image[75] << 24u);
    for (uint16_t index = 0u; index < regions; ++index) {
        size_t offset = base + (size_t)index * 16u;
        uint16_t forward =
            (uint16_t)loop->image[offset + 2u] |
            ((uint16_t)loop->image[offset + 3u] << 8u);
        if (loop->image[offset] == 3u && forward == node) {
            *route = (saga_route){
                .compensation_app =
                    (uint16_t)loop->image[offset + 4u] |
                    ((uint16_t)loop->image[offset + 5u] << 8u),
                .compensation_action =
                    (uint16_t)loop->image[offset + 6u] |
                    ((uint16_t)loop->image[offset + 7u] << 8u),
                .source_type =
                    (uint16_t)loop->image[offset + 8u] |
                    ((uint16_t)loop->image[offset + 9u] << 8u),
                .destination_type =
                    (uint16_t)loop->image[offset + 10u] |
                    ((uint16_t)loop->image[offset + 11u] << 8u),
                .ordinal =
                    (uint16_t)loop->image[offset + 12u] |
                    ((uint16_t)loop->image[offset + 13u] << 8u)
            };
            return true;
        }
    }
    return false;
}

static hermas2_saga_state saga_state(
    const hermas2_loop_slot *slot) {
    return slot->saga.execution.state;
}

static bool slot_ready(const hermas2_loop_slot *slot) {
    return slot->compensating
               ? saga_state(slot) == HERMAS2_SAGA_READY
               : slot->execution.state == HERMAS2_EXECUTION_READY;
}

static bool slot_prepared(const hermas2_loop_slot *slot) {
    return slot->compensating
               ? saga_state(slot) == HERMAS2_SAGA_PREPARED
               : slot->execution.state == HERMAS2_EXECUTION_PREPARED;
}

static bool slot_sent(const hermas2_loop_slot *slot) {
    return slot->compensating
               ? saga_state(slot) == HERMAS2_SAGA_SENT
               : slot->execution.state == HERMAS2_EXECUTION_SENT;
}

static void fail_compensation(
    hermas2_loop_slot *slot,
    uint16_t outcome) {
    slot->compensating = false;
    slot->execution.terminal_outcome =
        outcome == HERMAS2_OUTCOME_NOT_SENT
            ? HERMAS2_OUTCOME_NOT_SENT
            : HERMAS2_OUTCOME_UNKNOWN;
    slot->execution.value_length = 0u;
    slot->execution.value_source_type = 0u;
    slot->execution.value_destination_type = 0u;
}

static hermas2_loop_result begin_live_compensation(
    hermas2_daemon_loop *loop,
    hermas2_loop_slot *slot,
    uint16_t outcome) {
    if (slot->saga_success_count == 0u) {
        return HERMAS2_LOOP_OK;
    }
    if (loop->compensation_lookup == NULL ||
        loop->saga_log == NULL || slot->request_id == UINT64_MAX) {
        return HERMAS2_LOOP_COMPENSATION_ERROR;
    }
    hermas2_saga_execution execution;
    if (hermas2_saga_begin_live(
            &execution, loop->image, loop->image_size,
            slot->execution.execution_id, loop->workflow_id,
            outcome, slot->saga_forward_requests,
            slot->saga_success_count, slot->request_id + 1u,
            loop->compensation_lookup,
            loop->compensation_lookup_context) != HERMAS2_SAGA_OK ||
        hermas2_saga_driver_begin(
            &slot->saga, &execution, loop->saga_log, 0) !=
            HERMAS2_SAGA_OK) {
        return HERMAS2_LOOP_COMPENSATION_ERROR;
    }
    slot->compensating = true;
    return HERMAS2_LOOP_OK;
}

static hermas2_loop_result append_compensation_token(
    hermas2_daemon_loop *loop,
    hermas2_loop_slot *slot,
    const hermas2_frame *result) {
    saga_route route;
    if (!find_saga_route(loop, slot->node_id, &route)) {
        return HERMAS2_LOOP_OK;
    }
    if (loop->compensation == NULL ||
        result->source_type != route.source_type) {
        return HERMAS2_LOOP_COMPENSATION_ERROR;
    }
    hermas2_compensation_record record = {
        .key = {
            .execution_id = slot->execution.execution_id,
            .workflow_id = loop->workflow_id,
            .request_id = slot->request_id,
            .node_id = slot->node_id,
            .image_fingerprint = loop->image_fingerprint
        },
        .compensation_app_id = route.compensation_app,
        .compensation_action_id = route.compensation_action,
        .source_type = route.source_type,
        .destination_type = route.destination_type,
        .token = result->payload,
        .token_length = result->payload_length
    };
    hermas2_compensation_result appended =
        hermas2_compensation_writer_append(
               loop->compensation, record,
               loop->compensation_scratch,
               sizeof(loop->compensation_scratch));
    if (appended != HERMAS2_COMPENSATION_OK ||
        route.ordinal == 0u ||
        route.ordinal > HERMAS2_SAGA_MAX_STEPS) {
        return HERMAS2_LOOP_COMPENSATION_ERROR;
    }
    slot->saga_forward_requests[route.ordinal - 1u] =
        slot->request_id;
    if (route.ordinal > slot->saga_success_count) {
        slot->saga_success_count = (uint8_t)route.ordinal;
    }
    return HERMAS2_LOOP_OK;
}

static hermas2_loop_result prepare_ready(
    hermas2_daemon_loop *loop,
    size_t *progress_count) {
    for (size_t step = 0u; step < HERMAS2_DAEMON_MAX_EXECUTIONS; ++step) {
        size_t index =
            (loop->scheduler_cursor + step) % HERMAS2_DAEMON_MAX_EXECUTIONS;
        hermas2_loop_slot *slot = &loop->executions[index];
        if (!slot->active || !slot_ready(slot)) {
            continue;
        }
        hermas2_frame invocation;
        if (slot->compensating) {
            if (hermas2_saga_driver_prepare(
                    &slot->saga,
                    slot->packet + HERMAS2_PROTOCOL_HEADER_SIZE,
                    sizeof(slot->packet) -
                        HERMAS2_PROTOCOL_HEADER_SIZE,
                    &invocation) != HERMAS2_SAGA_OK) {
                return HERMAS2_LOOP_COMPENSATION_ERROR;
            }
        } else if (hermas2_execution_prepare(
                       &slot->execution, &invocation) !=
                   HERMAS2_RUNTIME_OK) {
            return HERMAS2_LOOP_RUNTIME_ERROR;
        }
        if (hermas2_protocol_encode(&invocation, slot->packet,
                                    sizeof(slot->packet),
                                    &slot->packet_size) !=
            HERMAS2_PROTOCOL_OK) {
            return HERMAS2_LOOP_PROTOCOL_ERROR;
        }
        slot->app_id = invocation.app_id;
        slot->action_id = invocation.action_id;
        slot->node_id = slot->compensating
                            ? slot->saga.execution.steps[
                                  slot->saga.execution.remaining - 1u]
                                  .forward_node
                            : slot->execution.current_node;
        slot->request_id = invocation.request_id;
        if (!slot->compensating) {
            hermas2_loop_result journaled = append_record(
                loop, slot, HERMAS2_JOURNAL_DELIVERY_PREPARED,
                HERMAS2_OUTCOME_NONE, true);
            if (journaled != HERMAS2_LOOP_OK) {
                return journaled;
            }
        }
        ++*progress_count;
    }
    loop->scheduler_cursor =
        (loop->scheduler_cursor + 1u) % HERMAS2_DAEMON_MAX_EXECUTIONS;
    return HERMAS2_LOOP_OK;
}

static void assign_available_apps(hermas2_daemon_loop *loop) {
    for (size_t step = 0u; step < HERMAS2_DAEMON_MAX_EXECUTIONS; ++step) {
        size_t index =
            (loop->scheduler_cursor + step) % HERMAS2_DAEMON_MAX_EXECUTIONS;
        hermas2_loop_slot *slot = &loop->executions[index];
        if (!slot->active || slot->owns_app ||
            !slot_prepared(slot) ||
            hermas2_daemon_registry_find(loop->registry, slot->app_id) < 0 ||
            app_owned_by_other(loop, slot)) {
            continue;
        }
        slot->owns_app = true;
    }
}

static hermas2_loop_result reconcile_missing_apps(
    hermas2_daemon_loop *loop,
    size_t *progress_count) {
    for (size_t index = 0u; index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas2_loop_slot *slot = &loop->executions[index];
        if (!slot->active || !slot->owns_app ||
            hermas2_daemon_registry_find(loop->registry, slot->app_id) >= 0) {
            continue;
        }
        slot->owns_app = false;
        if (slot_sent(slot)) {
            if (slot->compensating) {
                if (hermas2_saga_driver_mark_unknown(&slot->saga) !=
                    HERMAS2_SAGA_OK) {
                    return HERMAS2_LOOP_COMPENSATION_ERROR;
                }
                fail_compensation(slot, HERMAS2_OUTCOME_UNKNOWN);
                ++*progress_count;
                continue;
            }
            hermas2_loop_result journaled = append_record(
                loop, slot, HERMAS2_JOURNAL_ACTION_UNKNOWN,
                HERMAS2_OUTCOME_UNKNOWN, true);
            if (journaled != HERMAS2_LOOP_OK) {
                return journaled;
            }
            if (hermas2_execution_mark_unknown(&slot->execution) !=
                HERMAS2_RUNTIME_OK) {
                return HERMAS2_LOOP_RUNTIME_ERROR;
            }
            journaled = append_finished_if_complete(loop, slot);
            if (journaled != HERMAS2_LOOP_OK) {
                return journaled;
            }
            ++*progress_count;
        }
    }
    return HERMAS2_LOOP_OK;
}

static hermas2_loop_result send_invocation(
    hermas2_daemon_loop *loop,
    hermas2_loop_slot *slot,
    size_t *progress_count) {
    int descriptor =
        hermas2_daemon_registry_find(loop->registry, slot->app_id);
    if (descriptor < 0) {
        slot->owns_app = false;
        return HERMAS2_LOOP_OK;
    }
    ssize_t sent = send(descriptor, slot->packet, slot->packet_size,
                        MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent == (ssize_t)slot->packet_size) {
        if (slot->compensating) {
            if (hermas2_saga_driver_mark_sent(&slot->saga) !=
                HERMAS2_SAGA_OK) {
                return HERMAS2_LOOP_COMPENSATION_ERROR;
            }
            ++*progress_count;
            return HERMAS2_LOOP_OK;
        }
        hermas2_loop_result journaled = append_record(
            loop, slot, HERMAS2_JOURNAL_DELIVERY_SENT,
            HERMAS2_OUTCOME_NONE, true);
        if (journaled != HERMAS2_LOOP_OK) {
            return journaled;
        }
        if (hermas2_execution_mark_sent(&slot->execution) !=
            HERMAS2_RUNTIME_OK) {
            return HERMAS2_LOOP_RUNTIME_ERROR;
        }
        ++*progress_count;
        return HERMAS2_LOOP_OK;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                     errno == EINTR)) {
        return HERMAS2_LOOP_OK;
    }
    if (slot->compensating) {
        if (hermas2_saga_driver_mark_not_sent(&slot->saga) !=
            HERMAS2_SAGA_OK) {
            return HERMAS2_LOOP_COMPENSATION_ERROR;
        }
        slot->owns_app = false;
        drop_app(loop->registry, slot->app_id);
        fail_compensation(slot, HERMAS2_OUTCOME_NOT_SENT);
        ++*progress_count;
        return HERMAS2_LOOP_OK;
    }
    hermas2_loop_result journaled = append_record(
        loop, slot, HERMAS2_JOURNAL_ACTION_FAILED,
        HERMAS2_OUTCOME_NOT_SENT, true);
    if (journaled != HERMAS2_LOOP_OK) {
        return journaled;
    }
    if (hermas2_execution_mark_not_sent(&slot->execution) !=
        HERMAS2_RUNTIME_OK) {
        return HERMAS2_LOOP_RUNTIME_ERROR;
    }
    slot->owns_app = false;
    drop_app(loop->registry, slot->app_id);
    journaled = append_finished_if_complete(loop, slot);
    if (journaled != HERMAS2_LOOP_OK) {
        return journaled;
    }
    hermas2_loop_result compensation = begin_live_compensation(
        loop, slot, HERMAS2_OUTCOME_NOT_SENT);
    if (compensation != HERMAS2_LOOP_OK) {
        return compensation;
    }
    ++*progress_count;
    return HERMAS2_LOOP_OK;
}

static hermas2_loop_result receive_result(
    hermas2_daemon_loop *loop,
    hermas2_loop_slot *slot,
    size_t *progress_count) {
    int descriptor =
        hermas2_daemon_registry_find(loop->registry, slot->app_id);
    if (descriptor < 0) {
        if (slot->compensating) {
            if (hermas2_saga_driver_mark_unknown(&slot->saga) !=
                HERMAS2_SAGA_OK) {
                return HERMAS2_LOOP_COMPENSATION_ERROR;
            }
            slot->owns_app = false;
            fail_compensation(slot, HERMAS2_OUTCOME_UNKNOWN);
            ++*progress_count;
            return HERMAS2_LOOP_OK;
        }
        hermas2_loop_result journaled = append_record(
            loop, slot, HERMAS2_JOURNAL_ACTION_UNKNOWN,
            HERMAS2_OUTCOME_UNKNOWN, true);
        if (journaled != HERMAS2_LOOP_OK) {
            return journaled;
        }
        if (hermas2_execution_mark_unknown(&slot->execution) !=
            HERMAS2_RUNTIME_OK) {
            return HERMAS2_LOOP_RUNTIME_ERROR;
        }
        slot->owns_app = false;
        journaled = append_finished_if_complete(loop, slot);
        if (journaled != HERMAS2_LOOP_OK) {
            return journaled;
        }
        ++*progress_count;
        return HERMAS2_LOOP_OK;
    }
    struct iovec vector = {
        .iov_base = slot->packet,
        .iov_len = sizeof(slot->packet)
    };
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1u;
    ssize_t received = recvmsg(descriptor, &message, MSG_DONTWAIT);
    if (received < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return HERMAS2_LOOP_OK;
    }
    bool transport_failed =
        received <= 0 || (message.msg_flags & MSG_TRUNC) != 0;
    hermas2_frame result;
    bool decoded =
        !transport_failed &&
        hermas2_protocol_decode(slot->packet, (size_t)received, &result) ==
            HERMAS2_PROTOCOL_OK;
    if (slot->compensating) {
        bool accepted =
            decoded &&
            hermas2_saga_driver_accept_result(
                &slot->saga, &result) == HERMAS2_SAGA_OK;
        if (!accepted) {
            if (hermas2_saga_driver_mark_unknown(&slot->saga) !=
                HERMAS2_SAGA_OK) {
                return HERMAS2_LOOP_COMPENSATION_ERROR;
            }
            drop_app(loop->registry, slot->app_id);
            fail_compensation(slot, HERMAS2_OUTCOME_UNKNOWN);
        } else if (slot->saga.execution.state ==
                   HERMAS2_SAGA_COMPLETE) {
            slot->compensating = false;
        } else if (slot->saga.execution.state ==
                   HERMAS2_SAGA_BLOCKED) {
            fail_compensation(
                slot, slot->saga.execution.compensation_outcome);
        }
        slot->owns_app = false;
        ++*progress_count;
        return HERMAS2_LOOP_OK;
    }
    bool result_valid =
        decoded &&
        hermas2_execution_accept_result(&slot->execution, &result) ==
            HERMAS2_RUNTIME_OK;
    if (result_valid) {
        if (result.outcome == HERMAS2_OUTCOME_SUCCESS) {
            hermas2_loop_result tokenized =
                append_compensation_token(loop, slot, &result);
            if (tokenized != HERMAS2_LOOP_OK) {
                return tokenized;
            }
        }
        hermas2_journal_kind kind =
            result.outcome == HERMAS2_OUTCOME_SUCCESS
                ? HERMAS2_JOURNAL_ACTION_SUCCEEDED
                : HERMAS2_JOURNAL_ACTION_FAILED;
        hermas2_loop_result journaled = append_record(
            loop, slot, kind, result.outcome, true);
        if (journaled != HERMAS2_LOOP_OK) {
            return journaled;
        }
    }
    if (!result_valid) {
        hermas2_loop_result journaled = append_record(
            loop, slot, HERMAS2_JOURNAL_ACTION_UNKNOWN,
            HERMAS2_OUTCOME_UNKNOWN, true);
        if (journaled != HERMAS2_LOOP_OK) {
            return journaled;
        }
        if (hermas2_execution_mark_unknown(&slot->execution) !=
            HERMAS2_RUNTIME_OK) {
            return HERMAS2_LOOP_RUNTIME_ERROR;
        }
        drop_app(loop->registry, slot->app_id);
    }
    slot->owns_app = false;
    hermas2_loop_result finished =
        append_finished_if_complete(loop, slot);
    if (finished != HERMAS2_LOOP_OK) {
        return finished;
    }
    if (result_valid &&
        result.outcome == HERMAS2_OUTCOME_APP_ERROR) {
        hermas2_loop_result compensation =
            begin_live_compensation(
                loop, slot, HERMAS2_OUTCOME_APP_ERROR);
        if (compensation != HERMAS2_LOOP_OK) {
            return compensation;
        }
    }
    ++*progress_count;
    return HERMAS2_LOOP_OK;
}

hermas2_loop_result hermas2_daemon_loop_init(
    hermas2_daemon_loop *loop,
    hermas2_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size) {
    if (loop == NULL || registry == NULL || image == NULL) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    if (hermas2_image_validate(image, image_size, NULL) != HERMAS2_IMAGE_OK) {
        return HERMAS2_LOOP_INVALID_IMAGE;
    }
    memset(loop, 0, sizeof(*loop));
    loop->image = image;
    loop->image_size = image_size;
    loop->registry = registry;
    loop->image_fingerprint =
        hermas2_journal_image_fingerprint(image, image_size);
    return HERMAS2_LOOP_OK;
}

hermas2_loop_result hermas2_daemon_loop_attach_journal(
    hermas2_daemon_loop *loop,
    hermas2_journal_writer *journal,
    uint32_t workflow_id) {
    if (loop == NULL || journal == NULL || workflow_id == 0u ||
        loop->image == NULL || hermas2_daemon_loop_active(loop) != 0u) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    loop->journal = journal;
    loop->workflow_id = workflow_id;
    return HERMAS2_LOOP_OK;
}

hermas2_loop_result hermas2_daemon_loop_attach_compensation(
    hermas2_daemon_loop *loop,
    hermas2_compensation_writer *compensation) {
    if (loop == NULL || compensation == NULL ||
        compensation->write == NULL || loop->image == NULL ||
        hermas2_daemon_loop_active(loop) != 0u) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    loop->compensation = compensation;
    return HERMAS2_LOOP_OK;
}

hermas2_loop_result hermas2_daemon_loop_attach_saga(
    hermas2_daemon_loop *loop,
    hermas2_compensation_writer *compensation,
    hermas2_compensation_lookup compensation_lookup,
    void *compensation_lookup_context,
    hermas2_saga_log_writer *saga_log) {
    if (loop == NULL || compensation == NULL ||
        compensation->write == NULL || compensation_lookup == NULL ||
        saga_log == NULL || saga_log->write == NULL ||
        loop->image == NULL ||
        hermas2_daemon_loop_active(loop) != 0u) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    loop->compensation = compensation;
    loop->compensation_lookup = compensation_lookup;
    loop->compensation_lookup_context =
        compensation_lookup_context;
    loop->saga_log = saga_log;
    return HERMAS2_LOOP_OK;
}

hermas2_loop_result hermas2_daemon_loop_resume_saga(
    hermas2_daemon_loop *loop,
    const hermas2_saga_execution *execution) {
    if (loop == NULL || execution == NULL ||
        loop->compensation_lookup == NULL ||
        loop->saga_log == NULL ||
        execution->state != HERMAS2_SAGA_READY ||
        execution->remaining == 0u ||
        execution->execution_id == 0u ||
        execution->workflow_id != loop->workflow_id ||
        execution->image_fingerprint != loop->image_fingerprint ||
        find_execution(loop, execution->execution_id) != NULL) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    for (size_t index = 0u;
         index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas2_loop_slot *slot = &loop->executions[index];
        if (slot->active) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        hermas2_saga_execution resumed = *execution;
        resumed.image = loop->image;
        resumed.image_size = loop->image_size;
        resumed.tokens = NULL;
        resumed.token_bytes = 0u;
        resumed.token_lookup = loop->compensation_lookup;
        resumed.token_lookup_context =
            loop->compensation_lookup_context;
        if (hermas2_saga_driver_begin(
                &slot->saga, &resumed, loop->saga_log, 1) !=
            HERMAS2_SAGA_OK) {
            memset(slot, 0, sizeof(*slot));
            return HERMAS2_LOOP_COMPENSATION_ERROR;
        }
        slot->execution = (hermas2_execution){
            .image = loop->image,
            .image_size = loop->image_size,
            .value_buffer = slot->value,
            .value_capacity = sizeof(slot->value),
            .execution_id = execution->execution_id,
            .terminal_outcome = HERMAS2_OUTCOME_UNKNOWN,
            .state = HERMAS2_EXECUTION_COMPLETE
        };
        slot->active = true;
        slot->journal_finished = true;
        slot->compensating = true;
        return HERMAS2_LOOP_OK;
    }
    return HERMAS2_LOOP_CAPACITY_EXHAUSTED;
}

hermas2_loop_result hermas2_daemon_loop_admit(
    hermas2_daemon_loop *loop,
    uint64_t execution_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length) {
    if (loop == NULL || execution_id == 0u) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    if (find_execution(loop, execution_id) != NULL) {
        return HERMAS2_LOOP_DUPLICATE_EXECUTION;
    }
    for (size_t index = 0u; index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas2_loop_slot *slot = &loop->executions[index];
        if (slot->active) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        hermas2_runtime_result started = hermas2_execution_start(
            &slot->execution, loop->image, loop->image_size, execution_id,
            slot->value, sizeof(slot->value), input_type, input, input_length);
        if (started != HERMAS2_RUNTIME_OK) {
            return HERMAS2_LOOP_RUNTIME_ERROR;
        }
        hermas2_loop_result journaled = append_record(
            loop, slot, HERMAS2_JOURNAL_EXECUTION_STARTED,
            HERMAS2_OUTCOME_NONE, false);
        if (journaled != HERMAS2_LOOP_OK) {
            memset(slot, 0, sizeof(*slot));
            return journaled;
        }
        slot->active = true;
        return HERMAS2_LOOP_OK;
    }
    return HERMAS2_LOOP_CAPACITY_EXHAUSTED;
}

hermas2_loop_result hermas2_daemon_loop_poll(
    hermas2_daemon_loop *loop,
    int timeout_milliseconds,
    size_t *progress_count) {
    if (loop == NULL || progress_count == NULL || timeout_milliseconds < -1) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    *progress_count = 0u;
    hermas2_loop_result prepared = prepare_ready(loop, progress_count);
    if (prepared != HERMAS2_LOOP_OK) {
        return prepared;
    }
    hermas2_loop_result reconciled =
        reconcile_missing_apps(loop, progress_count);
    if (reconciled != HERMAS2_LOOP_OK) {
        return reconciled;
    }
    assign_available_apps(loop);
    struct pollfd poll_items[HERMAS2_DAEMON_MAX_EXECUTIONS];
    hermas2_loop_slot *owners[HERMAS2_DAEMON_MAX_EXECUTIONS];
    size_t poll_count = 0u;
    for (size_t index = 0u; index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas2_loop_slot *slot = &loop->executions[index];
        if (!slot->active || !slot->owns_app) {
            continue;
        }
        int descriptor =
            hermas2_daemon_registry_find(loop->registry, slot->app_id);
        if (descriptor < 0) {
            continue;
        }
        poll_items[poll_count] = (struct pollfd){
            .fd = descriptor,
            .events = slot_prepared(slot) ? POLLOUT : POLLIN
        };
        owners[poll_count] = slot;
        ++poll_count;
    }
    int poll_result;
    do {
        poll_result = poll(poll_items, poll_count, timeout_milliseconds);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result < 0) {
        return HERMAS2_LOOP_POLL_ERROR;
    }
    for (size_t index = 0u; index < poll_count; ++index) {
        hermas2_loop_slot *slot = owners[index];
        short events = poll_items[index].revents;
        hermas2_loop_result result = HERMAS2_LOOP_OK;
        if ((events & POLLOUT) != 0 && slot_prepared(slot)) {
            result = send_invocation(loop, slot, progress_count);
        } else if ((events & POLLIN) != 0 && slot_sent(slot)) {
            result = receive_result(loop, slot, progress_count);
        } else if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            result = slot_prepared(slot)
                         ? send_invocation(loop, slot, progress_count)
                         : receive_result(loop, slot, progress_count);
        }
        if (result != HERMAS2_LOOP_OK) {
            return result;
        }
    }
    return HERMAS2_LOOP_OK;
}

hermas2_loop_result hermas2_daemon_loop_result(
    const hermas2_daemon_loop *loop,
    uint64_t execution_id,
    hermas2_frame *result) {
    if (loop == NULL || result == NULL || execution_id == 0u) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    const hermas2_loop_slot *slot =
        find_const_execution(loop, execution_id);
    if (slot == NULL) {
        return HERMAS2_LOOP_UNKNOWN_EXECUTION;
    }
    if (slot->compensating) {
        return HERMAS2_LOOP_EXECUTION_ACTIVE;
    }
    return hermas2_execution_get_result(&slot->execution, result) ==
                   HERMAS2_RUNTIME_OK
               ? HERMAS2_LOOP_OK
               : HERMAS2_LOOP_EXECUTION_ACTIVE;
}

hermas2_loop_result hermas2_daemon_loop_release(
    hermas2_daemon_loop *loop,
    uint64_t execution_id) {
    if (loop == NULL || execution_id == 0u) {
        return HERMAS2_LOOP_INVALID_ARGUMENT;
    }
    hermas2_loop_slot *slot = find_execution(loop, execution_id);
    if (slot == NULL) {
        return HERMAS2_LOOP_UNKNOWN_EXECUTION;
    }
    if (slot->execution.state != HERMAS2_EXECUTION_COMPLETE ||
        slot->compensating) {
        return HERMAS2_LOOP_EXECUTION_ACTIVE;
    }
    memset(slot, 0, sizeof(*slot));
    return HERMAS2_LOOP_OK;
}

size_t hermas2_daemon_loop_active(const hermas2_daemon_loop *loop) {
    if (loop == NULL) {
        return 0u;
    }
    size_t count = 0u;
    for (size_t index = 0u; index < HERMAS2_DAEMON_MAX_EXECUTIONS; ++index) {
        if (loop->executions[index].active) {
            ++count;
        }
    }
    return count;
}

const char *hermas2_loop_result_name(hermas2_loop_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "invalid-image", "capacity-exhausted",
        "duplicate-execution", "unknown-execution", "execution-active",
        "poll-error", "runtime-error", "protocol-error", "journal-error",
        "compensation-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
