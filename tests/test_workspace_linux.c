#define _POSIX_C_SOURCE 200809L

#include "hermas/workspace_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "test_workspace: %s\n", message);
    return 1;
}

static int private_directory(const char *path) {
    struct stat status;
    return lstat(path, &status) == 0 &&
           S_ISDIR(status.st_mode) &&
           (status.st_mode & 0777u) == 0700u;
}

static int join_expected(
    char *destination,
    size_t capacity,
    const char *directory,
    const char *suffix) {
    size_t directory_length = strlen(directory);
    size_t suffix_length = strlen(suffix);
    if (directory_length >= capacity ||
        suffix_length >= capacity - directory_length) {
        return 0;
    }
    memcpy(destination, directory, directory_length);
    memcpy(
        destination + directory_length,
        suffix, suffix_length + 1u);
    return 1;
}

int main(void) {
    char base[] = "/tmp/hermas-workspace-XXXXXX";
    if (mkdtemp(base) == NULL) {
        return fail("cannot create test root");
    }
    char workspace[PATH_MAX];
    char expected[PATH_MAX];
    char link_path[PATH_MAX];
    char file_path[PATH_MAX];
    char long_path[PATH_MAX];
    (void)snprintf(
        workspace, sizeof(workspace), "%s/runtime", base);
    hermas_workspace_paths paths;
    if (hermas_workspace_open(workspace, true, &paths) !=
            HERMAS_WORKSPACE_OK ||
        !private_directory(workspace) ||
        !private_directory(paths.state_directory)) {
        return fail("private workspace was not created");
    }
    if (!join_expected(
            expected, sizeof(expected), workspace, "/state") ||
        strcmp(paths.state_directory, expected) != 0) {
        return fail("state path differs");
    }
    if (!join_expected(
            expected, sizeof(expected), workspace, "/apps.sock") ||
        strcmp(paths.app_socket, expected) != 0) {
        return fail("app socket path differs");
    }
    if (!join_expected(
            expected, sizeof(expected), workspace, "/control.sock") ||
        strcmp(paths.control_socket, expected) != 0) {
        return fail("control socket path differs");
    }
    if (!join_expected(
            expected, sizeof(expected),
            workspace, "/state/journal.hj") ||
        strcmp(paths.journal_path, expected) != 0 ||
        hermas_workspace_open(workspace, false, &paths) !=
            HERMAS_WORKSPACE_OK) {
        return fail("existing workspace did not resolve");
    }
    if (chmod(workspace, 0750) != 0 ||
        hermas_workspace_open(workspace, false, &paths) !=
            HERMAS_WORKSPACE_UNSAFE_DIRECTORY ||
        chmod(workspace, 0700) != 0) {
        return fail("unsafe permissions were accepted");
    }
    if (!join_expected(
            expected, sizeof(expected), workspace, "/state") ||
        chmod(expected, 0750) != 0 ||
        hermas_workspace_open(workspace, false, &paths) !=
            HERMAS_WORKSPACE_UNSAFE_DIRECTORY ||
        chmod(expected, 0700) != 0) {
        return fail("unsafe state permissions were accepted");
    }
    if (rmdir(expected) != 0 ||
        symlink(workspace, expected) != 0 ||
        hermas_workspace_open(workspace, false, &paths) !=
            HERMAS_WORKSPACE_UNSAFE_DIRECTORY ||
        unlink(expected) != 0 ||
        mkdir(expected, 0700) != 0) {
        return fail("state-directory symlink was accepted");
    }
    (void)snprintf(
        link_path, sizeof(link_path), "%s/workspace-link", base);
    if (symlink(workspace, link_path) != 0 ||
        hermas_workspace_open(link_path, false, &paths) !=
            HERMAS_WORKSPACE_UNSAFE_DIRECTORY) {
        return fail("workspace symlink was accepted");
    }
    (void)snprintf(
        file_path, sizeof(file_path), "%s/workspace-file", base);
    FILE *file = fopen(file_path, "wb");
    if (file == NULL || fclose(file) != 0 ||
        hermas_workspace_open(file_path, true, &paths) !=
            HERMAS_WORKSPACE_UNSAFE_DIRECTORY) {
        return fail("workspace file was accepted");
    }
    int prefix = snprintf(
        long_path, sizeof(long_path), "%s/", base);
    if (prefix <= 0 || (size_t)prefix + 96u >= sizeof(long_path)) {
        return fail("cannot construct long path");
    }
    memset(long_path + prefix, 'a', 96u);
    long_path[prefix + 96] = '\0';
    if (mkdir(long_path, 0700) != 0 ||
        hermas_workspace_open(long_path, true, &paths) !=
            HERMAS_WORKSPACE_PATH_TOO_LONG) {
        return fail("overlong socket path was accepted");
    }
    if (hermas_workspace_open(NULL, false, &paths) !=
            HERMAS_WORKSPACE_INVALID_ARGUMENT ||
        hermas_workspace_open(workspace, false, NULL) !=
            HERMAS_WORKSPACE_INVALID_ARGUMENT) {
        return fail("invalid arguments were accepted");
    }
    (void)rmdir(long_path);
    (void)unlink(file_path);
    (void)unlink(link_path);
    (void)join_expected(
        expected, sizeof(expected), workspace, "/state");
    (void)rmdir(expected);
    (void)rmdir(workspace);
    (void)rmdir(base);
    puts("workspace path tests passed");
    return 0;
}
