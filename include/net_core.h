#ifndef NET_CORE_H
#define NET_CORE_H

int net_init(int port);

void net_poll(void);

void net_send_response(int client_fd, const char *msg);

void net_cleanup(void);

#endif
