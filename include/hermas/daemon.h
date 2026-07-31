#ifndef HERMAS_DAEMON_H
#define HERMAS_DAEMON_H

#include "hermas/image.h"
#include "hermas/runtime.h"
#include "hermas/journal.h"
#include "hermas/compensation.h"
#include "hermas/result.h"
#include "hermas/saga.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HERMAS_DAEMON_MAX_ACTIONS 80u
#define HERMAS_DAEMON_MAX_EXECUTIONS 16u
#define HERMAS_DAEMON_MAX_GROUP_EXECUTIONS 2u

typedef enum hermas_daemon_result {
    HERMAS_DAEMON_OK = 0,
    HERMAS_DAEMON_INVALID_ARGUMENT,
    HERMAS_DAEMON_INVALID_IMAGE,
    HERMAS_DAEMON_ACCEPT_ERROR,
    HERMAS_DAEMON_RECEIVE_ERROR,
    HERMAS_DAEMON_TRUNCATED_PACKET,
    HERMAS_DAEMON_PROTOCOL_ERROR,
    HERMAS_DAEMON_UNEXPECTED_APP,
    HERMAS_DAEMON_CONTRACT_MISMATCH,
    HERMAS_DAEMON_DUPLICATE_APP,
    HERMAS_DAEMON_SEND_ERROR
} hermas_daemon_result;

typedef struct hermas_daemon_action {
    uint16_t app_id;
    uint16_t action_id;
    uint16_t registered_action_id;
    int file_descriptor;
    uint8_t contract_fingerprint[32];
} hermas_daemon_action;

typedef struct hermas_daemon_registry {
    hermas_daemon_action actions[HERMAS_DAEMON_MAX_ACTIONS];
    size_t action_count;
} hermas_daemon_registry;

hermas_daemon_result hermas_daemon_registry_init(
    hermas_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size);

hermas_daemon_result hermas_daemon_registry_accept(
    hermas_daemon_registry *registry,
    int listener,
    uint8_t *packet_buffer,
    size_t packet_capacity);

int hermas_daemon_registry_find_action(
    const hermas_daemon_registry *registry,
    uint16_t app_id,
    uint16_t action_id,
    uint16_t *registered_action_id);

void hermas_daemon_registry_close(hermas_daemon_registry *registry);

const char *hermas_daemon_result_name(hermas_daemon_result result);

typedef enum hermas_loop_result {
    HERMAS_LOOP_OK = 0,
    HERMAS_LOOP_INVALID_ARGUMENT,
    HERMAS_LOOP_INVALID_IMAGE,
    HERMAS_LOOP_CAPACITY_EXHAUSTED,
    HERMAS_LOOP_DUPLICATE_EXECUTION,
    HERMAS_LOOP_UNKNOWN_EXECUTION,
    HERMAS_LOOP_EXECUTION_ACTIVE,
    HERMAS_LOOP_POLL_ERROR,
    HERMAS_LOOP_RUNTIME_ERROR,
    HERMAS_LOOP_PROTOCOL_ERROR,
    HERMAS_LOOP_JOURNAL_ERROR,
    HERMAS_LOOP_COMPENSATION_ERROR,
    HERMAS_LOOP_RESULT_ERROR,
    HERMAS_LOOP_UNSUPPORTED_GRAPH
} hermas_loop_result;

typedef struct hermas_loop_delivery {
    uint64_t request_id;
    uint16_t node_id;
    uint16_t app_id;
    uint16_t action_id;
    bool owns_action;
} hermas_loop_delivery;

typedef struct hermas_loop_slot {
    hermas_execution execution;
    uint8_t value[HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE];
    uint8_t packet[HERMAS_PROTOCOL_MAX_PACKET_SIZE];
    size_t packet_size;
    uint64_t request_id;
    uint16_t node_id;
    uint16_t app_id;
    uint16_t action_id;
    bool active;
    bool owns_action;
    bool journal_finished;
    bool result_stored;
    bool compensating;
    bool grouped;
    uint8_t group_index;
    hermas_loop_delivery group_deliveries[HERMAS_RUNTIME_MAX_FLOWS];
    uint8_t saga_success_count;
    uint64_t saga_forward_requests[HERMAS_SAGA_MAX_STEPS];
    hermas_saga_driver saga;
} hermas_loop_slot;

