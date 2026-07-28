#include "hermas/protocol.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "test_protocol: %s\n", message);
    return 1;
}

static int round_trip(const hermas_frame *source) {
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    size_t size = 0u;
    hermas_frame decoded;
    if (hermas_protocol_encode(source, packet, sizeof(packet), &size) !=
            HERMAS_PROTOCOL_OK ||
        hermas_protocol_decode(packet, size, &decoded) != HERMAS_PROTOCOL_OK) {
        return 0;
    }
    return decoded.kind == source->kind &&
           decoded.execution_id == source->execution_id &&
           decoded.request_id == source->request_id &&
           decoded.app_id == source->app_id &&
           decoded.action_id == source->action_id &&
           decoded.source_type == source->source_type &&
           decoded.destination_type == source->destination_type &&
           decoded.outcome == source->outcome &&
           decoded.payload_length == source->payload_length &&
           (source->payload_length == 0u ||
            memcmp(decoded.payload, source->payload, source->payload_length) == 0);
}

int main(void) {
    uint8_t fingerprint[32] = {1u};
    uint8_t payload[16] = {2u, 3u, 5u, 7u};
    hermas_frame frames[] = {
        {HERMAS_FRAME_REGISTER_APP, 0u, 0u, 1u, 2u, 0u, 0u,
         HERMAS_OUTCOME_NONE, fingerprint, sizeof(fingerprint)},
        {HERMAS_FRAME_REGISTER_OK, 0u, 0u, 1u, 2u, 0u, 0u,
         HERMAS_OUTCOME_NONE, NULL, 0u},
        {HERMAS_FRAME_INVOKE, 10u, 20u, 1u, 2u, 3u, 4u,
         HERMAS_OUTCOME_NONE, payload, sizeof(payload)},
        {HERMAS_FRAME_RESULT, 10u, 20u, 1u, 2u, 4u, 5u,
         HERMAS_OUTCOME_SUCCESS, payload, sizeof(payload)},
        {HERMAS_FRAME_EXECUTE, 10u, 0u, 0u, 0u, 3u, 0u,
         HERMAS_OUTCOME_NONE, payload, sizeof(payload)},
        {HERMAS_FRAME_EXECUTION_RESULT, 10u, 0u, 0u, 0u, 3u, 5u,
         HERMAS_OUTCOME_SUCCESS, payload, sizeof(payload)}
    };
    for (size_t index = 0u; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        if (!round_trip(&frames[index])) {
            return fail("valid frame did not round trip");
        }
    }

    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    size_t size = 0u;
    if (hermas_protocol_encode(&frames[2], packet, sizeof(packet), &size) !=
        HERMAS_PROTOCOL_OK) {
        return fail("cannot create malformed corpus base");
    }
    for (size_t prefix = 0u; prefix < size; ++prefix) {
        hermas_frame decoded;
        if (hermas_protocol_decode(packet, prefix, &decoded) ==
            HERMAS_PROTOCOL_OK) {
            return fail("truncated prefix was accepted");
        }
    }
    const size_t mutations[] = {0u, 4u, 6u, 8u, 12u, 42u, 44u};
    for (size_t index = 0u; index < sizeof(mutations) / sizeof(mutations[0]); ++index) {
        size_t offset = mutations[index];
        uint8_t original = packet[offset];
        packet[offset] ^= 0xffu;
        hermas_frame decoded;
        if (hermas_protocol_decode(packet, size, &decoded) ==
            HERMAS_PROTOCOL_OK) {
            return fail("malformed header was accepted");
        }
        packet[offset] = original;
    }
    hermas_frame invalid = frames[2];
    invalid.request_id = 0u;
    if (hermas_protocol_encode(&invalid, packet, sizeof(packet), &size) ==
        HERMAS_PROTOCOL_OK) {
        return fail("invalid routed identifiers were accepted");
    }
    invalid = frames[3];
    invalid.outcome = HERMAS_OUTCOME_UNKNOWN;
    if (hermas_protocol_encode(&invalid, packet, sizeof(packet), &size) ==
        HERMAS_PROTOCOL_OK) {
        return fail("invalid app result outcome was accepted");
    }
    return 0;
}
