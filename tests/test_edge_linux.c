#include "hermas2/edge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int handled = 0;

static int handler(
    void *user_data,
    uint16_t action_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint16_t *result_type,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length) {
    (void)user_data;
    if (action_id != 2u || input_type != 3u || input_length != 8u ||
        result_capacity < 8u) {
        return 0;
    }
    ++handled;
    memcpy(result, input, input_length);
    *outcome = HERMAS2_OUTCOME_SUCCESS;
    *result_type = 4u;
    *result_length = input_length;
    return 1;
}

static int send_encoded(int fd, const hermas2_frame *frame) {
    uint8_t packet[128];
    size_t size = 0u;
    return hermas2_protocol_encode(frame, packet, sizeof(packet), &size) ==
               HERMAS2_PROTOCOL_OK &&
           send(fd, packet, size, MSG_NOSIGNAL) == (ssize_t)size;
}

static int server(int listener) {
    int client = accept(listener, NULL, NULL);
    uint8_t packet[128];
    ssize_t size = recv(client, packet, sizeof(packet), 0);
    hermas2_frame frame;
    if (client < 0 || size <= 0 ||
        hermas2_protocol_decode(packet, (size_t)size, &frame) !=
            HERMAS2_PROTOCOL_OK ||
        frame.kind != HERMAS2_FRAME_REGISTER_APP || frame.app_id != 1u) {
        return 1;
    }
    hermas2_frame acknowledged = {
        .kind = HERMAS2_FRAME_REGISTER_OK,
        .app_id = 1u,
        .outcome = HERMAS2_OUTCOME_NONE
    };
    if (!send_encoded(client, &acknowledged)) {
        return 1;
    }
    uint8_t value[8] = {80u};
    hermas2_frame invoke = {
        .kind = HERMAS2_FRAME_INVOKE,
        .execution_id = 10u,
        .request_id = 20u,
        .app_id = 1u,
        .action_id = 2u,
        .source_type = 3u,
        .destination_type = 3u,
        .outcome = HERMAS2_OUTCOME_NONE,
        .payload = value,
        .payload_length = sizeof(value)
    };
    if (!send_encoded(client, &invoke)) {
        return 1;
    }
    size = recv(client, packet, sizeof(packet), 0);
    int valid = size > 0 &&
                hermas2_protocol_decode(packet, (size_t)size, &frame) ==
                    HERMAS2_PROTOCOL_OK &&
                frame.kind == HERMAS2_FRAME_RESULT &&
                frame.outcome == HERMAS2_OUTCOME_SUCCESS &&
                frame.source_type == 4u && frame.payload_length == 8u;
    close(client);
    return valid ? 0 : 1;
}

int main(void) {
    char path[96];
    snprintf(path, sizeof(path), "/tmp/hermas2-edge-%ld.sock", (long)getpid());
    int listener = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1u);
    unlink(path);
    if (listener < 0 ||
        bind(listener, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        return 1;
    }
    pid_t child = fork();
    if (child < 0) {
        return 1;
    }
    if (child == 0) {
        int result = server(listener);
        close(listener);
        _exit(result);
    }
    uint8_t fingerprint[32] = {1u};
    hermas2_edge edge;
    uint8_t packet[128];
    uint8_t result[16];
    int valid =
        hermas2_edge_connect(&edge, path, 1u, fingerprint) == HERMAS2_EDGE_OK &&
        hermas2_edge_serve_once(&edge, packet, sizeof(packet), result,
                                sizeof(result), handler, NULL) ==
            HERMAS2_EDGE_OK &&
        edge.delivered_invocations == 1u && handled == 1;
    hermas2_edge_disconnect(&edge);
    int status = 0;
    waitpid(child, &status, 0);
    close(listener);
    unlink(path);
    return valid && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}
