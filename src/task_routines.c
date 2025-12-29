#include <stdio.h>
#include <string.h>
#include <time.h>
#include "task_routines.h"
#include "constants.h"

static void burn_cpu(long ms) {
    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        // Busy wait to simulate CPU load
        volatile double x = 1.0;
        for (int i = 0; i < 1000; i++) x *= 1.000001;

        clock_gettime(CLOCK_MONOTONIC, &current);
        long elapsed_ms = (current.tv_sec - start.tv_sec) * 1000 +
                          (current.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= ms) break;
    }
}

static void task_func_A(void) { burn_cpu(50); }
static void task_func_B(void) { burn_cpu(100); }
static void task_func_C(void) { burn_cpu(200); }

static TaskType task_catalog[N_TASKS] = {
    {"t1", 50, 300, 300, task_func_A},
    {"t2", 100, 500, 500, task_func_B},
    {"t3", 200, 1000, 1000, task_func_C}
};

void routines_init(void) {
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
