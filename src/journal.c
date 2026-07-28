#include "hermas/journal.h"

#include <stdbool.h>
#include <string.h>

typedef enum scan_delivery_state {
    SCAN_DELIVERY_NONE = 0,
    SCAN_DELIVERY_PREPARED,
    SCAN_DELIVERY_SENT
} scan_delivery_state;

typedef struct scan_execution {
    hermas_journal_interrupted value;
    scan_delivery_state delivery;
    uint16_t last_outcome;
    bool has_action_outcome;
    bool active;
} scan_execution;

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

static bool is_action_record(hermas_journal_kind kind) {
    return kind >= HERMAS_JOURNAL_DELIVERY_PREPARED &&
           kind <= HERMAS_JOURNAL_ACTION_UNKNOWN;
}

static hermas_journal_result validate_record(
    const hermas_journal_record *record) {
    if (record->kind < HERMAS_JOURNAL_EXECUTION_STARTED ||
        record->kind > HERMAS_JOURNAL_EXECUTION_FINISHED) {
        return HERMAS_JOURNAL_INVALID_KIND;
    }
    bool outcome_valid = false;
    switch (record->kind) {
        case HERMAS_JOURNAL_EXECUTION_STARTED:
        case HERMAS_JOURNAL_DELIVERY_PREPARED:
        case HERMAS_JOURNAL_DELIVERY_SENT:
            outcome_valid = record->outcome == HERMAS_OUTCOME_NONE;
            break;
        case HERMAS_JOURNAL_ACTION_SUCCEEDED:
            outcome_valid = record->outcome == HERMAS_OUTCOME_SUCCESS;
            break;
        case HERMAS_JOURNAL_ACTION_FAILED:
            outcome_valid =
                record->outcome == HERMAS_OUTCOME_APP_ERROR ||
                record->outcome == HERMAS_OUTCOME_NOT_SENT;
            break;
        case HERMAS_JOURNAL_ACTION_UNKNOWN:
            outcome_valid = record->outcome == HERMAS_OUTCOME_UNKNOWN;
            break;
        case HERMAS_JOURNAL_EXECUTION_FINISHED:
            outcome_valid =
                record->outcome == HERMAS_OUTCOME_SUCCESS ||
                record->outcome == HERMAS_OUTCOME_APP_ERROR ||
                record->outcome == HERMAS_OUTCOME_NOT_SENT ||
                record->outcome == HERMAS_OUTCOME_UNKNOWN;
            break;
    }
    if (!outcome_valid) {
        return HERMAS_JOURNAL_INVALID_OUTCOME;
    }
    if (record->sequence == 0u || record->execution_id == 0u ||
        record->workflow_id == 0u ||
        record->image_fingerprint == 0u) {
        return HERMAS_JOURNAL_INVALID_FIELD;
    }
    bool action = is_action_record(record->kind);
    if (action &&
        (record->request_id == 0u || record->node_id == 0u ||
         record->app_id == 0u || record->action_id == 0u)) {
        return HERMAS_JOURNAL_INVALID_FIELD;
    }
    if (!action &&
        (record->request_id != 0u || record->node_id != 0u ||
         record->app_id != 0u || record->action_id != 0u)) {
        return HERMAS_JOURNAL_INVALID_FIELD;
    }
    return HERMAS_JOURNAL_OK;
}

hermas_journal_result hermas_journal_encode(
    const hermas_journal_record *record,
    uint8_t *destination,
    size_t destination_size) {
    if (record == NULL || destination == NULL) {
        return HERMAS_JOURNAL_INVALID_ARGUMENT;
    }
    if (destination_size < HERMAS_JOURNAL_RECORD_SIZE) {
        return HERMAS_JOURNAL_BUFFER_TOO_SMALL;
    }
    hermas_journal_result valid = validate_record(record);
    if (valid != HERMAS_JOURNAL_OK) {
        return valid;
    }
    memset(destination, 0, HERMAS_JOURNAL_RECORD_SIZE);
    memcpy(destination, "HJR1", 4u);
    write_u16(destination, 4u, HERMAS_JOURNAL_VERSION);
    write_u16(destination, 6u, HERMAS_JOURNAL_RECORD_SIZE);
    write_u16(destination, 8u, (uint16_t)record->kind);
    write_u16(destination, 10u, record->outcome);
    write_u64(destination, 16u, record->sequence);
    write_u64(destination, 24u, record->execution_id);
    write_u32(destination, 32u, record->workflow_id);
    write_u64(destination, 36u, record->request_id);
    write_u16(destination, 44u, record->node_id);
    write_u16(destination, 46u, record->app_id);
    write_u16(destination, 48u, record->action_id);
    write_u64(destination, 52u, record->image_fingerprint);
    write_u32(destination, 60u, checksum(destination, 60u));
    return HERMAS_JOURNAL_OK;
}

