#ifndef HERMAS_JOURNAL_LINUX_H
#define HERMAS_JOURNAL_LINUX_H

#include "hermas/journal.h"

#include <stddef.h>
#include <stdint.h>

typedef struct hermas_journal_file {
    int file_descriptor;
    hermas_journal_writer writer;
} hermas_journal_file;

hermas_journal_result hermas_journal_file_open(
    hermas_journal_file *file,
    const char *path,
    hermas_journal_summary *summary);

hermas_journal_result hermas_journal_file_close_interrupted(
    hermas_journal_file *file,
    const hermas_journal_summary *summary,
    size_t *closed_count);

hermas_journal_result hermas_journal_file_inspect(
    const char *path,
    hermas_journal_visitor visitor,
    void *visitor_context,
    hermas_journal_summary *summary);

void hermas_journal_file_close(
    hermas_journal_file *file);

#endif
