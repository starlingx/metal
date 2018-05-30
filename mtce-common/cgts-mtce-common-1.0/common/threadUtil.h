#ifndef __INCLUDE_THREADBASE_H__
#define __INCLUDE_THREADBASE_H__

/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/******************************************************************************
 *
 * This is the header file for the common Threads Utility Module of Maintenance.
 *
 * This module offers the following API public to other maintenance modules
 * for the purpose of running pthreads.
 *
 * Limitations: Does not support thread signal handling and only supports
 *
 *              - a single thread per host
 *              - detached pthreads ; pthread_join would stall the parent process.
 *
 * There are 2 main structures used for managing pthreads.
 *
 *   thread_ctrl_type - owned and updated and only visible to the parent service.
 *   thread_info_type - initially init'ed by the parent process, updated by the
 *                      thread and once the thread is done the parent process
 *                      consumes the results of the thread execution.
 *
 *                      See these structures definition below for more details.
 *
 * Thead Utility API Summary
 *
 *   threadUtil_init  - module init   ; called in daemon_init
 *   threadUtil_fini  - module finish ; called in dameon_exit
 *
 *   thread_handler   - thread FSM ; called periodically in parent process main loop
 *
 *   thread_init      - setup a thread for launch ; default stage is IDLE
 *   thread_idle      - returns true if that thread is in the IDLE state
 *   thread_launch    - requests thread launch if in IDLE state
 *   thread_done      - puts a thread into the IDLE state ; called after parent
 *                      consumes thread results. Required before next thread launch
 *   thread_kill      - sends cooperative cancel and SIGKILL to thread via info.signal
 *
 * Any maintenance service that wants thread execution support must ...
 *
 * Run thread_init    ( ctrl, info, ... )' before running thread_handler.
 * Run thread_handler ( ctrl, info ) periodically in the service's FSM loop.
 *
 * With the above done and in place ; when a service wants to run a thread
 * it makes the following calls to launch a thread when idle and monitor
 * completion or timeout of a thread.
 *
 * if ( thread_idle ( ctrl )
 *    if ( thread_launch ( ctrl ) == PASS )
 *      start thread timeout timer
 *
 * service_FSM ( ; ; )
 * {
 *    if ( timeout )
 *       thread_kill ( ctrl );
 *    else if ( ctrl.runcount > info.runcount )
 *       parent consumes info data and status
 *       thread_done ( ctrl );
 *
 *    thread_handler
 * }
 *
 * The thread_handler FSM lauches the thread, starts its own thread timeout
 * timer, monitors for thread timeout or completion and handles thread_done
 * and thread_kill requests.
 *
 * thread_done is used to handle the transition between when the thread
 * indicates it is done and when the parent service is finished consuming
 * the thread results.
 *
 * A thread is expected to update ...
 *
 *  - info.id with the thread_self thread identifier.
 *  - info.data with the thread's consumable results.
 *  - info.status with its exection status ; a maintenance return code returnCodes.h
 *  - info.status_string with a string representative of the info.status condition.
 *  - info.progress should be incremented in the thread's main loop to
 *                  represent forward progress of the thread.
 *  - info.runcount should be incremented by 1 as the last operation before
 *                  pthread_exit to indicate that the thread is done.
 *
 *  A thread execution is cooperative completion and is therefore expected to
 *  periodically call pthread_signal_handler in the main loop in support of
 *  1. info.signal for SIGKILL exit request
 *  2. pthread_testcancel to force monitor a cancellation point
 *
 * The State Machine: The 'thread_handler' FSM manages a threads life cycle
 * through the following stages.
 *
 * IDLE     - default 'do nothing' starting and ending stage.
 * LAUNCH   - set by thread_launch call
 *          - launches the thread and starts a timeout timer
 *          - changes to stage MONITOR after successful launch
 *          - changes to stage DONE if launch fails (failure status is updated)
 * MONITOR  - set by LAUNCH stage
 *          - monitors for completion or timeout
 *          - timeout changes stage to KILL
 *          - completion changes stage to DONE
 * KILL     - set by thread_kill call or MONITOR for timout case
 * WAIT     - set by KILL to add kill wait time before going IDLE
 *
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#define gettid() syscall(SYS_gettid)

using namespace std;

#include "daemon_common.h"
#include "nodeBase.h"
#include "nodeTimers.h"

/** Info logger macro*/
#define ilog_t(format, args...) { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s: Info : " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define elog_t(format, args...) { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s:Error : " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define wlog_t(format, args...) { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s: Warn : " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define dlog_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&1) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug : " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}
#define dlog1_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&2) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug2: " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}
#define dlog2_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&4) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug4: " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}
#define dlog3_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&8) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug8: " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}


#define blog_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_bmgmt&1) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt : " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}
#define blog1_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_bmgmt&2) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt2: " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}
#define blog2_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_bmgmt&4) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt4: " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}
#define blog3_t(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_bmgmt&8) \
    { syslog(LOG_INFO, "[%ld.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt8: " format, gettid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }}



