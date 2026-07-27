#define _POSIX_C_SOURCE 200809L

#include "hermas2/control_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_control_server_linux: %s\n", message);
    return 1;
}

static int load_image(
    const char *path,
    uint8_t **image,
    size_t *image_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 0;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    *image = malloc((size_t)length);
    if (*image == NULL ||
        fread(*image, 1u, (size_t)length, file) != (size_t)length) {
        free(*image);
        fclose(file);
        return 0;
    }
    fclose(file);
    *image_size = (size_t)length;
    return 1;
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static int send_execute(
    int descriptor,
    uint16_t input_type,
    uint64_t execution_id) {
    uint8_t input[8] = {3u};
    hermas2_frame request = {
        .kind = HERMAS2_FRAME_EXECUTE,
        .execution_id = execution_id,
        .source_type = input_type,
        .payload = input,
        .payload_length = sizeof(input)
    };
    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    size_t size = 0u;
    return hermas2_protocol_encode(
               &request, packet, sizeof(packet), &size) ==
                   HERMAS2_PROTOCOL_OK &&
           send(descriptor, packet, size, 0) == (ssize_t)size;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    uint8_t *image = NULL;
    size_t image_size = 0u;
    if (!load_image(argv[1], &image, &image_size)) {
        return 2;
    }
    hermas2_daemon_registry registry;
    hermas2_daemon_loop *loop = malloc(sizeof(*loop));
    hermas2_control_server *server = malloc(sizeof(*server));
    if (loop == NULL || server == NULL ||
        hermas2_daemon_registry_init(
            &registry, image, image_size) != HERMAS2_DAEMON_OK ||
        hermas2_daemon_loop_init(
            loop, &registry, image, image_size) != HERMAS2_LOOP_OK ||
        hermas2_control_server_init(server, loop) !=
            HERMAS2_CONTROL_SERVER_OK) {
        return fail("cannot initialize server fixture");
    }
    int valid[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, valid) != 0 ||
        hermas2_control_server_attach(server, valid[0]) !=
            HERMAS2_CONTROL_SERVER_OK ||
        !send_execute(valid[1], read_u16(image, 22u), 71u)) {
        return fail("cannot attach valid caller");
    }
    size_t progress = 0u;
    if (hermas2_control_server_step(server, 0, &progress) !=
            HERMAS2_CONTROL_SERVER_OK ||
        hermas2_daemon_loop_active(loop) != 1u ||
        hermas2_control_server_active(server) != 1u) {
        return fail("valid caller was not admitted");
    }
    close(valid[1]);
    if (hermas2_control_server_step(server, 10, &progress) !=
            HERMAS2_CONTROL_SERVER_OK ||
        hermas2_daemon_loop_active(loop) != 1u ||
        hermas2_control_server_active(server) != 1u ||
        server->clients[0].file_descriptor >= 0) {
        return fail("detached execution was discarded");
    }

    int invalid[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, invalid) != 0 ||
        hermas2_control_server_attach(server, invalid[0]) !=
            HERMAS2_CONTROL_SERVER_OK) {
        return fail("cannot attach invalid caller");
    }
    uint8_t malformed = 0u;
    if (send(invalid[1], &malformed, 1u, 0) != 1 ||
        hermas2_control_server_step(server, 10, &progress) !=
            HERMAS2_CONTROL_SERVER_OK) {
        return fail("cannot process invalid caller");
    }
    uint8_t response[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    ssize_t received = recv(invalid[1], response, sizeof(response), 0);
    hermas2_frame error;
    if (received <= 0 ||
        hermas2_protocol_decode(
            response, (size_t)received, &error) !=
            HERMAS2_PROTOCOL_OK ||
        error.kind != HERMAS2_FRAME_PROTOCOL_ERROR ||
        hermas2_control_server_active(server) != 1u) {
        return fail("invalid caller was not rejected");
    }
    close(invalid[1]);
    hermas2_control_server_close(server);
    hermas2_daemon_registry_close(&registry);
    free(server);
    free(loop);
    free(image);
    puts("control connection ownership tests passed");
    return 0;
}
