#include "hermas/image.h"
#include "hermas/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__)
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static int fail(const char *message) {
    fprintf(stderr, "test_parallel_runtime: %s\n", message);
    return 1;
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint16_t action_success_type(
    const uint8_t *image,
    uint16_t node) {
    size_t count = read_u16(
        image, HERMAS_IMAGE_HEADER_EDGE_COUNT_OFFSET);
    size_t edges = read_u32(
        image, HERMAS_IMAGE_HEADER_EDGES_OFFSET);
    for (size_t index = 0u; index < count; ++index) {
        size_t edge =
            edges + index * HERMAS_IMAGE_EDGE_RECORD_SIZE;
        if (image[edge] == 1u && read_u16(image, edge + 4u) == node) {
            return read_u16(image, edge + 8u);
        }
    }
    return 0u;
}

static uint8_t *read_fixture(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(bytes);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static hermas_frame success(
    const hermas_frame *request,
    uint16_t type,
    const uint8_t *payload,
    uint32_t payload_length) {
    return (hermas_frame){
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = request->execution_id,
        .request_id = request->request_id,
        .app_id = request->app_id,
        .action_id = request->action_id,
        .source_type = type,
        .destination_type = type,
        .outcome = HERMAS_OUTCOME_SUCCESS,
        .payload = payload,
        .payload_length = payload_length
    };
}

#if defined(__unix__)
static int deliver_over_independent_sockets(
    const hermas_frame *alpha,
    const hermas_frame *beta) {
    int sockets[2][2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets[0]) != 0 ||
        socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets[1]) != 0) {
        return fail("cannot create independent branch sockets");
    }
    uint8_t packets[2][HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    size_t sizes[2] = {0u, 0u};
    const hermas_frame *frames[2] = {alpha, beta};
    for (size_t index = 0u; index < 2u; ++index) {
        if (hermas_protocol_encode(
                frames[index], packets[index], sizeof(packets[index]),
                &sizes[index]) != HERMAS_PROTOCOL_OK ||
            send(sockets[index][0], packets[index], sizes[index],
                 MSG_NOSIGNAL) != (ssize_t)sizes[index]) {
            return fail("could not deliver parallel branch");
        }
    }
    for (size_t index = 0u; index < 2u; ++index) {
        uint8_t received[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
        ssize_t length =
            recv(sockets[index][1], received, sizeof(received), 0);
        hermas_frame decoded;
        if (length <= 0 ||
            hermas_protocol_decode(received, (size_t)length, &decoded) !=
                HERMAS_PROTOCOL_OK ||
            decoded.request_id != frames[index]->request_id ||
            recv(sockets[index][1], received, sizeof(received),
                 MSG_DONTWAIT) >= 0 ||
            (errno != EAGAIN && errno != EWOULDBLOCK)) {
            return fail("parallel branch delivery was not exactly once");
        }
        close(sockets[index][0]);
        close(sockets[index][1]);
    }
    return 0;
}
#endif

static int reach_fork(
    hermas_group_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][8],
    uint64_t execution_id) {
    if (hermas_group_start(
            execution, image, image_size, execution_id, &storage[0][0],
            HERMAS_RUNTIME_MAX_FLOWS * 8u, 8u, 3u, NULL, 0u) !=
        HERMAS_RUNTIME_OK) {
        return fail("could not start bounded all");
    }
    hermas_frame request;
    if (hermas_group_prepare(execution, 0u, &request) !=
            HERMAS_RUNTIME_OK ||
        request.app_id != 1u ||
        hermas_group_mark_sent(execution, 0u) != HERMAS_RUNTIME_OK) {
        return fail("source Action was not prepared");
    }
    uint8_t item[8] = {42u};
    hermas_frame response = success(&request, 1u, item, sizeof(item));
    if (hermas_group_accept_result(execution, 0u, &response) !=
            HERMAS_RUNTIME_OK ||
        execution->flows[0].state != HERMAS_EXECUTION_READY ||
        execution->flows[1].state != HERMAS_EXECUTION_READY) {
        return fail("Fork did not make both branches ready");
    }
    return 0;
}

static int test_overlap(const uint8_t *image, size_t image_size) {
    hermas_group_execution execution;
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][8] = {{0u}};
    int result = reach_fork(&execution, image, image_size, storage, 301u);
    if (result != 0) {
        return result;
    }
    hermas_frame alpha;
    hermas_frame beta;
    if (hermas_group_prepare(&execution, 0u, &alpha) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_prepare(&execution, 1u, &beta) !=
            HERMAS_RUNTIME_OK ||
        alpha.app_id != 2u || beta.app_id != 3u ||
        alpha.request_id == beta.request_id) {
        return fail("independent branches did not overlap");
    }
#if defined(__unix__)
    if (deliver_over_independent_sockets(&alpha, &beta) != 0) {
        return 1;
    }
#endif
    if (hermas_group_mark_sent(&execution, 0u) != HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&execution, 1u) != HERMAS_RUNTIME_OK) {
        return fail("delivered branches did not enter sent state");
    }
    uint8_t beta_value[8] = {44u};
    hermas_frame beta_result =
        success(&beta, 9u, beta_value, sizeof(beta_value));
    if (hermas_group_accept_result(&execution, 1u, &beta_result) !=
            HERMAS_RUNTIME_OK ||
        execution.flows[1].state != HERMAS_EXECUTION_WAITING) {
        return fail("Join completed before every branch");
    }
    uint8_t alpha_value[8] = {43u};
    hermas_frame alpha_result =
        success(&alpha, 6u, alpha_value, sizeof(alpha_value));
    if (hermas_group_accept_result(&execution, 0u, &alpha_result) !=
            HERMAS_RUNTIME_OK ||
        execution.flows[0].state != HERMAS_EXECUTION_READY ||
        execution.flows[1].active != 0u) {
        return fail("Join did not expose the named used field");
    }
    hermas_frame sink;
    if (hermas_group_prepare(&execution, 0u, &sink) !=
            HERMAS_RUNTIME_OK ||
        sink.app_id != 4u ||
        hermas_group_mark_sent(&execution, 0u) != HERMAS_RUNTIME_OK) {
        return fail("post-Join Action was not prepared");
    }
    uint8_t done[1] = {1u};
    hermas_frame sink_result = success(&sink, 10u, done, sizeof(done));
    hermas_frame final;
    if (hermas_group_accept_result(&execution, 0u, &sink_result) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_get_result(&execution, &final) != HERMAS_RUNTIME_OK ||
        final.outcome != HERMAS_OUTCOME_SUCCESS ||
        final.payload_length != 1u || final.payload[0] != 1u) {
        return fail("bounded all did not complete successfully");
    }
    return 0;
}

