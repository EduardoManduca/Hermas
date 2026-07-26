#include "hermas2/daemon.h"

#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TOKEN_STORE_CAPACITY \
    (HERMAS2_SAGA_MAX_STEPS * \
     (HERMAS2_COMPENSATION_HEADER_SIZE + 8u))
#define SAGA_STORE_CAPACITY \
    (32u * HERMAS2_SAGA_LOG_RECORD_SIZE)

typedef struct durable_probe {
    unsigned order;
    unsigned last_token_order;
    unsigned last_success_order;
    uint8_t tokens[TOKEN_STORE_CAPACITY];
    size_t token_bytes;
    uint8_t saga[SAGA_STORE_CAPACITY];
    size_t saga_bytes;
} durable_probe;

typedef struct app_channel {
    uint16_t app_id;
    int peer;
} app_channel;

typedef struct saga_route {
    uint16_t forward_node;
    uint16_t app_id;
    uint16_t action_id;
    uint16_t success_type;
} saga_route;

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
        probe->last_success_order = probe->order;
    }
    return HERMAS2_JOURNAL_OK;
}

static hermas2_compensation_result write_token(
    void *context,
    const uint8_t *record,
    size_t size) {
    durable_probe *probe = context;
    if (size > sizeof(probe->tokens) - probe->token_bytes) {
        return HERMAS2_COMPENSATION_WRITE_ERROR;
    }
    memcpy(probe->tokens + probe->token_bytes, record, size);
    probe->token_bytes += size;
    probe->last_token_order = ++probe->order;
    return HERMAS2_COMPENSATION_OK;
}

static hermas2_compensation_result lookup_token(
    void *context,
    hermas2_compensation_key key,
    hermas2_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found) {
    const durable_probe *probe = context;
    return hermas2_compensation_find(
        probe->tokens, probe->token_bytes, key, record,
        token, token_capacity, found);
}

