#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "task_routines.h"

static unsigned long long loops_per_ms = 0;

static void task_A(void);
static void task_B(void);
static void task_C(void);

static TaskType task_catalog[N_TASKS] = {
    {"t1", 50, 300, 300, task_A},
    {"t2", 100, 500, 500, task_B},
    {"t3", 200, 1000, 1000, task_C}
};

// Use volatile to prevent compiler from optimizing away the loop
static inline void workload(double i) {
    volatile double r = sqrt(i) * 0.001 + sin(i / 1000.0);
    (void)r;
}

static void calibrate(void) {
    struct timespec s, e;
    unsigned long long count = 0;
    const long long target_ns = 100000000; // 100 ms

    printf("[Routines] Calibrating CPU (target: 100ms sample)...\n");
    clock_gettime(CLOCK_MONOTONIC, &s);
    do {
        workload((double)count++);
        clock_gettime(CLOCK_MONOTONIC, &e);
    } while ((e.tv_sec - s.tv_sec) * 1e9 + (e.tv_nsec - s.tv_nsec) < target_ns);

    loops_per_ms = count / 100;
    printf("[Routines] Calibration done: %llu loops/ms\n", loops_per_ms);
}

static void burn(long ms) {
    unsigned long long max = loops_per_ms * ms;
    for (unsigned long long i = 0; i < max; i++) workload((double)i);
}

static void task_A(void) { burn(50); }
static void task_B(void) { burn(100); }
static void task_C(void) { burn(200); }

void routines_init(void) { calibrate(); }

const TaskType *routines_get_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < N_TASKS; i++) {
        if (strcmp(task_catalog[i].name, name) == 0) return &task_catalog[i];
    }
    return NULL;
}

const TaskType *routines_get_all(int *count) {
    if (count) *count = N_TASKS;
    return task_catalog;
}