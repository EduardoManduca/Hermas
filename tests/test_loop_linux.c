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
        summary.interrupted[0].delivery_was_sent != 1u) {
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

static int test_group_graphs_fail_closed(
    hermas_daemon_loop *loop,
    hermas_daemon_registry *registry,
    const char *path) {
    size_t image_size = 0u;
    uint8_t *image = read_fixture(path, &image_size);
    if (image == NULL) {
        return fail("cannot read bounded-flow fixture");
    }
    int rejected =
        hermas_daemon_loop_init(loop, registry, image, image_size) ==
        HERMAS_LOOP_UNSUPPORTED_GRAPH;
    free(image);
    return rejected
        ? 0
        : fail("daemon accepted a graph requiring bounded-flow execution");
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
        result = test_group_graphs_fail_closed(
            loop, &registry, argv[2]);
    }
    if (result == 0) {
        result = test_group_graphs_fail_closed(
            loop, &registry, argv[3]);
    }
    hermas_daemon_registry_close(&registry);
    free(loop);
    free(image);
    return result;
}
