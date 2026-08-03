#include "hermas/edge.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static hermas_edge_result send_frame(
    int file_descriptor,
    const hermas_frame *frame,
    uint8_t *packet,
    size_t capacity) {
    size_t packet_size = 0u;
    if (hermas_protocol_encode(frame, packet, capacity, &packet_size) !=
        HERMAS_PROTOCOL_OK) {
        return HERMAS_EDGE_PROTOCOL_ERROR;
    }
    ssize_t sent;
    do {
        sent = send(file_descriptor, packet, packet_size, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent == (ssize_t)packet_size ? HERMAS_EDGE_OK
                                        : HERMAS_EDGE_SEND_ERROR;
}

static hermas_edge_result receive_packet(
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
        return HERMAS_EDGE_RECEIVE_ERROR;
    }
    if ((message.msg_flags & MSG_TRUNC) != 0 ||
        (size_t)received > capacity) {
        return HERMAS_EDGE_TRUNCATED_PACKET;
    }
    *packet_size = (size_t)received;
    return HERMAS_EDGE_OK;
}

hermas_edge_result hermas_edge_connect(
    hermas_edge *edge,
    const char *socket_path,
    uint16_t app_id,
    uint16_t action_id,
    const uint8_t contract_fingerprint[32]) {
    if (edge == NULL || socket_path == NULL ||
        contract_fingerprint == NULL || app_id == 0u ||
        action_id == 0u) {
        return HERMAS_EDGE_INVALID_ARGUMENT;
    }
    size_t path_length = strlen(socket_path);
    struct sockaddr_un address;
    if (path_length == 0u || path_length >= sizeof(address.sun_path)) {
        return HERMAS_EDGE_PATH_TOO_LONG;
    }
    memset(edge, 0, sizeof(*edge));
    edge->file_descriptor = -1;
    int file_descriptor = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (file_descriptor < 0) {
        return HERMAS_EDGE_SOCKET_ERROR;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, path_length + 1u);
    if (connect(file_descriptor, (const struct sockaddr *)&address,
                sizeof(address)) != 0) {
        close(file_descriptor);
        return HERMAS_EDGE_CONNECT_ERROR;
    }
    uint8_t packet[HERMAS_PROTOCOL_HEADER_SIZE + 32u];
    hermas_frame registration = {
        .kind = HERMAS_FRAME_REGISTER_APP,
        .app_id = app_id,
        .action_id = action_id,
        .outcome = HERMAS_OUTCOME_NONE,
        .payload = contract_fingerprint,
        .payload_length = 32u
    };
    hermas_edge_result result =
        send_frame(file_descriptor, &registration, packet, sizeof(packet));
    size_t packet_size = 0u;
    hermas_frame response;
    if (result == HERMAS_EDGE_OK) {
        result = receive_packet(file_descriptor, packet, sizeof(packet),
                                &packet_size);
    }
    if (result == HERMAS_EDGE_OK &&
        (hermas_protocol_decode(packet, packet_size, &response) !=
             HERMAS_PROTOCOL_OK ||
         response.kind != HERMAS_FRAME_REGISTER_OK)) {
        result = HERMAS_EDGE_PROTOCOL_ERROR;
    }
    if (result == HERMAS_EDGE_OK &&
        (response.app_id != app_id ||
         response.action_id != action_id)) {
        result = HERMAS_EDGE_WRONG_APP;
    }
    if (result != HERMAS_EDGE_OK) {
        close(file_descriptor);
        return result;
    }
    edge->file_descriptor = file_descriptor;
    edge->app_id = app_id;
    edge->action_id = action_id;
    memcpy(edge->contract_fingerprint, contract_fingerprint, 32u);
    return HERMAS_EDGE_OK;
}

hermas_edge_result hermas_edge_serve_once(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    uint8_t *result_buffer,
    size_t result_capacity,
    hermas_action_handler handler,
    void *user_data) {
    if (edge == NULL || edge->file_descriptor < 0 ||
        packet_buffer == NULL ||
        packet_capacity < HERMAS_PROTOCOL_HEADER_SIZE ||
        result_buffer == NULL || handler == NULL) {
        return HERMAS_EDGE_INVALID_ARGUMENT;
    }
    hermas_edge_invocation invocation;
    const uint8_t *input = NULL;
    size_t input_length = 0u;
    hermas_edge_result received = hermas_edge_receive_invocation(
        edge, packet_buffer, packet_capacity, &invocation, &input,
        &input_length);
    if (received != HERMAS_EDGE_OK) {
        return received;
    }
    uint16_t outcome = HERMAS_OUTCOME_NONE;
    uint16_t result_type = 0u;
    size_t result_length = 0u;
    if (!handler(user_data, invocation.action_id, invocation.input_type,
                 input, input_length, &outcome,
                 &result_type, result_buffer, result_capacity,
                 &result_length) ||
        result_length > result_capacity ||
        result_length > HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE ||
        (outcome != HERMAS_OUTCOME_SUCCESS &&
         outcome != HERMAS_OUTCOME_APP_ERROR) ||
        result_type == 0u) {
        return HERMAS_EDGE_HANDLER_ERROR;
    }
    return hermas_edge_send_result(
        edge, packet_buffer, packet_capacity, &invocation, outcome,
        result_type, result_buffer, result_length);
}

hermas_edge_result hermas_edge_serve_many(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    uint8_t *result_buffer,
    size_t result_capacity,
    uint64_t invocation_count,
    hermas_action_handler handler,
    void *user_data) {
    if (invocation_count == 0u) {
        return HERMAS_EDGE_INVALID_ARGUMENT;
    }
    for (uint64_t index = 0u; index < invocation_count; ++index) {
        hermas_edge_result served = hermas_edge_serve_once(
            edge, packet_buffer, packet_capacity, result_buffer,
            result_capacity, handler, user_data);
        if (served != HERMAS_EDGE_OK) {
            return served;
        }
    }
    return HERMAS_EDGE_OK;
}

hermas_edge_result hermas_edge_receive_invocation(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    hermas_edge_invocation *invocation,
    const uint8_t **input,
    size_t *input_length) {
    if (edge == NULL || edge->file_descriptor < 0 ||
        packet_buffer == NULL ||
        packet_capacity < HERMAS_PROTOCOL_HEADER_SIZE ||
        invocation == NULL || input == NULL || input_length == NULL) {
        return HERMAS_EDGE_INVALID_ARGUMENT;
    }
    size_t packet_size = 0u;
    hermas_edge_result received = receive_packet(
        edge->file_descriptor, packet_buffer, packet_capacity, &packet_size);
    if (received != HERMAS_EDGE_OK) {
        return received;
    }
    hermas_frame request;
    if (hermas_protocol_decode(packet_buffer, packet_size, &request) !=
            HERMAS_PROTOCOL_OK ||
        request.kind != HERMAS_FRAME_INVOKE ||
        request.execution_id == 0u || request.request_id == 0u) {
        return HERMAS_EDGE_PROTOCOL_ERROR;
    }
    if (request.app_id != edge->app_id ||
        request.action_id != edge->action_id) {
        return HERMAS_EDGE_WRONG_APP;
    }
    *invocation = (hermas_edge_invocation){
        .execution_id = request.execution_id,
        .request_id = request.request_id,
        .action_id = request.action_id,
        .input_type = request.destination_type,
    };
    *input = request.payload;
    *input_length = request.payload_length;
    ++edge->delivered_invocations;
    return HERMAS_EDGE_OK;
}

hermas_edge_result hermas_edge_send_result(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    const hermas_edge_invocation *invocation,
    uint16_t outcome,
    uint16_t result_type,
    const uint8_t *result,
    size_t result_length) {
    if (edge == NULL || edge->file_descriptor < 0 ||
        packet_buffer == NULL ||
        packet_capacity < HERMAS_PROTOCOL_HEADER_SIZE ||
        invocation == NULL || invocation->execution_id == 0u ||
        invocation->request_id == 0u ||
        invocation->action_id != edge->action_id || result_type == 0u ||
        (outcome != HERMAS_OUTCOME_SUCCESS &&
         outcome != HERMAS_OUTCOME_APP_ERROR) ||
        result_length > HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE ||
        (result_length != 0u && result == NULL)) {
        return HERMAS_EDGE_INVALID_ARGUMENT;
    }
    hermas_frame response = {
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = invocation->execution_id,
        .request_id = invocation->request_id,
        .app_id = edge->app_id,
        .action_id = invocation->action_id,
        .source_type = result_type,
        .destination_type = result_type,
        .outcome = outcome,
        .payload = result,
        .payload_length = (uint32_t)result_length
    };
    return send_frame(edge->file_descriptor, &response,
                      packet_buffer, packet_capacity);
}

void hermas_edge_disconnect(hermas_edge *edge) {
    if (edge != NULL) {
        if (edge->file_descriptor >= 0) {
            close(edge->file_descriptor);
        }
        memset(edge, 0, sizeof(*edge));
        edge->file_descriptor = -1;
    }
}

const char *hermas_edge_result_name(hermas_edge_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "path-too-long", "socket-error",
        "connect-error", "send-error", "receive-error", "truncated-packet",
        "protocol-error", "wrong-app", "handler-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
