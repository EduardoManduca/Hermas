#define _POSIX_C_SOURCE 200809L

#include "hermas2/saga_log_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static hermas2_saga_log_result write_synchronized(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    hermas2_saga_log_file *file = context;
    size_t written = 0u;
    while (written < record_size) {
        ssize_t result = write(
            file->file_descriptor, record + written,
            record_size - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return HERMAS2_SAGA_LOG_WRITE_ERROR;
        }
        written += (size_t)result;
    }
    return fdatasync(file->file_descriptor) == 0
               ? HERMAS2_SAGA_LOG_OK
               : HERMAS2_SAGA_LOG_WRITE_ERROR;
}

static hermas2_saga_log_result scan_descriptor(
    int descriptor,
    hermas2_saga_log_summary *summary) {
    struct stat status;
    if (fstat(descriptor, &status) != 0 || status.st_size < 0 ||
        (uint64_t)status.st_size > SIZE_MAX) {
        return HERMAS2_SAGA_LOG_INVALID_RECORD;
    }
    size_t size = (size_t)status.st_size;
    if (size == 0u) {
        return hermas2_saga_log_scan(NULL, 0u, summary);
    }
    void *mapping =
        mmap(NULL, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (mapping == MAP_FAILED) {
        return HERMAS2_SAGA_LOG_WRITE_ERROR;
    }
    hermas2_saga_log_result result =
        hermas2_saga_log_scan(mapping, size, summary);
    (void)munmap(mapping, size);
    return result;
}

hermas2_saga_log_result hermas2_saga_log_file_open(
    hermas2_saga_log_file *file,
    const char *path,
    hermas2_saga_log_summary *summary) {
    if (file == NULL || path == NULL || path[0] == '\0' ||
        summary == NULL) {
        return HERMAS2_SAGA_LOG_INVALID_ARGUMENT;
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
        return HERMAS2_SAGA_LOG_WRITE_ERROR;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid()) {
        (void)flock(descriptor, LOCK_UN);
        close(descriptor);
        return HERMAS2_SAGA_LOG_INVALID_RECORD;
    }
    hermas2_saga_log_result scanned =
        scan_descriptor(descriptor, summary);
    if (scanned != HERMAS2_SAGA_LOG_OK) {
        (void)flock(descriptor, LOCK_UN);
        close(descriptor);
        return scanned;
    }
    file->file_descriptor = descriptor;
    hermas2_saga_log_result initialized =
        hermas2_saga_log_writer_init(
            &file->writer, write_synchronized, file,
            summary->next_sequence);
    if (initialized != HERMAS2_SAGA_LOG_OK) {
        (void)flock(descriptor, LOCK_UN);
        close(descriptor);
        file->file_descriptor = -1;
    }
    return initialized;
}

hermas2_saga_log_result hermas2_saga_log_file_scan(
    const hermas2_saga_log_file *file,
    hermas2_saga_log_summary *summary) {
    if (file == NULL || file->file_descriptor < 0 ||
        summary == NULL) {
        return HERMAS2_SAGA_LOG_INVALID_ARGUMENT;
    }
    return scan_descriptor(file->file_descriptor, summary);
}

void hermas2_saga_log_file_close(
    hermas2_saga_log_file *file) {
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
