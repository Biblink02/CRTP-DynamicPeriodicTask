#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include "net_core.h"
#include "supervisor.h"
#include "event.h"

static struct pollfd poll_fds[MAX_CLIENTS + 1];
static char client_buffers[MAX_CLIENTS + 1][NET_BUFFER_SIZE];
static int client_buf_lens[MAX_CLIENTS + 1];

int net_init(const int port) {
    for (int i = 0; i <= MAX_CLIENTS; i++) {
        poll_fds[i].fd = -1;
        poll_fds[i].events = 0;
        client_buf_lens[i] = 0;
        memset(client_buffers[i], 0, NET_BUFFER_SIZE);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    const int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG_SIZE) < 0) {
        close(server_fd);
        return -1;
    }

    // Set non-blocking to prevent accept/recv from freezing the main loop
    fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL, 0) | O_NONBLOCK);

    poll_fds[0].fd = server_fd;
    poll_fds[0].events = POLLIN;

    printf("[Net] Server listening on port %d\n", port);
    return 0;
}

static void handle_line(int fd, char *line) {
    line[strcspn(line, "\r\n")] = '\0';
    if (strlen(line) == 0) return;

    Event ev;
    if (event_parse(line, fd, &ev) == 0) {
        if (ev.type == EV_SHUTDOWN) {
            net_send_response(fd, "OK Shutting Down\n");
        }

        // Push to supervisor queue. If full, reject immediately to prevent timeout.
        if (supervisor_push_event(ev) != 0) {
            net_send_response(fd, "ERR System Busy\n");
        }
    } else {
        net_send_response(fd, "ERR Invalid Command\n");
    }
}

void net_poll(void) {
    int ret = poll(poll_fds, MAX_CLIENTS + 1, 100);
    if (ret <= 0) return;

    // Accept new connections
    if (poll_fds[0].revents & POLLIN) {
        int new_sock = accept(poll_fds[0].fd, NULL, NULL);
        if (new_sock >= 0) {
            fcntl(new_sock, F_SETFL, fcntl(new_sock, F_GETFL, 0) | O_NONBLOCK);
            int added = 0;
            for (int i = 1; i <= MAX_CLIENTS; i++) {
                if (poll_fds[i].fd == -1) {
                    poll_fds[i].fd = new_sock;
                    poll_fds[i].events = POLLIN;
                    client_buf_lens[i] = 0;
                    added = 1;
                    printf("[Net] Client connected on FD %d\n", new_sock);
                    break;
                }
            }
            if (!added) {
                printf("[Net] Max clients reached, rejecting FD %d\n", new_sock);
                close(new_sock);
            }
        }
    }

    // Handle data from clients
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        if (poll_fds[i].fd == -1) continue;

        if (poll_fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
            char temp_buf[NET_BUFFER_SIZE];
            ssize_t n = recv(poll_fds[i].fd, temp_buf, sizeof(temp_buf), 0);

            if (n > 0) {
                // Buffer Overflow Protection
                if (client_buf_lens[i] + n < NET_BUFFER_SIZE) {
                    memcpy(client_buffers[i] + client_buf_lens[i], temp_buf, n);
                    client_buf_lens[i] += n;
                    client_buffers[i][client_buf_lens[i]] = '\0';

                    // Process all complete lines (TCP fragmentation handling)
                    char *line_start = client_buffers[i];
                    char *newline_ptr;
                    while ((newline_ptr = strchr(line_start, '\n')) != NULL) {
                        *newline_ptr = '\0';
                        handle_line(poll_fds[i].fd, line_start);
                        line_start = newline_ptr + 1;
                    }

                    // Move remaining partial data to start of buffer
                    long remaining = client_buf_lens[i] - (line_start - client_buffers[i]);
                    if (remaining > 0) memmove(client_buffers[i], line_start, remaining);
                    client_buf_lens[i] = (int) remaining;
                } else {
                    client_buf_lens[i] = 0; // Reset buffer on overflow
                    net_send_response(poll_fds[i].fd, "ERR Buffer Overflow\n");
                    printf("[Net] Buffer overflow on FD %d. Dropped data.\n", poll_fds[i].fd);
                }
            } else {
                printf("[Net] Client FD %d disconnected\n", poll_fds[i].fd);
                close(poll_fds[i].fd);
                poll_fds[i].fd = -1;
                client_buf_lens[i] = 0;
            }
        }
    }
}

void net_send_response(const int client_fd, const char *msg) {
    if (client_fd >= 0) {
        char buf[NET_RESPONSE_BUF_SIZE];
        snprintf(buf, sizeof(buf), "[SERVER]: %s", msg);
        send(client_fd, buf, strlen(buf), MSG_NOSIGNAL);
    }
}

void net_cleanup(void) {
    for (int i = 0; i <= MAX_CLIENTS; i++) {
        if (poll_fds[i].fd != -1) {
            close(poll_fds[i].fd);
            poll_fds[i].fd = -1;
        }
    }
}
