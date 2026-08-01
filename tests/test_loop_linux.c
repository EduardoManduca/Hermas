#include "hermas/daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_loop: %s\n", message);
    return 1;
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

static hermas_daemon_action *action_slot(
    hermas_daemon_registry *registry,
    uint16_t app_id,
    uint16_t action_id) {
    for (size_t index = 0u; index < registry->action_count; ++index) {
        if (registry->actions[index].app_id == app_id &&
            registry->actions[index].action_id == action_id) {
            return &registry->actions[index];
        }
    }
    return NULL;
}

static int receive_request(int descriptor, hermas_frame *request) {
    static uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    ssize_t received = recv(descriptor, packet, sizeof(packet), 0);
    return received > 0 &&
           hermas_protocol_decode(packet, (size_t)received, request) ==
               HERMAS_PROTOCOL_OK &&
           request->kind == HERMAS_FRAME_INVOKE;
}

static int receive_request_now(int descriptor, hermas_frame *request) {
    static uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    ssize_t received = recv(
        descriptor, packet, sizeof(packet), MSG_DONTWAIT);
    return received > 0 &&
           hermas_protocol_decode(packet, (size_t)received, request) ==
               HERMAS_PROTOCOL_OK &&
           request->kind == HERMAS_FRAME_INVOKE;
}

