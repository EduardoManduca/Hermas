#define _GNU_SOURCE

#include "hermas/registration_linux.h"

#include "hermas/protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void close_client(hermas_registration_client *client) {
    if (client->file_descriptor >= 0) {
        close(client->file_descriptor);
    }
    memset(client, 0, sizeof(*client));
    client->file_descriptor = -1;
}

static void release_client(hermas_registration_client *client) {
    memset(client, 0, sizeof(*client));
    client->file_descriptor = -1;
}

static void reject_client(
    hermas_registration_server *server,
    hermas_registration_client *client) {
    hermas_frame error = {
        .kind = HERMAS_FRAME_PROTOCOL_ERROR,
        .outcome = HERMAS_OUTCOME_PROTOCOL_ERROR
    };
    size_t packet_size = 0u;
    if (hermas_protocol_encode(
            &error, server->packet, sizeof(server->packet),
            &packet_size) == HERMAS_PROTOCOL_OK) {
        (void)send(
            client->file_descriptor, server->packet, packet_size,
            MSG_DONTWAIT | MSG_NOSIGNAL);
    }
    close_client(client);
}

static bool action_is_pending(
    const hermas_registration_server *server,
    size_t action_index,
    const hermas_registration_client *except) {
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        const hermas_registration_client *candidate =
            &server->clients[index];
        if (candidate != except && candidate->active &&
            candidate->validated &&
            candidate->action_index == action_index) {
            return true;
        }
    }
    return false;
}

static void receive_registration(
    hermas_registration_server *server,
    hermas_registration_client *client,
    size_t *progress_count) {
    struct iovec vector = {
        .iov_base = server->packet,
        .iov_len = sizeof(server->packet)
    };
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1u;
    ssize_t received = recvmsg(
        client->file_descriptor, &message, MSG_DONTWAIT);
    if (received < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK ||
         errno == EINTR)) {
        return;
    }
    if (received <= 0 || (message.msg_flags & MSG_TRUNC) != 0) {
        reject_client(server, client);
        ++*progress_count;
        return;
    }
    hermas_frame registration;
    if (hermas_protocol_decode(
            server->packet, (size_t)received, &registration) !=
            HERMAS_PROTOCOL_OK ||
        registration.kind != HERMAS_FRAME_REGISTER_APP) {
        reject_client(server, client);
        ++*progress_count;
        return;
    }
    size_t action_index = server->registry->action_count;
    for (size_t index = 0u;
         index < server->registry->action_count; ++index) {
        if (server->registry->actions[index].app_id ==
                registration.app_id &&
            memcmp(
                server->registry->actions[index].contract_fingerprint,
                registration.payload, 32u) == 0) {
            action_index = index;
            break;
        }
    }
    if (action_index == server->registry->action_count ||
        server->registry->actions[action_index].file_descriptor >= 0 ||
        action_is_pending(server, action_index, client)) {
        reject_client(server, client);
        ++*progress_count;
        return;
    }
    client->action_index = action_index;
    client->registered_action_id = registration.action_id;
    client->validated = true;
    ++*progress_count;
}

static hermas_registration_server_result flush_acknowledgements(
    hermas_registration_server *server,
    size_t *progress_count) {
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        hermas_registration_client *client =
            &server->clients[index];
        if (!client->active || !client->validated) {
            continue;
        }
        if (client->action_index >= server->registry->action_count ||
            server->registry->actions[client->action_index]
                    .file_descriptor >= 0) {
            return HERMAS_REGISTRATION_SERVER_STATE_ERROR;
        }
        hermas_frame acknowledgement = {
            .kind = HERMAS_FRAME_REGISTER_OK,
            .app_id =
                server->registry->actions[client->action_index].app_id,
            .action_id = client->registered_action_id,
            .outcome = HERMAS_OUTCOME_NONE
        };
        size_t packet_size = 0u;
        if (hermas_protocol_encode(
                &acknowledgement, server->packet,
                sizeof(server->packet), &packet_size) !=
                HERMAS_PROTOCOL_OK) {
            return HERMAS_REGISTRATION_SERVER_STATE_ERROR;
        }
        ssize_t sent = send(
            client->file_descriptor, server->packet, packet_size,
            MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK ||
             errno == EINTR)) {
            continue;
        }
        if (sent != (ssize_t)packet_size) {
            close_client(client);
            ++*progress_count;
            continue;
        }
        server->registry->actions[client->action_index].file_descriptor =
            client->file_descriptor;
        server->registry->actions[client->action_index]
            .registered_action_id = client->registered_action_id;
        release_client(client);
        ++*progress_count;
    }
    return HERMAS_REGISTRATION_SERVER_OK;
}

