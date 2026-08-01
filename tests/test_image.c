#include "hermas/image.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "test_image: %s\n", message);
    return 1;
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static int validate_additional_fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return fail("cannot open additional fixture");
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return fail("invalid additional fixture length");
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(bytes);
        return fail("cannot read additional fixture");
    }
    fclose(file);
    int result = 0;
    if (hermas_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS_IMAGE_OK) {
        result = fail("additional Rust image was rejected");
    }
    hermas_graph_features features = 0u;
    if (result == 0 &&
        (hermas_image_features(
             bytes, (size_t)length, &features) != HERMAS_IMAGE_OK ||
         features != (HERMAS_GRAPH_FEATURE_ACTION |
                      HERMAS_GRAPH_FEATURE_ALL))) {
        result = fail("parallel image features differ");
    }
    if (result == 0) {
        size_t nodes_offset = read_u32(bytes, 48u);
        uint16_t node_count =
            (uint16_t)bytes[30] | ((uint16_t)bytes[31] << 8u);
        bool tested_fork = false;
        for (size_t index = 0u; index < node_count; ++index) {
            size_t offset = nodes_offset + index * 8u;
            if (bytes[offset] == 4u) {
                uint8_t original = bytes[offset + 1u];
                bytes[offset + 1u] = 1u;
                if (hermas_image_validate(bytes, (size_t)length, NULL) ==
                    HERMAS_IMAGE_OK) {
                    result = fail("invalid Fork branch count was accepted");
                }
                bytes[offset + 1u] = original;
                tested_fork = true;
                break;
            }
        }
        if (!tested_fork) {
            result = fail("additional fixture contains no Fork");
        }
    }
    free(bytes);
    return result;
}

