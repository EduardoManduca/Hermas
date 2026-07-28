#ifndef HERMAS_CONTROL_LINUX_H
#define HERMAS_CONTROL_LINUX_H

#include "hermas/control.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HERMAS_CONTROL_MAX_CLIENTS HERMAS_DAEMON_MAX_EXECUTIONS
#define HERMAS_CONTROL_ACTIVE_QUANTUM_MS 10

typedef enum hermas_control_server_result {
    HERMAS_CONTROL_SERVER_OK = 0,
    HERMAS_CONTROL_SERVER_INVALID_ARGUMENT,
    HERMAS_CONTROL_SERVER_CAPACITY_EXHAUSTED,
    HERMAS_CONTROL_SERVER_ACCEPT_ERROR,
    HERMAS_CONTROL_SERVER_POLL_ERROR,
    HERMAS_CONTROL_SERVER_LOOP_ERROR,
    HERMAS_CONTROL_SERVER_STATE_ERROR
} hermas_control_server_result;

typedef struct hermas_control_client {
    int file_descriptor;
    uint64_t execution_id;
    bool active;
    bool admitted;
} hermas_control_client;

typedef struct hermas_control_server {
    hermas_daemon_loop *loop;
    hermas_control_client clients[HERMAS_CONTROL_MAX_CLIENTS];
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
} hermas_control_server;

hermas_control_server_result hermas_control_server_init(
    hermas_control_server *server,
    hermas_daemon_loop *loop);

hermas_control_server_result hermas_control_server_attach(
    hermas_control_server *server,
    int client);

hermas_control_server_result hermas_control_server_accept(
    hermas_control_server *server,
    int listener);

hermas_control_server_result hermas_control_server_step(
    hermas_control_server *server,
    int timeout_milliseconds,
    size_t *progress_count);

size_t hermas_control_server_active(
    const hermas_control_server *server);

void hermas_control_server_close(
    hermas_control_server *server);

const char *hermas_control_server_result_name(
    hermas_control_server_result result);

#endif
