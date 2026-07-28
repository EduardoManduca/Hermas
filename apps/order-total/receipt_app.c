#include "../example_app.h"

#include <stdint.h>
#include <stdio.h>

enum {
    RECEIPT_APP_ID = 3,
    ISSUE_ACTION_ID = 3,
    RECEIPT_INPUT_TYPE_ID = 9,
    ISSUED_TYPE_ID = 7
};

static int64_t read_i64(const uint8_t *bytes) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return (int64_t)value;
}

static int issue_receipt(
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
    if (action_id != ISSUE_ACTION_ID ||
        input_type != RECEIPT_INPUT_TYPE_ID || input_length != 8u ||
        result_capacity < 1u ||
        printf("Order total: %lld cents\n",
               (long long)read_i64(input)) < 0 ||
        fflush(stdout) != 0) {
        return 0;
    }
    result[0] = 1u;
    *outcome = HERMAS_OUTCOME_SUCCESS;
    *result_type = ISSUED_TYPE_ID;
    *result_length = 1u;
    return 1;
}

int main(int argc, char **argv) {
    uint8_t result[1];
    return hermas_example_app_run_once(
        argc, argv, RECEIPT_APP_ID, ISSUE_ACTION_ID, issue_receipt,
        result, sizeof(result));
}