hermas_registration_server_result hermas_registration_server_init(
    hermas_registration_server *server,
    hermas_daemon_registry *registry) {
    if (server == NULL || registry == NULL ||
        registry->action_count > HERMAS_DAEMON_MAX_ACTIONS) {
        return HERMAS_REGISTRATION_SERVER_INVALID_ARGUMENT;
    }
    memset(server, 0, sizeof(*server));
    server->registry = registry;
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        server->clients[index].file_descriptor = -1;
    }
    return HERMAS_REGISTRATION_SERVER_OK;
}

hermas_registration_server_result hermas_registration_server_attach(
    hermas_registration_server *server,
    int client_descriptor) {
    if (server == NULL || server->registry == NULL ||
        client_descriptor < 0) {
        return HERMAS_REGISTRATION_SERVER_INVALID_ARGUMENT;
    }
    int flags = fcntl(client_descriptor, F_GETFL);
    int descriptor_flags = fcntl(client_descriptor, F_GETFD);
    if (flags < 0 || descriptor_flags < 0 ||
        fcntl(
            client_descriptor, F_SETFL,
            flags | O_NONBLOCK) != 0 ||
        fcntl(
            client_descriptor, F_SETFD,
            descriptor_flags | FD_CLOEXEC) != 0) {
        return HERMAS_REGISTRATION_SERVER_STATE_ERROR;
    }
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        hermas_registration_client *client =
            &server->clients[index];
        if (!client->active) {
            *client = (hermas_registration_client){
                .file_descriptor = client_descriptor,
                .active = true
            };
            return HERMAS_REGISTRATION_SERVER_OK;
        }
    }
    return HERMAS_REGISTRATION_SERVER_CAPACITY_EXHAUSTED;
}

hermas_registration_server_result hermas_registration_server_accept(
    hermas_registration_server *server,
    int listener) {
    if (server == NULL || server->registry == NULL || listener < 0) {
        return HERMAS_REGISTRATION_SERVER_INVALID_ARGUMENT;
    }
    int client = accept4(
        listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK ||
                       errno == EINTR
                   ? HERMAS_REGISTRATION_SERVER_OK
                   : HERMAS_REGISTRATION_SERVER_ACCEPT_ERROR;
    }
    hermas_registration_server_result attached =
        hermas_registration_server_attach(server, client);
    if (attached != HERMAS_REGISTRATION_SERVER_OK) {
        close(client);
    }
    return attached;
}

hermas_registration_server_result hermas_registration_server_step(
    hermas_registration_server *server,
    int timeout_milliseconds,
    size_t *progress_count) {
    if (server == NULL || server->registry == NULL ||
        progress_count == NULL || timeout_milliseconds < -1) {
        return HERMAS_REGISTRATION_SERVER_INVALID_ARGUMENT;
    }
    *progress_count = 0u;
    hermas_registration_server_result flushed =
        flush_acknowledgements(server, progress_count);
    if (flushed != HERMAS_REGISTRATION_SERVER_OK) {
        return flushed;
    }
    struct pollfd items[HERMAS_REGISTRATION_MAX_PENDING];
    hermas_registration_client
        *owners[HERMAS_REGISTRATION_MAX_PENDING];
    nfds_t count = 0u;
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        hermas_registration_client *client =
            &server->clients[index];
        if (!client->active) {
            continue;
        }
        items[count] = (struct pollfd){
            .fd = client->file_descriptor,
            .events = client->validated ? POLLOUT : POLLIN
        };
        owners[count] = client;
        ++count;
    }
    int polled;
    do {
        polled = poll(items, count, timeout_milliseconds);
    } while (polled < 0 && errno == EINTR);
    if (polled < 0) {
        return HERMAS_REGISTRATION_SERVER_POLL_ERROR;
    }
    for (nfds_t index = 0u; index < count; ++index) {
        hermas_registration_client *client = owners[index];
        short events = items[index].revents;
        if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            close_client(client);
            ++*progress_count;
        } else if (!client->validated && (events & POLLIN) != 0) {
            receive_registration(server, client, progress_count);
        }
    }
    return flush_acknowledgements(server, progress_count);
}

size_t hermas_registration_server_pending(
    const hermas_registration_server *server) {
    if (server == NULL) {
        return 0u;
    }
    size_t count = 0u;
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        count += server->clients[index].active ? 1u : 0u;
    }
    return count;
}

void hermas_registration_server_close(
    hermas_registration_server *server) {
    if (server == NULL) {
        return;
    }
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        close_client(&server->clients[index]);
    }
    memset(server, 0, sizeof(*server));
    for (size_t index = 0u;
         index < HERMAS_REGISTRATION_MAX_PENDING; ++index) {
        server->clients[index].file_descriptor = -1;
    }
}

const char *hermas_registration_server_result_name(
    hermas_registration_server_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "capacity-exhausted",
        "accept-error", "poll-error", "state-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