static int test_unknown_precedence(const uint8_t *image, size_t image_size) {
    hermas_group_execution execution;
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][8] = {{0u}};
    int result = reach_fork(&execution, image, image_size, storage, 302u);
    if (result != 0) {
        return result;
    }
    hermas_frame alpha;
    hermas_frame beta;
    if (hermas_group_prepare(&execution, 0u, &alpha) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_prepare(&execution, 1u, &beta) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&execution, 0u) != HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&execution, 1u) != HERMAS_RUNTIME_OK) {
        return fail("could not deliver precedence branches");
    }
    hermas_frame alpha_error = {
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = alpha.execution_id,
        .request_id = alpha.request_id,
        .app_id = alpha.app_id,
        .action_id = alpha.action_id,
        .source_type = 4u,
        .destination_type = 4u,
        .outcome = HERMAS_OUTCOME_APP_ERROR
    };
    if (hermas_group_accept_result(&execution, 0u, &alpha_error) !=
            HERMAS_RUNTIME_OK ||
        execution.complete != 0u ||
        hermas_group_mark_unknown(&execution, 1u) != HERMAS_RUNTIME_OK) {
        return fail("delivered sibling was not awaited");
    }
    hermas_frame final;
    if (hermas_group_get_result(&execution, &final) != HERMAS_RUNTIME_OK ||
        final.outcome != HERMAS_OUTCOME_UNKNOWN) {
        return fail("Unknown did not outrank a known branch failure");
    }
    return 0;
}

