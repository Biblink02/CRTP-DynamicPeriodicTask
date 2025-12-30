#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include "supervisor.h"
#include "task_routines.h"
#include "task_runtime.h"
#include "net_core.h"
#include "constants.h"

typedef struct {
    Event buffer[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} EventQueue;

typedef struct {
    const TaskType *type;
    int instance_id;
} ActiveTask;

static ActiveTask active_set[MAX_INSTANCES];
static int active_count = 0;
static EventQueue event_queue;

static void queue_init(void) {
    event_queue.head = 0;
    event_queue.tail = 0;
    event_queue.count = 0;
    pthread_mutex_init(&event_queue.mutex, NULL);
    pthread_cond_init(&event_queue.cond, NULL);
}

void supervisor_push_event(Event ev) {
    pthread_mutex_lock(&event_queue.mutex);
    if (event_queue.count < MAX_QUEUE_SIZE) {
        event_queue.buffer[event_queue.tail] = ev;
        event_queue.tail = (event_queue.tail + 1) % MAX_QUEUE_SIZE;
        event_queue.count++;
        pthread_cond_signal(&event_queue.cond);
        printf("[Supervisor] Event pushed to queue (Count: %d)\n", event_queue.count);
    } else {
        fprintf(stderr, "[Supervisor] Queue full, dropping event\n");
    }
    pthread_mutex_unlock(&event_queue.mutex);
}

static Event queue_pop_blocking(void) {
    pthread_mutex_lock(&event_queue.mutex);
    while (event_queue.count == 0) {
        pthread_cond_wait(&event_queue.cond, &event_queue.mutex);
    }
    const Event ev = event_queue.buffer[event_queue.head];
    event_queue.head = (event_queue.head + 1) % MAX_QUEUE_SIZE;
    event_queue.count--;
    pthread_mutex_unlock(&event_queue.mutex);
    return ev;
}

// Comparator for Rate Monotonic Scheduling (RMS): shorter period = higher priority
static int compare_tasks_rm(const void *a, const void *b) {
    const TaskType *ta = *(const TaskType **) a;
    const TaskType *tb = *(const TaskType **) b;
    return (int) (ta->period_ms - tb->period_ms);
}

/*
 * Performs Response Time Analysis (RTA) to determine if adding a new task
 * would violate any deadlines in the system.
 * It assumes Fixed-Priority Preemptive Scheduling based on Rate Monotonic priorities.
 */
static int check_schedulability(const TaskType *candidate) {
    printf("[RTA] Starting Analysis. Candidate: %s (C=%ld, T=%ld)\n",
           candidate->name, candidate->wcet_ms, candidate->period_ms);

    const TaskType *temp_list[MAX_INSTANCES + 1];
    int count = 0;

    for (int i = 0; i < active_count; i++) {
        temp_list[count++] = active_set[i].type;
    }
    temp_list[count++] = candidate;

    qsort(temp_list, count, sizeof(const TaskType *), compare_tasks_rm);

    for (int i = 0; i < count; i++) {
        const TaskType *tau_i = temp_list[i];
        double R = (double) tau_i->wcet_ms;

        // Iteratively calculate response time including interference from higher priority tasks
        while (1) {
            double interference = 0;
            for (int j = 0; j < i; j++) {
                const TaskType *tau_j = temp_list[j];
                interference += ceil(R / (double) tau_j->period_ms) * (double) tau_j->wcet_ms;
            }

            const double R_new = (double) tau_i->wcet_ms + interference;

            if (R_new > (double) tau_i->deadline_ms) {
                printf("[RTA] FAILED. Task %s R=%.1f > D=%ld\n", tau_i->name, R_new, tau_i->deadline_ms);
                return 0;
            }
            if (R_new == R) break;
            R = R_new;
        }
        printf("[RTA] Task %s Feasible (R=%.1f <= D=%ld)\n", tau_i->name, R, tau_i->deadline_ms);
    }
    return 1;
}

static void handle_activate(Event ev) {
    printf("[Supervisor] Processing ACTIVATE '%s'\n", ev.payload.task_name);
    char response[64];

    const TaskType *task = routines_get_by_name(ev.payload.task_name);
    if (!task) {
        printf("[Supervisor] Task '%s' not found in catalog\n", ev.payload.task_name);
        snprintf(response, sizeof(response), "ERR Unknown Task\n");
        net_send_response(ev.client_fd, response);
        return;
    }

    if (!check_schedulability(task)) {
        printf("[Supervisor] Schedulability check rejected '%s'\n", task->name);
        snprintf(response, sizeof(response), "ERR Schedulability\n");
        net_send_response(ev.client_fd, response);
        return;
    }

    const int id = runtime_create_instance(task);
    if (id < 0) {
        snprintf(response, sizeof(response), "ERR System Full\n");
    } else {
        active_set[active_count].type = task;
        active_set[active_count].instance_id = id;
        active_count++;
        snprintf(response, sizeof(response), "OK ID=%d\n", id);
        printf("[Supervisor] Activation Success. Active Tasks: %d\n", active_count);
    }
    net_send_response(ev.client_fd, response);
}

static void handle_deactivate(const Event ev) {
    printf("[Supervisor] Processing DEACTIVATE ID %ld\n", ev.payload.target_id);
    char response[64];
    const int id = (int) ev.payload.target_id;

    if (runtime_stop_instance(id) == 0) {
        int found_idx = -1;
        for (int i = 0; i < active_count; i++) {
            if (active_set[i].instance_id == id) {
                found_idx = i;
                break;
            }
        }

        if (found_idx != -1) {
            // Compact the active set array
            for (int i = found_idx; i < active_count - 1; i++) {
                active_set[i] = active_set[i + 1];
            }
            active_count--;
        }
        snprintf(response, sizeof(response), "OK\n");
    } else {
        snprintf(response, sizeof(response), "ERR Invalid ID\n");
    }
    net_send_response(ev.client_fd, response);
}

static void handle_list(const Event ev) {
    // Large buffer to hold the multiline string. NET_RESPONSE_BUF_SIZE is 4096.
    char response[NET_RESPONSE_BUF_SIZE - 64];
    int offset = 0;

    offset += snprintf(response + offset, sizeof(response) - offset, "Active Count: %d\n", active_count);

    if (active_count == 0) {
        offset += snprintf(response + offset, sizeof(response) - offset, "  (No active tasks)\n");
    } else {
        for (int i = 0; i < active_count; i++) {
            // Prevent buffer overflow
            if (sizeof(response) - offset < 100) {
                offset += snprintf(response + offset, sizeof(response) - offset, "  ... (list truncated)\n");
                break;
            }
            offset += snprintf(response + offset, sizeof(response) - offset,
                               "  [ID %d] Name: %s | C: %ld | T: %ld | D: %ld\n",
                               active_set[i].instance_id,
                               active_set[i].type->name,
                               active_set[i].type->wcet_ms,
                               active_set[i].type->period_ms,
                               active_set[i].type->deadline_ms
            );
        }
    }

    net_send_response(ev.client_fd, response);
}

void supervisor_loop(void) {
    queue_init();
    routines_init();
    runtime_init();

    printf("[Supervisor] Event Loop Started.\n");

    while (1) {
        const Event ev = queue_pop_blocking();
        switch (ev.type) {
            case EV_ACTIVATE: handle_activate(ev);
                break;
            case EV_DEACTIVATE: handle_deactivate(ev);
                break;
            case EV_SHUTDOWN:
                printf("[Supervisor] Shutdown signal received.\n");
                return;
            case EV_LIST: handle_list(ev);
            default: break;
        }
    }
}
