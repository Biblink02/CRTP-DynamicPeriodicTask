#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "event.h"

/**
 * Initializes the supervisor queue and synchronization primitives.
 * Must be called before starting the supervisor loop or pushing events.
 */
void supervisor_init(void);

/**
 * Main loop. Initializes subsystems and processes the event queue.
 */
void supervisor_loop(void);

/**
 * Thread-safe queue insertion.
 */
int supervisor_push_event(Event ev);

#endif
