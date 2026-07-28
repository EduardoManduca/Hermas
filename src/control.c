#include "hermas/control.h"

hermas_control_result hermas_control_submit(
    hermas_daemon_loop *loop,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t *execution_id) {
    if (loop == NULL || packet == NULL || execution_id == NULL) {
        return HERMAS_CONTROL_INVALID_ARGUMENT;
    }
    hermas_frame request;
    if (hermas_protocol_decode(packet, packet_size, &request) !=
            HERMAS_PROTOCOL_OK ||
        request.kind != HERMAS_FRAME_EXECUTE) {
        return HERMAS_CONTROL_PROTOCOL_ERROR;
    }
    hermas_loop_result admitted = hermas_daemon_loop_admit(
        loop, request.execution_id, request.source_type,
        request.payload, request.payload_length);
    if (admitted != HERMAS_LOOP_OK) {
        return HERMAS_CONTROL_ADMISSION_ERROR;
    }
    *execution_id = request.execution_id;
    return HERMAS_CONTROL_OK;
}

hermas_control_result hermas_control_collect(
    const hermas_daemon_loop *loop,
    uint64_t execution_id,
    uint8_t *packet,
    size_t packet_capacity,
    size_t *packet_size) {
    if (loop == NULL || execution_id == 0u || packet == NULL ||
        packet_size == NULL) {
        return HERMAS_CONTROL_INVALID_ARGUMENT;
    }
    *packet_size = 0u;
    hermas_frame result;
    hermas_loop_result available =
        hermas_daemon_loop_result(loop, execution_id, &result);
    if (available == HERMAS_LOOP_EXECUTION_ACTIVE) {
        return HERMAS_CONTROL_EXECUTION_ACTIVE;
    }
    if (available != HERMAS_LOOP_OK) {
        return HERMAS_CONTROL_ADMISSION_ERROR;
    }
    return hermas_protocol_encode(
               &result, packet, packet_capacity, packet_size) ==
                   HERMAS_PROTOCOL_OK
               ? HERMAS_CONTROL_OK
               : HERMAS_CONTROL_ENCODE_ERROR;
}

hermas_control_result hermas_control_release(
    hermas_daemon_loop *loop,
    uint64_t execution_id) {
    if (loop == NULL || execution_id == 0u) {
        return HERMAS_CONTROL_INVALID_ARGUMENT;
    }
    hermas_loop_result released =
        hermas_daemon_loop_release(loop, execution_id);
    if (released == HERMAS_LOOP_EXECUTION_ACTIVE) {
        return HERMAS_CONTROL_EXECUTION_ACTIVE;
    }
    return released == HERMAS_LOOP_OK
               ? HERMAS_CONTROL_OK
               : HERMAS_CONTROL_RELEASE_ERROR;
}

const char *hermas_control_result_name(
    hermas_control_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "protocol-error",
        "admission-error", "execution-active", "encode-error",
        "release-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
