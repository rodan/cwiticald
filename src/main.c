
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include "main.h"
#include "fifo.h"
#include "fips.h"

pthread_mutex_t fifo_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int keep_running = 1;


///////////////////////////////
// default values that can be changed via 
// command line arguments
//

int debug = 0;
int max_clients = 20;
int port = 41300;
char *ip = "127.0.0.1";
char *rng_device = "/dev/truerng";
///////////////////////////////


void sig_handler(int signo)
{
    if (signo == SIGINT) {
        fprintf(stderr, " exiting ...\n");
        keep_running = 0;
    } else if (signo == SIGUSR1) {
        // show some stats
    }
}

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

    fd = open(rng_device, O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "critical error: cannot open RNG device\n");
        keep_running = 0;
        pthread_exit(0);
    }

    rng_read(fd, &dev_buff, 4);
    initial_data =
        dev_buff[0] | (dev_buff[1] << 8) | (dev_buff[2] << 16) | (dev_buff[3] <<
                                                                  24);
    fips_init(&fipsctx, initial_data);

    while ( keep_running ) {
        // if there is space in the fifo
        while ( fifo->free && keep_running ) {
            rng_read(fd, rng_buffer, sizeof(rng_buffer));
            fips_result = fips_run_rng_test(&fipsctx, &rng_buffer);

            if (fips_result) {
                // fips test failed
                fprintf(stderr, "fips test failed\n");
                sleep(1);
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

    fprintf(stderr, "harvest thread exiting\n");

    close(fd);
    return NULL;
}

// thread that feeds entropy to the TCP clients
void *server(void *param)
{
    uint8_t buff_rx[2];
    uint8_t buff_tx[255];

    pkt_t *p = (pkt_t *) param;

    if (fcntl(p->sd, F_SETFL, fcntl(p->sd, F_GETFL) | O_NONBLOCK) < 0) {
        fprintf(stderr, "fcntl returned %s\n", strerror(errno));
    }
    // longest request is 2 bytes long
    if ((recv(p->sd, buff_rx, 2, MSG_DONTWAIT) == 2)) {
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
                        fprintf(stdout, "%s %d: %d bytes sent\n", inet_ntoa(p->addr->sin_addr), p->sd, buff_rx[1]);
                    }
                } else {
                    pthread_mutex_unlock(&fifo_mutex);
                    fprintf(stdout, "%s %d: %d bytes requested, but only %d available\n", inet_ntoa(p->addr->sin_addr), p->sd, buff_rx[1], (int) p->fifo->size - (int) p->fifo->free - 1);
                    usleep(200000);
                    // FIXME - maybe client_socket and sd needs to be closed at this point?
                }
            }
        } else if (buff_rx[0] == 0x03) { // write entropy
        } else if (buff_rx[0] == 0x04) { // report PID
        } else {
            fprintf(stdout, "%s %d: bogus packet received\n", inet_ntoa(p->addr->sin_addr), p->sd);
            // bogus packet
        }
    } else {
        fprintf(stdout, "%s %d: connection closed\n", inet_ntoa(p->addr->sin_addr), p->sd);
        *p->client_socket = 0;
        close(p->sd);
    }

    free(p->addr);
    free(p);

    //pthread_exit(0);
    return NULL;
}

void parse_options(int argc, char *argv[])
{
    static const char short_options[] = "hed:i:p:m:";
    static const struct option long_options[] = {
        {.name = "help",.val = '?'},
        {.name = "device",.has_arg = 1,.val = 'd'},
        {.name = "ip",.has_arg = 1,.val = 'i'},
        {.name = "port",.has_arg = 1,.val = 'p'},
        {.name = "max-clients",.has_arg = 1,.val = 'm'},
        {.name = "debug",.val = 'e'}
    };
    int option;

    while ((option = getopt_long(argc, argv, short_options,
                                 long_options, NULL)) != -1) {
        switch (option) {
        case 'h':
            fprintf(stdout, "Usage: cwiticald [OPTION]\n\n");
            fprintf(stdout,
                    "Mandatory arguments to long options are mandatory for short options too.\n");
            fprintf(stdout,
                    "  -h, --help              this help\n"
                    "  -d, --device=NAME       block file that outputs random data (default '/dev/truerng')\n"
                    "  -i, --ip=IP             IP used for listening for connections (default '127.0.0.1')\n"
                    "  -p, --port=NUM          port used (default '41300')\n"
                    "  -m, --max-clients=NUM   maximum number of clients accepted (default '20')\n"
                    "  -e, --debug             output extra info\n");
            exit(EXIT_SUCCESS);
            break;
        case 'd':
            rng_device = optarg;
            break;
        case 'i':
            ip = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            if (port < 1 || port > 65535) {
                fprintf(stderr, "invalid port value\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'm':
            max_clients = atoi(optarg);
            if (port < 1 || port > 65535) {
                fprintf(stderr, "invalid max_clients value\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'e':
            break;
        default:
            fprintf(stderr, "unknown option: %c\n", option);
            exit(EXIT_FAILURE);
        }
    }
}


int main(int argc, char *argv[])
{
    fifo_t *fifo;
    pthread_t harvest_thread;
    //pthread_t server_thread;

    int opt = 1;
    int master_socket, sd, max_sd, addrlen, new_socket;
    int activity;
    uint8_t *client_socket;
    fd_set readfds;
    int i;

    struct sockaddr_in address;

    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        fprintf(stderr, "\ncan't catch SIGINT\n");
    }
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        fprintf(stderr, "\ncan't catch SIGUSR1\n");
    }

    parse_options(argc, argv);

    fifo = create_fifo(FIFO_SZ);

    // thread that feeds the random data into the buffer
    if (pthread_create(&harvest_thread, NULL, harvest, fifo)) {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    client_socket = (uint8_t *)malloc(max_clients * sizeof(uint8_t));
    memset((uint8_t *)client_socket, 0, max_clients * sizeof(uint8_t));

    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return EXIT_FAILURE;
    }

    if (setsockopt
        (master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt,
         sizeof(opt)) < 0) {
        perror("setsockopt");
        return EXIT_FAILURE;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip);
    address.sin_port = htons(port);

    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return EXIT_FAILURE;
    }
    // try to specify maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        return EXIT_FAILURE;
    }
    // accept the incoming connection
    addrlen = sizeof(address);

    while ( keep_running ) {
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
                return EXIT_FAILURE;
            }
            // inform user of socket number - used in send and receive commands
            //printf (" * new connection , socket fd is %d, %s:%d \n",
            //     new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

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

                if (fcntl(sd, F_SETFL, fcntl(sd, F_GETFL) | O_NONBLOCK) < 0) {
                    fprintf(stderr, "fcntl returned %s\n", strerror(errno));
                }

                //pthread_create(&server_thread, NULL, server, (void *)p);
                server((void *)p);
            }
        }
    }

    fprintf(stderr, "main thread exiting\n");

    if (pthread_join(harvest_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
    }

    free(fifo->buffer);
    free(client_socket);

    return EXIT_SUCCESS;
}

