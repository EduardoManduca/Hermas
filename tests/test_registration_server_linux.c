#include "hermas2/image.h"
#include "hermas2/protocol.h"
#include "hermas2/registration_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "failure: %s\n", message);
    return 1;
}

static uint8_t *read_file(const char *path, size_t *size) {
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
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static int send_registration(
    int descriptor,
    uint16_t app_id,
    uint16_t action_id,
    const uint8_t fingerprint[32]) {
    uint8_t packet[HERMAS2_PROTOCOL_HEADER_SIZE + 32u];
    hermas2_frame registration = {
        .kind = HERMAS2_FRAME_REGISTER_APP,
        .app_id = app_id,
        .action_id = action_id,
        .outcome = HERMAS2_OUTCOME_NONE,
        .payload = fingerprint,
        .payload_length = 32u
    };
    size_t packet_size = 0u;
    return hermas2_protocol_encode(
               &registration, packet, sizeof(packet), &packet_size) ==
               HERMAS2_PROTOCOL_OK &&
           send(descriptor, packet, packet_size, 0) ==
               (ssize_t)packet_size;
}

static int receive_kind(int descriptor, uint16_t expected_kind) {
    uint8_t packet[HERMAS2_PROTOCOL_HEADER_SIZE + 32u];
    ssize_t received = recv(descriptor, packet, sizeof(packet), 0);
    hermas2_frame frame;
    return received > 0 &&
           hermas2_protocol_decode(
               packet, (size_t)received, &frame) ==
               HERMAS2_PROTOCOL_OK &&
           frame.kind == expected_kind;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return fail("expected graph image path");
    }
    size_t image_size = 0u;
    uint8_t *image = read_file(argv[1], &image_size);
    hermas2_image_summary summary;
    if (image == NULL ||
        hermas2_image_validate(image, image_size, &summary) !=
            HERMAS2_IMAGE_OK ||
        summary.action_contract_count < 2u) {
        free(image);
        return fail("could not load multi-app image");
    }
    hermas2_daemon_registry registry;
    hermas2_registration_server server;
    if (hermas2_daemon_registry_init(
            &registry, image, image_size) != HERMAS2_DAEMON_OK ||
        hermas2_registration_server_init(
            &server, &registry) != HERMAS2_REGISTRATION_SERVER_OK) {
        free(image);
        return fail("could not initialize registration server");
    }

    int slow[2];
    int ready[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, slow) != 0 ||
        socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ready) != 0 ||
        hermas2_registration_server_attach(&server, slow[1]) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        hermas2_registration_server_attach(&server, ready[1]) !=
            HERMAS2_REGISTRATION_SERVER_OK) {
        return fail("could not attach pending connections");
    }
    size_t progress = 99u;
    if (hermas2_registration_server_step(&server, 0, &progress) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        progress != 0u ||
        hermas2_registration_server_pending(&server) != 2u) {
        return fail("idle registration step was not nonblocking");
    }
    uint16_t first_id = registry.actions[0].app_id;
    uint16_t local_action_id = 77u;
    if (!send_registration(
            ready[0], first_id, local_action_id,
            registry.actions[0].contract_fingerprint) ||
        hermas2_registration_server_step(&server, 0, &progress) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        progress != 2u ||
        hermas2_registration_server_pending(&server) != 1u ||
        hermas2_daemon_registry_find_action(
            &registry, first_id, registry.actions[0].action_id, NULL) < 0 ||
        registry.actions[0].registered_action_id != local_action_id ||
        !receive_kind(ready[0], HERMAS2_FRAME_REGISTER_OK)) {
        return fail("ready app was blocked by idle peer");
    }
    close(ready[0]);

    int wrong[2];
    uint8_t wrong_fingerprint[32];
    memcpy(
        wrong_fingerprint, registry.actions[1].contract_fingerprint,
        sizeof(wrong_fingerprint));
    wrong_fingerprint[0] ^= 0xffu;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, wrong) != 0 ||
        hermas2_registration_server_attach(&server, wrong[1]) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        !send_registration(
            wrong[0], registry.actions[1].app_id,
            registry.actions[1].action_id, wrong_fingerprint) ||
        hermas2_registration_server_step(&server, 0, &progress) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        progress != 1u ||
        hermas2_daemon_registry_find_action(
            &registry, registry.actions[1].app_id,
            registry.actions[1].action_id, NULL) >= 0 ||
        !receive_kind(wrong[0], HERMAS2_FRAME_PROTOCOL_ERROR)) {
        return fail("contract mismatch was not isolated");
    }
    close(wrong[0]);

    int duplicate[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, duplicate) != 0 ||
        hermas2_registration_server_attach(&server, duplicate[1]) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        !send_registration(
            duplicate[0], first_id, registry.actions[0].action_id,
            registry.actions[0].contract_fingerprint) ||
        hermas2_registration_server_step(&server, 0, &progress) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        progress != 1u ||
        !receive_kind(
            duplicate[0], HERMAS2_FRAME_PROTOCOL_ERROR)) {
        return fail("duplicate registration was not rejected");
    }
    close(duplicate[0]);

    close(slow[0]);
    if (hermas2_registration_server_step(&server, 0, &progress) !=
            HERMAS2_REGISTRATION_SERVER_OK ||
        progress != 1u ||
        hermas2_registration_server_pending(&server) != 0u) {
        return fail("disconnected pending app was not reclaimed");
    }

    int capacity[HERMAS2_REGISTRATION_MAX_PENDING][2];
    for (size_t index = 0u;
         index < HERMAS2_REGISTRATION_MAX_PENDING; ++index) {
        if (socketpair(
                AF_UNIX, SOCK_SEQPACKET, 0, capacity[index]) != 0 ||
            hermas2_registration_server_attach(
                &server, capacity[index][1]) !=
                HERMAS2_REGISTRATION_SERVER_OK) {
            return fail("could not fill bounded registration capacity");
        }
    }
    int excess[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, excess) != 0 ||
        hermas2_registration_server_attach(&server, excess[1]) !=
            HERMAS2_REGISTRATION_SERVER_CAPACITY_EXHAUSTED ||
        hermas2_registration_server_pending(&server) !=
            HERMAS2_REGISTRATION_MAX_PENDING) {
        return fail("registration capacity was not enforced");
    }
    close(excess[0]);
    close(excess[1]);

    hermas2_registration_server_close(&server);
    hermas2_registration_server_close(&server);
    for (size_t index = 0u;
         index < HERMAS2_REGISTRATION_MAX_PENDING; ++index) {
        if (server.clients[index].file_descriptor != -1) {
            return fail(
                "registration close did not preserve descriptor sentinels");
        }
        close(capacity[index][0]);
    }
    hermas2_daemon_registry_close(&registry);
    free(image);
    puts("nonblocking app registration tests passed");
    return 0;
}