typedef struct hermas_daemon_loop {
    const uint8_t *image;
    size_t image_size;
    hermas_daemon_registry *registry;
    hermas_loop_slot executions[HERMAS_DAEMON_MAX_EXECUTIONS];
    hermas_group_execution
        group_executions[HERMAS_DAEMON_MAX_GROUP_EXECUTIONS];
    uint8_t group_values[HERMAS_DAEMON_MAX_GROUP_EXECUTIONS]
                        [HERMAS_RUNTIME_MAX_FLOWS]
                        [HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE];
    bool group_used[HERMAS_DAEMON_MAX_GROUP_EXECUTIONS];
    size_t scheduler_cursor;
    hermas_journal_writer *journal;
    hermas_result_writer *results;
    hermas_result_lookup result_lookup;
    void *result_lookup_context;
    hermas_compensation_writer *compensation;
    hermas_compensation_lookup compensation_lookup;
    void *compensation_lookup_context;
    hermas_saga_log_writer *saga_log;
    uint8_t compensation_scratch[
        HERMAS_COMPENSATION_HEADER_SIZE +
        HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE];
    uint32_t workflow_id;
    uint64_t image_fingerprint;
    uint64_t minimum_execution_id;
} hermas_daemon_loop;

/*
 * Validates both the graph-image format and the execution capabilities of
 * this daemon build without creating runtime or durable state.
 */
hermas_loop_result hermas_daemon_image_check(
    const uint8_t *image,
    size_t image_size);

/* The authoritative flow-feature mask implemented by this daemon build. */
hermas_graph_features hermas_daemon_supported_graph_features(void);

hermas_loop_result hermas_daemon_loop_init(
    hermas_daemon_loop *loop,
    hermas_daemon_registry *registry,
    const uint8_t *image,
    size_t image_size);

hermas_loop_result hermas_daemon_loop_admit(
    hermas_daemon_loop *loop,
    uint64_t execution_id,
    uint16_t input_type,
    const uint8_t *input,
    size_t input_length);

hermas_loop_result hermas_daemon_loop_attach_journal(
    hermas_daemon_loop *loop,
    hermas_journal_writer *journal,
    uint32_t workflow_id);

hermas_loop_result hermas_daemon_loop_set_execution_floor(
    hermas_daemon_loop *loop,
    uint64_t minimum_execution_id);

hermas_loop_result hermas_daemon_loop_attach_compensation(
    hermas_daemon_loop *loop,
    hermas_compensation_writer *compensation);

hermas_loop_result hermas_daemon_loop_attach_results(
    hermas_daemon_loop *loop,
    hermas_result_writer *results,
    hermas_result_lookup result_lookup,
    void *result_lookup_context);

hermas_loop_result hermas_daemon_loop_attach_saga(
    hermas_daemon_loop *loop,
    hermas_compensation_writer *compensation,
    hermas_compensation_lookup compensation_lookup,
    void *compensation_lookup_context,
    hermas_saga_log_writer *saga_log);

hermas_loop_result hermas_daemon_loop_resume_saga(
    hermas_daemon_loop *loop,
    const hermas_saga_execution *execution);

hermas_loop_result hermas_daemon_loop_poll(
    hermas_daemon_loop *loop,
    int timeout_milliseconds,
    size_t *progress_count);

hermas_loop_result hermas_daemon_loop_result(
    const hermas_daemon_loop *loop,
    uint64_t execution_id,
    hermas_frame *result);

hermas_loop_result hermas_daemon_loop_release(
    hermas_daemon_loop *loop,
    uint64_t execution_id);

size_t hermas_daemon_loop_active(const hermas_daemon_loop *loop);

const char *hermas_loop_result_name(hermas_loop_result result);

#endif
