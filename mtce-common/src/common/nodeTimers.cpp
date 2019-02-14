/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance "Timer Facility"
 * Implementation
 */

/**
 * @detail
 * Detailed description ...
 *
 * Common timer struct
 *
 */

#include "daemon_common.h"
#include "nodeBase.h"
#include "nodeTimers.h"

static int timer_count = 0 ;

int _timer_start ( struct mtc_timer * mtcTimer_ptr,
                   void (*handler)(int, siginfo_t*, void*),
                   int secs, int msec )

{
    int rc = PASS ;

    if ( mtcTimer_ptr == NULL)
    {
        return (FAIL_NULL_POINTER);
    }

    mtcTimer_ptr->mutex = true ;
    mtcTimer_ptr->ring  = true ; /* default to rung for failure path cases */

    /* Avoid programming mistake that leads to over-writing
     * a seemingly active timer; if .tid is not null then
     * cancel that timr first */
    if (( mtcTimer_ptr->tid ) && ( timer_count > 0 ))
    {
        wlog ("%s (%s) called with active timer ; stopping first \n",
                  mtcTimer_ptr->hostname.c_str(),
                  mtcTimer_ptr->service.c_str());
        mtcTimer_stop ( mtcTimer_ptr );
    }

    if ((  handler == NULL ) ||
        (( secs == 0 ) && ( msec == 0 )) ||
        ( secs > MAX_TIMER_DURATION ))
    {
        elog ("%s (%s) Invalid Duration (%d:%d)\n",
                  mtcTimer_ptr->hostname.c_str(),
                  mtcTimer_ptr->service.c_str(),
                  secs, msec );

        rc = FAIL_BAD_PARM ;
        goto _timer_start_out ;
    }

    /* Clean the timer struct */
    memset ( &mtcTimer_ptr->sev,   0, sizeof(struct sigevent));
    memset ( &mtcTimer_ptr->value, 0, sizeof(struct itimerspec));
    memset ( &mtcTimer_ptr->sa,    0, sizeof(struct sigaction));

    /* Setup the timer */
    mtcTimer_ptr->sa.sa_flags = SA_SIGINFO;
    mtcTimer_ptr->sa.sa_sigaction = handler;
    sigemptyset(&mtcTimer_ptr->sa.sa_mask);
    if (sigaction(SIGRTMIN, &mtcTimer_ptr->sa, NULL) == -1)
    {
        elog ("%s (%s) Timer 'set action' (sigaction) failed\n",
                  mtcTimer_ptr->hostname.c_str(),
                  mtcTimer_ptr->service.c_str());

        rc = FAIL_TIMER_SET_ACTION ;
        goto _timer_start_out ;

    }
    /* set and enable alarm */
    mtcTimer_ptr->sev.sigev_notify = SIGEV_SIGNAL;
    mtcTimer_ptr->sev.sigev_signo  = SIGRTMIN;
    mtcTimer_ptr->sev.sigev_value.sival_ptr = &mtcTimer_ptr->tid;

    /* TODO: move up or set block till time is set ? */
    mtcTimer_ptr->value.it_value.tv_sec     = secs;
    mtcTimer_ptr->value.it_value.tv_nsec    = (msec*1000000) ;
    mtcTimer_ptr->value.it_interval.tv_sec  = secs ;
    mtcTimer_ptr->value.it_interval.tv_nsec = (msec*1000000) ;

    if ( timer_create (CLOCK_REALTIME, &mtcTimer_ptr->sev, &mtcTimer_ptr->tid) == -1 )
    {
        elog ("%s (%s) Timer 'create' (timer_create) failed (-1)\n",
                  mtcTimer_ptr->hostname.c_str(),
                  mtcTimer_ptr->service.c_str() );

        rc = FAIL_TIMER_CREATE ;
        goto _timer_start_out ;
    }

    /* make a backup copy just for fun */
    mtcTimer_ptr->secs = secs ;
    mtcTimer_ptr->msec = msec ;
    timer_count++ ;

    /* Set the ring to false DEFORE we start the timer */
    mtcTimer_ptr->_guard = 0x12345678 ;
    mtcTimer_ptr->guard_ = 0x77654321 ;
    mtcTimer_ptr->ring = false ;

    if ( timer_settime (mtcTimer_ptr->tid, 0, &mtcTimer_ptr->value, NULL) == -1 )
    {
        elog ("%s (%s) Timer 'set time' (timer_settime) failed (-1)\n",
                  mtcTimer_ptr->hostname.c_str(),
                  mtcTimer_ptr->service.c_str() );

        timer_count-- ;
        rc = FAIL_TIMER_SET ;
        goto _timer_start_out ;
    }
    mtcTimer_ptr->active = true ;

    /* moved here so that the tid will be valid in the log for debug purposes */
    tlog ("%s (%s) Tid:%p with %d.%03d second timeout (count:%d)\n",
              mtcTimer_ptr->hostname.c_str(),
              mtcTimer_ptr->service.c_str(),
              mtcTimer_ptr->tid,
              mtcTimer_ptr->secs,
              mtcTimer_ptr->msec,
              timer_count );

_timer_start_out:

    mtcTimer_ptr->mutex = false ;

    return rc ;
}

