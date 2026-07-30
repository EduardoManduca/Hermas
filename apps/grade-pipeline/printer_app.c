#include "../example_app.h"
#include "printer.contract.h"

#include <stdint.h>
#include <stdio.h>

static const uint8_t PRINT_ACTION_FINGERPRINT[32] =
    HERMAS_PRINTER_ACTION_PRINT_FINGERPRINT;

static int64_t read_i64(const uint8_t *bytes) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return (int64_t)value;
}

static int print_mean(
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length) {
    if (input_length != 8u || result_capacity < 1u ||
        printf("Mean: %lld\n", (long long)read_i64(input)) < 0 ||
        fflush(stdout) != 0) {
        return 0;
    }
    result[0] = 1u;
    *outcome = HERMAS_OUTCOME_SUCCESS;
    *result_length = 1u;
    return 1;
}

int main(int argc, char **argv) {
    uint8_t result[1];
    return hermas_example_app_run_once(
        argc, argv, PRINT_ACTION_FINGERPRINT, print_mean,
        result, sizeof(result));
}
