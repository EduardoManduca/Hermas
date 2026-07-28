#ifndef HERMAS_RESULT_LINUX_H
#define HERMAS_RESULT_LINUX_H

#include "hermas/result.h"

typedef struct hermas_result_file {
    int file_descriptor;
    hermas_result_writer writer;
} hermas_result_file;

hermas_result_store_result hermas_result_file_open(
    hermas_result_file *file,
    const char *path,
    hermas_result_summary *summary);

hermas_result_store_result hermas_result_file_find(
    const hermas_result_file *file,
    hermas_result_key key,
    hermas_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

hermas_result_store_result hermas_result_file_lookup(
    void *context,
    hermas_result_key key,
    hermas_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found);

void hermas_result_file_close(hermas_result_file *file);

#endif
