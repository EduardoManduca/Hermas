#define _GNU_SOURCE

#include "hermas2/control_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void close_descriptor(hermas2_control_client *client) {
    if (client->file_descriptor >= 0) {
        close(client->file_descriptor);
        client->file_descriptor = -1;
    }
}

static void clear_client(hermas2_control_client *client) {
    close_descriptor(client);
    memset(client, 0, sizeof(*client));
    client->file_descriptor = -1;
}

static hermas2_control_server_result advance_loop(
    hermas2_control_server *server,
    size_t *progress_count) {
    size_t progressed = 0u;
    if (hermas2_daemon_loop_poll(
            server->loop, 0, &progressed) != HERMAS2_LOOP_OK) {
        return HERMAS2_CONTROL_SERVER_LOOP_ERROR;
    }
    *progress_count += progressed;
    return HERMAS2_CONTROL_SERVER_OK;
}

static void reject_client(
    hermas2_control_server *server,
    hermas2_control_client *client) {
    hermas2_frame error = {
        .kind = HERMAS2_FRAME_PROTOCOL_ERROR,
        .outcome = HERMAS2_OUTCOME_PROTOCOL_ERROR
    };
    size_t packet_size = 0u;
    if (client->file_descriptor >= 0 &&
        hermas2_protocol_encode(
            &error, server->packet, sizeof(server->packet),
            &packet_size) == HERMAS2_PROTOCOL_OK) {
        (void)send(
            client->file_descriptor, server->packet, packet_size,
            MSG_DONTWAIT | MSG_NOSIGNAL);
    }
    clear_client(client);
}

static hermas2_control_server_result receive_execute(
    hermas2_control_server *server,
    hermas2_control_client *client,
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
        return HERMAS2_CONTROL_SERVER_OK;
    }
    if (received <= 0 || (message.msg_flags & MSG_TRUNC) != 0) {
        clear_client(client);
        ++*progress_count;
        return HERMAS2_CONTROL_SERVER_OK;
    }
    uint64_t execution_id = 0u;
    if (hermas2_control_submit(
            server->loop, server->packet, (size_t)received,
            &execution_id) != HERMAS2_CONTROL_OK) {
        reject_client(server, client);
        ++*progress_count;
        return HERMAS2_CONTROL_SERVER_OK;
    }
    client->execution_id = execution_id;
    client->admitted = true;
    ++*progress_count;
    return HERMAS2_CONTROL_SERVER_OK;
}

static hermas2_control_server_result flush_results(
    hermas2_control_server *server,
    size_t *progress_count) {
    for (size_t index = 0u;
         index < HERMAS2_CONTROL_MAX_CLIENTS; ++index) {
        hermas2_control_client *client = &server->clients[index];
        if (!client->active || !client->admitted) {
            continue;
        }
        size_t packet_size = 0u;
        hermas2_control_result collected = hermas2_control_collect(
            server->loop, client->execution_id,
            server->packet, sizeof(server->packet), &packet_size);
        if (collected == HERMAS2_CONTROL_EXECUTION_ACTIVE) {
            continue;
        }
        if (collected != HERMAS2_CONTROL_OK) {
            return HERMAS2_CONTROL_SERVER_STATE_ERROR;
        }
        if (client->file_descriptor >= 0) {
            ssize_t sent = send(
                client->file_descriptor, server->packet, packet_size,
                MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent < 0 &&
                (errno == EAGAIN || errno == EWOULDBLOCK ||
                 errno == EINTR)) {
                continue;
            }
            if (sent != (ssize_t)packet_size) {
                close_descriptor(client);
            }
        }
        hermas2_control_result released =
            hermas2_control_release(
                server->loop, client->execution_id);
        if (released != HERMAS2_CONTROL_OK) {
            return HERMAS2_CONTROL_SERVER_STATE_ERROR;
        }
        clear_client(client);
        ++*progress_count;
    }
    return HERMAS2_CONTROL_SERVER_OK;
}

hermas2_control_server_result hermas2_control_server_init(
    hermas2_control_server *server,
    hermas2_daemon_loop *loop) {
    if (server == NULL || loop == NULL || loop->image == NULL) {
        return HERMAS2_CONTROL_SERVER_INVALID_ARGUMENT;
    }
    memset(server, 0, sizeof(*server));
    server->loop = loop;
    for (size_t index = 0u;
         index < HERMAS2_CONTROL_MAX_CLIENTS; ++index) {
        server->clients[index].file_descriptor = -1;
    }
    return HERMAS2_CONTROL_SERVER_OK;
}

