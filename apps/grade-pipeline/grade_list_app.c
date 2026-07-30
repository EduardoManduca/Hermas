#include "../example_app.h"
#include "grade-list.contract.h"

#include <stdint.h>
#include <string.h>

static const uint8_t GET_ACTION_FINGERPRINT[32] =
    HERMAS_GRADE_LIST_ACTION_GET_FINGERPRINT;

static void write_i64(uint8_t *bytes, int64_t value) {
    uint64_t bits = (uint64_t)value;
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(bits >> (index * 8u));
    }
}

static int get_grades(
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length) {
    (void)input;
    if (input_length != 0u || result_capacity < 32u) {
        return 0;
    }
    memset(result, 0, 32u);
    result[0] = 3u;
    write_i64(result + 8u, 70);
    write_i64(result + 16u, 80);
    write_i64(result + 24u, 90);
    *outcome = HERMAS_OUTCOME_SUCCESS;
    *result_length = 32u;
    return 1;
}

int main(int argc, char **argv) {
    uint8_t result[32];
    return hermas_example_app_run_once(
        argc, argv, GET_ACTION_FINGERPRINT, get_grades,
        result, sizeof(result));
}
