#include "hermas2/daemon.h"

#include "hermas2/image.h"
#include "hermas2/protocol.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define HERMAS2_HEADER_APPS_OFFSET 40u
#define HERMAS2_APP_RECORD_SIZE 36u

static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static hermas2_daemon_result receive_packet(
    int descriptor,
    uint8_t *packet,
    size_t capacity,
    size_t *packet_size) {
    struct iovec vector = {.iov_base = packet, .iov_len = capacity};
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1u;
    ssize_t received;
    do {
        received = recvmsg(descriptor, &message, 0);
    } while (received < 0 && errno == EINTR);
    if (received <= 0) {
        return HERMAS2_DAEMON_RECEIVE_ERROR;
    }
    if ((message.msg_flags & MSG_TRUNC) != 0 ||
        (size_t)received > capacity) {
        return HERMAS2_DAEMON_TRUNCATED_PACKET;
    }
    *packet_size = (size_t)received;
    return HERMAS2_DAEMON_OK;
}

hermas2_daemon_result hermas2_daemon_registry_init(
    hermas2_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size) {
    if (registry == NULL || image == NULL) {
        return HERMAS2_DAEMON_INVALID_ARGUMENT;
    }
    hermas2_image_summary summary;
    if (hermas2_image_validate(image, image_size, &summary) !=
            HERMAS2_IMAGE_OK ||
        summary.app_count > HERMAS2_DAEMON_MAX_APPS) {
        return HERMAS2_DAEMON_INVALID_IMAGE;
    }
    hermas2_daemon_registry fresh;
    memset(&fresh, 0, sizeof(fresh));
    fresh.app_count = summary.app_count;
    size_t apps_offset = read_u32(image, HERMAS2_HEADER_APPS_OFFSET);
    for (size_t index = 0u; index < fresh.app_count; ++index) {
        size_t offset = apps_offset + index * HERMAS2_APP_RECORD_SIZE;
        fresh.apps[index].app_id = read_u16(image, offset);
        fresh.apps[index].file_descriptor = -1;
        memcpy(fresh.apps[index].contract_fingerprint,
               image + offset + 4u, 32u);
    }
    *registry = fresh;
    return HERMAS2_DAEMON_OK;
}

hermas2_daemon_result hermas2_daemon_registry_accept(
    hermas2_daemon_registry *registry,
    int listener,
    uint8_t *packet_buffer,
    size_t packet_capacity) {
    if (registry == NULL || listener < 0 || packet_buffer == NULL ||
        packet_capacity < HERMAS2_PROTOCOL_HEADER_SIZE + 32u) {
        return HERMAS2_DAEMON_INVALID_ARGUMENT;
    }
    int connection;
    do {
        connection = accept(listener, NULL, NULL);
    } while (connection < 0 && errno == EINTR);
    if (connection < 0) {
        return HERMAS2_DAEMON_ACCEPT_ERROR;
    }
    size_t packet_size = 0u;
    hermas2_daemon_result received =
        receive_packet(connection, packet_buffer, packet_capacity,
                       &packet_size);
    if (received != HERMAS2_DAEMON_OK) {
        close(connection);
        return received;
    }
    hermas2_frame registration;
    if (hermas2_protocol_decode(packet_buffer, packet_size, &registration) !=
            HERMAS2_PROTOCOL_OK ||
        registration.kind != HERMAS2_FRAME_REGISTER_APP) {
        close(connection);
        return HERMAS2_DAEMON_PROTOCOL_ERROR;
    }
    hermas2_daemon_app *slot = NULL;
    for (size_t index = 0u; index < registry->app_count; ++index) {
        if (registry->apps[index].app_id == registration.app_id) {
            slot = &registry->apps[index];
            break;
        }
    }
    if (slot == NULL) {
        close(connection);
        return HERMAS2_DAEMON_UNEXPECTED_APP;
    }
    if (slot->file_descriptor >= 0) {
        close(connection);
        return HERMAS2_DAEMON_DUPLICATE_APP;
    }
    if (memcmp(slot->contract_fingerprint, registration.payload, 32u) != 0) {
        close(connection);
        return HERMAS2_DAEMON_CONTRACT_MISMATCH;
    }
    hermas2_frame acknowledgement = {
        .kind = HERMAS2_FRAME_REGISTER_OK,
        .app_id = registration.app_id,
        .outcome = HERMAS2_OUTCOME_NONE
    };
    size_t response_size = 0u;
    if (hermas2_protocol_encode(&acknowledgement, packet_buffer,
                                packet_capacity, &response_size) !=
            HERMAS2_PROTOCOL_OK ||
        send(connection, packet_buffer, response_size, MSG_NOSIGNAL) !=
            (ssize_t)response_size) {
        close(connection);
        return HERMAS2_DAEMON_SEND_ERROR;
    }
    slot->file_descriptor = connection;
    return HERMAS2_DAEMON_OK;
}

int hermas2_daemon_registry_find(
    const hermas2_daemon_registry *registry,
    uint16_t app_id) {
    if (registry == NULL || app_id == 0u) {
        return -1;
    }
    for (size_t index = 0u; index < registry->app_count; ++index) {
        if (registry->apps[index].app_id == app_id) {
            return registry->apps[index].file_descriptor;
        }
    }
    return -1;
}

void hermas2_daemon_registry_close(hermas2_daemon_registry *registry) {
    if (registry == NULL) {
        return;
    }
    for (size_t index = 0u; index < registry->app_count; ++index) {
        if (registry->apps[index].file_descriptor >= 0) {
            close(registry->apps[index].file_descriptor);
        }
    }
    memset(registry, 0, sizeof(*registry));
}

const char *hermas2_daemon_result_name(hermas2_daemon_result result) {
    static const char *const names[] = {
        "ok", "invalid-argument", "invalid-image", "accept-error",
        "receive-error", "truncated-packet", "protocol-error",
        "unexpected-app", "contract-mismatch", "duplicate-app", "send-error"
    };
    size_t index = (size_t)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}
