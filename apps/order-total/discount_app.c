#include "../example_app.h"

#include <stdint.h>

enum {
    DISCOUNT_APP_ID = 1,
    APPLY_ACTION_ID = 1,
    SUBTOTAL_TYPE_ID = 3,
    DISCOUNTED_TYPE_ID = 2
};

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

static int apply_discount(
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
    if (action_id != APPLY_ACTION_ID ||
        input_type != SUBTOTAL_TYPE_ID || input_length != 8u ||
        result_capacity < 8u) {
        return 0;
    }
    int64_t subtotal = read_i64(input);
    if (subtotal < 0) {
        return 0;
    }
    write_i64(result, subtotal * 9 / 10);
    *outcome = HERMAS_OUTCOME_SUCCESS;
    *result_type = DISCOUNTED_TYPE_ID;
    *result_length = 8u;
    return 1;
}

int main(int argc, char **argv) {
    uint8_t result[8];
    return hermas_example_app_run_once(
        argc, argv, DISCOUNT_APP_ID, APPLY_ACTION_ID, apply_discount,
        result, sizeof(result));
}
