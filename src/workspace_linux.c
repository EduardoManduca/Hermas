#define _POSIX_C_SOURCE 200809L

#include "hermas/compensation.h"
#include "hermas/image.h"
#include "hermas/journal.h"
#include "hermas/protocol.h"
#include "hermas/result.h"
#include "hermas/saga_log.h"
#include "hermas/workspace_linux.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put_u16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    for (size_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] =
            (uint8_t)(value >> (index * 8u));
    }
}

static void put_u64(uint8_t *bytes, size_t offset, uint64_t value) {
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[offset + index] =
            (uint8_t)(value >> (index * 8u));
    }
}

static uint16_t get_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] |
           (uint16_t)((uint16_t)bytes[offset + 1u] << 8u);
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

static bool secure_directory(const char *path) {
    struct stat status;
    return lstat(path, &status) == 0 &&
           S_ISDIR(status.st_mode) &&
           status.st_uid == geteuid() &&
           (status.st_mode & 077u) == 0u &&
           access(path, R_OK | W_OK | X_OK) == 0;
}

static bool append_path(
    char *destination,
    size_t capacity,
    const char *directory,
    const char *name) {
    int length = snprintf(
        destination, capacity, "%s/%s", directory, name);
    return length > 0 && (size_t)length < capacity;
}

hermas_workspace_result hermas_workspace_open(
    const char *directory,
    bool create,
    hermas_workspace_paths *paths) {
    if (directory == NULL || directory[0] == '\0' || paths == NULL) {
        return HERMAS_WORKSPACE_INVALID_ARGUMENT;
    }
    memset(paths, 0, sizeof(*paths));
    if (create && mkdir(directory, 0700) != 0 && errno != EEXIST) {
        return HERMAS_WORKSPACE_UNSAFE_DIRECTORY;
    }
    if (!secure_directory(directory)) {
        return HERMAS_WORKSPACE_UNSAFE_DIRECTORY;
    }
    int copied = snprintf(
        paths->directory, sizeof(paths->directory), "%s", directory);
    if (copied <= 0 || (size_t)copied >= sizeof(paths->directory) ||
        !append_path(
            paths->state_directory,
            sizeof(paths->state_directory),
            directory, "state") ||
        !append_path(
            paths->journal_path,
            sizeof(paths->journal_path),
            paths->state_directory, "journal.hj") ||
        !append_path(
            paths->image_path,
            sizeof(paths->image_path),
            directory, "workflow.hgi") ||
        !append_path(
            paths->manifest_path,
            sizeof(paths->manifest_path),
            directory, "manifest.hwm") ||
        !append_path(
            paths->app_socket,
            sizeof(paths->app_socket),
            directory, "apps.sock") ||
        !append_path(
            paths->control_socket,
            sizeof(paths->control_socket),
            directory, "control.sock")) {
        memset(paths, 0, sizeof(*paths));
        return HERMAS_WORKSPACE_PATH_TOO_LONG;
    }
    if (create &&
        mkdir(paths->state_directory, 0700) != 0 &&
        errno != EEXIST) {
        memset(paths, 0, sizeof(*paths));
        return HERMAS_WORKSPACE_UNSAFE_DIRECTORY;
    }
    if (!secure_directory(paths->state_directory)) {
        memset(paths, 0, sizeof(*paths));
        return HERMAS_WORKSPACE_UNSAFE_DIRECTORY;
    }
    return HERMAS_WORKSPACE_OK;
}

