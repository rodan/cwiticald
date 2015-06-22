#ifndef __MAIN_H_
#define __MAIN_H_

#include "fifo.h"

int fifo_size;
fifo_t *fifo;
pthread_mutex_t fifo_mutex;
static volatile int keep_running;


///////////////////////////////
// default values that can be changed via 
// command line arguments
//

int debug;
int max_clients;
int port;
char *ip4;
char *ip6;
char *rng_device;
///////////////////////////////

typedef struct {
    int sd;
    fd_set read_fd_set;
    struct sockaddr_in *addr;
    uint8_t *client_socket;
    fifo_t *fifo;
} pkt_t;

#endif
