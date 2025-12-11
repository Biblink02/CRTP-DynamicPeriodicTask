#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "event.h"

void supervisor_loop(void);

void supervisor_push_event(Event ev);

#endif
