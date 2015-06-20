
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
int endofworld = 0;

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

    fd = open("/dev/urandom", O_RDONLY);
    //fd = open("/dev/truerng", O_RDONLY);
    //fd = open("/dev/zero", O_RDONLY);
    rng_read(fd, &dev_buff, 4);
    initial_data =
        dev_buff[0] | (dev_buff[1] << 8) | (dev_buff[2] << 16) | (dev_buff[3] <<
                                                                  24);
    fips_init(&fipsctx, initial_data);

    while (!endofworld) {
        // if there is space in the fifo
        while (fifo->free) {
            rng_read(fd, rng_buffer, sizeof(rng_buffer));
            fips_result = fips_run_rng_test(&fipsctx, &rng_buffer);

            if (fips_result) {
                // fips test failed
                fprintf(stderr, "fips test failed\n");
            } else {
                pthread_mutex_lock(&fifo_mutex);
                if (fifo->free > FIPS_RNG_BUFFER_SIZE) {
                    fifo_push(fifo, rng_buffer, FIPS_RNG_BUFFER_SIZE);
                } else {
                    fifo_push(fifo, rng_buffer, fifo->free);
                }
                pthread_mutex_unlock(&fifo_mutex);
                fprintf(stdout, "%d bytes free\n", (unsigned int)fifo->free);
            }
        }
        // fifo is now full, get some rest
        usleep(10000);
    }

    close(fd);
    return NULL;
}

// thread that feeds entropy to the TCP clients
//void *server(void *param)
int server(void *param)
{
    uint8_t buff_rx[2];
    uint8_t buff_tx[255];

    pkt_t *p = (pkt_t *) param;

    fprintf(stdout, " %d: handled\n", p->sd);
    if ((recv(p->sd, buff_rx, 2, 0) == 2)) {
        fprintf(stdout, " %d: received cmd %02x%02x\n", p->sd, buff_rx[0], buff_rx[1]);

        // EGD protocol
        // see http://egd.sourceforge.net/ for details

        if (buff_rx[0] == 0x00) {
        } else if (buff_rx[0] == 0x01) {
        } else if (buff_rx[0] == 0x02) {
            fprintf(stdout, " %d: r req for %d bytes\n", p->sd, buff_rx[1]);
            // client needs buff_rx[1] bytes of random
            if (buff_rx[1]) {
                pthread_mutex_lock(&fifo_mutex);
                if (p->fifo->size - p->fifo->free > buff_rx[1]) {
                    // there is enough entropy in the fifo buffer
                    fifo_pop(p->fifo, buff_tx, buff_rx[1]);
                    pthread_mutex_unlock(&fifo_mutex);
                } else {
                    pthread_mutex_unlock(&fifo_mutex);
                    // FIXME
                    usleep(200000);
                }
                if ((send(p->sd, buff_tx, buff_rx[1], 0) == buff_rx[1])) {
                    fprintf(stdout, " %d: %d bytes sent ok\n", p->sd, buff_rx[1]);
                }
            }
        } else if (buff_rx[0] == 0x03) {
            // client wants to send us entropy
            // 'fuck off!' to that
        } else if (buff_rx[0] == 0x04) {
            //
        } else {
            fprintf(stdout, " ! bogus packet received\n");
            // bogus packet
        }
    } else {
        close(p->sd);
        fprintf(stdout, " ! recv is invalid for %d\n", p->sd);
        return -1;
    }

    free(p);
    //pthread_exit(0);
    //return NULL;
    return 0;
}

int main()
{
    fifo_t *fifo;
    pthread_t harvest_thread;
    //pthread_t server_thread;

    //struct hostent *ptrh;     // pointer to a host table entry
    //struct protoent *ptrp;      // pointer to a protocol table entry
    //struct sockaddr_in srv_addr;        // structure to hold server's address
    //struct sockaddr_in cl_addr; // structure to hold client's address
    //int sd, sd2;                // socket descriptors
    //int port;                   // protocol port number
    //socklen_t alen;             // length of address

    int opt = 1;
    int master_socket, addrlen, new_socket, client_socket[30], max_clients =
        30, activity, i, sd;
    int max_sd;
    fd_set readfds;

    struct sockaddr_in address;
    //char buffer[1025];

    // initialize the large buffer
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
//type of socket created
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
//bind the socket to localhost port 8888
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
//try to specify maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
//accept the incoming connection
    addrlen = sizeof(address);
    puts("Waiting for connections ...");
    while (TRUE) {
//clear the socket set
        FD_ZERO(&readfds);
//add master socket to set
        FD_SET(master_socket, &readfds);
        max_sd = master_socket;
//add child sockets to set
        for (i = 0; i < max_clients; i++) {
//socket descriptor
            sd = client_socket[i];
//if valid socket descriptor then add to read list
            if (sd > 0)
                FD_SET(sd, &readfds);
//highest file descriptor number, need it for the select function
            if (sd > max_sd)
                max_sd = sd;
        }
//wait for an activity on one of the sockets , timeout is NULL , so wait indefinitely
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            printf(" ! select error\n");
        }
//If something happened on the master socket , then its an incoming connection
        if (FD_ISSET(master_socket, &readfds)) {
            if ((new_socket =
                 accept(master_socket, (struct sockaddr *)&address,
                        (socklen_t *) & addrlen)) < 0) {
                fprintf(stderr, " E accept failed\n");
                exit(EXIT_FAILURE);
            }
//inform user of socket number - used in send and receive commands
            printf
                (" * new connection , socket fd is %d, %s:%d \n",
                 new_socket, inet_ntoa(address.sin_addr),
                 ntohs(address.sin_port));

/*
//send new connection greeting message
            if (send(new_socket, message, strlen(message), 0) !=
                strlen(message)) {
                perror("send");
            }
*/
//add new socket to array of sockets
            for (i = 0; i < max_clients; i++) {
//if position is empty
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    printf(" * adding to list of sockets as %d\n", i);

                    printf(" %d: master IO activity\n", new_socket);

/*
                    pkt_t *p = (pkt_t *) malloc(sizeof(pkt_t));
                    p->sd = new_socket;
                    p->fifo = fifo;
                    //pthread_create(&server_thread, NULL, server, (void *)p);
*/

                    break;
                }
            }
        }
//else its some IO operation on some other socket :)
        for (i = 0; i < max_clients; i++) {
            sd = client_socket[i];
            if (FD_ISSET(sd, &readfds)) {

                printf(" %d: IO activity\n", sd);
                pkt_t *p = (pkt_t *) malloc(sizeof(pkt_t));
                p->sd = sd;
                p->fifo = fifo;
                //pthread_create(&server_thread, NULL, server, (void *)p);
                if (server((void *)p) == -1) {
                    close(sd);
                    client_socket[i] = 0;
                }

/*
//Check if it was for closing , and also read the incoming message
                if ((valread = read(sd, buffer, 1024)) == 0) {
//Somebody disconnected , get his details and print
                    getpeername(sd, (struct sockaddr *)&address,
                                (socklen_t *) & addrlen);
                    printf("Host disconnected , ip %s , port %d \n",
                           inet_ntoa(address.sin_addr),
                           ntohs(address.sin_port));
//Close the socket and mark as 0 in list for reuse
                    close(sd);
                    client_socket[i] = 0;
                } else {
                }
*/
            }
        }
    }

    if (pthread_join(harvest_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return EXIT_FAILURE;
    }
    //printf("main process: head=%d\n", (unsigned int) p.fifo->head);
    return EXIT_SUCCESS;
}
