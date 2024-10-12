#ifndef __libevent_glue_h__
#define __libevent_glue_h__

#ifdef LIBEVENT_LEVEL
    #define LIBEVENT_EXPORT
#else
    #define LIBEVENT_EXPORT extern 
#endif

LIBEVENT_EXPORT struct event_base *evbase;

int libevent_glue(void);
void stop_libevent(struct event_base *base);

#endif