int mtcTimer_start ( struct mtc_timer & mtcTimer,
                     void (*handler)(int, siginfo_t*, void*),
                     int secs )
{
    return ( _timer_start ( &mtcTimer, handler, secs, 0 ));
}

int mtcTimer_start_msec ( struct mtc_timer & mtcTimer,
                          void (*handler)(int, siginfo_t*, void*),
                          int msec )
{
    return ( _timer_start ( &mtcTimer, handler, 0, msec ));
}

int mtcTimer_start ( struct mtc_timer * mtcTimer_ptr,
                     void (*handler)(int, siginfo_t*, void*),
                     int secs )
{
    return ( _timer_start ( mtcTimer_ptr, handler, secs, 0 ));
}

int mtcTimer_start_sec_msec ( struct mtc_timer * mtcTimer_ptr,
                              void (*handler)(int, siginfo_t*, void*),
                              int secs , int msec )
{
    return ( _timer_start ( mtcTimer_ptr, handler, secs, msec ));
}

int mtcTimer_start_msec ( struct mtc_timer * mtcTimer_ptr,
                          void (*handler)(int, siginfo_t*, void*),
                          int msec )
{
    return ( _timer_start ( mtcTimer_ptr, handler, 0, msec ));
}

/*************************************************************************/

int _timer_stop ( struct mtc_timer * mtcTimer_ptr , bool int_safe)
{
    int rc = PASS ;

    if ( mtcTimer_ptr == NULL)
    {
        return (FAIL_NULL_POINTER);
    }

    if ( timer_count == 0 )
    {
        if ( int_safe == false )
        {
           elog ("%s (%s) called with no outstanding timers\n",
                      mtcTimer_ptr->hostname.c_str(),
                      mtcTimer_ptr->service.c_str());
        }
        goto _timer_stop_out ;
    }
    else if ( mtcTimer_ptr->tid )
    {
        mtcTimer_ptr->value.it_value.tv_sec = 0;
        mtcTimer_ptr->value.it_value.tv_nsec = 0;
        mtcTimer_ptr->value.it_interval.tv_sec = 0;
        mtcTimer_ptr->value.it_interval.tv_nsec = 0;
        if ( timer_settime (mtcTimer_ptr->tid, 0, &mtcTimer_ptr->value, NULL) == -1 )
        {
            if ( int_safe == false )
            {
                elog ("%s (%s) timer_settime failed (tid:%p)\n",
                          mtcTimer_ptr->hostname.c_str(),
                          mtcTimer_ptr->service.c_str(),
                          mtcTimer_ptr->tid );
            }
            rc = FAIL_TIMER_STOP ;
            goto _timer_stop_out ;
        }

        if ( int_safe == false )
        {
            tlog ("%s (%s) Tid:%p with %d.%d second timeout (count:%d)\n",
                      mtcTimer_ptr->hostname.c_str(),
                      mtcTimer_ptr->service.c_str(),
                      mtcTimer_ptr->tid,
                      mtcTimer_ptr->secs,
                      mtcTimer_ptr->msec,
                      timer_count );
        }
            timer_delete (mtcTimer_ptr->tid);
            mtcTimer_ptr->tid    = NULL  ;
            if ( timer_count )
                timer_count-- ;
        }
    else if ( int_safe == false )
    {
        elog ("%s (%s) called with null TID (count:%d)\n",
                  mtcTimer_ptr->hostname.c_str(),
                  mtcTimer_ptr->service.c_str(),
                  timer_count);
    }

#ifdef WANT_OUTSTANDING_TIMER_COUNT
    if ( int_safe == false )
    {
        tlog ("%s (%s) Outstanding timers: %d\n",
                  mtcTimer_ptr->hostname.c_str(),
                  mtcTimer_ptr->service.c_str(),
                  timer_count );
    }
#endif

_timer_stop_out:
    mtcTimer_ptr->active = false ;
    return rc ;
}


/* Interrupt level safe timer stop utility by pointer */
int mtcTimer_stop_int_safe ( struct mtc_timer * mtcTimer_ptr )
{
    return ( _timer_stop ( mtcTimer_ptr, true )) ;
}

/* Interrupt level safe timer stop utility by reference */
int mtcTimer_stop_int_safe ( struct mtc_timer & mtcTimer )
{
    return ( _timer_stop ( &mtcTimer, true ) );
}

/* timer stop utility by pointer */
int mtcTimer_stop ( struct mtc_timer * mtcTimer_ptr )
{
    return ( _timer_stop ( mtcTimer_ptr, false ) );
}

