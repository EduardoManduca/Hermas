#include "hermas2/edge.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static hermas2_edge_result send_frame(
    int file_descriptor,
    const hermas2_frame *frame,
    uint8_t *packet,
    size_t capacity) {
    size_t packet_size = 0u;
    if (hermas2_protocol_encode(frame, packet, capacity, &packet_size) !=
        HERMAS2_PROTOCOL_OK) {
        return HERMAS2_EDGE_PROTOCOL_ERROR;
    }
    ssize_t sent;
    do {
        sent = send(file_descriptor, packet, packet_size, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent == (ssize_t)packet_size ? HERMAS2_EDGE_OK
                                        : HERMAS2_EDGE_SEND_ERROR;
}

static hermas2_edge_result receive_packet(
    int file_descriptor,
    uint8_t *packet,
    size_t capacity,
    size_t *packet_size) {
    struct iovec vector = {.iov_base = packet, .iov_len = capacity};
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1u;
    ssize_t received;
    do {
        received = recvmsg(file_descriptor, &message, 0);
    } while (received < 0 && errno == EINTR);
    if (received <= 0) {
        return HERMAS2_EDGE_RECEIVE_ERROR;
    }
    if ((message.msg_flags & MSG_TRUNC) != 0 ||
        (size_t)received > capacity) {
        return HERMAS2_EDGE_TRUNCATED_PACKET;
    }
    *packet_size = (size_t)received;
    return HERMAS2_EDGE_OK;
}

hermas2_edge_result hermas2_edge_connect(
    hermas2_edge *edge,
    const char *socket_path,
    uint16_t app_id,
    const uint8_t contract_fingerprint[32]) {
    if (edge == NULL || socket_path == NULL ||
        contract_fingerprint == NULL || app_id == 0u) {
        return HERMAS2_EDGE_INVALID_ARGUMENT;
    }
    size_t path_length = strlen(socket_path);
    struct sockaddr_un address;
    if (path_length == 0u || path_length >= sizeof(address.sun_path)) {
        return HERMAS2_EDGE_PATH_TOO_LONG;
    }
    memset(edge, 0, sizeof(*edge));
    edge->file_descriptor = -1;
    int file_descriptor = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (file_descriptor < 0) {
        return HERMAS2_EDGE_SOCKET_ERROR;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, path_length + 1u);
    if (connect(file_descriptor, (const struct sockaddr *)&address,
                sizeof(address)) != 0) {
        close(file_descriptor);
        return HERMAS2_EDGE_CONNECT_ERROR;
    }
    uint8_t packet[HERMAS2_PROTOCOL_HEADER_SIZE + 32u];
    hermas2_frame registration = {
        .kind = HERMAS2_FRAME_REGISTER_APP,
        .app_id = app_id,
        .outcome = HERMAS2_OUTCOME_NONE,
        .payload = contract_fingerprint,
        .payload_length = 32u
    };
    hermas2_edge_result result =
        send_frame(file_descriptor, &registration, packet, sizeof(packet));
    size_t packet_size = 0u;
    hermas2_frame response;
    if (result == HERMAS2_EDGE_OK) {
        result = receive_packet(file_descriptor, packet, sizeof(packet),
                                &packet_size);
    }
    if (result == HERMAS2_EDGE_OK &&
        (hermas2_protocol_decode(packet, packet_size, &response) !=
             HERMAS2_PROTOCOL_OK ||
         response.kind != HERMAS2_FRAME_REGISTER_OK)) {
        result = HERMAS2_EDGE_PROTOCOL_ERROR;
    }
    if (result == HERMAS2_EDGE_OK && response.app_id != app_id) {
        result = HERMAS2_EDGE_WRONG_APP;
    }
    if (result != HERMAS2_EDGE_OK) {
        close(file_descriptor);
        return result;
    }
    edge->file_descriptor = file_descriptor;
    edge->app_id = app_id;
    memcpy(edge->contract_fingerprint, contract_fingerprint, 32u);
    return HERMAS2_EDGE_OK;
}

hermas2_edge_result hermas2_edge_serve_once(
    hermas2_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    uint8_t *result_buffer,
    size_t result_capacity,
    hermas2_action_handler handler,
    void *user_data) {
    if (edge == NULL || edge->file_descriptor < 0 ||
        packet_buffer == NULL ||
        packet_capacity < HERMAS2_PROTOCOL_HEADER_SIZE ||
        result_buffer == NULL || handler == NULL) {
        return HERMAS2_EDGE_INVALID_ARGUMENT;
    }
    size_t packet_size = 0u;
    hermas2_edge_result result = receive_packet(
        edge->file_descriptor, packet_buffer, packet_capacity, &packet_size);
    hermas2_frame request;
    if (result != HERMAS2_EDGE_OK) {
        return result;
    }
    if (hermas2_protocol_decode(packet_buffer, packet_size, &request) !=
            HERMAS2_PROTOCOL_OK ||
        request.kind != HERMAS2_FRAME_INVOKE) {
        return HERMAS2_EDGE_PROTOCOL_ERROR;
    }
    if (request.app_id != edge->app_id) {
        return HERMAS2_EDGE_WRONG_APP;
    }
    ++edge->delivered_invocations;
    uint16_t outcome = HERMAS2_OUTCOME_NONE;
    uint16_t result_type = 0u;
    size_t result_length = 0u;
    if (!handler(user_data, request.action_id, request.destination_type,
                 request.payload, request.payload_length, &outcome,
                 &result_type, result_buffer, result_capacity,
                 &result_length) ||
        result_length > result_capacity ||
        result_length > HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE ||
        (outcome != HERMAS2_OUTCOME_SUCCESS &&
         outcome != HERMAS2_OUTCOME_APP_ERROR) ||
        result_type == 0u) {
        return HERMAS2_EDGE_HANDLER_ERROR;
    }
    hermas2_frame response = {
        .kind = HERMAS2_FRAME_RESULT,
        .execution_id = request.execution_id,
        .request_id = request.request_id,
        .app_id = request.app_id,
        .action_id = request.action_id,
        .source_type = result_type,
        .destination_type = result_type,
        .outcome = outcome,
        .payload = result_buffer,
        .payload_length = (uint32_t)result_length
    };
    return send_frame(edge->file_descriptor, &response,
                      packet_buffer, packet_capacity);
}

void hermas2_edge_disconnect(hermas2_edge *edge) {
    if (edge != NULL) {
        if (edge->file_descriptor >= 0) {
            close(edge->file_descriptor);
        }
        memset(edge, 0, sizeof(*edge));
        edge->file_descriptor = -1;
    }
}

const char *hermas2_edge_result_name(hermas2_edge_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "path-too-long", "socket-error",
        "connect-error", "send-error", "receive-error", "truncated-packet",
        "protocol-error", "wrong-app", "handler-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
