#define _POSIX_C_SOURCE 200809L

#include "hermas/workspace_linux.h"

#include <fcntl.h>
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

static int private_file(const char *path) {
    struct stat status;
    return lstat(path, &status) == 0 &&
           S_ISREG(status.st_mode) &&
           (status.st_mode & 0777u) == 0600u;
}

static int replace_byte(
    const char *path,
    off_t offset,
    unsigned char value) {
    int descriptor = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return 0;
    }
    ssize_t written = pwrite(descriptor, &value, 1u, offset);
    int synced = fsync(descriptor);
    int closed = close(descriptor);
    return written == 1 && synced == 0 && closed == 0;
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

int main(int argc, char **argv) {
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
    if (hermas_workspace_open(workspace, false, &paths) !=
            HERMAS_WORKSPACE_OK) {
        return fail("workspace did not reopen before binding");
    }
    if (hermas_workspace_load(&paths, NULL) !=
            HERMAS_WORKSPACE_INVALID_ARGUMENT) {
        return fail("invalid binding arguments were accepted");
    }
    if (argc == 2) {
        hermas_workspace_binding binding;
        if (hermas_workspace_load(&paths, &binding) !=
                HERMAS_WORKSPACE_NOT_INITIALIZED ||
            hermas_workspace_bind(&paths, argv[1], 7u, &binding) !=
                HERMAS_WORKSPACE_OK ||
            binding.workflow_id != 7u ||
            binding.image_fingerprint == 0u ||
            binding.image_size == 0u ||
            !private_file(paths.image_path) ||
            !private_file(paths.manifest_path)) {
            return fail("workspace binding was not created");
        }
        hermas_workspace_binding loaded;
        uint8_t action_fingerprint[32];
        if (hermas_workspace_load(&paths, &loaded) !=
                HERMAS_WORKSPACE_OK ||
            loaded.workflow_id != binding.workflow_id ||
            loaded.image_fingerprint != binding.image_fingerprint ||
            loaded.image_size != binding.image_size ||
            hermas_workspace_bind(&paths, argv[1], 7u, &loaded) !=
                HERMAS_WORKSPACE_OK ||
            hermas_workspace_bind(&paths, argv[1], 8u, &loaded) !=
                HERMAS_WORKSPACE_INCOMPATIBLE ||
            hermas_workspace_action_fingerprint(
                &paths, 1u, 1u, action_fingerprint) !=
                HERMAS_WORKSPACE_OK ||
            hermas_workspace_action_fingerprint(
                &paths, 99u, 99u, action_fingerprint) !=
                HERMAS_WORKSPACE_ACTION_NOT_FOUND ||
            hermas_workspace_action_fingerprint(
                &paths, 0u, 1u, action_fingerprint) !=
                HERMAS_WORKSPACE_INVALID_ARGUMENT) {
            return fail("binding identity was not enforced");
        }
        if (!replace_byte(paths.manifest_path, 4, 2u) ||
            hermas_workspace_load(&paths, &loaded) !=
                HERMAS_WORKSPACE_INCOMPATIBLE ||
            !replace_byte(paths.manifest_path, 4, 1u) ||
            !replace_byte(paths.manifest_path, 63, 1u) ||
            hermas_workspace_load(&paths, &loaded) !=
                HERMAS_WORKSPACE_INVALID_MANIFEST ||
            !replace_byte(paths.manifest_path, 63, 0u) ||
            hermas_workspace_load(&paths, &loaded) !=
                HERMAS_WORKSPACE_OK) {
            return fail("manifest compatibility was not enforced");
        }
        char image_backup[PATH_MAX];
        if (!join_expected(
                image_backup, sizeof(image_backup),
                workspace, "/workflow.backup") ||
            rename(paths.image_path, image_backup) != 0 ||
            hermas_workspace_load(&paths, &loaded) !=
                HERMAS_WORKSPACE_NOT_INITIALIZED ||
            hermas_workspace_bind(&paths, argv[1], 7u, &loaded) !=
                HERMAS_WORKSPACE_INCOMPATIBLE ||
            access(paths.image_path, F_OK) == 0 ||
            rename(image_backup, paths.image_path) != 0 ||
            hermas_workspace_load(&paths, &loaded) !=
                HERMAS_WORKSPACE_OK) {
            return fail("missing managed image was silently recreated");
        }
        char legacy_workspace[PATH_MAX];
        if (!join_expected(
                legacy_workspace, sizeof(legacy_workspace),
                base, "/legacy-runtime")) {
            return fail("cannot construct legacy workspace path");
        }
        hermas_workspace_paths legacy_paths;
        if (hermas_workspace_open(
                legacy_workspace, true, &legacy_paths) !=
                HERMAS_WORKSPACE_OK) {
            return fail("cannot create legacy workspace");
        }
        FILE *legacy_journal =
            fopen(legacy_paths.journal_path, "wb");
        if (legacy_journal == NULL ||
            fclose(legacy_journal) != 0 ||
            hermas_workspace_bind(
                &legacy_paths, argv[1], 7u, &loaded) !=
                HERMAS_WORKSPACE_INCOMPATIBLE ||
            access(legacy_paths.manifest_path, F_OK) == 0 ||
            access(legacy_paths.image_path, F_OK) == 0) {
            return fail("unbound durable state was silently adopted");
        }
        (void)unlink(legacy_paths.journal_path);
        (void)rmdir(legacy_paths.state_directory);
        (void)rmdir(legacy_workspace);
    }
    (void)rmdir(long_path);
    (void)unlink(file_path);
    (void)unlink(link_path);
    (void)unlink(paths.manifest_path);
    (void)unlink(paths.image_path);
    (void)join_expected(
        expected, sizeof(expected), workspace, "/state");
    (void)rmdir(expected);
    (void)rmdir(workspace);
    (void)rmdir(base);
    puts("workspace path tests passed");
    return 0;
}
