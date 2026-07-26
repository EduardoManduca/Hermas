#ifndef HERMAS2_GRADE_PIPELINE_APP_COMMON_H
#define HERMAS2_GRADE_PIPELINE_APP_COMMON_H

#include "hermas2/edge.h"

int hermas2_example_app_run_once(
    int argc,
    char **argv,
    uint16_t app_id,
    hermas2_action_handler handler,
    uint8_t *result,
    size_t result_capacity);

#endif