static hermas2_saga_log_result write_saga(
    void *context,
    const uint8_t *record,
    size_t size) {
    durable_probe *probe = context;
    if (size > sizeof(probe->saga) - probe->saga_bytes) {
        return HERMAS2_SAGA_LOG_WRITE_ERROR;
    }
    memcpy(probe->saga + probe->saga_bytes, record, size);
    probe->saga_bytes += size;
    return HERMAS2_SAGA_LOG_OK;
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

static uint16_t edge_type(
    const uint8_t *image,
    uint16_t node,
    uint8_t source_kind) {
    uint16_t edge_count = read_u16(image, 32u);
    size_t edges = read_u32(image, 52u);
    for (uint16_t index = 0u; index < edge_count; ++index) {
        size_t offset = edges + (size_t)index * 16u;
        if (image[offset] == source_kind &&
            read_u16(image, offset + 4u) == node) {
            return read_u16(image, offset + 8u);
        }
    }
    return 0u;
}

static int read_routes(
    const uint8_t *image,
    saga_route routes[3]) {
    uint16_t region_count = read_u16(image, 68u);
    size_t regions = read_u32(image, 72u);
    size_t found = 0u;
    for (uint16_t index = 0u; index < region_count; ++index) {
        size_t offset = regions + (size_t)index * 16u;
        if (image[offset] != 3u) {
            continue;
        }
        uint16_t ordinal = read_u16(image, offset + 12u);
        if (ordinal == 0u || ordinal > 3u || index + 1u >= region_count) {
            return 0;
        }
        size_t outcome = offset + 16u;
        if (image[outcome] != 4u) {
            return 0;
        }
        routes[ordinal - 1u] = (saga_route){
            .forward_node = read_u16(image, offset + 2u),
            .app_id = read_u16(image, offset + 4u),
            .action_id = read_u16(image, offset + 6u),
            .success_type = read_u16(image, outcome + 4u)
        };
        ++found;
    }
    return found == 3u;
}

static int receive_invocation(
    hermas2_daemon_loop *loop,
    app_channel *channels,
    size_t channel_count,
    uint8_t *packet,
    hermas2_frame *invocation) {
    for (unsigned attempt = 0u; attempt < 8u; ++attempt) {
        size_t progress = 0u;
        if (hermas2_daemon_loop_poll(loop, 100, &progress) !=
            HERMAS2_LOOP_OK) {
            return 0;
        }
        struct pollfd items[HERMAS2_DAEMON_MAX_APPS];
        for (size_t index = 0u; index < channel_count; ++index) {
            items[index] = (struct pollfd){
                .fd = channels[index].peer,
                .events = POLLIN
            };
        }
        int ready = poll(items, channel_count, 0);
        if (ready < 0) {
            return 0;
        }
        for (size_t index = 0u; index < channel_count; ++index) {
            if ((items[index].revents & POLLIN) == 0) {
                continue;
            }
            ssize_t received = recv(
                channels[index].peer, packet,
                HERMAS2_PROTOCOL_MAX_PACKET_SIZE, 0);
            return received > 0 &&
                   hermas2_protocol_decode(
                       packet, (size_t)received, invocation) ==
                       HERMAS2_PROTOCOL_OK &&
                   invocation->kind == HERMAS2_FRAME_INVOKE &&
                   invocation->app_id == channels[index].app_id;
        }
    }
    return 0;
}

static int send_result(
    const app_channel *channels,
    size_t channel_count,
    const hermas2_frame *invocation,
    uint16_t outcome,
    uint16_t type,
    const uint8_t *payload,
    uint32_t payload_length,
    uint8_t *packet) {
    int peer = -1;
    for (size_t index = 0u; index < channel_count; ++index) {
        if (channels[index].app_id == invocation->app_id) {
            peer = channels[index].peer;
            break;
        }
    }
    hermas2_frame result = {
        .kind = HERMAS2_FRAME_RESULT,
        .execution_id = invocation->execution_id,
        .request_id = invocation->request_id,
        .app_id = invocation->app_id,
        .action_id = invocation->action_id,
        .source_type = type,
        .destination_type = type,
        .outcome = outcome,
        .payload = payload,
        .payload_length = payload_length
    };
    size_t packet_size = 0u;
    return peer >= 0 &&
           hermas2_protocol_encode(
               &result, packet, HERMAS2_PROTOCOL_MAX_PACKET_SIZE,
               &packet_size) == HERMAS2_PROTOCOL_OK &&
           send(peer, packet, packet_size, 0) == (ssize_t)packet_size;
}

static int load_image(
    const char *path,
    uint8_t *image,
    size_t capacity,
    size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 0;
    }
    long length = ftell(file);
    if (length <= 0 || (size_t)length > capacity ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    *size = (size_t)length;
    int loaded = fread(image, 1u, *size, file) == *size;
    fclose(file);
    return loaded;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    uint8_t image[4096];
    size_t image_size = 0u;
    saga_route routes[3];
    memset(routes, 0, sizeof(routes));
    if (!load_image(argv[1], image, sizeof(image), &image_size) ||
        !read_routes(image, routes)) {
        return 2;
    }

    hermas2_daemon_registry registry;
    if (hermas2_daemon_registry_init(
            &registry, image, image_size) != HERMAS2_DAEMON_OK) {
        fputs("registry initialization failed\n", stderr);
        return 1;
    }
    app_channel channels[HERMAS2_DAEMON_MAX_APPS];
    memset(channels, 0, sizeof(channels));
    for (size_t index = 0u; index < registry.app_count; ++index) {
        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
            fputs("socketpair creation failed\n", stderr);
            return 1;
        }
        registry.apps[index].file_descriptor = sockets[0];
        channels[index] = (app_channel){
            .app_id = registry.apps[index].app_id,
            .peer = sockets[1]
        };
    }

    durable_probe probe;
    memset(&probe, 0, sizeof(probe));
    hermas2_journal_writer journal;
    hermas2_compensation_writer compensation;
    hermas2_saga_log_writer saga_log;
    hermas2_daemon_loop loop;
    if (hermas2_journal_writer_init(
            &journal, write_journal, &probe, 1u) !=
            HERMAS2_JOURNAL_OK ||
        hermas2_compensation_writer_init(
            &compensation, write_token, &probe, 1u) !=
            HERMAS2_COMPENSATION_OK ||
        hermas2_saga_log_writer_init(
            &saga_log, write_saga, &probe, 1u) !=
            HERMAS2_SAGA_LOG_OK ||
        hermas2_daemon_loop_init(
            &loop, &registry, image, image_size) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_journal(&loop, &journal, 7u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_saga(
            &loop, &compensation, lookup_token, &probe,
            &saga_log) != HERMAS2_LOOP_OK) {
        fputs("loop durability attachment failed\n", stderr);
        return 1;
    }

    uint8_t input[8] = {3u};
    uint16_t input_type = read_u16(image, 22u);
    if (hermas2_daemon_loop_admit(
            &loop, 41u, input_type, input, sizeof(input)) !=
        HERMAS2_LOOP_OK) {
        fputs("execution admission failed\n", stderr);
        return 1;
    }
    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    hermas2_frame invocation;
    for (size_t ordinal = 0u; ordinal < 3u; ++ordinal) {
        if (!receive_invocation(
                &loop, channels, registry.app_count,
                packet, &invocation)) {
            fputs("forward invocation was not delivered\n", stderr);
            return 1;
        }
        uint16_t node = routes[ordinal].forward_node;
        uint16_t outcome = ordinal < 2u
                               ? HERMAS2_OUTCOME_SUCCESS
                               : HERMAS2_OUTCOME_APP_ERROR;
        uint16_t type = edge_type(
            image, node, ordinal < 2u ? 1u : 2u);
        uint8_t token[8] = {
            (uint8_t)(11u * (ordinal + 1u))
        };
        if (type == 0u ||
            !send_result(
                channels, registry.app_count, &invocation,
                outcome, type, token, ordinal < 2u ? 8u : 0u,
                packet)) {
            fputs("forward result could not be sent\n", stderr);
            return 1;
        }
    }

    hermas2_frame early_result;
    size_t progress = 0u;
    if (hermas2_daemon_loop_poll(&loop, 100, &progress) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_result(
            &loop, 41u, &early_result) !=
            HERMAS2_LOOP_EXECUTION_ACTIVE) {
        fputs("original failure escaped before compensation\n", stderr);
        return 1;
    }

    for (size_t reverse = 0u; reverse < 2u; ++reverse) {
        size_t ordinal = 1u - reverse;
        if (!receive_invocation(
                &loop, channels, registry.app_count,
                packet, &invocation) ||
            invocation.app_id != routes[ordinal].app_id ||
            invocation.action_id != routes[ordinal].action_id ||
            invocation.payload_length != 8u ||
            invocation.payload[0] != (uint8_t)(11u * (ordinal + 1u)) ||
            !send_result(
                channels, registry.app_count, &invocation,
                HERMAS2_OUTCOME_SUCCESS, routes[ordinal].success_type,
                NULL, 0u, packet)) {
            fputs("reverse compensation order failed\n", stderr);
            return 1;
        }
    }
    if (hermas2_daemon_loop_poll(&loop, 100, &progress) !=
        HERMAS2_LOOP_OK) {
        return 1;
    }

    hermas2_frame final_result;
    hermas2_saga_log_summary saga_summary;
    hermas2_compensation_summary token_summary;
    int ok =
        hermas2_daemon_loop_result(&loop, 41u, &final_result) ==
            HERMAS2_LOOP_OK &&
        final_result.outcome == HERMAS2_OUTCOME_APP_ERROR &&
        final_result.payload_length == 0u &&
        probe.last_token_order != 0u &&
        probe.last_success_order > probe.last_token_order &&
        hermas2_compensation_scan(
            probe.tokens, probe.token_bytes, NULL, NULL,
            &token_summary) == HERMAS2_COMPENSATION_OK &&
        token_summary.record_count == 2u &&
        hermas2_saga_log_scan(
            probe.saga, probe.saga_bytes, &saga_summary) ==
            HERMAS2_SAGA_LOG_OK &&
        saga_summary.active_count == 0u &&
        saga_summary.record_count == 8u &&
        hermas2_daemon_loop_release(&loop, 41u) ==
            HERMAS2_LOOP_OK;
    for (size_t index = 0u; index < registry.app_count; ++index) {
        close(channels[index].peer);
    }
    hermas2_daemon_registry_close(&registry);
    if (!ok) {
        fputs("live saga compensation failed\n", stderr);
        return 1;
    }
    puts("live saga compensation passed");
    return 0;
}
