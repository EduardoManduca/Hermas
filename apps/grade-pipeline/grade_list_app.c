#include "app_common.h"

#include <stdint.h>
#include <string.h>

enum {
    GRADE_LIST_APP_ID = 1,
    GET_ACTION_ID = 1,
    EMPTY_TYPE_ID = 1,
    GRADES_TYPE_ID = 4
};

static void write_i64(uint8_t *bytes, int64_t value) {
    uint64_t bits = (uint64_t)value;
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(bits >> (index * 8u));
    }
}

static int get_grades(
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
    (void)input;
    if (action_id != GET_ACTION_ID ||
        input_type != EMPTY_TYPE_ID || input_length != 0u ||
        result_capacity < 32u) {
        return 0;
    }
    memset(result, 0, 32u);
    result[0] = 3u;
    write_i64(result + 8u, 70);
    write_i64(result + 16u, 80);
    write_i64(result + 24u, 90);
    *outcome = HERMAS2_OUTCOME_SUCCESS;
    *result_type = GRADES_TYPE_ID;
    *result_length = 32u;
    return 1;
}

int main(int argc, char **argv) {
    uint8_t result[32];
    return hermas2_example_app_run_once(
        argc, argv, GRADE_LIST_APP_ID, GET_ACTION_ID, get_grades,
        result, sizeof(result));
}
