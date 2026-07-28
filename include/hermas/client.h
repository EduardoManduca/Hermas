#ifndef HERMAS_CLIENT_H
#define HERMAS_CLIENT_H

#include "hermas/protocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas_client_result {
    HERMAS_CLIENT_OK = 0,
    HERMAS_CLIENT_INVALID_ARGUMENT,
    HERMAS_CLIENT_SOCKET_ERROR,
    HERMAS_CLIENT_CONNECT_ERROR,
    HERMAS_CLIENT_ENCODE_ERROR,
    HERMAS_CLIENT_SEND_ERROR,
    HERMAS_CLIENT_RECEIVE_ERROR,
    HERMAS_CLIENT_TRUNCATED_PACKET,
    HERMAS_CLIENT_PROTOCOL_ERROR,
    HERMAS_CLIENT_UNEXPECTED_RESULT
} hermas_client_result;

typedef struct hermas_client {
    int file_descriptor;
} hermas_client;

hermas_client_result hermas_client_connect(
    hermas_client *client,
    const char *socket_path);

hermas_client_result hermas_client_execute(
    hermas_client *client,
    uint64_t execution_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    hermas_frame *result);

void hermas_client_close(hermas_client *client);

const char *hermas_client_result_name(
    hermas_client_result result);

#endif
