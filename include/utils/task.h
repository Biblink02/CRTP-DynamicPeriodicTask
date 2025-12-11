#ifndef TASK_H
#define TASK_H
#include <stdbool.h>
#include <bits/pthreadtypes.h>

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
    volatile bool stop;
    bool active;
} TaskInstance;
#endif //TASK_H
