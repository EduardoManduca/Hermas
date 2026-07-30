#ifndef HERMAS_EXAMPLE_APP_H
#define HERMAS_EXAMPLE_APP_H

#include "hermas/edge.h"

int hermas_example_app_run_once(
    int argc,
    char **argv,
    uint16_t app_id,
    uint16_t action_id,
    const uint8_t expected_fingerprint[32],
    hermas_action_handler handler,
    uint8_t *result,
    size_t result_capacity);

#endif