static int test_known_failure_after_sibling_reaches_join(
    const uint8_t *image,
    size_t image_size) {
    hermas_group_execution execution;
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][8] = {{0u}};
    int result = reach_fork(&execution, image, image_size, storage, 303u);
    if (result != 0) {
        return result;
    }
    hermas_frame alpha;
    hermas_frame beta;
    if (hermas_group_prepare(&execution, 0u, &alpha) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_prepare(&execution, 1u, &beta) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&execution, 0u) != HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&execution, 1u) != HERMAS_RUNTIME_OK) {
        return fail("could not deliver known-failure branches");
    }
    hermas_frame alpha_error = {
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = alpha.execution_id,
        .request_id = alpha.request_id,
        .app_id = alpha.app_id,
        .action_id = alpha.action_id,
        .source_type = 4u,
        .destination_type = 4u,
        .outcome = HERMAS_OUTCOME_APP_ERROR
    };
    uint8_t beta_value[8] = {44u};
    hermas_frame beta_result =
        success(&beta, 9u, beta_value, sizeof(beta_value));
    if (hermas_group_accept_result(&execution, 0u, &alpha_error) !=
            HERMAS_RUNTIME_OK ||
        execution.complete != 0u ||
        hermas_group_accept_result(&execution, 1u, &beta_result) !=
            HERMAS_RUNTIME_OK) {
        return fail("known branch failure did not await delivered sibling");
    }
    hermas_frame final;
    if (hermas_group_get_result(&execution, &final) != HERMAS_RUNTIME_OK ||
        final.outcome != HERMAS_OUTCOME_APP_ERROR) {
        return fail("known branch failure did not finish after sibling joined");
    }
    return 0;
}

static int test_unsent_cutoff(const uint8_t *image, size_t image_size) {
    hermas_group_execution execution;
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][8] = {{0u}};
    int result = reach_fork(&execution, image, image_size, storage, 303u);
    if (result != 0) {
        return result;
    }
    hermas_frame alpha;
    if (hermas_group_prepare(&execution, 0u, &alpha) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_not_sent(&execution, 0u) !=
            HERMAS_RUNTIME_OK ||
        execution.flows[1].active != 0u) {
        return fail("failure did not stop undelivered sibling admission");
    }
    hermas_frame final;
    if (hermas_group_get_result(&execution, &final) != HERMAS_RUNTIME_OK ||
        final.outcome != HERMAS_OUTCOME_NOT_SENT) {
        return fail("NotSent cutoff outcome differs");
    }
    return 0;
}

static int test_same_app_serialization(
    const uint8_t *image,
    size_t image_size) {
    hermas_group_execution execution;
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][8] = {{0u}};
    int result = reach_fork(&execution, image, image_size, storage, 304u);
    if (result != 0) {
        return result;
    }
    hermas_frame first;
    hermas_frame second;
    if (hermas_group_prepare(&execution, 0u, &first) !=
            HERMAS_RUNTIME_OK ||
        first.app_id != 2u ||
        hermas_group_prepare(&execution, 1u, &second) !=
            HERMAS_RUNTIME_INVALID_STATE ||
        hermas_group_mark_sent(&execution, 0u) != HERMAS_RUNTIME_OK ||
        hermas_group_prepare(&execution, 1u, &second) !=
            HERMAS_RUNTIME_INVALID_STATE) {
        return fail("same-app branches were prepared concurrently");
    }
    uint8_t first_value[8] = {43u};
    hermas_frame first_result =
        success(&first, 6u, first_value, sizeof(first_value));
    if (hermas_group_accept_result(&execution, 0u, &first_result) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_prepare(&execution, 1u, &second) !=
            HERMAS_RUNTIME_OK ||
        second.app_id != first.app_id) {
        return fail("same-app sibling was not admitted after release");
    }
    return 0;
}

