#include "hermas/compensation.h"
#include "hermas/image.h"
#include "hermas/journal.h"
#include "hermas/protocol.h"
#include "hermas/result.h"
#include "hermas/saga_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static void fuzz_image(const uint8_t *bytes, size_t size) {
    hermas_image_summary summary;
    if (hermas_image_validate(bytes, size, &summary) !=
            HERMAS_IMAGE_OK ||
        summary.action_contract_count == 0u) {
        return;
    }
    size_t contracts = read_u32(
        bytes, HERMAS_IMAGE_HEADER_ACTION_CONTRACTS_OFFSET);
    uint8_t fingerprint[32];
    memcpy(fingerprint, bytes + contracts + 4u, 32u);
    hermas_image_action_contract contract;
    (void)hermas_image_find_action_contract(
        bytes, size, fingerprint, &contract);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    fuzz_image(data, size);
    if (size == 0u) {
        return 0;
    }
    const uint8_t *payload = data + 1u;
    size_t payload_size = size - 1u;
    switch (data[0] % 6u) {
        case 0u: {
            fuzz_image(payload, payload_size);
            break;
        }
        case 1u: {
            hermas_frame frame;
            (void)hermas_protocol_decode(
                payload, payload_size, &frame);
            break;
        }
        case 2u: {
            hermas_journal_summary summary;
            (void)hermas_journal_scan(
                payload, payload_size, NULL, NULL, &summary);
            break;
        }
        case 3u: {
            hermas_result_summary summary;
            (void)hermas_result_scan(
                payload, payload_size, NULL, NULL, &summary);
            break;
        }
        case 4u: {
            hermas_compensation_summary summary;
            (void)hermas_compensation_scan(
                payload, payload_size, NULL, NULL, &summary);
            break;
        }
        default: {
            hermas_saga_log_summary summary;
            (void)hermas_saga_log_scan(
                payload, payload_size, &summary);
            break;
        }
    }
    return 0;
}
