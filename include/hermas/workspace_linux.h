#ifndef HERMAS_WORKSPACE_LINUX_H
#define HERMAS_WORKSPACE_LINUX_H

#include <stdbool.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/un.h>

#define HERMAS_WORKSPACE_MANIFEST_VERSION 1u
#define HERMAS_WORKSPACE_MANIFEST_SIZE 64u

typedef enum hermas_workspace_result {
    HERMAS_WORKSPACE_OK = 0,
    HERMAS_WORKSPACE_INVALID_ARGUMENT,
    HERMAS_WORKSPACE_PATH_TOO_LONG,
    HERMAS_WORKSPACE_UNSAFE_DIRECTORY,
    HERMAS_WORKSPACE_NOT_INITIALIZED,
    HERMAS_WORKSPACE_INVALID_MANIFEST,
    HERMAS_WORKSPACE_INCOMPATIBLE,
    HERMAS_WORKSPACE_INVALID_IMAGE,
    HERMAS_WORKSPACE_IO_ERROR
} hermas_workspace_result;

typedef struct hermas_workspace_paths {
    char directory[PATH_MAX];
    char state_directory[PATH_MAX];
    char journal_path[PATH_MAX];
    char image_path[PATH_MAX];
    char manifest_path[PATH_MAX];
    char app_socket[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char control_socket[sizeof(((struct sockaddr_un *)0)->sun_path)];
} hermas_workspace_paths;

typedef struct hermas_workspace_binding {
    uint32_t workflow_id;
    uint64_t image_fingerprint;
    uint64_t image_size;
} hermas_workspace_binding;

/*
 * Resolves the fixed alpha workspace layout. When `create` is true, missing
 * workspace and state directories are created with mode 0700. Existing
 * directories are never replaced and must be owned by the effective user,
 * non-symlinks, and inaccessible to group or other users.
 */
hermas_workspace_result hermas_workspace_open(
    const char *directory,
    bool create,
    hermas_workspace_paths *paths);

/*
 * Pins a workspace to an exact validated graph image and workflow ID. The
 * managed image and versioned manifest are created durably and never silently
 * replaced. Rebinding is idempotent only for identical bytes and identity.
 */
hermas_workspace_result hermas_workspace_bind(
    const hermas_workspace_paths *paths,
    const char *image_path,
    uint32_t workflow_id,
    hermas_workspace_binding *binding);

/*
 * Loads and fully validates an existing binding, including every runtime
 * format version and the exact managed image fingerprint.
 */
hermas_workspace_result hermas_workspace_load(
    const hermas_workspace_paths *paths,
    hermas_workspace_binding *binding);

const char *hermas_workspace_result_name(
    hermas_workspace_result result);

#endif
