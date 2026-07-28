#ifndef HERMAS_PROTOCOL_H
#define HERMAS_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define HERMAS_PROTOCOL_VERSION 1u
#define HERMAS_PROTOCOL_HEADER_SIZE 48u
#define HERMAS_PROTOCOL_MAX_PACKET_SIZE 65536u
#define HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE \
    (HERMAS_PROTOCOL_MAX_PACKET_SIZE - HERMAS_PROTOCOL_HEADER_SIZE)

typedef enum hermas_frame_kind {
    HERMAS_FRAME_REGISTER_APP = 1,
    HERMAS_FRAME_REGISTER_OK = 2,
    HERMAS_FRAME_INVOKE = 3,
    HERMAS_FRAME_RESULT = 4,
    HERMAS_FRAME_PROTOCOL_ERROR = 5,
    HERMAS_FRAME_EXECUTE = 6,
    HERMAS_FRAME_EXECUTION_RESULT = 7
} hermas_frame_kind;

typedef enum hermas_outcome {
    HERMAS_OUTCOME_NONE = 0,
    HERMAS_OUTCOME_SUCCESS = 1,
    HERMAS_OUTCOME_APP_ERROR = 2,
    HERMAS_OUTCOME_NOT_SENT = 3,
    HERMAS_OUTCOME_UNKNOWN = 4,
    HERMAS_OUTCOME_PROTOCOL_ERROR = 5
} hermas_outcome;

typedef enum hermas_protocol_result {
    HERMAS_PROTOCOL_OK = 0,
    HERMAS_PROTOCOL_BUFFER_TOO_SMALL,
    HERMAS_PROTOCOL_FRAME_TOO_LARGE,
    HERMAS_PROTOCOL_TRUNCATED,
    HERMAS_PROTOCOL_BAD_MAGIC,
    HERMAS_PROTOCOL_UNSUPPORTED_VERSION,
    HERMAS_PROTOCOL_INVALID_KIND,
    HERMAS_PROTOCOL_INVALID_FLAGS,
    HERMAS_PROTOCOL_NONZERO_RESERVED,
    HERMAS_PROTOCOL_LENGTH_MISMATCH,
    HERMAS_PROTOCOL_INVALID_IDENTIFIER,
    HERMAS_PROTOCOL_INVALID_OUTCOME,
    HERMAS_PROTOCOL_INVALID_PAYLOAD
} hermas_protocol_result;

typedef struct hermas_frame {
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
} hermas_frame;

hermas_protocol_result hermas_protocol_encode(
    const hermas_frame *frame,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size);

hermas_protocol_result hermas_protocol_decode(
    const uint8_t *packet,
    size_t packet_size,
    hermas_frame *frame);

const char *hermas_protocol_result_name(hermas_protocol_result result);

#endif
