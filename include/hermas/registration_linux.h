#ifndef HERMAS_REGISTRATION_LINUX_H
#define HERMAS_REGISTRATION_LINUX_H

#include "hermas/daemon.h"

#include <stdbool.h>
#include <stddef.h>

#define HERMAS_REGISTRATION_MAX_PENDING HERMAS_DAEMON_MAX_ACTIONS

typedef enum hermas_registration_server_result {
    HERMAS_REGISTRATION_SERVER_OK = 0,
    HERMAS_REGISTRATION_SERVER_INVALID_ARGUMENT,
    HERMAS_REGISTRATION_SERVER_CAPACITY_EXHAUSTED,
    HERMAS_REGISTRATION_SERVER_ACCEPT_ERROR,
    HERMAS_REGISTRATION_SERVER_POLL_ERROR,
    HERMAS_REGISTRATION_SERVER_STATE_ERROR
} hermas_registration_server_result;

typedef struct hermas_registration_client {
    int file_descriptor;
    size_t action_index;
    uint16_t registered_action_id;
    bool active;
    bool validated;
} hermas_registration_client;

typedef struct hermas_registration_server {
    hermas_daemon_registry *registry;
    hermas_registration_client
        clients[HERMAS_REGISTRATION_MAX_PENDING];
    uint8_t packet[HERMAS_PROTOCOL_HEADER_SIZE + 32u];
} hermas_registration_server;

hermas_registration_server_result hermas_registration_server_init(
    hermas_registration_server *server,
    hermas_daemon_registry *registry);

hermas_registration_server_result hermas_registration_server_attach(
    hermas_registration_server *server,
    int client_descriptor);

hermas_registration_server_result hermas_registration_server_accept(
    hermas_registration_server *server,
    int listener);

hermas_registration_server_result hermas_registration_server_step(
    hermas_registration_server *server,
    int timeout_milliseconds,
    size_t *progress_count);

size_t hermas_registration_server_pending(
    const hermas_registration_server *server);

void hermas_registration_server_close(
    hermas_registration_server *server);

const char *hermas_registration_server_result_name(
    hermas_registration_server_result result);

#endif
