#define _POSIX_C_SOURCE 200809L

#include "hermas/host_linux.h"
#include "hermas/image.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct saga_route {
    uint16_t forward_node;
    uint16_t forward_app;
    uint16_t forward_action;
    uint16_t compensation_app;
    uint16_t compensation_action;
    uint16_t token_source_type;
    uint16_t token_destination_type;
    uint16_t success_type;
    uint16_t failure_source_type;
    uint16_t failure_destination_type;
} saga_route;

typedef struct app_channel {
    uint16_t app_id;
    uint16_t action_id;
    int peer;
} app_channel;

typedef struct fixture_paths {
    char directory[64];
    char image[96];
    char app_socket[96];
    char control_socket[96];
    char journal[96];
    char compensation[96];
    char results[96];
    char saga[96];
} fixture_paths;

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

static int load_image(
    const char *path,
    uint8_t *image,
    size_t capacity,
    size_t *image_size) {
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
    *image_size = (size_t)length;
    int loaded = fread(image, 1u, *image_size, file) == *image_size;
    fclose(file);
    return loaded;
}

static int read_routes(
    const uint8_t *image,
    saga_route routes[3]) {
    uint16_t region_count = read_u16(
        image, HERMAS_IMAGE_HEADER_REGION_COUNT_OFFSET);
    size_t regions = read_u32(
        image, HERMAS_IMAGE_HEADER_REGIONS_OFFSET);
    size_t nodes = read_u32(
        image, HERMAS_IMAGE_HEADER_NODES_OFFSET);
    size_t found = 0u;
    for (uint16_t index = 0u; index < region_count; ++index) {
        size_t offset =
            regions + (size_t)index *
                          HERMAS_IMAGE_REGION_RECORD_SIZE;
        if (image[offset] != 3u) {
            continue;
        }
        uint16_t ordinal = read_u16(image, offset + 12u);
        if (ordinal == 0u || ordinal > 3u ||
            index + 1u >= region_count ||
            image[offset + HERMAS_IMAGE_REGION_RECORD_SIZE] != 4u) {
            return 0;
        }
        uint16_t forward_node = read_u16(image, offset + 2u);
        size_t node =
            nodes + ((size_t)forward_node - 1u) *
                        HERMAS_IMAGE_NODE_RECORD_SIZE;
        routes[ordinal - 1u] = (saga_route){
            .forward_node = forward_node,
            .forward_app = read_u16(image, node + 4u),
            .forward_action = read_u16(image, node + 2u),
            .compensation_app = read_u16(image, offset + 4u),
            .compensation_action = read_u16(image, offset + 6u),
            .token_source_type = read_u16(image, offset + 8u),
            .token_destination_type = read_u16(image, offset + 10u),
            .success_type = read_u16(image, offset + 20u)
        };
        ++found;
    }
    uint16_t edge_count = read_u16(
        image, HERMAS_IMAGE_HEADER_EDGE_COUNT_OFFSET);
    size_t edges = read_u32(
        image, HERMAS_IMAGE_HEADER_EDGES_OFFSET);
    for (uint16_t index = 0u; index < edge_count; ++index) {
        size_t edge =
            edges + (size_t)index *
                        HERMAS_IMAGE_EDGE_RECORD_SIZE;
        if (image[edge] == 2u &&
            read_u16(image, edge + 4u) ==
                routes[2].forward_node) {
            routes[2].failure_source_type =
                read_u16(image, edge + 8u);
            routes[2].failure_destination_type =
                read_u16(image, edge + 10u);
        }
    }
    return found == 3u &&
           routes[2].failure_source_type != 0u &&
           routes[2].failure_destination_type != 0u;
}

static int make_paths(fixture_paths *paths) {
    memset(paths, 0, sizeof(*paths));
    memcpy(
        paths->directory, "/tmp/hermas-host-XXXXXX",
        sizeof("/tmp/hermas-host-XXXXXX"));
    if (mkdtemp(paths->directory) == NULL) {
        return 0;
    }
    return snprintf(
               paths->image, sizeof(paths->image),
               "%s/workflow.hgi", paths->directory) > 0 &&
           snprintf(
               paths->app_socket, sizeof(paths->app_socket),
               "%s/apps.sock", paths->directory) > 0 &&
           snprintf(
               paths->control_socket, sizeof(paths->control_socket),
               "%s/control.sock", paths->directory) > 0 &&
           snprintf(
               paths->journal, sizeof(paths->journal),
               "%s/journal.hj", paths->directory) > 0 &&
           snprintf(
               paths->compensation, sizeof(paths->compensation),
               "%s/compensation.hc", paths->directory) > 0 &&
           snprintf(
               paths->results, sizeof(paths->results),
               "%s/results.hr", paths->directory) > 0 &&
           snprintf(
               paths->saga, sizeof(paths->saga),
               "%s/saga.hs", paths->directory) > 0;
}