static int send_unit_error(int descriptor, const hermas_frame *request) {
    uint8_t packet[HERMAS_PROTOCOL_HEADER_SIZE];
    size_t packet_size = 0u;
    hermas_frame response = {
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = request->execution_id,
        .request_id = request->request_id,
        .app_id = request->app_id,
        .action_id = request->action_id,
        .source_type = 3u,
        .destination_type = 3u,
        .outcome = HERMAS_OUTCOME_APP_ERROR
    };
    return hermas_protocol_encode(&response, packet, sizeof(packet),
                                   &packet_size) == HERMAS_PROTOCOL_OK &&
           send(descriptor, packet, packet_size, MSG_NOSIGNAL) ==
               (ssize_t)packet_size;
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint16_t action_success_type(
    const uint8_t *image,
    uint16_t app_id) {
    uint16_t node_count = read_u16(
        image, HERMAS_IMAGE_HEADER_NODE_COUNT_OFFSET);
    size_t nodes = read_u32(
        image, HERMAS_IMAGE_HEADER_NODES_OFFSET);
    uint16_t action_node = 0u;
    for (uint16_t index = 0u; index < node_count; ++index) {
        size_t node =
            nodes + (size_t)index * HERMAS_IMAGE_NODE_RECORD_SIZE;
        if (image[node] == 1u &&
            read_u16(image, node + 4u) == app_id) {
            action_node = (uint16_t)(index + 1u);
            break;
        }
    }
    uint16_t edge_count = read_u16(
        image, HERMAS_IMAGE_HEADER_EDGE_COUNT_OFFSET);
    size_t edges = read_u32(
        image, HERMAS_IMAGE_HEADER_EDGES_OFFSET);
    for (uint16_t index = 0u; index < edge_count; ++index) {
        size_t edge =
            edges + (size_t)index * HERMAS_IMAGE_EDGE_RECORD_SIZE;
        if (image[edge] == 1u &&
            read_u16(image, edge + 4u) == action_node) {
            return read_u16(image, edge + 8u);
        }
    }
    return 0u;
}

static int send_success(
    int descriptor,
    const hermas_frame *request,
    uint16_t type,
    const uint8_t *payload,
    uint32_t payload_length) {
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    size_t packet_size = 0u;
    hermas_frame response = {
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
    return hermas_protocol_encode(
               &response, packet, sizeof(packet), &packet_size) ==
               HERMAS_PROTOCOL_OK &&
           send(descriptor, packet, packet_size, MSG_NOSIGNAL) ==
               (ssize_t)packet_size;
}

typedef struct journal_memory {
    uint8_t bytes[8u * HERMAS_JOURNAL_RECORD_SIZE];
    size_t length;
} journal_memory;

static hermas_journal_result write_journal_memory(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    journal_memory *memory = context;
    if (record_size > sizeof(memory->bytes) - memory->length) {
        return HERMAS_JOURNAL_WRITE_ERROR;
    }
    memcpy(memory->bytes + memory->length, record, record_size);
    memory->length += record_size;
    return HERMAS_JOURNAL_OK;
}

static int test_not_sent(
    hermas_daemon_loop *loop,
    hermas_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size) {
    int sockets[2];
    hermas_daemon_action *app = action_slot(registry, 1u, 1u);
    if (app == NULL ||
        socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
        return fail("cannot create NotSent socket pair");
    }
    app->file_descriptor = sockets[0];
    close(sockets[1]);
    if (hermas_daemon_loop_init(loop, registry, image, image_size) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_admit(loop, 201u, 1u, NULL, 0u) !=
            HERMAS_LOOP_OK) {
        return fail("cannot admit NotSent execution");
    }
    size_t progress = 0u;
    if (hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK) {
        return fail("NotSent poll failed");
    }
    hermas_frame result;
    if (hermas_daemon_loop_result(loop, 201u, &result) != HERMAS_LOOP_OK ||
        result.outcome != HERMAS_OUTCOME_NOT_SENT ||
        app->file_descriptor != -1) {
        return fail("pre-delivery disconnect did not become NotSent");
    }
    return 0;
}

static int test_unknown(
    hermas_daemon_loop *loop,
    hermas_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size) {
    int sockets[2];
    hermas_daemon_action *app = action_slot(registry, 1u, 1u);
    if (app == NULL ||
        socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
        return fail("cannot create Unknown socket pair");
    }
    app->registered_action_id = 77u;
    app->file_descriptor = sockets[0];
    if (hermas_daemon_loop_init(loop, registry, image, image_size) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_admit(loop, 202u, 1u, NULL, 0u) !=
            HERMAS_LOOP_OK) {
        return fail("cannot admit Unknown execution");
    }
    size_t progress = 0u;
    hermas_frame request;
    if (hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK ||
        !receive_request(sockets[1], &request) ||
        request.action_id != app->registered_action_id) {
        return fail("invocation did not use the registered local Action ID");
    }
    close(sockets[1]);
    if (hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK) {
        return fail("Unknown poll failed");
    }
    hermas_frame result;
    if (hermas_daemon_loop_result(loop, 202u, &result) != HERMAS_LOOP_OK ||
        result.outcome != HERMAS_OUTCOME_UNKNOWN ||
        app->file_descriptor != -1) {
        return fail("post-delivery disconnect did not become Unknown");
    }
    return 0;
}

static int test_single_flight(
    hermas_daemon_loop *loop,
    hermas_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size) {
    int sockets[2];
    hermas_daemon_action *app = action_slot(registry, 1u, 1u);
    if (app == NULL ||
        socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
        return fail("cannot create single-flight socket pair");
    }
    app->file_descriptor = sockets[0];
    if (hermas_daemon_loop_init(loop, registry, image, image_size) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_admit(loop, 203u, 1u, NULL, 0u) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_admit(loop, 204u, 1u, NULL, 0u) !=
            HERMAS_LOOP_OK) {
        return fail("cannot admit contending executions");
    }
    size_t progress = 0u;
    hermas_frame first;
    if (hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK ||
        !receive_request(sockets[1], &first)) {
        return fail("first same-app invocation was not delivered");
    }
    size_t sent_count = 0u;
    size_t prepared_count = 0u;
    for (size_t index = 0u; index < HERMAS_DAEMON_MAX_EXECUTIONS; ++index) {
        const hermas_loop_slot *slot = &loop->executions[index];
        sent_count += slot->active &&
                      slot->execution.state == HERMAS_EXECUTION_SENT;
        prepared_count += slot->active &&
                          slot->execution.state == HERMAS_EXECUTION_PREPARED;
    }
    if (sent_count != 1u || prepared_count != 1u ||
        !send_unit_error(sockets[1], &first) ||
        hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK ||
        hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK) {
        return fail("same app was not serialized");
    }
    hermas_frame second;
    if (!receive_request(sockets[1], &second) ||
        second.execution_id == first.execution_id ||
        !send_unit_error(sockets[1], &second) ||
        hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK) {
        return fail("second same-app invocation did not advance");
    }
    hermas_frame first_result;
    hermas_frame second_result;
    if (hermas_daemon_loop_result(loop, first.execution_id, &first_result) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_result(loop, second.execution_id,
                                   &second_result) != HERMAS_LOOP_OK ||
        first_result.outcome != HERMAS_OUTCOME_APP_ERROR ||
        second_result.outcome != HERMAS_OUTCOME_APP_ERROR) {
        return fail("serialized execution results differ");
    }
    close(sockets[1]);
    return 0;
}

static int test_durable_delivery_facts(
    hermas_daemon_loop *loop,
    hermas_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size) {
    int sockets[2];
    hermas_daemon_action *app = action_slot(registry, 1u, 1u);
    if (app == NULL ||
        socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
        return fail("cannot create durable journal socket pair");
    }
    if (app->file_descriptor >= 0) {
        close(app->file_descriptor);
    }
    app->file_descriptor = sockets[0];
    journal_memory memory;
    memset(&memory, 0, sizeof(memory));
    hermas_journal_writer writer;
    if (hermas_journal_writer_init(
            &writer, write_journal_memory, &memory, 1u) !=
            HERMAS_JOURNAL_OK ||
        hermas_daemon_loop_init(loop, registry, image, image_size) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_attach_journal(loop, &writer, 11u) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_set_execution_floor(loop, 205u) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_admit(loop, 204u, 1u, NULL, 0u) !=
            HERMAS_LOOP_DUPLICATE_EXECUTION ||
        hermas_daemon_loop_admit(loop, 205u, 1u, NULL, 0u) !=
            HERMAS_LOOP_OK) {
        close(sockets[1]);
        return fail("cannot initialize durable execution");
    }
    size_t progress = 0u;
    hermas_frame request;
    if (hermas_daemon_loop_poll(loop, 1000, &progress) !=
            HERMAS_LOOP_OK ||
        !receive_request(sockets[1], &request)) {
        close(sockets[1]);
        return fail("durable invocation was not delivered");
    }
    hermas_journal_summary summary;
    if (hermas_journal_scan(
            memory.bytes, memory.length, NULL, NULL, &summary) !=
            HERMAS_JOURNAL_OK ||
        summary.record_count != 3u ||
        summary.interrupted_count != 1u ||
        summary.interrupted[0].open_delivery_count != 1u ||
        summary.interrupted[0].open_deliveries[0]
                .delivery_was_sent != 1u) {
        close(sockets[1]);
        return fail("sent delivery facts were not durable");
    }
    if (!send_unit_error(sockets[1], &request) ||
        hermas_daemon_loop_poll(loop, 1000, &progress) !=
            HERMAS_LOOP_OK ||
        hermas_journal_scan(
            memory.bytes, memory.length, NULL, NULL, &summary) !=
            HERMAS_JOURNAL_OK ||
        summary.record_count != 5u ||
        summary.interrupted_count != 0u) {
        close(sockets[1]);
        return fail("completed execution journal did not close");
    }
    static const hermas_journal_kind expected[] = {
        HERMAS_JOURNAL_EXECUTION_STARTED,
        HERMAS_JOURNAL_DELIVERY_PREPARED,
        HERMAS_JOURNAL_DELIVERY_SENT,
        HERMAS_JOURNAL_ACTION_FAILED,
        HERMAS_JOURNAL_EXECUTION_FINISHED
    };
    for (size_t index = 0u;
         index < sizeof(expected) / sizeof(expected[0]); ++index) {
        hermas_journal_record record;
        if (hermas_journal_decode(
                memory.bytes + index * HERMAS_JOURNAL_RECORD_SIZE,
                HERMAS_JOURNAL_RECORD_SIZE, &record) !=
                HERMAS_JOURNAL_OK ||
            record.kind != expected[index]) {
            close(sockets[1]);
            return fail("durable transition order differs");
        }
    }
    close(sockets[1]);
    return 0;
}

typedef struct large_journal_memory {
    uint8_t bytes[32u * HERMAS_JOURNAL_RECORD_SIZE];
    size_t length;
} large_journal_memory;

static hermas_journal_result write_large_journal_memory(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    large_journal_memory *memory = context;
    if (record_size > sizeof(memory->bytes) - memory->length) {
        return HERMAS_JOURNAL_WRITE_ERROR;
    }
    memcpy(memory->bytes + memory->length, record, record_size);
    memory->length += record_size;
    return HERMAS_JOURNAL_OK;
}

static int test_bounded_all(
    hermas_daemon_loop *loop,
    const char *path) {
    size_t image_size = 0u;
    uint8_t *image = read_fixture(path, &image_size);
    if (image == NULL) {
        return fail("cannot read bounded-all fixture");
    }
    hermas_daemon_registry registry;
    if (hermas_daemon_registry_init(&registry, image, image_size) !=
        HERMAS_DAEMON_OK) {
        free(image);
        return fail("cannot initialize bounded-all registry");
    }
    int peers[4] = {-1, -1, -1, -1};
    for (size_t index = 0u; index < registry.action_count; ++index) {
        int sockets[2];
        if (index >= 4u ||
            socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
            hermas_daemon_registry_close(&registry);
            free(image);
            return fail("cannot create bounded-all Action sockets");
        }
        registry.actions[index].file_descriptor = sockets[0];
        registry.actions[index].registered_action_id =
            (uint16_t)(71u + index);
        peers[registry.actions[index].app_id - 1u] = sockets[1];
    }
    large_journal_memory memory;
    memset(&memory, 0, sizeof(memory));
    hermas_journal_writer writer;
    if (registry.action_count != 4u ||
        hermas_journal_writer_init(
            &writer, write_large_journal_memory, &memory, 1u) !=
            HERMAS_JOURNAL_OK ||
        hermas_daemon_loop_init(loop, &registry, image, image_size) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_attach_journal(loop, &writer, 12u) !=
            HERMAS_LOOP_OK ||
        hermas_daemon_loop_admit(loop, 301u, 3u, NULL, 0u) !=
            HERMAS_LOOP_OK) {
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("cannot admit bounded-all execution");
    }
    size_t progress = 0u;
    hermas_frame source;
    uint8_t integer[8] = {42u};
    if (hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK ||
        !receive_request(peers[0], &source) ||
        !send_success(
            peers[0], &source,
            action_success_type(image, source.app_id), integer,
            sizeof(integer)) ||
        hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK ||
        hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK) {
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("bounded-all source did not reach its Fork");
    }
    hermas_frame alpha;
    hermas_frame beta;
    if (!receive_request(peers[1], &alpha) ||
        !receive_request(peers[2], &beta) ||
        alpha.request_id == beta.request_id) {
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("bounded-all branches did not overlap");
    }
    hermas_journal_summary summary;
    if (hermas_journal_scan(
            memory.bytes, memory.length, NULL, NULL, &summary) !=
            HERMAS_JOURNAL_OK ||
        summary.interrupted_count != 1u ||
        summary.interrupted[0].open_delivery_count != 2u) {
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("parallel deliveries were not both durable");
    }
    uint8_t alpha_value[8] = {43u};
    uint8_t beta_value[8] = {44u};
    uint16_t beta_type =
        action_success_type(image, beta.app_id);
    uint16_t alpha_type =
        action_success_type(image, alpha.app_id);
    if (beta_type == 0u || alpha_type == 0u ||
        !send_success(
            peers[2], &beta, beta_type, beta_value,
            sizeof(beta_value)) ||
        !send_success(
            peers[1], &alpha, alpha_type, alpha_value,
            sizeof(alpha_value))) {
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("cannot return bounded-all branch results");
    }
    hermas_loop_result first_poll =
        hermas_daemon_loop_poll(loop, 1000, &progress);
    hermas_loop_result second_poll =
        first_poll == HERMAS_LOOP_OK
            ? hermas_daemon_loop_poll(loop, 1000, &progress)
            : first_poll;
    if (first_poll != HERMAS_LOOP_OK || second_poll != HERMAS_LOOP_OK) {
        fprintf(stderr, "test_loop: bounded-all poll results: %s, %s\n",
                hermas_loop_result_name(first_poll),
                hermas_loop_result_name(second_poll));
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("bounded-all branches did not join");
    }
    hermas_frame sink;
    uint8_t done = 1u;
    if (!receive_request(peers[3], &sink) ||
        !send_success(
            peers[3], &sink,
            action_success_type(image, sink.app_id), &done, 1u) ||
        hermas_daemon_loop_poll(loop, 1000, &progress) != HERMAS_LOOP_OK) {
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("bounded-all sink did not complete");
    }
    hermas_frame result;
    int valid =
        hermas_daemon_loop_result(loop, 301u, &result) == HERMAS_LOOP_OK &&
        result.outcome == HERMAS_OUTCOME_SUCCESS &&
        result.payload_length == 1u && result.payload[0] == 1u &&
        hermas_journal_scan(
            memory.bytes, memory.length, NULL, NULL, &summary) ==
            HERMAS_JOURNAL_OK &&
        summary.record_count == 14u && summary.interrupted_count == 0u &&
        hermas_daemon_loop_release(loop, 301u) == HERMAS_LOOP_OK &&
        hermas_daemon_loop_admit(loop, 302u, 3u, NULL, 0u) ==
            HERMAS_LOOP_OK;
    hermas_frame source_unknown;
    hermas_frame alpha_unknown;
    hermas_frame beta_unknown;
    if (valid) {
        valid =
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK &&
            receive_request(peers[0], &source_unknown) &&
            send_success(
                peers[0], &source_unknown,
                action_success_type(image, source_unknown.app_id), integer,
                sizeof(integer)) &&
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK &&
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK &&
            receive_request(peers[1], &alpha_unknown) &&
            receive_request(peers[2], &beta_unknown);
    }
    if (valid) {
        close(peers[2]);
        peers[2] = -1;
        valid =
            send_success(
                peers[1], &alpha_unknown,
                action_success_type(image, alpha_unknown.app_id),
                alpha_value, sizeof(alpha_value)) &&
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK &&
            hermas_daemon_loop_result(loop, 302u, &result) ==
                HERMAS_LOOP_OK &&
            result.outcome == HERMAS_OUTCOME_UNKNOWN &&
            hermas_journal_scan(
                memory.bytes, memory.length, NULL, NULL, &summary) ==
                HERMAS_JOURNAL_OK &&
            summary.record_count == 25u &&
            summary.interrupted_count == 0u &&
            hermas_daemon_loop_release(loop, 302u) == HERMAS_LOOP_OK;
    }
    if (valid) {
        valid =
            hermas_daemon_loop_admit(
                loop, 303u, 3u, NULL, 0u) == HERMAS_LOOP_OK &&
            hermas_daemon_loop_admit(
                loop, 304u, 3u, NULL, 0u) == HERMAS_LOOP_OK &&
            hermas_daemon_loop_admit(
                loop, 305u, 3u, NULL, 0u) ==
                HERMAS_LOOP_CAPACITY_EXHAUSTED &&
            hermas_daemon_loop_active(loop) ==
                HERMAS_DAEMON_MAX_GROUP_EXECUTIONS;
    }
    hermas_daemon_registry_close(&registry);
    free(image);
    return valid ? 0 : fail("bounded-all result or journal differs");
}

static int test_bounded_each(
    hermas_daemon_loop *loop,
    const char *path) {
    size_t image_size = 0u;
    uint8_t *image = read_fixture(path, &image_size);
    hermas_image_summary image_summary;
    if (image == NULL ||
        hermas_image_validate(image, image_size, &image_summary) !=
            HERMAS_IMAGE_OK) {
        free(image);
        return fail("cannot read bounded-each fixture");
    }
    hermas_daemon_registry registry;
    if (hermas_daemon_registry_init(&registry, image, image_size) !=
        HERMAS_DAEMON_OK) {
        free(image);
        return fail("cannot initialize bounded-each registry");
    }
    int peers[3] = {-1, -1, -1};
    for (size_t index = 0u; index < registry.action_count; ++index) {
        int sockets[2];
        if (registry.actions[index].app_id == 0u ||
            registry.actions[index].app_id > 3u ||
            socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
            hermas_daemon_registry_close(&registry);
            free(image);
            return fail("cannot create bounded-each Action sockets");
        }
        registry.actions[index].file_descriptor = sockets[0];
        registry.actions[index].registered_action_id =
            (uint16_t)(81u + index);
        peers[registry.actions[index].app_id - 1u] = sockets[1];
    }
    large_journal_memory memory;
    memset(&memory, 0, sizeof(memory));
    hermas_journal_writer writer;
    int valid =
        registry.action_count == 3u &&
        hermas_journal_writer_init(
            &writer, write_large_journal_memory, &memory, 1u) ==
            HERMAS_JOURNAL_OK &&
        hermas_daemon_loop_init(loop, &registry, image, image_size) ==
            HERMAS_LOOP_OK &&
        hermas_daemon_loop_attach_journal(loop, &writer, 13u) ==
            HERMAS_LOOP_OK &&
        hermas_daemon_loop_admit(
            loop, 401u, image_summary.input_type, NULL, 0u) ==
            HERMAS_LOOP_OK;
    const char *stage = "setup";
    size_t progress = 0u;
    hermas_frame source;
    uint8_t orders[40] = {0u};
    orders[0] = 4u;
    orders[8] = 10u;
    orders[16] = 20u;
    orders[24] = 30u;
    orders[32] = 40u;
    uint16_t list_type = action_success_type(image, 1u);
    if (valid) {
        stage = "source";
        valid =
            list_type != 0u &&
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK &&
            receive_request(peers[0], &source) &&
            send_success(
                peers[0], &source, list_type, orders,
                (uint32_t)sizeof(orders)) &&
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK &&
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK;
    }
    hermas_frame item_requests[4];
    memset(item_requests, 0, sizeof(item_requests));
    size_t delivered_items[4] = {0u};
    bool item_seen[4] = {false, false, false, false};
    uint16_t report_type = action_success_type(image, 2u);
    for (size_t delivered = 0u; valid && delivered < 3u; ++delivered) {
        stage = "item";
        hermas_frame *request = &item_requests[delivered];
        int received = receive_request_now(peers[1], request);
        size_t item = request->payload_length == 8u &&
                              request->payload[0] >= 10u &&
                              request->payload[0] <= 40u &&
                              request->payload[0] % 10u == 0u
                          ? (size_t)(request->payload[0] / 10u - 1u)
                          : 4u;
        valid = report_type != 0u && received &&
                request->payload_length == 8u &&
                item < 4u && !item_seen[item];
        if (!valid) {
            fprintf(
                stderr,
                "test_loop: each delivery %zu receive=%d type=%u length=%u "
                "value=%u\n",
                delivered, received, (unsigned)report_type,
                (unsigned)request->payload_length,
                request->payload_length == 0u ? 0u : request->payload[0]);
        }
        if (valid) {
            item_seen[item] = true;
            delivered_items[delivered] = item;
        }
        for (size_t prior = 0u; valid && prior < delivered; ++prior) {
            valid = item_requests[prior].request_id != request->request_id;
        }
    }
    const size_t completion_order[3] = {1u, 0u, 2u};
    for (size_t completed = 0u; valid && completed < 3u; ++completed) {
        size_t delivered = completion_order[completed];
        size_t item = delivered_items[delivered];
        uint8_t report[8] = {(uint8_t)(101u + item)};
        valid =
            send_success(
                peers[1], &item_requests[delivered], report_type,
                report, (uint32_t)sizeof(report));
    }
    if (valid) {
        valid = hermas_daemon_loop_poll(loop, 1000, &progress) ==
                    HERMAS_LOOP_OK &&
                hermas_daemon_loop_poll(loop, 1000, &progress) ==
                    HERMAS_LOOP_OK &&
                receive_request(peers[1], &item_requests[3]);
    }
    if (valid) {
        size_t item = item_requests[3].payload_length == 8u &&
                              item_requests[3].payload[0] >= 10u &&
                              item_requests[3].payload[0] <= 40u &&
                              item_requests[3].payload[0] % 10u == 0u
                          ? (size_t)(item_requests[3].payload[0] / 10u - 1u)
                          : 4u;
        uint8_t report[8] = {(uint8_t)(101u + item)};
        valid = item_requests[3].payload_length == 8u && item < 4u &&
                !item_seen[item];
        for (size_t prior = 0u; valid && prior < 3u; ++prior) {
            valid = item_requests[prior].request_id !=
                    item_requests[3].request_id;
        }
        if (valid) {
            item_seen[item] = true;
        }
        valid = valid &&
                send_success(
                    peers[1], &item_requests[3], report_type, report,
                    (uint32_t)sizeof(report)) &&
                hermas_daemon_loop_poll(loop, 1000, &progress) ==
                    HERMAS_LOOP_OK;
    }
    hermas_frame archive;
    uint8_t done = 1u;
    uint16_t done_type = action_success_type(image, 3u);
    if (valid) {
        stage = "archive";
        valid =
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK &&
            receive_request(peers[2], &archive) &&
            archive.payload_length == sizeof(orders) &&
            archive.payload[0] == 4u &&
            archive.payload[8] == 101u &&
            archive.payload[16] == 102u &&
            archive.payload[24] == 103u &&
            archive.payload[32] == 104u && done_type != 0u &&
            send_success(peers[2], &archive, done_type, &done, 1u) &&
            hermas_daemon_loop_poll(loop, 1000, &progress) ==
                HERMAS_LOOP_OK;
    }
    hermas_frame result;
    hermas_journal_summary summary;
    if (valid) {
        stage = "result";
        valid =
            hermas_daemon_loop_result(loop, 401u, &result) ==
                HERMAS_LOOP_OK &&
            result.outcome == HERMAS_OUTCOME_SUCCESS &&
            result.payload_length == 1u && result.payload[0] == 1u &&
            hermas_journal_scan(
                memory.bytes, memory.length, NULL, NULL, &summary) ==
                HERMAS_JOURNAL_OK &&
            summary.record_count == 20u &&
            summary.interrupted_count == 0u &&
            hermas_daemon_loop_release(loop, 401u) == HERMAS_LOOP_OK;
    }
    for (size_t index = 0u; index < 3u; ++index) {
        if (peers[index] >= 0) {
            close(peers[index]);
        }
    }
    hermas_daemon_registry_close(&registry);
    free(image);
    if (!valid) {
        fprintf(stderr, "test_loop: bounded-each failed during %s\n", stage);
    }
    return valid ? 0 : fail("bounded-each daemon execution differs");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        return fail(
            "expected sequential, parallel, and each graph-image fixtures");
    }
    size_t image_size = 0u;
    uint8_t *image = read_fixture(argv[1], &image_size);
    if (image == NULL) {
        return fail("cannot read fixture");
    }
    hermas_daemon_registry registry;
    if (hermas_daemon_registry_init(&registry, image, image_size) !=
        HERMAS_DAEMON_OK) {
        free(image);
        return fail("cannot initialize registry");
    }
    hermas_daemon_loop *loop = malloc(sizeof(*loop));
    if (loop == NULL) {
        hermas_daemon_registry_close(&registry);
        free(image);
        return fail("test cannot allocate loop fixture");
    }
    int result = test_not_sent(loop, &registry, image, image_size);
    if (result == 0) {
        result = test_unknown(loop, &registry, image, image_size);
    }
    if (result == 0) {
        result = test_single_flight(loop, &registry, image, image_size);
    }
    if (result == 0) {
        result = test_durable_delivery_facts(
            loop, &registry, image, image_size);
    }
    if (result == 0) {
        result = test_bounded_all(loop, argv[2]);
    }
    if (result == 0) {
        result = test_bounded_each(loop, argv[3]);
    }
    hermas_daemon_registry_close(&registry);
    free(loop);
    free(image);
    return result;
}
