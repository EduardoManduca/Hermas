#ifndef HERMAS2_COMPENSATION_LINUX_H
#define HERMAS2_COMPENSATION_LINUX_H

#include "hermas2/compensation.h"

typedef struct hermas2_compensation_file {
    int file_descriptor;
    hermas2_compensation_writer writer;
} hermas2_compensation_file;

hermas2_compensation_result hermas2_compensation_file_open(
    hermas2_compensation_file *file,
    const char *path,
    hermas2_compensation_summary *summary);

hermas2_compensation_result hermas2_compensation_file_find(
    const hermas2_compensation_file *file,
    hermas2_compensation_key key,
    hermas2_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found);

void hermas2_compensation_file_close(
    hermas2_compensation_file *file);

#endif
