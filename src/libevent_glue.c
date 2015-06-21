
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>

#include "libevent_glue.h"
#include "main.h"

void read_cb(struct bufferevent *bev, void *ctx)
{
    uint8_t buff_rx[2];
    uint8_t buff_tx[255];

    if (bufferevent_read(bev, &buff_rx, 2) == 2) {

        // EGD protocol
        // see http://egd.sourceforge.net/ for details

        if (buff_rx[0] == 0x00) {       // get entropy level
        } else if (buff_rx[0] == 0x01) {        // read entropy nonblocking
        } else if (buff_rx[0] == 0x02) {        // read entropy blocking
            // client requests buff_rx[1] bytes of entropy
            if (buff_rx[1]) {
                pthread_mutex_lock(&fifo_mutex);
                if (fifo->size - fifo->free > buff_rx[1]) {
                    // there is enough entropy in the fifo buffer
                    fifo_pop(fifo, buff_tx, buff_rx[1]);
                    pthread_mutex_unlock(&fifo_mutex);

                    if ((bufferevent_write(bev, buff_tx, buff_rx[1]) == 0)) {
                        // fprintf(stdout, "%s %d: %d bytes sent\n", inet_ntoa(p->addr->sin_addr), p->sd, buff_rx[1]);
                        fprintf(stdout, "%d bytes sent\n", buff_rx[1]);
                    }
                } else {
                    pthread_mutex_unlock(&fifo_mutex);
                    //fprintf(stdout, "%s %d: %d bytes requested, but only %d available\n", inet_ntoa(p->addr->sin_addr), p->sd, buff_rx[1], (int) p->fifo->size - (int) p->fifo->free - 1);
                    fprintf(stdout,
                            "%d bytes requested, but only %d available\n",
                            buff_rx[1], (int)fifo->size - (int)fifo->free - 1);
                    //usleep(200000);
                }
            }
        } else if (buff_rx[0] == 0x03) {        // write entropy
        } else if (buff_rx[0] == 0x04) {        // report PID
        } else {
            //fprintf(stdout, "%s %d: bogus packet received\n", inet_ntoa(p->addr->sin_addr), p->sd);
            fprintf(stdout, "bogus packet received\n");
        }
    }
}

void error_cb(struct bufferevent *bev, short error, void *ctx)
{
    if (error & BEV_EVENT_EOF) {
        /* connection has been closed, do any clean up here */
        /* ... */
    } else if (error & BEV_EVENT_ERROR) {
        /* check errno to see what error occurred */
        /* ... */
    } else if (error & BEV_EVENT_TIMEOUT) {
        /* must be a timeout event handle, handle it */
        /* ... */
    }
    bufferevent_free(bev);
}

void do_accept(evutil_socket_t listener, short event, void *arg)
{
    struct event_base *base = arg;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listener, (struct sockaddr *)&ss, &slen);
    if (fd < 0) {
        perror("accept");
    } else if (fd > FD_SETSIZE) {
        close(fd);
    } else {
        struct bufferevent *bev;
        evutil_make_socket_nonblocking(fd);
        bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(bev, read_cb, NULL, error_cb, NULL);
        bufferevent_setwatermark(bev, EV_READ, 0, 2);
        bufferevent_enable(bev, EV_READ | EV_WRITE);
    }
}

void libevent_glue(void)
{
    evutil_socket_t listener;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *listener_event;

    base = event_base_new();
    if (!base)
        return;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(ip);
    sin.sin_port = htons(port);
    listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener == 0) {
        perror("socket failed");
        return;
    }
    evutil_make_socket_nonblocking(listener);

#ifndef WIN32
    {
        int one = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
#endif

    if (bind(listener, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind failed");
        return;
    }

    if (listen(listener, 16) < 0) {
        perror("listen failed");
        return;
    }

    listener_event =
        event_new(base, listener, EV_READ | EV_PERSIST, do_accept,
                  (void *)base);
    /*XXX check it */
    event_add(listener_event, NULL);

    event_base_dispatch(base);
}