/* stop utility by reference */
int mtcTimer_stop ( struct mtc_timer & mtcTimer )
{
    return ( _timer_stop ( &mtcTimer , false ));
}



bool mtcTimer_expired ( struct mtc_timer & mtcTimer )
{
    if (( mtcTimer.ring   == true  ) ||
        ( mtcTimer.active == false ) ||
        ( mtcTimer.tid    == NULL  ))
    {
        return (true);
    }
    return (false);
}


bool mtcTimer_expired ( struct mtc_timer * mtcTimer_ptr )
{
    if ( mtcTimer_ptr )
    {
        if (( mtcTimer_ptr->ring   == true  ) ||
            ( mtcTimer_ptr->active == false ) ||
            ( mtcTimer_ptr->tid    == NULL  ))
        {
            return (true);
        }
    }
    return (false);
}


void mtcTimer_reset ( struct mtc_timer & mtcTimer )
{
    if ( mtcTimer.tid )
        _timer_stop ( &mtcTimer , false );

    if ( mtcTimer.active )
        mtcTimer.active = false ;

    mtcTimer.ring = false ;
}

void mtcTimer_reset ( struct mtc_timer * mtcTimer_ptr )
{
    if ( mtcTimer_ptr )
    {
        if ( mtcTimer_ptr->tid )
            _timer_stop ( mtcTimer_ptr , false );

        if ( mtcTimer_ptr->active )
            mtcTimer_ptr->active = false ;

        mtcTimer_ptr->ring = false ;
    }
}

/*************************************************************************
 *
 * Issue: These static vars record unknown/stale timer data.
 *            The time of the ring, the TID, and the number of outstanding
 *            timers at that time. They are defaulted to zero and should
 *            remain that way. The mtcTimer_dump_data utility can be
 *            called periodically by a process audit, will create a Swerr
 *            log with the recorded data and then clear these vars only
 *            to allow the next occurance to be recorded and loged on the
 *            next audit interval.
 *
 **************************************************************************/
static timer_t * stale_tid_ptr = NULL ;
static string    stale_tid_time = ""  ;
static int       stale_tid_count = 0  ;

/* Dump the mtcTimer data - currently only dumps stale data */
void mtcTimer_dump_data ( void )
{
    if ( stale_tid_ptr )
    {
        slog ("Unknown timer fired at '%s' with tid '%p' ; module has %d loaded timers\n",
                stale_tid_time.c_str(),
                stale_tid_ptr,
                stale_tid_count );

        stale_tid_ptr = NULL ;
        stale_tid_time.clear() ;
        stale_tid_count = 0 ;
    }
}

int _timer_stop_tid ( timer_t * tid_ptr , bool int_safe )
{
    int rc = PASS ;

#ifdef UNUSED
    UNUSED (int_safe);
#endif

    /*********************************************************************
     *
     * Issue reported a segfault that was a result of trying to cancel
     * a timer based on a stale/unknown TID. Its better to record the error
     * and leave the timer alone than to try and cancel and get a segfault.
     *
     * This update records the fact that this condition has happened only
     * to be logged by the host process with a call to mtcTimer_dump_data
     * and debugged after-the-fact.
     *
     **********************************************************************/
    if ( stale_tid_ptr == NULL )
    {
        stale_tid_ptr   = tid_ptr ;
        stale_tid_time  = pt();
        stale_tid_count = timer_count ;
    }

/* This defined out due to potential for segfault */
#ifdef WANT_TIMER_STOP_BY_ID

    if ( tid_ptr )
    {
        struct mtc_timer ghostTimer ;
        ghostTimer.value.it_value.tv_sec = 0;
        ghostTimer.value.it_value.tv_nsec = 0;
        ghostTimer.value.it_interval.tv_sec = 0;
        ghostTimer.value.it_interval.tv_nsec = 0;
        if ( timer_settime (*tid_ptr, 0, &ghostTimer.value, NULL) == -1 )
        {
            if ( int_safe == false )
            {
                elog ("ghostTimer stop (timer_settime) failed\n");
            }
            rc = FAIL_TIMER_STOP ;
            goto _timer_stop_tid_out ;
        }

        timer_delete (*tid_ptr);
        if ( timer_count )
           timer_count-- ;
    }
    else if ( int_safe == false )
    {
        elog ("called with NULL TID (%d)\n", timer_count);
    }

    if ( int_safe == false )
    {
        tlog ("Remaining outstanding timers: %d\n", timer_count );
    }

_timer_stop_tid_out:

#endif

    return rc ;
}

int mtcTimer_stop_tid ( timer_t * tid_ptr )
{
    return (_timer_stop_tid ( tid_ptr , false ));
}

