
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "main.h"
#include "fifo.h"
#include "fips.h"

pthread_mutex_t fifo_mutex = PTHREAD_MUTEX_INITIALIZER;
int exit_now = 0;

#define TRUE 1
#define FALSE 0
#define PORT 41300

int visits = 0;

static int rng_read(const int fd, void *buf, const size_t size)
{
    size_t off = 0;
    ssize_t r;
    size_t sz = size;

    while (sz) {
        r = read(fd, buf + off, sz);
        if (r < 0) {
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;
            break;
        } else if (!r) {
            fprintf(stderr, "entropy source drained\n");
            return EXIT_FAILURE;
        }
        off += r;
        sz -= r;
    }

    if (sz) {
        fprintf(stderr, "error reading input: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// thread that feeds the random data into the buffer
void *harvest(void *param)
{
    int fd;
    // random source device buffer 
    // (one cannot read more than 63 bytes at a time from a trueRNGv2 device)
    uint8_t dev_buff[32];
    // buffer sent to the fips check function
    uint8_t rng_buffer[FIPS_RNG_BUFFER_SIZE];
    //pkt_t *p_ptr = (pkt_t *) pkt_ptr;
    fifo_t *fifo = (fifo_t *) param;
    // context for the FIPS tests
    static fips_ctx_t fipsctx;
    int fips_result;
    int initial_data;

    //fd = open("/dev/urandom", O_RDONLY);
    fd = open("/dev/truerng", O_RDONLY);
    //fd = open("/dev/vcs5", O_RDONLY);
    //fd = open("/dev/zero", O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "critical error: cannot open RNG device\n");
        exit_now = 1;
        pthread_exit(0);
    }

    rng_read(fd, &dev_buff, 4);
    initial_data =
        dev_buff[0] | (dev_buff[1] << 8) | (dev_buff[2] << 16) | (dev_buff[3] <<
                                                                  24);
    fips_init(&fipsctx, initial_data);

    while ( ! exit_now) {
        // if there is space in the fifo
        while (fifo->free) {
            rng_read(fd, rng_buffer, sizeof(rng_buffer));
            fips_result = fips_run_rng_test(&fipsctx, &rng_buffer);

            if (fips_result) {
                // fips test failed
                //fprintf(stderr, "fips test failed\n");
            } else {
                pthread_mutex_lock(&fifo_mutex);
                if (fifo->free > FIPS_RNG_BUFFER_SIZE) {
                    fifo_push(fifo, rng_buffer, FIPS_RNG_BUFFER_SIZE);
                } else {
                    fifo_push(fifo, rng_buffer, fifo->free);
                }
                pthread_mutex_unlock(&fifo_mutex);
                fprintf(stdout, "%d/%d bytes of entropy in buffer\n", (unsigned int) fifo->size - (unsigned int) fifo->free - 1, (unsigned int)fifo->size - 1);
            }
        }
        // fifo is now full, get some rest
        usleep(10000);
    }

    close(fd);
    return NULL;
}

// thread that feeds entropy to the TCP clients
void *server(void *param)
{
    uint8_t buff_rx[2];
    uint8_t buff_tx[255];

    pkt_t *p = (pkt_t *) param;

    // longest request is 2 bytes long
    if ((recv(p->sd, buff_rx, 2, 0) == 2)) {
        //fprintf(stdout, " %d: received cmd %02x%02x\n", p->sd, buff_rx[0], buff_rx[1]);

        // EGD protocol
        // see http://egd.sourceforge.net/ for details

        if (buff_rx[0] == 0x00) { // get entropy level
        } else if (buff_rx[0] == 0x01) { // read entropy nonblocking
        } else if (buff_rx[0] == 0x02) { // read entropy blocking
            // client requests buff_rx[1] bytes of entropy
            if (buff_rx[1]) {
                pthread_mutex_lock(&fifo_mutex);
                if (p->fifo->size - p->fifo->free > buff_rx[1]) {
                    // there is enough entropy in the fifo buffer
                    fifo_pop(p->fifo, buff_tx, buff_rx[1]);
                    pthread_mutex_unlock(&fifo_mutex);
                    if ((send(p->sd, buff_tx, buff_rx[1], 0) == buff_rx[1])) {
                        fprintf(stdout, "%s sfd%d: %d bytes sent\n", inet_ntoa(p->addr->sin_addr), p->sd, buff_rx[1]);
                    }
                } else {
                    pthread_mutex_unlock(&fifo_mutex);
                    fprintf(stdout, "%s sfd%d: %d bytes requested, but only %d available\n", inet_ntoa(p->addr->sin_addr), p->sd, buff_rx[1], (int) p->fifo->size - (int) p->fifo->free - 1);
                    usleep(200000);
                    // FIXME - maybe client_socket and sd needs to be closed at this point?
                }
            }
        } else if (buff_rx[0] == 0x03) { // write entropy
        } else if (buff_rx[0] == 0x04) { // report PID
        } else {
            fprintf(stdout, "%s sfd%d: bogus packet received\n", inet_ntoa(p->addr->sin_addr), p->sd);
            // bogus packet
        }
    } else {
        fprintf(stdout, "%s sfd%d: connection closed\n", inet_ntoa(p->addr->sin_addr), p->sd);
        *p->client_socket = 0;
        close(p->sd);
    }

    free(p->addr);
    free(p);

    //pthread_exit(0);
    return NULL;
}

int main()
{
    fifo_t *fifo;
    pthread_t harvest_thread;
    //pthread_t server_thread;

    int opt = 1;
    int master_socket, sd, max_sd, addrlen, new_socket;
    int max_clients = 30, activity;
    uint8_t client_socket[30];
    fd_set readfds;
    int i;

    struct sockaddr_in address;

    fifo = create_fifo(FIFO_SZ);

    // thread that feeds the random data into the buffer
    if (pthread_create(&harvest_thread, NULL, harvest, fifo)) {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    //memset((char *)&srv_addr, 0, sizeof(srv_addr));     /* clear sockaddr structure   */
    for (i = 0; i < max_clients; i++) {
        client_socket[i] = 0;
    }

    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt
        (master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt,
         sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    // try to specify maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    // accept the incoming connection
    addrlen = sizeof(address);
    puts("Waiting for connections ...");
    while ( ! exit_now ) {
        FD_ZERO(&readfds);
        FD_SET(master_socket, &readfds);
        max_sd = master_socket;
        // add child sockets to set
        for (i = 0; i < max_clients; i++) {
            // socket descriptor
            sd = client_socket[i];
            // if valid socket descriptor then add to read list
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            // highest file descriptor number, need it for the select function
            if (sd > max_sd) {
                max_sd = sd;
            }
        }
        // wait for activity on one of the sockets , timeout is NULL , so wait indefinitely
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            printf(" ! select error\n");
        }
        // if something happened on the master socket , then its an incoming connection
        if (FD_ISSET(master_socket, &readfds)) {
            if ((new_socket =
                 accept(master_socket, (struct sockaddr *)&address,
                        (socklen_t *) & addrlen)) < 0) {
                fprintf(stderr, " E accept failed\n");
                exit(EXIT_FAILURE);
            }
            // inform user of socket number - used in send and receive commands
            printf
                (" * new connection , socket fd is %d, %s:%d \n",
                 new_socket, inet_ntoa(address.sin_addr),
                 ntohs(address.sin_port));

            // add new socket to array of sockets
            for (i = 0; i < max_clients; i++) {
                // if position is empty
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    //printf(" * adding to list of sockets as %d\n", i);
                    //printf(" %d: master IO activity\n", new_socket);
                    break;
                }
            }
        }
        // else its some IO operation on some other socket :)
        for (i = 0; i < max_clients; i++) {
            sd = client_socket[i];
            if (FD_ISSET(sd, &readfds)) {
                struct sockaddr_in *addr;
                //printf(" %d: IO activity\n", sd);
                pkt_t *p = (pkt_t *) malloc(sizeof(pkt_t));
                addr = (struct sockaddr_in *) malloc(sizeof(struct sockaddr_in));
                memcpy(addr, &address, sizeof(address));
                p->sd = sd;
                p->fifo = fifo;
                p->addr = addr;
                p->client_socket = &client_socket[i];
                //pthread_create(&server_thread, NULL, server, (void *)p);
                server((void *)p);
            }
        }
    }

    if (pthread_join(harvest_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return EXIT_FAILURE;
    }

    free(fifo->buffer);

    return EXIT_SUCCESS;
}

