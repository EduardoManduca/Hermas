#define _POSIX_C_SOURCE 200809L

#include "hermas2/saga_linux.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef struct mapped_file {
    const uint8_t *bytes;
    size_t size;
} mapped_file;

static bool map_descriptor(int descriptor, mapped_file *mapping) {
    struct stat status;
    if (descriptor < 0 || mapping == NULL ||
        fstat(descriptor, &status) != 0 || status.st_size < 0 ||
        (uint64_t)status.st_size > SIZE_MAX) {
        return false;
    }
    mapping->size = (size_t)status.st_size;
    mapping->bytes = NULL;
    if (mapping->size == 0u) {
        return true;
    }
    void *bytes = mmap(
        NULL, mapping->size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (bytes == MAP_FAILED) {
        mapping->size = 0u;
        return false;
    }
    mapping->bytes = bytes;
    return true;
}

static void unmap_file(mapped_file *mapping) {
    if (mapping->bytes != NULL) {
        (void)munmap((void *)mapping->bytes, mapping->size);
    }
    mapping->bytes = NULL;
    mapping->size = 0u;
}

hermas2_saga_result hermas2_saga_recover_files(
    hermas2_saga_execution *execution,
    const uint8_t *image,
    size_t image_size,
    const hermas2_journal_file *journal,
    const hermas2_compensation_file *compensation,
    const hermas2_saga_log_file *saga_log,
    uint64_t execution_id,
    uint32_t workflow_id) {
    if (execution == NULL || image == NULL || journal == NULL ||
        compensation == NULL || saga_log == NULL ||
        journal->file_descriptor < 0 ||
        compensation->file_descriptor < 0 ||
        saga_log->file_descriptor < 0 ||
        execution_id == 0u || workflow_id == 0u) {
        return HERMAS2_SAGA_INVALID_ARGUMENT;
    }
    mapped_file journal_mapping;
    mapped_file token_mapping;
    mapped_file saga_mapping;
    if (!map_descriptor(
            journal->file_descriptor, &journal_mapping)) {
        return HERMAS2_SAGA_INCONSISTENT_HISTORY;
    }
    if (!map_descriptor(
            compensation->file_descriptor, &token_mapping)) {
        unmap_file(&journal_mapping);
        return HERMAS2_SAGA_INVALID_TOKEN;
    }
    if (!map_descriptor(
            saga_log->file_descriptor, &saga_mapping)) {
        unmap_file(&token_mapping);
        unmap_file(&journal_mapping);
        return HERMAS2_SAGA_INCONSISTENT_HISTORY;
    }
    hermas2_saga_result result = hermas2_saga_recover(
        execution, image, image_size,
        journal_mapping.bytes, journal_mapping.size,
        token_mapping.bytes, token_mapping.size,
        execution_id, workflow_id);
    if (result == HERMAS2_SAGA_OK) {
        result = hermas2_saga_reconcile(
            execution, saga_mapping.bytes, saga_mapping.size);
    }
    if (result == HERMAS2_SAGA_OK ||
        result == HERMAS2_SAGA_UNSAFE_HISTORY) {
        execution->tokens = NULL;
        execution->token_bytes = 0u;
        execution->token_lookup =
            hermas2_compensation_file_lookup;
        execution->token_lookup_context =
            (void *)compensation;
    }
    unmap_file(&saga_mapping);
    unmap_file(&token_mapping);
    unmap_file(&journal_mapping);
    return result;
}
