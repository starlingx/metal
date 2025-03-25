/*
 * Copyright (c) 2016-2017, 2024 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/****************************************************************************
 *
 * @file
 * Wind River Titanium Cloud - Threading Base Implementation Module"
 *
 * This module implements the common thread utility module that can be used
 * by any maintenance process to
 *
 *  - launch threads
 *  - monitor thread execution
 *  - detect thread completion
 *  - kill a thread if needed
 *
 *  - thread exeuction FSM
 *
 * For more complehensive description please refer
 * to the module header threadUtil.h
 *
 ****************************************************************************/

#include "daemon_common.h"   /* for ... daemon_health_test           */
#include "nodeBase.h"        /* for ... mtce node common definitions */
#include "hostUtil.h"        /* for ... mtce host common definitions */
#include "threadUtil.h"      /* for ... this module header           */
#include "nodeUtil.h"        /* for ... fork_execv                   */

/* Stores the parent process's timer handler */
static void (*thread_timer_handler)(int, siginfo_t*, void*) = NULL ;

static pthread_attr_t __attr;
static sigset_t       __enabled_mask ;
static sigset_t       __disabled_mask;
static unsigned int  __thread_init_sig ;


/*****************************************************************************
 *
 * Name       : threadUtil_init
 *
 * Description: Module init with caller specified timer handler.
 *
 ****************************************************************************/

static std::string threadStages_str[THREAD_STAGE__STAGES+1];

int threadUtil_init ( void (*handler)(int, siginfo_t*, void* ), size_t stack_size )
{
    /* preserve parent process timer handler */
    thread_timer_handler = handler ;

    /* setup the stage strings */
    threadStages_str[THREAD_STAGE__IDLE]     = "Idle"    ;
    threadStages_str[THREAD_STAGE__IGNORE]   = "Ignore"  ;
    threadStages_str[THREAD_STAGE__LAUNCH]   = "Launch"  ;
    threadStages_str[THREAD_STAGE__MONITOR]  = "Monitor" ;
    threadStages_str[THREAD_STAGE__DONE]     = "Done"    ;
    threadStages_str[THREAD_STAGE__KILL]     = "Kill"    ;
    threadStages_str[THREAD_STAGE__WAIT]     = "Wait"    ;

    /* setup to create a 'detached' threads */
    pthread_attr_init(&__attr);
    pthread_attr_setdetachstate(&__attr, PTHREAD_CREATE_DETACHED);
    threadUtil_setstack_size   ( stack_size );

    __thread_init_sig = THREAD_INIT_SIG ;

    return (PASS);
}

void threadUtil_setstack_size ( size_t stack_size )
{
    size_t stack_size_before = 0 ;
    size_t stack_size_after = 0 ;
    /* manage pthread stack size */
    if ( pthread_attr_getstacksize (&__attr,&stack_size_before) == PASS )
    {
        if ( stack_size_before > stack_size )
        {
            if ( pthread_attr_setstacksize ( &__attr, stack_size ) == PASS )
            {
                if ( pthread_attr_getstacksize (&__attr,&stack_size_after) == PASS )
                {
                    ilog ("thread Stack: %zu KB (was %zu)\n",
                              stack_size_after/1024,
                              stack_size_before/1024 );
                }
                else
                {
                    elog ("failed to set pthread stack size (%d:%m)\n", errno );
                }
            }
        }
        else
        {
            ilog ("pthread stack size is %zu bytes\n", stack_size_before );
        }
    }
    else
    {
        elog ("failed to get pthread stack size (%d:%m)\n", errno );
    }
}


/*****************************************************************************
 *
 * Name       : threadUtil_fini
 *
 * Description: Module cleanup
 *
 ****************************************************************************/

void threadUtil_fini ( void )
{
    ; // ilog ("called\n");
}

/*****************************************************************************
  *
 * Name       : threadUtil_bmcSystemCall
 *
 * Description: Execute a bmc system call using the supplied request string.
 *
 *              If the call takes longer than the supplied latency threshold
 *              then print a log indicating how long it took.
 *
 * Returns    : the system call's return code.
 *
 ****************************************************************************/

