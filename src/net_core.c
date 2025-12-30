#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include "net_core.h"
#include "supervisor.h"
#include "event.h"
#include "constants.h"

static int server_fd = -1;
static int client_fds[MAX_CLIENTS];

int net_init(const int port) {
    for (int i = 0; i < MAX_CLIENTS; i++) client_fds[i] = -1;

    struct sockaddr_in address;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Net] Socket creation failed");
        return -1;
    }

    const int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        perror("[Net] Bind failed");
        return -1;
    }
    if (listen(server_fd, BACKLOG_SIZE) < 0) {
        perror("[Net] Listen failed");
        return -1;
    }

    /* Set non-blocking mode to handle multiple clients in a single thread */
    const int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    printf("[Net] Server initialized on port %d\n", port);
    return 0;
}

static void process_buffer(const int fd, char *buffer) {
    // Strip buffer delimiters
    buffer[strcspn(buffer, "\r\n")] = '\0';
    if (strlen(buffer) == 0) return;

    printf("[Net] Received from FD %d: '%s'\n", fd, buffer);

    Event ev = {0};
    ev.client_fd = fd;

    char cmd[32] = {0};
    char arg[32] = {0};

    // Safe parsing of command and argument
    int tokens = sscanf(buffer, "%31s %31s", cmd, arg);

    if (tokens < 1) return;

    if (strcasecmp(cmd, "ACTIVATE") == 0 || strcasecmp(cmd, "A") == 0) {
        if (tokens < 2) {
            net_send_response(fd, "ERR Missing Task Name\n");
            return;
        }
        ev.type = EV_ACTIVATE;
        strncpy(ev.payload.task_name, arg, TASK_NAME_LEN - 1);
        supervisor_push_event(ev);
    }
    else if (strcasecmp(cmd, "DEACTIVATE") == 0 || strcasecmp(cmd, "D") == 0) {
        if (tokens < 2) {
            net_send_response(fd, "ERR Missing ID\n");
            return;
        }
        char *end;
        long val = strtol(arg, &end, 10);
        if (*end == '\0') {
            ev.type = EV_DEACTIVATE;
            ev.payload.target_id = val;
            supervisor_push_event(ev);
        } else {
            net_send_response(fd, "ERR Invalid ID Format\n");
        }
    } else if (strcasecmp(cmd, "SHUTDOWN") == 0 || strcasecmp(cmd, "S") == 0) {
        net_send_response(fd, "OK Shutting Down\n");
        ev.type = EV_SHUTDOWN;
        supervisor_push_event(ev);
    }
    else {
        net_send_response(fd, "ERR Unknown Command\n");
    }
}

void net_poll(void) {
    /* * Priority Strategy: Process existing clients BEFORE accepting new ones.
     * This prevents connection flooding from starving active sessions and allows
     * immediate slot reclamation when a client disconnects.
     */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) {
            char buffer[NET_BUFFER_SIZE];
            const ssize_t n = recv(client_fds[i], buffer, sizeof(buffer) - 1, MSG_DONTWAIT);

            if (n > 0) {
                buffer[n] = '\0';
                process_buffer(client_fds[i], buffer);
            } else if (n == 0) {
                close(client_fds[i]);
                client_fds[i] = -1;
            } else {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    printf("[Net] Error on FD %d: %s\n", client_fds[i], strerror(errno));
                    close(client_fds[i]);
                    client_fds[i] = -1;
                }
            }
        }
    }

    // Accept pending connections from backlog
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int new_sock;

    while ((new_sock = accept(server_fd, (struct sockaddr *) &addr, &len)) >= 0) {
        int flags = fcntl(new_sock, F_GETFL, 0);
        fcntl(new_sock, F_SETFL, flags | O_NONBLOCK);

        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] == -1) {
                client_fds[i] = new_sock;
                added = 1;
                break;
            }
        }

        if (!added) {
            // Reject if max capacity reached to maintain deterministic load
            close(new_sock);
        }
    }
}

void net_send_response(const int client_fd, const char *msg) {
    if (client_fd >= 0) {
        char buf[NET_RESPONSE_BUF_SIZE];
        snprintf(buf, sizeof(buf), "[SERVER]: %s", msg);

        // MSG_NOSIGNAL prevents SIGPIPE crash if the client closed abruptly
        ssize_t sent = send(client_fd, buf, strlen(buf), MSG_NOSIGNAL);

        char log_msg[128];
        strncpy(log_msg, buf, sizeof(log_msg) - 1);
        log_msg[sizeof(log_msg) - 1] = '\0';
        log_msg[strcspn(log_msg, "\r\n")] = '\0';

        if (sent < 0 && errno == EPIPE) {
            printf("[Net] Warning: Client FD %d closed before response could be sent.\n", client_fd);
        } else {
            printf("[Net] Sent to FD %d: '%s'\n", client_fd, log_msg);
        }
    }
}

void net_cleanup(void) {
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) {
            close(client_fds[i]);
            client_fds[i] = -1;
        }
    }
    printf("[Net] Cleanup complete. Sockets closed.\n");
}