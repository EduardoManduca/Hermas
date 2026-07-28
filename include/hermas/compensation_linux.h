#ifndef HERMAS_COMPENSATION_LINUX_H
#define HERMAS_COMPENSATION_LINUX_H

#include "hermas/compensation.h"

typedef struct hermas_compensation_file {
    int file_descriptor;
    hermas_compensation_writer writer;
} hermas_compensation_file;

hermas_compensation_result hermas_compensation_file_open(
    hermas_compensation_file *file,
    const char *path,
    hermas_compensation_summary *summary);

hermas_compensation_result hermas_compensation_file_find(
    const hermas_compensation_file *file,
    hermas_compensation_key key,
    hermas_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found);

hermas_compensation_result hermas_compensation_file_lookup(
    void *context,
    hermas_compensation_key key,
    hermas_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found);

void hermas_compensation_file_close(
    hermas_compensation_file *file);

#endif
