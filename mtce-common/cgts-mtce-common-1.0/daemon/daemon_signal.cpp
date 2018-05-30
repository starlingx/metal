/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Maintenance Daemon Signal Handling
  */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <signal.h>
#include <errno.h>

using namespace std;

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "sig"

#include "daemon_common.h" /* Common Daemon Structs and Utils */
#include "nodeBase.h"      /* Common Definitions              */
#include "nodeUtil.h"      /* Common Definitions              */

/* Flag indicating that the signal handler is initialized */
static bool __signal_init_done = false ;

/* Flag indicating that there is an active signal assertion */
static bool __signal_assertion = false ;

/* Flag to control the enabled state of the latency monitor
 * - default = disabled (false) */
static bool __signal_want_latency_monitor = false;

static unsigned long long __signal_prev_time = 0 ;
static unsigned long long __signal_this_time = 0 ;

/* Signal specific flag assertions */
static bool __signal_sigchld_assertion = false ;
static bool __signal_sigusr1_assertion = false ;
static bool __signal_sigusr2_assertion = false ;
static bool __signal_sigint_assertion  = false ;
static bool __signal_sigterm_assertion = false ;
static bool __signal_sigkill_assertion = false ;
static bool __signal_sigabrt_assertion = false ;
static bool __signal_sighup_assertion  = false ;
static bool __signal_sigcont_assertion = false ;
static bool __signal_sigstp_assertion  = false ;
static bool __signal_sigpipe_assertion = false ;
static bool __signal_unsupported       = false ;

/* List of supported signals */
#define MAX_SIGNALS 9
static const int signals [MAX_SIGNALS]={ SIGTERM, SIGINT, SIGUSR1, SIGUSR2, SIGHUP, SIGTSTP, SIGCHLD, SIGABRT, SIGPIPE};
static const char signal_names [MAX_SIGNALS][5] = {"TERM", "INT","USR1","USR2","HUP","TSTP","CHLD", "ABRT", "PIPE" };

/*
 * Control enabled state of the base level signal handler latency monitor.
 * state - true to enable or false to disable
 */
void daemon_latency_monitor ( bool state )
{
   __signal_want_latency_monitor = state ;
   __signal_prev_time = gettime_monotonic_nsec();
}

void __signal_hdlr ( int signo, siginfo_t * siginfo_ptr , void * uc )
{
    UNUSED(uc);
    UNUSED(siginfo_ptr); /* Future */

    if      (signo == SIGCHLD) __signal_sigchld_assertion = true ;
    else if (signo == SIGUSR1) __signal_sigusr1_assertion = true ;
    else if (signo == SIGUSR2) __signal_sigusr2_assertion = true ;
    else if (signo == SIGHUP)  __signal_sighup_assertion  = true ;
    else if (signo == SIGINT ) __signal_sigint_assertion  = true ;
    else if (signo == SIGTERM) __signal_sigterm_assertion = true ;
    else if (signo == SIGKILL) __signal_sigkill_assertion = true ;
    else if (signo == SIGABRT) __signal_sigabrt_assertion = true ;
    else if (signo == SIGCONT) __signal_sigcont_assertion = true ;
    else if (signo == SIGTSTP) __signal_sigstp_assertion  = true ;
    else if (signo == SIGPIPE) __signal_sigpipe_assertion = true ;
    else                       __signal_unsupported       = true ;

    /* set the glabal flag indicating there is a signal to handle */
    __signal_assertion = true ;
}

#define LATENCY_THRESHOLD_2SECS (2000)

