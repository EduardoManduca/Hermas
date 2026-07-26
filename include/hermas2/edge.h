#ifndef HERMAS2_EDGE_H
#define HERMAS2_EDGE_H

#include "hermas2/protocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas2_edge_result {
    HERMAS2_EDGE_OK = 0,
    HERMAS2_EDGE_INVALID_ARGUMENT,
    HERMAS2_EDGE_PATH_TOO_LONG,
    HERMAS2_EDGE_SOCKET_ERROR,
    HERMAS2_EDGE_CONNECT_ERROR,
    HERMAS2_EDGE_SEND_ERROR,
    HERMAS2_EDGE_RECEIVE_ERROR,
    HERMAS2_EDGE_TRUNCATED_PACKET,
    HERMAS2_EDGE_PROTOCOL_ERROR,
    HERMAS2_EDGE_WRONG_APP,
    HERMAS2_EDGE_HANDLER_ERROR
} hermas2_edge_result;

typedef struct hermas2_edge {
    int file_descriptor;
    uint16_t app_id;
    uint8_t contract_fingerprint[32];
    uint64_t delivered_invocations;
} hermas2_edge;

typedef int (*hermas2_action_handler)(
    void *user_data,
    uint16_t action_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint16_t *result_type,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length);

hermas2_edge_result hermas2_edge_connect(
    hermas2_edge *edge,
    const char *socket_path,
    uint16_t app_id,
    const uint8_t contract_fingerprint[32]);

hermas2_edge_result hermas2_edge_serve_once(
    hermas2_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    uint8_t *result_buffer,
    size_t result_capacity,
    hermas2_action_handler handler,
    void *user_data);

void hermas2_edge_disconnect(hermas2_edge *edge);

const char *hermas2_edge_result_name(hermas2_edge_result result);

#endif
