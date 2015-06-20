#ifndef __MAIN_H_
#define __MAIN_H_

#define FIFO_SZ  100000UL

#include "fifo.h"

typedef struct {
    int sd;
    fd_set read_fd_set;
    fifo_t *fifo;
} pkt_t;

#endif
