#include "hermas/result.h"

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
    const uint8_t *value_bytes,
    size_t value_length) {
    uint32_t value = UINT32_C(0xffffffff);
    for (size_t part = 0u; part < 2u; ++part) {
        const uint8_t *bytes =
            part == 0u ? header : value_bytes;
        size_t length = part == 0u ? 56u : value_length;
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
    hermas_result_key left,
    hermas_result_key right) {
    return left.execution_id == right.execution_id &&
           left.workflow_id == right.workflow_id &&
           left.image_fingerprint == right.image_fingerprint;
}

static bool record_valid(const hermas_result_record *record) {
    return record->sequence != 0u &&
           record->key.execution_id != 0u &&
           record->key.workflow_id != 0u &&
           record->key.image_fingerprint != 0u &&
           (record->outcome == HERMAS_OUTCOME_SUCCESS ||
            record->outcome == HERMAS_OUTCOME_APP_ERROR) &&
           record->source_type != 0u &&
           record->destination_type != 0u &&
           record->value_length <=
               HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE &&
           (record->value_length == 0u || record->value != NULL);
}

hermas_result_store_result hermas_result_encode(
    const hermas_result_record *record,
    uint8_t *destination,
    size_t destination_size,
    size_t *encoded_size) {
    if (record == NULL || destination == NULL ||
        encoded_size == NULL) {
        return HERMAS_RESULT_STORE_INVALID_ARGUMENT;
    }
    if (!record_valid(record)) {
        return HERMAS_RESULT_STORE_INVALID_RECORD;
    }
    size_t size = HERMAS_RESULT_HEADER_SIZE +
                  record->value_length;
    if (destination_size < size) {
        return HERMAS_RESULT_STORE_BUFFER_TOO_SMALL;
    }
    memset(destination, 0, HERMAS_RESULT_HEADER_SIZE);
    memcpy(destination, "HRS1", 4u);
    put_u16(destination, 4u, HERMAS_RESULT_VERSION);
    put_u16(destination, 6u, HERMAS_RESULT_HEADER_SIZE);
    put_u32(destination, 8u, (uint32_t)size);
    put_u32(destination, 12u, record->value_length);
    put_u64(destination, 16u, record->sequence);
    put_u64(destination, 24u, record->key.execution_id);
    put_u32(destination, 32u, record->key.workflow_id);
    put_u64(destination, 40u, record->key.image_fingerprint);
    put_u16(destination, 48u, record->outcome);
    put_u16(destination, 50u, record->source_type);
    put_u16(destination, 52u, record->destination_type);
    if (record->value_length != 0u) {
        memcpy(
            destination + HERMAS_RESULT_HEADER_SIZE,
            record->value, record->value_length);
    }
    put_u32(destination, 56u, checksum_parts(
        destination, destination + HERMAS_RESULT_HEADER_SIZE,
        record->value_length));
    *encoded_size = size;
    return HERMAS_RESULT_STORE_OK;
}

hermas_result_store_result hermas_result_decode(
    const uint8_t *source,
    size_t source_size,
    hermas_result_record *record,
    size_t *record_size) {
    if (source == NULL || record == NULL || record_size == NULL) {
        return HERMAS_RESULT_STORE_INVALID_ARGUMENT;
    }
    if (source_size < HERMAS_RESULT_HEADER_SIZE) {
        return HERMAS_RESULT_STORE_BUFFER_TOO_SMALL;
    }
    uint32_t size = get_u32(source, 8u);
    uint32_t value_length = get_u32(source, 12u);
    if (memcmp(source, "HRS1", 4u) != 0 ||
        get_u16(source, 4u) != HERMAS_RESULT_VERSION ||
        get_u16(source, 6u) != HERMAS_RESULT_HEADER_SIZE ||
        size != HERMAS_RESULT_HEADER_SIZE + value_length ||
        size > source_size || get_u32(source, 36u) != 0u ||
        get_u16(source, 54u) != 0u ||
        get_u32(source, 60u) != 0u ||
        value_length > HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return HERMAS_RESULT_STORE_INVALID_RECORD;
    }
    if (get_u32(source, 56u) != checksum_parts(
            source, source + HERMAS_RESULT_HEADER_SIZE,
            value_length)) {
        return HERMAS_RESULT_STORE_CHECKSUM_MISMATCH;
    }
    hermas_result_record decoded = {
        .sequence = get_u64(source, 16u),
        .key = {
            .execution_id = get_u64(source, 24u),
            .workflow_id = get_u32(source, 32u),
            .image_fingerprint = get_u64(source, 40u)
        },
        .outcome = get_u16(source, 48u),
        .source_type = get_u16(source, 50u),
        .destination_type = get_u16(source, 52u),
        .value = source + HERMAS_RESULT_HEADER_SIZE,
        .value_length = value_length
    };
    if (!record_valid(&decoded)) {
        return HERMAS_RESULT_STORE_INVALID_RECORD;
    }
    *record = decoded;
    *record_size = size;
    return HERMAS_RESULT_STORE_OK;
}

hermas_result_store_result hermas_result_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_result_visitor visitor,
    void *visitor_context,
    hermas_result_summary *summary) {
    if ((bytes == NULL && byte_count != 0u) || summary == NULL) {
        return HERMAS_RESULT_STORE_INVALID_ARGUMENT;
    }
    hermas_result_summary result = {.next_sequence = 1u};
    size_t offset = 0u;
    while (offset < byte_count) {
        hermas_result_record record;
        size_t size = 0u;
        hermas_result_store_result decoded =
            hermas_result_decode(
                bytes + offset, byte_count - offset,
                &record, &size);
        if (decoded != HERMAS_RESULT_STORE_OK) {
            return decoded;
        }
        if (record.sequence != result.next_sequence) {
            return HERMAS_RESULT_STORE_INVALID_SEQUENCE;
        }
        size_t prior_offset = 0u;
        while (prior_offset < offset) {
            hermas_result_record prior;
            size_t prior_size = 0u;
            hermas_result_store_result prior_decoded =
                hermas_result_decode(
                    bytes + prior_offset,
                    offset - prior_offset,
                    &prior, &prior_size);
            if (prior_decoded != HERMAS_RESULT_STORE_OK) {
                return prior_decoded;
            }
            if (key_equal(prior.key, record.key)) {
                return HERMAS_RESULT_STORE_DUPLICATE_RESULT;
            }
            prior_offset += prior_size;
        }
        if (visitor != NULL) {
            hermas_result_store_result visited =
                visitor(visitor_context, &record);
            if (visited != HERMAS_RESULT_STORE_OK) {
                return visited;
            }
        }
        ++result.record_count;
        ++result.next_sequence;
        offset += size;
    }
    *summary = result;
    return HERMAS_RESULT_STORE_OK;
}

