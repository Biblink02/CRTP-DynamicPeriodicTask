#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "task_runtime.h"

#include "logger.h"

static TaskInstance pool[MAX_INSTANCES];
static pthread_mutex_t pool_mutex;
static atomic_int id_counter = 1;

// --- Time Helpers ---

static long long diff_ns(const struct timespec t1, const struct timespec t2) {
    return (long long) (t2.tv_sec - t1.tv_sec) * 1000000000LL + (t2.tv_nsec - t1.tv_nsec);
}

static inline void timespec_add_ns(struct timespec *t, const long long ns) {
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000LL) {
        t->tv_sec++;
        t->tv_nsec -= 1000000000LL;
    }
}

static inline int timespec_cmp(const struct timespec *a, const struct timespec *b) {
    if (a->tv_sec != b->tv_sec) return (a->tv_sec > b->tv_sec) ? 1 : -1;
    if (a->tv_nsec != b->tv_nsec) return (a->tv_nsec > b->tv_nsec) ? 1 : -1;
    return 0;
}

// --- Thread Loop ---

static void *thread_entry(void *arg) {
    TaskInstance *inst = arg;
    struct timespec next_activation, start, end, now;
    const long long period_ns = inst->type->period_ms * 1000000LL;
    const long long deadline_ns = inst->type->deadline_ms * 1000000LL;

    clock_gettime(CLOCK_MONOTONIC, &next_activation);

    while (!atomic_load_explicit(&inst->stop, memory_order_relaxed)) {

        struct timespec release = next_activation;   // ideal activation for *this* job

        clock_gettime(CLOCK_MONOTONIC, &start);
        if (inst->type->routine_fn) inst->type->routine_fn();
        clock_gettime(CLOCK_MONOTONIC, &end);

        struct timespec abs_deadline = release;
        timespec_add_ns(&abs_deadline, deadline_ns);

        if (timespec_cmp(&end, &abs_deadline) > 0) {
            const long long response_time = diff_ns(release, end);
            rt_log("[Runtime] DEADLINE MISS: Task %s (ID %d) | Resp: %.2f ms > Limit: %ld ms\n",
                   inst->type->name, inst->id, response_time / 1000000.0, inst->type->deadline_ms);
        }

        next_activation = release;
        timespec_add_ns(&next_activation, period_ns);

        clock_gettime(CLOCK_MONOTONIC, &now);
        while (timespec_cmp(&next_activation, &now) <= 0) {
            timespec_add_ns(&next_activation, period_ns);
        }

        while (!atomic_load_explicit(&inst->stop, memory_order_relaxed)) {
            int ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_activation, NULL);
            if (ret == 0) break;
            if (ret == EINTR) break;
        }
    }

    return NULL;
}

int runtime_init(void) {
    if (pthread_mutex_init(&pool_mutex, NULL) != 0) {
        return -1;
    }

    for (int i = 0; i < MAX_INSTANCES; i++) {
        pool[i].active = false;
        pool[i].id = -1;
        atomic_init(&pool[i].stop, false);
    }
    atomic_store(&id_counter, 1);

    return 0;
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
    atomic_store_explicit(&inst->stop, false, memory_order_relaxed);
    inst->active = true;

    pthread_attr_t attr;
    struct sched_param param;

    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    // RMS: Higher frequency = Higher Priority
    // Mapped to range [2, 90] to leave room for system threads
    // Logger is at 1, so the lowest task priority must be 2.
    const int prio = 90 - (int) (type->period_ms / 100);
    param.sched_priority = prio < 2 ? 2 : prio > 90 ? 90 : prio;
    pthread_attr_setschedparam(&attr, &param);

    if (pthread_create(&inst->thread, &attr, thread_entry, inst) != 0) {
        inst->active = false;
        pthread_attr_destroy(&attr);
        pthread_mutex_unlock(&pool_mutex);
        fprintf(stderr, "[Runtime] Error creating thread. Check sudo/permissions.\n");
        return -1;
    }

    pthread_attr_destroy(&attr);
    const int id = inst->id;
    pthread_mutex_unlock(&pool_mutex);
    return id;
}

int runtime_stop_instance(const int id) {
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

    atomic_store_explicit(&pool[idx].stop, true, memory_order_relaxed);
    const pthread_t t = pool[idx].thread;

    pthread_mutex_unlock(&pool_mutex);

    pthread_kill(t, SIGUSR1);
    pthread_join(t, NULL);

    pthread_mutex_lock(&pool_mutex);
    pool[idx].active = false;
    pool[idx].id = -1;
    pthread_mutex_unlock(&pool_mutex);
    return 0;
}

void runtime_cleanup(void) {
    pthread_mutex_lock(&pool_mutex);
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (pool[i].active) {
            atomic_store_explicit(&pool[i].stop, true, memory_order_relaxed);
            pthread_kill(pool[i].thread, SIGUSR1);
        }
    }
    pthread_mutex_unlock(&pool_mutex);

    for (int i = 0; i < MAX_INSTANCES; i++) {
        pthread_t t = 0;
        bool active = false;

        pthread_mutex_lock(&pool_mutex);
        if (pool[i].active) {
            t = pool[i].thread;
            active = true;
        }
        pthread_mutex_unlock(&pool_mutex);

        if (active) {
            pthread_join(t, NULL);
            pthread_mutex_lock(&pool_mutex);
            pool[i].active = false;
            pool[i].id = -1;
            pthread_mutex_unlock(&pool_mutex);
        }
    }
}