static void remove_fixture(const fixture_paths *paths) {
    (void)unlink(paths->image);
    (void)unlink(paths->app_socket);
    (void)unlink(paths->control_socket);
    (void)unlink(paths->journal);
    (void)unlink(paths->compensation);
    (void)unlink(paths->results);
    (void)unlink(paths->saga);
    (void)rmdir(paths->directory);
}

static int write_secure_image(
    const fixture_paths *paths,
    const uint8_t *image,
    size_t image_size) {
    FILE *file = fopen(paths->image, "wb");
    if (file == NULL) {
        return 0;
    }
    int written = fwrite(image, 1u, image_size, file) == image_size &&
                  fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) {
        written = 0;
    }
    return written;
}

static hermas_journal_record journal_action(
    hermas_journal_kind kind,
    uint16_t outcome,
    uint64_t execution_id,
    uint64_t request_id,
    const saga_route *route,
    uint64_t fingerprint) {
    return (hermas_journal_record){
        .kind = kind,
        .outcome = outcome,
        .execution_id = execution_id,
        .workflow_id = 7u,
        .request_id = request_id,
        .node_id = route->forward_node,
        .app_id = route->forward_app,
        .action_id = route->forward_action,
        .image_fingerprint = fingerprint
    };
}

static int write_fixture(
    const fixture_paths *paths,
    const uint8_t *image,
    size_t image_size,
    const saga_route routes[3],
    int uncertain) {
    hermas_journal_file journal;
    hermas_compensation_file compensation;
    hermas_result_file results;
    hermas_saga_log_file saga;
    hermas_journal_summary journal_summary;
    hermas_compensation_summary compensation_summary;
    hermas_result_summary result_summary;
    hermas_saga_log_summary saga_summary;
    if (hermas_journal_file_open(
            &journal, paths->journal, &journal_summary) !=
            HERMAS_JOURNAL_OK ||
        hermas_compensation_file_open(
            &compensation, paths->compensation,
            &compensation_summary) != HERMAS_COMPENSATION_OK ||
        hermas_result_file_open(
            &results, paths->results, &result_summary) !=
            HERMAS_RESULT_STORE_OK ||
        hermas_saga_log_file_open(
            &saga, paths->saga, &saga_summary) !=
            HERMAS_SAGA_LOG_OK) {
        return 0;
    }
    uint64_t fingerprint =
        hermas_journal_image_fingerprint(image, image_size);
    uint64_t execution_id = uncertain != 0 ? 43u : 42u;
    int ok = hermas_journal_writer_append(
                 &journal.writer,
                 (hermas_journal_record){
                     .kind = HERMAS_JOURNAL_EXECUTION_STARTED,
                     .execution_id = execution_id,
                     .workflow_id = 7u,
                     .image_fingerprint = fingerprint
                 }) == HERMAS_JOURNAL_OK;
    for (size_t ordinal = 0u; ok && ordinal < 3u; ++ordinal) {
        uint64_t request_id = ordinal + 1u;
        ok =
            hermas_journal_writer_append(
                &journal.writer,
                journal_action(
                    HERMAS_JOURNAL_DELIVERY_PREPARED,
                    HERMAS_OUTCOME_NONE, execution_id,
                    request_id, &routes[ordinal],
                    fingerprint)) == HERMAS_JOURNAL_OK &&
            hermas_journal_writer_append(
                &journal.writer,
                journal_action(
                    HERMAS_JOURNAL_DELIVERY_SENT,
                    HERMAS_OUTCOME_NONE, execution_id,
                    request_id, &routes[ordinal],
                    fingerprint)) == HERMAS_JOURNAL_OK &&
            hermas_journal_writer_append(
                &journal.writer,
                journal_action(
                    ordinal < 2u
                        ? HERMAS_JOURNAL_ACTION_SUCCEEDED
                        : HERMAS_JOURNAL_ACTION_FAILED,
                    ordinal < 2u
                        ? HERMAS_OUTCOME_SUCCESS
                        : HERMAS_OUTCOME_APP_ERROR,
                    execution_id, request_id, &routes[ordinal],
                    fingerprint)) == HERMAS_JOURNAL_OK;
    }
    uint8_t scratch[
        HERMAS_COMPENSATION_HEADER_SIZE +
        HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE];
    for (size_t ordinal = 0u; ok && ordinal < 2u; ++ordinal) {
        uint8_t token[8] = {
            (uint8_t)(11u * (ordinal + 1u))
        };
        ok = hermas_compensation_writer_append(
                 &compensation.writer,
                 (hermas_compensation_record){
                     .key = {
                         .execution_id = execution_id,
                         .workflow_id = 7u,
                         .request_id = ordinal + 1u,
                         .node_id = routes[ordinal].forward_node,
                         .image_fingerprint = fingerprint
                     },
                     .compensation_app_id =
                         routes[ordinal].compensation_app,
                     .compensation_action_id =
                         routes[ordinal].compensation_action,
                     .source_type =
                         routes[ordinal].token_source_type,
                     .destination_type =
                         routes[ordinal].token_destination_type,
                     .token = token,
                     .token_length = sizeof(token)
                 },
                 scratch, sizeof(scratch)) ==
             HERMAS_COMPENSATION_OK;
    }
    ok = ok &&
         hermas_result_writer_append(
             &results.writer,
             (hermas_result_record){
                 .key = {
                     .execution_id = execution_id,
                     .workflow_id = 7u,
                     .image_fingerprint = fingerprint
                 },
                 .outcome = HERMAS_OUTCOME_APP_ERROR,
                 .source_type = routes[2].failure_source_type,
                 .destination_type =
                     routes[2].failure_destination_type,
                 .value = NULL,
                 .value_length = 0u
             },
             scratch, sizeof(scratch)) ==
             HERMAS_RESULT_STORE_OK &&
         hermas_journal_writer_append(
             &journal.writer,
             (hermas_journal_record){
                 .kind = HERMAS_JOURNAL_EXECUTION_FINISHED,
                 .outcome = HERMAS_OUTCOME_APP_ERROR,
                 .execution_id = execution_id,
                 .workflow_id = 7u,
                 .image_fingerprint = fingerprint
             }) == HERMAS_JOURNAL_OK &&
         hermas_saga_log_writer_append(
             &saga.writer,
             (hermas_saga_log_record){
                 .kind = HERMAS_SAGA_LOG_STARTED,
                 .outcome = HERMAS_OUTCOME_APP_ERROR,
                 .execution_id = execution_id,
                 .workflow_id = 7u,
                 .ordinal = 2u,
                 .image_fingerprint = fingerprint
             }) == HERMAS_SAGA_LOG_OK;
    if (ok && uncertain != 0) {
        hermas_saga_log_record delivery = {
            .kind = HERMAS_SAGA_LOG_DELIVERY_PREPARED,
            .execution_id = execution_id,
            .workflow_id = 7u,
            .request_id = 4u,
            .forward_node = routes[1].forward_node,
            .app_id = routes[1].compensation_app,
            .action_id = routes[1].compensation_action,
            .ordinal = 2u,
            .image_fingerprint = fingerprint
        };
        ok = hermas_saga_log_writer_append(
                 &saga.writer, delivery) == HERMAS_SAGA_LOG_OK;
        delivery.kind = HERMAS_SAGA_LOG_DELIVERY_SENT;
        ok = ok && hermas_saga_log_writer_append(
                       &saga.writer, delivery) ==
                       HERMAS_SAGA_LOG_OK;
    }
    hermas_saga_log_file_close(&saga);
    hermas_result_file_close(&results);
    hermas_compensation_file_close(&compensation);
    hermas_journal_file_close(&journal);
    return ok;
}

