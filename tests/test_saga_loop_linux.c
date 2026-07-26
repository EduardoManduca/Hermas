#include "hermas2/daemon.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct durable_probe {
    unsigned order;
    unsigned token_order;
    unsigned success_order;
    uint8_t token_record[
        HERMAS2_COMPENSATION_HEADER_SIZE +
        HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
    size_t token_size;
} durable_probe;

static hermas2_journal_result write_journal(
    void *context,
    const uint8_t *record,
    size_t size) {
    durable_probe *probe = context;
    hermas2_journal_record decoded;
    if (hermas2_journal_decode(record, size, &decoded) !=
        HERMAS2_JOURNAL_OK) {
        return HERMAS2_JOURNAL_WRITE_ERROR;
    }
    ++probe->order;
    if (decoded.kind == HERMAS2_JOURNAL_ACTION_SUCCEEDED) {
        probe->success_order = probe->order;
    }
    return HERMAS2_JOURNAL_OK;
}

static hermas2_compensation_result write_token(
    void *context,
    const uint8_t *record,
    size_t size) {
    durable_probe *probe = context;
    if (size > sizeof(probe->token_record)) {
        return HERMAS2_COMPENSATION_WRITE_ERROR;
    }
    memcpy(probe->token_record, record, size);
    probe->token_size = size;
    probe->token_order = ++probe->order;
    return HERMAS2_COMPENSATION_OK;
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

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0 ||
        length > 4096) {
        fclose(file);
        return 2;
    }
    uint8_t image[4096];
    if (fread(image, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        return 2;
    }
    fclose(file);

    hermas2_daemon_registry registry;
    if (hermas2_daemon_registry_init(
            &registry, image, (size_t)length) != HERMAS2_DAEMON_OK) {
        return 1;
    }
    size_t edges = read_u32(image, 52u);
    uint16_t root_node = read_u16(image, edges + 6u);
    size_t nodes = read_u32(image, 48u);
    uint16_t first_app =
        read_u16(image, nodes + ((size_t)root_node - 1u) * 8u + 4u);
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
        return 1;
    }
    for (size_t index = 0u; index < registry.app_count; ++index) {
        registry.apps[index].file_descriptor =
            registry.apps[index].app_id == first_app ? sockets[0] : -1;
    }

    durable_probe probe;
    memset(&probe, 0, sizeof(probe));
    hermas2_journal_writer journal;
    hermas2_compensation_writer compensation;
    hermas2_daemon_loop loop;
    if (hermas2_journal_writer_init(
            &journal, write_journal, &probe, 1u) !=
            HERMAS2_JOURNAL_OK ||
        hermas2_compensation_writer_init(
            &compensation, write_token, &probe, 1u) !=
            HERMAS2_COMPENSATION_OK ||
        hermas2_daemon_loop_init(
            &loop, &registry, image, (size_t)length) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_journal(&loop, &journal, 7u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_compensation(
            &loop, &compensation) != HERMAS2_LOOP_OK) {
        close(sockets[0]);
        close(sockets[1]);
        return 1;
    }
    uint8_t input[8] = {3u};
    uint16_t input_type = read_u16(image, 22u);
    if (hermas2_daemon_loop_admit(
            &loop, 41u, input_type, input, sizeof(input)) !=
        HERMAS2_LOOP_OK) {
        return 1;
    }
    size_t progress = 0u;
    if (hermas2_daemon_loop_poll(&loop, 100, &progress) !=
        HERMAS2_LOOP_OK) {
        return 1;
    }
    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    ssize_t received = recv(sockets[1], packet, sizeof(packet), 0);
    hermas2_frame invocation;
    if (received <= 0 ||
        hermas2_protocol_decode(
            packet, (size_t)received, &invocation) !=
            HERMAS2_PROTOCOL_OK) {
        return 1;
    }
    uint16_t result_type = 0u;
    uint16_t edge_count = read_u16(image, 32u);
    for (uint16_t index = 0u; index < edge_count; ++index) {
        size_t offset = edges + (size_t)index * 16u;
        if (image[offset] == 1u &&
            read_u16(image, offset + 4u) == root_node) {
            result_type = read_u16(image, offset + 8u);
            break;
        }
    }
    uint8_t token[8] = {77u};
    hermas2_frame result = {
        .kind = HERMAS2_FRAME_RESULT,
        .execution_id = invocation.execution_id,
        .request_id = invocation.request_id,
        .app_id = invocation.app_id,
        .action_id = invocation.action_id,
        .source_type = result_type,
        .destination_type = result_type,
        .outcome = HERMAS2_OUTCOME_SUCCESS,
        .payload = token,
        .payload_length = sizeof(token)
    };
    size_t packet_size = 0u;
    if (hermas2_protocol_encode(
            &result, packet, sizeof(packet), &packet_size) !=
            HERMAS2_PROTOCOL_OK ||
        send(sockets[1], packet, packet_size, 0) !=
            (ssize_t)packet_size ||
        hermas2_daemon_loop_poll(&loop, 100, &progress) !=
            HERMAS2_LOOP_OK) {
        return 1;
    }
    hermas2_compensation_record decoded;
    size_t decoded_size = 0u;
    int ok =
        probe.token_order != 0u &&
        probe.success_order > probe.token_order &&
        hermas2_compensation_decode(
            probe.token_record, probe.token_size,
            &decoded, &decoded_size) == HERMAS2_COMPENSATION_OK &&
        decoded.key.execution_id == 41u &&
        decoded.key.node_id == root_node &&
        decoded.token_length == sizeof(token) &&
        decoded.token[0] == 77u;
    close(sockets[1]);
    hermas2_daemon_registry_close(&registry);
    if (!ok) {
        fputs("saga loop token ordering failed\n", stderr);
        return 1;
    }
    puts("saga loop token ordering passed");
    return 0;
}
