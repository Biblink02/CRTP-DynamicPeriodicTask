#ifndef NET_CORE_H
#define NET_CORE_H

/**
 * Initializes server socket, binds port, and sets non-blocking mode.
 * @return 0 on success, -1 on failure.
 */
int net_init(int port);

/**
 * Handles I/O multiplexing (poll). Accepts connections and reads data.
 * Passes complete lines to the event parser.
 */
void net_poll(void);

/**
 * Sends a response to a specific client. Safe against broken pipes.
 */
void net_send_response(int client_fd, const char *msg);

/**
 * Closes all sockets.
 */
void net_cleanup(void);

#endif
