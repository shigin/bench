#ifdef HAVE_PATHS_H
#include <paths.h>
#else
/*
 * #include <paths.h> - sun os hasn't got paths.h and won't compile 
 * if I include one.
 * 
 * I don't think _PATH_DEVNULL is usefull define, but who knows
 */
#define _PATH_DEVNULL "/dev/null"
#endif

#ifndef timersub
static void timersub(struct timeval *a, struct timeval *b, struct timeval *r) {
    r->tv_sec = a->tv_sec - b->tv_sec;
    r->tv_usec = a->tv_usec - b->tv_usec;
    if (r->tv_usec < 0) {
        r->tv_sec -= 1;
        r->tv_usec += 1000*1000;
    }
}
#endif
