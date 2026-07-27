#ifndef HERMAS2_REGISTRATION_LINUX_H
#define HERMAS2_REGISTRATION_LINUX_H

#include "hermas2/daemon.h"

#include <stdbool.h>
#include <stddef.h>

#define HERMAS2_REGISTRATION_MAX_PENDING HERMAS2_DAEMON_MAX_APPS

typedef enum hermas2_registration_server_result {
    HERMAS2_REGISTRATION_SERVER_OK = 0,
    HERMAS2_REGISTRATION_SERVER_INVALID_ARGUMENT,
    HERMAS2_REGISTRATION_SERVER_CAPACITY_EXHAUSTED,
    HERMAS2_REGISTRATION_SERVER_ACCEPT_ERROR,
    HERMAS2_REGISTRATION_SERVER_POLL_ERROR,
    HERMAS2_REGISTRATION_SERVER_STATE_ERROR
} hermas2_registration_server_result;

typedef struct hermas2_registration_client {
    int file_descriptor;
    size_t app_index;
    bool active;
    bool validated;
} hermas2_registration_client;

typedef struct hermas2_registration_server {
    hermas2_daemon_registry *registry;
    hermas2_registration_client
        clients[HERMAS2_REGISTRATION_MAX_PENDING];
    uint8_t packet[HERMAS2_PROTOCOL_HEADER_SIZE + 32u];
} hermas2_registration_server;

hermas2_registration_server_result hermas2_registration_server_init(
    hermas2_registration_server *server,
    hermas2_daemon_registry *registry);

hermas2_registration_server_result hermas2_registration_server_attach(
    hermas2_registration_server *server,
    int client_descriptor);

hermas2_registration_server_result hermas2_registration_server_accept(
    hermas2_registration_server *server,
    int listener);

hermas2_registration_server_result hermas2_registration_server_step(
    hermas2_registration_server *server,
    int timeout_milliseconds,
    size_t *progress_count);

size_t hermas2_registration_server_pending(
    const hermas2_registration_server *server);

void hermas2_registration_server_close(
    hermas2_registration_server *server);

const char *hermas2_registration_server_result_name(
    hermas2_registration_server_result result);

#endif
