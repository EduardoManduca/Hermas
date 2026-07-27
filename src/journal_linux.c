#define _POSIX_C_SOURCE 200809L

#include "hermas2/journal_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static hermas2_journal_result write_synchronized(
    void *context,
    const uint8_t *record,
    size_t record_size) {
    hermas2_journal_file *file = context;
    size_t written = 0u;
    while (written < record_size) {
        ssize_t result = write(
            file->file_descriptor, record + written,
            record_size - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return HERMAS2_JOURNAL_WRITE_ERROR;
        }
        written += (size_t)result;
    }
    return fdatasync(file->file_descriptor) == 0
               ? HERMAS2_JOURNAL_OK
               : HERMAS2_JOURNAL_WRITE_ERROR;
}

static hermas2_journal_result scan_descriptor(
    int descriptor,
    hermas2_journal_visitor visitor,
    void *visitor_context,
    hermas2_journal_summary *summary) {
    struct stat status;
    if (fstat(descriptor, &status) != 0 || status.st_size < 0 ||
        (uint64_t)status.st_size > SIZE_MAX ||
        (size_t)status.st_size % HERMAS2_JOURNAL_RECORD_SIZE != 0u) {
        return HERMAS2_JOURNAL_INVALID_SIZE;
    }
    size_t byte_count = (size_t)status.st_size;
    if (byte_count == 0u) {
        return hermas2_journal_scan(
            NULL, 0u, visitor, visitor_context, summary);
    }
    void *mapping = mmap(
        NULL, byte_count, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (mapping == MAP_FAILED) {
        return HERMAS2_JOURNAL_WRITE_ERROR;
    }
    hermas2_journal_result scanned = hermas2_journal_scan(
        mapping, byte_count, visitor, visitor_context, summary);
    munmap(mapping, byte_count);
    return scanned;
}

hermas2_journal_result hermas2_journal_file_open(
    hermas2_journal_file *file,
    const char *path,
    hermas2_journal_summary *summary) {
    if (file == NULL || path == NULL || path[0] == '\0' ||
        summary == NULL) {
        return HERMAS2_JOURNAL_INVALID_ARGUMENT;
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
        return HERMAS2_JOURNAL_WRITE_ERROR;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() ||
        (status.st_mode & 077u) != 0u) {
        (void)flock(descriptor, LOCK_UN);
        close(descriptor);
        return HERMAS2_JOURNAL_INVALID_SIZE;
    }
    hermas2_journal_result scanned = scan_descriptor(
        descriptor, NULL, NULL, summary);
    if (scanned != HERMAS2_JOURNAL_OK) {
        close(descriptor);
        return scanned;
    }
    file->file_descriptor = descriptor;
    hermas2_journal_result initialized = hermas2_journal_writer_init(
        &file->writer, write_synchronized, file,
        summary->next_sequence);
    if (initialized != HERMAS2_JOURNAL_OK) {
        close(descriptor);
        file->file_descriptor = -1;
        return initialized;
    }
    return HERMAS2_JOURNAL_OK;
}

hermas2_journal_result hermas2_journal_file_close_interrupted(
    hermas2_journal_file *file,
    const hermas2_journal_summary *summary,
    size_t *closed_count) {
    if (file == NULL || file->file_descriptor < 0 ||
        summary == NULL || closed_count == NULL) {
        return HERMAS2_JOURNAL_INVALID_ARGUMENT;
    }
    *closed_count = 0u;
    for (size_t index = 0u;
         index < summary->interrupted_count; ++index) {
        const hermas2_journal_interrupted *interrupted =
            &summary->interrupted[index];
        if (interrupted->has_open_delivery != 0u) {
            hermas2_journal_record unknown = {
                .kind = HERMAS2_JOURNAL_ACTION_UNKNOWN,
                .outcome = HERMAS2_OUTCOME_UNKNOWN,
                .execution_id = interrupted->execution_id,
                .workflow_id = interrupted->workflow_id,
                .request_id = interrupted->request_id,
                .node_id = interrupted->node_id,
                .app_id = interrupted->app_id,
                .action_id = interrupted->action_id,
                .image_fingerprint =
                    interrupted->image_fingerprint
            };
            hermas2_journal_result appended =
                hermas2_journal_writer_append(
                    &file->writer, unknown);
            if (appended != HERMAS2_JOURNAL_OK) {
                return appended;
            }
        }
        hermas2_journal_record finished = {
            .kind = HERMAS2_JOURNAL_EXECUTION_FINISHED,
            .outcome = HERMAS2_OUTCOME_UNKNOWN,
            .execution_id = interrupted->execution_id,
            .workflow_id = interrupted->workflow_id,
            .image_fingerprint =
                interrupted->image_fingerprint
        };
        hermas2_journal_result appended =
            hermas2_journal_writer_append(
                &file->writer, finished);
        if (appended != HERMAS2_JOURNAL_OK) {
            return appended;
        }
        ++*closed_count;
    }
    return HERMAS2_JOURNAL_OK;
}

hermas2_journal_result hermas2_journal_file_inspect(
    const char *path,
    hermas2_journal_visitor visitor,
    void *visitor_context,
    hermas2_journal_summary *summary) {
    if (path == NULL || path[0] == '\0' || summary == NULL) {
        return HERMAS2_JOURNAL_INVALID_ARGUMENT;
    }
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return HERMAS2_JOURNAL_WRITE_ERROR;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() ||
        (status.st_mode & 077u) != 0u) {
        close(descriptor);
        return HERMAS2_JOURNAL_INVALID_SIZE;
    }
    hermas2_journal_result result = scan_descriptor(
        descriptor, visitor, visitor_context, summary);
    close(descriptor);
    return result;
}

void hermas2_journal_file_close(
    hermas2_journal_file *file) {
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