hermas2_control_server_result hermas2_control_server_attach(
    hermas2_control_server *server,
    int client_descriptor) {
    if (server == NULL || server->loop == NULL ||
        client_descriptor < 0) {
        return HERMAS2_CONTROL_SERVER_INVALID_ARGUMENT;
    }
    int flags = fcntl(client_descriptor, F_GETFL);
    int descriptor_flags = fcntl(client_descriptor, F_GETFD);
    if (flags < 0 || descriptor_flags < 0 ||
        fcntl(client_descriptor, F_SETFL, flags | O_NONBLOCK) != 0 ||
        fcntl(
            client_descriptor, F_SETFD,
            descriptor_flags | FD_CLOEXEC) != 0) {
        return HERMAS2_CONTROL_SERVER_STATE_ERROR;
    }
    for (size_t index = 0u;
         index < HERMAS2_CONTROL_MAX_CLIENTS; ++index) {
        hermas2_control_client *client = &server->clients[index];
        if (!client->active) {
            *client = (hermas2_control_client){
                .file_descriptor = client_descriptor,
                .active = true
            };
            return HERMAS2_CONTROL_SERVER_OK;
        }
    }
    return HERMAS2_CONTROL_SERVER_CAPACITY_EXHAUSTED;
}

hermas2_control_server_result hermas2_control_server_accept(
    hermas2_control_server *server,
    int listener) {
    if (server == NULL || listener < 0) {
        return HERMAS2_CONTROL_SERVER_INVALID_ARGUMENT;
    }
    int client = accept4(
        listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK ||
                       errno == EINTR
                   ? HERMAS2_CONTROL_SERVER_OK
                   : HERMAS2_CONTROL_SERVER_ACCEPT_ERROR;
    }
    hermas2_control_server_result attached =
        hermas2_control_server_attach(server, client);
    if (attached != HERMAS2_CONTROL_SERVER_OK) {
        close(client);
    }
    return attached;
}

hermas2_control_server_result hermas2_control_server_step(
    hermas2_control_server *server,
    int timeout_milliseconds,
    size_t *progress_count) {
    if (server == NULL || server->loop == NULL ||
        progress_count == NULL || timeout_milliseconds < -1) {
        return HERMAS2_CONTROL_SERVER_INVALID_ARGUMENT;
    }
    *progress_count = 0u;
    hermas2_control_server_result advanced =
        advance_loop(server, progress_count);
    if (advanced != HERMAS2_CONTROL_SERVER_OK) {
        return advanced;
    }
    hermas2_control_server_result flushed =
        flush_results(server, progress_count);
    if (flushed != HERMAS2_CONTROL_SERVER_OK) {
        return flushed;
    }
    struct pollfd items[HERMAS2_CONTROL_MAX_CLIENTS];
    hermas2_control_client *owners[HERMAS2_CONTROL_MAX_CLIENTS];
    nfds_t count = 0u;
    bool executing = false;
    for (size_t index = 0u;
         index < HERMAS2_CONTROL_MAX_CLIENTS; ++index) {
        hermas2_control_client *client = &server->clients[index];
        if (!client->active) {
            continue;
        }
        executing = executing || client->admitted;
        if (client->file_descriptor < 0) {
            continue;
        }
        items[count] = (struct pollfd){
            .fd = client->file_descriptor,
            .events = POLLIN
        };
        owners[count] = client;
        ++count;
    }
    int wait = timeout_milliseconds;
    if (executing &&
        (wait < 0 || wait > HERMAS2_CONTROL_ACTIVE_QUANTUM_MS)) {
        wait = HERMAS2_CONTROL_ACTIVE_QUANTUM_MS;
    }
    int polled;
    do {
        polled = poll(items, count, wait);
    } while (polled < 0 && errno == EINTR);
    if (polled < 0) {
        return HERMAS2_CONTROL_SERVER_POLL_ERROR;
    }
    for (nfds_t index = 0u; index < count; ++index) {
        hermas2_control_client *client = owners[index];
        short events = items[index].revents;
        if ((events & POLLIN) != 0) {
            if (client->admitted) {
                close_descriptor(client);
                ++*progress_count;
            } else {
                hermas2_control_server_result received =
                    receive_execute(server, client, progress_count);
                if (received != HERMAS2_CONTROL_SERVER_OK) {
                    return received;
                }
            }
        } else if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            if (client->admitted) {
                close_descriptor(client);
            } else {
                clear_client(client);
            }
            ++*progress_count;
        }
    }
    advanced = advance_loop(server, progress_count);
    if (advanced != HERMAS2_CONTROL_SERVER_OK) {
        return advanced;
    }
    return flush_results(server, progress_count);
}

size_t hermas2_control_server_active(
    const hermas2_control_server *server) {
    if (server == NULL) {
        return 0u;
    }
    size_t count = 0u;
    for (size_t index = 0u;
         index < HERMAS2_CONTROL_MAX_CLIENTS; ++index) {
        count += server->clients[index].active ? 1u : 0u;
    }
    return count;
}

void hermas2_control_server_close(
    hermas2_control_server *server) {
    if (server == NULL) {
        return;
    }
    for (size_t index = 0u;
         index < HERMAS2_CONTROL_MAX_CLIENTS; ++index) {
        close_descriptor(&server->clients[index]);
    }
    memset(server, 0, sizeof(*server));
    for (size_t index = 0u;
         index < HERMAS2_CONTROL_MAX_CLIENTS; ++index) {
        server->clients[index].file_descriptor = -1;
    }
}

const char *hermas2_control_server_result_name(
    hermas2_control_server_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "capacity-exhausted",
        "accept-error", "poll-error", "loop-error", "state-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0])
               ? names[index]
               : "unknown";
}
