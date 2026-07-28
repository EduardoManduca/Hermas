#ifndef HERMAS_SAGA_LOG_LINUX_H
#define HERMAS_SAGA_LOG_LINUX_H

#include "hermas/saga_log.h"

typedef struct hermas_saga_log_file {
    int file_descriptor;
    hermas_saga_log_writer writer;
} hermas_saga_log_file;

hermas_saga_log_result hermas_saga_log_file_open(
    hermas_saga_log_file *file,
    const char *path,
    hermas_saga_log_summary *summary);

hermas_saga_log_result hermas_saga_log_file_scan(
    const hermas_saga_log_file *file,
    hermas_saga_log_summary *summary);

void hermas_saga_log_file_close(
    hermas_saga_log_file *file);

#endif
