#include "hermas2/daemon.h"
#include "hermas2/edge.h"
#include "hermas2/runtime.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static int fail(const char *message) {
    fprintf(stderr, "test_pipeline: %s\n", message);
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

static void run_bad_registration(
    const char *socket_path,
    const uint8_t fingerprint[32]) {
    uint8_t wrong[32];
    memcpy(wrong, fingerprint, sizeof(wrong));
    wrong[0] ^= 0xffu;
    hermas2_edge edge;
    hermas2_edge_result result =
        hermas2_edge_connect(&edge, socket_path, 1u, 1u, wrong);
    if (result == HERMAS2_EDGE_OK) {
        hermas2_edge_disconnect(&edge);
        _exit(1);
    }
    _exit(0);
}

static void fingerprint_hex(
    const uint8_t fingerprint[32],
    char text[65]) {
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0u; index < 32u; ++index) {
        text[index * 2u] = digits[fingerprint[index] >> 4u];
        text[index * 2u + 1u] = digits[fingerprint[index] & 0x0fu];
    }
    text[64] = '\0';
}

static int create_listener(const char *path) {
    int descriptor = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (descriptor < 0) {
        return -1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1u);
    unlink(path);
    if (bind(descriptor, (const struct sockaddr *)&address, sizeof(address)) !=
            0 ||
        listen(descriptor, 3) != 0) {
        close(descriptor);
        unlink(path);
        return -1;
    }
    return descriptor;
}

static bool drive_pipeline(
    const uint8_t *image,
    size_t image_size,
    hermas2_daemon_registry *registry) {
    hermas2_daemon_loop loop;
    if (hermas2_daemon_loop_init(&loop, registry, image, image_size) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_admit(&loop, 91u, 1u, NULL, 0u) !=
            HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_admit(&loop, 91u, 1u, NULL, 0u) !=
            HERMAS2_LOOP_DUPLICATE_EXECUTION) {
        return false;
    }
    hermas2_frame result;
    size_t iterations = 0u;
    while (hermas2_daemon_loop_result(&loop, 91u, &result) ==
           HERMAS2_LOOP_EXECUTION_ACTIVE) {
        size_t progress = 0u;
        if (iterations++ >= 32u ||
            hermas2_daemon_loop_poll(&loop, 1000, &progress) !=
                HERMAS2_LOOP_OK) {
            return false;
        }
    }
    if (hermas2_daemon_loop_result(&loop, 91u, &result) !=
            HERMAS2_LOOP_OK ||
        result.outcome != HERMAS2_OUTCOME_SUCCESS ||
        result.source_type != 11u || result.payload_length != 1u ||
        result.payload[0] != 1u) {
        return false;
    }
    if (hermas2_daemon_loop_release(&loop, 91u) != HERMAS2_LOOP_OK ||
        hermas2_daemon_loop_active(&loop) != 0u) {
        return false;
    }
    for (uint64_t id = 100u;
         id < 100u + HERMAS2_DAEMON_MAX_EXECUTIONS; ++id) {
        if (hermas2_daemon_loop_admit(&loop, id, 1u, NULL, 0u) !=
            HERMAS2_LOOP_OK) {
            return false;
        }
    }
    return hermas2_daemon_loop_active(&loop) ==
               HERMAS2_DAEMON_MAX_EXECUTIONS &&
           hermas2_daemon_loop_admit(&loop, 999u, 1u, NULL, 0u) ==
               HERMAS2_LOOP_CAPACITY_EXHAUSTED;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        return fail("expected image and three app executable paths");
    }
    size_t image_size = 0u;
    uint8_t *image = read_fixture(argv[1], &image_size);
    if (image == NULL) {
        return fail("cannot read graph image");
    }
    uint8_t fingerprints[4][32] = {{0u}};
    char fingerprint_text[4][65] = {{0}};
    size_t action_contract_count = read_u16(image, 28u);
    size_t apps_offset = read_u32(image, 40u);
    for (size_t index = 0u; index < action_contract_count; ++index) {
        size_t offset = apps_offset + index * 36u;
        uint16_t app_id = read_u16(image, offset);
        if (app_id == 0u || app_id > 3u) {
            free(image);
            return fail("fixture app IDs differ");
        }
        memcpy(fingerprints[app_id], image + offset + 4u, 32u);
        fingerprint_hex(fingerprints[app_id], fingerprint_text[app_id]);
    }

    char socket_path[96];
    snprintf(socket_path, sizeof(socket_path), "/tmp/hermas2-pipeline-%ld.sock",
             (long)getpid());
    int listener = create_listener(socket_path);
    int output_pipe[2];
    if (listener < 0 || pipe(output_pipe) != 0) {
        free(image);
        return fail("cannot create local endpoints");
    }
    hermas2_daemon_registry registry;
    memset(&registry, 0, sizeof(registry));
    if (hermas2_daemon_registry_init(&registry, image, image_size) !=
        HERMAS2_DAEMON_OK) {
        free(image);
        return fail("cannot initialize app registry");
    }
    uint8_t registration_packet[HERMAS2_PROTOCOL_HEADER_SIZE + 32u];
    pid_t bad_child = fork();
    if (bad_child == 0) {
        close(listener);
        close(output_pipe[0]);
        close(output_pipe[1]);
        run_bad_registration(socket_path, fingerprints[1]);
    }
    int bad_status = 0;
    if (bad_child < 0 ||
        hermas2_daemon_registry_accept(
            &registry, listener, registration_packet,
            sizeof(registration_packet)) != HERMAS2_DAEMON_CONTRACT_MISMATCH ||
        waitpid(bad_child, &bad_status, 0) != bad_child ||
        !WIFEXITED(bad_status) || WEXITSTATUS(bad_status) != 0) {
        hermas2_daemon_registry_close(&registry);
        free(image);
        return fail("contract mismatch registration was not rejected");
    }
    pid_t children[3];
    for (uint16_t app = 1u; app <= 3u; ++app) {
        children[app - 1u] = fork();
        if (children[app - 1u] == 0) {
            close(listener);
            close(output_pipe[0]);
            if (dup2(output_pipe[1], STDOUT_FILENO) < 0) {
                _exit(1);
            }
            close(output_pipe[1]);
            const char *executable = argv[(size_t)app + 1u];
            execl(executable, executable, socket_path,
                  fingerprint_text[app], (char *)NULL);
            _exit(1);
        }
        if (children[app - 1u] < 0) {
            free(image);
            return fail("cannot fork app");
        }
    }
    close(output_pipe[1]);
    bool success = true;
    for (size_t index = 0u; success && index < 3u; ++index) {
        success = hermas2_daemon_registry_accept(
                      &registry, listener, registration_packet,
                      sizeof(registration_packet)) == HERMAS2_DAEMON_OK;
    }
    success = success && drive_pipeline(image, image_size, &registry);
    close(listener);
    unlink(socket_path);
    hermas2_daemon_registry_close(&registry);
    char output[64] = {0};
    ssize_t output_length = read(output_pipe[0], output, sizeof(output) - 1u);
    close(output_pipe[0]);
    if (output_length != 9 || memcmp(output, "Mean: 80\n", 9u) != 0) {
        success = false;
    }
    for (size_t index = 0u; index < 3u; ++index) {
        int status = 0;
        if (waitpid(children[index], &status, 0) != children[index] ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            success = false;
        }
    }
    free(image);
    return success ? 0 : fail("four-process Grade Pipeline failed");
}
