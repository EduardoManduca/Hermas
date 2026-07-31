#ifndef HERMAS_HOST_LINUX_H
#define HERMAS_HOST_LINUX_H

#include "hermas/compensation_linux.h"
#include "hermas/control_linux.h"
#include "hermas/journal_linux.h"
#include "hermas/registration_linux.h"
#include "hermas/result_linux.h"
#include "hermas/saga_log_linux.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/un.h>

typedef enum hermas_host_result {
    HERMAS_HOST_OK = 0,
    HERMAS_HOST_INVALID_ARGUMENT,
    HERMAS_HOST_IMAGE_ERROR,
    HERMAS_HOST_STATE_ERROR,
    HERMAS_HOST_RECOVERY_REQUIRED,
    HERMAS_HOST_SOCKET_ERROR,
    HERMAS_HOST_REGISTRATION_ERROR,
    HERMAS_HOST_CONTROL_ERROR,
    HERMAS_HOST_POLL_ERROR,
    HERMAS_HOST_UNSUPPORTED_GRAPH
} hermas_host_result;

typedef struct hermas_host_config {
    const char *image_path;
    const char *state_directory;
    const char *app_socket_path;
    const char *control_socket_path;
    uint32_t workflow_id;
} hermas_host_config;

typedef struct hermas_host {
    int image_descriptor;
    const uint8_t *image;
    size_t image_size;
    int app_listener;
    int control_listener;
    char app_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char control_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    bool app_socket_bound;
    bool control_socket_bound;
    bool registry_initialized;
    bool registration_initialized;
    bool control_initialized;
    bool journal_open;
    bool compensation_open;
    bool result_open;
    bool saga_log_open;
    uint64_t next_execution_id;
    uint64_t recovered_execution_ids[HERMAS_SAGA_LOG_MAX_ACTIVE];
    uint8_t recovered_execution_count;
    hermas_journal_file journal;
    hermas_compensation_file compensation;
    hermas_result_file results;
    hermas_saga_log_file saga_log;
    hermas_daemon_registry registry;
    hermas_registration_server registration;
    hermas_daemon_loop loop;
    hermas_control_server control;
} hermas_host;

hermas_host_result hermas_host_open(
    hermas_host *host,
    const hermas_host_config *config);

hermas_host_result hermas_host_step(
    hermas_host *host,
    int timeout_milliseconds,
    size_t *progress_count);

void hermas_host_close(hermas_host *host);

const char *hermas_host_result_name(hermas_host_result result);

#endif
