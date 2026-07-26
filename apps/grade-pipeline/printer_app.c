#include "app_common.h"

#include <stdint.h>
#include <stdio.h>

enum {
    PRINTER_APP_ID = 3,
    PRINT_ACTION_ID = 3,
    PRINT_INPUT_TYPE_ID = 10,
    PRINTED_TYPE_ID = 11
};

static int64_t read_i64(const uint8_t *bytes) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return (int64_t)value;
}

static int print_mean(
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
    if (action_id != PRINT_ACTION_ID ||
        input_type != PRINT_INPUT_TYPE_ID ||
        input_length != 8u || result_capacity < 1u ||
        printf("Mean: %lld\n", (long long)read_i64(input)) < 0 ||
        fflush(stdout) != 0) {
        return 0;
    }
    result[0] = 1u;
    *outcome = HERMAS2_OUTCOME_SUCCESS;
    *result_type = PRINTED_TYPE_ID;
    *result_length = 1u;
    return 1;
}

int main(int argc, char **argv) {
    uint8_t result[1];
    return hermas2_example_app_run_once(
        argc, argv, PRINTER_APP_ID, print_mean,
        result, sizeof(result));
}
