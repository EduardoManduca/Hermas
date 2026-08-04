#ifndef HERMAS_EDGE_H
#define HERMAS_EDGE_H

#include "hermas/protocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas_edge_result {
    HERMAS_EDGE_OK = 0,
    HERMAS_EDGE_INVALID_ARGUMENT,
    HERMAS_EDGE_PATH_TOO_LONG,
    HERMAS_EDGE_SOCKET_ERROR,
    HERMAS_EDGE_CONNECT_ERROR,
    HERMAS_EDGE_SEND_ERROR,
    HERMAS_EDGE_RECEIVE_ERROR,
    HERMAS_EDGE_TRUNCATED_PACKET,
    HERMAS_EDGE_PROTOCOL_ERROR,
    HERMAS_EDGE_WRONG_APP,
    HERMAS_EDGE_HANDLER_ERROR
} hermas_edge_result;

typedef struct hermas_edge {
    int file_descriptor;
    uint16_t app_id;
    uint16_t action_id;
    uint8_t contract_fingerprint[32];
    uint64_t delivered_invocations;
} hermas_edge;

typedef struct hermas_edge_invocation {
    uint64_t execution_id;
    uint64_t request_id;
    uint16_t action_id;
    uint16_t input_type;
} hermas_edge_invocation;

typedef int (*hermas_action_handler)(
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

hermas_edge_result hermas_edge_connect(
    hermas_edge *edge,
    const char *socket_path,
    uint16_t app_id,
    uint16_t action_id,
    const uint8_t contract_fingerprint[32]);

hermas_edge_result hermas_edge_serve_once(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    uint8_t *result_buffer,
    size_t result_capacity,
    hermas_action_handler handler,
    void *user_data);

hermas_edge_result hermas_edge_serve_many(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    uint8_t *result_buffer,
    size_t result_capacity,
    uint64_t invocation_count,
    hermas_action_handler handler,
    void *user_data);

hermas_edge_result hermas_edge_receive_invocation(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    hermas_edge_invocation *invocation,
    const uint8_t **input,
    size_t *input_length);

hermas_edge_result hermas_edge_send_result(
    hermas_edge *edge,
    uint8_t *packet_buffer,
    size_t packet_capacity,
    const hermas_edge_invocation *invocation,
    uint16_t outcome,
    uint16_t result_type,
    const uint8_t *result,
    size_t result_length);

void hermas_edge_disconnect(hermas_edge *edge);

const char *hermas_edge_result_name(hermas_edge_result result);

#endif
