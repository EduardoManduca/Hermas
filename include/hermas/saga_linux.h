#ifndef HERMAS_SAGA_LINUX_H
#define HERMAS_SAGA_LINUX_H

#include "hermas/compensation_linux.h"
#include "hermas/journal_linux.h"
#include "hermas/saga.h"
#include "hermas/saga_log_linux.h"

hermas_saga_result hermas_saga_recover_files(
    hermas_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    const hermas_journal_file *journal,
    const hermas_compensation_file *compensation,
    const hermas_saga_log_file *saga_log,
    uint64_t execution_id,
    uint32_t workflow_id);

#endif
