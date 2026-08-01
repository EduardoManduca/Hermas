#define _POSIX_C_SOURCE 200809L

#include "hermas/journal_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_journal_linux: %s\n", message);
    return 1;
}

static hermas_journal_result count_record(
    void *context,
    const hermas_journal_record *record) {
    size_t *count = context;
    if (record->sequence != *count + 1u) {
        return HERMAS_JOURNAL_INVALID_SEQUENCE;
    }
    ++*count;
    return HERMAS_JOURNAL_OK;
}

int main(void) {
    char path[] = "/tmp/hermas-journal-XXXXXX";
    int temporary = mkstemp(path);
    if (temporary < 0) {
        return fail("cannot create temporary journal");
    }
    close(temporary);

    hermas_journal_file file;
    hermas_journal_summary summary;
    if (hermas_journal_file_open(&file, path, &summary) !=
            HERMAS_JOURNAL_OK ||
        summary.next_sequence != 1u ||
        summary.next_execution_id != 1u) {
        unlink(path);
        return fail("cannot open empty journal");
    }
    hermas_journal_file locked;
    hermas_journal_summary ignored;
    if (hermas_journal_file_open(&locked, path, &ignored) !=
        HERMAS_JOURNAL_WRITE_ERROR) {
        hermas_journal_file_close(&file);
        unlink(path);
        return fail("second writer acquired journal lock");
    }
    const uint64_t fingerprint = UINT64_C(0x123456789abcdef0);
    hermas_journal_record started = {
        .kind = HERMAS_JOURNAL_EXECUTION_STARTED,
        .outcome = HERMAS_OUTCOME_NONE,
        .execution_id = 41u,
        .workflow_id = 7u,
        .image_fingerprint = fingerprint
    };
    hermas_journal_record prepared = {
        .kind = HERMAS_JOURNAL_DELIVERY_PREPARED,
        .outcome = HERMAS_OUTCOME_NONE,
        .execution_id = 41u,
        .workflow_id = 7u,
        .request_id = 1u,
        .node_id = 2u,
        .app_id = 3u,
        .action_id = 4u,
        .image_fingerprint = fingerprint
    };
    hermas_journal_record sent = prepared;
    sent.kind = HERMAS_JOURNAL_DELIVERY_SENT;
    hermas_journal_record second = prepared;
    second.request_id = 2u;
    second.node_id = 5u;
    second.app_id = 6u;
    second.action_id = 7u;
    if (hermas_journal_writer_append(&file.writer, started) !=
            HERMAS_JOURNAL_OK ||
        hermas_journal_writer_append(&file.writer, prepared) !=
            HERMAS_JOURNAL_OK ||
        hermas_journal_writer_append(&file.writer, sent) !=
            HERMAS_JOURNAL_OK ||
        hermas_journal_writer_append(&file.writer, second) !=
            HERMAS_JOURNAL_OK) {
        hermas_journal_file_close(&file);
        unlink(path);
        return fail("cannot synchronize crash fixture");
    }
    hermas_journal_file_close(&file);

    if (hermas_journal_file_open(&file, path, &summary) !=
            HERMAS_JOURNAL_OK ||
        summary.record_count != 4u ||
        summary.interrupted_count != 1u ||
        summary.interrupted[0].open_delivery_count != 2u ||
        summary.interrupted[0].open_deliveries[0]
                .delivery_was_sent != 1u ||
        summary.interrupted[0].open_deliveries[1]
                .delivery_was_sent != 0u) {
        unlink(path);
        return fail("overlapping crash was not classified");
    }
    size_t closed = 0u;
    if (hermas_journal_file_close_interrupted(
            &file, &summary, &closed) != HERMAS_JOURNAL_OK ||
        closed != 1u) {
        hermas_journal_file_close(&file);
        unlink(path);
        return fail("interrupted execution was not closed");
    }
    hermas_journal_file_close(&file);

    if (hermas_journal_file_open(&file, path, &summary) !=
            HERMAS_JOURNAL_OK ||
        summary.record_count != 7u ||
        summary.interrupted_count != 0u ||
        summary.next_sequence != 8u ||
        summary.next_execution_id != 42u) {
        unlink(path);
        return fail("recovered journal did not validate");
    }
    hermas_journal_file_close(&file);

    size_t inspected = 0u;
    if (hermas_journal_file_inspect(
            path, count_record, &inspected, &summary) !=
            HERMAS_JOURNAL_OK ||
        inspected != 7u || summary.record_count != 7u) {
        unlink(path);
        return fail("history inspection did not visit every record");
    }

    FILE *corrupt = fopen(path, "ab");
    inspected = 0u;
    if (corrupt == NULL || fputc(0, corrupt) == EOF ||
        fclose(corrupt) != 0 ||
        hermas_journal_file_open(&file, path, &summary) !=
            HERMAS_JOURNAL_INVALID_SIZE ||
        hermas_journal_file_inspect(
            path, count_record, &inspected, &summary) !=
            HERMAS_JOURNAL_INVALID_SIZE ||
        inspected != 0u) {
        unlink(path);
        return fail("truncated journal was exposed to an inspector");
    }
    unlink(path);

    char target[] = "/tmp/hermas-journal-target-XXXXXX";
    int target_file = mkstemp(target);
    char link_path[] = "/tmp/hermas-journal-link-XXXXXX";
    int link_placeholder = mkstemp(link_path);
    if (target_file < 0 || link_placeholder < 0) {
        return fail("cannot create journal security fixture");
    }
    close(target_file);
    close(link_placeholder);
    unlink(link_path);
    if (symlink(target, link_path) != 0 ||
        hermas_journal_file_open(&file, link_path, &summary) ==
            HERMAS_JOURNAL_OK) {
        unlink(link_path);
        unlink(target);
        return fail("journal symlink was accepted");
    }
    unlink(link_path);
    if (chmod(target, 0644) != 0 ||
        hermas_journal_file_open(&file, target, &summary) ==
            HERMAS_JOURNAL_OK ||
        hermas_journal_file_inspect(
            target, NULL, NULL, &summary) ==
            HERMAS_JOURNAL_OK) {
        unlink(target);
        return fail("non-private journal was accepted");
    }
    unlink(target);
    puts("journal Linux durability tests passed");
    return 0;
}
