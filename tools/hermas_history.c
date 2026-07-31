#define _POSIX_C_SOURCE 200809L

#include "hermas/journal_linux.h"
#include "hermas/version.h"
#include "hermas/workspace_linux.h"

#include <inttypes.h>
#include <stdbool.h>
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

static const char *outcome_name(uint16_t outcome) {
    static const char *const names[] = {
        "none", "success", "app-error", "not-sent", "unknown"
    };
    size_t index = (size_t)outcome;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "invalid";
}

static hermas_journal_result print_text_record(
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

static hermas_journal_result print_json_record(
    void *context,
    const hermas_journal_record *record) {
    (void)context;
    printf(
        "{\"format\":\"hermas-history-v2\",\"type\":\"record\","
        "\"sequence\":\"%" PRIu64 "\","
        "\"execution_id\":\"%" PRIu64 "\","
        "\"workflow_id\":%u,"
        "\"kind\":\"%s\",\"outcome\":\"%s\",",
        record->sequence, record->execution_id, record->workflow_id,
        kind_name(record->kind), outcome_name(record->outcome));
    if (record->request_id == 0u) {
        fputs(
            "\"request_id\":null,\"node_id\":null,"
            "\"app_id\":null,\"action_id\":null,",
            stdout);
    } else {
        printf(
            "\"request_id\":\"%" PRIu64 "\","
            "\"node_id\":%u,\"app_id\":%u,\"action_id\":%u,",
            record->request_id, record->node_id,
            record->app_id, record->action_id);
    }
    printf(
        "\"image_fingerprint\":\"%016" PRIx64 "\"}\n",
        record->image_fingerprint);
    return ferror(stdout) == 0
               ? HERMAS_JOURNAL_OK
               : HERMAS_JOURNAL_WRITE_ERROR;
}

static void print_json_summary(
    const hermas_journal_summary *summary,
    const hermas_workspace_binding *binding) {
    fputs(
        "{\"format\":\"hermas-history-v2\",\"type\":\"summary\",",
        stdout);
    printf(
        "\"journal_version\":%u,\"record_count\":\"%" PRIu64
        "\",\"next_execution_id\":\"%" PRIu64 "\",",
        HERMAS_JOURNAL_VERSION, summary->record_count,
        summary->next_execution_id);
    if (binding == NULL) {
        fputs("\"workspace\":null,", stdout);
    } else {
        printf(
            "\"workspace\":{\"manifest_version\":%u,"
            "\"workflow_id\":%u,"
            "\"image_fingerprint\":\"%016" PRIx64 "\"},",
            HERMAS_WORKSPACE_MANIFEST_VERSION,
            binding->workflow_id, binding->image_fingerprint);
    }
    fputs("\"interrupted\":[", stdout);
    for (size_t index = 0u;
         index < summary->interrupted_count; ++index) {
        const hermas_journal_interrupted *item =
            &summary->interrupted[index];
        if (index != 0u) {
            fputc(',', stdout);
        }
        printf(
            "{\"execution_id\":\"%" PRIu64 "\","
            "\"workflow_id\":%u,"
            "\"image_fingerprint\":\"%016" PRIx64 "\","
            "\"open_deliveries\":[",
            item->execution_id, item->workflow_id,
            item->image_fingerprint);
        for (size_t delivery = 0u;
             delivery < item->open_delivery_count; ++delivery) {
            const hermas_journal_open_delivery *open =
                &item->open_deliveries[delivery];
            if (delivery != 0u) {
                fputc(',', stdout);
            }
            printf(
                "{\"delivery_was_sent\":%s,"
                "\"request_id\":\"%" PRIu64 "\","
                "\"node_id\":%u,\"app_id\":%u,\"action_id\":%u}",
                open->delivery_was_sent != 0u ? "true" : "false",
                open->request_id, open->node_id,
                open->app_id, open->action_id);
        }
        fputs("]}", stdout);
    }
    fputs("]}\n", stdout);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf(
            "Hermas %s (hermas_history; journal %u, "
            "workspace-manifest %u)\n",
            HERMAS_VERSION, HERMAS_JOURNAL_VERSION,
            HERMAS_WORKSPACE_MANIFEST_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts(
            "usage: hermas_history FILE.hj\n"
            "       hermas_history --workspace DIRECTORY\n"
            "       hermas_history --json FILE.hj\n"
            "       hermas_history --json --workspace DIRECTORY\n\n"
            "Inspect a validated append-only execution journal. --json "
            "emits versioned JSON Lines.");
        return 0;
    }
    int cursor = 1;
    bool json = false;
    if (cursor < argc && strcmp(argv[cursor], "--json") == 0) {
        json = true;
        ++cursor;
    }
    bool workspace_mode =
        argc - cursor == 2 &&
        strcmp(argv[cursor], "--workspace") == 0;
    bool file_mode = argc - cursor == 1;
    if (!file_mode && !workspace_mode) {
        fprintf(
            stderr,
            "usage: hermas_history FILE.hj\n"
            "       hermas_history --workspace DIRECTORY\n"
            "       hermas_history --json FILE.hj\n"
            "       hermas_history --json --workspace DIRECTORY\n");
        return 2;
    }
    hermas_workspace_paths workspace;
    hermas_workspace_binding binding;
    const hermas_workspace_binding *json_binding = NULL;
    const char *path = argv[cursor];
    if (workspace_mode) {
        hermas_workspace_result opened =
            hermas_workspace_open(argv[cursor + 1], false, &workspace);
        if (opened != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "hermas_history: workspace error: %s\n",
                hermas_workspace_result_name(opened));
            return 2;
        }
        hermas_workspace_result loaded =
            hermas_workspace_load(&workspace, &binding);
        if (loaded != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr,
                "hermas_history: workspace binding failed: %s\n",
                hermas_workspace_result_name(loaded));
            return 2;
        }
        if (json) {
            json_binding = &binding;
        } else {
            fprintf(
                stderr,
                "workspace workflow=%u image=%016" PRIx64
                " manifest=%u\n",
                binding.workflow_id, binding.image_fingerprint,
                HERMAS_WORKSPACE_MANIFEST_VERSION);
        }
        path = workspace.journal_path;
    }
    if (!json) {
        puts("sequence\texecution\tworkflow\tkind\toutcome\trequest"
             "\tnode\tapp\taction\timage");
    }
    hermas_journal_summary summary;
    hermas_journal_result result = hermas_journal_file_inspect(
        path, json ? print_json_record : print_text_record,
        NULL, &summary);
    if (result != HERMAS_JOURNAL_OK) {
        fprintf(stderr, "%s: %s\n", path,
                hermas_journal_result_name(result));
        return 1;
    }
    if (json) {
        print_json_summary(&summary, json_binding);
        if (ferror(stdout) != 0) {
            fprintf(stderr, "hermas_history: output write failed\n");
            return 1;
        }
    } else {
        fprintf(
            stderr,
            "records=%" PRIu64 " interrupted=%u next-execution=%" PRIu64
            "\n",
            summary.record_count, summary.interrupted_count,
            summary.next_execution_id);
    }
    return 0;
}