static int test_deadline_regions(
    const uint8_t *image,
    size_t image_size) {
    hermas_image_summary summary;
    if (hermas_image_validate(image, image_size, &summary) !=
        HERMAS_IMAGE_OK) {
        return fail("deadline image is invalid");
    }
    uint8_t input[8] = {0u};
    uint8_t *malformed = malloc(image_size);
    if (malformed == NULL) {
        return fail("cannot allocate malformed deadline image");
    }
    memcpy(malformed, image, image_size);
    size_t region = read_u32(
        malformed, HERMAS_IMAGE_HEADER_REGIONS_OFFSET);
    memset(malformed + region + 8u, 0, 8u);
    if (hermas_image_validate(malformed, image_size, NULL) ==
        HERMAS_IMAGE_OK) {
        free(malformed);
        return fail("zero-duration deadline region was accepted");
    }
    free(malformed);
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][264] = {{0u}};
    hermas_group_execution before_delivery;
    if (hermas_group_start(
            &before_delivery, image, image_size, 305u, &storage[0][0],
            sizeof(storage), sizeof(storage[0]), summary.input_type,
            input,
            sizeof(input)) != HERMAS_RUNTIME_OK ||
        hermas_group_deadline_ms(&before_delivery) != 5000u ||
        hermas_group_expire(&before_delivery) != HERMAS_RUNTIME_OK) {
        return fail("pre-delivery deadline could not expire");
    }
    hermas_frame result;
    if (hermas_group_get_result(&before_delivery, &result) !=
            HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_NOT_SENT) {
        return fail("pre-delivery deadline was not NotSent");
    }

    hermas_group_execution after_delivery;
    if (hermas_group_start(
            &after_delivery, image, image_size, 306u, &storage[0][0],
            sizeof(storage), sizeof(storage[0]), summary.input_type,
            input,
            sizeof(input)) != HERMAS_RUNTIME_OK) {
        return fail("post-delivery deadline could not start");
    }
    hermas_frame request;
    if (hermas_group_prepare(&after_delivery, 0u, &request) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&after_delivery, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_expire(&after_delivery) != HERMAS_RUNTIME_OK ||
        hermas_group_get_result(&after_delivery, &result) !=
            HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_UNKNOWN) {
        return fail("post-delivery deadline was not Unknown");
    }
    return 0;
}

static int enter_nested_region(
    hermas_group_execution *execution,
    const uint8_t *image,
    size_t image_size,
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][264],
    uint64_t execution_id) {
    if (hermas_group_start(
            execution, image, image_size, execution_id, &storage[0][0],
            HERMAS_RUNTIME_MAX_FLOWS * 264u, 264u, 1u, NULL, 0u) !=
        HERMAS_RUNTIME_OK) {
        return fail("cannot start nested deadline execution");
    }
    hermas_frame request;
    if (hermas_group_prepare(execution, 0u, &request) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(execution, 0u) != HERMAS_RUNTIME_OK) {
        return fail("cannot deliver pre-region Action");
    }
    uint8_t empty_list[8] = {0u};
    hermas_frame response =
        success(&request, 4u, empty_list, sizeof(empty_list));
    if (hermas_group_accept_result(execution, 0u, &response) !=
            HERMAS_RUNTIME_OK ||
        execution->flows[0].current_node != 2u ||
        execution->flows[0].state != HERMAS_EXECUTION_READY) {
        return fail("nested deadline region was not entered");
    }
    return 0;
}

static int test_nested_deadline_region(
    const uint8_t *image,
    size_t image_size) {
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][264] = {{0u}};
    hermas_group_execution before_delivery;
    if (enter_nested_region(
            &before_delivery, image, image_size, storage, 307u) != 0 ||
        hermas_group_region_deadline_ms(&before_delivery, 1u) != 5000u ||
        hermas_group_region_deadline_ms(&before_delivery, 2u) != 1000u ||
        hermas_group_expire_region(&before_delivery, 2u) !=
            HERMAS_RUNTIME_OK) {
        return fail("nested pre-delivery expiry failed");
    }
    hermas_frame result;
    if (hermas_group_get_result(&before_delivery, &result) !=
            HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_NOT_SENT) {
        return fail("nested pre-delivery expiry was not NotSent");
    }

    hermas_group_execution after_delivery;
    if (enter_nested_region(
            &after_delivery, image, image_size, storage, 308u) != 0) {
        return 1;
    }
    hermas_frame request;
    if (hermas_group_prepare(&after_delivery, 0u, &request) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&after_delivery, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_expire_region(&after_delivery, 2u) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_get_result(&after_delivery, &result) !=
            HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_UNKNOWN) {
        return fail("nested post-delivery expiry was not Unknown");
    }
    return 0;
}

