#include "hermas/image.h"
#include "hermas/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "test_runtime: %s\n", message);
    return 1;
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint8_t *read_fixture(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(bytes);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static hermas_frame app_result(
    const hermas_frame *request,
    uint16_t outcome,
    uint16_t type,
    const uint8_t *payload,
    uint32_t payload_length) {
    return (hermas_frame){
        .kind = HERMAS_FRAME_RESULT,
        .execution_id = request->execution_id,
        .request_id = request->request_id,
        .app_id = request->app_id,
        .action_id = request->action_id,
        .source_type = type,
        .destination_type = type,
        .outcome = outcome,
        .payload = payload,
        .payload_length = payload_length
    };
}

static int test_success(const uint8_t *image, size_t image_size) {
    uint8_t storage[264] = {0u};
    hermas_execution execution;
    if (hermas_execution_start(&execution, image, image_size, 41u,
                                storage, sizeof(storage), 1u, NULL, 0u) !=
        HERMAS_RUNTIME_OK) {
        return fail("could not start valid execution");
    }
    hermas_frame invoke;
    if (hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        invoke.app_id != 1u || invoke.action_id != 1u ||
        invoke.source_type != 1u || invoke.destination_type != 1u ||
        invoke.request_id != 1u) {
        return fail("first invocation differs");
    }
    if (hermas_execution_mark_sent(&execution) != HERMAS_RUNTIME_OK) {
        return fail("first invocation was not marked sent");
    }
    uint8_t grades[32] = {0u};
    grades[0] = 3u;
    grades[8] = 70u;
    grades[16] = 80u;
    grades[24] = 90u;
    hermas_frame grades_result =
        app_result(&invoke, HERMAS_OUTCOME_SUCCESS, 4u,
                   grades, sizeof(grades));
    if (hermas_execution_accept_result(&execution, &grades_result) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        invoke.app_id != 2u || invoke.action_id != 2u ||
        invoke.source_type != 4u || invoke.destination_type != 8u ||
        invoke.request_id != 2u) {
        return fail("presentation into second invocation differs");
    }
    hermas_execution_mark_sent(&execution);
    uint8_t mean[8] = {80u};
    hermas_frame mean_result =
        app_result(&invoke, HERMAS_OUTCOME_SUCCESS, 6u, mean, sizeof(mean));
    if (hermas_execution_accept_result(&execution, &mean_result) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        invoke.app_id != 3u || invoke.source_type != 6u ||
        invoke.destination_type != 10u || invoke.request_id != 3u) {
        return fail("presentation into third invocation differs");
    }
    hermas_execution_mark_sent(&execution);
    uint8_t printed = 1u;
    hermas_frame printed_result =
        app_result(&invoke, HERMAS_OUTCOME_SUCCESS, 11u, &printed, 1u);
    if (hermas_execution_accept_result(&execution, &printed_result) !=
        HERMAS_RUNTIME_OK) {
        return fail("final app result was rejected");
    }
    hermas_frame result;
    if (hermas_execution_get_result(&execution, &result) !=
            HERMAS_RUNTIME_OK ||
        result.kind != HERMAS_FRAME_EXECUTION_RESULT ||
        result.outcome != HERMAS_OUTCOME_SUCCESS ||
        result.source_type != 11u || result.destination_type != 11u ||
        result.payload_length != 1u || result.payload[0] != 1u) {
        return fail("workflow success differs");
    }
    return 0;
}

static int test_terminal_outcomes(const uint8_t *image, size_t image_size) {
    uint8_t storage[264] = {0u};
    hermas_execution execution;
    hermas_frame invoke;
    hermas_frame result;
    if (hermas_execution_start(&execution, image, image_size, 42u,
                                storage, sizeof(storage), 1u, NULL, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        hermas_execution_mark_not_sent(&execution) != HERMAS_RUNTIME_OK ||
        hermas_execution_get_result(&execution, &result) != HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_NOT_SENT ||
        result.source_type != 0u || result.payload_length != 0u) {
        return fail("NotSent transition differs");
    }
    if (hermas_execution_start(&execution, image, image_size, 43u,
                                storage, sizeof(storage), 1u, NULL, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        hermas_execution_mark_sent(&execution) != HERMAS_RUNTIME_OK ||
        hermas_execution_mark_unknown(&execution) != HERMAS_RUNTIME_OK ||
        hermas_execution_get_result(&execution, &result) != HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_UNKNOWN) {
        return fail("Unknown transition differs");
    }
    if (hermas_execution_start(&execution, image, image_size, 44u,
                                storage, sizeof(storage), 1u, NULL, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        hermas_execution_mark_sent(&execution) != HERMAS_RUNTIME_OK) {
        return fail("could not prepare error execution");
    }
    hermas_frame error =
        app_result(&invoke, HERMAS_OUTCOME_APP_ERROR, 3u, NULL, 0u);
    if (hermas_execution_accept_result(&execution, &error) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_get_result(&execution, &result) != HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_APP_ERROR ||
        result.source_type != 3u || result.destination_type != 3u) {
        return fail("known app error differs");
    }
    return 0;
}

static int test_rejections(const uint8_t *image, size_t image_size) {
    uint8_t storage[8] = {0u};
    hermas_execution execution;
    hermas_frame frame;
    if (hermas_execution_start(&execution, image, image_size, 0u,
                                storage, sizeof(storage), 1u, NULL, 0u) !=
        HERMAS_RUNTIME_INVALID_ARGUMENT) {
        return fail("zero execution ID accepted");
    }
    if (hermas_execution_start(&execution, image, image_size, 45u,
                                storage, sizeof(storage), 2u, NULL, 0u) !=
        HERMAS_RUNTIME_INVALID_VALUE) {
        return fail("wrong input nominal type accepted");
    }
    if (hermas_execution_start(&execution, image, image_size, 45u,
                                storage, sizeof(storage), 1u, NULL, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_mark_sent(&execution) !=
            HERMAS_RUNTIME_INVALID_STATE ||
        hermas_execution_prepare(&execution, &frame) != HERMAS_RUNTIME_OK ||
        hermas_execution_mark_unknown(&execution) !=
            HERMAS_RUNTIME_INVALID_STATE) {
        return fail("invalid delivery state transition accepted");
    }
    hermas_execution_mark_sent(&execution);
    uint8_t malformed_grade_list[1] = {0u};
    hermas_frame malformed =
        app_result(&frame, HERMAS_OUTCOME_SUCCESS, 4u,
                   malformed_grade_list, sizeof(malformed_grade_list));
    if (hermas_execution_accept_result(&execution, &malformed) !=
        HERMAS_RUNTIME_INVALID_VALUE) {
        return fail("malformed result payload accepted");
    }
    return 0;
}

static int test_typed_choice(const uint8_t *image, size_t image_size) {
    uint8_t storage[64] = {0u};
    hermas_execution execution;
    hermas_frame invoke;
    if (hermas_execution_start(&execution, image, image_size, 301u,
                                storage, sizeof(storage), 6u, NULL, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        invoke.action_id != 2u ||
        hermas_execution_mark_sent(&execution) != HERMAS_RUNTIME_OK) {
        return fail("could not start typed choice");
    }
    uint8_t approved[16] = {0u};
    approved[8] = 42u;
    hermas_frame decision =
        app_result(&invoke, HERMAS_OUTCOME_SUCCESS, 2u,
                   approved, sizeof(approved));
    if (hermas_execution_accept_result(&execution, &decision) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        invoke.action_id != 1u || invoke.source_type != 1u ||
        invoke.destination_type != 1u || invoke.payload_length != 8u ||
        invoke.payload[0] != 42u || invoke.request_id != 2u) {
        return fail("approved Dispatch case did not route its payload");
    }
    hermas_execution_mark_sent(&execution);
    uint8_t done = 1u;
    hermas_frame accepted =
        app_result(&invoke, HERMAS_OUTCOME_SUCCESS, 4u, &done, 1u);
    hermas_frame result;
    if (hermas_execution_accept_result(&execution, &accepted) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_get_result(&execution, &result) !=
            HERMAS_RUNTIME_OK ||
        result.outcome != HERMAS_OUTCOME_SUCCESS ||
        result.source_type != 4u || result.payload_length != 1u) {
        return fail("typed choice did not complete");
    }

    if (hermas_execution_start(&execution, image, image_size, 302u,
                                storage, sizeof(storage), 6u, NULL, 0u) !=
            HERMAS_RUNTIME_OK ||
        hermas_execution_prepare(&execution, &invoke) != HERMAS_RUNTIME_OK ||
        hermas_execution_mark_sent(&execution) != HERMAS_RUNTIME_OK) {
        return fail("could not start malformed choice");
    }
    uint8_t bad_tag[8] = {9u};
    decision = app_result(&invoke, HERMAS_OUTCOME_SUCCESS, 2u,
                          bad_tag, sizeof(bad_tag));
    if (hermas_execution_accept_result(&execution, &decision) !=
        HERMAS_RUNTIME_INVALID_VALUE) {
        return fail("invalid variant tag reached Dispatch");
    }
    uint8_t *malformed = malloc(image_size);
    if (malformed == NULL) {
        return fail("cannot allocate malformed choice fixture");
    }
    memcpy(malformed, image, image_size);
    size_t edges_offset = read_u32(malformed, 52u);
    uint16_t edge_count = read_u16(malformed, 32u);
    size_t case_edge = 0u;
    for (size_t index = 0u; index < edge_count; ++index) {
        size_t offset = edges_offset + index * 16u;
        if (malformed[offset] == 5u) {
            case_edge = offset;
            break;
        }
    }
    if (case_edge == 0u) {
        free(malformed);
        return fail("choice fixture has no Dispatch case");
    }
    malformed[case_edge + 3u] = 9u;
    if (hermas_image_validate(malformed, image_size, NULL) ==
        HERMAS_IMAGE_OK) {
        free(malformed);
        return fail("out-of-range Dispatch tag accepted");
    }
    memcpy(malformed, image, image_size);
    malformed[case_edge + 8u] = 5u;
    malformed[case_edge + 9u] = 0u;
    if (hermas_image_validate(malformed, image_size, NULL) ==
        HERMAS_IMAGE_OK) {
        free(malformed);
        return fail("wrong Dispatch payload representation accepted");
    }
    free(malformed);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        return fail("expected sequential and optional choice image paths");
    }
    size_t image_size = 0u;
    uint8_t *image = read_fixture(argv[1], &image_size);
    if (image == NULL) {
        return fail("cannot read fixture");
    }
    int result = test_success(image, image_size);
    if (result == 0) {
        result = test_terminal_outcomes(image, image_size);
    }
    if (result == 0) {
        result = test_rejections(image, image_size);
    }
    free(image);
    if (result == 0 && argc == 3) {
        image = read_fixture(argv[2], &image_size);
        if (image == NULL) {
            return fail("cannot read typed-choice fixture");
        }
        result = test_typed_choice(image, image_size);
        free(image);
    }
    return result;
}
