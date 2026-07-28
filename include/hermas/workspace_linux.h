#ifndef HERMAS_WORKSPACE_LINUX_H
#define HERMAS_WORKSPACE_LINUX_H

#include <stdbool.h>
#include <linux/limits.h>
#include <sys/un.h>

typedef enum hermas_workspace_result {
    HERMAS_WORKSPACE_OK = 0,
    HERMAS_WORKSPACE_INVALID_ARGUMENT,
    HERMAS_WORKSPACE_PATH_TOO_LONG,
    HERMAS_WORKSPACE_UNSAFE_DIRECTORY
} hermas_workspace_result;

typedef struct hermas_workspace_paths {
    char state_directory[PATH_MAX];
    char journal_path[PATH_MAX];
    char app_socket[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char control_socket[sizeof(((struct sockaddr_un *)0)->sun_path)];
} hermas_workspace_paths;

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

const char *hermas_workspace_result_name(
    hermas_workspace_result result);

#endif
