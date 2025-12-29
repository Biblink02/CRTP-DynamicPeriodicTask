#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
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

    // Set non blocking mode for the socket
    const int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    printf("[Net] Server initialized on port %d\n", port);
    return 0;
}

static void process_buffer(const int fd, char *buffer) {
    buffer[strcspn(buffer, "\r\n")] = '\0';
    if (strlen(buffer) == 0) return;

    printf("[Net] Received from FD %d: '%s'\n", fd, buffer);

    Event ev = {0};
    ev.client_fd = fd;

    if (strncmp(buffer, "ACTIVATE ", 9) == 0) {
        ev.type = EV_ACTIVATE;
        strncpy(ev.payload.task_name, buffer + 9, TASK_NAME_LEN - 1);
        ev.payload.task_name[TASK_NAME_LEN - 1] = '\0';
        supervisor_push_event(ev);
    } else if (strncmp(buffer, "DEACTIVATE ", 11) == 0) {
        char *end;
        const long val = strtol(buffer + 11, &end, 10);
        if (end != buffer + 11) {
            ev.type = EV_DEACTIVATE;
            ev.payload.target_id = val;
            supervisor_push_event(ev);
        } else {
            printf("[Net] Invalid ID format from FD %d\n", fd);
            net_send_response(fd, "ERR Invalid ID\n");
        }
    } else {
        printf("[Net] Unknown command from FD %d\n", fd);
        net_send_response(fd, "ERR Unknown Command\n");
    }
}

void net_poll(void) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    const int new_sock = accept(server_fd, (struct sockaddr *) &addr, &len);

    if (new_sock >= 0) {
        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] == -1) {
                const int flags = fcntl(new_sock, F_GETFL, 0);
                fcntl(new_sock, F_SETFL, flags | O_NONBLOCK);
                client_fds[i] = new_sock;
                added = 1;
                printf("[Net] Client connected on FD %d\n", new_sock);
                break;
            }
        }
        if (!added) {
            printf("[Net] Max clients reached, rejecting connection\n");
            close(new_sock);
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) {
            char buffer[NET_BUFFER_SIZE];
            const ssize_t n = read(client_fds[i], buffer, sizeof(buffer) - 1);

            if (n > 0) {
                buffer[n] = '\0';
                process_buffer(client_fds[i], buffer);
            } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
                printf("[Net] Client disconnected on FD %d\n", client_fds[i]);
                close(client_fds[i]);
                client_fds[i] = -1;
            }
        }
    }
}

void net_send_response(const int client_fd, const char *msg) {
    if (client_fd >= 0) {
        char buf[NET_RESPONSE_BUF_SIZE];
        snprintf(buf, sizeof(buf), "[SERVER]: %s", msg);

        // MSG_NOSIGNAL prevents SIGPIPE if the client closed the connection
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
