#define _POSIX_C_SOURCE 200809L

#include "hermas/workspace_linux.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    if (!append_path(
            paths->state_directory,
            sizeof(paths->state_directory),
            directory, "state") ||
        !append_path(
            paths->journal_path,
            sizeof(paths->journal_path),
            paths->state_directory, "journal.hj") ||
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

const char *hermas_workspace_result_name(
    hermas_workspace_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "path-too-long",
        "unsafe-directory"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
