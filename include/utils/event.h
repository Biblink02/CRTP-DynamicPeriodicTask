#ifndef EVENT_H
#define EVENT_H

#include "constants.h"

typedef enum {
    EV_UNKNOWN = 0,
    EV_ACTIVATE,
    EV_DEACTIVATE,
    EV_LIST,
    EV_INFO,
    EV_SHUTDOWN
} EventType;

typedef struct {
    EventType type;

    union {
        char task_name[TASK_NAME_LEN];
        long target_id;
    } payload;

    int client_fd;
} Event;

/**
 * Parses a raw command string into an Event structure.
 * @param line The raw string received from the network.
 * @param client_fd The file descriptor of the client sending the command.
 * @param out_event Pointer to store the result.
 * @return 0 on success, -1 on parsing error (out_event type set to EV_UNKNOWN).
 */
int event_parse(const char *line, int client_fd, Event *out_event);

#endif
