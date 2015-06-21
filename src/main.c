
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
#include "libevent_glue.h"

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
        usleep(100000);
    }

    fprintf(stderr, "harvest thread exiting\n");

    close(fd);
    return NULL;
}


void parse_options(int argc, char *argv[])
{
    static const char short_options[] = "hed:i:p:m:b:";
    static const struct option long_options[] = {
        {.name = "help",.val = 'h'},
        {.name = "device",.has_arg = 1,.val = 'd'},
        {.name = "ip",.has_arg = 1,.val = 'i'},
        {.name = "port",.has_arg = 1,.val = 'p'},
        {.name = "max-clients",.has_arg = 1,.val = 'm'},
        {.name = "buffer-size",.has_arg = 1,.val = 'b'},
        {.name = "debug",.val = 'e'}
    };
    int option;

    // default values
    rng_device = "/dev/truerng";
    ip = "127.0.0.1";
    port = 41300;
    max_clients = 20;
    fifo_size = 100000;
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
                    "  -d, --device=NAME       block file that outputs random data (default '/dev/truerng')\n"
                    "  -i, --ip=IP             IP used for listening for connections (default '127.0.0.1')\n"
                    "  -p, --port=NUM          port used (default '41300')\n"
                    "  -m, --max-clients=NUM   maximum number of clients accepted (default '20')\n"
                    "  -b, --buffer-size=NUM   buffer size used for storing entropy (default '100000')\n"
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
    pthread_t harvest_thread;

    //if (signal(SIGINT, sig_handler) == SIG_ERR) {
    //    fprintf(stderr, "\ncan't catch SIGINT\n");
    //}
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        fprintf(stderr, "\ncan't catch SIGUSR1\n");
    }

    //fifo_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init(&fifo_mutex, NULL);

    keep_running = 1;

    parse_options(argc, argv);

    fifo = create_fifo(fifo_size);

    // thread that feeds the random data into the buffer
    if (pthread_create(&harvest_thread, NULL, harvest, fifo)) {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    libevent_glue();
    keep_running = 0;

    fprintf(stderr, "main thread exiting\n");

    if (pthread_join(harvest_thread, NULL)) {
        fprintf(stderr, "error joining thread\n");
    }

    free_fifo(fifo);

    pthread_exit(NULL);
    return EXIT_SUCCESS;
}

