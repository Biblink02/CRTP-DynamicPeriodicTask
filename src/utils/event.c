#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include "event.h"

int event_parse(const char *line, const int client_fd, Event *out_event) {
    char cmd[32] = {0};
    char arg[32] = {0};

    out_event->client_fd = client_fd;
    out_event->type = EV_UNKNOWN;
    memset(&out_event->payload, 0, sizeof(out_event->payload));

    const int tokens = sscanf(line, "%31s %31s", cmd, arg);
    if (tokens < 1) return -1;

    if (strcasecmp(cmd, "ACTIVATE") == 0 || strcasecmp(cmd, "A") == 0) {
        if (tokens < 2) return -1;
        out_event->type = EV_ACTIVATE;
        strncpy(out_event->payload.task_name, arg, TASK_NAME_LEN - 1);
        return 0;
    }

    if (strcasecmp(cmd, "DEACTIVATE") == 0 || strcasecmp(cmd, "D") == 0) {
        if (tokens < 2) return -1;
        char *end;
        long val = strtol(arg, &end, 10);
        if (*end != '\0') return -1;
        out_event->type = EV_DEACTIVATE;
        out_event->payload.target_id = val;
        return 0;
    }

    if (strcasecmp(cmd, "LIST") == 0 || strcasecmp(cmd, "L") == 0) {
        out_event->type = EV_LIST;
        return 0;
    }

    if (strcasecmp(cmd, "INFO") == 0 || strcasecmp(cmd, "I") == 0) {
        out_event->type = EV_INFO;
        return 0;
    }

    if (strcasecmp(cmd, "SHUTDOWN") == 0 || strcasecmp(cmd, "S") == 0) {
        out_event->type = EV_SHUTDOWN;
        return 0;
    }

    return -1;
}
