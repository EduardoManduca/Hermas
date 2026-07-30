#include "example_app.h"
#include "hermas/workspace_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct example_handler_context {
    const hermas_image_action_contract *contract;
    hermas_example_action_handler handler;
} example_handler_context;

static int dispatch_action(
    void *user_data,
    uint16_t action_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length,
    uint16_t *outcome,
    uint16_t *result_type,
    uint8_t *result,
    size_t result_capacity,
    size_t *result_length) {
    example_handler_context *context = user_data;
    if (context == NULL || context->contract == NULL ||
        context->handler == NULL ||
        action_id != context->contract->action_id ||
        input_type != context->contract->input_type ||
        !context->handler(
            input, input_length, outcome, result, result_capacity,
            result_length)) {
        return 0;
    }
    if (*outcome == HERMAS_OUTCOME_SUCCESS) {
        *result_type = context->contract->success_type;
    } else if (*outcome == HERMAS_OUTCOME_APP_ERROR) {
        *result_type = context->contract->error_type;
    } else {
        return 0;
    }
    return 1;
}

static int load_explicit_contract(
    const char *path,
    const uint8_t fingerprint[32],
    hermas_image_action_contract *contract) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }
    long length = ftell(file);
    if (length <= 0 || (unsigned long)length > HERMAS_IMAGE_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    uint8_t *image = malloc((size_t)length);
    if (image == NULL) {
        fclose(file);
        return 0;
    }
    int read_ok =
        fread(image, 1u, (size_t)length, file) == (size_t)length;
    int close_ok = fclose(file) == 0;
    if (!read_ok || !close_ok) {
        free(image);
        return 0;
    }
    hermas_image_result found = hermas_image_find_action_contract(
        image, (size_t)length, fingerprint, contract);
    free(image);
    return found == HERMAS_IMAGE_OK;
}

int hermas_example_app_run_once(
    int argc,
    char **argv,
    const uint8_t expected_fingerprint[32],
    hermas_example_action_handler handler,
    uint8_t *result,
    size_t result_capacity) {
    int workspace_mode =
        argc >= 2 && strcmp(argv[1], "--workspace") == 0;
    if (argc != 3 || expected_fingerprint == NULL ||
        handler == NULL || result == NULL) {
        fprintf(
            stderr,
            "usage: %s SOCKET GRAPH_IMAGE\n"
            "       %s --workspace DIRECTORY\n",
            argv[0], argv[0]);
        return 2;
    }
    hermas_workspace_paths workspace;
    const char *socket_path = argv[1];
    hermas_image_action_contract contract;
    if (workspace_mode) {
        hermas_workspace_result opened =
            hermas_workspace_open(argv[2], false, &workspace);
        if (opened != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "workspace error: %s\n",
                hermas_workspace_result_name(opened));
            return 2;
        }
        hermas_workspace_result resolved =
            hermas_workspace_find_action_contract(
                &workspace, expected_fingerprint, &contract);
        if (resolved != HERMAS_WORKSPACE_OK) {
            fprintf(
                stderr, "workspace Action identity failed: %s\n",
                hermas_workspace_result_name(resolved));
            return 2;
        }
        socket_path = workspace.app_socket;
    } else if (!load_explicit_contract(
                   argv[2], expected_fingerprint, &contract)) {
        fputs("graph image does not install this Action contract\n", stderr);
        return 2;
    }
    hermas_edge edge;
    if (hermas_edge_connect(
            &edge, socket_path, contract.app_id, contract.action_id,
            contract.fingerprint) !=
        HERMAS_EDGE_OK) {
        return 1;
    }
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    example_handler_context context = {
        .contract = &contract,
        .handler = handler
    };
    hermas_edge_result served = hermas_edge_serve_once(
        &edge, packet, sizeof(packet), result, result_capacity,
        dispatch_action, &context);
    int succeeded =
        served == HERMAS_EDGE_OK &&
        edge.delivered_invocations == 1u;
    hermas_edge_disconnect(&edge);
    return succeeded ? 0 : 1;
}
