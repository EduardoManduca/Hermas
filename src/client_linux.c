#define _POSIX_C_SOURCE 200809L

#include "hermas2/client.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

hermas2_client_result hermas2_client_connect(
    hermas2_client *client,
    const char *socket_path) {
    if (client == NULL || socket_path == NULL ||
        socket_path[0] == '\0') {
        return HERMAS2_CLIENT_INVALID_ARGUMENT;
    }
    memset(client, 0, sizeof(*client));
    client->file_descriptor = -1;
    size_t path_length = strlen(socket_path);
    if (path_length >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        return HERMAS2_CLIENT_INVALID_ARGUMENT;
    }
    int descriptor =
        socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (descriptor < 0) {
        return HERMAS2_CLIENT_SOCKET_ERROR;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, path_length + 1u);
    int connected;
    do {
        connected = connect(
            descriptor, (const struct sockaddr *)&address,
            sizeof(address));
    } while (connected != 0 && errno == EINTR);
    if (connected != 0) {
        close(descriptor);
        return HERMAS2_CLIENT_CONNECT_ERROR;
    }
    client->file_descriptor = descriptor;
    return HERMAS2_CLIENT_OK;
}

hermas2_client_result hermas2_client_execute(
    hermas2_client *client,
    uint64_t execution_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    hermas2_frame *result) {
    if (client == NULL || client->file_descriptor < 0 ||
        execution_id == 0u || input_type == 0u ||
        (input == NULL && input_length != 0u) ||
        input_length > UINT32_MAX || packet_buffer == NULL ||
        result == NULL) {
        return HERMAS2_CLIENT_INVALID_ARGUMENT;
    }
    hermas2_frame request = {
        .kind = HERMAS2_FRAME_EXECUTE,
        .execution_id = execution_id,
        .source_type = input_type,
        .outcome = HERMAS2_OUTCOME_NONE,
        .payload = input,
        .payload_length = (uint32_t)input_length
    };
    size_t packet_size = 0u;
    if (hermas2_protocol_encode(
            &request, packet_buffer, packet_capacity,
            &packet_size) != HERMAS2_PROTOCOL_OK) {
        return HERMAS2_CLIENT_ENCODE_ERROR;
    }
    ssize_t sent;
    do {
        sent = send(
            client->file_descriptor, packet_buffer, packet_size,
            MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    if (sent != (ssize_t)packet_size) {
        return HERMAS2_CLIENT_SEND_ERROR;
    }
    struct iovec vector = {
        .iov_base = packet_buffer,
        .iov_len = packet_capacity
    };
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1u;
    ssize_t received;
    do {
        received = recvmsg(client->file_descriptor, &message, 0);
    } while (received < 0 && errno == EINTR);
    if (received <= 0) {
        return HERMAS2_CLIENT_RECEIVE_ERROR;
    }
    if ((message.msg_flags & MSG_TRUNC) != 0) {
        return HERMAS2_CLIENT_TRUNCATED_PACKET;
    }
    if (hermas2_protocol_decode(
            packet_buffer, (size_t)received, result) !=
        HERMAS2_PROTOCOL_OK) {
        return HERMAS2_CLIENT_PROTOCOL_ERROR;
    }
    return result->kind == HERMAS2_FRAME_EXECUTION_RESULT &&
                   result->execution_id == execution_id
               ? HERMAS2_CLIENT_OK
               : HERMAS2_CLIENT_UNEXPECTED_RESULT;
}

void hermas2_client_close(hermas2_client *client) {
    if (client == NULL) {
        return;
    }
    if (client->file_descriptor >= 0) {
        close(client->file_descriptor);
    }
    memset(client, 0, sizeof(*client));
    client->file_descriptor = -1;
}

const char *hermas2_client_result_name(
    hermas2_client_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "socket-error", "connect-error",
        "encode-error", "send-error", "receive-error",
        "truncated-packet", "protocol-error", "unexpected-result"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
