#define _POSIX_C_SOURCE 200809L

#include "hermas/client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_client_linux: %s\n", message);
    return 1;
}

static int create_listener(const char *path) {
    int listener =
        socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (listener < 0) {
        return -1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1u);
    if (bind(
            listener, (const struct sockaddr *)&address,
            sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        close(listener);
        return -1;
    }
    return listener;
}

static int server(int listener) {
    int client = accept(listener, NULL, NULL);
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    ssize_t received =
        client >= 0 ? recv(client, packet, sizeof(packet), 0) : -1;
    hermas_frame request;
    if (received <= 0 ||
        hermas_protocol_decode(
            packet, (size_t)received, &request) !=
            HERMAS_PROTOCOL_OK ||
        request.kind != HERMAS_FRAME_EXECUTE ||
        request.execution_id != 91u ||
        request.source_type != 3u ||
        request.payload_length != 8u ||
        request.payload[0] != 17u) {
        if (client >= 0) {
            close(client);
        }
        return 1;
    }
    uint8_t value[8] = {29u};
    hermas_frame response = {
        .kind = HERMAS_FRAME_EXECUTION_RESULT,
        .execution_id = request.execution_id,
        .source_type = 4u,
        .destination_type = 5u,
        .outcome = HERMAS_OUTCOME_SUCCESS,
        .payload = value,
        .payload_length = sizeof(value)
    };
    size_t size = 0u;
    int ok =
        hermas_protocol_encode(
            &response, packet, sizeof(packet), &size) ==
            HERMAS_PROTOCOL_OK &&
        send(client, packet, size, 0) == (ssize_t)size;
    close(client);
    return ok ? 0 : 1;
}

int main(void) {
    char directory[] = "/tmp/hermas-client-XXXXXX";
    if (mkdtemp(directory) == NULL) {
        return fail("cannot create socket directory");
    }
    char path[108];
    if (snprintf(path, sizeof(path), "%s/control.sock", directory) <= 0) {
        return fail("cannot form socket path");
    }
    int listener = create_listener(path);
    if (listener < 0) {
        rmdir(directory);
        return fail("cannot create control listener");
    }
    pid_t child = fork();
    if (child < 0) {
        return fail("cannot fork mock daemon");
    }
    if (child == 0) {
        int status = server(listener);
        close(listener);
        _exit(status);
    }
    hermas_client client = {.file_descriptor = -1};
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    uint8_t input[8] = {17u};
    hermas_frame result;
    int ok =
        hermas_client_connect(&client, path) == HERMAS_CLIENT_OK &&
        hermas_client_execute(
            &client, 91u, 3u, input, sizeof(input),
            packet, sizeof(packet), &result) == HERMAS_CLIENT_OK &&
        result.outcome == HERMAS_OUTCOME_SUCCESS &&
        result.source_type == 4u &&
        result.destination_type == 5u &&
        result.payload_length == 8u &&
        result.payload[0] == 29u;
    hermas_client_close(&client);
    int child_status = 0;
    close(listener);
    (void)waitpid(child, &child_status, 0);
    unlink(path);
    rmdir(directory);
    if (!ok || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0) {
        return fail("caller ABI did not round trip execution");
    }
    puts("caller client ABI tests passed");
    return 0;
}
