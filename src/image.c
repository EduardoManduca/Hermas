#include "hermas2/image.h"

#include <stdbool.h>
#include <string.h>

#define HERMAS2_HEADER_SIZE 80u
#define HERMAS2_APP_RECORD_SIZE 36u
#define HERMAS2_TYPE_RECORD_SIZE 8u
#define HERMAS2_NODE_RECORD_SIZE 8u
#define HERMAS2_EDGE_RECORD_SIZE 16u
#define HERMAS2_REGION_RECORD_SIZE 16u
#define HERMAS2_MAX_NODES 64u
#define HERMAS2_MAX_EDGES 192u
#define HERMAS2_MAX_ERRORS 256u
#define HERMAS2_MAX_ALL_BRANCHES 8u

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static bool checked_table_end(
    size_t offset,
    size_t count,
    size_t record_size,
    size_t *end) {
    if (count > (SIZE_MAX - offset) / record_size) {
        return false;
    }
    *end = offset + count * record_size;
    return true;
}

static bool valid_utf8(const uint8_t *bytes, size_t size) {
    size_t index = 0u;
    while (index < size) {
        uint8_t first = bytes[index++];
        uint32_t codepoint = 0u;
        size_t continuation = 0u;
        if (first <= 0x7fu) {
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = (uint32_t)(first & 0x1fu);
            continuation = 1u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = (uint32_t)(first & 0x0fu);
            continuation = 2u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = (uint32_t)(first & 0x07u);
            continuation = 3u;
        } else {
            return false;
        }
        if (continuation > size - index) {
            return false;
        }
        for (size_t item = 0u; item < continuation; ++item) {
            uint8_t next = bytes[index++];
            if ((next & 0xc0u) != 0x80u) {
                return false;
            }
            codepoint = (codepoint << 6u) | (uint32_t)(next & 0x3fu);
        }
        if ((continuation == 2u && codepoint < 0x800u) ||
            (continuation == 3u && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
            codepoint > 0x10ffffu) {
            return false;
        }
    }
    return true;
}

static bool contains_u16(
    const uint8_t *bytes,
    size_t offset,
    size_t count,
    size_t stride,
    uint16_t value) {
    for (size_t index = 0u; index < count; ++index) {
        if (read_u16(bytes, offset + index * stride) == value) {
            return true;
        }
    }
    return false;
}

static hermas2_image_result validate_representation(
    const uint8_t *bytes,
    size_t end,
    size_t *cursor,
    size_t depth) {
    if (depth > 64u || *cursor > end || end - *cursor < 8u) {
        return HERMAS2_IMAGE_INVALID_RECORD;
    }
    size_t offset = *cursor;
    uint8_t kind = bytes[offset];
    uint8_t reserved = bytes[offset + 1u];
    uint16_t children = read_u16(bytes, offset + 2u);
    uint32_t bound = read_u32(bytes, offset + 4u);
    bool valid =
        reserved == 0u &&
        (((kind >= 1u && kind <= 3u) && children == 0u && bound == 0u) ||
         ((kind == 4u || kind == 5u) && children == 0u &&
          bound >= 1u && bound <= 1048576u) ||
         (kind == 6u && bound == 0u) ||
         (kind == 7u && children == 1u && bound >= 1u && bound <= 256u) ||
         (kind == 8u && children != 0u && bound == 0u));
    if (!valid) {
        return HERMAS2_IMAGE_INVALID_RECORD;
    }
    *cursor += 8u;
    for (size_t child = 0u; child < children; ++child) {
        hermas2_image_result result =
            validate_representation(bytes, end, cursor, depth + 1u);
        if (result != HERMAS2_IMAGE_OK) {
            return result;
        }
    }
    return HERMAS2_IMAGE_OK;
}

static bool type_descriptor(
    const uint8_t *bytes,
    size_t types_offset,
    size_t type_count,
    size_t strings_offset,
    uint16_t type_id,
    size_t *start,
    size_t *end) {
    for (size_t index = 0u; index < type_count; ++index) {
        size_t record = types_offset + index * HERMAS2_TYPE_RECORD_SIZE;
        if (read_u16(bytes, record) != type_id) {
            continue;
        }
        *start = read_u32(bytes, record + 4u);
        *end = index + 1u < type_count
                   ? read_u32(bytes, record + HERMAS2_TYPE_RECORD_SIZE + 4u)
                   : strings_offset;
        return true;
    }
    return false;
}

static bool representation_child(
    const uint8_t *bytes,
    size_t descriptor,
    size_t descriptor_end,
    size_t child_index,
    size_t *child_start,
    size_t *child_end) {
    uint16_t children = read_u16(bytes, descriptor + 2u);
    if (child_index >= children) {
        return false;
    }
    size_t cursor = descriptor + 8u;
    for (size_t index = 0u; index < children; ++index) {
        size_t start = cursor;
        if (validate_representation(bytes, descriptor_end, &cursor, 1u) !=
            HERMAS2_IMAGE_OK) {
            return false;
        }
        if (index == child_index) {
            *child_start = start;
            *child_end = cursor;
            return true;
        }
    }
    return false;
}

static bool payload_u32(
    const uint8_t *payload,
    size_t payload_size,
    size_t offset,
    uint32_t *value) {
    if (offset > payload_size || payload_size - offset < 4u) {
        return false;
    }
    *value = read_u32(payload, offset);
    return true;
}

static hermas2_image_result validate_value_inner(
    const uint8_t *image,
    size_t representation_end,
    size_t *representation_cursor,
    const uint8_t *payload,
    size_t payload_size,
    size_t *payload_cursor,
    size_t depth) {
    if (depth > 64u || *representation_cursor > representation_end ||
        representation_end - *representation_cursor < 8u) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    size_t representation = *representation_cursor;
    uint8_t kind = image[representation];
    uint16_t children = read_u16(image, representation + 2u);
    uint32_t bound = read_u32(image, representation + 4u);
    size_t child_start = representation + 8u;
    *representation_cursor = child_start;
    if (kind == 1u) {
        return HERMAS2_IMAGE_OK;
    }
    if (kind == 2u) {
        if (*payload_cursor > payload_size ||
            payload_size - *payload_cursor < 8u) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        *payload_cursor += 8u;
        return HERMAS2_IMAGE_OK;
    }
    if (kind == 3u) {
        if (*payload_cursor >= payload_size || payload[*payload_cursor] > 1u) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        ++*payload_cursor;
        return HERMAS2_IMAGE_OK;
    }
    if (kind == 4u || kind == 5u) {
        uint32_t length = 0u;
        uint32_t reserved = 0u;
        if (!payload_u32(payload, payload_size, *payload_cursor, &length) ||
            !payload_u32(payload, payload_size, *payload_cursor + 4u, &reserved) ||
            reserved != 0u || length > bound ||
            *payload_cursor > payload_size - 8u ||
            (size_t)length > payload_size - (*payload_cursor + 8u)) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        const uint8_t *data = payload + *payload_cursor + 8u;
        if (kind == 4u &&
            (!valid_utf8(data, length) || memchr(data, 0, length) != NULL)) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        *payload_cursor += 8u + (size_t)length;
        return HERMAS2_IMAGE_OK;
    }
    if (kind == 6u) {
        for (size_t child = 0u; child < children; ++child) {
            hermas2_image_result result = validate_value_inner(
                image, representation_end, representation_cursor,
                payload, payload_size, payload_cursor, depth + 1u);
            if (result != HERMAS2_IMAGE_OK) {
                return result;
            }
        }
        return HERMAS2_IMAGE_OK;
    }
    if (kind == 7u) {
        uint32_t count = 0u;
        uint32_t reserved = 0u;
        if (!payload_u32(payload, payload_size, *payload_cursor, &count) ||
            !payload_u32(payload, payload_size, *payload_cursor + 4u, &reserved) ||
            reserved != 0u || count > bound) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        *payload_cursor += 8u;
        size_t child_end = child_start;
        if (validate_representation(image, representation_end,
                                    &child_end, depth + 1u) !=
            HERMAS2_IMAGE_OK) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        for (size_t item = 0u; item < count; ++item) {
            size_t child_cursor = child_start;
            hermas2_image_result result = validate_value_inner(
                image, representation_end, &child_cursor, payload,
                payload_size, payload_cursor, depth + 1u);
            if (result != HERMAS2_IMAGE_OK || child_cursor != child_end) {
                return HERMAS2_IMAGE_INVALID_VALUE;
            }
        }
        *representation_cursor = child_end;
        return HERMAS2_IMAGE_OK;
    }
    if (kind == 8u) {
        uint32_t tag = 0u;
        uint32_t reserved = 0u;
        if (!payload_u32(payload, payload_size, *payload_cursor, &tag) ||
            !payload_u32(payload, payload_size, *payload_cursor + 4u, &reserved) ||
            reserved != 0u || tag >= children) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        *payload_cursor += 8u;
        for (size_t child = 0u; child < children; ++child) {
            if (child == tag) {
                hermas2_image_result result = validate_value_inner(
                    image, representation_end, representation_cursor,
                    payload, payload_size, payload_cursor, depth + 1u);
                if (result != HERMAS2_IMAGE_OK) {
                    return result;
                }
            } else if (validate_representation(
                           image, representation_end,
                           representation_cursor, depth + 1u) !=
                       HERMAS2_IMAGE_OK) {
                return HERMAS2_IMAGE_INVALID_VALUE;
            }
        }
        return HERMAS2_IMAGE_OK;
    }
    return HERMAS2_IMAGE_INVALID_VALUE;
}

hermas2_image_result hermas2_image_validate(
    const uint8_t *bytes,
    size_t size,
    hermas2_image_summary *summary) {
    if (bytes == NULL || size < HERMAS2_HEADER_SIZE) {
        return HERMAS2_IMAGE_TRUNCATED;
    }
    if (memcmp(bytes, "H2GI", 4u) != 0) {
        return HERMAS2_IMAGE_BAD_MAGIC;
    }
    if (read_u16(bytes, 4u) != 1u) {
        return HERMAS2_IMAGE_UNSUPPORTED_VERSION;
    }
    if (read_u16(bytes, 6u) != HERMAS2_HEADER_SIZE ||
        (size > UINT32_MAX) ||
        read_u32(bytes, 8u) != (uint32_t)size ||
        read_u32(bytes, 12u) != 0u ||
        read_u16(bytes, 70u) != 0u ||
        read_u32(bytes, 76u) != 0u) {
        return HERMAS2_IMAGE_INVALID_HEADER;
    }

    size_t name_offset = read_u32(bytes, 16u);
    uint16_t name_length = read_u16(bytes, 20u);
    uint16_t input_type = read_u16(bytes, 22u);
    uint16_t success_type = read_u16(bytes, 24u);
    uint16_t error_count = read_u16(bytes, 26u);
    uint16_t app_count = read_u16(bytes, 28u);
    uint16_t node_count = read_u16(bytes, 30u);
    uint16_t edge_count = read_u16(bytes, 32u);
    uint16_t type_count = read_u16(bytes, 34u);
    uint16_t region_count = read_u16(bytes, 68u);
    if (input_type == 0u || success_type == 0u || error_count == 0u ||
        error_count > HERMAS2_MAX_ERRORS || app_count == 0u ||
        app_count > HERMAS2_MAX_NODES || type_count == 0u ||
        type_count > HERMAS2_MAX_ERRORS || node_count > HERMAS2_MAX_NODES ||
        edge_count > HERMAS2_MAX_EDGES || region_count > 32u) {
        return HERMAS2_IMAGE_INVALID_COUNT;
    }

    size_t errors_offset = read_u32(bytes, 36u);
    size_t apps_offset = read_u32(bytes, 40u);
    size_t types_offset = read_u32(bytes, 44u);
    size_t nodes_offset = read_u32(bytes, 48u);
    size_t edges_offset = read_u32(bytes, 52u);
    size_t representations_offset = read_u32(bytes, 56u);
    size_t strings_offset = read_u32(bytes, 60u);
    size_t representations_length = read_u32(bytes, 64u);
    size_t regions_offset = read_u32(bytes, 72u);
    size_t expected_apps = (HERMAS2_HEADER_SIZE + (size_t)error_count * 2u + 3u) & ~(size_t)3u;
    size_t expected_types = 0u;
    size_t expected_nodes = 0u;
    size_t expected_edges = 0u;
    size_t expected_regions = 0u;
    size_t expected_strings = 0u;
    if (!checked_table_end(expected_apps, app_count, HERMAS2_APP_RECORD_SIZE, &expected_types) ||
        !checked_table_end(expected_types, type_count, HERMAS2_TYPE_RECORD_SIZE, &expected_nodes) ||
        !checked_table_end(expected_nodes, node_count, HERMAS2_NODE_RECORD_SIZE, &expected_edges) ||
        !checked_table_end(expected_edges, edge_count, HERMAS2_EDGE_RECORD_SIZE, &expected_regions) ||
        !checked_table_end(expected_regions, region_count, HERMAS2_REGION_RECORD_SIZE, &expected_strings) ||
        representations_length > SIZE_MAX - expected_strings ||
        errors_offset != HERMAS2_HEADER_SIZE ||
        apps_offset != expected_apps || types_offset != expected_types ||
        nodes_offset != expected_nodes || edges_offset != expected_edges ||
        regions_offset != expected_regions ||
        representations_offset != expected_strings ||
        strings_offset != expected_strings + representations_length ||
        name_offset != strings_offset || name_offset > size ||
        (size_t)name_length != size - name_offset) {
        return HERMAS2_IMAGE_INVALID_OFFSET;
    }
    if (name_length == 0u || !valid_utf8(bytes + name_offset, name_length)) {
        return HERMAS2_IMAGE_INVALID_STRING;
    }

    for (size_t left = 0u; left < error_count; ++left) {
        uint16_t value = read_u16(bytes, errors_offset + left * 2u);
        if (value == 0u) {
            return HERMAS2_IMAGE_DUPLICATE_RECORD;
        }
        for (size_t right = 0u; right < left; ++right) {
            if (value == read_u16(bytes, errors_offset + right * 2u)) {
                return HERMAS2_IMAGE_DUPLICATE_RECORD;
            }
        }
    }
    for (size_t left = 0u; left < app_count; ++left) {
        size_t offset = apps_offset + left * HERMAS2_APP_RECORD_SIZE;
        uint16_t app = read_u16(bytes, offset);
        if (app == 0u || read_u16(bytes, offset + 2u) != 0u) {
            return HERMAS2_IMAGE_INVALID_RECORD;
        }
        for (size_t right = 0u; right < left; ++right) {
            if (app == read_u16(bytes, apps_offset + right * HERMAS2_APP_RECORD_SIZE)) {
                return HERMAS2_IMAGE_INVALID_RECORD;
            }
        }
    }
    size_t representation_cursor = representations_offset;
    for (size_t left = 0u; left < type_count; ++left) {
        size_t offset = types_offset + left * HERMAS2_TYPE_RECORD_SIZE;
        uint16_t type_id = read_u16(bytes, offset);
        if (type_id == 0u || read_u16(bytes, offset + 2u) != 0u ||
            read_u32(bytes, offset + 4u) != representation_cursor) {
            return HERMAS2_IMAGE_INVALID_RECORD;
        }
        for (size_t right = 0u; right < left; ++right) {
            if (type_id ==
                read_u16(bytes, types_offset + right * HERMAS2_TYPE_RECORD_SIZE)) {
                return HERMAS2_IMAGE_INVALID_RECORD;
            }
        }
        if (validate_representation(bytes, strings_offset,
                                    &representation_cursor, 0u) !=
            HERMAS2_IMAGE_OK) {
            return HERMAS2_IMAGE_INVALID_RECORD;
        }
    }
    if (representation_cursor != strings_offset ||
        !contains_u16(bytes, types_offset, type_count,
                      HERMAS2_TYPE_RECORD_SIZE, input_type) ||
        !contains_u16(bytes, types_offset, type_count,
                      HERMAS2_TYPE_RECORD_SIZE, success_type)) {
        return HERMAS2_IMAGE_INVALID_RECORD;
    }
    for (size_t index = 0u; index < error_count; ++index) {
        if (!contains_u16(bytes, types_offset, type_count,
                          HERMAS2_TYPE_RECORD_SIZE,
                          read_u16(bytes, errors_offset + index * 2u))) {
            return HERMAS2_IMAGE_INVALID_RECORD;
        }
    }

    bool action_nodes[HERMAS2_MAX_NODES + 1u] = {false};
    uint16_t dispatch_types[HERMAS2_MAX_NODES + 1u] = {0u};
    uint16_t dispatch_cases[HERMAS2_MAX_NODES + 1u] = {0u};
    uint16_t fork_types[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t fork_branches[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t join_branches[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t terminal_nodes[HERMAS2_MAX_NODES + 1u] = {0u};
    bool terminal_kinds[5] = {false};
    for (size_t index = 0u; index < node_count; ++index) {
        size_t offset = nodes_offset + index * HERMAS2_NODE_RECORD_SIZE;
        uint8_t kind = bytes[offset];
        uint8_t subtype = bytes[offset + 1u];
        uint16_t action = read_u16(bytes, offset + 2u);
        uint16_t app = read_u16(bytes, offset + 4u);
        uint16_t reserved = read_u16(bytes, offset + 6u);
        size_t node = index + 1u;
        if (kind == 1u && subtype == 0u && action != 0u && reserved == 0u &&
            contains_u16(bytes, apps_offset, app_count, HERMAS2_APP_RECORD_SIZE, app)) {
            action_nodes[node] = true;
        } else if (kind == 3u && subtype == 0u && action != 0u &&
                   app == 0u && reserved == 0u) {
            size_t descriptor = 0u;
            size_t descriptor_end = 0u;
            if (!type_descriptor(bytes, types_offset, type_count,
                                 strings_offset, action, &descriptor,
                                 &descriptor_end) ||
                bytes[descriptor] != 8u ||
                read_u16(bytes, descriptor + 2u) > HERMAS2_MAX_EDGES) {
                return HERMAS2_IMAGE_INVALID_RECORD;
            }
            dispatch_types[node] = action;
            dispatch_cases[node] = read_u16(bytes, descriptor + 2u);
        } else if (kind == 4u && subtype >= 2u &&
                   subtype <= HERMAS2_MAX_ALL_BRANCHES && action != 0u &&
                   app == 0u && reserved == 0u &&
                   contains_u16(bytes, types_offset, type_count,
                                HERMAS2_TYPE_RECORD_SIZE, action)) {
            fork_types[node] = action;
            fork_branches[node] = subtype;
        } else if (kind == 5u && subtype >= 2u &&
                   subtype <= HERMAS2_MAX_ALL_BRANCHES && action == 0u &&
                   app == 0u && reserved == 0u) {
            join_branches[node] = subtype;
        } else if (kind == 2u && subtype >= 1u && subtype <= 4u &&
                   action == 0u && app == 0u && reserved == 0u &&
                   !terminal_kinds[subtype]) {
            terminal_kinds[subtype] = true;
            terminal_nodes[node] = subtype;
        } else {
            return HERMAS2_IMAGE_INVALID_RECORD;
        }
    }
    if (!terminal_kinds[1] || !terminal_kinds[2] ||
        !terminal_kinds[3] || !terminal_kinds[4]) {
        return HERMAS2_IMAGE_INVALID_TOPOLOGY;
    }

    uint16_t deadline_first[8] = {0u};
    uint16_t deadline_last[8] = {0u};
    size_t deadline_count = 0u;
    size_t each_count = 0u;
    uint16_t each_template[9] = {0u};
    uint16_t each_source_list[9] = {0u};
    uint16_t each_item_input[9] = {0u};
    uint16_t each_item_output[9] = {0u};
    uint16_t each_collected[9] = {0u};
    uint16_t each_bound[9] = {0u};
    uint8_t each_concurrency[9] = {0u};
    size_t saga_count = 0u;
    uint16_t saga_source_type[HERMAS2_MAX_NODES + 1u] = {0u};
    for (size_t index = 0u; index < region_count; ++index) {
        size_t offset = regions_offset + index * HERMAS2_REGION_RECORD_SIZE;
        uint8_t kind = bytes[offset];
        if (kind == 1u) {
            uint16_t first = read_u16(bytes, offset + 2u);
            uint16_t count = read_u16(bytes, offset + 4u);
            uint16_t parent = read_u16(bytes, offset + 6u);
            size_t last = (size_t)first + count - 1u;
            bool within_parent =
                parent == 0u ||
                (parent <= deadline_count &&
                 first >= deadline_first[parent - 1u] &&
                 last <= deadline_last[parent - 1u]);
            if (deadline_count >= 8u || each_count != 0u || saga_count != 0u ||
                bytes[offset + 1u] != 0u || first == 0u ||
                count == 0u || last > node_count ||
                parent > deadline_count || !within_parent ||
                (deadline_count == 0u &&
                 (first != 1u || count != node_count || parent != 0u)) ||
                read_u64(bytes, offset + 8u) == 0u) {
                return HERMAS2_IMAGE_INVALID_RECORD;
            }
            deadline_first[deadline_count] = first;
            deadline_last[deadline_count] = (uint16_t)last;
            ++deadline_count;
        } else if (kind == 2u) {
            if (each_count >= 8u || saga_count != 0u) {
                return HERMAS2_IMAGE_INVALID_RECORD;
            }
            size_t id = ++each_count;
            each_concurrency[id] = bytes[offset + 1u];
            each_template[id] = read_u16(bytes, offset + 2u);
            each_source_list[id] = read_u16(bytes, offset + 4u);
            each_item_input[id] = read_u16(bytes, offset + 6u);
            each_item_output[id] = read_u16(bytes, offset + 8u);
            each_collected[id] = read_u16(bytes, offset + 10u);
            each_bound[id] = read_u16(bytes, offset + 12u);
            size_t source_start = 0u;
            size_t source_end = 0u;
            size_t input_start = 0u;
            size_t input_end = 0u;
            size_t collected_start = 0u;
            size_t collected_end = 0u;
            size_t output_start = 0u;
            size_t output_end = 0u;
            size_t child_start = 0u;
            size_t child_end = 0u;
            bool source_matches =
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, each_source_list[id],
                                &source_start, &source_end) &&
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, each_item_input[id],
                                &input_start, &input_end) &&
                bytes[source_start] == 7u &&
                read_u32(bytes, source_start + 4u) == each_bound[id] &&
                representation_child(bytes, source_start, source_end, 0u,
                                     &child_start, &child_end) &&
                child_end - child_start == input_end - input_start &&
                memcmp(bytes + child_start, bytes + input_start,
                       child_end - child_start) == 0;
            bool output_matches =
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, each_collected[id],
                                &collected_start, &collected_end) &&
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, each_item_output[id],
                                &output_start, &output_end) &&
                bytes[collected_start] == 7u &&
                read_u32(bytes, collected_start + 4u) >= each_bound[id] &&
                representation_child(bytes, collected_start, collected_end,
                                     0u, &child_start, &child_end) &&
                child_end - child_start == output_end - output_start &&
                memcmp(bytes + child_start, bytes + output_start,
                       child_end - child_start) == 0;
            if (each_concurrency[id] == 0u ||
                each_concurrency[id] > HERMAS2_MAX_ALL_BRANCHES ||
                each_concurrency[id] > each_bound[id] ||
                each_template[id] == 0u ||
                each_template[id] > node_count ||
                !action_nodes[each_template[id]] ||
                !source_matches || !output_matches ||
                read_u16(bytes, offset + 14u) != 0u) {
                return HERMAS2_IMAGE_INVALID_RECORD;
            }
        } else if (kind == 3u) {
            uint16_t forward = read_u16(bytes, offset + 2u);
            uint16_t compensation_app = read_u16(bytes, offset + 4u);
            uint16_t compensation_action = read_u16(bytes, offset + 6u);
            uint16_t source_type = read_u16(bytes, offset + 8u);
            uint16_t destination_type = read_u16(bytes, offset + 10u);
            uint16_t ordinal = read_u16(bytes, offset + 12u);
            size_t source_start = 0u;
            size_t source_end = 0u;
            size_t destination_start = 0u;
            size_t destination_end = 0u;
            bool compatible =
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, source_type,
                                &source_start, &source_end) &&
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, destination_type,
                                &destination_start, &destination_end) &&
                source_end - source_start == destination_end - destination_start &&
                memcmp(bytes + source_start, bytes + destination_start,
                       source_end - source_start) == 0;
            if (bytes[offset + 1u] != 0u || forward == 0u ||
                forward > node_count || !action_nodes[forward] ||
                compensation_app == 0u || compensation_action == 0u ||
                !contains_u16(bytes, apps_offset, app_count,
                              HERMAS2_APP_RECORD_SIZE, compensation_app) ||
                ordinal != saga_count + 1u || saga_source_type[forward] != 0u ||
                !compatible || read_u16(bytes, offset + 14u) != 0u) {
                return HERMAS2_IMAGE_INVALID_RECORD;
            }
            saga_source_type[forward] = source_type;
            ++saga_count;
        } else {
            return HERMAS2_IMAGE_INVALID_RECORD;
        }
    }

    uint8_t input_counts[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t success_counts[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t error_counts[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t not_sent_counts[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t unknown_counts[HERMAS2_MAX_NODES + 1u] = {0u};
    uint8_t dispatch_case_counts[HERMAS2_MAX_NODES + 1u][256] = {{0u}};
    uint8_t fork_branch_counts[HERMAS2_MAX_NODES + 1u][HERMAS2_MAX_ALL_BRANCHES] = {{0u}};
    uint8_t join_input_counts[HERMAS2_MAX_NODES + 1u][HERMAS2_MAX_ALL_BRANCHES] = {{0u}};
    uint8_t join_output_counts[HERMAS2_MAX_NODES + 1u][HERMAS2_MAX_ALL_BRANCHES] = {{0u}};
    uint16_t join_input_types[HERMAS2_MAX_NODES + 1u][HERMAS2_MAX_ALL_BRANCHES] = {{0u}};
    uint16_t join_output_types[HERMAS2_MAX_NODES + 1u][HERMAS2_MAX_ALL_BRANCHES] = {{0u}};
    uint8_t each_input_counts[9] = {0u};
    uint8_t each_collect_counts[9] = {0u};
    uint8_t each_item_counts[9] = {0u};
    uint8_t each_output_counts[9] = {0u};
    uint16_t action_success_type[HERMAS2_MAX_NODES + 1u] = {0u};
    bool reachable[HERMAS2_MAX_NODES + 1u] = {false};
    unsigned workflow_input_count = 0u;
    for (size_t index = 0u; index < edge_count; ++index) {
        size_t offset = edges_offset + index * HERMAS2_EDGE_RECORD_SIZE;
        uint8_t source_kind = bytes[offset];
        uint8_t target_kind = bytes[offset + 1u];
        uint8_t flags = bytes[offset + 2u];
        uint8_t case_tag = bytes[offset + 3u];
        uint16_t source_node = read_u16(bytes, offset + 4u);
        uint16_t target_node = read_u16(bytes, offset + 6u);
        uint16_t source_type = read_u16(bytes, offset + 8u);
        uint16_t target_type = read_u16(bytes, offset + 10u);
        uint16_t presentation = read_u16(bytes, offset + 12u);
        bool valid_dispatch_source = false;
        if (source_kind == 5u && source_node <= node_count &&
            dispatch_types[source_node] != 0u &&
            case_tag < dispatch_cases[source_node]) {
            size_t variant_start = 0u;
            size_t variant_end = 0u;
            size_t source_start = 0u;
            size_t source_end = 0u;
            size_t child_start = 0u;
            size_t child_end = 0u;
            valid_dispatch_source =
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, dispatch_types[source_node],
                                &variant_start, &variant_end) &&
                type_descriptor(bytes, types_offset, type_count,
                                strings_offset, source_type,
                                &source_start, &source_end) &&
                representation_child(bytes, variant_start, variant_end,
                                     case_tag, &child_start, &child_end) &&
                child_end - child_start == source_end - source_start &&
                memcmp(bytes + child_start, bytes + source_start,
                       child_end - child_start) == 0;
        }
        bool valid_source =
            (source_kind == 0u && source_node == 0u && source_type == input_type &&
             contains_u16(bytes, types_offset, type_count,
                          HERMAS2_TYPE_RECORD_SIZE, source_type)) ||
            ((source_kind == 1u || source_kind == 2u) &&
             source_node <= node_count && action_nodes[source_node] &&
             contains_u16(bytes, types_offset, type_count,
                          HERMAS2_TYPE_RECORD_SIZE, source_type)) ||
            ((source_kind == 3u || source_kind == 4u) &&
             source_node <= node_count &&
             action_nodes[source_node] && source_type == 0u && target_type == 0u) ||
            valid_dispatch_source ||
            (source_kind == 6u && source_node <= node_count &&
             fork_types[source_node] != 0u &&
             case_tag < fork_branches[source_node] &&
             source_type == fork_types[source_node]) ||
            (source_kind == 7u && source_node <= node_count &&
             join_branches[source_node] != 0u &&
             case_tag < join_branches[source_node] &&
             contains_u16(bytes, types_offset, type_count,
                          HERMAS2_TYPE_RECORD_SIZE, source_type)) ||
            (source_kind == 8u && source_node > 0u &&
             source_node <= each_count && case_tag == 0u &&
             source_type == each_item_input[source_node]) ||
            (source_kind == 9u && source_node > 0u &&
             source_node <= each_count && case_tag == 0u &&
             source_type == each_collected[source_node]);
        bool valid_target = false;
        if (target_node > 0u && target_node <= node_count) {
            if (target_kind == 1u) {
                valid_target = action_nodes[target_node] &&
                    contains_u16(bytes, types_offset, type_count,
                                 HERMAS2_TYPE_RECORD_SIZE, target_type);
            } else if (target_kind == 2u) {
                uint8_t terminal = terminal_nodes[target_node];
                valid_target =
                    (terminal == 1u && target_type == success_type) ||
                    (terminal == 2u && contains_u16(bytes, errors_offset, error_count, 2u, target_type)) ||
                    ((terminal == 3u || terminal == 4u) && target_type == 0u);
            } else if (target_kind == 3u) {
                valid_target = dispatch_types[target_node] != 0u &&
                               target_type == dispatch_types[target_node];
            } else if (target_kind == 4u) {
                valid_target = fork_types[target_node] != 0u &&
                               target_type == fork_types[target_node];
            } else if (target_kind == 5u) {
                valid_target = join_branches[target_node] != 0u &&
                               case_tag < join_branches[target_node] &&
                               contains_u16(bytes, types_offset, type_count,
                                            HERMAS2_TYPE_RECORD_SIZE,
                                            target_type);
            } else if (target_kind == 6u) {
                valid_target = target_node <= each_count &&
                               target_type == each_source_list[target_node] &&
                               case_tag == 0u;
            } else if (target_kind == 7u) {
                valid_target = target_node <= each_count &&
                               target_type == each_item_output[target_node] &&
                               case_tag == 0u;
            }
        }
        bool valid_presentation =
            (flags == 0u && presentation == 0u) ||
            (flags == 1u && presentation == target_type);
        if (!valid_source || !valid_target || !valid_presentation ||
            (source_kind != 5u && source_kind != 6u && source_kind != 7u &&
             source_kind != 8u && source_kind != 9u &&
             target_kind != 5u && case_tag != 0u) ||
            read_u16(bytes, offset + 14u) != 0u) {
            return HERMAS2_IMAGE_INVALID_RECORD;
        }
        for (size_t prior = 0u; prior < index; ++prior) {
            size_t previous = edges_offset + prior * HERMAS2_EDGE_RECORD_SIZE;
            if (source_kind == bytes[previous] &&
                source_node == read_u16(bytes, previous + 4u) &&
                case_tag == bytes[previous + 3u] &&
                target_kind == bytes[previous + 1u] &&
                target_node == read_u16(bytes, previous + 6u)) {
                return HERMAS2_IMAGE_DUPLICATE_RECORD;
            }
        }
        if ((target_kind == 1u || target_kind == 3u || target_kind == 4u) &&
            ++input_counts[target_node] > 1u) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
        if (target_kind == 5u) {
            if (++join_input_counts[target_node][case_tag] > 1u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
            join_input_types[target_node][case_tag] = target_type;
        }
        if (target_kind == 6u &&
            ++each_input_counts[target_node] > 1u) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
        if (target_kind == 7u &&
            ++each_collect_counts[target_node] > 1u) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
        if (source_kind == 0u) {
            ++workflow_input_count;
            uint16_t graph_target =
                target_kind == 6u ? each_template[target_node] :
                target_kind == 7u ? 0u : target_node;
            if (graph_target != 0u) {
                reachable[graph_target] = true;
            }
        } else if (source_kind == 1u) {
            ++success_counts[source_node];
            action_success_type[source_node] = source_type;
        } else if (source_kind == 2u) {
            ++error_counts[source_node];
        } else if (source_kind == 3u) {
            ++not_sent_counts[source_node];
        } else if (source_kind == 4u) {
            ++unknown_counts[source_node];
        } else if (source_kind == 5u) {
            if (++dispatch_case_counts[source_node][case_tag] > 1u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
        } else if (source_kind == 6u) {
            if (++fork_branch_counts[source_node][case_tag] > 1u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
        } else if (source_kind == 7u) {
            if (++join_output_counts[source_node][case_tag] > 1u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
            join_output_types[source_node][case_tag] = source_type;
        } else if (source_kind == 8u) {
            if (++each_item_counts[source_node] > 1u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
        } else if (++each_output_counts[source_node] > 1u) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
    }
    if (workflow_input_count != 1u) {
        return HERMAS2_IMAGE_INVALID_TOPOLOGY;
    }
    for (size_t node = 1u; node <= node_count; ++node) {
        if (action_nodes[node] &&
            (input_counts[node] != 1u || success_counts[node] != 1u ||
             error_counts[node] != 1u || not_sent_counts[node] != 1u ||
             unknown_counts[node] != 1u)) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
        if (action_nodes[node] && saga_count != 0u &&
            (saga_source_type[node] == 0u ||
             saga_source_type[node] != action_success_type[node])) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
        if (dispatch_types[node] != 0u) {
            if (input_counts[node] != 1u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
            for (size_t tag = 0u; tag < dispatch_cases[node]; ++tag) {
                if (dispatch_case_counts[node][tag] != 1u) {
                    return HERMAS2_IMAGE_INVALID_TOPOLOGY;
                }
            }
        }
        if (fork_types[node] != 0u) {
            if (input_counts[node] != 1u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
            for (size_t tag = 0u; tag < fork_branches[node]; ++tag) {
                if (fork_branch_counts[node][tag] != 1u) {
                    return HERMAS2_IMAGE_INVALID_TOPOLOGY;
                }
            }
        }
        if (join_branches[node] != 0u) {
            unsigned used_outputs = 0u;
            for (size_t tag = 0u; tag < join_branches[node]; ++tag) {
                used_outputs += join_output_counts[node][tag];
                if (join_input_counts[node][tag] != 1u ||
                    join_output_counts[node][tag] > 1u ||
                    (join_output_counts[node][tag] == 1u &&
                     join_input_types[node][tag] != join_output_types[node][tag])) {
                    return HERMAS2_IMAGE_INVALID_TOPOLOGY;
                }
            }
            if (used_outputs == 0u) {
                return HERMAS2_IMAGE_INVALID_TOPOLOGY;
            }
        }
    }
    if (saga_count != 0u) {
        size_t action_count = 0u;
        for (size_t node = 1u; node <= node_count; ++node) {
            action_count += action_nodes[node] ? 1u : 0u;
        }
        if (saga_count != action_count) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
    }
    for (size_t region = 1u; region <= each_count; ++region) {
        if (each_input_counts[region] != 1u ||
            each_collect_counts[region] != 1u ||
            each_item_counts[region] != 1u ||
            each_output_counts[region] != 1u) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
    }
    for (size_t pass = 0u; pass < node_count; ++pass) {
        bool changed = false;
        for (size_t index = 0u; index < edge_count; ++index) {
            size_t offset = edges_offset + index * HERMAS2_EDGE_RECORD_SIZE;
            uint8_t source_kind = bytes[offset];
            uint8_t target_kind = bytes[offset + 1u];
            uint16_t source_node = read_u16(bytes, offset + 4u);
            uint16_t target_node = read_u16(bytes, offset + 6u);
            uint16_t graph_source =
                source_kind == 8u ? 0u :
                source_kind == 9u ? each_template[source_node] :
                source_node;
            uint16_t graph_target =
                target_kind == 6u ? each_template[target_node] :
                target_kind == 7u ? 0u :
                target_node;
            if (source_kind != 0u && graph_source != 0u &&
                graph_target != 0u && reachable[graph_source] &&
                !reachable[graph_target]) {
                reachable[graph_target] = true;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    for (size_t node = 1u; node <= node_count; ++node) {
        if (!reachable[node]) {
            return HERMAS2_IMAGE_INVALID_TOPOLOGY;
        }
    }
    if (summary != NULL) {
        summary->workflow_name = bytes + name_offset;
        summary->workflow_name_length = name_length;
        summary->input_type = input_type;
        summary->success_type = success_type;
        summary->error_count = error_count;
        summary->app_count = app_count;
        summary->type_count = type_count;
        summary->node_count = node_count;
        summary->edge_count = edge_count;
    }
    return HERMAS2_IMAGE_OK;
}

hermas2_image_result hermas2_image_validate_value(
    const uint8_t *image,
    size_t image_size,
    uint16_t type_id,
    const uint8_t *payload,
    size_t payload_size) {
    if ((payload == NULL && payload_size != 0u) ||
        hermas2_image_validate(image, image_size, NULL) != HERMAS2_IMAGE_OK) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    uint16_t type_count = read_u16(image, 34u);
    size_t types_offset = read_u32(image, 44u);
    size_t representation_end = read_u32(image, 60u);
    size_t representation_cursor = 0u;
    bool found = false;
    for (size_t index = 0u; index < type_count; ++index) {
        size_t offset = types_offset + index * HERMAS2_TYPE_RECORD_SIZE;
        if (read_u16(image, offset) == type_id) {
            representation_cursor = read_u32(image, offset + 4u);
            found = true;
            break;
        }
    }
    if (!found) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    size_t payload_cursor = 0u;
    hermas2_image_result result = validate_value_inner(
        image, representation_end, &representation_cursor,
        payload, payload_size, &payload_cursor, 0u);
    return result == HERMAS2_IMAGE_OK && payload_cursor == payload_size
               ? HERMAS2_IMAGE_OK
               : HERMAS2_IMAGE_INVALID_VALUE;
}

hermas2_image_result hermas2_image_list_items(
    const uint8_t *image,
    size_t image_size,
    uint16_t type_id,
    const uint8_t *payload,
    size_t payload_size,
    uint16_t *item_count,
    size_t *item_offsets,
    size_t *item_lengths,
    size_t item_capacity) {
    if (image == NULL || payload == NULL || item_count == NULL ||
        item_offsets == NULL || item_lengths == NULL ||
        hermas2_image_validate(image, image_size, NULL) != HERMAS2_IMAGE_OK ||
        payload_size < 8u) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    size_t descriptor = 0u;
    size_t descriptor_end = 0u;
    if (!type_descriptor(
            image, read_u32(image, 44u), read_u16(image, 34u),
            read_u32(image, 60u), type_id, &descriptor,
            &descriptor_end) ||
        image[descriptor] != 7u) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    uint32_t count = read_u32(payload, 0u);
    if (read_u32(payload, 4u) != 0u ||
        count > read_u32(image, descriptor + 4u) ||
        count > item_capacity || count > UINT16_MAX) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    size_t child_start = descriptor + 8u;
    size_t child_end = child_start;
    if (validate_representation(
            image, descriptor_end, &child_end, 1u) != HERMAS2_IMAGE_OK) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    size_t payload_cursor = 8u;
    for (size_t index = 0u; index < count; ++index) {
        size_t item_start = payload_cursor;
        size_t child_cursor = child_start;
        if (validate_value_inner(
                image, descriptor_end, &child_cursor, payload,
                payload_size, &payload_cursor, 1u) != HERMAS2_IMAGE_OK ||
            child_cursor != child_end) {
            return HERMAS2_IMAGE_INVALID_VALUE;
        }
        item_offsets[index] = item_start;
        item_lengths[index] = payload_cursor - item_start;
    }
    if (payload_cursor != payload_size) {
        return HERMAS2_IMAGE_INVALID_VALUE;
    }
    *item_count = (uint16_t)count;
    return HERMAS2_IMAGE_OK;
}

const char *hermas2_image_result_name(hermas2_image_result result) {
    static const char *const names[] = {
        "ok", "truncated", "bad-magic", "unsupported-version",
        "invalid-header", "invalid-offset", "invalid-count", "invalid-string",
        "invalid-record", "duplicate-record", "invalid-topology", "invalid-value"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
