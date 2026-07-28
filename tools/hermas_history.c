#define _POSIX_C_SOURCE 200809L

#include "hermas/journal_linux.h"
#include "hermas/version.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *kind_name(hermas_journal_kind kind) {
    static const char *const names[] = {
        "invalid", "execution-started", "delivery-prepared",
        "delivery-sent", "action-succeeded", "action-failed",
        "action-unknown", "execution-finished"
    };
    size_t index = (size_t)kind;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "invalid";
}

static hermas_journal_result print_record(
    void *context,
    const hermas_journal_record *record) {
    (void)context;
    printf(
        "%" PRIu64 "\t%" PRIu64 "\t%u\t%s\t%u\t%" PRIu64
        "\t%u\t%u\t%u\t%016" PRIx64 "\n",
        record->sequence, record->execution_id, record->workflow_id,
        kind_name(record->kind), record->outcome, record->request_id,
        record->node_id, record->app_id, record->action_id,
        record->image_fingerprint);
    return HERMAS_JOURNAL_OK;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf(
            "Hermas %s (hermas_history; journal %u)\n",
            HERMAS_VERSION, HERMAS_JOURNAL_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts(
            "usage: hermas_history FILE.hj\n\n"
            "Inspect a validated append-only execution journal.");
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: hermas_history FILE.hjournal\n");
        return 2;
    }
    puts("sequence\texecution\tworkflow\tkind\toutcome\trequest"
         "\tnode\tapp\taction\timage");
    hermas_journal_summary summary;
    hermas_journal_result result = hermas_journal_file_inspect(
        argv[1], print_record, NULL, &summary);
    if (result != HERMAS_JOURNAL_OK) {
        fprintf(stderr, "%s: %s\n", argv[1],
                hermas_journal_result_name(result));
        return 1;
    }
    fprintf(stderr,
            "records=%" PRIu64 " interrupted=%u next-execution=%" PRIu64
            "\n",
            summary.record_count, summary.interrupted_count,
            summary.next_execution_id);
    return 0;
}
