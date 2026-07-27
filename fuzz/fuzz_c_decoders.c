#include "hermas2/compensation.h"
#include "hermas2/image.h"
#include "hermas2/journal.h"
#include "hermas2/protocol.h"
#include "hermas2/result.h"
#include "hermas2/saga_log.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0u) {
        return 0;
    }
    const uint8_t *payload = data + 1u;
    size_t payload_size = size - 1u;
    switch (data[0] % 6u) {
        case 0u: {
            (void)hermas2_image_validate(
                payload, payload_size, NULL);
            break;
        }
        case 1u: {
            hermas2_frame frame;
            (void)hermas2_protocol_decode(
                payload, payload_size, &frame);
            break;
        }
        case 2u: {
            hermas2_journal_summary summary;
            (void)hermas2_journal_scan(
                payload, payload_size, NULL, NULL, &summary);
            break;
        }
        case 3u: {
            hermas2_result_summary summary;
            (void)hermas2_result_scan(
                payload, payload_size, NULL, NULL, &summary);
            break;
        }
        case 4u: {
            hermas2_compensation_summary summary;
            (void)hermas2_compensation_scan(
                payload, payload_size, NULL, NULL, &summary);
            break;
        }
        default: {
            hermas2_saga_log_summary summary;
            (void)hermas2_saga_log_scan(
                payload, payload_size, &summary);
            break;
        }
    }
    return 0;
}
