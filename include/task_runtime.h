#ifndef TASK_RUNTIME_H
#define TASK_RUNTIME_H
#include "task.h"

/**
 * Initializes the thread pool and synchronization primitives.
 * Must be called before creating any instance.
 */
void runtime_init(void);

/**
 * Spawns a new real-time thread for the given task type.
 * Maps the period to a SCHED_FIFO priority.
 * @param type Pointer to the task definition (WCET, Period, etc.).
 * @return The assigned instance ID, or -1 if the pool is full.
 */
int runtime_create_instance(const TaskType *type);

/**
 * Signals a specific task instance to stop and joins its thread.
 * @param id The instance ID to stop.
 * @return 0 on success, -1 if ID is invalid.
 */
int runtime_stop_instance(int id);

/**
 * Retrieves pointers to currently active task instances.
 * @param out_instances Output array to store pointers.
 * @param max_len Maximum number of instances to retrieve.
 * @return The number of active instances found.
 */
int runtime_get_active_instances(TaskInstance **out_instances, int max_len);

/**
 * Stops all active tasks and cleans up thread resources.
 */
void runtime_cleanup(void);
#endif
