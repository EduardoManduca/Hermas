#define _POSIX_C_SOURCE 200809L

#include "hermas2/journal_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_journal_linux: %s\n", message);
    return 1;
}

static hermas2_journal_result count_record(
    void *context,
    const hermas2_journal_record *record) {
    size_t *count = context;
    if (record->sequence != *count + 1u) {
        return HERMAS2_JOURNAL_INVALID_SEQUENCE;
    }
    ++*count;
    return HERMAS2_JOURNAL_OK;
}

int main(void) {
    char path[] = "/tmp/hermas2-journal-XXXXXX";
    int temporary = mkstemp(path);
    if (temporary < 0) {
        return fail("cannot create temporary journal");
    }
    close(temporary);

    hermas2_journal_file file;
    hermas2_journal_summary summary;
    if (hermas2_journal_file_open(&file, path, &summary) !=
            HERMAS2_JOURNAL_OK ||
        summary.next_sequence != 1u ||
        summary.next_execution_id != 1u) {
        unlink(path);
        return fail("cannot open empty journal");
    }
    hermas2_journal_file locked;
    hermas2_journal_summary ignored;
    if (hermas2_journal_file_open(&locked, path, &ignored) !=
        HERMAS2_JOURNAL_WRITE_ERROR) {
        hermas2_journal_file_close(&file);
        unlink(path);
        return fail("second writer acquired journal lock");
    }
    const uint64_t fingerprint = UINT64_C(0x123456789abcdef0);
    hermas2_journal_record started = {
        .kind = HERMAS2_JOURNAL_EXECUTION_STARTED,
        .outcome = HERMAS2_OUTCOME_NONE,
        .execution_id = 41u,
        .workflow_id = 7u,
        .image_fingerprint = fingerprint
    };
    hermas2_journal_record prepared = {
        .kind = HERMAS2_JOURNAL_DELIVERY_PREPARED,
        .outcome = HERMAS2_OUTCOME_NONE,
        .execution_id = 41u,
        .workflow_id = 7u,
        .request_id = 1u,
        .node_id = 2u,
        .app_id = 3u,
        .action_id = 4u,
        .image_fingerprint = fingerprint
    };
    if (hermas2_journal_writer_append(&file.writer, started) !=
            HERMAS2_JOURNAL_OK ||
        hermas2_journal_writer_append(&file.writer, prepared) !=
            HERMAS2_JOURNAL_OK) {
        hermas2_journal_file_close(&file);
        unlink(path);
        return fail("cannot synchronize crash fixture");
    }
    hermas2_journal_file_close(&file);

    if (hermas2_journal_file_open(&file, path, &summary) !=
            HERMAS2_JOURNAL_OK ||
        summary.record_count != 2u ||
        summary.interrupted_count != 1u ||
        summary.interrupted[0].has_open_delivery != 1u ||
        summary.interrupted[0].delivery_was_sent != 0u) {
        unlink(path);
        return fail("prepared crash was not classified");
    }
    size_t closed = 0u;
    if (hermas2_journal_file_close_interrupted(
            &file, &summary, &closed) != HERMAS2_JOURNAL_OK ||
        closed != 1u) {
        hermas2_journal_file_close(&file);
        unlink(path);
        return fail("interrupted execution was not closed");
    }
    hermas2_journal_file_close(&file);

    if (hermas2_journal_file_open(&file, path, &summary) !=
            HERMAS2_JOURNAL_OK ||
        summary.record_count != 4u ||
        summary.interrupted_count != 0u ||
        summary.next_sequence != 5u ||
        summary.next_execution_id != 42u) {
        unlink(path);
        return fail("recovered journal did not validate");
    }
    hermas2_journal_file_close(&file);

    size_t inspected = 0u;
    if (hermas2_journal_file_inspect(
            path, count_record, &inspected, &summary) !=
            HERMAS2_JOURNAL_OK ||
        inspected != 4u || summary.record_count != 4u) {
        unlink(path);
        return fail("history inspection did not visit every record");
    }

    FILE *corrupt = fopen(path, "ab");
    if (corrupt == NULL || fputc(0, corrupt) == EOF ||
        fclose(corrupt) != 0 ||
        hermas2_journal_file_open(&file, path, &summary) !=
            HERMAS2_JOURNAL_INVALID_SIZE) {
        unlink(path);
        return fail("truncated journal was accepted");
    }
    unlink(path);
    puts("journal Linux durability tests passed");
    return 0;
}