static bool all_zero(
    const uint8_t *bytes,
    size_t begin,
    size_t end) {
    for (size_t index = begin; index < end; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

static hermas_workspace_result read_owned_file(
    const char *path,
    uint8_t *bytes,
    size_t capacity,
    size_t *size) {
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return errno == ENOENT
                   ? HERMAS_WORKSPACE_NOT_INITIALIZED
                   : HERMAS_WORKSPACE_IO_ERROR;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() ||
        (status.st_mode & 077u) != 0u ||
        status.st_size < 0 ||
        (uint64_t)status.st_size > capacity) {
        close(descriptor);
        return HERMAS_WORKSPACE_UNSAFE_DIRECTORY;
    }
    size_t expected = (size_t)status.st_size;
    size_t offset = 0u;
    while (offset < expected) {
        ssize_t count =
            read(descriptor, bytes + offset, expected - offset);
        if (count <= 0) {
            close(descriptor);
            return HERMAS_WORKSPACE_IO_ERROR;
        }
        offset += (size_t)count;
    }
    uint8_t extra = 0u;
    ssize_t trailing = read(descriptor, &extra, 1u);
    if (close(descriptor) != 0 || trailing != 0) {
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    *size = expected;
    return HERMAS_WORKSPACE_OK;
}

static hermas_workspace_result read_source_image(
    const char *path,
    uint8_t *bytes,
    size_t capacity,
    size_t *size) {
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return HERMAS_WORKSPACE_INVALID_IMAGE;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        (uint64_t)status.st_size > capacity) {
        close(descriptor);
        return HERMAS_WORKSPACE_INVALID_IMAGE;
    }
    size_t expected = (size_t)status.st_size;
    size_t offset = 0u;
    while (offset < expected) {
        ssize_t count =
            read(descriptor, bytes + offset, expected - offset);
        if (count <= 0) {
            close(descriptor);
            return HERMAS_WORKSPACE_IO_ERROR;
        }
        offset += (size_t)count;
    }
    if (close(descriptor) != 0) {
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    *size = expected;
    return HERMAS_WORKSPACE_OK;
}

static hermas_workspace_result write_new_file(
    const char *path,
    const uint8_t *bytes,
    size_t size) {
    char temporary[PATH_MAX];
    int length = snprintf(
        temporary, sizeof(temporary), "%s.tmp.%ld",
        path, (long)getpid());
    if (length <= 0 || (size_t)length >= sizeof(temporary)) {
        return HERMAS_WORKSPACE_PATH_TOO_LONG;
    }
    int descriptor = open(
        temporary,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        0600);
    if (descriptor < 0) {
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    size_t offset = 0u;
    while (offset < size) {
        ssize_t count =
            write(descriptor, bytes + offset, size - offset);
        if (count <= 0) {
            close(descriptor);
            unlink(temporary);
            return HERMAS_WORKSPACE_IO_ERROR;
        }
        offset += (size_t)count;
    }
    if (fsync(descriptor) != 0 || close(descriptor) != 0) {
        unlink(temporary);
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    if (link(temporary, path) != 0) {
        int saved = errno;
        unlink(temporary);
        return saved == EEXIST
                   ? HERMAS_WORKSPACE_INCOMPATIBLE
                   : HERMAS_WORKSPACE_IO_ERROR;
    }
    return unlink(temporary) == 0
               ? HERMAS_WORKSPACE_OK
               : HERMAS_WORKSPACE_IO_ERROR;
}

static hermas_workspace_result sync_workspace(
    const hermas_workspace_paths *paths) {
    int descriptor = open(
        paths->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    int synced = fsync(descriptor);
    int closed = close(descriptor);
    return synced == 0 && closed == 0
               ? HERMAS_WORKSPACE_OK
               : HERMAS_WORKSPACE_IO_ERROR;
}

static hermas_workspace_result require_empty_state(
    const hermas_workspace_paths *paths) {
    DIR *directory = opendir(paths->state_directory);
    if (directory == NULL) {
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            closedir(directory);
            return HERMAS_WORKSPACE_INCOMPATIBLE;
        }
    }
    int saved = errno;
    int closed = closedir(directory);
    return saved == 0 && closed == 0
               ? HERMAS_WORKSPACE_OK
               : HERMAS_WORKSPACE_IO_ERROR;
}

static void encode_manifest(
    uint8_t bytes[HERMAS_WORKSPACE_MANIFEST_SIZE],
    const hermas_workspace_binding *binding) {
    memset(bytes, 0, HERMAS_WORKSPACE_MANIFEST_SIZE);
    memcpy(bytes, "HWM1", 4u);
    put_u16(bytes, 4u, HERMAS_WORKSPACE_MANIFEST_VERSION);
    put_u16(bytes, 6u, HERMAS_WORKSPACE_MANIFEST_SIZE);
    put_u32(bytes, 8u, binding->workflow_id);
    put_u16(bytes, 12u, HERMAS_GRAPH_IMAGE_VERSION);
    put_u16(bytes, 14u, HERMAS_PROTOCOL_VERSION);
    put_u16(bytes, 16u, HERMAS_JOURNAL_VERSION);
    put_u16(bytes, 18u, HERMAS_RESULT_VERSION);
    put_u16(bytes, 20u, HERMAS_COMPENSATION_VERSION);
    put_u16(bytes, 22u, HERMAS_SAGA_LOG_VERSION);
    put_u64(bytes, 24u, binding->image_fingerprint);
    put_u64(bytes, 32u, binding->image_size);
}

static hermas_workspace_result decode_manifest(
    const uint8_t *bytes,
    size_t size,
    hermas_workspace_binding *binding) {
    if (size != HERMAS_WORKSPACE_MANIFEST_SIZE ||
        memcmp(bytes, "HWM1", 4u) != 0 ||
        get_u16(bytes, 6u) != HERMAS_WORKSPACE_MANIFEST_SIZE ||
        get_u32(bytes, 8u) == 0u ||
        get_u64(bytes, 24u) == 0u ||
        get_u64(bytes, 32u) == 0u ||
        get_u64(bytes, 32u) > HERMAS_IMAGE_MAX_SIZE ||
        !all_zero(bytes, 40u, HERMAS_WORKSPACE_MANIFEST_SIZE)) {
        return HERMAS_WORKSPACE_INVALID_MANIFEST;
    }
    if (get_u16(bytes, 4u) != HERMAS_WORKSPACE_MANIFEST_VERSION ||
        get_u16(bytes, 12u) != HERMAS_GRAPH_IMAGE_VERSION ||
        get_u16(bytes, 14u) != HERMAS_PROTOCOL_VERSION ||
        get_u16(bytes, 16u) != HERMAS_JOURNAL_VERSION ||
        get_u16(bytes, 18u) != HERMAS_RESULT_VERSION ||
        get_u16(bytes, 20u) != HERMAS_COMPENSATION_VERSION ||
        get_u16(bytes, 22u) != HERMAS_SAGA_LOG_VERSION) {
        return HERMAS_WORKSPACE_INCOMPATIBLE;
    }
    binding->workflow_id = get_u32(bytes, 8u);
    binding->image_fingerprint = get_u64(bytes, 24u);
    binding->image_size = get_u64(bytes, 32u);
    return HERMAS_WORKSPACE_OK;
}

static hermas_workspace_result validate_image_bytes(
    const uint8_t *bytes,
    size_t size,
    uint64_t *fingerprint) {
    if (hermas_image_validate(bytes, size, NULL) != HERMAS_IMAGE_OK) {
        return HERMAS_WORKSPACE_INVALID_IMAGE;
    }
    *fingerprint = hermas_journal_image_fingerprint(bytes, size);
    return *fingerprint == 0u
               ? HERMAS_WORKSPACE_INVALID_IMAGE
               : HERMAS_WORKSPACE_OK;
}

hermas_workspace_result hermas_workspace_load(
    const hermas_workspace_paths *paths,
    hermas_workspace_binding *binding) {
    if (paths == NULL || binding == NULL ||
        paths->manifest_path[0] == '\0' ||
        paths->image_path[0] == '\0') {
        return HERMAS_WORKSPACE_INVALID_ARGUMENT;
    }
    memset(binding, 0, sizeof(*binding));
    uint8_t manifest[HERMAS_WORKSPACE_MANIFEST_SIZE];
    size_t manifest_size = 0u;
    hermas_workspace_result result = read_owned_file(
        paths->manifest_path, manifest, sizeof(manifest),
        &manifest_size);
    if (result != HERMAS_WORKSPACE_OK) {
        return result;
    }
    result = decode_manifest(manifest, manifest_size, binding);
    if (result != HERMAS_WORKSPACE_OK) {
        memset(binding, 0, sizeof(*binding));
        return result;
    }
    uint8_t *image = malloc(HERMAS_IMAGE_MAX_SIZE);
    if (image == NULL) {
        memset(binding, 0, sizeof(*binding));
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    size_t image_size = 0u;
    result = read_owned_file(
        paths->image_path, image, HERMAS_IMAGE_MAX_SIZE, &image_size);
    uint64_t fingerprint = 0u;
    if (result == HERMAS_WORKSPACE_OK) {
        result = validate_image_bytes(image, image_size, &fingerprint);
    }
    free(image);
    if (result != HERMAS_WORKSPACE_OK ||
        image_size != binding->image_size ||
        fingerprint != binding->image_fingerprint) {
        memset(binding, 0, sizeof(*binding));
        return result == HERMAS_WORKSPACE_OK
                   ? HERMAS_WORKSPACE_INCOMPATIBLE
                   : result;
    }
    return HERMAS_WORKSPACE_OK;
}

static hermas_workspace_result load_bound_image(
    const hermas_workspace_paths *paths,
    uint8_t **image,
    size_t *image_size) {
    if (paths == NULL || image == NULL || image_size == NULL) {
        return HERMAS_WORKSPACE_INVALID_ARGUMENT;
    }
    hermas_workspace_binding binding;
    hermas_workspace_result result =
        hermas_workspace_load(paths, &binding);
    if (result != HERMAS_WORKSPACE_OK) {
        return result;
    }
    uint8_t *loaded = malloc(HERMAS_IMAGE_MAX_SIZE);
    if (loaded == NULL) {
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    size_t loaded_size = 0u;
    result = read_owned_file(
        paths->image_path, loaded, HERMAS_IMAGE_MAX_SIZE, &loaded_size);
    uint64_t image_fingerprint = 0u;
    if (result == HERMAS_WORKSPACE_OK) {
        result = validate_image_bytes(
            loaded, loaded_size, &image_fingerprint);
    }
    if (result == HERMAS_WORKSPACE_OK &&
        (loaded_size != binding.image_size ||
         image_fingerprint != binding.image_fingerprint)) {
        result = HERMAS_WORKSPACE_INCOMPATIBLE;
    }
    if (result != HERMAS_WORKSPACE_OK) {
        free(loaded);
        return result;
    }
    *image = loaded;
    *image_size = loaded_size;
    return HERMAS_WORKSPACE_OK;
}

hermas_workspace_result hermas_workspace_action_fingerprint(
    const hermas_workspace_paths *paths,
    uint16_t app_id,
    uint16_t action_id,
    uint8_t fingerprint[32]) {
    if (app_id == 0u || action_id == 0u || fingerprint == NULL) {
        return HERMAS_WORKSPACE_INVALID_ARGUMENT;
    }
    uint8_t *image = NULL;
    size_t image_size = 0u;
    hermas_workspace_result result =
        load_bound_image(paths, &image, &image_size);
    if (result != HERMAS_WORKSPACE_OK) {
        return result;
    }
    hermas_image_result found = hermas_image_action_fingerprint(
        image, image_size, app_id, action_id, fingerprint);
    free(image);
    if (found == HERMAS_IMAGE_ACTION_NOT_FOUND) {
        return HERMAS_WORKSPACE_ACTION_NOT_FOUND;
    }
    return found == HERMAS_IMAGE_OK
               ? HERMAS_WORKSPACE_OK
               : HERMAS_WORKSPACE_INVALID_IMAGE;
}

hermas_workspace_result hermas_workspace_find_action_contract(
    const hermas_workspace_paths *paths,
    const uint8_t fingerprint[32],
    hermas_image_action_contract *contract) {
    if (fingerprint == NULL || contract == NULL) {
        return HERMAS_WORKSPACE_INVALID_ARGUMENT;
    }
    uint8_t *image = NULL;
    size_t image_size = 0u;
    hermas_workspace_result result =
        load_bound_image(paths, &image, &image_size);
    if (result != HERMAS_WORKSPACE_OK) {
        return result;
    }
    hermas_image_result found = hermas_image_find_action_contract(
        image, image_size, fingerprint, contract);
    free(image);
    if (found == HERMAS_IMAGE_ACTION_NOT_FOUND) {
        return HERMAS_WORKSPACE_ACTION_NOT_FOUND;
    }
    return found == HERMAS_IMAGE_OK
               ? HERMAS_WORKSPACE_OK
               : HERMAS_WORKSPACE_INVALID_IMAGE;
}

hermas_workspace_result hermas_workspace_bind(
    const hermas_workspace_paths *paths,
    const char *image_path,
    uint32_t workflow_id,
    hermas_workspace_binding *binding) {
    if (paths == NULL || image_path == NULL || image_path[0] == '\0' ||
        workflow_id == 0u || binding == NULL) {
        return HERMAS_WORKSPACE_INVALID_ARGUMENT;
    }
    uint8_t *source = malloc(HERMAS_IMAGE_MAX_SIZE);
    if (source == NULL) {
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    size_t source_size = 0u;
    hermas_workspace_result result = read_source_image(
        image_path, source, HERMAS_IMAGE_MAX_SIZE, &source_size);
    if (result != HERMAS_WORKSPACE_OK) {
        free(source);
        return result;
    }
    hermas_workspace_binding wanted = {
        .workflow_id = workflow_id,
        .image_size = source_size
    };
    result = validate_image_bytes(
        source, source_size, &wanted.image_fingerprint);
    if (result != HERMAS_WORKSPACE_OK) {
        free(source);
        return result;
    }
    hermas_workspace_binding existing;
    result = hermas_workspace_load(paths, &existing);
    if (result == HERMAS_WORKSPACE_OK) {
        uint8_t *managed = malloc(HERMAS_IMAGE_MAX_SIZE);
        size_t managed_size = 0u;
        hermas_workspace_result read_result =
            managed == NULL
                ? HERMAS_WORKSPACE_IO_ERROR
                : read_owned_file(
                      paths->image_path, managed,
                      HERMAS_IMAGE_MAX_SIZE, &managed_size);
        bool identical =
            read_result == HERMAS_WORKSPACE_OK &&
            existing.workflow_id == wanted.workflow_id &&
            managed_size == source_size &&
            memcmp(managed, source, source_size) == 0;
        free(managed);
        free(source);
        if (!identical) {
            return HERMAS_WORKSPACE_INCOMPATIBLE;
        }
        *binding = existing;
        return HERMAS_WORKSPACE_OK;
    }
    if (result != HERMAS_WORKSPACE_NOT_INITIALIZED) {
        free(source);
        return result;
    }
    struct stat manifest_status;
    if (lstat(paths->manifest_path, &manifest_status) == 0) {
        free(source);
        return HERMAS_WORKSPACE_INCOMPATIBLE;
    }
    if (errno != ENOENT) {
        free(source);
        return HERMAS_WORKSPACE_IO_ERROR;
    }
    result = require_empty_state(paths);
    if (result != HERMAS_WORKSPACE_OK) {
        free(source);
        return result;
    }
    uint8_t *orphan = malloc(HERMAS_IMAGE_MAX_SIZE);
    size_t orphan_size = 0u;
    hermas_workspace_result orphan_result =
        orphan == NULL
            ? HERMAS_WORKSPACE_IO_ERROR
            : read_owned_file(
                  paths->image_path, orphan, HERMAS_IMAGE_MAX_SIZE,
                  &orphan_size);
    if (orphan_result == HERMAS_WORKSPACE_OK) {
        if (orphan_size != source_size ||
            memcmp(orphan, source, source_size) != 0) {
            free(orphan);
            free(source);
            return HERMAS_WORKSPACE_INCOMPATIBLE;
        }
        result = HERMAS_WORKSPACE_OK;
    } else if (orphan_result == HERMAS_WORKSPACE_NOT_INITIALIZED) {
        result = write_new_file(
            paths->image_path, source, source_size);
    } else {
        result = orphan_result;
    }
    free(orphan);
    free(source);
    if (result != HERMAS_WORKSPACE_OK) {
        return result;
    }
    uint8_t manifest[HERMAS_WORKSPACE_MANIFEST_SIZE];
    encode_manifest(manifest, &wanted);
    result = write_new_file(
        paths->manifest_path, manifest, sizeof(manifest));
    if (result == HERMAS_WORKSPACE_OK) {
        result = sync_workspace(paths);
    }
    if (result != HERMAS_WORKSPACE_OK) {
        return result;
    }
    *binding = wanted;
    return HERMAS_WORKSPACE_OK;
}

const char *hermas_workspace_result_name(
    hermas_workspace_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "path-too-long",
        "unsafe-directory", "not-initialized", "invalid-manifest",
        "incompatible", "invalid-image", "io-error",
        "action-not-found"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