static int validate_saga_fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return fail("cannot open saga fixture");
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return fail("invalid saga fixture length");
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(bytes);
        return fail("cannot read saga fixture");
    }
    fclose(file);
    int result = 0;
    if (hermas_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS_IMAGE_OK) {
        result = fail("valid saga image was rejected");
    } else {
        hermas_graph_features features = 0u;
        if (hermas_image_features(
                bytes, (size_t)length, &features) != HERMAS_IMAGE_OK ||
            features != (HERMAS_GRAPH_FEATURE_ACTION |
                         HERMAS_GRAPH_FEATURE_SAGA)) {
            result = fail("saga image features differ");
        }
        uint16_t contract_count =
            (uint16_t)bytes[
                HERMAS_IMAGE_HEADER_ACTION_CONTRACT_COUNT_OFFSET] |
            ((uint16_t)bytes[
                HERMAS_IMAGE_HEADER_ACTION_CONTRACT_COUNT_OFFSET + 1u]
             << 8u);
        size_t contracts = read_u32(
            bytes, HERMAS_IMAGE_HEADER_ACTION_CONTRACTS_OFFSET);
        size_t nodes =
            read_u32(bytes, HERMAS_IMAGE_HEADER_NODES_OFFSET);
        uint16_t node_count =
            (uint16_t)bytes[HERMAS_IMAGE_HEADER_NODE_COUNT_OFFSET] |
            ((uint16_t)bytes[
                HERMAS_IMAGE_HEADER_NODE_COUNT_OFFSET + 1u]
             << 8u);
        bool resolved_compensation_only = false;
        for (size_t index = 0u; index < contract_count; ++index) {
            size_t record =
                contracts +
                index * HERMAS_IMAGE_ACTION_CONTRACT_RECORD_SIZE;
            uint16_t app =
                (uint16_t)bytes[record] |
                ((uint16_t)bytes[record + 1u] << 8u);
            uint16_t action =
                (uint16_t)bytes[record + 2u] |
                ((uint16_t)bytes[record + 3u] << 8u);
            bool has_forward_node = false;
            for (size_t node = 0u; node < node_count; ++node) {
                size_t node_record =
                    nodes + node * HERMAS_IMAGE_NODE_RECORD_SIZE;
                if (bytes[node_record] == 1u &&
                    ((uint16_t)bytes[node_record + 2u] |
                     ((uint16_t)bytes[node_record + 3u] << 8u)) ==
                        action &&
                    ((uint16_t)bytes[node_record + 4u] |
                     ((uint16_t)bytes[node_record + 5u] << 8u)) ==
                        app) {
                    has_forward_node = true;
                    break;
                }
            }
            hermas_image_action_contract contract;
            if (hermas_image_find_action_contract(
                    bytes, (size_t)length, bytes + record + 4u,
                    &contract) != HERMAS_IMAGE_OK ||
                contract.app_id != app ||
                contract.action_id != action ||
                contract.input_type == 0u ||
                contract.success_type == 0u ||
                contract.error_type == 0u) {
                result = fail("Saga Action contract did not resolve");
                break;
            }
            resolved_compensation_only |= !has_forward_node;
        }
        if (result == 0 && !resolved_compensation_only) {
            result = fail("saga fixture has no compensation-only contract");
        }
        size_t regions = read_u32(bytes, 72u);
        uint16_t region_count =
            (uint16_t)bytes[68] | ((uint16_t)bytes[69] << 8u);
        if (region_count == 0u || bytes[regions] != 3u) {
            result = fail("saga fixture contains no SagaStep");
        } else {
            uint8_t saved[2] = {bytes[regions + 12u],
                                bytes[regions + 13u]};
            bytes[regions + 12u] = 0u;
            bytes[regions + 13u] = 0u;
            if (hermas_image_validate(bytes, (size_t)length, NULL) ==
                HERMAS_IMAGE_OK) {
                result = fail("invalid saga ordinal was accepted");
            }
            bytes[regions + 12u] = saved[0];
            bytes[regions + 13u] = saved[1];
        }
    }
    free(bytes);
    return result;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        return fail("expected graph image and optional parallel/saga fixtures");
    }
    uint8_t oversized_marker = 0u;
    if (hermas_image_validate(
            &oversized_marker, HERMAS_IMAGE_MAX_SIZE + 1u, NULL) !=
        HERMAS_IMAGE_INVALID_HEADER) {
        return fail("oversized graph image was not rejected before decoding");
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return fail("cannot open fixture");
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        return fail("invalid fixture length");
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        return fail("cannot read fixture");
    }
    fclose(file);
    hermas_image_summary summary;
    if (hermas_image_validate(bytes, (size_t)length, &summary) != HERMAS_IMAGE_OK) {
        return fail("valid Rust image was rejected");
    }
    if (summary.node_count != 7u || summary.edge_count != 13u ||
        summary.action_contract_count != 3u || summary.type_count != 9u ||
        summary.workflow_name_length != 14u ||
        memcmp(summary.workflow_name, "grade_pipeline", 14u) != 0) {
        return fail("decoded summary differs");
    }
    hermas_graph_features features = 0u;
    if (hermas_image_features(
            bytes, (size_t)length, &features) != HERMAS_IMAGE_OK ||
        features != HERMAS_GRAPH_FEATURE_ACTION ||
        hermas_image_features(
            bytes, (size_t)length, NULL) != HERMAS_IMAGE_INVALID_VALUE) {
        return fail("sequential image features differ");
    }
    uint8_t action_fingerprint[32];
    hermas_image_action_contract action_contract;
    size_t action_contracts =
        read_u32(bytes, HERMAS_IMAGE_HEADER_ACTION_CONTRACTS_OFFSET);
    if (hermas_image_action_fingerprint(
            bytes, (size_t)length, 1u, 1u,
            action_fingerprint) != HERMAS_IMAGE_OK ||
        memcmp(
            action_fingerprint, bytes + action_contracts + 4u,
            sizeof(action_fingerprint)) != 0 ||
        hermas_image_action_fingerprint(
            bytes, (size_t)length, 99u, 99u,
            action_fingerprint) != HERMAS_IMAGE_ACTION_NOT_FOUND ||
        hermas_image_action_fingerprint(
            bytes, (size_t)length, 0u, 1u,
            action_fingerprint) != HERMAS_IMAGE_INVALID_VALUE ||
        hermas_image_action_fingerprint(
            bytes, (size_t)length, 1u, 1u, NULL) !=
            HERMAS_IMAGE_INVALID_VALUE) {
        return fail("Action fingerprint lookup differs");
    }
    if (hermas_image_find_action_contract(
            bytes, (size_t)length, action_fingerprint,
            &action_contract) != HERMAS_IMAGE_OK ||
        action_contract.app_id != 1u ||
        action_contract.action_id != 1u ||
        action_contract.input_type != 1u ||
        action_contract.success_type != 4u ||
        action_contract.error_type != 3u ||
        memcmp(
            action_contract.fingerprint, action_fingerprint,
            sizeof(action_fingerprint)) != 0) {
        return fail("Action contract resolution differs");
    }
    uint8_t second_fingerprint[32];
    memcpy(
        second_fingerprint,
        bytes + action_contracts +
            HERMAS_IMAGE_ACTION_CONTRACT_RECORD_SIZE + 4u,
        sizeof(second_fingerprint));
    memcpy(
        bytes + action_contracts +
            HERMAS_IMAGE_ACTION_CONTRACT_RECORD_SIZE + 4u,
        action_fingerprint, sizeof(action_fingerprint));
    if (hermas_image_find_action_contract(
            bytes, (size_t)length, action_fingerprint,
            &action_contract) != HERMAS_IMAGE_INVALID_RECORD) {
        return fail("ambiguous Action fingerprint was resolved");
    }
    memcpy(
        bytes + action_contracts +
            HERMAS_IMAGE_ACTION_CONTRACT_RECORD_SIZE + 4u,
        second_fingerprint, sizeof(second_fingerprint));
    action_fingerprint[0] ^= 0xffu;
    if (hermas_image_find_action_contract(
            bytes, (size_t)length, action_fingerprint,
            &action_contract) != HERMAS_IMAGE_ACTION_NOT_FOUND ||
        hermas_image_find_action_contract(
            bytes, (size_t)length, NULL, &action_contract) !=
            HERMAS_IMAGE_INVALID_VALUE) {
        return fail("unknown Action contract was resolved");
    }
    action_fingerprint[0] ^= 0xffu;
    hermas_image_type_summary type;
    if (hermas_image_describe_type(
            bytes, (size_t)length, 1u, &type) != HERMAS_IMAGE_OK ||
        type.kind != HERMAS_IMAGE_VALUE_UNIT || type.bound != 0u ||
        hermas_image_describe_type(
            bytes, (size_t)length, 4u, &type) != HERMAS_IMAGE_OK ||
        type.kind != HERMAS_IMAGE_VALUE_LIST || type.bound != 32u ||
        hermas_image_describe_type(
            bytes, (size_t)length, 6u, &type) != HERMAS_IMAGE_OK ||
        type.kind != HERMAS_IMAGE_VALUE_INTEGER || type.bound != 0u ||
        hermas_image_describe_type(
            bytes, (size_t)length, 11u, &type) != HERMAS_IMAGE_OK ||
        type.kind != HERMAS_IMAGE_VALUE_BOOLEAN || type.bound != 0u ||
        hermas_image_describe_type(
            bytes, (size_t)length, UINT16_MAX, &type) ==
            HERMAS_IMAGE_OK ||
        hermas_image_describe_type(
            bytes, (size_t)length, 1u, NULL) ==
            HERMAS_IMAGE_OK) {
        return fail("type representation inspection differs");
    }
    uint8_t grades[32] = {0u};
    grades[0] = 3u;
    grades[8] = 70u;
    grades[16] = 80u;
    grades[24] = 90u;
    uint8_t mean[8] = {80u};
    uint8_t printed[1] = {1u};
    if (hermas_image_validate_value(bytes, (size_t)length, 1u, NULL, 0u) !=
            HERMAS_IMAGE_OK ||
        hermas_image_validate_value(bytes, (size_t)length, 4u,
                                     grades, sizeof(grades)) !=
            HERMAS_IMAGE_OK ||
        hermas_image_validate_value(bytes, (size_t)length, 6u,
                                     mean, sizeof(mean)) !=
            HERMAS_IMAGE_OK ||
        hermas_image_validate_value(bytes, (size_t)length, 11u,
                                     printed, sizeof(printed)) !=
            HERMAS_IMAGE_OK) {
        return fail("valid canonical payload was rejected");
    }
    grades[0] = 33u;
    printed[0] = 2u;
    if (hermas_image_validate_value(bytes, (size_t)length, 4u,
                                     grades, sizeof(grades)) ==
            HERMAS_IMAGE_OK ||
        hermas_image_validate_value(bytes, (size_t)length, 6u,
                                     mean, sizeof(mean) - 1u) ==
            HERMAS_IMAGE_OK ||
        hermas_image_validate_value(bytes, (size_t)length, 11u,
                                     printed, sizeof(printed)) ==
            HERMAS_IMAGE_OK) {
        return fail("malformed canonical payload was accepted");
    }
    for (size_t prefix = 0u; prefix < (size_t)length; ++prefix) {
        if (hermas_image_validate(bytes, prefix, NULL) == HERMAS_IMAGE_OK) {
            return fail("truncated prefix was accepted");
        }
    }
    uint8_t original = bytes[12];
    bytes[12] = 1u;
    if (hermas_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS_IMAGE_INVALID_HEADER) {
        return fail("reserved header mutation was accepted");
    }
    bytes[12] = original;
    original = bytes[44];
    bytes[44] = 0u;
    if (hermas_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS_IMAGE_INVALID_OFFSET) {
        return fail("node offset mutation was accepted");
    }
    bytes[44] = original;
    size_t action_contracts_offset = read_u32(bytes, 40u);
    size_t nodes_offset = read_u32(bytes, 48u);
    size_t edges_offset = read_u32(bytes, 52u);
    const size_t record_mutations[] = {
        action_contracts_offset + 2u,
        nodes_offset,
        edges_offset,
        edges_offset + 3u,
        edges_offset + 14u
    };
    for (size_t index = 0u;
         index < sizeof(record_mutations) / sizeof(record_mutations[0]);
         ++index) {
        size_t offset = record_mutations[index];
        original = bytes[offset];
        bytes[offset] = 0xffu;
        if (hermas_image_validate(bytes, (size_t)length, NULL) ==
            HERMAS_IMAGE_OK) {
            return fail("malformed table record was accepted");
        }
        bytes[offset] = original;
    }
    uint8_t saved_error[2] = {bytes[82], bytes[83]};
    bytes[82] = bytes[80];
    bytes[83] = bytes[81];
    if (hermas_image_validate(bytes, (size_t)length, NULL) !=
        HERMAS_IMAGE_DUPLICATE_RECORD) {
        return fail("duplicate workflow error was accepted");
    }
    bytes[82] = saved_error[0];
    bytes[83] = saved_error[1];
    free(bytes);
    int result = argc >= 3 ? validate_additional_fixture(argv[2]) : 0;
    return result == 0 && argc == 4 ? validate_saga_fixture(argv[3]) : result;
}