hermas_journal_result hermas_journal_decode(
    const uint8_t *source,
    size_t source_size,
    hermas_journal_record *record) {
    if (source == NULL || record == NULL) {
        return HERMAS_JOURNAL_INVALID_ARGUMENT;
    }
    if (source_size < HERMAS_JOURNAL_RECORD_SIZE) {
        return HERMAS_JOURNAL_BUFFER_TOO_SMALL;
    }
    if (memcmp(source, "HJR1", 4u) != 0) {
        return HERMAS_JOURNAL_INVALID_MAGIC;
    }
    if (read_u16(source, 4u) != HERMAS_JOURNAL_VERSION) {
        return HERMAS_JOURNAL_INVALID_VERSION;
    }
    if (read_u16(source, 6u) != HERMAS_JOURNAL_RECORD_SIZE) {
        return HERMAS_JOURNAL_INVALID_SIZE;
    }
    if (read_u32(source, 12u) != 0u ||
        read_u16(source, 50u) != 0u) {
        return HERMAS_JOURNAL_NONZERO_RESERVED;
    }
    if (read_u32(source, 60u) != checksum(source, 60u)) {
        return HERMAS_JOURNAL_CHECKSUM_MISMATCH;
    }
    hermas_journal_record decoded = {
        .kind = (hermas_journal_kind)read_u16(source, 8u),
        .outcome = read_u16(source, 10u),
        .sequence = read_u64(source, 16u),
        .execution_id = read_u64(source, 24u),
        .workflow_id = read_u32(source, 32u),
        .request_id = read_u64(source, 36u),
        .node_id = read_u16(source, 44u),
        .app_id = read_u16(source, 46u),
        .action_id = read_u16(source, 48u),
        .image_fingerprint = read_u64(source, 52u)
    };
    hermas_journal_result valid = validate_record(&decoded);
    if (valid != HERMAS_JOURNAL_OK) {
        return valid;
    }
    *record = decoded;
    return HERMAS_JOURNAL_OK;
}

static scan_execution *find_scan_execution(
    scan_execution *executions,
    uint64_t execution_id) {
    for (size_t index = 0u;
         index < HERMAS_JOURNAL_MAX_INTERRUPTED; ++index) {
        if (executions[index].active &&
            executions[index].value.execution_id == execution_id) {
            return &executions[index];
        }
    }
    return NULL;
}

static bool route_matches(
    const hermas_journal_interrupted *execution,
    const hermas_journal_record *record) {
    return execution->request_id == record->request_id &&
           execution->node_id == record->node_id &&
           execution->app_id == record->app_id &&
           execution->action_id == record->action_id;
}

