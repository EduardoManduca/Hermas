#include "hermas/saga_log.h"

#include <stdbool.h>
#include <string.h>

typedef enum delivery_state {
    DELIVERY_NONE = 0,
    DELIVERY_PREPARED,
    DELIVERY_SENT
} delivery_state;

typedef struct scan_active {
    hermas_saga_log_active value;
    delivery_state delivery;
    bool terminal;
    bool active;
} scan_active;

static void write_u16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)(value & 0xffu);
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    for (size_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] =
            (uint8_t)((value >> (index * 8u)) & 0xffu);
    }
}

static void write_u64(uint8_t *bytes, size_t offset, uint64_t value) {
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[offset + index] =
            (uint8_t)((value >> (index * 8u)) & UINT64_C(0xff));
    }
}

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    uint32_t value = 0u;
    for (size_t index = 0u; index < 4u; ++index) {
        value |= (uint32_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static uint64_t read_u64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static uint32_t checksum(const uint8_t *bytes, size_t length) {
    uint32_t value = UINT32_C(0xffffffff);
    for (size_t index = 0u; index < length; ++index) {
        value ^= bytes[index];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            uint32_t mask =
                (uint32_t)-(int32_t)(value & UINT32_C(1));
            value = (value >> 1u) ^
                    (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~value;
}

static bool action_kind(hermas_saga_log_kind kind) {
    return kind >= HERMAS_SAGA_LOG_DELIVERY_PREPARED &&
           kind <= HERMAS_SAGA_LOG_STEP_UNKNOWN;
}

static hermas_saga_log_result validate(
    const hermas_saga_log_record *record) {
    if (record->kind < HERMAS_SAGA_LOG_STARTED ||
        record->kind > HERMAS_SAGA_LOG_FINISHED ||
        record->sequence == 0u || record->execution_id == 0u ||
        record->workflow_id == 0u ||
        record->image_fingerprint == 0u) {
        return HERMAS_SAGA_LOG_INVALID_RECORD;
    }
    bool outcome_valid = false;
    switch (record->kind) {
        case HERMAS_SAGA_LOG_STARTED:
            outcome_valid =
                record->outcome == HERMAS_OUTCOME_APP_ERROR ||
                record->outcome == HERMAS_OUTCOME_NOT_SENT;
            break;
        case HERMAS_SAGA_LOG_DELIVERY_PREPARED:
        case HERMAS_SAGA_LOG_DELIVERY_SENT:
            outcome_valid = record->outcome == HERMAS_OUTCOME_NONE;
            break;
        case HERMAS_SAGA_LOG_STEP_SUCCEEDED:
            outcome_valid =
                record->outcome == HERMAS_OUTCOME_SUCCESS;
            break;
        case HERMAS_SAGA_LOG_STEP_FAILED:
            outcome_valid =
                record->outcome == HERMAS_OUTCOME_APP_ERROR ||
                record->outcome == HERMAS_OUTCOME_NOT_SENT;
            break;
        case HERMAS_SAGA_LOG_STEP_UNKNOWN:
            outcome_valid =
                record->outcome == HERMAS_OUTCOME_UNKNOWN;
            break;
        case HERMAS_SAGA_LOG_FINISHED:
            outcome_valid =
                record->outcome == HERMAS_OUTCOME_SUCCESS ||
                record->outcome == HERMAS_OUTCOME_APP_ERROR ||
                record->outcome == HERMAS_OUTCOME_NOT_SENT ||
                record->outcome == HERMAS_OUTCOME_UNKNOWN;
            break;
    }
    if (!outcome_valid) {
        return HERMAS_SAGA_LOG_INVALID_RECORD;
    }
    bool action = action_kind(record->kind);
    bool route_present =
        record->request_id != 0u && record->forward_node != 0u &&
        record->app_id != 0u && record->action_id != 0u &&
        record->ordinal != 0u &&
        record->ordinal <= HERMAS_SAGA_LOG_MAX_STEPS;
    if ((action && !route_present) ||
        (!action && record->kind == HERMAS_SAGA_LOG_FINISHED &&
         (record->request_id != 0u || record->forward_node != 0u ||
          record->app_id != 0u || record->action_id != 0u ||
          record->ordinal != 0u)) ||
        (record->kind == HERMAS_SAGA_LOG_STARTED &&
         (record->request_id != 0u || record->forward_node != 0u ||
          record->app_id != 0u || record->action_id != 0u ||
          record->ordinal == 0u ||
          record->ordinal > HERMAS_SAGA_LOG_MAX_STEPS))) {
        return HERMAS_SAGA_LOG_INVALID_RECORD;
    }
    return HERMAS_SAGA_LOG_OK;
}

hermas_saga_log_result hermas_saga_log_encode(
    const hermas_saga_log_record *record,
    uint8_t *destination,
    size_t destination_size) {
    if (record == NULL || destination == NULL) {
        return HERMAS_SAGA_LOG_INVALID_ARGUMENT;
    }
    if (destination_size < HERMAS_SAGA_LOG_RECORD_SIZE) {
        return HERMAS_SAGA_LOG_BUFFER_TOO_SMALL;
    }
    hermas_saga_log_result valid = validate(record);
    if (valid != HERMAS_SAGA_LOG_OK) {
        return valid;
    }
    memset(destination, 0, HERMAS_SAGA_LOG_RECORD_SIZE);
    memcpy(destination, "HSG1", 4u);
    write_u16(destination, 4u, HERMAS_SAGA_LOG_VERSION);
    write_u16(destination, 6u, HERMAS_SAGA_LOG_RECORD_SIZE);
    write_u16(destination, 8u, (uint16_t)record->kind);
    write_u16(destination, 10u, record->outcome);
    write_u64(destination, 16u, record->sequence);
    write_u64(destination, 24u, record->execution_id);
    write_u32(destination, 32u, record->workflow_id);
    write_u64(destination, 36u, record->request_id);
    write_u16(destination, 44u, record->forward_node);
    write_u16(destination, 46u, record->app_id);
    write_u16(destination, 48u, record->action_id);
    write_u16(destination, 50u, record->ordinal);
    write_u64(destination, 52u, record->image_fingerprint);
    write_u32(destination, 60u, checksum(destination, 60u));
    return HERMAS_SAGA_LOG_OK;
}

hermas_saga_log_result hermas_saga_log_decode(
    const uint8_t *source,
    size_t source_size,
    hermas_saga_log_record *record) {
    if (source == NULL || record == NULL) {
        return HERMAS_SAGA_LOG_INVALID_ARGUMENT;
    }
    if (source_size < HERMAS_SAGA_LOG_RECORD_SIZE) {
        return HERMAS_SAGA_LOG_BUFFER_TOO_SMALL;
    }
    if (memcmp(source, "HSG1", 4u) != 0 ||
        read_u16(source, 4u) != HERMAS_SAGA_LOG_VERSION ||
        read_u16(source, 6u) != HERMAS_SAGA_LOG_RECORD_SIZE ||
        read_u32(source, 12u) != 0u) {
        return HERMAS_SAGA_LOG_INVALID_RECORD;
    }
    if (read_u32(source, 60u) != checksum(source, 60u)) {
        return HERMAS_SAGA_LOG_CHECKSUM_MISMATCH;
    }
    hermas_saga_log_record decoded = {
        .kind = (hermas_saga_log_kind)read_u16(source, 8u),
        .outcome = read_u16(source, 10u),
        .sequence = read_u64(source, 16u),
        .execution_id = read_u64(source, 24u),
        .workflow_id = read_u32(source, 32u),
        .request_id = read_u64(source, 36u),
        .forward_node = read_u16(source, 44u),
        .app_id = read_u16(source, 46u),
        .action_id = read_u16(source, 48u),
        .ordinal = read_u16(source, 50u),
        .image_fingerprint = read_u64(source, 52u)
    };
    hermas_saga_log_result valid = validate(&decoded);
    if (valid != HERMAS_SAGA_LOG_OK) {
        return valid;
    }
    *record = decoded;
    return HERMAS_SAGA_LOG_OK;
}

static scan_active *find_active(
    scan_active *active,
    uint64_t execution_id) {
    for (size_t index = 0u; index < HERMAS_SAGA_LOG_MAX_ACTIVE;
         ++index) {
        if (active[index].active &&
            active[index].value.execution_id == execution_id) {
            return &active[index];
        }
    }
    return NULL;
}

static bool route_matches(
    const hermas_saga_log_active *active,
    const hermas_saga_log_record *record) {
    return active->request_id == record->request_id &&
           active->forward_node == record->forward_node &&
           active->app_id == record->app_id &&
           active->action_id == record->action_id &&
           active->next_ordinal == record->ordinal;
}

static hermas_saga_log_result transition(
    scan_active *entries,
    const hermas_saga_log_record *record) {
    scan_active *entry = find_active(entries, record->execution_id);
    if (record->kind == HERMAS_SAGA_LOG_STARTED) {
        if (entry != NULL) {
            return HERMAS_SAGA_LOG_INVALID_TRANSITION;
        }
        for (size_t index = 0u; index < HERMAS_SAGA_LOG_MAX_ACTIVE;
             ++index) {
            if (!entries[index].active) {
                entries[index].active = true;
                entries[index].value.execution_id =
                    record->execution_id;
                entries[index].value.workflow_id =
                    record->workflow_id;
                entries[index].value.image_fingerprint =
                    record->image_fingerprint;
                entries[index].value.next_ordinal =
                    record->ordinal;
                return HERMAS_SAGA_LOG_OK;
            }
        }
        return HERMAS_SAGA_LOG_CAPACITY_EXHAUSTED;
    }
    if (entry == NULL ||
        entry->value.workflow_id != record->workflow_id ||
        entry->value.image_fingerprint !=
            record->image_fingerprint) {
        return HERMAS_SAGA_LOG_INVALID_TRANSITION;
    }
    if (record->kind == HERMAS_SAGA_LOG_DELIVERY_PREPARED) {
        if (entry->delivery != DELIVERY_NONE || entry->terminal ||
            record->ordinal != entry->value.next_ordinal) {
            return HERMAS_SAGA_LOG_INVALID_TRANSITION;
        }
        entry->delivery = DELIVERY_PREPARED;
        entry->value.has_open_delivery = 1u;
        entry->value.delivery_was_sent = 0u;
        entry->value.request_id = record->request_id;
        entry->value.forward_node = record->forward_node;
        entry->value.app_id = record->app_id;
        entry->value.action_id = record->action_id;
        return HERMAS_SAGA_LOG_OK;
    }
    if (record->kind == HERMAS_SAGA_LOG_DELIVERY_SENT) {
        if (entry->delivery != DELIVERY_PREPARED ||
            !route_matches(&entry->value, record)) {
            return HERMAS_SAGA_LOG_INVALID_TRANSITION;
        }
        entry->delivery = DELIVERY_SENT;
        entry->value.delivery_was_sent = 1u;
        return HERMAS_SAGA_LOG_OK;
    }
    if (record->kind >= HERMAS_SAGA_LOG_STEP_SUCCEEDED &&
        record->kind <= HERMAS_SAGA_LOG_STEP_UNKNOWN) {
        bool route = route_matches(&entry->value, record);
        bool delivery =
            (record->kind == HERMAS_SAGA_LOG_STEP_SUCCEEDED &&
             entry->delivery == DELIVERY_SENT) ||
            (record->kind == HERMAS_SAGA_LOG_STEP_FAILED &&
             ((record->outcome == HERMAS_OUTCOME_APP_ERROR &&
               entry->delivery == DELIVERY_SENT) ||
              (record->outcome == HERMAS_OUTCOME_NOT_SENT &&
               entry->delivery == DELIVERY_PREPARED))) ||
            (record->kind == HERMAS_SAGA_LOG_STEP_UNKNOWN &&
             entry->delivery != DELIVERY_NONE);
        if (!route || !delivery) {
            return HERMAS_SAGA_LOG_INVALID_TRANSITION;
        }
        entry->delivery = DELIVERY_NONE;
        entry->value.has_open_delivery = 0u;
        entry->value.delivery_was_sent = 0u;
        if (record->kind == HERMAS_SAGA_LOG_STEP_SUCCEEDED) {
            --entry->value.next_ordinal;
            memset(&entry->value.request_id, 0,
                   sizeof(entry->value.request_id));
            entry->value.forward_node = 0u;
            entry->value.app_id = 0u;
            entry->value.action_id = 0u;
        } else {
            entry->terminal = true;
            entry->value.terminal_outcome = record->outcome;
        }
        return HERMAS_SAGA_LOG_OK;
    }
    if (record->kind == HERMAS_SAGA_LOG_FINISHED) {
        bool success =
            !entry->terminal &&
            entry->value.next_ordinal == 0u &&
            record->outcome == HERMAS_OUTCOME_SUCCESS;
        bool failed =
            entry->terminal &&
            entry->value.terminal_outcome == record->outcome;
        if (entry->delivery != DELIVERY_NONE ||
            (!success && !failed)) {
            return HERMAS_SAGA_LOG_INVALID_TRANSITION;
        }
        memset(entry, 0, sizeof(*entry));
        return HERMAS_SAGA_LOG_OK;
    }
    return HERMAS_SAGA_LOG_INVALID_TRANSITION;
}

hermas_saga_log_result hermas_saga_log_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_saga_log_summary *summary) {
    if ((bytes == NULL && byte_count != 0u) || summary == NULL ||
        byte_count % HERMAS_SAGA_LOG_RECORD_SIZE != 0u) {
        return HERMAS_SAGA_LOG_INVALID_ARGUMENT;
    }
    scan_active entries[HERMAS_SAGA_LOG_MAX_ACTIVE];
    memset(entries, 0, sizeof(entries));
    hermas_saga_log_summary result;
    memset(&result, 0, sizeof(result));
    result.next_sequence = 1u;
    for (size_t offset = 0u; offset < byte_count;
         offset += HERMAS_SAGA_LOG_RECORD_SIZE) {
        hermas_saga_log_record record;
        hermas_saga_log_result decoded = hermas_saga_log_decode(
            bytes + offset, HERMAS_SAGA_LOG_RECORD_SIZE, &record);
        if (decoded != HERMAS_SAGA_LOG_OK) {
            return decoded;
        }
        if (record.sequence != result.next_sequence) {
            return HERMAS_SAGA_LOG_INVALID_SEQUENCE;
        }
        hermas_saga_log_result changed =
            transition(entries, &record);
        if (changed != HERMAS_SAGA_LOG_OK) {
            return changed;
        }
        ++result.record_count;
        ++result.next_sequence;
    }
    for (size_t index = 0u; index < HERMAS_SAGA_LOG_MAX_ACTIVE;
         ++index) {
        if (entries[index].active) {
            result.active[result.active_count++] =
                entries[index].value;
        }
    }
    *summary = result;
    return HERMAS_SAGA_LOG_OK;
}

hermas_saga_log_result hermas_saga_log_writer_init(
    hermas_saga_log_writer *writer,
    hermas_saga_log_write write,
    void *context,
    uint64_t next_sequence) {
    if (writer == NULL || write == NULL || next_sequence == 0u) {
        return HERMAS_SAGA_LOG_INVALID_ARGUMENT;
    }
    *writer = (hermas_saga_log_writer){
        .write = write,
        .context = context,
        .next_sequence = next_sequence
    };
    return HERMAS_SAGA_LOG_OK;
}

hermas_saga_log_result hermas_saga_log_writer_append(
    hermas_saga_log_writer *writer,
    hermas_saga_log_record record) {
    if (writer == NULL || writer->write == NULL ||
        writer->next_sequence == 0u ||
        writer->next_sequence == UINT64_MAX) {
        return HERMAS_SAGA_LOG_INVALID_ARGUMENT;
    }
    record.sequence = writer->next_sequence;
    uint8_t encoded[HERMAS_SAGA_LOG_RECORD_SIZE];
    hermas_saga_log_result result =
        hermas_saga_log_encode(&record, encoded, sizeof(encoded));
    if (result != HERMAS_SAGA_LOG_OK) {
        return result;
    }
    result = writer->write(writer->context, encoded, sizeof(encoded));
    if (result == HERMAS_SAGA_LOG_OK) {
        ++writer->next_sequence;
    }
    return result;
}

const char *hermas_saga_log_result_name(
    hermas_saga_log_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "buffer-too-small",
        "invalid-record", "checksum-mismatch", "invalid-sequence",
        "invalid-transition", "capacity-exhausted", "write-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
