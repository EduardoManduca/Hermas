#define _POSIX_C_SOURCE 200809L

#include "hermas2/saga_log_linux.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_saga_log_linux: %s\n", message);
    return 1;
}

static hermas2_saga_log_record started(void) {
    return (hermas2_saga_log_record){
        .kind = HERMAS2_SAGA_LOG_STARTED,
        .outcome = HERMAS2_OUTCOME_APP_ERROR,
        .execution_id = 9u,
        .workflow_id = 3u,
        .ordinal = 2u,
        .image_fingerprint = UINT64_C(0x1122334455667788)
    };
}

int main(void) {
    char path[] = "/tmp/hermas2-saga-log-XXXXXX";
    int temporary = mkstemp(path);
    if (temporary < 0 || close(temporary) != 0) {
        return fail("cannot create saga log");
    }
    hermas2_saga_log_file file;
    hermas2_saga_log_summary summary;
    if (hermas2_saga_log_file_open(&file, path, &summary) !=
            HERMAS2_SAGA_LOG_OK ||
        summary.next_sequence != 1u ||
        hermas2_saga_log_writer_append(
            &file.writer, started()) != HERMAS2_SAGA_LOG_OK) {
        unlink(path);
        return fail("cannot append synchronized record");
    }
    hermas2_saga_log_file competing;
    if (hermas2_saga_log_file_open(
            &competing, path, &summary) !=
        HERMAS2_SAGA_LOG_WRITE_ERROR) {
        hermas2_saga_log_file_close(&competing);
        hermas2_saga_log_file_close(&file);
        unlink(path);
        return fail("exclusive writer lock was not enforced");
    }
    if (hermas2_saga_log_file_scan(&file, &summary) !=
            HERMAS2_SAGA_LOG_OK ||
        summary.record_count != 1u ||
        summary.active_count != 1u ||
        summary.active[0].next_ordinal != 2u) {
        hermas2_saga_log_file_close(&file);
        unlink(path);
        return fail("synchronized state was not visible");
    }
    hermas2_saga_log_file_close(&file);
    if (hermas2_saga_log_file_open(&file, path, &summary) !=
            HERMAS2_SAGA_LOG_OK ||
        summary.next_sequence != 2u) {
        unlink(path);
        return fail("saga log did not survive reopen");
    }
    hermas2_saga_log_file_close(&file);

    int descriptor = open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
    uint8_t byte = 0u;
    if (descriptor < 0 || write(descriptor, &byte, 1u) != 1 ||
        close(descriptor) != 0 ||
        hermas2_saga_log_file_open(&file, path, &summary) !=
            HERMAS2_SAGA_LOG_INVALID_ARGUMENT) {
        /*
         * A non-record-aligned file is rejected by the core scanner as an
         * invalid scan argument. It must never be opened for appends.
         */
        unlink(path);
        return fail("truncated saga log was accepted");
    }
    unlink(path);
    puts("saga log Linux durability tests passed");
    return 0;
}