static int test_bounded_each(
    const uint8_t *image,
    size_t image_size) {
    uint8_t *malformed = malloc(image_size);
    if (malformed == NULL) {
        return fail("cannot allocate malformed each image");
    }
    memcpy(malformed, image, image_size);
    size_t region = read_u32(
        malformed, HERMAS_IMAGE_HEADER_REGIONS_OFFSET);
    malformed[region + 1u] = 0u;
    if (hermas_image_validate(malformed, image_size, NULL) ==
        HERMAS_IMAGE_OK) {
        free(malformed);
        return fail("zero-concurrency each region was accepted");
    }
    free(malformed);

    hermas_image_summary summary;
    if (hermas_image_validate(image, image_size, &summary) !=
        HERMAS_IMAGE_OK) {
        return fail("bounded each image is invalid");
    }
    hermas_group_execution execution;
    uint8_t storage[HERMAS_RUNTIME_MAX_FLOWS][128] = {{0u}};
    if (hermas_group_start(
            &execution, image, image_size, 309u, &storage[0][0],
            sizeof(storage), sizeof(storage[0]), summary.input_type,
            NULL, 0u) != HERMAS_RUNTIME_OK) {
        return fail("bounded each could not start");
    }
    hermas_frame request;
    if (hermas_group_prepare(&execution, 0u, &request) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&execution, 0u) != HERMAS_RUNTIME_OK) {
        return fail("bounded each source was not delivered");
    }
    uint8_t orders[40] = {0u};
    orders[0] = 4u;
    orders[8] = 10u;
    orders[16] = 20u;
    orders[24] = 30u;
    orders[32] = 40u;
    uint16_t list_type = action_success_type(image, 1u);
    hermas_frame listed =
        success(&request, list_type, orders, sizeof(orders));
    if (list_type == 0u ||
        hermas_group_accept_result(&execution, 0u, &listed) !=
            HERMAS_RUNTIME_OK) {
        return fail("bounded each did not accept its source list");
    }
    size_t ready = 0u;
    for (size_t index = 0u; index < HERMAS_RUNTIME_MAX_FLOWS; ++index) {
        if (execution.flows[index].active != 0u &&
            execution.flows[index].state == HERMAS_EXECUTION_READY) {
            ++ready;
        }
    }
    if (ready != 3u) {
        return fail("bounded each did not admit its concurrency ceiling");
    }

    uint8_t reports[4][8] = {{101u}, {102u}, {103u}, {104u}};
    hermas_frame item_requests[4];
    size_t item_slots[4] = {0u};
    for (size_t item = 0u; item < 3u; ++item) {
        size_t slot = HERMAS_RUNTIME_MAX_FLOWS;
        for (size_t index = 0u; index < HERMAS_RUNTIME_MAX_FLOWS; ++index) {
            if (execution.flows[index].active != 0u &&
                execution.flows[index].state == HERMAS_EXECUTION_READY &&
                execution.flows[index].item_index == item) {
                slot = index;
                break;
            }
        }
        if (slot == HERMAS_RUNTIME_MAX_FLOWS ||
            execution.flows[slot].current_node != 2u ||
            hermas_group_prepare(&execution, slot, &request) !=
                HERMAS_RUNTIME_OK ||
            hermas_group_mark_sent(&execution, slot) !=
                HERMAS_RUNTIME_OK) {
            return fail("bounded each item was not delivered");
        }
        item_requests[item] = request;
        item_slots[item] = slot;
    }
    const size_t completion_order[3] = {1u, 0u, 2u};
    for (size_t completed = 0u; completed < 3u; ++completed) {
        size_t item = completion_order[completed];
        uint16_t report_type = action_success_type(image, 2u);
        hermas_frame reported = success(
            &item_requests[item], report_type, reports[item],
            sizeof(reports[item]));
        if (report_type == 0u ||
            hermas_group_accept_result(
                &execution, item_slots[item], &reported) !=
                HERMAS_RUNTIME_OK) {
            return fail("bounded each item result was rejected");
        }
    }
    size_t fourth_slot = HERMAS_RUNTIME_MAX_FLOWS;
    for (size_t index = 0u; index < HERMAS_RUNTIME_MAX_FLOWS; ++index) {
        if (execution.flows[index].active != 0u &&
            execution.flows[index].state == HERMAS_EXECUTION_READY &&
            execution.flows[index].item_index == 3u) {
            fourth_slot = index;
            break;
        }
    }
    if (fourth_slot == HERMAS_RUNTIME_MAX_FLOWS ||
        hermas_group_prepare(
            &execution, fourth_slot, &item_requests[3]) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(&execution, fourth_slot) !=
            HERMAS_RUNTIME_OK) {
        return fail("bounded each did not replenish below its ceiling");
    }
    hermas_frame fourth = success(
        &item_requests[3], action_success_type(image, 2u), reports[3],
        sizeof(reports[3]));
    if (hermas_group_accept_result(
            &execution, fourth_slot, &fourth) != HERMAS_RUNTIME_OK) {
        return fail("bounded each fourth result was rejected");
    }

    size_t archive_slot = HERMAS_RUNTIME_MAX_FLOWS;
    for (size_t index = 0u; index < HERMAS_RUNTIME_MAX_FLOWS; ++index) {
        if (execution.flows[index].active != 0u &&
            execution.flows[index].state == HERMAS_EXECUTION_READY) {
            archive_slot = index;
            break;
        }
    }
    if (archive_slot == HERMAS_RUNTIME_MAX_FLOWS ||
        hermas_group_prepare(
            &execution, archive_slot, &request) != HERMAS_RUNTIME_OK ||
        request.payload_length != sizeof(orders) ||
        request.payload[0] != 4u ||
        request.payload[8] != 101u ||
        request.payload[16] != 102u ||
        request.payload[24] != 103u ||
        request.payload[32] != 104u) {
        return fail("collect did not preserve source item order");
    }

    hermas_group_execution *cutoff = malloc(sizeof(*cutoff));
    if (cutoff == NULL ||
        hermas_group_start(
            cutoff, image, image_size, 310u, &storage[0][0],
            sizeof(storage), sizeof(storage[0]), summary.input_type,
            NULL, 0u) != HERMAS_RUNTIME_OK ||
        hermas_group_prepare(cutoff, 0u, &request) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_sent(cutoff, 0u) != HERMAS_RUNTIME_OK) {
        free(cutoff);
        return fail("bounded each cutoff execution could not start");
    }
    listed = success(&request, list_type, orders, sizeof(orders));
    if (hermas_group_accept_result(cutoff, 0u, &listed) !=
            HERMAS_RUNTIME_OK) {
        free(cutoff);
        return fail("bounded each cutoff source list was rejected");
    }
    size_t cutoff_slot = HERMAS_RUNTIME_MAX_FLOWS;
    for (size_t index = 0u; index < HERMAS_RUNTIME_MAX_FLOWS; ++index) {
        if (cutoff->flows[index].active != 0u &&
            cutoff->flows[index].state == HERMAS_EXECUTION_READY) {
            cutoff_slot = index;
            break;
        }
    }
    hermas_frame final;
    if (cutoff_slot == HERMAS_RUNTIME_MAX_FLOWS ||
        hermas_group_prepare(cutoff, cutoff_slot, &request) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_mark_not_sent(cutoff, cutoff_slot) !=
            HERMAS_RUNTIME_OK ||
        hermas_group_get_result(cutoff, &final) != HERMAS_RUNTIME_OK ||
        final.outcome != HERMAS_OUTCOME_NOT_SENT) {
        free(cutoff);
        return fail("bounded each did not cut off undelivered items");
    }
    for (size_t index = 0u; index < HERMAS_RUNTIME_MAX_FLOWS; ++index) {
        if (cutoff->flows[index].active != 0u) {
            free(cutoff);
            return fail("bounded each retained work after cutoff");
        }
    }
    free(cutoff);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        return fail("expected bounded-all, same-app, deadline, nested, and each images");
    }
    size_t image_size = 0u;
    uint8_t *image = read_fixture(argv[1], &image_size);
    if (image == NULL) {
        return fail("cannot read bounded-all fixture");
    }
    int result = test_overlap(image, image_size);
    if (result == 0) {
        result = test_unknown_precedence(image, image_size);
    }
    if (result == 0) {
        result = test_known_failure_after_sibling_reaches_join(
            image, image_size);
    }
    if (result == 0) {
        result = test_unsent_cutoff(image, image_size);
    }
    free(image);
    if (result == 0) {
        image = read_fixture(argv[2], &image_size);
        if (image == NULL) {
            return fail("cannot read same-app fixture");
        }
        result = test_same_app_serialization(image, image_size);
        free(image);
    }
    if (result == 0) {
        image = read_fixture(argv[3], &image_size);
        if (image == NULL) {
            return fail("cannot read deadline fixture");
        }
        result = test_deadline_regions(image, image_size);
        free(image);
    }
    if (result == 0) {
        image = read_fixture(argv[4], &image_size);
        if (image == NULL) {
            return fail("cannot read nested deadline fixture");
        }
        result = test_nested_deadline_region(image, image_size);
        free(image);
    }
    if (result == 0) {
        image = read_fixture(argv[5], &image_size);
        if (image == NULL) {
            return fail("cannot read bounded each fixture");
        }
        result = test_bounded_each(image, image_size);
        free(image);
    }
    return result;
}
