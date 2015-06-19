
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
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

void *serverthread(void *parm);

pthread_mutex_t mut;

#define PROTOPORT         2002
#define QLEN              6

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
void *harvest(void *f)
{
    int fd;
    // random source device buffer 
    // (one cannot read more than 63 bytes at a time from a trueRNGv2 device)
    uint8_t dev_buff[32];
    // buffer sent to the fips check function
    uint8_t rng_buffer[FIPS_RNG_BUFFER_SIZE];
    //pkt_t *p_ptr = (pkt_t *) pkt_ptr;
    fifo_t *fifo = (fifo_t *) f;
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
                fprintf(stdout, "%d bytes free\n",
                        (unsigned int)fifo->free);
            }
        }
        // fifo is now full, get some rest
        usleep(10000);
    }

    close(fd);
    return NULL;
}

int main()
{
    //pkt_t *p;
    fifo_t *fifo;
    pthread_t harvest_thread;

    struct hostent *ptrh;       /* pointer to a host table entry */
    struct protoent *ptrp;      /* pointer to a protocol table entry */
    struct sockaddr_in srv_addr;     /* structure to hold server's address */
    struct sockaddr_in cl_addr;     /* structure to hold client's address */
    int sd, sd2;                /* socket descriptors */
    int port;                   /* protocol port number */
    int alen;                   /* length of address */
    pthread_t tid;              /* variable to hold thread ID */

    // initialize the large buffer
    fifo = create_fifo(FIFO_SZ);

    // thread that feeds the random data into the buffer
    if (pthread_create(&harvest_thread, NULL, harvest, fifo)) {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    pthread_mutex_init(&mut, NULL);
    memset((char *)&srv_addr, 0, sizeof(srv_addr));       /* clear sockaddr structure   */
    srv_addr.sin_family = AF_INET;   /* set family to Internet     */
    srv_addr.sin_addr.s_addr = INADDR_ANY;   /* set the local IP address */

    /* Check  command-line argument for protocol port and extract      */
    /* port number if one is specfied.  Otherwise, use the default     */
    /* port value given by constant PROTOPORT                          */

    port = PROTOPORT;           /* use default port number   */
    srv_addr.sin_port = htons((u_short) port);

    /* Map TCP transport protocol name to protocol number */
    if (((int)(ptrp = getprotobyname("tcp"))) == 0) {
        fprintf(stderr, "cannot map \"tcp\" to protocol number");
        exit(1);
    }

    /* Create a socket */
    sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
    if (sd < 0) {
        fprintf(stderr, "socket creation failed\n");
        exit(1);
    }

    /* Bind a local address to the socket */
    if (bind(sd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        fprintf(stderr, "bind failed\n");
        exit(1);
    }

    /* Specify a size of request queue */
    if (listen(sd, QLEN) < 0) {
        fprintf(stderr, "listen failed\n");
        exit(1);
    }

    alen = sizeof(cl_addr);

    /* Main server loop - accept and handle requests */
    fprintf(stderr, "Server up and running.\n");
    while (1) {

        printf("SERVER: Waiting for contact ...\n");

        if ((sd2 = accept(sd, (struct sockaddr *)&cl_addr, &alen)) < 0) {
            fprintf(stderr, "accept failed\n");
            exit(1);
        }

        pkt_t *p = (pkt_t *)malloc(sizeof(pkt_t));
        p->sd = sd2;
        p->fifo = fifo;
        pthread_create(&tid, NULL, serverthread, (void *)p);
    }
    close(sd);

    if (pthread_join(harvest_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return EXIT_FAILURE;
    }
    //printf("main process: head=%d\n", (unsigned int) p.fifo->head);
    return EXIT_SUCCESS;
}

void *serverthread(void *parm)
{
    int tvisits;
    char buf[100];              /* buffer for string the server sends */
    uint8_t rng_out[255];
    //int tsd;

    //tsd = (int)parm;
   
    pkt_t *p = (pkt_t *) parm;

    pthread_mutex_lock(&mut);
    tvisits = ++visits;
    pthread_mutex_unlock(&mut);

    //pthread_mutex_lock(&fifo_mutex);
    //if (p_ptr->fifo->free > 255) {
        fifo_pop(p->fifo, rng_out, 255);
    //} else {
    //    fifo_push(p_ptr->fifo, rng_buffer, p_ptr->fifo->free);
    //}
    //pthread_mutex_unlock(&fifo_mutex);

    sprintf(buf, "size is %d\n", p->fifo->free);

    printf("SERVER thread: %s", buf);
    send(p->sd, buf, strlen(buf), 0);
    close(p->sd);
    //free(p);
    pthread_exit(0);
}

