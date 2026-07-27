#define _POSIX_C_SOURCE 200809L

#include "hermas2/daemon.h"
#include "hermas2/result_linux.h"
#include "hermas2/saga_linux.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TOKEN_STORE_CAPACITY \
    (HERMAS2_SAGA_MAX_STEPS * \
     (HERMAS2_COMPENSATION_HEADER_SIZE + 8u))
#define SAGA_STORE_CAPACITY \
    (32u * HERMAS2_SAGA_LOG_RECORD_SIZE)
#define JOURNAL_STORE_CAPACITY \
    (64u * HERMAS2_JOURNAL_RECORD_SIZE)
#define RESULT_STORE_CAPACITY \
    (HERMAS2_DAEMON_MAX_EXECUTIONS * \
     (HERMAS2_RESULT_HEADER_SIZE + 8u))

typedef struct durable_probe {
    unsigned order;
    unsigned last_token_order;
    unsigned last_success_order;
    unsigned last_result_order;
    unsigned last_finished_order;
    uint8_t journal[JOURNAL_STORE_CAPACITY];
    size_t journal_bytes;
    uint8_t tokens[TOKEN_STORE_CAPACITY];
    size_t token_bytes;
    uint8_t saga[SAGA_STORE_CAPACITY];
    size_t saga_bytes;
    uint8_t results[RESULT_STORE_CAPACITY];
    size_t result_bytes;
} durable_probe;

typedef struct app_channel {
    uint16_t app_id;
    uint16_t action_id;
    int peer;
} app_channel;

typedef struct saga_route {
    uint16_t forward_node;
    uint16_t app_id;
    uint16_t action_id;
    uint16_t success_type;
} saga_route;

typedef struct durable_files {
    char directory[64];
    char journal_path[96];
    char token_path[96];
    char saga_path[96];
    char result_path[96];
    hermas2_journal_file journal;
    hermas2_compensation_file compensation;
    hermas2_saga_log_file saga;
    hermas2_result_file results;
} durable_files;

static hermas2_journal_result write_journal(
    void *context,
    const uint8_t *record,
    size_t size) {
    durable_probe *probe = context;
    hermas2_journal_record decoded;
    if (size > sizeof(probe->journal) - probe->journal_bytes ||
        hermas2_journal_decode(record, size, &decoded) !=
        HERMAS2_JOURNAL_OK) {
        return HERMAS2_JOURNAL_WRITE_ERROR;
    }
    memcpy(probe->journal + probe->journal_bytes, record, size);
    probe->journal_bytes += size;
    ++probe->order;
    if (decoded.kind == HERMAS2_JOURNAL_ACTION_SUCCEEDED) {
        probe->last_success_order = probe->order;
    } else if (decoded.kind == HERMAS2_JOURNAL_EXECUTION_FINISHED) {
        probe->last_finished_order = probe->order;
    }
    return HERMAS2_JOURNAL_OK;
}

static hermas2_result_store_result write_result_value(
    void *context,
    const uint8_t *record,
    size_t size) {
    durable_probe *probe = context;
    if (size > sizeof(probe->results) - probe->result_bytes) {
        return HERMAS2_RESULT_STORE_WRITE_ERROR;
    }
    memcpy(probe->results + probe->result_bytes, record, size);
    probe->result_bytes += size;
    probe->last_result_order = ++probe->order;
    return HERMAS2_RESULT_STORE_OK;
}

static hermas2_result_store_result lookup_result_value(
    void *context,
    hermas2_result_key key,
    hermas2_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found) {
    const durable_probe *probe = context;
    return hermas2_result_find(
        probe->results, probe->result_bytes, key, record,
        value, value_capacity, found);
}