void daemon_signal_hdlr ( void )
{
    /* Monitor base level signal handler scheduling latency */
    if (( __signal_init_done ) && ( __signal_want_latency_monitor ))
    {
        __signal_this_time = gettime_monotonic_nsec();
        if ( __signal_this_time > (__signal_prev_time + (NSEC_TO_MSEC*(LATENCY_THRESHOLD_2SECS))))
        {
            llog ("... %4llu.%-4llu msec - base level signal handler\n",
                     ((__signal_this_time-__signal_prev_time) > NSEC_TO_MSEC) ? ((__signal_this_time-__signal_prev_time)/NSEC_TO_MSEC) : 0,
                     ((__signal_this_time-__signal_prev_time) > NSEC_TO_MSEC) ? ((__signal_this_time-__signal_prev_time)%NSEC_TO_MSEC) : 0 );
        }
        __signal_prev_time = __signal_this_time ;
    }

    /* handle signals at base level */
    if (( __signal_init_done == true ) && ( __signal_assertion == true ))
    {
        __signal_assertion = false ; /* prevent recursion */

        if ( __signal_sigchld_assertion )
        {
            // ilog("Received SIGCHLD\n");
            {
                daemon_sigchld_hdlr ();
            }
            __signal_sigchld_assertion = false ;
        }
        if ( __signal_sigusr1_assertion )
        {
            // ilog("Received SIGUSR1 ----------------------------\n");
            {
                daemon_health_test ();
            }
            __signal_sigusr1_assertion = false ;
        }
        if ( __signal_sigusr2_assertion )
        {
            ilog("Received SIGUSR2\n");

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__SIGNAL_NOEXIT ) == false )
#endif
            {
                daemon_dump_info   () ;
                daemon_dump_cfg ();
            }
            __signal_sigusr2_assertion = false ;
        }
        if ( __signal_sighup_assertion )
        {
            ilog("Received SIGHUP ; Reloading Config\n");
#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__SIGNAL_NOEXIT ) == false )
#endif
            {
                daemon_configure () ;
            }
            __signal_sighup_assertion = false ;
        }
        if ( __signal_sigint_assertion  )
        {
            ilog("Received SIGINT\n");

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__SIGNAL_NOEXIT ) == false )
#endif
            {
                daemon_remove_pidfile ();
                daemon_dump_cfg ();
                daemon_exit();
            }
            __signal_sigint_assertion = false ;
        }
        if ( __signal_sigterm_assertion  )
        {
            ilog("Received SIGTERM\n");

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__SIGNAL_NOEXIT ) == false)
#endif
            {
                daemon_remove_pidfile ();
                daemon_dump_cfg ();
                daemon_exit();
            }
            __signal_sigterm_assertion = false ;
        }
        if ( __signal_sigkill_assertion )
        {
            ilog("Received SIGKILL\n");

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__SIGNAL_NOEXIT ) == false )
#endif
            {
                daemon_remove_pidfile ();
                daemon_exit();
            }
            __signal_sigkill_assertion = false ;
        }
        if ( __signal_sigabrt_assertion  )
        {
            ilog("Received SIGABRT\n");

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__SIGNAL_NOEXIT ) == false )
#endif
            {
                daemon_remove_pidfile ();
                daemon_exit();
            }
            __signal_sigabrt_assertion = false ;
        }
        if ( __signal_sigcont_assertion )
        {
             ilog("Received SIGCONT ; not supported\n");
             __signal_sigcont_assertion = false ;
        }
        if ( __signal_sigstp_assertion )
        {
             ilog("Received SIGSTP ; not supported\n");
             __signal_sigstp_assertion = false ;
        }
        if ( __signal_sigpipe_assertion )
        {
             ilog("Received SIGPIPE\n");
             __signal_sigpipe_assertion = false ;
        }
        if ( __signal_unsupported )
        {
             ilog ("Error: unsupported signal\n");
             __signal_unsupported = false ;
        }
    }
}

/* bind the handler to each supported signal */
int daemon_signal_init ( void )
{
    int i ;
    int rc = PASS ;

    struct sigaction act;
    memset( &act, 0, sizeof(act) );

    act.sa_sigaction = __signal_hdlr;
    act.sa_flags = (SA_SIGINFO | SA_NODEFER) ;
    for ( i = 0 ; i < MAX_SIGNALS ; i++ )
    {
        if ( sigaction ( signals[i], &act, NULL ))
        {
            elog("Error: Registering '%s' Signal (%i) with kernel (%d:%m)\n",
                signal_names[i], signals[i], errno );

            rc = FAIL ;
        }
    }

    ilog ("Signal Hdlr : Installed (sigaction)\n");

    __signal_prev_time = __signal_this_time = gettime_monotonic_nsec() ;
    __signal_assertion = false;
    __signal_init_done = true ;
    return rc ;
}
