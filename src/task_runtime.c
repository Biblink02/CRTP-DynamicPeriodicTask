#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "task_runtime.h"

static TaskInstance pool[MAX_INSTANCES];
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int id_counter = 1;

static long long diff_ns(struct timespec t1, struct timespec t2) {
    return (long long) (t2.tv_sec - t1.tv_sec) * 1000000000LL + (t2.tv_nsec - t1.tv_nsec);
}

static void *thread_entry(void *arg) {
    TaskInstance *inst = (TaskInstance *) arg;
    struct timespec next_activation, start, end;
    long long period_ns = inst->type->period_ms * 1000000LL;
    long long deadline_ns = inst->type->deadline_ms * 1000000LL;

    // Anchor: Absolute time for first activation
    clock_gettime(CLOCK_MONOTONIC, &next_activation);

    while (!inst->stop) {
        clock_gettime(CLOCK_MONOTONIC, &start);

        // Execute Workload
        if (inst->type->routine_fn) inst->type->routine_fn();

        clock_gettime(CLOCK_MONOTONIC, &end);

        // Check Deadline
        long long exec_time = diff_ns(start, end);
        if (exec_time > deadline_ns) {
            printf("[Runtime] DEADLINE MISS: Task %s (ID %d) | Exec: %.2f ms > Limit: %ld ms\n",
                   inst->type->name, inst->id,
                   exec_time / 1000000.0, inst->type->deadline_ms);
        }

        // Calculate next absolute wake-up time (t_{k+1} = t_k + T)
        // This prevents accumulated drift.
        next_activation.tv_nsec += period_ns;
        while (next_activation.tv_nsec >= 1000000000LL) {
            next_activation.tv_sec++;
            next_activation.tv_nsec -= 1000000000LL;
        }

        // Sleep until next absolute time (TIMER_ABSTIME)
        // Handles SIGUSR1 (EINTR) for immediate shutdown.
        while (!inst->stop) {
            int ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_activation, NULL);
            if (ret == 0) break;
            if (ret == EINTR) break;
        }
    }
    return NULL;
}

void runtime_init(void) {
    pthread_mutex_lock(&pool_mutex);
    for (int i = 0; i < MAX_INSTANCES; i++) {
        pool[i].active = false;
        pool[i].id = -1;
    }
    atomic_store(&id_counter, 1);
    pthread_mutex_unlock(&pool_mutex);
}

int runtime_create_instance(const TaskType *type) {
    pthread_mutex_lock(&pool_mutex);
    int idx = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!pool[i].active) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        pthread_mutex_unlock(&pool_mutex);
        return -1;
    }

    TaskInstance *inst = &pool[idx];
    inst->id = atomic_fetch_add(&id_counter, 1);
    inst->type = type;
    inst->stop = false;
    inst->active = true;

    pthread_attr_t attr;
    struct sched_param param;

    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    // RMS: Higher frequency = Higher Priority
    // Mapped to range [1, 90] to leave room for system threads
    int prio = 90 - (int) (type->period_ms / 100);
    param.sched_priority = (prio < 1) ? 1 : (prio > 90) ? 90 : prio;
    pthread_attr_setschedparam(&attr, &param);

    if (pthread_create(&inst->thread, &attr, thread_entry, inst) != 0) {
        inst->active = false;
        pthread_attr_destroy(&attr);
        pthread_mutex_unlock(&pool_mutex);
        fprintf(stderr, "[Runtime] Error creating thread. Check sudo/permissions.\n");
        return -1;
    }

    pthread_attr_destroy(&attr);
    int id = inst->id;
    pthread_mutex_unlock(&pool_mutex);
    return id;
}

int runtime_stop_instance(int id) {
    pthread_mutex_lock(&pool_mutex);
    int idx = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (pool[i].active && pool[i].id == id) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        pthread_mutex_unlock(&pool_mutex);
        return -1;
    }

    pool[idx].stop = true;

    // Interrupt nanosleep immediately to avoid waiting for the full period
    pthread_kill(pool[idx].thread, SIGUSR1);

    pthread_mutex_unlock(&pool_mutex);
    pthread_join(pool[idx].thread, NULL);

    pthread_mutex_lock(&pool_mutex);
    pool[idx].active = false;
    pool[idx].id = -1;
    pthread_mutex_unlock(&pool_mutex);
    return 0;
}

void runtime_cleanup(void) {
    pthread_mutex_lock(&pool_mutex);
    // Signal all threads to stop
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (pool[i].active) {
            pool[i].stop = true;
            pthread_kill(pool[i].thread, SIGUSR1);
        }
    }
    pthread_mutex_unlock(&pool_mutex);

    // Join all threads
    for (int i = 0; i < MAX_INSTANCES; i++) {
        pthread_t t = 0;
        bool active = false;
        pthread_mutex_lock(&pool_mutex);
        if (pool[i].active) {
            t = pool[i].thread;
            active = true;
        }
        pthread_mutex_unlock(&pool_mutex);

        if (active) pthread_join(t, NULL);
    }
}
