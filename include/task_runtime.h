#ifndef TASK_RUNTIME_H
#define TASK_RUNTIME_H
#include "task.h"

void runtime_init(void);

int runtime_create_instance(const TaskType *type);

int runtime_stop_instance(int id);

int runtime_get_active_instances(TaskInstance **out_instances, int max_len);

#endif
