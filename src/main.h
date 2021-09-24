#ifndef __MAIN_H_
#define __MAIN_H_

#include "fifo.h"

#ifdef MAIN_LEVEL
    #define MAIN_EXPORT
#else
    #define MAIN_EXPORT extern 
#endif

///////////////////////////////
// default values that can be changed via 
// command line arguments
//

MAIN_EXPORT int debug;
MAIN_EXPORT int max_clients;
MAIN_EXPORT int fifo_trigger;
MAIN_EXPORT int port;
MAIN_EXPORT char *ip4;
MAIN_EXPORT char *ip6;
MAIN_EXPORT char *rng_device;

MAIN_EXPORT fifo_t *fifo;
MAIN_EXPORT pthread_mutex_t fifo_mutex;

typedef struct {
    int sd;
    fd_set read_fd_set;
    struct sockaddr_in *addr;
    uint8_t *client_socket;
    fifo_t *fifo;
} pkt_t;

#endif
