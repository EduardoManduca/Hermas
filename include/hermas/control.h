#ifndef HERMAS_CONTROL_H
#define HERMAS_CONTROL_H

#include "hermas/daemon.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas_control_result {
    HERMAS_CONTROL_OK = 0,
    HERMAS_CONTROL_INVALID_ARGUMENT,
    HERMAS_CONTROL_PROTOCOL_ERROR,
    HERMAS_CONTROL_ADMISSION_ERROR,
    HERMAS_CONTROL_EXECUTION_ACTIVE,
    HERMAS_CONTROL_ENCODE_ERROR,
    HERMAS_CONTROL_RELEASE_ERROR
} hermas_control_result;

hermas_control_result hermas_control_submit(
    hermas_daemon_loop *loop,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t *execution_id);

hermas_control_result hermas_control_collect(
    const hermas_daemon_loop *loop,
    uint64_t execution_id,
    uint8_t *packet,
    size_t packet_capacity,
    size_t *packet_size);

hermas_control_result hermas_control_release(
    hermas_daemon_loop *loop,
    uint64_t execution_id);

const char *hermas_control_result_name(
    hermas_control_result result);

#endif
