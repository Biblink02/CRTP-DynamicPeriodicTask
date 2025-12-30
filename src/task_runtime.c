#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include "task_runtime.h"

static TaskInstance instance_pool[MAX_INSTANCES];
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int global_id_counter = 1;

static long long timespec_diff_ns(const struct timespec t_start, const struct timespec t_end) {
    return (long long) (t_end.tv_sec - t_start.tv_sec) * 1000000000LL +
           (t_end.tv_nsec - t_start.tv_nsec);
}

static void high_precision_sleep(const long long ns) {
    if (ns <= 0) return;
    struct timespec req;
    req.tv_sec = ns / 1000000000LL;
    req.tv_nsec = ns % 1000000000LL;
    nanosleep(&req, NULL);
}

static void *task_thread_entry(const void *arg) {
    const TaskInstance *inst = arg;
    printf("[Runtime] Thread for Task ID %d (%s) started\n", inst->id, inst->type->name);

    struct timespec t_start, t_now;
    const long long period_ns = inst->type->period_ms * 1000000LL;

    while (!inst->stop) {
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        if (inst->type->routine_fn) {
            inst->type->routine_fn();
        }

        /* Drift Compensation:
         * Calculate elapsed time and subtract it from the period.
         * This prevents execution errors from accumulating over time.
         */
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        const long long elapsed_ns = timespec_diff_ns(t_start, t_now);
        const long long sleep_ns = period_ns - elapsed_ns;

        /*printf("[Runtime][Debug] Task %d (%s): Computed WCET: %ld ms | ACTUAL Execution: %.2f ms | Load: %.1f%%\n",
               inst->id, inst->type->name,
               inst->type->wcet_ms,
               (double) elapsed_ns / 1000000.0,
               (double) elapsed_ns / (double) period_ns * 100.0);*/

        if (sleep_ns > 0) {
            high_precision_sleep(sleep_ns);
        } else {
            printf("[Runtime] WARNING: Deadline miss/Overrun for Task ID %d (%lld us overrun)\n",
                   inst->id, -sleep_ns / 1000);
        }
    }
    printf("[Runtime] Thread for Task ID %d exiting\n", inst->id);
    return NULL;
}

void runtime_init(void) {
    pthread_mutex_lock(&pool_mutex);
    for (int i = 0; i < MAX_INSTANCES; i++) {
        instance_pool[i].active = false;
        instance_pool[i].id = -1;
    }
    atomic_store(&global_id_counter, 1);
    pthread_mutex_unlock(&pool_mutex);
    printf("[Runtime] Pool initialized. Capacity: %d\n", MAX_INSTANCES);
}

int runtime_create_instance(const TaskType *type) {
    if (!type) return -1;

    pthread_mutex_lock(&pool_mutex);
    int idx = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!instance_pool[i].active) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        pthread_mutex_unlock(&pool_mutex);
        printf("[Runtime] Error: Pool full, cannot create %s\n", type->name);
        return -1;
    }

    TaskInstance *inst = &instance_pool[idx];
    inst->id = atomic_fetch_add(&global_id_counter, 1);
    inst->type = type;
    inst->stop = false;
    inst->active = true;

    pthread_attr_t attr;
    struct sched_param param;

    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    // Set Real-Time FIFO policy to ensure strict priority adherence
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    /* Map Period to Priority (Rate Monotonic):
     * Shorter period = Higher priority (1-99).
     * Heuristic: 95 - (period / 50).
     */
    int prio = 95 - (int) (type->period_ms / 50);
    if (prio < 1) prio = 1;
    if (prio > 99) prio = 99;

    param.sched_priority = prio;
    pthread_attr_setschedparam(&attr, &param);

    printf("[Runtime] Creating Task %s with SCHED_FIFO Priority %d\n", type->name, prio);

    const int rc = pthread_create(&inst->thread, &attr, (void *) task_thread_entry, inst);
    if (rc != 0) {
        inst->active = false;
        pthread_attr_destroy(&attr);
        pthread_mutex_unlock(&pool_mutex);

        if (rc == EPERM) {
            fprintf(stderr, "[Runtime] Error: Operation not permitted. Root/sudo is required for SCHED_FIFO.\n");
        } else {
            fprintf(stderr, "[Runtime] Error: pthread_create failed: %s\n", strerror(rc));
        }
        return -1;
    }

    pthread_attr_destroy(&attr);
    const int created_id = inst->id;
    pthread_mutex_unlock(&pool_mutex);

    printf("[Runtime] Instance created: ID %d [%s]\n", created_id, type->name);
    return created_id;
}

int runtime_stop_instance(int id) {
    pthread_mutex_lock(&pool_mutex);
    int idx = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (instance_pool[i].active && instance_pool[i].id == id) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        pthread_mutex_unlock(&pool_mutex);
        printf("[Runtime] Error: Stop request for non-existent ID %d\n", id);
        return -1;
    }

    instance_pool[idx].stop = true;
    pthread_mutex_unlock(&pool_mutex);

    printf("[Runtime] Joining thread for ID %d...\n", id);
    pthread_join(instance_pool[idx].thread, NULL);

    pthread_mutex_lock(&pool_mutex);
    instance_pool[idx].active = false;
    instance_pool[idx].id = -1;
    pthread_mutex_unlock(&pool_mutex);

    printf("[Runtime] Instance ID %d stopped and cleaned up\n", id);
    return 0;
}

int runtime_get_active_instances(TaskInstance **out_instances, int max_len) {
    int count = 0;
    pthread_mutex_lock(&pool_mutex);
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (instance_pool[i].active) {
            if (count < max_len && out_instances) {
                out_instances[count] = &instance_pool[i];
            }
            count++;
        }
    }
    pthread_mutex_unlock(&pool_mutex);
    return count;
}

void runtime_cleanup(void) {
    // Signal all active tasks to stop
    pthread_mutex_lock(&pool_mutex);
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (instance_pool[i].active) {
            instance_pool[i].stop = true;
        }
    }
    pthread_mutex_unlock(&pool_mutex);

    // Join all threads to ensure resource deallocation
    for (int i = 0; i < MAX_INSTANCES; i++) {
        pthread_t thread_handle = 0;
        int id = -1;
        bool needs_join = false;

        pthread_mutex_lock(&pool_mutex);
        if (instance_pool[i].active) {
            thread_handle = instance_pool[i].thread;
            id = instance_pool[i].id;
            needs_join = true;
        }
        pthread_mutex_unlock(&pool_mutex);

        if (needs_join) {
            printf("[Runtime] Shutdown: Joining Task ID %d...\n", id);
            pthread_join(thread_handle, NULL);
            printf("[Runtime] Task ID %d joined.\n", id);
        }
    }
    printf("[Runtime] Cleanup complete. All tasks stopped.\n");
}
