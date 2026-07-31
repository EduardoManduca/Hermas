#include "hermas/daemon.h"

#include "hermas/image.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static uint16_t image_u16(
    const uint8_t *image,
    size_t offset) {
    return (uint16_t)image[offset] |
           ((uint16_t)image[offset + 1u] << 8u);
}

static size_t image_u32(
    const uint8_t *image,
    size_t offset) {
    return (size_t)image[offset] |
           ((size_t)image[offset + 1u] << 8u) |
           ((size_t)image[offset + 2u] << 16u) |
           ((size_t)image[offset + 3u] << 24u);
}

static bool image_requires_group_runtime(
    const uint8_t *image) {
    uint16_t node_count = image_u16(
        image, HERMAS_IMAGE_HEADER_NODE_COUNT_OFFSET);
    size_t nodes = image_u32(
        image, HERMAS_IMAGE_HEADER_NODES_OFFSET);
    for (uint16_t index = 0u; index < node_count; ++index) {
        uint8_t kind =
            image[nodes + (size_t)index *
                              HERMAS_IMAGE_NODE_RECORD_SIZE];
        if (kind >= 4u && kind <= 7u) {
            return true;
        }
    }
    uint16_t region_count = image_u16(
        image, HERMAS_IMAGE_HEADER_REGION_COUNT_OFFSET);
    size_t regions = image_u32(
        image, HERMAS_IMAGE_HEADER_REGIONS_OFFSET);
    for (uint16_t index = 0u; index < region_count; ++index) {
        uint8_t kind =
            image[regions + (size_t)index *
                                HERMAS_IMAGE_REGION_RECORD_SIZE];
        if (kind == 2u) {
            return true;
        }
    }
    return false;
}

static hermas_loop_slot *find_execution(
    hermas_daemon_loop *loop,
    uint64_t execution_id) {
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas_loop_slot *slot = &loop->executions[index];
        if (slot->active && slot->execution.execution_id == execution_id) {
            return slot;
        }
    }
    return NULL;
}

static const hermas_loop_slot *find_const_execution(
    const hermas_daemon_loop *loop,
    uint64_t execution_id) {
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        const hermas_loop_slot *slot = &loop->executions[index];
        if (slot->active && slot->execution.execution_id == execution_id) {
            return slot;
        }
    }
    return NULL;
}

static void drop_action(
    hermas_daemon_registry *registry,
    uint16_t app_id,
    uint16_t action_id) {
    for (size_t index = 0u; index < registry->action_count; ++index) {
        hermas_daemon_action *app = &registry->actions[index];
        if (app->app_id == app_id && app->action_id == action_id) {
            if (app->file_descriptor >= 0) {
                close(app->file_descriptor);
                app->file_descriptor = -1;
            }
            return;
        }
    }
}

static bool action_owned_by_other(
    const hermas_daemon_loop *loop,
    const hermas_loop_slot *candidate) {
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        const hermas_loop_slot *slot = &loop->executions[index];
        if (slot != candidate && slot->active && slot->owns_action &&
            slot->app_id == candidate->app_id &&
            slot->action_id == candidate->action_id) {
            return true;
        }
    }
    return false;
}