int mtcTimer_stop_tid_int_safe ( timer_t * tid_ptr )
{
    return (_timer_stop_tid ( tid_ptr , true ));
}

/*************************************************************************/

void _timer_init ( struct mtc_timer * mtcTimer_ptr , string hostname, string service )
{
    if ( mtcTimer_ptr == NULL)
    {
        return ;
    }

    if ( hostname.empty() )
        mtcTimer_ptr->hostname = "unset_hostname" ;
    else
        mtcTimer_ptr->hostname = hostname ;

    if ( service.empty() )
        mtcTimer_ptr->service = "unset_service" ;
    else
        mtcTimer_ptr->service = service ;

    if (( mtcTimer_ptr->init == TIMER_INIT_SIGNATURE ) && ( mtcTimer_ptr->tid != NULL ))
    {
        slog ("%s '%s' service is unexpectedly re-initializated ; stopping active timer\n",
                  hostname.c_str(),
                  mtcTimer_ptr->service.c_str());
        mtcTimer_reset ( mtcTimer_ptr );
    }

    tlog ("%s '%s' service initialized (%p)\n",
               hostname.c_str(),
               mtcTimer_ptr->service.c_str(),
               mtcTimer_ptr->tid) ;

    mtcTimer_ptr->init = TIMER_INIT_SIGNATURE ;
    mtcTimer_ptr->tid  = NULL ;
    mtcTimer_ptr->secs = 0 ;
    mtcTimer_ptr->msec = 0 ;
    mtcTimer_ptr->ring = false ;
    mtcTimer_ptr->active= false ;
    mtcTimer_ptr->error = false ;
    mtcTimer_ptr->mutex = false ;

}

/* Init / clean a user timer */
void _timer_fini ( struct mtc_timer * mtcTimer_ptr )
{
    mtcTimer_reset ( mtcTimer_ptr );
    mtcTimer_ptr->init = 0 ;
}

/* de-init a user timer */
void mtcTimer_fini ( struct mtc_timer & mtcTimer )
{
    _timer_fini (&mtcTimer );
}

/* de-init a user timer */
void mtcTimer_fini ( struct mtc_timer* mtcTimer_ptr )
{
    _timer_fini ( mtcTimer_ptr );
}


/* Init / clean a user timer */
void mtcTimer_init ( struct mtc_timer * mtcTimer_ptr )
{
    _timer_init ( mtcTimer_ptr, "" , "" );
}

/* Init / clean a user timer */
void mtcTimer_init ( struct mtc_timer * mtcTimer_ptr , string hostname, string service)
{
    _timer_init ( mtcTimer_ptr, hostname , service );
}

/* Init / clean a user timer */
void mtcTimer_init ( struct mtc_timer & mtcTimer )
{
    _timer_init (&mtcTimer, "", "" );
}

/* Init / clean a user timer */
void mtcTimer_init ( struct mtc_timer & mtcTimer , string hostname )
{
    _timer_init (&mtcTimer, hostname, "" );
}

/* Init / clean a user timer */
void mtcTimer_init ( struct mtc_timer & mtcTimer , string hostname, string service )
{
    _timer_init (&mtcTimer, hostname, service );
}

/* Wait Utilities - only use during init */

static struct mtc_timer waitTimer ;

static void waitTimer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    UNUSED(sig);
    UNUSED(uc);
    if ( !(*tid_ptr) )
    {
        return ;
    }
    /* is base mtc timer */
    else if (( *tid_ptr == waitTimer.tid ) )
    {
        waitTimer.ring = true ;
        mtcTimer_stop ( waitTimer );
    }
    else
    {
        wlog ("Unexpected timer (%p)\n", *tid_ptr );
        mtcTimer_stop_tid ( tid_ptr );
    }
}

void mtcWait_msecs ( int millisecs )
{
    if ( waitTimer.init != TIMER_INIT_SIGNATURE )
        mtcTimer_init ( waitTimer , "localhost", "mtcWait_msecs" );

    if ( millisecs > 999 )
    {
        wlog ("Wait request too long, rounding down to 999\n");
        millisecs = 999 ;
    }
    mtcTimer_start_msec ( waitTimer, waitTimer_handler, millisecs );
    do
    {
        usleep (1000);
        daemon_signal_hdlr ();
    } while ( waitTimer.ring == false ) ;
}

void mtcWait_secs ( int secs )
{
    if ( waitTimer.init != TIMER_INIT_SIGNATURE )
        mtcTimer_init ( waitTimer , "localhost", "mtcWait_secs" );

    mtcTimer_start ( waitTimer, waitTimer_handler, secs );
    do
    {
        sleep (1);
        daemon_signal_hdlr ();
    } while ( waitTimer.ring == false ) ;
}

void mtcTimer_mem_log ( void )
{
    char str [64] ;
    sprintf ( str, "Working Timers: %d\n", timer_count );
    mem_log ( str );
}
