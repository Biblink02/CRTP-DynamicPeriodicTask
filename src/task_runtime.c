#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include "task_runtime.h"

static TaskInstance instance_pool[MAX_INSTANCES];
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int global_id_counter = 1;

static long long timespec_diff_ns(struct timespec t_start, struct timespec t_end) {
    return (long long) (t_end.tv_sec - t_start.tv_sec) * 1000000000LL +
           (t_end.tv_nsec - t_start.tv_nsec);
}

static void high_precision_sleep(long long ns) {
    if (ns <= 0) return;
    struct timespec req;
    req.tv_sec = ns / 1000000000LL;
    req.tv_nsec = ns % 1000000000LL;
    nanosleep(&req, NULL);
}

static void *task_thread_entry(void *arg) {
    TaskInstance *inst = (TaskInstance *) arg;
    printf("[Runtime] Thread for Task ID %d (%s) started\n", inst->id, inst->type->name);

    struct timespec t_start, t_now;
    long long period_ns = inst->type->period_ms * 1000000LL;

    while (!inst->stop) {
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        if (inst->type->routine_fn) {
            inst->type->routine_fn();
        }

        // Drift Compensation to ensure period accuracy
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        long long elapsed_ns = timespec_diff_ns(t_start, t_now);
        long long sleep_ns = period_ns - elapsed_ns;

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
    atomic_init(&global_id_counter, 1);
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

    if (pthread_create(&inst->thread, NULL, task_thread_entry, inst) != 0) {
        inst->active = false;
        pthread_mutex_unlock(&pool_mutex);
        perror("[Runtime] pthread_create failed");
        return -1;
    }

    int created_id = inst->id;
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