static int attach_apps(
    hermas_host *host,
    app_channel channels[HERMAS_DAEMON_MAX_ACTIONS]) {
    for (size_t index = 0u;
         index < host->registry.action_count; ++index) {
        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
            return 0;
        }
        host->registry.actions[index].file_descriptor = sockets[0];
        channels[index] = (app_channel){
            .app_id = host->registry.actions[index].app_id,
            .action_id = host->registry.actions[index].action_id,
            .peer = sockets[1]
        };
    }
    return 1;
}

static int receive_compensation(
    hermas_host *host,
    const app_channel *channels,
    size_t channel_count,
    const saga_route *route,
    uint8_t expected_token) {
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    hermas_frame invocation;
    for (unsigned attempt = 0u; attempt < 16u; ++attempt) {
        size_t progress = 0u;
        if (hermas_host_step(host, 10, &progress) !=
            HERMAS_HOST_OK) {
            return 0;
        }
        for (size_t index = 0u; index < channel_count; ++index) {
            struct pollfd item = {
                .fd = channels[index].peer,
                .events = POLLIN
            };
            if (poll(&item, 1u, 0) <= 0 ||
                (item.revents & POLLIN) == 0) {
                continue;
            }
            ssize_t received = recv(
                item.fd, packet, sizeof(packet), 0);
            if (received <= 0 ||
                hermas_protocol_decode(
                    packet, (size_t)received, &invocation) !=
                    HERMAS_PROTOCOL_OK ||
                invocation.kind != HERMAS_FRAME_INVOKE ||
                invocation.app_id != route->compensation_app ||
                invocation.action_id !=
                    route->compensation_action ||
                invocation.payload_length != 8u ||
                invocation.payload[0] != expected_token) {
                return 0;
            }
            hermas_frame result = {
                .kind = HERMAS_FRAME_RESULT,
                .execution_id = invocation.execution_id,
                .request_id = invocation.request_id,
                .app_id = invocation.app_id,
                .action_id = invocation.action_id,
                .source_type = route->success_type,
                .destination_type = route->success_type,
                .outcome = HERMAS_OUTCOME_SUCCESS,
                .payload = NULL,
                .payload_length = 0u
            };
            size_t packet_size = 0u;
            return hermas_protocol_encode(
                       &result, packet, sizeof(packet),
                       &packet_size) == HERMAS_PROTOCOL_OK &&
                   send(item.fd, packet, packet_size, 0) ==
                       (ssize_t)packet_size;
        }
    }
    return 0;
}