#define THREAD_INIT_SIG     (0xbabef00d)
#define MAX_PTHREADS                 (1) /* max number concurrent pthreads  */
#define DEFAULT_THREAD_TIMEOUT_SECS (60) /* default pthread exec timout     */
#define MAX_LOG_PREFIX_LEN          (MAX_CHARS_HOSTNAME*4)
#define THREAD_POST_KILL_WAIT       (10) /* wait time between KILL and IDLE */

typedef enum
{
    THREAD_STAGE__IDLE = 0, /* do nothing stage                             */
    THREAD_STAGE__IGNORE,   /* unmonitored thread                           */
    THREAD_STAGE__LAUNCH,   /* run the thread                               */
    THREAD_STAGE__MONITOR,  /* look for done status and timeout             */
    THREAD_STAGE__DONE,     /* wait for parent to consume done results      */
    THREAD_STAGE__KILL,     /* send cancel and kill requests to thread      */
    THREAD_STAGE__WAIT,     /* wait time before changing to idle stage      */
    THREAD_STAGE__STAGES,   /* number of stages                             */
} thread_stages_enum ;

/****************************************************************************
 *
 * Name       : thread_ctrl_type
 *
 * Description: Structure updated by and only visible to the parent.
 *              Used to control execution of the thread.
 *
 ****************************************************************************/
typedef struct
{
   thread_stages_enum stage ; /* current FSM stage                          */

   /* Thread details                                                        */
   pthread_t             id ; /* the thread id                              */
   void* (*thread) (void*)  ; /* pointer to the thread                      */
   string          hostname ; /* hostname this thread is tied to            */
   string              name ; /* short name of the thread                   */

   /* Timout controls                                                       */
   struct mtc_timer   timer ; /* the timer to use for the thread            */
   int              timeout ; /* timout in msecs , 0 for no timeout         */

   /* FSM Level Completion Control and Status                               */
   int               status ; /* FSM status ; overrides info status         */
   bool                done ; /* flag indicating thread data was consumed   */
   int             runcount ; /* copy of info.runcount before launch ;      */
   int              retries ; /* max thread retries                         */

   /* Miscellaneous                                                         */
   int   stage_log_throttle ; /* limit number of logs in this stage         */
   bool                idle ; /* flags entry into idle stage & log throttle */
} thread_ctrl_type ;


/****************************************************************************
 *
 * Name       : thread_info_type
 *
 * Description: Structure initialized by the parent, updated by the thread
 *              during execution and then data and status is consumed by
 *              the parent when the thread is done.
 *
 ****************************************************************************/

typedef struct
{
    /* -------------------------------------------------------------------- */
    /*                   Thread Read Only Data                              */
    /* -------------------------------------------------------------------- */

    string hostname       ; /* hostname this thread is tied to              */
    string name           ; /* short name of the thread                     */
    int    command        ; /* the command the thread should execute        */
    int    signal         ; /* parent request signal ; SIGKILL exit request */
    void * extra_info_ptr ; /* pointer to thread specific command data      */
    char   log_prefix[MAX_LOG_PREFIX_LEN]; /* preformatted log prefix       */

    /* -------------------------------------------------------------------- */
    /* Thread Write Data - Parent Read Only Result/Progress/Monitoring Data */
    /* -------------------------------------------------------------------- */

    pthread_t id         ; /* the thread id of self                         */
    int    status        ; /* thread execution status set before runcount++ */
    string status_string ; /* status string representing unique error case  */
    int    runcount      ; /* thread increments just before exit - complete */
    int    progress      ; /* incremented by thread ; show forward progress */
    string data          ; /* data that resulted from the thread execution  */
    int    signal_handling;/* incremented by thread calling signal handler  */
    int    pw_file_fd    ; /* file descriptor for the password file         */
    string password_file ; /* the name of the password file                 */

} thread_info_type ;

/****************************************************************************/
/*                       Thread Module API                                  */
/* **************************************************************************/

/* module init/fini */
void threadUtil_fini ( void );
int  threadUtil_init ( void (*handler)(int, siginfo_t*, void* ));

void threadUtil_setstack_size ( void );

/* Onetime thread init setup */
void   thread_init   ( thread_ctrl_type & ctrl,
                       thread_info_type & info,
                       void*, /* extra_info_ptr */
                       void* (*thread) (void*),
                       int timeout,
                       string & hostname,
                       string threadname );

/* The thread FSM */
int    thread_handler( thread_ctrl_type & ctrl, thread_info_type & info );

/* Thread execution management APIs */
int    thread_launch ( thread_ctrl_type & ctrl , thread_info_type & info );
bool   thread_done   ( thread_ctrl_type & ctrl );
bool   thread_idle   ( thread_ctrl_type & ctrl );
void   thread_kill   ( thread_ctrl_type & ctrl , thread_info_type & info );
string thread_stage  ( thread_ctrl_type & ctrl );

/* Cooperative service of cancel and exit requests from parent */
void pthread_signal_handler ( thread_info_type * info_ptr );

pthread_t thread_launch_thread (void*(thread)(void*), void*);

void * thread_test ( void * arg );

#endif // __INCLUDE_THREADBASE_H__
