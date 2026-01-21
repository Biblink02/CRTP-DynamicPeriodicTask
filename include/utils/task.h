#ifndef TASK_H
#define TASK_H

#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#include "constants.h"

typedef struct {
    char name[TASK_NAME_LEN];
    long wcet_ms;
    long period_ms;
    long deadline_ms;

    void (*routine_fn)(void);
} TaskType;

typedef struct {
    int id;
    pthread_t thread;
    const TaskType *type;
    atomic_bool stop;
    bool active;
} TaskInstance;

#endif
