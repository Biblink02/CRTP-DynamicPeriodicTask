#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include "supervisor.h"
#include "task_routines.h"
#include "task_runtime.h"
#include "net_core.h"

typedef struct {
    Event buffer[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} EventQueue;

static EventQueue queue;

static struct {
    const TaskType *type;
    int instance_id;
} active_set[MAX_INSTANCES];

static int active_count = 0;
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;

void supervisor_init(void) {
    queue.head = 0;
    queue.tail = 0;
    queue.count = 0;
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.cond, NULL);
    printf("[Supervisor] Subsystem Initialized.\n");
}

int supervisor_push_event(Event ev) {
    int ret = -1;
    pthread_mutex_lock(&queue.mutex);
    if (queue.count < MAX_QUEUE_SIZE) {
        queue.buffer[queue.tail] = ev;
        queue.tail = (queue.tail + 1) % MAX_QUEUE_SIZE;
        queue.count++;
        pthread_cond_signal(&queue.cond);
        ret = 0;
    }
    pthread_mutex_unlock(&queue.mutex);
    return ret;
}

static Event queue_pop(void) {
    pthread_mutex_lock(&queue.mutex);
    while (queue.count == 0) {
        pthread_cond_wait(&queue.cond, &queue.mutex);
    }
    Event ev = queue.buffer[queue.head];
    queue.head = (queue.head + 1) % MAX_QUEUE_SIZE;
    queue.count--;
    pthread_mutex_unlock(&queue.mutex);
    return ev;
}

// --- Response Time Analysis ---

static int compare_period(const void *a, const void *b) {
    const TaskType *ta = *(const TaskType **) a;
    const TaskType *tb = *(const TaskType **) b;
    return (int) (ta->period_ms - tb->period_ms);
}

static int check_rta(const TaskType *candidate) {
    const TaskType *tasks[MAX_INSTANCES + 1];
    int count = 0;

    pthread_mutex_lock(&active_mutex);
    for (int i = 0; i < active_count; i++) tasks[count++] = active_set[i].type;
    pthread_mutex_unlock(&active_mutex);
    tasks[count++] = candidate;

    // Utilization Test (Necessary Condition)
    double util = 0;
    for (int i = 0; i < count; i++) {
        util += (double) tasks[i]->wcet_ms / (double) tasks[i]->period_ms;
    }
    if (util > 1.0) {
        printf("[RTA] Rejected %s: Utilization %.2f > 1.0\n", candidate->name, util);
        return 0;
    }

    // Response Time Analysis (Sufficient Condition)
    qsort(tasks, count, sizeof(const TaskType *), compare_period);

    for (int i = 0; i < count; i++) {
        double R = (double) tasks[i]->wcet_ms;
        double R_new = R;
        int converged = 0;

        for (int k = 0; k < 100; k++) {
            double I = 0;
            for (int j = 0; j < i; j++) {
                I += ceil(R / (double) tasks[j]->period_ms) * (double) tasks[j]->wcet_ms;
            }
            R_new = (double) tasks[i]->wcet_ms + I;

            if (R_new > (double) tasks[i]->deadline_ms) {
                printf("[RTA] Rejected %s: R=%.1f > D=%ld\n", candidate->name, R_new, tasks[i]->deadline_ms);
                return 0;
            }
            if (R_new == R) {
                converged = 1;
                break;
            }
            R = R_new;
        }
        if (!converged) return 0;
    }
    return 1;
}

static void handle_activate(Event ev) {
    char resp[64];
    const TaskType *task = routines_get_by_name(ev.payload.task_name);

    if (!task) {
        net_send_response(ev.client_fd, "ERR Unknown Task\n");
        return;
    }

    if (!check_rta(task)) {
        net_send_response(ev.client_fd, "ERR Schedulability\n");
        return;
    }

    // Pre-check capacity to avoid unnecessary thread spawning
    pthread_mutex_lock(&active_mutex);
    if (active_count >= MAX_INSTANCES) {
        pthread_mutex_unlock(&active_mutex);
        net_send_response(ev.client_fd, "ERR System Full\n");
        return;
    }
    pthread_mutex_unlock(&active_mutex);

    int id = runtime_create_instance(task);
    if (id < 0) {
        net_send_response(ev.client_fd, "ERR System Full\n");
        return;
    }

    pthread_mutex_lock(&active_mutex);
    if (active_count < MAX_INSTANCES) {
        active_set[active_count].type = task;
        active_set[active_count].instance_id = id;
        active_count++;
        snprintf(resp, sizeof(resp), "OK ID=%d\n", id);
        printf("[Supervisor] Activated task '%s' as ID %d (Total: %d)\n", task->name, id, active_count);
    } else {
        // Safe fallback in case of race condition
        runtime_stop_instance(id);
        snprintf(resp, sizeof(resp), "ERR System Full\n");
    }
    pthread_mutex_unlock(&active_mutex);

    net_send_response(ev.client_fd, resp);
}

static void handle_deactivate(Event ev) {
    int id = (int) ev.payload.target_id;
    if (runtime_stop_instance(id) == 0) {
        pthread_mutex_lock(&active_mutex);
        int idx = -1;
        for (int i = 0; i < active_count; i++) {
            if (active_set[i].instance_id == id) {
                idx = i;
                break;
            }
        }
        if (idx != -1) {
            for (int i = idx; i < active_count - 1; i++) active_set[i] = active_set[i + 1];
            active_count--;
        }
        pthread_mutex_unlock(&active_mutex);

        net_send_response(ev.client_fd, "OK\n");
        printf("[Supervisor] Deactivated task ID %d\n", id);
    } else {
        net_send_response(ev.client_fd, "ERR Invalid ID\n");
    }
}

static void handle_list(Event ev) {
    char resp[NET_RESPONSE_BUF_SIZE];
    int off = 0;
    pthread_mutex_lock(&active_mutex);
    off += snprintf(resp + off, sizeof(resp) - off, "Running: %d\n", active_count);
    for (int i = 0; i < active_count; i++) {
        if (sizeof(resp) - off < 100) break;
        off += snprintf(resp + off, sizeof(resp) - off, "  [ID %d] %s (C=%ld, T=%ld)\n",
                        active_set[i].instance_id, active_set[i].type->name,
                        active_set[i].type->wcet_ms, active_set[i].type->period_ms);
    }
    pthread_mutex_unlock(&active_mutex);
    net_send_response(ev.client_fd, resp);
}

static void handle_info(Event ev) {
    char resp[NET_RESPONSE_BUF_SIZE];
    int count, off = 0;
    const TaskType *cat = routines_get_all(&count);
    off += snprintf(resp + off, sizeof(resp) - off, "Capacity: %d/%d active\nTasks:\n", active_count, MAX_INSTANCES);
    for (int i = 0; i < count; i++) {
        off += snprintf(resp + off, sizeof(resp) - off, "  %s: C=%ld T=%ld D=%ld\n",
                        cat[i].name, cat[i].wcet_ms, cat[i].period_ms, cat[i].deadline_ms);
    }
    net_send_response(ev.client_fd, resp);
}

void supervisor_loop(void) {
    printf("[Supervisor] Event Loop Started.\n");
    while (1) {
        Event ev = queue_pop();
        switch (ev.type) {
            case EV_ACTIVATE: handle_activate(ev);
                break;
            case EV_DEACTIVATE: handle_deactivate(ev);
                break;
            case EV_LIST: handle_list(ev);
                break;
            case EV_INFO: handle_info(ev);
                break;
            case EV_SHUTDOWN:
                printf("[Supervisor] Shutdown signal received.\n");
                return;
            default: break;
        }
    }
}