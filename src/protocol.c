#include "hermas/protocol.h"

#include <stdbool.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static void write_u16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    for (size_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void write_u64(uint8_t *bytes, size_t offset, uint64_t value) {
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static bool known_kind(uint16_t kind) {
    return kind >= HERMAS_FRAME_REGISTER_APP &&
           kind <= HERMAS_FRAME_EXECUTION_RESULT;
}

static bool valid_outcome(uint16_t outcome) {
    return outcome <= HERMAS_OUTCOME_PROTOCOL_ERROR;
}

static hermas_protocol_result validate_semantics(const hermas_frame *frame) {
    bool no_execution_ids =
        frame->execution_id == 0u && frame->request_id == 0u &&
        frame->source_type == 0u &&
                        frame->destination_type == 0u;
    bool routed = frame->execution_id != 0u && frame->request_id != 0u;
    switch (frame->kind) {
        case HERMAS_FRAME_REGISTER_APP:
            return no_execution_ids && frame->app_id != 0u &&
                           frame->action_id != 0u &&
                           frame->outcome == HERMAS_OUTCOME_NONE &&
                           frame->payload_length == 32u
                       ? HERMAS_PROTOCOL_OK
                       : HERMAS_PROTOCOL_INVALID_PAYLOAD;
        case HERMAS_FRAME_REGISTER_OK:
            return no_execution_ids && frame->app_id != 0u &&
                           frame->action_id != 0u &&
                           frame->outcome == HERMAS_OUTCOME_NONE &&
                           frame->payload_length == 0u
                       ? HERMAS_PROTOCOL_OK
                       : HERMAS_PROTOCOL_INVALID_IDENTIFIER;
        case HERMAS_FRAME_INVOKE:
            return routed && frame->app_id != 0u && frame->action_id != 0u &&
                           frame->source_type != 0u &&
                           frame->destination_type != 0u &&
                           frame->outcome == HERMAS_OUTCOME_NONE
                       ? HERMAS_PROTOCOL_OK
                       : HERMAS_PROTOCOL_INVALID_IDENTIFIER;
        case HERMAS_FRAME_RESULT:
            return routed && frame->app_id != 0u && frame->action_id != 0u &&
                           frame->source_type != 0u &&
                           frame->destination_type != 0u &&
                           (frame->outcome == HERMAS_OUTCOME_SUCCESS ||
                            frame->outcome == HERMAS_OUTCOME_APP_ERROR)
                       ? HERMAS_PROTOCOL_OK
                       : HERMAS_PROTOCOL_INVALID_OUTCOME;
        case HERMAS_FRAME_PROTOCOL_ERROR:
            return frame->outcome == HERMAS_OUTCOME_PROTOCOL_ERROR &&
                           frame->payload_length == 0u
                       ? HERMAS_PROTOCOL_OK
                       : HERMAS_PROTOCOL_INVALID_OUTCOME;
        case HERMAS_FRAME_EXECUTE:
            return frame->execution_id != 0u && frame->request_id == 0u &&
                           frame->app_id == 0u && frame->action_id == 0u &&
                           frame->source_type != 0u &&
                           frame->destination_type == 0u &&
                           frame->outcome == HERMAS_OUTCOME_NONE
                       ? HERMAS_PROTOCOL_OK
                       : HERMAS_PROTOCOL_INVALID_IDENTIFIER;
        case HERMAS_FRAME_EXECUTION_RESULT:
            return frame->execution_id != 0u && frame->request_id == 0u &&
                           frame->app_id == 0u && frame->action_id == 0u &&
                           (((frame->outcome == HERMAS_OUTCOME_SUCCESS ||
                              frame->outcome == HERMAS_OUTCOME_APP_ERROR) &&
                             frame->source_type != 0u &&
                             frame->destination_type != 0u) ||
                            ((frame->outcome == HERMAS_OUTCOME_NOT_SENT ||
                              frame->outcome == HERMAS_OUTCOME_UNKNOWN) &&
                             frame->source_type == 0u &&
                             frame->destination_type == 0u &&
                             frame->payload_length == 0u))
                       ? HERMAS_PROTOCOL_OK
                       : HERMAS_PROTOCOL_INVALID_OUTCOME;
        default:
            return HERMAS_PROTOCOL_INVALID_KIND;
    }
}

