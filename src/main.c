
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

// thread that feeds the random data into a buffer
void *harvest(void *pkt_ptr)
{
    int fd;
    // random source device buffer 
    // (one cannot read more than 63 bytes at a time from a trueRNGv2 device)
    uint8_t dev_buff[32];
    // buffer sent to the fips check function
    uint8_t rng_buffer[FIPS_RNG_BUFFER_SIZE];
    pkt_t *p_ptr = (pkt_t *) pkt_ptr;
    // context for the FIPS tests
    static fips_ctx_t fipsctx;
    int fips_result;
    int initial_data;

    fd = open("/dev/truerng", O_RDONLY);
    //fd = open("/dev/zero", O_RDONLY);
    rng_read(fd, &dev_buff, 4);
    initial_data =
        dev_buff[0] | (dev_buff[1] << 8) | (dev_buff[2] << 16) | (dev_buff[3] <<
                                                                  24);
    fips_init(&fipsctx, initial_data);

    while (!endofworld) {
        // if there is space in the fifo
        while (p_ptr->fifo->free) {
            rng_read(fd, rng_buffer, sizeof(rng_buffer));
            fips_result = fips_run_rng_test(&fipsctx, &rng_buffer);

            if (fips_result) {
                // fips test failed
                fprintf(stderr, "fips test failed\n");
            } else {
                pthread_mutex_lock(&fifo_mutex);
                if (p_ptr->fifo->free > FIPS_RNG_BUFFER_SIZE) {
                    fifo_push(p_ptr->fifo, rng_buffer, FIPS_RNG_BUFFER_SIZE);
                } else {
                    fifo_push(p_ptr->fifo, rng_buffer, p_ptr->fifo->free);
                }
                pthread_mutex_unlock(&fifo_mutex);
                fprintf(stdout, "%d bytes free\n", (unsigned int) p_ptr->fifo->free);
            }
        }
        // fifo is now full, get some rest
        usleep(200000);
    }

    close(fd);
    return NULL;
}

int main()
{
    pkt_t p;
    pthread_t harvest_thread;

    p.fifo = create_fifo(FIFO_SZ);

    if (pthread_create(&harvest_thread, NULL, harvest, &p)) {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    if (pthread_join(harvest_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return EXIT_FAILURE;
    }

    printf("main process: head=%d\n", (unsigned int) p.fifo->head);
    return EXIT_SUCCESS;
}
