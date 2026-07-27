#ifndef HERMAS2_RESULT_LINUX_H
#define HERMAS2_RESULT_LINUX_H

#include "hermas2/result.h"

typedef struct hermas2_result_file {
    int file_descriptor;
    hermas2_result_writer writer;
} hermas2_result_file;

hermas2_result_store_result hermas2_result_file_open(
    hermas2_result_file *file,
    const char *path,
    hermas2_result_summary *summary);

hermas2_result_store_result hermas2_result_file_find(
    const hermas2_result_file *file,
    hermas2_result_key key,
    hermas2_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

hermas2_result_store_result hermas2_result_file_lookup(
    void *context,
    hermas2_result_key key,
    hermas2_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

void hermas2_result_file_close(hermas2_result_file *file);

#endif
