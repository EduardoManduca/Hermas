#include "hermas2/control.h"

hermas2_control_result hermas2_control_submit(
    hermas2_daemon_loop *loop,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t *execution_id) {
    if (loop == NULL || packet == NULL || execution_id == NULL) {
        return HERMAS2_CONTROL_INVALID_ARGUMENT;
    }
    hermas2_frame request;
    if (hermas2_protocol_decode(packet, packet_size, &request) !=
            HERMAS2_PROTOCOL_OK ||
        request.kind != HERMAS2_FRAME_EXECUTE) {
        return HERMAS2_CONTROL_PROTOCOL_ERROR;
    }
    hermas2_loop_result admitted = hermas2_daemon_loop_admit(
        loop, request.execution_id, request.source_type,
        request.payload, request.payload_length);
    if (admitted != HERMAS2_LOOP_OK) {
        return HERMAS2_CONTROL_ADMISSION_ERROR;
    }
    *execution_id = request.execution_id;
    return HERMAS2_CONTROL_OK;
}

hermas2_control_result hermas2_control_collect(
    const hermas2_daemon_loop *loop,
    uint64_t execution_id,
    uint8_t *packet,
    size_t packet_capacity,
    size_t *packet_size) {
    if (loop == NULL || execution_id == 0u || packet == NULL ||
        packet_size == NULL) {
        return HERMAS2_CONTROL_INVALID_ARGUMENT;
    }
    *packet_size = 0u;
    hermas2_frame result;
    hermas2_loop_result available =
        hermas2_daemon_loop_result(loop, execution_id, &result);
    if (available == HERMAS2_LOOP_EXECUTION_ACTIVE) {
        return HERMAS2_CONTROL_EXECUTION_ACTIVE;
    }
    if (available != HERMAS2_LOOP_OK) {
        return HERMAS2_CONTROL_ADMISSION_ERROR;
    }
    return hermas2_protocol_encode(
               &result, packet, packet_capacity, packet_size) ==
                   HERMAS2_PROTOCOL_OK
               ? HERMAS2_CONTROL_OK
               : HERMAS2_CONTROL_ENCODE_ERROR;
}

hermas2_control_result hermas2_control_release(
    hermas2_daemon_loop *loop,
    uint64_t execution_id) {
    if (loop == NULL || execution_id == 0u) {
        return HERMAS2_CONTROL_INVALID_ARGUMENT;
    }
    hermas2_loop_result released =
        hermas2_daemon_loop_release(loop, execution_id);
    if (released == HERMAS2_LOOP_EXECUTION_ACTIVE) {
        return HERMAS2_CONTROL_EXECUTION_ACTIVE;
    }
    return released == HERMAS2_LOOP_OK
               ? HERMAS2_CONTROL_OK
               : HERMAS2_CONTROL_RELEASE_ERROR;
}

const char *hermas2_control_result_name(
    hermas2_control_result result) {
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
