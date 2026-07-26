#include "hermas2/compensation.h"

#include <stdbool.h>
#include <string.h>

static void put_u16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    for (size_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value) {
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static uint16_t get_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t get_u32(const uint8_t *bytes, size_t offset) {
    uint32_t value = 0u;
    for (size_t index = 0u; index < 4u; ++index) {
        value |= (uint32_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static uint64_t get_u64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static uint32_t checksum_parts(
    const uint8_t *header,
    const uint8_t *token,
    size_t token_length) {
    uint32_t value = UINT32_C(0xffffffff);
    for (size_t part = 0u; part < 2u; ++part) {
        const uint8_t *bytes = part == 0u ? header : token;
        size_t length = part == 0u ? 68u : token_length;
        for (size_t index = 0u; index < length; ++index) {
            value ^= bytes[index];
            for (unsigned bit = 0u; bit < 8u; ++bit) {
                uint32_t mask =
                    (uint32_t)-(int32_t)(value & 1u);
                value = (value >> 1u) ^
                        (UINT32_C(0xedb88320) & mask);
            }
        }
    }
    return ~value;
}

static bool key_equal(
    hermas2_compensation_key left,
    hermas2_compensation_key right) {
    return left.execution_id == right.execution_id &&
           left.workflow_id == right.workflow_id &&
           left.request_id == right.request_id &&
           left.node_id == right.node_id &&
           left.image_fingerprint == right.image_fingerprint;
}

static bool record_valid(const hermas2_compensation_record *record) {
    return record->sequence != 0u &&
           record->key.execution_id != 0u &&
           record->key.workflow_id != 0u &&
           record->key.request_id != 0u &&
           record->key.node_id != 0u &&
           record->key.image_fingerprint != 0u &&
           record->compensation_app_id != 0u &&
           record->compensation_action_id != 0u &&
           record->source_type != 0u &&
           record->destination_type != 0u &&
           record->token_length <= HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE &&
           (record->token_length == 0u || record->token != NULL);
}

hermas2_compensation_result hermas2_compensation_encode(
    const hermas2_compensation_record *record,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size) {
    if (record == NULL || destination == NULL || encoded_size == NULL) {
        return HERMAS2_COMPENSATION_INVALID_ARGUMENT;
    }
    if (!record_valid(record)) {
        return HERMAS2_COMPENSATION_INVALID_RECORD;
    }
    size_t size = HERMAS2_COMPENSATION_HEADER_SIZE +
                  record->token_length;
    if (destination_size < size) {
        return HERMAS2_COMPENSATION_BUFFER_TOO_SMALL;
    }
    memset(destination, 0, HERMAS2_COMPENSATION_HEADER_SIZE);
    memcpy(destination, "H2CT", 4u);
    put_u16(destination, 4u, HERMAS2_COMPENSATION_VERSION);
    put_u16(destination, 6u, HERMAS2_COMPENSATION_HEADER_SIZE);
    put_u32(destination, 8u, (uint32_t)size);
    put_u32(destination, 12u, record->token_length);
    put_u64(destination, 16u, record->sequence);
    put_u64(destination, 24u, record->key.execution_id);
    put_u32(destination, 32u, record->key.workflow_id);
    put_u64(destination, 36u, record->key.request_id);
    put_u16(destination, 44u, record->key.node_id);
    put_u16(destination, 46u, record->compensation_app_id);
    put_u16(destination, 48u, record->compensation_action_id);
    put_u16(destination, 50u, record->source_type);
    put_u16(destination, 52u, record->destination_type);
    put_u64(destination, 56u, record->key.image_fingerprint);
    if (record->token_length != 0u) {
        memcpy(destination + HERMAS2_COMPENSATION_HEADER_SIZE,
               record->token, record->token_length);
    }
    put_u32(destination, 68u, checksum_parts(
        destination,
        destination + HERMAS2_COMPENSATION_HEADER_SIZE,
        record->token_length));
    *encoded_size = size;
    return HERMAS2_COMPENSATION_OK;
}

hermas2_compensation_result hermas2_compensation_decode(
    const uint8_t *source,
    size_t source_size,
    hermas2_compensation_record *record,
    size_t *record_size) {
    if (source == NULL || record == NULL || record_size == NULL) {
        return HERMAS2_COMPENSATION_INVALID_ARGUMENT;
    }
    if (source_size < HERMAS2_COMPENSATION_HEADER_SIZE) {
        return HERMAS2_COMPENSATION_BUFFER_TOO_SMALL;
    }
    uint32_t size = get_u32(source, 8u);
    uint32_t token_length = get_u32(source, 12u);
    if (memcmp(source, "H2CT", 4u) != 0 ||
        get_u16(source, 4u) != HERMAS2_COMPENSATION_VERSION ||
        get_u16(source, 6u) != HERMAS2_COMPENSATION_HEADER_SIZE ||
        size != HERMAS2_COMPENSATION_HEADER_SIZE + token_length ||
        size > source_size || get_u16(source, 54u) != 0u ||
        get_u32(source, 64u) != 0u ||
        token_length > HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return HERMAS2_COMPENSATION_INVALID_RECORD;
    }
    if (get_u32(source, 68u) != checksum_parts(
            source, source + HERMAS2_COMPENSATION_HEADER_SIZE,
            token_length)) {
        return HERMAS2_COMPENSATION_CHECKSUM_MISMATCH;
    }
    hermas2_compensation_record decoded = {
        .sequence = get_u64(source, 16u),
        .key = {
            .execution_id = get_u64(source, 24u),
            .workflow_id = get_u32(source, 32u),
            .request_id = get_u64(source, 36u),
            .node_id = get_u16(source, 44u),
            .image_fingerprint = get_u64(source, 56u)
        },
        .compensation_app_id = get_u16(source, 46u),
        .compensation_action_id = get_u16(source, 48u),
        .source_type = get_u16(source, 50u),
        .destination_type = get_u16(source, 52u),
        .token = source + HERMAS2_COMPENSATION_HEADER_SIZE,
        .token_length = token_length
    };
    if (!record_valid(&decoded)) {
        return HERMAS2_COMPENSATION_INVALID_RECORD;
    }
    *record = decoded;
    *record_size = size;
    return HERMAS2_COMPENSATION_OK;
}

hermas2_compensation_result hermas2_compensation_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_compensation_visitor visitor,
    void *visitor_context,
    hermas2_compensation_summary *summary) {
    if ((bytes == NULL && byte_count != 0u) || summary == NULL) {
        return HERMAS2_COMPENSATION_INVALID_ARGUMENT;
    }
    hermas2_compensation_summary result = {
        .next_sequence = 1u
    };
    size_t offset = 0u;
    while (offset < byte_count) {
        hermas2_compensation_record record;
        size_t size = 0u;
        hermas2_compensation_result decoded =
            hermas2_compensation_decode(
                bytes + offset, byte_count - offset, &record, &size);
        if (decoded != HERMAS2_COMPENSATION_OK) {
            return decoded;
        }
        if (record.sequence != result.next_sequence) {
            return HERMAS2_COMPENSATION_INVALID_SEQUENCE;
        }
        if (visitor != NULL) {
            hermas2_compensation_result visited =
                visitor(visitor_context, &record);
            if (visited != HERMAS2_COMPENSATION_OK) {
                return visited;
            }
        }
        ++result.record_count;
        ++result.next_sequence;
        offset += size;
    }
    *summary = result;
    return HERMAS2_COMPENSATION_OK;
}

hermas2_compensation_result hermas2_compensation_find(
    const uint8_t *bytes,
    size_t byte_count,
    hermas2_compensation_key key,
    hermas2_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found) {
    if ((bytes == NULL && byte_count != 0u) || record == NULL ||
        token == NULL || found == NULL) {
        return HERMAS2_COMPENSATION_INVALID_ARGUMENT;
    }
    *found = 0;
    size_t offset = 0u;
    while (offset < byte_count) {
        hermas2_compensation_record candidate;
        size_t size = 0u;
        hermas2_compensation_result decoded =
            hermas2_compensation_decode(
                bytes + offset, byte_count - offset, &candidate, &size);
        if (decoded != HERMAS2_COMPENSATION_OK) {
            return decoded;
        }
        if (key_equal(candidate.key, key)) {
            if (*found != 0) {
                return HERMAS2_COMPENSATION_DUPLICATE_TOKEN;
            }
            if (candidate.token_length > token_capacity) {
                return HERMAS2_COMPENSATION_BUFFER_TOO_SMALL;
            }
            memcpy(token, candidate.token, candidate.token_length);
            *record = candidate;
            record->token = token;
            *found = 1;
        }
        offset += size;
    }
    return HERMAS2_COMPENSATION_OK;
}

hermas2_compensation_result hermas2_compensation_writer_init(
    hermas2_compensation_writer *writer,
    hermas2_compensation_write write,
    void *context,
    uint64_t next_sequence) {
    if (writer == NULL || write == NULL || next_sequence == 0u) {
        return HERMAS2_COMPENSATION_INVALID_ARGUMENT;
    }
    *writer = (hermas2_compensation_writer){
        .write = write,
        .context = context,
        .next_sequence = next_sequence
    };
    return HERMAS2_COMPENSATION_OK;
}

hermas2_compensation_result hermas2_compensation_writer_append(
    hermas2_compensation_writer *writer,
    hermas2_compensation_record record,
    uint8_t *scratch,
    size_t scratch_capacity) {
    if (writer == NULL || writer->write == NULL || scratch == NULL ||
        writer->next_sequence == 0u ||
        writer->next_sequence == UINT64_MAX) {
        return HERMAS2_COMPENSATION_INVALID_ARGUMENT;
    }
    record.sequence = writer->next_sequence;
    size_t size = 0u;
    hermas2_compensation_result encoded =
        hermas2_compensation_encode(
            &record, scratch, scratch_capacity, &size);
    if (encoded != HERMAS2_COMPENSATION_OK) {
        return encoded;
    }
    hermas2_compensation_result written =
        writer->write(writer->context, scratch, size);
    if (written != HERMAS2_COMPENSATION_OK) {
        return written;
    }
    ++writer->next_sequence;
    return HERMAS2_COMPENSATION_OK;
}

const char *hermas2_compensation_result_name(
    hermas2_compensation_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "buffer-too-small", "invalid-record",
        "checksum-mismatch", "invalid-sequence", "duplicate-token",
        "write-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
