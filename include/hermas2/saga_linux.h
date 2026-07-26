#ifndef HERMAS2_SAGA_LINUX_H
#define HERMAS2_SAGA_LINUX_H

#include "hermas2/compensation_linux.h"
#include "hermas2/journal_linux.h"
#include "hermas2/saga.h"
#include "hermas2/saga_log_linux.h"

hermas2_saga_result hermas2_saga_recover_files(
    hermas2_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    const hermas2_journal_file *journal,
    const hermas2_compensation_file *compensation,
    const hermas2_saga_log_file *saga_log,
    uint64_t execution_id,
    uint32_t workflow_id);

#endif
