#ifndef HERMAS2_PROTOCOL_H
#define HERMAS2_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define HERMAS2_PROTOCOL_VERSION 1u
#define HERMAS2_PROTOCOL_HEADER_SIZE 48u
#define HERMAS2_PROTOCOL_MAX_PACKET_SIZE 65536u
#define HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE \
    (HERMAS2_PROTOCOL_MAX_PACKET_SIZE - HERMAS2_PROTOCOL_HEADER_SIZE)

typedef enum hermas2_frame_kind {
    HERMAS2_FRAME_REGISTER_APP = 1,
    HERMAS2_FRAME_REGISTER_OK = 2,
    HERMAS2_FRAME_INVOKE = 3,
    HERMAS2_FRAME_RESULT = 4,
    HERMAS2_FRAME_PROTOCOL_ERROR = 5,
    HERMAS2_FRAME_EXECUTE = 6,
    HERMAS2_FRAME_EXECUTION_RESULT = 7
} hermas2_frame_kind;

typedef enum hermas2_outcome {
    HERMAS2_OUTCOME_NONE = 0,
    HERMAS2_OUTCOME_SUCCESS = 1,
    HERMAS2_OUTCOME_APP_ERROR = 2,
    HERMAS2_OUTCOME_NOT_SENT = 3,
    HERMAS2_OUTCOME_UNKNOWN = 4,
    HERMAS2_OUTCOME_PROTOCOL_ERROR = 5
} hermas2_outcome;

typedef enum hermas2_protocol_result {
    HERMAS2_PROTOCOL_OK = 0,
    HERMAS2_PROTOCOL_BUFFER_TOO_SMALL,
    HERMAS2_PROTOCOL_FRAME_TOO_LARGE,
    HERMAS2_PROTOCOL_TRUNCATED,
    HERMAS2_PROTOCOL_BAD_MAGIC,
    HERMAS2_PROTOCOL_UNSUPPORTED_VERSION,
    HERMAS2_PROTOCOL_INVALID_KIND,
    HERMAS2_PROTOCOL_INVALID_FLAGS,
    HERMAS2_PROTOCOL_NONZERO_RESERVED,
    HERMAS2_PROTOCOL_LENGTH_MISMATCH,
    HERMAS2_PROTOCOL_INVALID_IDENTIFIER,
    HERMAS2_PROTOCOL_INVALID_OUTCOME,
    HERMAS2_PROTOCOL_INVALID_PAYLOAD
} hermas2_protocol_result;

typedef struct hermas2_frame {
    uint16_t kind;
    uint64_t execution_id;
    uint64_t request_id;
    uint16_t app_id;
    uint16_t action_id;
    uint16_t source_type;
    uint16_t destination_type;
    uint16_t outcome;
    const uint8_t *payload;
    uint32_t payload_length;
} hermas2_frame;

hermas2_protocol_result hermas2_protocol_encode(
    const hermas2_frame *frame,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size);

hermas2_protocol_result hermas2_protocol_decode(
    const uint8_t *packet,
    size_t packet_size,
    hermas2_frame *frame);

const char *hermas2_protocol_result_name(hermas2_protocol_result result);

#endif