static hermas_loop_result append_record(
    hermas_daemon_loop *loop,
    const hermas_loop_slot *slot,
    hermas_journal_kind kind,
    uint16_t outcome,
    bool action) {
    if (loop->journal == NULL) {
        return HERMAS_LOOP_OK;
    }
    hermas_journal_record record = {
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
    return hermas_journal_writer_append(loop->journal, record) ==
                   HERMAS_JOURNAL_OK
               ? HERMAS_LOOP_OK
               : HERMAS_LOOP_JOURNAL_ERROR;
}

static hermas_loop_result append_finished_if_complete(
    hermas_daemon_loop *loop,
    hermas_loop_slot *slot) {
    if (slot->execution.state != HERMAS_EXECUTION_COMPLETE ||
        slot->journal_finished) {
        return HERMAS_LOOP_OK;
    }
    hermas_loop_result result = append_record(
        loop, slot, HERMAS_JOURNAL_EXECUTION_FINISHED,
        slot->execution.terminal_outcome, false);
    if (result == HERMAS_LOOP_OK) {
        slot->journal_finished = true;
    }
    return result;
}

static hermas_loop_result append_result_if_complete(
    hermas_daemon_loop *loop,
    hermas_loop_slot *slot) {
    if (slot->execution.state != HERMAS_EXECUTION_COMPLETE ||
        slot->result_stored ||
        (slot->execution.terminal_outcome !=
             HERMAS_OUTCOME_SUCCESS &&
         slot->execution.terminal_outcome !=
             HERMAS_OUTCOME_APP_ERROR)) {
        return HERMAS_LOOP_OK;
    }
    if (loop->results == NULL) {
        return HERMAS_LOOP_OK;
    }
    hermas_result_record record = {
        .key = {
            .execution_id = slot->execution.execution_id,
            .workflow_id = loop->workflow_id,
            .image_fingerprint = loop->image_fingerprint
        },
        .outcome = slot->execution.terminal_outcome,
        .source_type = slot->execution.value_source_type,
        .destination_type =
            slot->execution.value_destination_type,
        .value = slot->execution.value_buffer,
        .value_length = (uint32_t)slot->execution.value_length
    };
    if (hermas_result_writer_append(
            loop->results, record, loop->compensation_scratch,
            sizeof(loop->compensation_scratch)) !=
        HERMAS_RESULT_STORE_OK) {
        return HERMAS_LOOP_RESULT_ERROR;
    }
    slot->result_stored = true;
    return HERMAS_LOOP_OK;
}

typedef struct saga_route {
    uint16_t compensation_app;
    uint16_t compensation_action;
    uint16_t source_type;
    uint16_t destination_type;
    uint16_t ordinal;
} saga_route;

static bool find_saga_route(
    const hermas_daemon_loop *loop,
    uint16_t node,
    saga_route *route) {
    uint16_t regions = image_u16(
        loop->image, HERMAS_IMAGE_HEADER_REGION_COUNT_OFFSET);
    size_t base = image_u32(
        loop->image, HERMAS_IMAGE_HEADER_REGIONS_OFFSET);
    for (uint16_t index = 0u; index < regions; ++index) {
        size_t offset =
            base + (size_t)index *
                       HERMAS_IMAGE_REGION_RECORD_SIZE;
        uint16_t forward =
            image_u16(loop->image, offset + 2u);
        if (loop->image[offset] == 3u && forward == node) {
            *route = (saga_route){
                .compensation_app =
                    image_u16(loop->image, offset + 4u),
                .compensation_action =
                    image_u16(loop->image, offset + 6u),
                .source_type =
                    image_u16(loop->image, offset + 8u),
                .destination_type =
                    image_u16(loop->image, offset + 10u),
                .ordinal =
                    image_u16(loop->image, offset + 12u)
            };
            return true;
        }
    }
    return false;
}

static bool recovered_failure_matches(
    const hermas_daemon_loop *loop,
    const hermas_saga_execution *execution,
    const hermas_result_record *result) {
    if (execution->completed_steps >= execution->step_count) {
        return false;
    }
    uint16_t node =
        execution->steps[execution->completed_steps].forward_node;
    uint16_t edge_count = image_u16(
        loop->image, HERMAS_IMAGE_HEADER_EDGE_COUNT_OFFSET);
    size_t edges = image_u32(
        loop->image, HERMAS_IMAGE_HEADER_EDGES_OFFSET);
    for (uint16_t index = 0u; index < edge_count; ++index) {
        size_t offset =
            edges + (size_t)index *
                        HERMAS_IMAGE_EDGE_RECORD_SIZE;
        uint16_t source_node =
            image_u16(loop->image, offset + 4u);
        if (loop->image[offset] == 2u && source_node == node) {
            uint16_t source_type =
                image_u16(loop->image, offset + 8u);
            uint16_t destination_type =
                image_u16(loop->image, offset + 10u);
            return result->source_type == source_type &&
                   result->destination_type == destination_type;
        }
    }
    return false;
}

static hermas_saga_state saga_state(
    const hermas_loop_slot *slot) {
    return slot->saga.execution.state;
}

static bool slot_ready(const hermas_loop_slot *slot) {
    return slot->compensating
               ? saga_state(slot) == HERMAS_SAGA_READY
               : slot->execution.state == HERMAS_EXECUTION_READY;
}

static bool slot_prepared(const hermas_loop_slot *slot) {
    return slot->compensating
               ? saga_state(slot) == HERMAS_SAGA_PREPARED
               : slot->execution.state == HERMAS_EXECUTION_PREPARED;
}

static bool slot_sent(const hermas_loop_slot *slot) {
    return slot->compensating
               ? saga_state(slot) == HERMAS_SAGA_SENT
               : slot->execution.state == HERMAS_EXECUTION_SENT;
}

static void fail_compensation(
    hermas_loop_slot *slot,
    uint16_t outcome) {
    slot->compensating = false;
    slot->execution.terminal_outcome =
        outcome == HERMAS_OUTCOME_NOT_SENT
            ? HERMAS_OUTCOME_NOT_SENT
            : HERMAS_OUTCOME_UNKNOWN;
    slot->execution.value_length = 0u;
    slot->execution.value_source_type = 0u;
    slot->execution.value_destination_type = 0u;
}

static hermas_loop_result begin_live_compensation(
    hermas_daemon_loop *loop,
    hermas_loop_slot *slot,
    uint16_t outcome) {
    if (slot->saga_success_count == 0u) {
        return HERMAS_LOOP_OK;
    }
    if (loop->compensation_lookup == NULL ||
        loop->saga_log == NULL || slot->request_id == UINT64_MAX) {
        return HERMAS_LOOP_COMPENSATION_ERROR;
    }
    hermas_saga_execution execution;
    if (hermas_saga_begin_live(
            &execution, loop->image, loop->image_size,
            slot->execution.execution_id, loop->workflow_id,
            outcome, slot->saga_forward_requests,
            slot->saga_success_count, slot->request_id + 1u,
            loop->compensation_lookup,
            loop->compensation_lookup_context) != HERMAS_SAGA_OK ||
        hermas_saga_driver_begin(
            &slot->saga, &execution, loop->saga_log, 0) !=
            HERMAS_SAGA_OK) {
        return HERMAS_LOOP_COMPENSATION_ERROR;
    }
    slot->compensating = true;
    return HERMAS_LOOP_OK;
}

static hermas_loop_result append_compensation_token(
    hermas_daemon_loop *loop,
    hermas_loop_slot *slot,
    const hermas_frame *result) {
    saga_route route;
    if (!find_saga_route(loop, slot->node_id, &route)) {
        return HERMAS_LOOP_OK;
    }
    if (loop->compensation == NULL ||
        result->source_type != route.source_type) {
        return HERMAS_LOOP_COMPENSATION_ERROR;
    }
    hermas_compensation_record record = {
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
    hermas_compensation_result appended =
        hermas_compensation_writer_append(
               loop->compensation, record,
               loop->compensation_scratch,
               sizeof(loop->compensation_scratch));
    if (appended != HERMAS_COMPENSATION_OK ||
        route.ordinal == 0u ||
        route.ordinal > HERMAS_SAGA_MAX_STEPS) {
        return HERMAS_LOOP_COMPENSATION_ERROR;
    }
    slot->saga_forward_requests[route.ordinal - 1u] =
        slot->request_id;
    if (route.ordinal > slot->saga_success_count) {
        slot->saga_success_count = (uint8_t)route.ordinal;
    }
    return HERMAS_LOOP_OK;
}

static hermas_loop_result prepare_ready(
    hermas_daemon_loop *loop,
    size_t *progress_count) {
    for (size_t step = 0u; step < HERMAS_DAEMON_MAX_EXECUTIONS; ++step) {
        size_t index =
            (loop->scheduler_cursor + step) % HERMAS_DAEMON_MAX_EXECUTIONS;
        hermas_loop_slot *slot = &loop->executions[index];
        if (!slot->active || !slot_ready(slot)) {
            continue;
        }
        hermas_frame invocation;
        if (slot->compensating) {
            if (hermas_saga_driver_prepare(
                    &slot->saga,
                    slot->packet + HERMAS_PROTOCOL_HEADER_SIZE,
                    sizeof(slot->packet) -
                        HERMAS_PROTOCOL_HEADER_SIZE,
                    &invocation) != HERMAS_SAGA_OK) {
                return HERMAS_LOOP_COMPENSATION_ERROR;
            }
        } else if (hermas_execution_prepare(
                       &slot->execution, &invocation) !=
                   HERMAS_RUNTIME_OK) {
            return HERMAS_LOOP_RUNTIME_ERROR;
        }
        if (hermas_protocol_encode(&invocation, slot->packet,
                                    sizeof(slot->packet),
                                    &slot->packet_size) !=
            HERMAS_PROTOCOL_OK) {
            return HERMAS_LOOP_PROTOCOL_ERROR;
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
            hermas_loop_result journaled = append_record(
                loop, slot, HERMAS_JOURNAL_DELIVERY_PREPARED,
                HERMAS_OUTCOME_NONE, true);
            if (journaled != HERMAS_LOOP_OK) {
                return journaled;
            }
        }
        ++*progress_count;
    }
    loop->scheduler_cursor =
        (loop->scheduler_cursor + 1u) % HERMAS_DAEMON_MAX_EXECUTIONS;
    return HERMAS_LOOP_OK;
}

static void assign_available_actions(hermas_daemon_loop *loop) {
    for (size_t step = 0u; step < HERMAS_DAEMON_MAX_EXECUTIONS; ++step) {
        size_t index =
            (loop->scheduler_cursor + step) % HERMAS_DAEMON_MAX_EXECUTIONS;
        hermas_loop_slot *slot = &loop->executions[index];
        if (!slot->active || slot->owns_action ||
            !slot_prepared(slot) ||
            hermas_daemon_registry_find_action(
                loop->registry, slot->app_id, slot->action_id, NULL) < 0 ||
            action_owned_by_other(loop, slot)) {
            continue;
        }
        slot->owns_action = true;
    }
}

static hermas_loop_result reconcile_missing_actions(
    hermas_daemon_loop *loop,
    size_t *progress_count) {
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas_loop_slot *slot = &loop->executions[index];
        if (!slot->active || !slot->owns_action ||
            hermas_daemon_registry_find_action(
                loop->registry, slot->app_id, slot->action_id, NULL) >= 0) {
            continue;
        }
        slot->owns_action = false;
        if (slot_sent(slot)) {
            if (slot->compensating) {
                if (hermas_saga_driver_mark_unknown(&slot->saga) !=
                    HERMAS_SAGA_OK) {
                    return HERMAS_LOOP_COMPENSATION_ERROR;
                }
                fail_compensation(slot, HERMAS_OUTCOME_UNKNOWN);
                ++*progress_count;
                continue;
            }
            hermas_loop_result journaled = append_record(
                loop, slot, HERMAS_JOURNAL_ACTION_UNKNOWN,
                HERMAS_OUTCOME_UNKNOWN, true);
            if (journaled != HERMAS_LOOP_OK) {
                return journaled;
            }
            if (hermas_execution_mark_unknown(&slot->execution) !=
                HERMAS_RUNTIME_OK) {
                return HERMAS_LOOP_RUNTIME_ERROR;
            }
            journaled = append_finished_if_complete(loop, slot);
            if (journaled != HERMAS_LOOP_OK) {
                return journaled;
            }
            ++*progress_count;
        }
    }
    return HERMAS_LOOP_OK;
}

static hermas_loop_result send_invocation(
    hermas_daemon_loop *loop,
    hermas_loop_slot *slot,
    size_t *progress_count) {
    uint16_t registered_action_id = 0u;
    int descriptor = hermas_daemon_registry_find_action(
        loop->registry, slot->app_id, slot->action_id,
        &registered_action_id);
    if (descriptor < 0) {
        slot->owns_action = false;
        return HERMAS_LOOP_OK;
    }
    slot->packet[34] = (uint8_t)(registered_action_id & 0xffu);
    slot->packet[35] = (uint8_t)(registered_action_id >> 8u);
    ssize_t sent = send(descriptor, slot->packet, slot->packet_size,
                        MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent == (ssize_t)slot->packet_size) {
        if (slot->compensating) {
            if (hermas_saga_driver_mark_sent(&slot->saga) !=
                HERMAS_SAGA_OK) {
                return HERMAS_LOOP_COMPENSATION_ERROR;
            }
            ++*progress_count;
            return HERMAS_LOOP_OK;
        }
        hermas_loop_result journaled = append_record(
            loop, slot, HERMAS_JOURNAL_DELIVERY_SENT,
            HERMAS_OUTCOME_NONE, true);
        if (journaled != HERMAS_LOOP_OK) {
            return journaled;
        }
        if (hermas_execution_mark_sent(&slot->execution) !=
            HERMAS_RUNTIME_OK) {
            return HERMAS_LOOP_RUNTIME_ERROR;
        }
        ++*progress_count;
        return HERMAS_LOOP_OK;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                     errno == EINTR)) {
        return HERMAS_LOOP_OK;
    }
    if (slot->compensating) {
        if (hermas_saga_driver_mark_not_sent(&slot->saga) !=
            HERMAS_SAGA_OK) {
            return HERMAS_LOOP_COMPENSATION_ERROR;
        }
        slot->owns_action = false;
        drop_action(loop->registry, slot->app_id, slot->action_id);
        fail_compensation(slot, HERMAS_OUTCOME_NOT_SENT);
        ++*progress_count;
        return HERMAS_LOOP_OK;
    }
    hermas_loop_result journaled = append_record(
        loop, slot, HERMAS_JOURNAL_ACTION_FAILED,
        HERMAS_OUTCOME_NOT_SENT, true);
    if (journaled != HERMAS_LOOP_OK) {
        return journaled;
    }
    if (hermas_execution_mark_not_sent(&slot->execution) !=
        HERMAS_RUNTIME_OK) {
        return HERMAS_LOOP_RUNTIME_ERROR;
    }
    slot->owns_action = false;
    drop_action(loop->registry, slot->app_id, slot->action_id);
    journaled = append_finished_if_complete(loop, slot);
    if (journaled != HERMAS_LOOP_OK) {
        return journaled;
    }
    hermas_loop_result compensation = begin_live_compensation(
        loop, slot, HERMAS_OUTCOME_NOT_SENT);
    if (compensation != HERMAS_LOOP_OK) {
        return compensation;
    }
    ++*progress_count;
    return HERMAS_LOOP_OK;
}

