#ifndef __libevent_glue_h__
#define __libevent_glue_h__

struct event_base *evbase;

void libevent_glue(void);
void stop_libevent(struct event_base *base);

#endif

