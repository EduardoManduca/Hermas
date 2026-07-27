#ifndef HERMAS2_CLIENT_H
#define HERMAS2_CLIENT_H

#include "hermas2/protocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas2_client_result {
    HERMAS2_CLIENT_OK = 0,
    HERMAS2_CLIENT_INVALID_ARGUMENT,
    HERMAS2_CLIENT_SOCKET_ERROR,
    HERMAS2_CLIENT_CONNECT_ERROR,
    HERMAS2_CLIENT_ENCODE_ERROR,
    HERMAS2_CLIENT_SEND_ERROR,
    HERMAS2_CLIENT_RECEIVE_ERROR,
    HERMAS2_CLIENT_TRUNCATED_PACKET,
    HERMAS2_CLIENT_PROTOCOL_ERROR,
    HERMAS2_CLIENT_UNEXPECTED_RESULT
} hermas2_client_result;

typedef struct hermas2_client {
    int file_descriptor;
} hermas2_client;

hermas2_client_result hermas2_client_connect(
    hermas2_client *client,
    const char *socket_path);

hermas2_client_result hermas2_client_execute(
    hermas2_client *client,
    uint64_t execution_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    hermas2_frame *result);

void hermas2_client_close(hermas2_client *client);

const char *hermas2_client_result_name(
    hermas2_client_result result);

#endif