hermas_result_store_result hermas_result_find(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_result_key key,
    hermas_result_record *record,
    uint8_t *value,
    size_t value_capacity,
    int *found) {
    if ((bytes == NULL && byte_count != 0u) || record == NULL ||
        value == NULL || found == NULL) {
        return HERMAS_RESULT_STORE_INVALID_ARGUMENT;
    }
    *found = 0;
    size_t offset = 0u;
    while (offset < byte_count) {
        hermas_result_record candidate;
        size_t size = 0u;
        hermas_result_store_result decoded =
            hermas_result_decode(
                bytes + offset, byte_count - offset,
                &candidate, &size);
        if (decoded != HERMAS_RESULT_STORE_OK) {
            return decoded;
        }
        if (key_equal(candidate.key, key)) {
            if (*found != 0) {
                return HERMAS_RESULT_STORE_DUPLICATE_RESULT;
            }
            if (candidate.value_length > value_capacity) {
                return HERMAS_RESULT_STORE_BUFFER_TOO_SMALL;
            }
            memcpy(value, candidate.value, candidate.value_length);
            *record = candidate;
            record->value = value;
            *found = 1;
        }
        offset += size;
    }
    return HERMAS_RESULT_STORE_OK;
}

hermas_result_store_result hermas_result_writer_init(
    hermas_result_writer *writer,
    hermas_result_write write,
    void *context,
    uint64_t next_sequence) {
    if (writer == NULL || write == NULL || next_sequence == 0u) {
        return HERMAS_RESULT_STORE_INVALID_ARGUMENT;
    }
    *writer = (hermas_result_writer){
        .write = write,
        .context = context,
        .next_sequence = next_sequence
    };
    return HERMAS_RESULT_STORE_OK;
}

hermas_result_store_result hermas_result_writer_append(
    hermas_result_writer *writer,
    hermas_result_record record,
    uint8_t *scratch,
    size_t scratch_capacity) {
    if (writer == NULL || writer->write == NULL || scratch == NULL ||
        writer->next_sequence == 0u ||
        writer->next_sequence == UINT64_MAX) {
        return HERMAS_RESULT_STORE_INVALID_ARGUMENT;
    }
    record.sequence = writer->next_sequence;
    size_t size = 0u;
    hermas_result_store_result encoded =
        hermas_result_encode(
            &record, scratch, scratch_capacity, &size);
    if (encoded != HERMAS_RESULT_STORE_OK) {
        return encoded;
    }
    hermas_result_store_result written =
        writer->write(writer->context, scratch, size);
    if (written != HERMAS_RESULT_STORE_OK) {
        return written;
    }
    ++writer->next_sequence;
    return HERMAS_RESULT_STORE_OK;
}

const char *hermas_result_store_result_name(
    hermas_result_store_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "buffer-too-small",
        "invalid-record", "checksum-mismatch", "invalid-sequence",
        "duplicate-result", "write-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