static hermas_host_config host_config(
    const char *image_path,
    const fixture_paths *paths) {
    return (hermas_host_config){
        .image_path = image_path,
        .state_directory = paths->directory,
        .app_socket_path = paths->app_socket,
        .control_socket_path = paths->control_socket,
        .workflow_id = 7u
    };
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    uint8_t image[4096];
    size_t image_size = 0u;
    saga_route routes[3];
    memset(routes, 0, sizeof(routes));
    if (!load_image(
            argv[1], image, sizeof(image), &image_size) ||
        hermas_image_validate(image, image_size, NULL) !=
            HERMAS_IMAGE_OK ||
        !read_routes(image, routes)) {
        fputs("saga image fixture failed\n", stderr);
        return 2;
    }

    fixture_paths safe_paths;
    if (!make_paths(&safe_paths) ||
        !write_secure_image(&safe_paths, image, image_size) ||
        !write_fixture(
            &safe_paths, image, image_size, routes, 0)) {
        fputs("safe recovery fixture failed\n", stderr);
        return 1;
    }
    hermas_host host;
    hermas_host_config config =
        host_config(safe_paths.image, &safe_paths);
    if (hermas_host_open(&host, &config) != HERMAS_HOST_OK ||
        host.recovered_execution_count != 1u ||
        hermas_daemon_loop_active(&host.loop) != 1u) {
        fputs("safe compensation was not restored\n", stderr);
        return 1;
    }
    app_channel channels[HERMAS_DAEMON_MAX_ACTIONS];
    memset(channels, 0, sizeof(channels));
    if (!attach_apps(&host, channels) ||
        !receive_compensation(
            &host, channels, host.registry.action_count,
            &routes[1], 22u) ||
        !receive_compensation(
            &host, channels, host.registry.action_count,
            &routes[0], 11u)) {
        fputs("restored compensation transport failed\n", stderr);
        return 1;
    }
    for (unsigned attempt = 0u;
         attempt < 16u &&
         host.recovered_execution_count != 0u; ++attempt) {
        size_t progress = 0u;
        if (hermas_host_step(&host, 10, &progress) !=
            HERMAS_HOST_OK) {
            fputs("restored compensation completion failed\n", stderr);
            return 1;
        }
    }
    if (host.recovered_execution_count != 0u ||
        hermas_daemon_loop_active(&host.loop) != 0u) {
        fputs("restored execution was not reaped\n", stderr);
        return 1;
    }
    for (size_t index = 0u;
         index < host.registry.action_count; ++index) {
        close(channels[index].peer);
    }
    hermas_host_close(&host);
    if (hermas_host_open(&host, &config) != HERMAS_HOST_OK ||
        host.recovered_execution_count != 0u ||
        hermas_daemon_loop_active(&host.loop) != 0u) {
        fputs("completed compensation was recovered twice\n", stderr);
        return 1;
    }
    hermas_host_close(&host);
    remove_fixture(&safe_paths);

    fixture_paths unsafe_paths;
    if (!make_paths(&unsafe_paths) ||
        !write_secure_image(&unsafe_paths, image, image_size) ||
        !write_fixture(
            &unsafe_paths, image, image_size, routes, 1)) {
        fputs("uncertain recovery fixture failed\n", stderr);
        return 1;
    }
    config = host_config(unsafe_paths.image, &unsafe_paths);
    if (hermas_host_open(&host, &config) !=
            HERMAS_HOST_RECOVERY_REQUIRED ||
        access(unsafe_paths.app_socket, F_OK) == 0 ||
        access(unsafe_paths.control_socket, F_OK) == 0) {
        fputs("uncertain compensation was replayed\n", stderr);
        return 1;
    }
    remove_fixture(&unsafe_paths);
    puts("safe and uncertain host recovery passed");
    return 0;
}
