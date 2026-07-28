#define _POSIX_C_SOURCE 200809L

#include "hermas/result_linux.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_result_linux: %s\n", message);
    return 1;
}

int main(void) {
    char path[] = "/tmp/hermas-result-XXXXXX";
    int temporary = mkstemp(path);
    if (temporary < 0 || close(temporary) != 0) {
        return fail("cannot create result log");
    }
    hermas_result_file file;
    hermas_result_summary summary;
    if (hermas_result_file_open(&file, path, &summary) !=
            HERMAS_RESULT_STORE_OK ||
        summary.next_sequence != 1u) {
        unlink(path);
        return fail("cannot open result log");
    }
    uint8_t value[8] = {77u};
    hermas_result_record input = {
        .key = {
            .execution_id = 23u,
            .workflow_id = 2u,
            .image_fingerprint = UINT64_C(0xaabbccdd)
        },
        .outcome = HERMAS_OUTCOME_APP_ERROR,
        .source_type = 7u,
        .destination_type = 8u,
        .value = value,
        .value_length = sizeof(value)
    };
    uint8_t scratch[
        HERMAS_RESULT_HEADER_SIZE +
        HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE];
    if (hermas_result_writer_append(
            &file.writer, input, scratch, sizeof(scratch)) !=
            HERMAS_RESULT_STORE_OK) {
        hermas_result_file_close(&file);
        unlink(path);
        return fail("cannot synchronize result");
    }
    hermas_result_file competing;
    if (hermas_result_file_open(
            &competing, path, &summary) !=
        HERMAS_RESULT_STORE_WRITE_ERROR) {
        hermas_result_file_close(&competing);
        hermas_result_file_close(&file);
        unlink(path);
        return fail("exclusive writer lock was not enforced");
    }
    hermas_result_record found_record;
    uint8_t found_value[8];
    int found = 0;
    if (hermas_result_file_find(
            &file, input.key, &found_record, found_value,
            sizeof(found_value), &found) !=
            HERMAS_RESULT_STORE_OK ||
        found != 1 || found_value[0] != 77u) {
        hermas_result_file_close(&file);
        unlink(path);
        return fail("cannot find synchronized result");
    }
    hermas_result_file_close(&file);
    if (hermas_result_file_open(&file, path, &summary) !=
            HERMAS_RESULT_STORE_OK ||
        summary.record_count != 1u ||
        summary.next_sequence != 2u) {
        unlink(path);
        return fail("result log did not survive reopen");
    }
    hermas_result_file_close(&file);

    int descriptor = open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
    uint8_t byte = 0u;
    if (descriptor < 0 || write(descriptor, &byte, 1u) != 1 ||
        close(descriptor) != 0 ||
        hermas_result_file_open(&file, path, &summary) ==
            HERMAS_RESULT_STORE_OK) {
        hermas_result_file_close(&file);
        unlink(path);
        return fail("truncated result log was accepted");
    }
    unlink(path);
    char exposed[] = "/tmp/hermas-result-exposed-XXXXXX";
    int exposed_file = mkstemp(exposed);
    if (exposed_file < 0 || close(exposed_file) != 0 ||
        chmod(exposed, 0644) != 0 ||
        hermas_result_file_open(&file, exposed, &summary) ==
            HERMAS_RESULT_STORE_OK) {
        unlink(exposed);
        return fail("non-private result log was accepted");
    }
    unlink(exposed);
    puts("terminal result Linux durability tests passed");
    return 0;
}