#ifndef NSEC_TO_SEC
#define NSEC_TO_SEC  (1000000000)
#endif

int threadUtil_bmcSystemCall (string hostname,
                              string request,
                              string datafile,
                              unsigned long long latency_threshold_secs)
{
    unsigned long long before_time = gettime_monotonic_nsec () ;
    int rc = fork_execv ( hostname, request , datafile ) ;
    unsigned long long after_time = gettime_monotonic_nsec () ;
    unsigned long long delta_time = after_time-before_time ;
    if ( delta_time > (latency_threshold_secs*1000000000))
    {
        wlog ("%s bmc system call took %2llu.%-8llu sec", hostname.c_str(),
              (delta_time > NSEC_TO_SEC) ? (delta_time/NSEC_TO_SEC) : 0,
              (delta_time > NSEC_TO_SEC) ? (delta_time%NSEC_TO_SEC) : 0);
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : _stage_change
 *
 * Description: Change thread FSM stage.
 *
 * See thread_stages_enum in threadUtil.h for a list of stage enums
 *
 ****************************************************************************/

void _stage_change ( thread_ctrl_type & ctrl, thread_stages_enum newStage )
{
    if ( newStage < THREAD_STAGE__STAGES )
    {
        clog ("%s %s thread stage from %s -> %s\n",
                  ctrl.hostname.c_str(),
                  ctrl.name.c_str(),
                  threadStages_str[ctrl.stage].c_str(),
                  threadStages_str[newStage].c_str());
        ctrl.stage = newStage ;
    }
    else
    {
        slog ("%s %s thread stage change to '%d' is invalid ; switching to KILL\n",
                  ctrl.hostname.c_str(),
                  ctrl.name.c_str(),
                  newStage );
        ctrl.stage = THREAD_STAGE__KILL ;
    }
    ctrl.stage_log_throttle = 0 ;
}

/*****************************************************************************
 *
 * Name       : thread_stage
 *
 * Description: Returns a string representing the current thread stage.
 *
 *****************************************************************************/

string thread_stage  ( thread_ctrl_type & ctrl )
{
    if ( ctrl.stage < THREAD_STAGE__STAGES )
        return(threadStages_str[ctrl.stage]);
    return("out-of-range thread stage");
}

/*****************************************************************************
 *
 * Name       : thread_init
 *
 * Description: Default a thread 'ctrl' and 'info' structs.
 *
 * Assumptions: Called at init time once.
 *
 * Warning    : Thread should be running when this is called.
 * Warning    : Should not be called more than once or else might create
 *              an orphan timer.
 *
 * Parameters:
 *
 *    - reference to the ctrl and info structs for a specified thread
 *    - pointer to thread specific extra data
 *    - the thread function pointer itself
 *    - thread execution timeout in seconds
 *    - reference to the host and thread names.
 *
 * Returns   : nothing
 *
 *****************************************************************************/

void thread_init ( thread_ctrl_type & thread_ctrl,
                   thread_info_type & thread_info,
                   void* extra_data_ptr,
                   void* (*thread) (void*),
                   int      timeout,
                   string & hostname,
                   string   threadname )
{
    /* default the ctrl struct */
    thread_ctrl.stage      = THREAD_STAGE__IDLE ;
    thread_ctrl.done       = true ;
    thread_ctrl.idle       = true ;
    thread_ctrl.id         = 0 ;
    thread_ctrl.thread     = thread ;
    thread_ctrl.hostname   = hostname ;
    thread_ctrl.name       = threadname ;

    thread_ctrl.timeout    = timeout ;
    mtcTimer_init ( thread_ctrl.timer, hostname, threadname );

    thread_ctrl.status     = PASS ;
    thread_ctrl.runcount   = 0 ;
    thread_ctrl.retries    = 0 ;

    thread_ctrl.stage_log_throttle = 0 ;

    /* Init the thread's info struct - the only non-stack memory the
     * thread can look at or touch */
    thread_info.hostname = hostname ;
    thread_info.name     = threadname ;
    thread_info.id       = 0 ;
    thread_info.command  = 0 ;
    thread_info.runcount = 0 ;
    thread_info.progress = 0 ;
    thread_info.signal   = 0 ;
    thread_info.data.clear() ;
    thread_info.extra_info_ptr = extra_data_ptr ;
    thread_info.pw_file_fd = 0 ;
    thread_info.password_file.clear() ;

    /* command execution status */
    thread_info.status_string.clear();
    thread_info.status = 0 ;

    snprintf ( thread_info.log_prefix, MAX_LOG_PREFIX_LEN, "%s %s thread",
               thread_ctrl.hostname.data(), thread_ctrl.name.data());
}

/****************************************************************************
 *
 * Name       : thread_done
 *
 * Description: Return true if we are in the DONE stage.
 *
 ****************************************************************************/

bool thread_done ( thread_ctrl_type & ctrl )
{
    if ( ctrl.stage == THREAD_STAGE__DONE )
    {
        return (true) ;
    }
    return (false);
}

/****************************************************************************
 *
 * Name       : thread_idle
 *
 * Description: Return true if we are in the IDLE stage.
 *
 ****************************************************************************/

bool thread_idle ( thread_ctrl_type & ctrl )
{
    if ( ctrl.stage == THREAD_STAGE__IDLE )
    {
        return (true) ;
    }
    return (false);
}

/****************************************************************************
 *
 * Name       : thread_done_consume
 *
 * Description: Return to IDLE stage.
 *
 ****************************************************************************/

int thread_done_consume ( thread_ctrl_type & ctrl, thread_info_type & info )
{
    if ( ctrl.stage == THREAD_STAGE__IDLE )
    {
        return PASS ;
    }
    else if ( ctrl.done == false )
    {
        if ( info.runcount > ctrl.runcount )
        {
            ilog("%s thread cleanup ; cmd:%d ; cnt:%d:%d",
                 info.hostname.c_str(),
                 info.command,
                 ctrl.runcount,
                 info.runcount);
            ctrl.done = true ;
            ctrl.stage = THREAD_STAGE__DONE ;
            thread_handler (ctrl, info);
            return PASS ;
        }
        else
        {
            thread_kill(ctrl, info);
            return RETRY ;
        }
    }
    else
    {
        ctrl.stage = THREAD_STAGE__DONE ;
        thread_handler( ctrl, info );
        return PASS ;
    }
}

/****************************************************************************
 *
 * Name       : thread_launch
 *
 * Description: Perform prechecks that verify the ctrl struct is ready for
 *              thread launch and if so change stage to THREAD_STAGE__LAUNCH.
 *
 ****************************************************************************/

int  thread_launch ( thread_ctrl_type & ctrl, thread_info_type & info )
{
    int rc = FAIL ;
    if ( ! thread_timer_handler )
    {
        slog ("%s no thread timer handler bound in\n",
                  ctrl.hostname.c_str());
        rc = FAIL_NULL_POINTER ;
    }

    else if ( ctrl.thread == NULL )
    {
        slog ("%s %s no thread bound in\n",
                  ctrl.hostname.c_str(),
                  ctrl.name.c_str());
        rc = FAIL_NULL_POINTER ;
    }

    else if ( ctrl.stage != THREAD_STAGE__IDLE )
    {
        wlog ("%s %s not in IDLE stage (in %s stage)\n",
                  ctrl.hostname.c_str(),
                  ctrl.name.c_str(),
                  threadStages_str[ctrl.stage].c_str());
        thread_kill ( ctrl, info );
        rc = FAIL_BAD_STATE ;
    }

    else if ( ctrl.id )
    {
        slog ("%s %s thread may be running ; id is not null and should be\n",
                  ctrl.hostname.c_str(),
                  ctrl.name.c_str());
        thread_kill ( ctrl, info );
        rc = FAIL_THREAD_RUNNING ;
    }

    else
    {
        _stage_change ( ctrl, THREAD_STAGE__LAUNCH );
        rc = PASS ;
    }
    return (rc);
}


/****************************************************************************
 *
 * Name       : thread_kill
 *
 * Description: put the FSM in the kill state.
 *
 ****************************************************************************/

void thread_kill ( thread_ctrl_type & ctrl, thread_info_type & info )
{
    info.signal = SIGKILL ;

    /* only go to kill if not already handling kill */
    if (( ctrl.stage != THREAD_STAGE__KILL ) &&
        ( ctrl.stage != THREAD_STAGE__WAIT ) &&
        ( ctrl.stage != THREAD_STAGE__IDLE ))
    {
        wlog ("%s kill request\n", ctrl.hostname.c_str() );
        _stage_change ( ctrl, THREAD_STAGE__KILL );
    }
}


/*****************************************************************************
 *
 * Name       : thread_handler
 *
 * Description: finite state machine to manage a pthread execution life cycle
 *
 * The parent must periodically run this thread_handler to service and make
 * forward progress in the FSM.
 *
 * Thread FSM life cycle and responsibilities:
 *
 * Parent calls thread_init once before any launch which sets up the ctrl
 * and info structs. Default state THREAD_STAGE__IDLE IDLE
 *
 * When there is a thread to be launched ...
 *
 *    1. Parent FSM calls thread_launch to launch the thread
 *         - Thread FSM performs thread launch pre-checks
 *            - check for timer handler binding
 *              - rc = FAIL_NULL_POINTER
 *            - check that there is a thread bound in
 *              - rc = FAIL_NULL_POINTER
 *            - verify we are in the correct stage for launch
 *              - rc = FAIL_BAD_STATE
 *            - verify the thread is not already running
 *              - rc = FAIL_THREAD_RUNNING
 *
 *            - if rc == PASS change state to THREAD_STAGE__LAUNCH
 *            - if rc != PASS change state to THREAD_STAGE__IDLE
 *
 *         - Parent FSM handles thread_launch return status
 *
 *            if ( thread_launch == PASS )
 *              - start a parent timer ; a longer umbrella timer
 *            else
 *              - fail operation or retry
 *
 *    2. Thread FSM launches the thread in THREAD_STAGE__LAUNCH stage
 *         - preserves parent signal mask
 *         - clears signal mask so that thread does not inherit signal handling
 *         - launch the thread
 *         - restore signal mask
 *
 *         if launch failed
 *           - change ctrl.status = FAIL_THREAD_CREATE
 *           - change ctrl.stage = THREAD_STAGE__DONE
 *
 *         if launch passed
 *           - start the thread timeout timer if timeout is !0
 *           - change ctrl.stage = THREAD_STAGE__MONITOR
 *
 *    3. Thread FSM monitors thread execution in THREAD_STAGE__MONITOR stage
 *         - waits for done conditions or thread timeout
 *           - ctrl.timer.ring or incremented info.runcount
 *             Note: thread increments info.runcount on exit/done
 *
 *         if ( thread timeout )
 *           - sets ctrl.status = FAIL_TIMEOUT
 *           - sets ctrl.stage  = THREAD_STAGE__KILL
 *
 *         if ( info.runcount > ctrl.runcount )
 *           - stop thread timer
 *           - change ctrl.stage = THREAD_STAGE__DONE
 *
 *     4. Parent FSM Monitors for thread done or parent timer timeout
 *         - has started its own umbrella timeout timer that is a
 *           few seconds longer than the actual thread timeout.
 *         - thread_done returns true when ctrl.stage == THREAD_STAGE__DONE
 *
 *         if ( parent timeout )
 *           - sets ctrl.status = FAIL_TIMEOUT
 *           - sets trl.stage  = THREAD_STAGE__KILL
 *
 *         if ( thread_done )
 *           - interprets ctrl.status
 *           - interprets info.status
 *           - consumes info.data which contains thread execution result
 *           - changes ctrl.done = true once data is consumed.
 *           - Parent FSM is done with this thread
 *
 *     5. Thread FSM monitors for Parent FSM done in THREAD_STAGE__DONE
 *        - Parent FSM changes ctrl.done to true once it has consumed the thread data
 *        - Thread FSM polls ctrl.done
 *
 *        if ( ctrl.done == true )
 *           - changes ctrl.stage = THREAD_STAGE__IDLE
 *           - Thread FSM is done with this thread
 *
 * Note: The ctrl and info structs are intentionally kept separate for two
 *       reasons ...
 *
 *       1. distinguish between parent process (ctrl) and thread (info) data.
 *       2. the parent might want them to occupying completely differnet memory
 *          spaces in the future.
 *
 *****************************************************************************/

int thread_handler ( thread_ctrl_type & ctrl, thread_info_type & info )
{
    int rc = PASS ;

    switch ( ctrl.stage )
    {
        case THREAD_STAGE__IGNORE:
        {
            break ;
        }
        case THREAD_STAGE__IDLE:
        {
            if ( ctrl.idle == false )
            {
                ctrl.idle = true ;
                dlog ("%s IDLE\n", info.log_prefix);
                if (( ctrl.id ) || ( info.id ) || ( ctrl.done == false ))
                {
                    slog ("%s bad thread state [%lu:%lu:%d]\n", info.log_prefix, ctrl.id, info.id, ctrl.done );
                }
            }

            /******************** Garbage Collection *****************/


            /* remove previous password file if it somehow did not get removed before */
            if ( info.pw_file_fd )
            {
                wlog ("%s closing pw fd (%d) ; garbage collected\n",
                          info.hostname.c_str(),
                          info.pw_file_fd );

                close(info.pw_file_fd);
                info.pw_file_fd = 0 ;
            }

            if ( ! info.password_file.empty() )
            {
                if ( daemon_is_file_present ( info.password_file.data() ))
                {
                    wlog ("%s removing pw file (%s) ; garbage collected\n",
                              info.hostname.c_str(),
                              info.password_file.c_str());

                    unlink(info.password_file.data());
                    daemon_remove_file (info.password_file.data());
                    info.password_file.clear();
                }
            }

            break ;
        }
        case THREAD_STAGE__WAIT:
        {
            if ( mtcTimer_expired ( ctrl.timer ) )
            {
                ctrl.timer.ring = false ;
                ctrl.done = true ;
                ctrl.id = 0 ;
                info.id = 0 ;
                info.command = 0 ;
                _stage_change ( ctrl, THREAD_STAGE__IDLE );
            }
            else if ( ctrl.done == true )
            {
                /* force wait completed */
                mtcTimer_reset ( ctrl.timer );
                info.command = 0 ;
                _stage_change ( ctrl, THREAD_STAGE__IDLE );
            }

            break ;
        }
        case THREAD_STAGE__DONE:
        {
            if ( ctrl.done == true )
            {
                if (( info.signal_handling == 0 ) && ( info.status == PASS ))
                {
                    wlog ("%s %s thread not servicing pthread_signal_handler\n",
                              ctrl.hostname.c_str(),
                              ctrl.name.c_str());
                }
                dlog ("%s %s thread data was consumed by parent ; switching to IDLE\n",
                          ctrl.hostname.c_str(),
                          ctrl.name.c_str());
                ctrl.id = 0 ;
                info.id = 0 ;

                dlog ("%s %s done\n", ctrl.hostname.c_str(), ctrl.name.c_str());

                _stage_change ( ctrl, THREAD_STAGE__IDLE );
            }
            else if ( info.signal == SIGKILL )
            {
                wlog ("%s %s thread completed ; waiting on DONE but got SIGKILL ; forcing DONE\n", ctrl.hostname.c_str(), ctrl.name.c_str() );
                ctrl.done = true ;
            }
            break ;
        }
        case THREAD_STAGE__LAUNCH:
        {
            /*
             * pre-check should never this this come in as non-null but just
             * to be sure a thread is actually created properly we set it to null
             */
            if ( ctrl.id )
            {
                slog ("%s %s thread id should be 0\n",
                           ctrl.hostname.c_str(),
                           ctrl.name.c_str());

                ctrl.id = 0 ;
            }
            /*
             * Prepare thread complete criteria.
             *
             * When info.runcount > ctrl.runcount then the thread is done.
             */
            ctrl.runcount = info.runcount ;

            /* thread updates this stuff */
            info.status_string.clear() ;
            info.status   = -1 ;
            info.progress = 0 ;
            info.signal   = 0 ;
            info.id       = 0 ;
            info.data.clear() ;
            info.signal_handling = 0 ;

            ctrl.idle = false ; /* not idle - for idle log throttle */
            ctrl.done = false ; /* declare the thread as running    */

            daemon_signal_hdlr ();

            /* Block signals */
            sigfillset(&__disabled_mask);

            // sigemptyset(&__enabled_mask); /* maybe not needed */
            pthread_sigmask(SIG_SETMASK, &__disabled_mask, NULL );
            pthread_sigmask(SIG_BLOCK, &__disabled_mask, &__enabled_mask);

            rc = pthread_create(&ctrl.id, &__attr, ctrl.thread, (void*)&info);

            if ( sigismember (&__enabled_mask, SIGINT ) == 0 )
            {
                slog ("%s SIGINT signal was not enabled ; enabling\n", ctrl.hostname.c_str());
                sigaddset(&__enabled_mask, SIGINT);
            }
            if ( sigismember (&__enabled_mask, SIGTERM ) == 0 )
            {
                slog ("%s SIGTERM signal was not enabled ; enabling\n", ctrl.hostname.c_str());
                sigaddset(&__enabled_mask, SIGTERM);
            }
            if ( sigismember (&__enabled_mask, SIGUSR1 ) == 0 )
            {
                slog ("%s SIGUSR1 signal was not enabled ; enabling\n", ctrl.hostname.c_str());
                sigaddset(&__enabled_mask, SIGUSR1);
            }

            /* restore signal mask */
            pthread_sigmask(SIG_SETMASK, &__enabled_mask, NULL );
            pthread_sigmask(SIG_UNBLOCK, &__enabled_mask, NULL );

            /* The above disables signal handling for a short period while a
             * thread is started. In the meantime the only signal that is
             * crutial not to miss is USR1.
             * Work Around: run the USR1 signal handler immediately following
             * the launch just in case it was requested during the launch
             * while the signals were masked. */
            daemon_health_test ();

            if (rc != PASS)
            {
                elog ("%s %s thread launch failed (%d:%d:%m]",
                           ctrl.hostname.c_str(),
                           ctrl.name.c_str(),
                           rc, errno );

                ctrl.status = info.status = FAIL_THREAD_CREATE ;
                _stage_change ( ctrl, THREAD_STAGE__DONE );
            }
            else if ( ctrl.id == 0 )
            {
                elog ("%s %s thread id is null\n",
                          ctrl.hostname.c_str(),
                          ctrl.name.c_str());

                ctrl.status = info.status = FAIL_THREAD_CREATE ;
                _stage_change ( ctrl, THREAD_STAGE__DONE );
            }
            else
            {
                dlog ("%s %s thread launched with command:%d\n",
                          ctrl.hostname.c_str(),
                          ctrl.name.c_str(),
                          info.command );
                ctrl.status = PASS ;

                if ( ctrl.timeout )
                {
                    mtcTimer_start ( ctrl.timer, thread_timer_handler, ctrl.timeout );
                }

                /* start monitoring */
                _stage_change ( ctrl, THREAD_STAGE__MONITOR );
            }
            break ;
        }
        case THREAD_STAGE__MONITOR:
        {
            /* provide subtle indication that the thread ids don't match */
            if (( ctrl.id != info.id ) && ( info.id != 0 ))
            {
                ilog_throttled (ctrl.stage_log_throttle, 50, "%s %s thread [%ld:%ld] monitoring (progress:%d)\n",
                                ctrl.hostname.c_str(),
                                ctrl.name.c_str(),
                                ctrl.id,
                                info.id,
                                info.progress);
            }
#ifdef WANT_THROTTLED_PROGRESS_LOG
            else
            {
                ilog_throttled (ctrl.stage_log_throttle, 50, "%s %s thread monitoring (progress:%d)\n",
                                ctrl.hostname.c_str(),
                                ctrl.name.c_str(),
                                info.progress);
            }
#endif
            if (( ctrl.timeout ) && ( mtcTimer_expired ( ctrl.timer ) ))
            {
                elog ("%s %s thread timeout\n",
                          ctrl.hostname.c_str(),
                          ctrl.name.c_str());

                ctrl.status = FAIL_TIMEOUT ;
                _stage_change ( ctrl, THREAD_STAGE__KILL );
            }
            else if ( info.runcount > ctrl.runcount )
            {
                mtcTimer_reset ( ctrl.timer );

                if ( info.runcount != (ctrl.runcount+1))
                {
                    wlog ("%s %s thread runcount jumped from %d to %d (rc:%u)\n",
                              ctrl.hostname.c_str(),
                              ctrl.name.c_str(),
                              ctrl.runcount,
                              info.runcount,
                              info.status);
                }
                else
                {
                    if ( info.status )
                    {
                        blog ("%s %s thread completed (rc:%u)\n",
                                  ctrl.hostname.c_str(),
                                  ctrl.name.c_str(),
                                  info.status);
                    }
                }
                ctrl.id = 0 ;
                info.id = 0 ;
                _stage_change ( ctrl, THREAD_STAGE__DONE );
            }
            break ;
        }
        case THREAD_STAGE__KILL:
        {
            info.signal = SIGKILL ;
            if ( info.id != 0 )
            {
                wlog ("%s %s thread kill req  (rc:%u)\n",
                          ctrl.hostname.c_str(),
                          ctrl.name.c_str(),
                          info.status);
            }
            if ( ctrl.id != 0 )
            {
                /* Tell the thread ; by way of cancellation points ; to exit
                 *
                 * WARNING: Cannot send a cancel to the thread because if the
                 * thread is already gone then, although the Linux man page says
                 * it will just return an error, in fact after testing with 0 and
                 * invalid numbers, causes the calling process to segfault.
                 * Too dangerous !! Need cooperative exit */
                // pthread_cancel(ctrl.id);
                ctrl.id = 0 ;
            }

            mtcTimer_reset ( ctrl.timer );
            mtcTimer_start ( ctrl.timer, thread_timer_handler, THREAD_POST_KILL_WAIT );
            _stage_change ( ctrl, THREAD_STAGE__WAIT );

            break ;
        }
        case THREAD_STAGE__STAGES:
        default:
        {
            slog ("%s %s has invalid stage ; changing to IDLE\n",
                      ctrl.hostname.c_str(),
                      ctrl.name.c_str() );

            _stage_change ( ctrl , THREAD_STAGE__IDLE );
            rc = FAIL ;
            break ;
        }
    }
    return (rc);
}

/* called by the thread */
void pthread_signal_handler ( thread_info_type * info_ptr )
{
    switch ( info_ptr->signal )
    {
        case SIGKILL:
            ilog ("%s SIGKILL ; exiting ...\n", info_ptr->log_prefix );

            /* avoid touching data after the sigkill is received */
            // info_ptr->data = "thread SIGKILL" ;
            // info_ptr->status = FAIL_THREAD_EXIT ;
            // info_ptr->runcount++ ;
            pthread_exit(&info_ptr->status );
            exit (FAIL_THREAD_EXIT);
            break ;
        default:
            info_ptr->signal_handling++ ;
    }

    /* check for a cancel request - handled internally */
    /* Note: No pint using pthread_testcancel since we don't
     * use pthread_cancel because if the risk of crashing the
     * parent process
     * pthread_testcancel ();
     */
}


pthread_t thread_launch_thread (void*(thread)(void*), void * arg)
{
    pthread_t id ;
    int rc = FAIL ;
    if ( __thread_init_sig == THREAD_INIT_SIG )
    {
        rc = pthread_create(&id, &__attr, thread, (void*)arg);
    }
    else
    {
        slog ("cannot launch thread ; threading not initialized yet\n");
    }
    if ( rc )
        return 0 ;
    return id ;
}

void * thread_test ( void * arg )
{
    UNUSED(arg);
    for ( ; ; )
        sleep (1);

    return NULL ;
}