hermas_protocol_result hermas_protocol_encode(
    const hermas_frame *frame,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size) {
    if (frame == NULL || destination == NULL || encoded_size == NULL) {
        return HERMAS_PROTOCOL_BUFFER_TOO_SMALL;
    }
    *encoded_size = 0u;
    if (!known_kind(frame->kind)) {
        return HERMAS_PROTOCOL_INVALID_KIND;
    }
    if (!valid_outcome(frame->outcome)) {
        return HERMAS_PROTOCOL_INVALID_OUTCOME;
    }
    if (frame->payload_length > HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return HERMAS_PROTOCOL_FRAME_TOO_LARGE;
    }
    if (frame->payload_length != 0u && frame->payload == NULL) {
        return HERMAS_PROTOCOL_INVALID_PAYLOAD;
    }
    hermas_protocol_result semantic = validate_semantics(frame);
    if (semantic != HERMAS_PROTOCOL_OK) {
        return semantic;
    }
    size_t total = HERMAS_PROTOCOL_HEADER_SIZE + (size_t)frame->payload_length;
    if (destination_size < total) {
        return HERMAS_PROTOCOL_BUFFER_TOO_SMALL;
    }
    memset(destination, 0, HERMAS_PROTOCOL_HEADER_SIZE);
    memcpy(destination, "HRP1", 4u);
    write_u16(destination, 4u, HERMAS_PROTOCOL_VERSION);
    write_u16(destination, 6u, frame->kind);
    write_u32(destination, 8u, 0u);
    write_u32(destination, 12u, HERMAS_PROTOCOL_HEADER_SIZE);
    write_u64(destination, 16u, frame->execution_id);
    write_u64(destination, 24u, frame->request_id);
    write_u16(destination, 32u, frame->app_id);
    write_u16(destination, 34u, frame->action_id);
    write_u16(destination, 36u, frame->source_type);
    write_u16(destination, 38u, frame->destination_type);
    write_u16(destination, 40u, frame->outcome);
    write_u16(destination, 42u, 0u);
    write_u32(destination, 44u, frame->payload_length);
    if (frame->payload_length != 0u) {
        memcpy(destination + HERMAS_PROTOCOL_HEADER_SIZE,
               frame->payload, frame->payload_length);
    }
    *encoded_size = total;
    return HERMAS_PROTOCOL_OK;
}

hermas_protocol_result hermas_protocol_decode(
    const uint8_t *packet,
    size_t packet_size,
    hermas_frame *frame) {
    if (packet == NULL || frame == NULL ||
        packet_size < HERMAS_PROTOCOL_HEADER_SIZE) {
        return HERMAS_PROTOCOL_TRUNCATED;
    }
    if (packet_size > HERMAS_PROTOCOL_MAX_PACKET_SIZE) {
        return HERMAS_PROTOCOL_FRAME_TOO_LARGE;
    }
    if (memcmp(packet, "HRP1", 4u) != 0) {
        return HERMAS_PROTOCOL_BAD_MAGIC;
    }
    if (read_u16(packet, 4u) != HERMAS_PROTOCOL_VERSION) {
        return HERMAS_PROTOCOL_UNSUPPORTED_VERSION;
    }
    uint16_t kind = read_u16(packet, 6u);
    if (!known_kind(kind)) {
        return HERMAS_PROTOCOL_INVALID_KIND;
    }
    if (read_u32(packet, 8u) != 0u) {
        return HERMAS_PROTOCOL_INVALID_FLAGS;
    }
    if (read_u32(packet, 12u) != HERMAS_PROTOCOL_HEADER_SIZE ||
        read_u16(packet, 42u) != 0u) {
        return HERMAS_PROTOCOL_NONZERO_RESERVED;
    }
    uint32_t payload_length = read_u32(packet, 44u);
    if ((size_t)payload_length != packet_size - HERMAS_PROTOCOL_HEADER_SIZE) {
        return HERMAS_PROTOCOL_LENGTH_MISMATCH;
    }
    uint16_t outcome = read_u16(packet, 40u);
    if (!valid_outcome(outcome)) {
        return HERMAS_PROTOCOL_INVALID_OUTCOME;
    }
    hermas_frame decoded = {
        .kind = kind,
        .execution_id = read_u64(packet, 16u),
        .request_id = read_u64(packet, 24u),
        .app_id = read_u16(packet, 32u),
        .action_id = read_u16(packet, 34u),
        .source_type = read_u16(packet, 36u),
        .destination_type = read_u16(packet, 38u),
        .outcome = outcome,
        .payload = packet + HERMAS_PROTOCOL_HEADER_SIZE,
        .payload_length = payload_length
    };
    hermas_protocol_result semantic = validate_semantics(&decoded);
    if (semantic != HERMAS_PROTOCOL_OK) {
        return semantic;
    }
    *frame = decoded;
    return HERMAS_PROTOCOL_OK;
}

const char *hermas_protocol_result_name(hermas_protocol_result result) {
    static const char *const names[] = {
        "ok", "buffer-too-small", "frame-too-large", "truncated", "bad-magic",
        "unsupported-version", "invalid-kind", "invalid-flags",
        "nonzero-reserved", "length-mismatch", "invalid-identifier",
        "invalid-outcome", "invalid-payload"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
