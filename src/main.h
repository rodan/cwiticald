#ifndef __MAIN_H_
#define __MAIN_H_

int fifo_size;

#include "fifo.h"

typedef struct {
    int sd;
    fd_set read_fd_set;
    struct sockaddr_in *addr;
    uint8_t *client_socket;
    fifo_t *fifo;
} pkt_t;

#endif
