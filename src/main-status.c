/*
    for printing the status to the command-line roughly once per second

    the complication is that we cann't afford a "time" check for each
    packet, since it's a system call, so we try to keep a rough
    approximation of when to print a status.

*/
#include "main-status.h"
#include "pixie-timer.h"
#include <stdio.h>
#include <string.h>

#ifndef UNUSEDPARM
#ifdef _MSC_VER
#define UNUSEDPARM(x) x
#else
#define UNUSEDPARM(x)
#endif
#endif

extern time_t global_now;
extern uint64_t global_tcb_count;

/***************************************************************************
 * Print a status message about once-per-second to the command-line. This
 * algorithm is a little funky because checking the timestamp on EVERY
 * packet is slow.
 ***************************************************************************/
void
status_print(struct Status *status, uint64_t count, uint64_t max_count)
{
    double elapsed;
    uint64_t now;

    /*
     * ####  FUGGLY TIME HACK  ####
     *
     * PF_RING doesn't timestamp packets well, so we can't base time from
     * incoming packets. Checking the time ourself is too ugly on per-packet
     * basis. Therefore, we are going to create a global variable that keeps
     * the time, and update that variable whenever it's convienient. This
     * is one of those convenient places.
     */
    global_now = time(0);

    /* speed up or slow down how often we report so that we get about
     * 1-second between reports */
    {
        time_t t = time(0);
        if ((int)t == (int)status->last.time) {
            status->timer <<= 1;
            status->timer |= 1;
        } else {
            status->timer >>= 1;
            status->timer |= 1;
        }
        status->last.time = t;
    }

    /* If nothing's changed, then stop here, because otherwise we'll
     * be dividing by zero or something */
    if (count <= status->last.count)
        return;


    /* Get the time. NOTE: this is CLOCK_MONOTONIC_RAW on Linux, not
     * wall-clock time. */
    now = pixie_gettime();
    elapsed = ((double)now - (double)status->last.clock)/(double)1000000.0;
    if (elapsed == 0)
        return;
    status->last.clock = now;

    /*
     * Print the message to <stderr> so that <stdout> can be redirected
     * to a file (<stdout> reports what systems were found).
     */
    {
        double rate = ((double)(count - status->last.count)*1.0/elapsed);
        double percent_done = (double)(count*100.0/max_count);
        double finished = 0;
        status->last_rates[status->last_count++ & 0x7] = rate;
        rate = status->last_rates[0]
                + status->last_rates[1]
                + status->last_rates[2]
                + status->last_rates[3]
                + status->last_rates[4]
                + status->last_rates[5]
                + status->last_rates[6]
                + status->last_rates[7]
                ;
        rate /= 8;
        if (rate)
            finished  = (1.0 - percent_done/100.0) * (max_count / rate);
        /* (%u-days %02u:%02u:%02u remaining) */
        fprintf(stderr, "rate:%6.2f-kpps, %5.2f%% done, %u:%02u:%02u remaining, %llu-tcbs             \r",
                        rate/1000.0,
                        percent_done,
            (unsigned)(finished/60/60),
            (unsigned)(finished/60)%60,
            (unsigned)(finished)%60,
            global_tcb_count
                        );
        fflush(stderr);
    }

    status->last.count = count;
}

/***************************************************************************
 ***************************************************************************/
void
status_finish(struct Status *status)
{
    UNUSEDPARM(status);
}

/***************************************************************************
 ***************************************************************************/
void
status_start(struct Status *status)
{
    memset(status, 0, sizeof(*status));
    status->last.clock = clock();
    status->last.time = time(0);
    status->last.count = 0;
    status->timer = 0x1;
}
