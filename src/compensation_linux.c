#define _POSIX_C_SOURCE 200809L

#include "hermas/compensation_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static hermas_compensation_result write_synchronized(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    hermas_compensation_file *file = context;
    size_t written = 0u;
    while (written < record_size) {
        ssize_t result = write(
            file->file_descriptor, record + written,
            record_size - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return HERMAS_COMPENSATION_WRITE_ERROR;
        }
        written += (size_t)result;
    }
    return fdatasync(file->file_descriptor) == 0
               ? HERMAS_COMPENSATION_OK
               : HERMAS_COMPENSATION_WRITE_ERROR;
}

static hermas_compensation_result map_file(
    int descriptor,
    const uint8_t **bytes,
    size_t *byte_count) {
    struct stat status;
    if (fstat(descriptor, &status) != 0 || status.st_size < 0 ||
        (uint64_t)status.st_size > SIZE_MAX) {
        return HERMAS_COMPENSATION_INVALID_RECORD;
    }
    *byte_count = (size_t)status.st_size;
    if (*byte_count == 0u) {
        *bytes = NULL;
        return HERMAS_COMPENSATION_OK;
    }
    void *mapping = mmap(
        NULL, *byte_count, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (mapping == MAP_FAILED) {
        return HERMAS_COMPENSATION_WRITE_ERROR;
    }
    *bytes = mapping;
    return HERMAS_COMPENSATION_OK;
}

hermas_compensation_result hermas_compensation_file_open(
    hermas_compensation_file *file,
    const char *path,
    hermas_compensation_summary *summary) {
    if (file == NULL || path == NULL || path[0] == '\0' ||
        summary == NULL) {
        return HERMAS_COMPENSATION_INVALID_ARGUMENT;
    }
    memset(file, 0, sizeof(*file));
    file->file_descriptor = -1;
    int descriptor = open(
        path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW,
        0600);
    if (descriptor < 0 ||
        flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return HERMAS_COMPENSATION_WRITE_ERROR;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() ||
        (status.st_mode & 077u) != 0u) {
        (void)flock(descriptor, LOCK_UN);
        close(descriptor);
        return HERMAS_COMPENSATION_INVALID_RECORD;
    }
    const uint8_t *bytes = NULL;
    size_t byte_count = 0u;
    hermas_compensation_result mapped =
        map_file(descriptor, &bytes, &byte_count);
    if (mapped != HERMAS_COMPENSATION_OK) {
        close(descriptor);
        return mapped;
    }
    hermas_compensation_result scanned =
        hermas_compensation_scan(
            bytes, byte_count, NULL, NULL, summary);
    if (bytes != NULL) {
        munmap((void *)bytes, byte_count);
    }
    if (scanned != HERMAS_COMPENSATION_OK) {
        close(descriptor);
        return scanned;
    }
    file->file_descriptor = descriptor;
    hermas_compensation_result initialized =
        hermas_compensation_writer_init(
            &file->writer, write_synchronized, file,
            summary->next_sequence);
    if (initialized != HERMAS_COMPENSATION_OK) {
        close(descriptor);
        file->file_descriptor = -1;
    }
    return initialized;
}

hermas_compensation_result hermas_compensation_file_find(
    const hermas_compensation_file *file,
    hermas_compensation_key key,
    hermas_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found) {
    if (file == NULL || file->file_descriptor < 0) {
        return HERMAS_COMPENSATION_INVALID_ARGUMENT;
    }
    const uint8_t *bytes = NULL;
    size_t byte_count = 0u;
    hermas_compensation_result mapped =
        map_file(file->file_descriptor, &bytes, &byte_count);
    if (mapped != HERMAS_COMPENSATION_OK) {
        return mapped;
    }
    hermas_compensation_result result = hermas_compensation_find(
        bytes, byte_count, key, record, token, token_capacity, found);
    if (bytes != NULL) {
        munmap((void *)bytes, byte_count);
    }
    return result;
}

hermas_compensation_result hermas_compensation_file_lookup(
    void *context,
    hermas_compensation_key key,
    hermas_compensation_record *record,
    uint8_t *token,
    size_t token_capacity,
    int *found) {
    return hermas_compensation_file_find(
        context, key, record, token, token_capacity, found);
}

void hermas_compensation_file_close(
    hermas_compensation_file *file) {
    if (file == NULL) {
        return;
    }
    if (file->file_descriptor >= 0) {
        (void)flock(file->file_descriptor, LOCK_UN);
        close(file->file_descriptor);
    }
    memset(file, 0, sizeof(*file));
    file->file_descriptor = -1;
}
