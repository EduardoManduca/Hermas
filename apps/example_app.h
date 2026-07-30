#ifndef HERMAS_EXAMPLE_APP_H
#define HERMAS_EXAMPLE_APP_H

#include "hermas/edge.h"

typedef int (*hermas_example_action_handler)(
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length);

int hermas_example_app_run_once(
    int argc,
    char **argv,
    const uint8_t expected_fingerprint[32],
    hermas_example_action_handler handler,
    uint8_t *result,
    size_t result_capacity);

#endif
