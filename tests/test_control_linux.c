#include "hermas/control.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *message) {
    fprintf(stderr, "test_control_linux: %s\n", message);
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

static int submit(
    hermas_daemon_loop *loop,
    uint16_t input_type,
    uint64_t execution_id,
    uint8_t *packet,
    size_t *packet_size) {
    uint8_t input[8] = {3u};
    hermas_frame request = {
        .kind = HERMAS_FRAME_EXECUTE,
        .execution_id = execution_id,
        .source_type = input_type,
        .outcome = HERMAS_OUTCOME_NONE,
        .payload = input,
        .payload_length = sizeof(input)
    };
    uint64_t admitted_id = 0u;
    return hermas_protocol_encode(
               &request, packet, HERMAS_PROTOCOL_MAX_PACKET_SIZE,
               packet_size) == HERMAS_PROTOCOL_OK &&
           hermas_control_submit(
               loop, packet, *packet_size, &admitted_id) ==
               HERMAS_CONTROL_OK &&
           admitted_id == execution_id;
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
    hermas_daemon_registry registry;
    hermas_daemon_loop *loop = malloc(sizeof(*loop));
    uint8_t *packet = malloc(HERMAS_PROTOCOL_MAX_PACKET_SIZE);
    if (loop == NULL || packet == NULL ||
        hermas_daemon_registry_init(
            &registry, image, image_size) != HERMAS_DAEMON_OK ||
        hermas_daemon_loop_init(
            loop, &registry, image, image_size) != HERMAS_LOOP_OK) {
        free(packet);
        free(loop);
        free(image);
        return fail("cannot initialize control fixture");
    }
    uint16_t input_type = read_u16(image, 22u);
    size_t packet_size = 0u;
    if (!submit(loop, input_type, 100u, packet, &packet_size)) {
        return fail("valid EXECUTE was not admitted");
    }
    uint64_t ignored = 0u;
    if (hermas_control_submit(
            loop, packet, packet_size, &ignored) !=
            HERMAS_CONTROL_ADMISSION_ERROR ||
        hermas_control_submit(
            loop, packet, packet_size - 1u, &ignored) !=
            HERMAS_CONTROL_PROTOCOL_ERROR ||
        hermas_control_collect(
            loop, 100u, packet, HERMAS_PROTOCOL_MAX_PACKET_SIZE,
            &packet_size) != HERMAS_CONTROL_EXECUTION_ACTIVE ||
        hermas_control_release(loop, 100u) !=
            HERMAS_CONTROL_EXECUTION_ACTIVE) {
        return fail("control state boundaries were not enforced");
    }
    for (uint64_t id = 101u;
         id < 100u + HERMAS_DAEMON_MAX_EXECUTIONS; ++id) {
        if (!submit(loop, input_type, id, packet, &packet_size)) {
            return fail("bounded execution slot was not admitted");
        }
    }
    if (submit(
            loop, input_type,
            100u + HERMAS_DAEMON_MAX_EXECUTIONS,
            packet, &packet_size)) {
        return fail("control execution capacity was exceeded");
    }
    hermas_daemon_registry_close(&registry);
    free(packet);
    free(loop);
    free(image);
    puts("control packet admission tests passed");
    return 0;
}