static hermas_journal_result scan_transition(
    scan_execution *executions,
    const hermas_journal_record *record) {
    scan_execution *execution =
        find_scan_execution(executions, record->execution_id);
    if (record->kind == HERMAS_JOURNAL_EXECUTION_STARTED) {
        if (execution != NULL) {
            return HERMAS_JOURNAL_INVALID_TRANSITION;
        }
        for (size_t index = 0u;
             index < HERMAS_JOURNAL_MAX_INTERRUPTED; ++index) {
            if (!executions[index].active) {
                executions[index].active = true;
                executions[index].value.execution_id =
                    record->execution_id;
                executions[index].value.workflow_id =
                    record->workflow_id;
                executions[index].value.image_fingerprint =
                    record->image_fingerprint;
                return HERMAS_JOURNAL_OK;
            }
        }
        return HERMAS_JOURNAL_CAPACITY_EXHAUSTED;
    }
    if (execution == NULL ||
        execution->value.workflow_id != record->workflow_id ||
        execution->value.image_fingerprint !=
            record->image_fingerprint) {
        return HERMAS_JOURNAL_INVALID_TRANSITION;
    }
    if (record->kind == HERMAS_JOURNAL_DELIVERY_PREPARED) {
        if (execution->delivery != SCAN_DELIVERY_NONE) {
            return HERMAS_JOURNAL_INVALID_TRANSITION;
        }
        execution->delivery = SCAN_DELIVERY_PREPARED;
        execution->value.has_open_delivery = 1u;
        execution->value.delivery_was_sent = 0u;
        execution->value.request_id = record->request_id;
        execution->value.node_id = record->node_id;
        execution->value.app_id = record->app_id;
        execution->value.action_id = record->action_id;
        return HERMAS_JOURNAL_OK;
    }
    if (record->kind == HERMAS_JOURNAL_DELIVERY_SENT) {
        if (execution->delivery != SCAN_DELIVERY_PREPARED ||
            !route_matches(&execution->value, record)) {
            return HERMAS_JOURNAL_INVALID_TRANSITION;
        }
        execution->delivery = SCAN_DELIVERY_SENT;
        execution->value.delivery_was_sent = 1u;
        return HERMAS_JOURNAL_OK;
    }
    if (record->kind >= HERMAS_JOURNAL_ACTION_SUCCEEDED &&
        record->kind <= HERMAS_JOURNAL_ACTION_UNKNOWN) {
        if (execution->delivery == SCAN_DELIVERY_NONE ||
            !route_matches(&execution->value, record) ||
            (record->kind == HERMAS_JOURNAL_ACTION_SUCCEEDED &&
             execution->delivery != SCAN_DELIVERY_SENT) ||
            (record->kind == HERMAS_JOURNAL_ACTION_FAILED &&
             record->outcome == HERMAS_OUTCOME_APP_ERROR &&
             execution->delivery != SCAN_DELIVERY_SENT) ||
            (record->kind == HERMAS_JOURNAL_ACTION_FAILED &&
             record->outcome == HERMAS_OUTCOME_NOT_SENT &&
             execution->delivery != SCAN_DELIVERY_PREPARED)) {
            return HERMAS_JOURNAL_INVALID_TRANSITION;
        }
        execution->delivery = SCAN_DELIVERY_NONE;
        execution->value.has_open_delivery = 0u;
        execution->value.delivery_was_sent = 0u;
        execution->last_outcome = record->outcome;
        execution->has_action_outcome = true;
        return HERMAS_JOURNAL_OK;
    }
    if (record->kind == HERMAS_JOURNAL_EXECUTION_FINISHED) {
        if (execution->delivery != SCAN_DELIVERY_NONE ||
            (execution->has_action_outcome &&
             execution->last_outcome != record->outcome) ||
            (!execution->has_action_outcome &&
             record->outcome != HERMAS_OUTCOME_UNKNOWN)) {
            return HERMAS_JOURNAL_INVALID_TRANSITION;
        }
        memset(execution, 0, sizeof(*execution));
        return HERMAS_JOURNAL_OK;
    }
    return HERMAS_JOURNAL_INVALID_TRANSITION;
}

