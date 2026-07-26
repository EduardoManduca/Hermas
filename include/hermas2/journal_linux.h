#ifndef HERMAS2_JOURNAL_LINUX_H
#define HERMAS2_JOURNAL_LINUX_H

#include "hermas2/journal.h"

#include <stddef.h>
#include <stdint.h>

typedef struct hermas2_journal_file {
    int file_descriptor;
    hermas2_journal_writer writer;
} hermas2_journal_file;

hermas2_journal_result hermas2_journal_file_open(
    hermas2_journal_file *file,
    const char *path,
    hermas2_journal_summary *summary);

hermas2_journal_result hermas2_journal_file_close_interrupted(
    hermas2_journal_file *file,
    const hermas2_journal_summary *summary,
    size_t *closed_count);

hermas2_journal_result hermas2_journal_file_inspect(
    const char *path,
    hermas2_journal_visitor visitor,
    void *visitor_context,
    hermas2_journal_summary *summary);

void hermas2_journal_file_close(
    hermas2_journal_file *file);

#endif
