
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include "main.h"
#include "fifo.h"
#include "fips.h"
#include "libevent_glue.h"

void sig_handler(const int signo)
{
    if (signo == SIGINT) {
        keep_running = 0;
        stop_libevent(evbase);
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

    while (keep_running) {
        // if there is space in the fifo
        if (fifo->free > fifo_size - fifo_trigger) {
            while (fifo->free && keep_running) {
                if (rng_read(fd, rng_buffer, sizeof(rng_buffer)) ==
                    EXIT_SUCCESS) {
                    fips_result = fips_run_rng_test(&fipsctx, &rng_buffer);

                    if (fips_result) {
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
                    }
                } else {
                    // error reading entropy, give the device time to settle
                    close(fd);
                    sleep(1);
                    fd = open(rng_device, O_RDONLY);
                }
                fprintf(stdout, "%8d/%d bytes of entropy available\n",
                        (unsigned int)fifo->size -
                        (unsigned int)fifo->free - 1,
                        (unsigned int)fifo->size - 1);
            }
            // in case fifo_trigger == fifo_size
            //usleep(200000);
        }
        // fifo is now full, get some rest
        usleep(200000);
    }

    fprintf(stderr, "harvest thread exiting\n");

    close(fd);
    return NULL;
}

void parse_options(int argc, char **argv)
{
    static const char short_options[] = "hed:i:I:p:m:b:t:";
    static const struct option long_options[] = {
        {.name = "help",.val = 'h'},
        {.name = "device",.has_arg = 1,.val = 'd'},
        {.name = "ipv4",.has_arg = 1,.val = 'i'},
        {.name = "ipv6",.has_arg = 1,.val = 'I'},
        {.name = "port",.has_arg = 1,.val = 'p'},
        {.name = "max-clients",.has_arg = 1,.val = 'm'},
        {.name = "buffer-size",.has_arg = 1,.val = 'b'},
        {.name = "trigger",.has_arg = 1,.val = 't'},
        {.name = "debug",.val = 'e'},
        {0, 0, 0, 0}
    };
    int option;

    // default values
    rng_device = "/dev/truerng";
    ip4 = "127.0.0.1";
    ip6 = "";
    port = 41300;
    max_clients = 20;
    fifo_size = 500000;
    fifo_trigger = 450000;
    debug = 0;

    while ((option = getopt_long(argc, argv, short_options,
                                 long_options, NULL)) != -1) {
        switch (option) {
        case 'h':
            fprintf(stdout, "Usage: cwiticald [OPTION]\n\n");
            fprintf(stdout,
                    "Mandatory arguments to long options are mandatory for short options too.\n");
            fprintf(stdout,
                    "  -h, --help              this help\n"
                    "  -d, --device=NAME       block file that outputs random data\n"
                    "                               (default '%s')\n"
                    "  -i, --ipv4=IP           IPv4 used for listening for connections\n"
                    "                               (default '%s')\n"
                    "  -i, --ipv6=IP           IPv6 used for listening for connections\n"
                    "                               (default '%s')\n"
                    "  -p, --port=NUM          port used\n"
                    "                               (default '%d')\n"
                    "  -m, --max-clients=NUM   maximum number of clients accepted\n"
                    "                               (default '%d') - not implemented\n"
                    "  -b, --buffer-size=NUM   size in bytes of the buffer used for storing entropy\n"
                    "                               (default '%d')\n"
                    "  -t, --trigger=NUM       at what buffer size should the program trigger entropy reads\n"
                    "                               (default '%d')\n",
                    rng_device, ip4, ip6, port, max_clients, fifo_size,
                    fifo_trigger);
            exit(EXIT_SUCCESS);
            break;
        case 'd':
            rng_device = optarg;
            break;
        case 'i':
            ip4 = optarg;
            break;
        case 'I':
            ip6 = optarg;
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
            if (max_clients < 1 || max_clients > 65535) {
                fprintf(stderr, "invalid max_clients value\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'b':
            fifo_size = atoi(optarg);
            if (fifo_size < 5000) {
                fprintf(stderr, "invalid buffer-size value\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 't':
            fifo_trigger = atoi(optarg);
            if ((fifo_trigger > fifo_size) || (fifo_trigger < 1000)) {
                fprintf(stderr, "invalid trigger value\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'e':
            // not implemented
            break;
        default:
            fprintf(stderr, "unknown option: %c\n", option);
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char **argv)
{
    pthread_t harvest_thread;

    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        fprintf(stderr, "\ncan't catch SIGINT\n");
    }
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        fprintf(stderr, "\ncan't catch SIGUSR1\n");
    }

    pthread_mutex_init(&fifo_mutex, NULL);
    keep_running = 1;
    parse_options(argc, argv);
    fifo = create_fifo(fifo_size);
    setvbuf(stdout, NULL, _IOLBF, 0);

    // thread that feeds the random data into the buffer
    if (pthread_create(&harvest_thread, NULL, harvest, fifo)) {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }
    // detect if there is an immediate error in the harvest thread
    // and in that case don't start the network loop
    usleep(200000);
    // networking loop
    if (keep_running) {
        libevent_glue();
    }
    // set in case libevent_glue return due to any error
    keep_running = 0;

    fprintf(stderr, "main thread exiting\n");

    if (pthread_join(harvest_thread, NULL)) {
        fprintf(stderr, "error joining thread\n");
    }

    free_fifo(fifo);
    pthread_exit(NULL);
    return EXIT_SUCCESS;
}
