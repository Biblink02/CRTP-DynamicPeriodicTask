#include <stdio.h>
#include <string.h>
#include <time.h>
#include "task_routines.h"
#include "constants.h"

static unsigned long long loops_per_ms = 0;

static void task_func_A(void);
static void task_func_B(void);
static void task_func_C(void);

static TaskType task_catalog[N_TASKS] = {
    {"t1", 50, 300, 300, task_func_A},
    {"t2", 100, 500, 500, task_func_B},
    {"t3", 200, 1000, 1000, task_func_C}
};

/*
 * Calculates the number of empty loop iterations required to consume approx. 1ms of CPU time.
 * This calibration is necessary because loop execution speed varies based on hardware
 * and compiler optimizations (-O3).
 */
static void calibrate_cpu(void) {
    struct timespec start, end;
    unsigned long long count = 0;
    const int calibration_time_ns = 100000000; // 100 ms

    clock_gettime(CLOCK_MONOTONIC, &start);
    while (1) {
        // Inline assembly prevents the compiler from optimizing away the busy-wait loop
        __asm__ volatile ("" : "+g" (count) : : );
        count++;

        clock_gettime(CLOCK_MONOTONIC, &end);
        long long diff = (end.tv_sec - start.tv_sec) * 1000000000LL +
                         (end.tv_nsec - start.tv_nsec);
        if (diff >= calibration_time_ns) break;
    }

    loops_per_ms = count / 100;
    printf("[Routines] Calibration done: %llu loops per ms\n", loops_per_ms);
}

/*
 * Simulates CPU load by busy-waiting for the specified duration.
 * This mimics the Worst-Case Execution Time (WCET) of a real-time task.
 */
static void burn_cpu(long ms) {
    unsigned long long total_loops = loops_per_ms * ms;
    unsigned long long i;
    for (i = 0; i < total_loops; i++) {
        __asm__ volatile ("" : "+g" (i) : : );
    }
}

static void task_func_A(void) { burn_cpu(50); }
static void task_func_B(void) { burn_cpu(100); }
static void task_func_C(void) { burn_cpu(200); }

void routines_init(void) {
    calibrate_cpu();
    printf("[Routines] Initializing task catalog with %d tasks\n", N_TASKS);
    for (int i = 0; i < N_TASKS; i++) {
        printf("  - %s: C=%ld, T=%ld, D=%ld\n",
               task_catalog[i].name, task_catalog[i].wcet_ms,
               task_catalog[i].period_ms, task_catalog[i].deadline_ms);
    }
}

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