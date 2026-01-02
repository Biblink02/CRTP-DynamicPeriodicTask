#ifndef TASK_ROUTINES_H
#define TASK_ROUTINES_H
#include "task.h"

/**
 * Performs CPU calibration to determine loops_per_ms.
 * Initializes the static task catalog.
 */
void routines_init(void);

/**
 * Look up a task definition by its name.
 * @param name The name string (e.g., "t1").
 * @return Pointer to TaskType or NULL if not found.
 */
const TaskType *routines_get_by_name(const char *name);

/**
 * Retrieves the entire catalog of available tasks.
 * @param count Output pointer for the number of tasks.
 * @return Array of TaskType.
 */
const TaskType *routines_get_all(int *count);

#endif
