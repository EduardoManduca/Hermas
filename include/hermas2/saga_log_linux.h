#ifndef HERMAS2_SAGA_LOG_LINUX_H
#define HERMAS2_SAGA_LOG_LINUX_H

#include "hermas2/saga_log.h"

typedef struct hermas2_saga_log_file {
    int file_descriptor;
    hermas2_saga_log_writer writer;
} hermas2_saga_log_file;

hermas2_saga_log_result hermas2_saga_log_file_open(
    hermas2_saga_log_file *file,
    const char *path,
    hermas2_saga_log_summary *summary);

hermas2_saga_log_result hermas2_saga_log_file_scan(
    const hermas2_saga_log_file *file,
    hermas2_saga_log_summary *summary);

void hermas2_saga_log_file_close(
    hermas2_saga_log_file *file);

#endif
