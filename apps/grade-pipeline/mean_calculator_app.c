#include "../example_app.h"
#include "mean-calculator.contract.h"

#include <stdint.h>

enum {
    MEAN_CALCULATOR_APP_ID = 2,
    CALCULATE_ACTION_ID = 2,
    MEAN_INPUT_TYPE_ID = 8,
    MEAN_TYPE_ID = 6
};

static const uint8_t CALCULATE_ACTION_FINGERPRINT[32] =
    HERMAS_MEAN_CALCULATOR_ACTION_CALCULATE_FINGERPRINT;

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static int64_t read_i64(const uint8_t *bytes) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return (int64_t)value;
}

static void write_i64(uint8_t *bytes, int64_t value) {
    uint64_t bits = (uint64_t)value;
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(bits >> (index * 8u));
    }
}

static int calculate_mean(
    void *user_data,
    uint16_t action_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint16_t *result_type,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length) {
    (void)user_data;
    if (action_id != CALCULATE_ACTION_ID ||
        input_type != MEAN_INPUT_TYPE_ID ||
        input_length != 32u || read_u32(input, 0u) != 3u ||
        read_u32(input, 4u) != 0u || result_capacity < 8u) {
        return 0;
    }
    int64_t total =
        read_i64(input + 8u) + read_i64(input + 16u) +
        read_i64(input + 24u);
    write_i64(result, total / 3);
    *outcome = HERMAS_OUTCOME_SUCCESS;
    *result_type = MEAN_TYPE_ID;
    *result_length = 8u;
    return 1;
}

int main(int argc, char **argv) {
    uint8_t result[8];
    return hermas_example_app_run_once(
        argc, argv, MEAN_CALCULATOR_APP_ID, CALCULATE_ACTION_ID,
        CALCULATE_ACTION_FINGERPRINT, calculate_mean,
        result, sizeof(result));
}
