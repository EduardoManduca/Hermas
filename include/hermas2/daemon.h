#ifndef HERMAS2_DAEMON_H
#define HERMAS2_DAEMON_H

#include "hermas2/runtime.h"
#include "hermas2/journal.h"
#include "hermas2/compensation.h"
#include "hermas2/result.h"
#include "hermas2/saga.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HERMAS2_DAEMON_MAX_APPS 64u
#define HERMAS2_DAEMON_MAX_EXECUTIONS 16u

typedef enum hermas2_daemon_result {
    HERMAS2_DAEMON_OK = 0,
    HERMAS2_DAEMON_INVALID_ARGUMENT,
    HERMAS2_DAEMON_INVALID_IMAGE,
    HERMAS2_DAEMON_ACCEPT_ERROR,
    HERMAS2_DAEMON_RECEIVE_ERROR,
    HERMAS2_DAEMON_TRUNCATED_PACKET,
    HERMAS2_DAEMON_PROTOCOL_ERROR,
    HERMAS2_DAEMON_UNEXPECTED_APP,
    HERMAS2_DAEMON_CONTRACT_MISMATCH,
    HERMAS2_DAEMON_DUPLICATE_APP,
    HERMAS2_DAEMON_SEND_ERROR
} hermas2_daemon_result;

typedef struct hermas2_daemon_app {
    uint16_t app_id;
    int file_descriptor;
    uint8_t contract_fingerprint[32];
} hermas2_daemon_app;

typedef struct hermas2_daemon_registry {
    hermas2_daemon_app apps[HERMAS2_DAEMON_MAX_APPS];
    size_t app_count;
} hermas2_daemon_registry;

hermas2_daemon_result hermas2_daemon_registry_init(
    hermas2_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size);

hermas2_daemon_result hermas2_daemon_registry_accept(
    hermas2_daemon_registry *registry,
    int listener,
    uint8_t *packet_buffer,
    size_t packet_capacity);

int hermas2_daemon_registry_find(
    const hermas2_daemon_registry *registry,
    uint16_t app_id);

void hermas2_daemon_registry_close(hermas2_daemon_registry *registry);

const char *hermas2_daemon_result_name(hermas2_daemon_result result);

typedef enum hermas2_loop_result {
    HERMAS2_LOOP_OK = 0,
    HERMAS2_LOOP_INVALID_ARGUMENT,
    HERMAS2_LOOP_INVALID_IMAGE,
    HERMAS2_LOOP_CAPACITY_EXHAUSTED,
    HERMAS2_LOOP_DUPLICATE_EXECUTION,
    HERMAS2_LOOP_UNKNOWN_EXECUTION,
    HERMAS2_LOOP_EXECUTION_ACTIVE,
    HERMAS2_LOOP_POLL_ERROR,
    HERMAS2_LOOP_RUNTIME_ERROR,
    HERMAS2_LOOP_PROTOCOL_ERROR,
    HERMAS2_LOOP_JOURNAL_ERROR,
    HERMAS2_LOOP_COMPENSATION_ERROR,
    HERMAS2_LOOP_RESULT_ERROR
} hermas2_loop_result;

typedef struct hermas2_loop_slot {
    hermas2_execution execution;
    uint8_t value[HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
    uint8_t packet[HERMAS2_PROTOCOL_MAX_PACKET_SIZE];
    size_t packet_size;
    uint64_t request_id;
    uint16_t node_id;
    uint16_t app_id;
    uint16_t action_id;
    bool active;
    bool owns_app;
    bool journal_finished;
    bool result_stored;
    bool compensating;
    uint8_t saga_success_count;
    uint64_t saga_forward_requests[HERMAS2_SAGA_MAX_STEPS];
    hermas2_saga_driver saga;
} hermas2_loop_slot;

typedef struct hermas2_daemon_loop {
    const uint8_t *image;
    size_t image_size;
    hermas2_daemon_registry *registry;
    hermas2_loop_slot executions[HERMAS2_DAEMON_MAX_EXECUTIONS];
    size_t scheduler_cursor;
    hermas2_journal_writer *journal;
    hermas2_result_writer *results;
    hermas2_result_lookup result_lookup;
    void *result_lookup_context;
    hermas2_compensation_writer *compensation;
    hermas2_compensation_lookup compensation_lookup;
    void *compensation_lookup_context;
    hermas2_saga_log_writer *saga_log;
    uint8_t compensation_scratch[
        HERMAS2_COMPENSATION_HEADER_SIZE +
        HERMAS2_PROTOCOL_MAX_PAYLOAD_SIZE];
    uint32_t workflow_id;
    uint64_t image_fingerprint;
} hermas2_daemon_loop;

hermas2_loop_result hermas2_daemon_loop_init(
    hermas2_daemon_loop *loop,
    hermas2_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size);

hermas2_loop_result hermas2_daemon_loop_admit(
    hermas2_daemon_loop *loop,
    uint64_t execution_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length);

hermas2_loop_result hermas2_daemon_loop_attach_journal(
    hermas2_daemon_loop *loop,
    hermas2_journal_writer *journal,
    uint32_t workflow_id);

hermas2_loop_result hermas2_daemon_loop_attach_compensation(
    hermas2_daemon_loop *loop,
    hermas2_compensation_writer *compensation);

hermas2_loop_result hermas2_daemon_loop_attach_results(
    hermas2_daemon_loop *loop,
    hermas2_result_writer *results,
    hermas2_result_lookup result_lookup,
    void *result_lookup_context);

hermas2_loop_result hermas2_daemon_loop_attach_saga(
    hermas2_daemon_loop *loop,
    hermas2_compensation_writer *compensation,
    hermas2_compensation_lookup compensation_lookup,
    void *compensation_lookup_context,
    hermas2_saga_log_writer *saga_log);

hermas2_loop_result hermas2_daemon_loop_resume_saga(
    hermas2_daemon_loop *loop,
    const hermas2_saga_execution *execution);

hermas2_loop_result hermas2_daemon_loop_poll(
    hermas2_daemon_loop *loop,
    int timeout_milliseconds,
    size_t *progress_count);

hermas2_loop_result hermas2_daemon_loop_result(
    const hermas2_daemon_loop *loop,
    uint64_t execution_id,
    hermas2_frame *result);

hermas2_loop_result hermas2_daemon_loop_release(
    hermas2_daemon_loop *loop,
    uint64_t execution_id);

size_t hermas2_daemon_loop_active(const hermas2_daemon_loop *loop);

const char *hermas2_loop_result_name(hermas2_loop_result result);

#endif
