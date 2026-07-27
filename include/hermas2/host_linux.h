#ifndef HERMAS2_HOST_LINUX_H
#define HERMAS2_HOST_LINUX_H

#include "hermas2/compensation_linux.h"
#include "hermas2/control_linux.h"
#include "hermas2/journal_linux.h"
#include "hermas2/registration_linux.h"
#include "hermas2/result_linux.h"
#include "hermas2/saga_log_linux.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/un.h>

typedef enum hermas2_host_result {
    HERMAS2_HOST_OK = 0,
    HERMAS2_HOST_INVALID_ARGUMENT,
    HERMAS2_HOST_IMAGE_ERROR,
    HERMAS2_HOST_STATE_ERROR,
    HERMAS2_HOST_RECOVERY_REQUIRED,
    HERMAS2_HOST_SOCKET_ERROR,
    HERMAS2_HOST_REGISTRATION_ERROR,
    HERMAS2_HOST_CONTROL_ERROR,
    HERMAS2_HOST_POLL_ERROR
} hermas2_host_result;

typedef struct hermas2_host_config {
    const char *image_path;
    const char *state_directory;
    const char *app_socket_path;
    const char *control_socket_path;
    uint32_t workflow_id;
} hermas2_host_config;

typedef struct hermas2_host {
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
    uint64_t recovered_execution_ids[HERMAS2_SAGA_LOG_MAX_ACTIVE];
    uint8_t recovered_execution_count;
    hermas2_journal_file journal;
    hermas2_compensation_file compensation;
    hermas2_result_file results;
    hermas2_saga_log_file saga_log;
    hermas2_daemon_registry registry;
    hermas2_registration_server registration;
    hermas2_daemon_loop loop;
    hermas2_control_server control;
} hermas2_host;

hermas2_host_result hermas2_host_open(
    hermas2_host *host,
    const hermas2_host_config *config);

hermas2_host_result hermas2_host_step(
    hermas2_host *host,
    int timeout_milliseconds,
    size_t *progress_count);

void hermas2_host_close(hermas2_host *host);

const char *hermas2_host_result_name(hermas2_host_result result);

#endif
