#ifndef HERMAS2_CONTROL_LINUX_H
#define HERMAS2_CONTROL_LINUX_H

#include "hermas2/control.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HERMAS2_CONTROL_MAX_CLIENTS HERMAS2_DAEMON_MAX_EXECUTIONS
#define HERMAS2_CONTROL_ACTIVE_QUANTUM_MS 10

typedef enum hermas2_control_server_result {
    HERMAS2_CONTROL_SERVER_OK = 0,
    HERMAS2_CONTROL_SERVER_INVALID_ARGUMENT,
    HERMAS2_CONTROL_SERVER_CAPACITY_EXHAUSTED,
    HERMAS2_CONTROL_SERVER_ACCEPT_ERROR,
    HERMAS2_CONTROL_SERVER_POLL_ERROR,
    HERMAS2_CONTROL_SERVER_LOOP_ERROR,
    HERMAS2_CONTROL_SERVER_STATE_ERROR
} hermas2_control_server_result;

typedef struct hermas2_control_client {
    int file_descriptor;
    uint64_t execution_id;
    bool active;
    bool admitted;
} hermas2_control_client;

typedef struct hermas2_control_server {
    hermas2_daemon_loop *loop;
    hermas2_control_client clients[HERMAS2_CONTROL_MAX_CLIENTS];
    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
} hermas2_control_server;

hermas2_control_server_result hermas2_control_server_init(
    hermas2_control_server *server,
    hermas2_daemon_loop *loop);

hermas2_control_server_result hermas2_control_server_attach(
    hermas2_control_server *server,
    int client);

hermas2_control_server_result hermas2_control_server_accept(
    hermas2_control_server *server,
    int listener);

hermas2_control_server_result hermas2_control_server_step(
    hermas2_control_server *server,
    int timeout_milliseconds,
    size_t *progress_count);

size_t hermas2_control_server_active(
    const hermas2_control_server *server);

void hermas2_control_server_close(
    hermas2_control_server *server);

const char *hermas2_control_server_result_name(
    hermas2_control_server_result result);

#endif
