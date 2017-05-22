#ifndef PROTO_H
#define PROTO_H
#include <stdbool.h>

/* called when any(!) app socket is closed */
typedef void (*proto_close_cb_t)(void);

int proto_server_init(unsigned port);
void proto_client_init(proto_close_cb_t close_cb);
int proto_client_add(int fd, bool crlf);
void proto_cond_close(void);

#endif
