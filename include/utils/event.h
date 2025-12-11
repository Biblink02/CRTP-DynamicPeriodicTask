#ifndef EVENT_H
#define EVENT_H
#include "constants.h"

typedef enum {
    EV_ACTIVATE,
    EV_DEACTIVATE,
    EV_SHUTDOWN
} EventType;

typedef struct {
    EventType type;

    union {
        char task_name[TASK_NAME_LEN];
        int target_id;
    } payload;

    int client_fd;
} Event;

static const char *event_type_to_string(const EventType t) {
    switch (t) {
        case EV_ACTIVATE: return "EV_ACTIVATE";
        case EV_DEACTIVATE: return "EV_DEACTIVATE";
        case EV_SHUTDOWN: return "EV_SHUTDOWN";
        default: return "UNKNOWN_EVENT";
    }
}

#endif //EVENT_H