hermas_journal_result hermas_journal_scan(
    const uint8_t *bytes,
    size_t byte_count,
    hermas_journal_visitor visitor,
    void *visitor_context,
    hermas_journal_summary *summary) {
    if ((bytes == NULL && byte_count != 0u) || summary == NULL ||
        byte_count % HERMAS_JOURNAL_RECORD_SIZE != 0u) {
        return HERMAS_JOURNAL_INVALID_ARGUMENT;
    }
    scan_execution executions[HERMAS_JOURNAL_MAX_INTERRUPTED];
    memset(executions, 0, sizeof(executions));
    hermas_journal_summary result;
    memset(&result, 0, sizeof(result));
    result.next_sequence = 1u;
    result.next_execution_id = 1u;
    uint64_t largest_started_id = 0u;
    for (size_t offset = 0u; offset < byte_count;
         offset += HERMAS_JOURNAL_RECORD_SIZE) {
        hermas_journal_record record;
        hermas_journal_result decoded = hermas_journal_decode(
            bytes + offset, HERMAS_JOURNAL_RECORD_SIZE, &record);
        if (decoded != HERMAS_JOURNAL_OK) {
            return decoded;
        }
        if (record.sequence != result.next_sequence) {
            return HERMAS_JOURNAL_INVALID_SEQUENCE;
        }
        if (record.kind == HERMAS_JOURNAL_EXECUTION_STARTED) {
            if (record.execution_id <= largest_started_id) {
                return HERMAS_JOURNAL_INVALID_TRANSITION;
            }
            largest_started_id = record.execution_id;
        }
        hermas_journal_result transitioned =
            scan_transition(executions, &record);
        if (transitioned != HERMAS_JOURNAL_OK) {
            return transitioned;
        }
        if (visitor != NULL) {
            hermas_journal_result visited =
                visitor(visitor_context, &record);
            if (visited != HERMAS_JOURNAL_OK) {
                return visited;
            }
        }
        ++result.record_count;
        ++result.next_sequence;
        if (record.execution_id >= result.next_execution_id) {
            if (record.execution_id == UINT64_MAX) {
                return HERMAS_JOURNAL_INVALID_FIELD;
            }
            result.next_execution_id = record.execution_id + 1u;
        }
    }
    for (size_t index = 0u;
         index < HERMAS_JOURNAL_MAX_INTERRUPTED; ++index) {
        if (executions[index].active) {
            result.interrupted[result.interrupted_count++] =
                executions[index].value;
        }
    }
    *summary = result;
    return HERMAS_JOURNAL_OK;
}

hermas_journal_result hermas_journal_writer_init(
    hermas_journal_writer *writer,
    hermas_journal_write write,
    void *context,
    uint64_t next_sequence) {
    if (writer == NULL || write == NULL || next_sequence == 0u) {
        return HERMAS_JOURNAL_INVALID_ARGUMENT;
    }
    *writer = (hermas_journal_writer){
        .write = write,
        .context = context,
        .next_sequence = next_sequence
    };
    return HERMAS_JOURNAL_OK;
}

hermas_journal_result hermas_journal_writer_append(
    hermas_journal_writer *writer,
    hermas_journal_record record) {
    if (writer == NULL || writer->write == NULL ||
        writer->next_sequence == 0u ||
        writer->next_sequence == UINT64_MAX) {
        return HERMAS_JOURNAL_INVALID_ARGUMENT;
    }
    record.sequence = writer->next_sequence;
    uint8_t encoded[HERMAS_JOURNAL_RECORD_SIZE];
    hermas_journal_result result =
        hermas_journal_encode(&record, encoded, sizeof(encoded));
    if (result != HERMAS_JOURNAL_OK) {
        return result;
    }
    result = writer->write(writer->context, encoded, sizeof(encoded));
    if (result != HERMAS_JOURNAL_OK) {
        return result;
    }
    ++writer->next_sequence;
    return HERMAS_JOURNAL_OK;
}

uint64_t hermas_journal_image_fingerprint(
    const uint8_t *image,
    size_t image_size) {
    if (image == NULL || image_size == 0u) {
        return 0u;
    }
    uint64_t value = UINT64_C(14695981039346656037);
    for (size_t index = 0u; index < image_size; ++index) {
        value ^= image[index];
        value *= UINT64_C(1099511628211);
    }
    return value == 0u ? UINT64_MAX : value;
}

const char *hermas_journal_result_name(
    hermas_journal_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "buffer-too-small", "invalid-magic",
        "invalid-version", "invalid-size", "invalid-kind",
        "invalid-outcome", "invalid-field", "nonzero-reserved",
        "checksum-mismatch", "invalid-sequence", "invalid-transition",
        "capacity-exhausted", "write-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