static hermas_loop_result receive_result(
    hermas_daemon_loop *loop,
    hermas_loop_slot *slot,
    size_t *progress_count) {
    uint16_t registered_action_id = 0u;
    int descriptor = hermas_daemon_registry_find_action(
        loop->registry, slot->app_id, slot->action_id,
        &registered_action_id);
    if (descriptor < 0) {
        if (slot->compensating) {
            if (hermas_saga_driver_mark_unknown(&slot->saga) !=
                HERMAS_SAGA_OK) {
                return HERMAS_LOOP_COMPENSATION_ERROR;
            }
            slot->owns_action = false;
            fail_compensation(slot, HERMAS_OUTCOME_UNKNOWN);
            ++*progress_count;
            return HERMAS_LOOP_OK;
        }
        hermas_loop_result journaled = append_record(
            loop, slot, HERMAS_JOURNAL_ACTION_UNKNOWN,
            HERMAS_OUTCOME_UNKNOWN, true);
        if (journaled != HERMAS_LOOP_OK) {
            return journaled;
        }
        if (hermas_execution_mark_unknown(&slot->execution) !=
            HERMAS_RUNTIME_OK) {
            return HERMAS_LOOP_RUNTIME_ERROR;
        }
        slot->owns_action = false;
        journaled = append_finished_if_complete(loop, slot);
        if (journaled != HERMAS_LOOP_OK) {
            return journaled;
        }
        ++*progress_count;
        return HERMAS_LOOP_OK;
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
        return HERMAS_LOOP_OK;
    }
    bool transport_failed =
        received <= 0 || (message.msg_flags & MSG_TRUNC) != 0;
    hermas_frame result;
    bool decoded =
        !transport_failed &&
        hermas_protocol_decode(slot->packet, (size_t)received, &result) ==
            HERMAS_PROTOCOL_OK;
    if (decoded) {
        if (result.action_id != registered_action_id) {
            decoded = false;
        } else {
            result.action_id = slot->action_id;
        }
    }
    if (slot->compensating) {
        bool accepted =
            decoded &&
            hermas_saga_driver_accept_result(
                &slot->saga, &result) == HERMAS_SAGA_OK;
        if (!accepted) {
            if (hermas_saga_driver_mark_unknown(&slot->saga) !=
                HERMAS_SAGA_OK) {
                return HERMAS_LOOP_COMPENSATION_ERROR;
            }
            drop_action(loop->registry, slot->app_id, slot->action_id);
            fail_compensation(slot, HERMAS_OUTCOME_UNKNOWN);
        } else if (slot->saga.execution.state ==
                   HERMAS_SAGA_COMPLETE) {
            slot->compensating = false;
        } else if (slot->saga.execution.state ==
                   HERMAS_SAGA_BLOCKED) {
            fail_compensation(
                slot, slot->saga.execution.compensation_outcome);
        }
        slot->owns_action = false;
        ++*progress_count;
        return HERMAS_LOOP_OK;
    }
    bool result_valid =
        decoded &&
        hermas_execution_accept_result(&slot->execution, &result) ==
            HERMAS_RUNTIME_OK;
    if (result_valid) {
        if (result.outcome == HERMAS_OUTCOME_SUCCESS) {
            hermas_loop_result tokenized =
                append_compensation_token(loop, slot, &result);
            if (tokenized != HERMAS_LOOP_OK) {
                return tokenized;
            }
        }
        hermas_journal_kind kind =
            result.outcome == HERMAS_OUTCOME_SUCCESS
                ? HERMAS_JOURNAL_ACTION_SUCCEEDED
                : HERMAS_JOURNAL_ACTION_FAILED;
        hermas_loop_result journaled = append_record(
            loop, slot, kind, result.outcome, true);
        if (journaled != HERMAS_LOOP_OK) {
            return journaled;
        }
    }
    if (!result_valid) {
        hermas_loop_result journaled = append_record(
            loop, slot, HERMAS_JOURNAL_ACTION_UNKNOWN,
            HERMAS_OUTCOME_UNKNOWN, true);
        if (journaled != HERMAS_LOOP_OK) {
            return journaled;
        }
        if (hermas_execution_mark_unknown(&slot->execution) !=
            HERMAS_RUNTIME_OK) {
            return HERMAS_LOOP_RUNTIME_ERROR;
        }
        drop_action(loop->registry, slot->app_id, slot->action_id);
    }
    slot->owns_action = false;
    hermas_loop_result stored =
        append_result_if_complete(loop, slot);
    if (stored != HERMAS_LOOP_OK) {
        return stored;
    }
    hermas_loop_result finished =
        append_finished_if_complete(loop, slot);
    if (finished != HERMAS_LOOP_OK) {
        return finished;
    }
    if (result_valid &&
        result.outcome == HERMAS_OUTCOME_APP_ERROR) {
        hermas_loop_result compensation =
            begin_live_compensation(
                loop, slot, HERMAS_OUTCOME_APP_ERROR);
        if (compensation != HERMAS_LOOP_OK) {
            return compensation;
        }
    }
    ++*progress_count;
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_init(
    hermas_daemon_loop *loop,
    hermas_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size) {
    if (loop == NULL || registry == NULL || image == NULL) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    if (hermas_image_validate(image, image_size, NULL) != HERMAS_IMAGE_OK) {
        return HERMAS_LOOP_INVALID_IMAGE;
    }
    if (image_requires_group_runtime(image)) {
        return HERMAS_LOOP_INVALID_IMAGE;
    }
    memset(loop, 0, sizeof(*loop));
    loop->image = image;
    loop->image_size = image_size;
    loop->registry = registry;
    loop->image_fingerprint =
        hermas_journal_image_fingerprint(image, image_size);
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_attach_journal(
    hermas_daemon_loop *loop,
    hermas_journal_writer *journal,
    uint32_t workflow_id) {
    if (loop == NULL || journal == NULL || workflow_id == 0u ||
        loop->image == NULL || hermas_daemon_loop_active(loop) != 0u) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    loop->journal = journal;
    loop->workflow_id = workflow_id;
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_set_execution_floor(
    hermas_daemon_loop *loop,
    uint64_t minimum_execution_id) {
    if (loop == NULL || loop->image == NULL ||
        loop->journal == NULL || minimum_execution_id == 0u ||
        hermas_daemon_loop_active(loop) != 0u) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    loop->minimum_execution_id = minimum_execution_id;
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_attach_compensation(
    hermas_daemon_loop *loop,
    hermas_compensation_writer *compensation) {
    if (loop == NULL || compensation == NULL ||
        compensation->write == NULL || loop->image == NULL ||
        hermas_daemon_loop_active(loop) != 0u) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    loop->compensation = compensation;
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_attach_results(
    hermas_daemon_loop *loop,
    hermas_result_writer *results,
    hermas_result_lookup result_lookup,
    void *result_lookup_context) {
    if (loop == NULL || results == NULL ||
        results->write == NULL || result_lookup == NULL ||
        loop->image == NULL ||
        hermas_daemon_loop_active(loop) != 0u) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    loop->results = results;
    loop->result_lookup = result_lookup;
    loop->result_lookup_context = result_lookup_context;
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_attach_saga(
    hermas_daemon_loop *loop,
    hermas_compensation_writer *compensation,
    hermas_compensation_lookup compensation_lookup,
    void *compensation_lookup_context,
    hermas_saga_log_writer *saga_log) {
    if (loop == NULL || compensation == NULL ||
        compensation->write == NULL || compensation_lookup == NULL ||
        saga_log == NULL || saga_log->write == NULL ||
        loop->image == NULL ||
        hermas_daemon_loop_active(loop) != 0u) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    loop->compensation = compensation;
    loop->compensation_lookup = compensation_lookup;
    loop->compensation_lookup_context =
        compensation_lookup_context;
    loop->saga_log = saga_log;
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_resume_saga(
    hermas_daemon_loop *loop,
    const hermas_saga_execution *execution) {
    if (loop == NULL || execution == NULL ||
        loop->compensation_lookup == NULL ||
        loop->saga_log == NULL ||
        execution->state != HERMAS_SAGA_READY ||
        execution->remaining == 0u ||
        execution->execution_id == 0u ||
        execution->workflow_id != loop->workflow_id ||
        execution->image_fingerprint != loop->image_fingerprint ||
        find_execution(loop, execution->execution_id) != NULL) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    for (size_t index = 0u;
         index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas_loop_slot *slot = &loop->executions[index];
        if (slot->active) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        hermas_saga_execution resumed = *execution;
        resumed.image = loop->image;
        resumed.image_size = loop->image_size;
        resumed.tokens = NULL;
        resumed.token_bytes = 0u;
        resumed.token_lookup = loop->compensation_lookup;
        resumed.token_lookup_context =
            loop->compensation_lookup_context;
        if (hermas_saga_driver_begin(
                &slot->saga, &resumed, loop->saga_log, 1) !=
            HERMAS_SAGA_OK) {
            memset(slot, 0, sizeof(*slot));
            return HERMAS_LOOP_COMPENSATION_ERROR;
        }
        uint16_t terminal_outcome = HERMAS_OUTCOME_UNKNOWN;
        uint16_t source_type = 0u;
        uint16_t destination_type = 0u;
        size_t value_length = 0u;
        if (loop->result_lookup != NULL &&
            execution->original_outcome ==
                HERMAS_OUTCOME_APP_ERROR) {
            hermas_result_record result;
            int found = 0;
            hermas_result_store_result looked_up =
                loop->result_lookup(
                    loop->result_lookup_context,
                    (hermas_result_key){
                        .execution_id = execution->execution_id,
                        .workflow_id = execution->workflow_id,
                        .image_fingerprint =
                            execution->image_fingerprint
                    },
                    &result, slot->value, sizeof(slot->value),
                    &found);
            if (looked_up != HERMAS_RESULT_STORE_OK ||
                found == 0 ||
                result.key.execution_id !=
                    execution->execution_id ||
                result.key.workflow_id != execution->workflow_id ||
                result.key.image_fingerprint !=
                    execution->image_fingerprint ||
                result.outcome != execution->original_outcome ||
                !recovered_failure_matches(
                    loop, execution, &result) ||
                hermas_image_validate_value(
                    loop->image, loop->image_size,
                    result.source_type, slot->value,
                    result.value_length) != HERMAS_IMAGE_OK ||
                hermas_image_validate_value(
                    loop->image, loop->image_size,
                    result.destination_type, slot->value,
                    result.value_length) != HERMAS_IMAGE_OK) {
                memset(slot, 0, sizeof(*slot));
                return HERMAS_LOOP_RESULT_ERROR;
            }
            terminal_outcome = result.outcome;
            source_type = result.source_type;
            destination_type = result.destination_type;
            value_length = result.value_length;
        }
        slot->execution = (hermas_execution){
            .image = loop->image,
            .image_size = loop->image_size,
            .value_buffer = slot->value,
            .value_capacity = sizeof(slot->value),
            .value_length = value_length,
            .execution_id = execution->execution_id,
            .value_source_type = source_type,
            .value_destination_type = destination_type,
            .terminal_outcome = terminal_outcome,
            .state = HERMAS_EXECUTION_COMPLETE
        };
        slot->active = true;
        slot->journal_finished = true;
        slot->result_stored =
            terminal_outcome != HERMAS_OUTCOME_UNKNOWN;
        slot->compensating = true;
        return HERMAS_LOOP_OK;
    }
    return HERMAS_LOOP_CAPACITY_EXHAUSTED;
}

hermas_loop_result hermas_daemon_loop_admit(
    hermas_daemon_loop *loop,
    uint64_t execution_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length) {
    if (loop == NULL || execution_id == 0u ||
        execution_id == UINT64_MAX) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    if (loop->minimum_execution_id != 0u &&
        execution_id < loop->minimum_execution_id) {
        return HERMAS_LOOP_DUPLICATE_EXECUTION;
    }
    if (find_execution(loop, execution_id) != NULL) {
        return HERMAS_LOOP_DUPLICATE_EXECUTION;
    }
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas_loop_slot *slot = &loop->executions[index];
        if (slot->active) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        hermas_runtime_result started = hermas_execution_start(
            &slot->execution, loop->image, loop->image_size, execution_id,
            slot->value, sizeof(slot->value), input_type, input, input_length);
        if (started != HERMAS_RUNTIME_OK) {
            return HERMAS_LOOP_RUNTIME_ERROR;
        }
        hermas_loop_result journaled = append_record(
            loop, slot, HERMAS_JOURNAL_EXECUTION_STARTED,
            HERMAS_OUTCOME_NONE, false);
        if (journaled != HERMAS_LOOP_OK) {
            memset(slot, 0, sizeof(*slot));
            return journaled;
        }
        if (loop->minimum_execution_id != 0u) {
            loop->minimum_execution_id = execution_id + 1u;
        }
        slot->active = true;
        return HERMAS_LOOP_OK;
    }
    return HERMAS_LOOP_CAPACITY_EXHAUSTED;
}

hermas_loop_result hermas_daemon_loop_poll(
    hermas_daemon_loop *loop,
    int timeout_milliseconds,
    size_t *progress_count) {
    if (loop == NULL || progress_count == NULL || timeout_milliseconds < -1) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    *progress_count = 0u;
    hermas_loop_result prepared = prepare_ready(loop, progress_count);
    if (prepared != HERMAS_LOOP_OK) {
        return prepared;
    }
    hermas_loop_result reconciled =
        reconcile_missing_actions(loop, progress_count);
    if (reconciled != HERMAS_LOOP_OK) {
        return reconciled;
    }
    assign_available_actions(loop);
    struct pollfd poll_items[HERMAS_DAEMON_MAX_EXECUTIONS];
    hermas_loop_slot *owners[HERMAS_DAEMON_MAX_EXECUTIONS];
    size_t poll_count = 0u;
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        hermas_loop_slot *slot = &loop->executions[index];
        if (!slot->active || !slot->owns_action) {
            continue;
        }
        int descriptor =
            hermas_daemon_registry_find_action(
                loop->registry, slot->app_id, slot->action_id, NULL);
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
        return HERMAS_LOOP_POLL_ERROR;
    }
    for (size_t index = 0u; index < poll_count; ++index) {
        hermas_loop_slot *slot = owners[index];
        short events = poll_items[index].revents;
        hermas_loop_result result = HERMAS_LOOP_OK;
        if ((events & POLLOUT) != 0 && slot_prepared(slot)) {
            result = send_invocation(loop, slot, progress_count);
        } else if ((events & POLLIN) != 0 && slot_sent(slot)) {
            result = receive_result(loop, slot, progress_count);
        } else if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            result = slot_prepared(slot)
                         ? send_invocation(loop, slot, progress_count)
                         : receive_result(loop, slot, progress_count);
        }
        if (result != HERMAS_LOOP_OK) {
            return result;
        }
    }
    return HERMAS_LOOP_OK;
}

hermas_loop_result hermas_daemon_loop_result(
    const hermas_daemon_loop *loop,
    uint64_t execution_id,
    hermas_frame *result) {
    if (loop == NULL || result == NULL || execution_id == 0u) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    const hermas_loop_slot *slot =
        find_const_execution(loop, execution_id);
    if (slot == NULL) {
        return HERMAS_LOOP_UNKNOWN_EXECUTION;
    }
    if (slot->compensating) {
        return HERMAS_LOOP_EXECUTION_ACTIVE;
    }
    return hermas_execution_get_result(&slot->execution, result) ==
                   HERMAS_RUNTIME_OK
               ? HERMAS_LOOP_OK
               : HERMAS_LOOP_EXECUTION_ACTIVE;
}

hermas_loop_result hermas_daemon_loop_release(
    hermas_daemon_loop *loop,
    uint64_t execution_id) {
    if (loop == NULL || execution_id == 0u) {
        return HERMAS_LOOP_INVALID_ARGUMENT;
    }
    hermas_loop_slot *slot = find_execution(loop, execution_id);
    if (slot == NULL) {
        return HERMAS_LOOP_UNKNOWN_EXECUTION;
    }
    if (slot->execution.state != HERMAS_EXECUTION_COMPLETE ||
        slot->compensating) {
        return HERMAS_LOOP_EXECUTION_ACTIVE;
    }
    memset(slot, 0, sizeof(*slot));
    return HERMAS_LOOP_OK;
}

size_t hermas_daemon_loop_active(const hermas_daemon_loop *loop) {
    if (loop == NULL) {
        return 0u;
    }
    size_t count = 0u;
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        if (loop->executions[index].active) {
            ++count;
        }
    }
    return count;
}

const char *hermas_loop_result_name(hermas_loop_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "invalid-image", "capacity-exhausted",
        "duplicate-execution", "unknown-execution", "execution-active",
        "poll-error", "runtime-error", "protocol-error", "journal-error",
        "compensation-error", "result-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
