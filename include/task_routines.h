#ifndef TASK_ROUTINES_H
#define TASK_ROUTINES_H
#include "task.h"

void routines_init(void);

const TaskType *routines_get_by_name(const char *name);

const TaskType *routines_get_all(int *count);

#endif
