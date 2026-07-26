#define _POSIX_C_SOURCE 200809L

#include "hermas2/compensation_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_compensation_linux: %s\n", message);
    return 1;
}

int main(void) {
    char path[] = "/tmp/hermas2-compensation-XXXXXX";
    int temporary = mkstemp(path);
    if (temporary < 0) {
        return fail("cannot create token log");
    }
    close(temporary);
    hermas2_compensation_file file;
    hermas2_compensation_summary summary;
    if (hermas2_compensation_file_open(&file, path, &summary) !=
            HERMAS2_COMPENSATION_OK ||
        summary.next_sequence != 1u) {
        unlink(path);
        return fail("cannot open token log");
    }
    uint8_t value[8] = {77u};
    hermas2_compensation_record input = {
        .key = {
            .execution_id = 23u,
            .workflow_id = 2u,
            .request_id = 5u,
            .node_id = 3u,
            .image_fingerprint = UINT64_C(0xaabbccdd)
        },
        .compensation_app_id = 4u,
        .compensation_action_id = 6u,
        .source_type = 7u,
        .destination_type = 8u,
        .token = value,
        .token_length = sizeof(value)
    };
    uint8_t scratch[
        HERMAS2_COMPENSATION_HEADER_SIZE +
        HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
    if (hermas2_compensation_writer_append(
            &file.writer, input, scratch, sizeof(scratch)) !=
            HERMAS2_COMPENSATION_OK) {
        hermas2_compensation_file_close(&file);
        unlink(path);
        return fail("cannot synchronize token");
    }
    hermas2_compensation_record found_record;
    uint8_t found_token[8];
    int found = 0;
    if (hermas2_compensation_file_find(
            &file, input.key, &found_record, found_token,
            sizeof(found_token), &found) != HERMAS2_COMPENSATION_OK ||
        found != 1 || found_token[0] != 77u) {
        hermas2_compensation_file_close(&file);
        unlink(path);
        return fail("cannot find synchronized token");
    }
    hermas2_compensation_file_close(&file);
    if (hermas2_compensation_file_open(&file, path, &summary) !=
            HERMAS2_COMPENSATION_OK ||
        summary.record_count != 1u ||
        summary.next_sequence != 2u) {
        unlink(path);
        return fail("token log did not survive reopen");
    }
    hermas2_compensation_file_close(&file);
    unlink(path);
    puts("compensation Linux durability tests passed");
    return 0;
}
