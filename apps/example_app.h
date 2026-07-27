#ifndef HERMAS2_EXAMPLE_APP_H
#define HERMAS2_EXAMPLE_APP_H

#include "hermas2/edge.h"

int hermas2_example_app_run_once(
    int argc,
    char **argv,
    uint16_t app_id,
    uint16_t action_id,
    hermas2_action_handler handler,
    uint8_t *result,
    size_t result_capacity);

#endif
