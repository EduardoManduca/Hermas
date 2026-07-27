#ifndef HERMAS2_CONTROL_H
#define HERMAS2_CONTROL_H

#include "hermas2/daemon.h"

#include <stddef.h>
#include <stdint.h>

typedef enum hermas2_control_result {
    HERMAS2_CONTROL_OK = 0,
    HERMAS2_CONTROL_INVALID_ARGUMENT,
    HERMAS2_CONTROL_PROTOCOL_ERROR,
    HERMAS2_CONTROL_ADMISSION_ERROR,
    HERMAS2_CONTROL_EXECUTION_ACTIVE,
    HERMAS2_CONTROL_ENCODE_ERROR,
    HERMAS2_CONTROL_RELEASE_ERROR
} hermas2_control_result;

hermas2_control_result hermas2_control_submit(
    hermas2_daemon_loop *loop,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t *execution_id);

hermas2_control_result hermas2_control_collect(
    const hermas2_daemon_loop *loop,
    uint64_t execution_id,
    uint8_t *packet,
    size_t packet_capacity,
    size_t *packet_size);

hermas2_control_result hermas2_control_release(
    hermas2_daemon_loop *loop,
    uint64_t execution_id);

const char *hermas2_control_result_name(
    hermas2_control_result result);

#endif