static hermas2_result_store_result lookup_missing_result(
    void *context,
    hermas2_result_key key,
    hermas2_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found) {
    (void)context;
    (void)key;
    (void)record;
    (void)value;
    (void)value_capacity;
    *found = 0;
    return HERMAS2_RESULT_STORE_OK;
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
        struct pollfd items[HERMAS2_DAEMON_MAX_ACTIONS];
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
                   invocation->app_id == channels[index].app_id &&
                   invocation->action_id == channels[index].action_id;
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
        if (channels[index].app_id == invocation->app_id &&
            channels[index].action_id == invocation->action_id) {
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

static int drive_forward_failure(
    hermas2_daemon_loop *loop,
    uint64_t execution_id,
    const uint8_t *image,
    const saga_route routes[3],
    app_channel *channels,
    size_t channel_count,
    uint8_t *packet) {
    uint8_t input[8] = {3u};
    if (hermas2_daemon_loop_admit(
            loop, execution_id, read_u16(image, 22u),
            input, sizeof(input)) != HERMAS2_LOOP_OK) {
        return 0;
    }
    for (size_t ordinal = 0u; ordinal < 3u; ++ordinal) {
        hermas2_frame invocation;
        if (!receive_invocation(
                loop, channels, channel_count, packet,
                &invocation)) {
            return 0;
        }
        uint16_t outcome = ordinal < 2u
                               ? HERMAS2_OUTCOME_SUCCESS
                               : HERMAS2_OUTCOME_APP_ERROR;
        uint16_t type = edge_type(
            image, routes[ordinal].forward_node,
            ordinal < 2u ? 1u : 2u);
        uint8_t token[8] = {
            (uint8_t)(11u * (ordinal + 1u))
        };
        if (type == 0u ||
            !send_result(
                channels, channel_count, &invocation,
                outcome, type, token, ordinal < 2u ? 8u : 0u,
                packet)) {
            return 0;
        }
    }
    return 1;
}

static int open_durable_files(
    durable_files *files,
    const durable_probe *probe) {
    memset(files, 0, sizeof(*files));
    memcpy(
        files->directory, "/tmp/hermas2-saga-XXXXXX",
        sizeof("/tmp/hermas2-saga-XXXXXX"));
    if (mkdtemp(files->directory) == NULL ||
        snprintf(
            files->journal_path, sizeof(files->journal_path),
            "%s/execution.h2journal", files->directory) <= 0 ||
        snprintf(
            files->token_path, sizeof(files->token_path),
            "%s/tokens.h2comp", files->directory) <= 0 ||
        snprintf(
            files->saga_path, sizeof(files->saga_path),
            "%s/attempts.h2saga", files->directory) <= 0 ||
        snprintf(
            files->result_path, sizeof(files->result_path),
            "%s/results.h2result", files->directory) <= 0) {
        return 0;
    }
    hermas2_journal_summary journal_summary;
    hermas2_compensation_summary token_summary;
    hermas2_saga_log_summary saga_summary;
    hermas2_result_summary result_summary;
    if (hermas2_journal_file_open(
            &files->journal, files->journal_path,
            &journal_summary) != HERMAS2_JOURNAL_OK ||
        hermas2_compensation_file_open(
            &files->compensation, files->token_path,
            &token_summary) != HERMAS2_COMPENSATION_OK ||
        hermas2_saga_log_file_open(
            &files->saga, files->saga_path,
            &saga_summary) != HERMAS2_SAGA_LOG_OK ||
        hermas2_result_file_open(
            &files->results, files->result_path,
            &result_summary) != HERMAS2_RESULT_STORE_OK) {
        return 0;
    }
    size_t journal_count =
        probe->journal_bytes / HERMAS2_JOURNAL_RECORD_SIZE;
    for (size_t index = 0u; index < journal_count; ++index) {
        hermas2_journal_record record;
        if (hermas2_journal_decode(
                probe->journal +
                    index * HERMAS2_JOURNAL_RECORD_SIZE,
                HERMAS2_JOURNAL_RECORD_SIZE, &record) !=
                HERMAS2_JOURNAL_OK ||
            hermas2_journal_writer_append(
                &files->journal.writer, record) !=
                HERMAS2_JOURNAL_OK) {
            return 0;
        }
    }
    size_t token_offset = 0u;
    uint8_t scratch[
        HERMAS2_COMPENSATION_HEADER_SIZE +
        HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
    while (token_offset < probe->token_bytes) {
        hermas2_compensation_record record;
        size_t record_size = 0u;
        if (hermas2_compensation_decode(
                probe->tokens + token_offset,
                probe->token_bytes - token_offset,
                &record, &record_size) != HERMAS2_COMPENSATION_OK ||
            hermas2_compensation_writer_append(
                &files->compensation.writer, record,
                scratch, sizeof(scratch)) !=
                HERMAS2_COMPENSATION_OK) {
            return 0;
        }
        token_offset += record_size;
    }
    size_t result_offset = 0u;
    while (result_offset < probe->result_bytes) {
        hermas2_result_record record;
        size_t record_size = 0u;
        if (hermas2_result_decode(
                probe->results + result_offset,
                probe->result_bytes - result_offset,
                &record, &record_size) !=
                HERMAS2_RESULT_STORE_OK ||
            hermas2_result_writer_append(
                &files->results.writer, record,
                scratch, sizeof(scratch)) !=
                HERMAS2_RESULT_STORE_OK) {
            return 0;
        }
        result_offset += record_size;
    }
    size_t saga_count =
        probe->saga_bytes / HERMAS2_SAGA_LOG_RECORD_SIZE;
    for (size_t index = 0u; index < saga_count; ++index) {
        hermas2_saga_log_record record;
        if (hermas2_saga_log_decode(
                probe->saga +
                    index * HERMAS2_SAGA_LOG_RECORD_SIZE,
                HERMAS2_SAGA_LOG_RECORD_SIZE, &record) !=
                HERMAS2_SAGA_LOG_OK ||
            hermas2_saga_log_writer_append(
                &files->saga.writer, record) !=
                HERMAS2_SAGA_LOG_OK) {
            return 0;
        }
    }
    return 1;
}

static void close_durable_files(durable_files *files) {
    hermas2_result_file_close(&files->results);
    hermas2_saga_log_file_close(&files->saga);
    hermas2_compensation_file_close(&files->compensation);
    hermas2_journal_file_close(&files->journal);
    (void)unlink(files->saga_path);
    (void)unlink(files->token_path);
    (void)unlink(files->journal_path);
    (void)unlink(files->result_path);
    (void)rmdir(files->directory);
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
    app_channel channels[HERMAS2_DAEMON_MAX_ACTIONS];
    memset(channels, 0, sizeof(channels));
    for (size_t index = 0u; index < registry.action_count; ++index) {
        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
            fputs("socketpair creation failed\n", stderr);
            return 1;
        }
        registry.actions[index].file_descriptor = sockets[0];
        channels[index] = (app_channel){
            .app_id = registry.actions[index].app_id,
            .action_id = registry.actions[index].action_id,
            .peer = sockets[1]
        };
    }

    durable_probe probe;
    memset(&probe, 0, sizeof(probe));
    hermas2_journal_writer journal;
    hermas2_compensation_writer compensation;
    hermas2_saga_log_writer saga_log;
    hermas2_result_writer results;
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
        hermas2_result_writer_init(
            &results, write_result_value, &probe, 1u) !=
            HERMAS2_RESULT_STORE_OK ||
        hermas2_daemon_loop_init(
            &loop, &registry, image, image_size) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_journal(&loop, &journal, 7u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_results(
            &loop, &results, lookup_result_value, &probe) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_saga(
            &loop, &compensation, lookup_token, &probe,
            &saga_log) != HERMAS2_LOOP_OK) {
        fputs("loop durability attachment failed\n", stderr);
        return 1;
    }

    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    hermas2_frame invocation;
    if (!drive_forward_failure(
            &loop, 41u, image, routes, channels,
            registry.action_count, packet)) {
        fputs("forward failure fixture failed\n", stderr);
        return 1;
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
                &loop, channels, registry.action_count,
                packet, &invocation) ||
            invocation.app_id != routes[ordinal].app_id ||
            invocation.action_id != routes[ordinal].action_id ||
            invocation.payload_length != 8u ||
            invocation.payload[0] != (uint8_t)(11u * (ordinal + 1u)) ||
            !send_result(
                channels, registry.action_count, &invocation,
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
        probe.last_result_order != 0u &&
        probe.last_finished_order > probe.last_result_order &&
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
    if (!ok) {
        fputs("live saga compensation failed\n", stderr);
        return 1;
    }

    if (!drive_forward_failure(
            &loop, 42u, image, routes, channels,
            registry.action_count, packet) ||
        hermas2_daemon_loop_poll(&loop, 100, &progress) !=
            HERMAS2_LOOP_OK ||
        !receive_invocation(
            &loop, channels, registry.action_count,
            packet, &invocation) ||
        invocation.app_id != routes[1].app_id ||
        invocation.action_id != routes[1].action_id ||
        !send_result(
            channels, registry.action_count, &invocation,
            HERMAS2_OUTCOME_SUCCESS, routes[1].success_type,
            NULL, 0u, packet) ||
        hermas2_daemon_loop_poll(&loop, 100, &progress) !=
            HERMAS2_LOOP_OK) {
        fputs("pre-restart compensation failed\n", stderr);
        return 1;
    }

    hermas2_saga_execution recovered;
    durable_files files;
    if (!open_durable_files(&files, &probe) ||
        hermas2_saga_recover_files(
            &recovered, image, image_size,
            &files.journal, &files.compensation,
            &files.saga, 42u, 7u) !=
            HERMAS2_SAGA_OK ||
        recovered.state != HERMAS2_SAGA_READY ||
        recovered.completed_steps != 2u ||
        recovered.remaining != 1u) {
        fputs("durable saga recovery failed\n", stderr);
        return 1;
    }

    if (hermas2_daemon_loop_init(
            &loop, &registry, image, image_size) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_journal(
            &loop, &files.journal.writer, 7u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_results(
            &loop, &files.results.writer,
            lookup_missing_result, NULL) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_saga(
            &loop, &files.compensation.writer,
            hermas2_compensation_file_lookup,
            &files.compensation, &files.saga.writer) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_resume_saga(
            &loop, &recovered) != HERMAS2_LOOP_RESULT_ERROR) {
        fputs("missing configured terminal result was accepted\n", stderr);
        return 1;
    }

    if (hermas2_daemon_loop_init(
            &loop, &registry, image, image_size) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_journal(
            &loop, &files.journal.writer, 7u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_results(
            &loop, &files.results.writer,
            hermas2_result_file_lookup,
            &files.results) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_saga(
            &loop, &files.compensation.writer,
            hermas2_compensation_file_lookup,
            &files.compensation, &files.saga.writer) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_resume_saga(
            &loop, &recovered) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_result(
            &loop, 42u, &early_result) !=
            HERMAS2_LOOP_EXECUTION_ACTIVE ||
        !receive_invocation(
            &loop, channels, registry.action_count,
            packet, &invocation) ||
        invocation.app_id != routes[0].app_id ||
        invocation.action_id != routes[0].action_id ||
        invocation.payload_length != 8u ||
        invocation.payload[0] != 11u ||
        !send_result(
            channels, registry.action_count, &invocation,
            HERMAS2_OUTCOME_SUCCESS, routes[0].success_type,
            NULL, 0u, packet) ||
        hermas2_daemon_loop_poll(
            &loop, 100, &progress) != HERMAS2_LOOP_OK) {
        fputs("resumed saga transport failed\n", stderr);
        return 1;
    }

    hermas2_frame restarted_result;
    ok =
        hermas2_daemon_loop_result(
            &loop, 42u, &restarted_result) ==
            HERMAS2_LOOP_OK &&
        restarted_result.outcome == HERMAS2_OUTCOME_APP_ERROR &&
        restarted_result.payload_length == 0u &&
        hermas2_saga_log_file_scan(
            &files.saga, &saga_summary) ==
            HERMAS2_SAGA_LOG_OK &&
        saga_summary.active_count == 0u &&
        saga_summary.record_count == 16u &&
        files.compensation.writer.next_sequence == 5u &&
        hermas2_daemon_loop_release(&loop, 42u) ==
            HERMAS2_LOOP_OK;
    if (!ok) {
        fputs("resumed saga completion failed\n", stderr);
        return 1;
    }

    if (hermas2_daemon_loop_init(
            &loop, &registry, image, image_size) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_journal(
            &loop, &files.journal.writer, 7u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_results(
            &loop, &files.results.writer,
            hermas2_result_file_lookup,
            &files.results) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_saga(
            &loop, &files.compensation.writer,
            hermas2_compensation_file_lookup,
            &files.compensation, &files.saga.writer) !=
            HERMAS2_LOOP_OK ||
        !drive_forward_failure(
            &loop, 43u, image, routes, channels,
            registry.action_count, packet) ||
        hermas2_daemon_loop_poll(
            &loop, 100, &progress) != HERMAS2_LOOP_OK ||
        !receive_invocation(
            &loop, channels, registry.action_count,
            packet, &invocation)) {
        fputs("uncertain delivery fixture failed\n", stderr);
        return 1;
    }
    hermas2_saga_result reconciled;
    if ((reconciled = hermas2_saga_recover_files(
             &recovered, image, image_size,
             &files.journal, &files.compensation,
             &files.saga, 43u, 7u)) !=
            HERMAS2_SAGA_UNSAFE_HISTORY ||
        recovered.state != HERMAS2_SAGA_BLOCKED ||
        recovered.compensation_outcome !=
            HERMAS2_OUTCOME_UNKNOWN) {
        fputs("uncertain compensation was not blocked\n", stderr);
        return 1;
    }
    if (hermas2_daemon_loop_init(
            &loop, &registry, image, image_size) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_journal(
            &loop, &files.journal.writer, 7u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_results(
            &loop, &files.results.writer,
            hermas2_result_file_lookup,
            &files.results) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_attach_saga(
            &loop, &files.compensation.writer,
            hermas2_compensation_file_lookup,
            &files.compensation, &files.saga.writer) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_resume_saga(
            &loop, &recovered) !=
            HERMAS2_LOOP_INVALID_ARGUMENT) {
        fputs("uncertain compensation was replayable\n", stderr);
        return 1;
    }
    for (size_t index = 0u; index < registry.action_count; ++index) {
        close(channels[index].peer);
    }
    hermas2_daemon_registry_close(&registry);
    close_durable_files(&files);
    puts("live, restarted, and uncertain saga compensation passed");
    return 0;
}
