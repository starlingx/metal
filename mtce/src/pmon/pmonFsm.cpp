/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Process Monitor Service
  * Passive and Active Monitoring FSMs.
  */

#include "pmon.h"
#include "alarmUtil.h"     /* for ... alarmUtil_getSev_str                 */

#define WARNING_THROTTLE (10)

const char passiveStages_str [PMON_STAGE__STAGES][32] =
{
    "Start",
    "Manage",
    "Respawn",
    "Monitor-Wait",
    "Monitor-Process",
    "Restart-Wait",
    "Ignore-Process",
    "Handler-Finish",
    "Subfunction-Polling",
    "Monitor-Start-Wait",
} ;

const char statusStages_str [STATUS_STAGE__STAGES][32] =
{
    "Begin",
    "Execute-Status",
    "Execute-Status-Wait",
    "Execute-Start",
    "Execute-Start-Wait",
    "Interval-Wait"
} ;

int statusStageChange ( process_config_type * ptr , statusStage_enum newStage )
{
    if ((   newStage < STATUS_STAGE__STAGES ) &&
        ( ptr->status_stage < STATUS_STAGE__STAGES ))
    {
        clog ("%s %s -> %s (%d->%d)\n",
               ptr->process,
               statusStages_str[ptr->status_stage],
               statusStages_str[newStage],
               ptr->status_stage, newStage);
        ptr->status_stage = newStage ;
        return (PASS);
    }
    else
    {
        slog ("%s Invalid Stage (now:%d new:%d)\n", ptr->process, ptr->status_stage, newStage );
        ptr->status_stage = STATUS_STAGE__BEGIN ;
        return (FAIL);
    }
}

int passiveStageChange ( process_config_type * ptr , passiveStage_enum newStage )
{
    if ((   newStage < PMON_STAGE__STAGES ) &&
        ( ptr->stage < PMON_STAGE__STAGES ))
    {
        clog ("%s %s -> %s (%d->%d)\n",
               ptr->process,
               passiveStages_str[ptr->stage],
               passiveStages_str[newStage],
               ptr->stage, newStage);
        ptr->stage = newStage ;
        return (PASS);
    }
    else
    {
        slog ("%s Invalid Stage (now:%d new:%d)\n",
                  ptr->process, ptr->stage, newStage );
        ptr->stage = PMON_STAGE__FINISH ;
        return (FAIL);
    }
}

const char * get_pmonStage_str  ( process_config_type * ptr )
{
    if ( ptr->stage < PMON_STAGE__STAGES )
    {
        return ( &passiveStages_str[ptr->stage][0] ) ;
    }
    return (NULL);
}

const char activeStages_str [ACTIVE_STAGE__STAGES][32] =
{
    "Idle",           /*  0 */
    "Start",          /*  1 */
    "Request",        /*  2 */
    "Wait",           /*  3 */
    "Response",       /*  4 */
    "Gap-Setup",      /*  5 */
    "Gap",            /*  6 */
    "Failed",         /*  7 */
    "Debounce-Setup",
    "Debounce",
    "Finish",
} ;

int activeStageChange ( process_config_type * ptr , activeStage_enum newStage )
{
    if ((   newStage < ACTIVE_STAGE__STAGES ) &&
        ( ptr->active_stage < ACTIVE_STAGE__STAGES ))
    {
        clog ("%s %s -> %s (%d->%d)\n",
               ptr->process,
               activeStages_str[ptr->active_stage],
               activeStages_str[newStage],
               ptr->active_stage, newStage);
        ptr->active_stage = newStage ;
        return (PASS);
    }
    else
    {
        slog ("%s Invalid Stage (now:%d new:%d)\n",
                  ptr->process, ptr->active_stage, newStage );
        ptr->active_stage = ACTIVE_STAGE__PULSE_REQUEST ;
        return (FAIL);
    }
}

const char * get_amonStage_str  ( process_config_type * ptr )
{
    if ( ptr->active_stage < ACTIVE_STAGE__STAGES )
    {
        return ( &activeStages_str[ptr->active_stage][0] ) ;
    }
    return (NULL);
}


void clear_amon_counts (  process_config_type * ptr )
{
    if ( ptr->b2b_miss_count > ptr->b2b_miss_peak )
        ptr->b2b_miss_peak = ptr->b2b_miss_count ;

    if ( ptr->mesg_err_cnt > ptr->mesg_err_peak )
        ptr->mesg_err_peak = ptr->mesg_err_cnt ;

    ptr->b2b_miss_count = 0 ;
    ptr->send_err_cnt   = 0 ;
    ptr->recv_err_cnt   = 0 ;
    ptr->mesg_err_cnt   = 0 ;
}


/* Active Monitoring Handler
 * --------------------------
 * Verifies that the process has an active pid */
int pmon_active_handler ( process_config_type * ptr )
{
    int rc = PASS ;

    if ( ptr->active_stage < ACTIVE_STAGE__STAGES )
    {
        dlog2 ("%s Active-%s Stage %d\n",
                   ptr->process,
                   activeStages_str[ptr->active_stage],
                   ptr->active_stage );
    }
    else
    {
        activeStageChange ( ptr, ACTIVE_STAGE__PULSE_REQUEST );
    }

    switch ( ptr->active_stage )
    {
        case ACTIVE_STAGE__IDLE:
        {
           break ;
        }
        case ACTIVE_STAGE__START_MONITOR:
        {
            rc = open_process_socket ( ptr );
            if ( rc != PASS )
            {
                ptr->active_failed = true ;
                elog ("%s 'open_process_socket' failed (%d)\n", ptr->process , rc );
                manage_process_failure ( ptr );
            }

            activeStageChange ( ptr, ACTIVE_STAGE__GAP_SETUP );
            break ;
        }
        case ACTIVE_STAGE__GAP_SETUP:
        {
            mtcTimer_reset ( ptr->pt_ptr );
            mtcTimer_start ( ptr->pt_ptr, pmon_timer_handler, ptr->period );
            activeStageChange ( ptr, ACTIVE_STAGE__GAP_WAIT );
            break ;
        }
        case ACTIVE_STAGE__GAP_WAIT:
        {
            if ( ptr->pt_ptr->ring == true )
            {
                activeStageChange ( ptr, ACTIVE_STAGE__PULSE_REQUEST );
            }
            break ;
        }
        case ACTIVE_STAGE__FAILED:
        {
            ptr->active_response = false ;
            ptr->active_failed = true ;
            ptr->afailed_count++ ;
            ptr->b2b_miss_count = 0 ;
            mtcTimer_reset ( ptr->pt_ptr );

            manage_process_failure ( ptr );

            /* Stage change is handled else where */
            break ;
        }
        case ACTIVE_STAGE__REQUEST_WAIT:
        {
            if ( ptr->pt_ptr->ring == true )
            {
                activeStageChange ( ptr, ACTIVE_STAGE__PULSE_REQUEST );
            }
            break ;
        }
        case ACTIVE_STAGE__PULSE_REQUEST:
        {
            ptr->waiting = true ;
            if ( amon_send_request ( ptr ) != PASS )
            {
                ptr->waiting = false ;
                ptr->send_err_cnt++ ;
                wlog ("%s pulse request send failed (%d:%d)\n",
                          ptr->process,
                          ptr->b2b_miss_count,
                          ptr->send_err_cnt );
                if ( ++ptr->b2b_miss_count >= ptr->threshold )
                {
                    activeStageChange ( ptr, ACTIVE_STAGE__FAILED );
                }
                else
                {
                    activeStageChange ( ptr, ACTIVE_STAGE__GAP_SETUP );
                }
            }
            else
            {
                ptr->pulse_count++ ;
                mtcTimer_start ( ptr->pt_ptr, pmon_timer_handler, ptr->timeout );
                activeStageChange ( ptr, ACTIVE_STAGE__PULSE_RESPONSE );
            }
            break ;
        }
        case ACTIVE_STAGE__PULSE_RESPONSE:
        {
            if ( ptr->rx_sequence != 0 )
            {
                /* handle the first response */
                if ( ptr->active_response == false )
                {
                    ptr->active_response = true ;
                }

                if ( ptr->rx_sequence != ptr->tx_sequence )
                {
                    ptr->b2b_miss_count++ ;
                    ptr->mesg_err_cnt++ ;
                    wlog ( "%s out-of-sequence response (%d:%d)\n",
                               ptr->process ,
                               ptr->tx_sequence,
                               ptr->rx_sequence);

                    if ( ptr->b2b_miss_count >= ptr->threshold )
                    {
                        activeStageChange ( ptr, ACTIVE_STAGE__FAILED );
                    }
                }
                else
                {
                    if ( ptr->b2b_miss_count > ptr->b2b_miss_peak )
                         ptr->b2b_miss_peak = ptr->b2b_miss_count ;

                    ptr->b2b_miss_count = 0 ;
                    if ( ptr->active_debounce == true )
                    {
                        ilog ("%s is healthy (debouncing)\n", ptr->process );
                    }
                    else
                    {
                        mlog2 ("%s is healthy\n", ptr->process );
                    }
                }
                /* manage active monitoring debounce */
                if ( ptr->active_debounce == true )
                {
                     if ( ++ptr->adebounce_cnt >= ((ptr->period+1)) )
                     {
                         ilog ("%s Debounced  (%d)\n", ptr->process, ptr->pid );
                         ptr->active_debounce = false;
                         ptr->adebounce_cnt   = 0    ;
                         ptr->restarts_cnt = 0 ;
                         ptr->quorum_failure       = false;
                         ptr->quorum_unrecoverable = false;

                         clear_amon_counts ( ptr );
                         ptr->active_failed = false ;
                         manage_alarm ( ptr , PMON_CLEAR );
                     }
                }
                ptr->rx_sequence = 0 ;

                /* Wait out the remaining part of the period */
                ptr->waiting = false ;
            }

            if ( ptr->pt_ptr->ring == true )
            {
                /* Are we still waiting for a response ? */
                if ( ptr->waiting == true )
                {
                    /* handle case where response is delayed due to goenabled */
                    if ( ptr->full_init_reqd &&
                         !( is_goenabled ( get_ctrl_ptr()->nodetype, true ) )
                       )
                    {
                        /* we don't expect a resonse... do nothing to wait
                           another loop */
                    }
                    else
                    {
                        ptr->recv_err_cnt++ ;

                        /* don't log the first single pulse miss. */
                        if ( ptr->b2b_miss_count++ > 1 )
                        {
                            wlog ("%s missing pulse response (Miss:%d) (%d:%d)\n",
                                      ptr->process,
                                      ptr->b2b_miss_count,
                                      ptr->tx_sequence,
                                      ptr->rx_sequence);
                        }
                        if ( ptr->b2b_miss_count >= ptr->threshold )
                        {
                            /*****************************************************
                             * Only fail active heartbeating after MTC_MINS_3 of
                             * never having received a response.
                             *
                             * This condition is added to address an issue
                             * reported where the kernel takes a
                             * long time to timeout on external dns namservers
                             * after a DOR when the system is isolated from the
                             * external network.
                             ****************************************************/
                            if (( ptr->active_response == false ) &&
                                ( ptr->period < MTC_MINS_3 ) &&
                                ( ptr->b2b_miss_count < (MTC_MINS_3/ptr->period )))
                            {
                                ; /* more forgiving startup handling */
                            }
                            else
                            {
                                activeStageChange ( ptr, ACTIVE_STAGE__FAILED );
                                break ;
                            }
                        }
                    }
                }
                activeStageChange ( ptr, ACTIVE_STAGE__PULSE_REQUEST );
                break ;
            }
            else if ( ptr->waiting == false )
            {
                ; /* got the data ; just wait out the timer */
            }
            break ;
        }
        default:
        {
            activeStageChange ( ptr, ACTIVE_STAGE__GAP_SETUP );
            break ;
        }
    }
    return (rc);
}

/* Passive Monitoring Handler
 * --------------------------
 * Verifies that the process has an active pid */
int pmon_passive_handler ( process_config_type * ptr )
{
    int rc = RETRY ;

    if ( ptr->stage < PMON_STAGE__STAGES )
    {
        flog ("%s %s Stage %d\n", ptr->process, passiveStages_str[ptr->stage], ptr->stage );
    }
    else
    {
        slog ("%s Invalid stage (%d) ; correcting\n", ptr->process, ptr->stage );
        passiveStageChange ( ptr, PMON_STAGE__FINISH );
    }

    switch ( ptr->stage )
    {
        case PMON_STAGE__START:
        {
            dlog ( "%s failed:%d severity:%s restarts_cnt:%d debounce_cnt:%d\n",
                       ptr->process,
                       ptr->failed,
                       alarmUtil_getSev_str(ptr->alarm_severity).c_str(),
                       ptr->restarts_cnt,
                       ptr->debounce_cnt);

            ptr->stage_cnt = 0 ;

            break ;
        }
        /* Manage Restart Counts */
        case PMON_STAGE__MANAGE:
        {
            if ( ptr->restart == true )
            {
                pmon_ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
                if ( ctrl_ptr->patching_in_progress == true )
                {
                    /* if patching is in progress and we get a process restart command
                     * then that means the rpms have all been installed already so we
                     * can exit patching in progress state */
                     ctrl_ptr->patching_in_progress = false ;
                }

                ; /* fall through and just change state at the bottom */
            }
            /* Handle Critical processes.
             * Critical with 0 restarts
             * Critical with # restarts
             * Req'ts:
             *   1. Avoid re-reporting the event
             *   2. Send minor for first occurance
             *   3. Try restarts if it supports it
             *   4. Stay in this stage once the max restarts has been reached.
             */
            else if ( ptr->sev == SEVERITY_CRITICAL )
            {
                /* handle the No-restarts case    */
                /* Go straight to event assertion */
                if ( ptr->restarts == 0 )
                {
                    manage_alarm ( ptr, PMON_ASSERT );

                    /* Send critical notification */
                    pmon_send_event ( MTC_EVENT_PMON_CRIT, ptr );

                    wlog ("%s auto-restart disabled\n", ptr->process );
                    passiveStageChange ( ptr, PMON_STAGE__IGNORE ) ;

                    /* if process is in quorum, and we're not trying to restart
                     * it, we declare the quorum failed */
                    if ( ptr->quorum )
                    {
                        quorum_process_failure ( ptr );
                    }
                    break ;
                }
                else if ( ptr->restarts_cnt >= ptr->restarts )
                {
                    manage_alarm ( ptr, PMON_ASSERT );

                    /* Send critical notification */
                    pmon_send_event ( MTC_EVENT_PMON_CRIT, ptr );

                    ptr->restarts_cnt = 0 ;
                    ilog ("%s allowing auto-restart of failed critical process\n", ptr->process);

                    /* if process is in quorum, and we haven't been able to
                     * restart it, we declare the quorum failed */
                    if ( ptr->quorum )
                    {
                        quorum_process_failure ( ptr );
                    }

                    /* Note: the above clear or restarts_cnt and commented break below
                     *       forces pmond to try and continue to recover the failing
                     *       critical process if for some reason the host does not
                     *       go through a reboot */
                    /* avoid stage change below and wait for the reboot */
                    // break ;
                }
                else
                {
                    /* Send a restart log to maintenance on the first restart only */
                    if ( ptr->restarts_cnt == 0 )
                    {
                        manage_alarm ( ptr, PMON_LOG );
                    }

                    /* Try and recover if the process is critical but
                     * supports some number of restart attempts first */
                }
            }
            /* Send a log on the first restart                     */
            /*                                                     */
            /* Note: This clause needs to be before the next one   */
            /*       to handle the restarts = 0 case               */
            else if (( ptr->restarts_cnt == 0 ) && ( ptr->restarts != 0 ))
            {
                ilog ("%s Sending Log Event to Maintenance\n", ptr->process );

                /* Send a log on the first one or every time
                 * we start a fresh restart cycle */
                manage_alarm ( ptr, PMON_LOG );
            }
            else if (( ptr->restarts_cnt == 0 ) && ( ptr->restarts == 0 ))
            {
                /* Auto recovery is disable, generate a log and raise a minor alarm */

                wlog ("%s Sending Log Event to Maintenance\n", ptr->process );
                manage_alarm ( ptr, PMON_LOG );

                manage_alarm ( ptr, PMON_ASSERT );

                wlog ("%s Auto-Restart Disabled ... but monitoring for recovery\n", ptr->process );

                /* if process is in quorum, and we're not trying to
                 * restart it, we declare the quorum failed */
                if ( ptr->quorum )
                {
                    ptr->quorum_failure = true;
                    quorum_process_failure ( ptr );
                }

                passiveStageChange ( ptr, PMON_STAGE__IGNORE ) ;
                break ;
            }
            /* Manage notification based on restart
             * threshold for non-critical processes */
            else if ( ptr->restarts_cnt >= ptr->restarts )
            {
                /* Restart threshold reached ; sending event to maintenance */
                manage_alarm ( ptr, PMON_ASSERT );

                /* Start the counts again */
                ptr->restarts_cnt = 0 ;
                ptr->debounce_cnt = 0 ;

                /* if process is in quorum, and we haven't been able to
                 * restart it, we declare the quorum failed */
                if ( ptr->quorum )
                {
                    quorum_process_failure ( ptr );
                }
            }
            passiveStageChange ( ptr, PMON_STAGE__RESPAWN ) ;
            break ;
        }

        /* Spawn the process */
        case PMON_STAGE__RESPAWN:
        {
            ilog ("%s stability period (%d secs)\n", ptr->process, ptr->debounce );

            /* Restart the process */
            respawn_process ( ptr ) ;

            /* Start the monitor debounce timer. */
            mtcTimer_reset ( ptr->pt_ptr );

            /* Don't wait for the debounce timer to take this process out of 'commanded restart' mode.
             * Do it now, otherwise tight patch loop stress testing might fail */
            if ( ptr->restart == true )
            {
                ilog ("%s Restarted\n", ptr->process )
                ptr->restart = false ;
                ptr->registered = false ;
                passiveStageChange ( ptr, PMON_STAGE__MANAGE ) ;
            }
            else
            {
                mtcTimer_start ( ptr->pt_ptr, pmon_timer_handler, ptr->startuptime );
                passiveStageChange ( ptr, PMON_STAGE__MONITOR_WAIT ) ;
            }
            break ;
        }

        /* Give the process time to startup
         * before trying to monitor it */
        case PMON_STAGE__MONITOR_WAIT:
        {
            /* Give the process time to start */
            if ( ptr->pt_ptr->ring == true )
            {
                if (( !ptr->sigchld_rxed ) || ( !ptr->child_pid ) || ( ptr->status ))
                {
                    if ( ptr->child_pid == 0 )
                    {
                        elog ("%s spawn has null child pid\n", ptr->process );
                    }
                    else if ( ptr->sigchld_rxed == false )
                    {
                        elog ("%s spawn timeout (%d)\n", ptr->process, ptr->child_pid );
                    }
                    else if ( ptr->status != PASS )
                    {
                        elog ("%s spawn failed (rc:%d) (%d)\n", ptr->process, ptr->status, ptr->child_pid );
                    }
                    kill_running_child ( ptr ) ;

                    /* we had a startup timeout ; do restart */
                    mtcTimer_start( ptr->pt_ptr, pmon_timer_handler, ptr->interval );
                    passiveStageChange ( ptr, PMON_STAGE__RESTART_WAIT ) ;
                }
                else
                {
                    /* clear the monitor debounce counter */
                    ptr->debounce_cnt = 0 ;

                    /* Start debounce monitor phase */
                    passiveStageChange ( ptr, PMON_STAGE__MONITOR ) ;
                    process_running ( ptr );
                    ilog ("%s Monitor    (%d)\n", ptr->process, ptr->pid );
                }

                ptr->sigchld_rxed = false ;
            }
            break ;
        }
         /* Monitor the newly respawned process */
        case PMON_STAGE__MONITOR:
        {
            /* The process needs to stay running for x seconds before
             * clearing any assertion or declaring that this restart
             * attempt was successful */

            /* The process should be running.
             * If not then cancel the timer and start over through
             * the RESTART_WAIT stage which ensures that we manage
             * back to back restarts properly */
            if ( ! process_running ( ptr ) )
            {
                wlog ("%s Respawn Monitor Failed (%d of %d), retrying in (%d secs)\n",
                          ptr->process,
                          ptr->restarts_cnt,
                          ptr->restarts,
                          ptr->interval);

                passiveStageChange ( ptr, PMON_STAGE__TIMER_WAIT ) ;
            }
            else if ( ptr->pt_ptr->ring == true )
            {
                if ( ++ptr->debounce_cnt >= ptr->debounce )
                {
                    /* We made it through the monitor debounce
                     * period so lets finish up */
                    ilog ("%s Stable     (%d)\n", ptr->process, ptr->pid );
                    passiveStageChange ( ptr, PMON_STAGE__FINISH ) ;
                }
                /* else continue to monitor the freshly respawned process */
                else
                {
                    /* Start the monitor timer again since
                     * the debounce period is not over */
                    mtcTimer_start  ( ptr->pt_ptr, pmon_timer_handler, 1 );
                    dlog ("%s Debounce Monitor (TID:%p)\n", ptr->process, ptr->pt_ptr->tid );
                }
            }
            break ;
        }

        case PMON_STAGE__TIMER_WAIT:
        {
            if ( mtcTimer_expired ( ptr->pt_ptr ) )
            {
                /* if restart interval is zero then just ring the timer right away */
                if ( ptr->interval == 0 )
                {
                   ptr->pt_ptr->ring = true ;
                }
                else
                {
                    /* Now we are in the restart wait phase */
                    mtcTimer_start( ptr->pt_ptr, pmon_timer_handler, ptr->interval );
                }

                kill_running_child ( ptr ) ;
                passiveStageChange ( ptr, PMON_STAGE__RESTART_WAIT ) ;
            }
            else
            {
                dlog ("%s debounce timer wait\n", ptr->process);
            }
            break ;
        }
        /* Lets wait a bit before we try another restart */
        case PMON_STAGE__RESTART_WAIT:
        {
            if ( ptr->pt_ptr->ring == true )
            {
                /* Force the immediate (re)start */
                passiveStageChange ( ptr, PMON_STAGE__MANAGE) ;
            }
            break ;
        }
        /* A state that leaves a process failed but takes it out of
         * that failed state if it auto recovers on its own or
         * through external means */
        case PMON_STAGE__IGNORE:
        {
            int pid ;
            if ((pid = get_process_pid ( ptr )))
            {
                int result = kill (pid, 0 );
                if ( result == 0 )
                {
                    /* allow process recovery if it is started outside pmond */
                    if ( ptr->stopped == true )
                        ptr->stopped = false ;

                    passiveStageChange ( ptr, PMON_STAGE__FINISH );
                }
            }
            break ;
        }
        case PMON_STAGE__FINISH:
        {
            kill_running_child ( ptr ) ;
            ilog ("%s Recovered  (%d)\n", ptr->process, ptr->pid );

            /* Set all counts to default state ;
             * Even if they may have already been :) */
            ptr->failed = false ;
            ptr->debounce_cnt = 0 ;

            passiveStageChange ( ptr, PMON_STAGE__START ) ;

            /* Register the new process with the kernel */
            register_process ( ptr );

            if ( !ptr->active_monitoring )
            {
                ptr->restarts_cnt = 0 ;
                /* It's possible that a restart succeeded even after the
                 * max restarts threshold was reached (and we thought things
                 * were dead, so we marked quorum processes as unrecoverable)
                 */
                if ( ptr->quorum )
                {
                    ptr->quorum_failure       = false;
                    ptr->quorum_unrecoverable = false;
                }

                manage_alarm ( ptr, PMON_CLEAR );
            }
            /* Recover Active monitoring ,
             * event clear will occur in the active monitoring
             * FSM after it passes the debouce cycle */
            else
            {
                /* Open the process's active monitoring
                 * socket if it was or is closed */
                if ( ptr->msg.tx_sock == 0 )
                    open_process_socket ( ptr );

                /* Clear sequence*/
                ptr->tx_sequence       = 0     ;
                ptr->rx_sequence       = 0     ;

                /* Clear active monitoring state controls */
                // ptr->active_failed  = false ;
                ptr->waiting        = false ;

                /* Set the active monitor debounce flag and clear its counter */
                ptr->active_debounce = true ;
                ptr->adebounce_cnt   = 0    ;

                activeStageChange ( ptr, ACTIVE_STAGE__PULSE_REQUEST ) ;
            }
            rc = PASS ;
            break ;
        }
        /******************************************************************************
         *
         *  This polling stage was introduced for the introduction of the 2-Server
         *  configuration, aka combo blade/host.
         *
         *  /etc/pmon.d/<process.conf> files that declare
         *
         *  subfunction = worker
         *  or
         *  subfunction = storage
         *
         *  .. are not immediately monitored by pmond on startup.
         *
         *  Instead, pmond will wait for the specified subfunction config complete
         *  file to be present before starting to monitor that process.
         *
         *  This stage is here to manage that delayed monitoring startup of
         *  subfunction dependent processes.
         *
         *******************************************************************************/
        case PMON_STAGE__POLLING:
        {
            if ( ptr->pt_ptr->ring == false )
            {
                break ;
            }
            else
            {
                string config_filename = "" ;
                pmon_ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
                if ( ptr->subfunction )
                {
                    if ( !strcmp (ptr->subfunction, "worker" ) )
                    {
                        config_filename = CONFIG_COMPLETE_WORKER ;
                    }
                    else if ( !strcmp (ptr->subfunction, "storage" ) )
                    {
                        config_filename = CONFIG_COMPLETE_STORAGE ;
                    }
                    /********************************************************
                     * issue: processes that set the subfunction to
                     *            'last-config' get a dependency override in
                     *            the AIO system. Such processes need to be
                     *            monitored only after the last configuration
                     *            step. Right now that is worker in aio.
                     *
                     ********************************************************/
                    else if (( ctrl_ptr->system_type != SYSTEM_TYPE__NORMAL ) &&
                             ( !strcmp (ptr->subfunction, "last-config" )))
                    {
                        config_filename = CONFIG_COMPLETE_WORKER ;
                        dlog ("%s dependency over-ride ; will wait for %s\n",
                                  ptr->process,
                                  config_filename.c_str());
                    }
                }

                if ( config_filename.empty() )
                {
                    passiveStageChange ( ptr, PMON_STAGE__IGNORE );
                    elog ("%s is subfunction polling with no subfunction ; ignoring\n", ptr->process );
                }
                else
                {
                    bool start_monitoring = true;
                    string waiting_for = "";

                    if ( daemon_is_file_present ( config_filename.data() ) != true )
                    {
                        start_monitoring = false;
                        waiting_for = config_filename;
                    }
                    else if ( !strcmp (ptr->subfunction, "worker" ) )
                    {
                        if ( daemon_is_file_present ( DISABLE_WORKER_SERVICES ) == true )
                        {
                            /* Compute services are disabled - do not start monitoring */
                            start_monitoring = false;
                            waiting_for = DISABLE_WORKER_SERVICES;
                        }
                    }

                    mtcTimer_reset ( ptr->pt_ptr );
                    if ( start_monitoring == true )
                    {
                        ptr->passive_monitoring = true ;

                        /* check for startup failures from alarm query. */
                        if ( ptr->failed == true )
                        {
                            /* manage the process if its in the failed state */
                            passiveStageChange ( ptr, PMON_STAGE__MANAGE );
                        }
                        else
                        {
                            ilog ("monitor start of %s in %d seconds\n", ptr->process, daemon_get_cfg_ptr()->start_delay );
                            mtcTimer_start ( ptr->pt_ptr, pmon_timer_handler, daemon_get_cfg_ptr()->start_delay  );
                            passiveStageChange ( ptr, PMON_STAGE__START_WAIT );
                        }
                    }
                    else
                    {
                        mtcTimer_start ( ptr->pt_ptr, pmon_timer_handler, 3 );
                        wlog_throttled ( ptr->stage_cnt, 500, "%s monitoring is waiting on %s\n",
                                         ptr->process, waiting_for.c_str());
                    }
                }
            }
            break ;
        }
        case PMON_STAGE__START_WAIT:
        {
            if ( ptr->pt_ptr->ring == true )
            {
                ilog ("%s process monitoring started\n", ptr->process );
                register_process ( ptr );
                if ( ptr->active_monitoring == true )
                {
                    if ( open_process_socket ( ptr ) != PASS )
                    {
                        elog ("%s failed to open process socket\n",
                                 ptr->process );
                    }
                }
                passiveStageChange ( ptr, PMON_STAGE__MANAGE );
            }
            break ;
        }

        default:
        {
            slog ("%s Invalid stage (%d)\n", ptr->process, ptr->stage );

            /* Default to finish for invalid case.
             * If there is an issue then it will be detected */
            passiveStageChange ( ptr, PMON_STAGE__FINISH );
        }
    }
    return (rc);
}

/* Status Monitoring Handler
 * --------------------------
 * Monitors a process with status command */
int pmon_status_handler ( process_config_type * ptr )
{
    if ( ptr->status_stage >= STATUS_STAGE__STAGES )
    {
        wlog ("%s Invalid status_stage (%d) ; correcting\n", ptr->process, ptr->status_stage );
        statusStageChange ( ptr, STATUS_STAGE__BEGIN);
    }

    switch ( ptr->status_stage )
    {
        // First state
        case STATUS_STAGE__BEGIN:
        {
            mtcTimer_start  ( ptr->pt_ptr, pmon_timer_handler, ptr->period );
            dlog ("%s start period timer  %p\n", ptr->process,  ptr->pt_ptr->tid );
            statusStageChange ( ptr, STATUS_STAGE__EXECUTE_STATUS );
            break ;
        }

        // Execute the status command
        case STATUS_STAGE__EXECUTE_STATUS:
        {
            if ( ptr->pt_ptr->ring == true ) //wake up from period
            {
                ptr->status = PASS;
                mtcTimer_start  ( ptr->pt_ptr, pmon_timer_handler, ptr->timeout );
                dlog ("%s start the status command timer %p\n", ptr->process,  ptr->pt_ptr->tid );

                // Execute the status call
                int rc = execute_status_command(ptr);
                if (rc != PASS)
                {
                   elog ("%s execute_status_command returned a failure (%d)\n", ptr->process,  rc);
                   ptr->status = rc;
                }

                statusStageChange ( ptr, STATUS_STAGE__EXECUTE_STATUS_WAIT );
            }
            break ;
        }

        // Wait for the status command to finish and process results
        case STATUS_STAGE__EXECUTE_STATUS_WAIT:
        {
            // Give the command time to execute. The daemon_sigchld_hdlr will force
            // a ring when the command execute successfully or returns a failure
            if ( (ptr->pt_ptr->ring == true) || (ptr->status != PASS ) )
            {
                mtcTimer_reset( ptr->pt_ptr);
                ptr->pt_ptr->ring = false;

                if (( !ptr->sigchld_rxed ) || ( !ptr->child_pid ) || (ptr->status != PASS))
                {
                    if ( ptr->child_pid == 0 )
                    {
                        elog ("%s status command has null child pid\n", ptr->process );
                    }
                    else if ( ptr->sigchld_rxed == false )
                    {
                        elog ("%s status command execution timed out (%d)\n", ptr->process, ptr->child_pid );
                        kill_running_process ( ptr->child_pid );
                    }

                    elog ("%s status returned a failure (rc:%d) ; process(es) start pending\n", ptr->process, ptr->status );

                    // Go to execute start state since we do not know the status of the process
                    ptr->status_failed = true;
                    ptr->was_failed = true ;
                    statusStageChange ( ptr, STATUS_STAGE__EXECUTE_START );
                }
                else
                {
                    // Status reports everything is ok, reset variables
                    dlog ("%s status command was successful\n", ptr->process);
                    ptr->restarts_cnt = 0;

                    if ( ptr->failed == true )
                    {
                        manage_alarm ( ptr, PMON_CLEAR );
                    }
                    ptr->status_failed = false;
                    ptr->failed = false;
                    statusStageChange ( ptr, STATUS_STAGE__BEGIN );

                }

                ptr->child_pid = 0;
                ptr->sigchld_rxed = false;
            }
            break;
        }

        // Interval wait time before doing a start again if the start
        // had previously failed
        case STATUS_STAGE__INTERVAL_WAIT:
        {
            if (ptr->pt_ptr->ring == true)
            {
               statusStageChange ( ptr, STATUS_STAGE__EXECUTE_START );
            }
            break ;
        }

        // Execute the start command
        case STATUS_STAGE__EXECUTE_START:
        {
            ptr->status = PASS;
            mtcTimer_start  ( ptr->pt_ptr, pmon_timer_handler, ptr->timeout );
            dlog ("%s start the start command timer %p\n", ptr->process,  ptr->pt_ptr->tid );

            int rc = execute_start_command (ptr);
            if (rc != PASS)
            {
               elog ("%s execute_start_command returned a failure (%d)\n", ptr->process, rc);
               ptr->status = rc;
            }
            statusStageChange ( ptr, STATUS_STAGE__EXECUTE_START_WAIT );
            break;
        }

        // Wait for the start command to finish and process results
        case STATUS_STAGE__EXECUTE_START_WAIT:
        {
            // Give the command time to execute. The daemon_sigchld_hdlr will force
            // a ring when the command execute successfully or returns a failure
            if ( (ptr->pt_ptr->ring == true) || (ptr->status != PASS) )
            {
                mtcTimer_reset( ptr->pt_ptr);
                ptr->pt_ptr->ring = false;

                // If the status had failed then ptr->status_failed will be set to true. Status failure
                // will also cause restarts count increment, alarm and degrade state
                if (( !ptr->sigchld_rxed ) || ( !ptr->child_pid ) || ( ptr->status ) || (ptr->status_failed))
                {
                    if ( ptr->child_pid == 0 )
                    {
                        elog ("%s start command has null child pid\n", ptr->process );
                    }
                    else if ( ptr->sigchld_rxed == false )
                    {
                        elog ("%s start command execution timed out (%d)\n", ptr->process, ptr->child_pid );
                        kill_running_process ( ptr->child_pid );
                    }
                    else if ( ptr->status != PASS )
                    {
                        elog ("%s start command returned a failure (rc:%d)\n", ptr->process, ptr->status);
                    }

                    /* Send a log on the first failure                     */
                    if (( ptr->restarts_cnt == 0 ) && ( ptr->restarts != 0 ) )
                    {
                        wlog ("%s Sending Log Event to Maintenance\n", ptr->process );

                        /* Send a log on the first one or every time we start a fresh restart cycle */
                        manage_alarm ( ptr, PMON_LOG );
                    }

                    /* Manage notification based on restart */
                    else if ( ptr->restarts_cnt >= ptr->restarts )
                    {
                        wlog ("%s Failure threshold (%d) reached ; alarming\n", ptr->process, ptr->restarts );
                        manage_alarm ( ptr, PMON_ASSERT );
                        ptr->failed = true;   // this is used to degrade un-degrade the host
                        ptr->restarts_cnt = 0 ;
                    }
                    else
                    {
                        wlog ("%s has %d of %d failures ; retrying ...\n",
                                  ptr->process,
                                  ptr->restarts_cnt,
                                  ptr->restarts );
                    }

                    ptr->restarts_cnt++;
                    ptr->failed_cnt++ ;

                    //only want to check for status false on first restart iteration so reset the flag
                    ptr->status_failed = false;

                    // Go to interval state only if start failed otherwise we want to check
                    // the process status again
                    if (( !ptr->sigchld_rxed ) || ( !ptr->child_pid ) || ( ptr->status ))
                    {
                        // In here because the start failed

                        // Wait the interval time and then execute a start command again
                        mtcTimer_start  ( ptr->pt_ptr, pmon_timer_handler, ptr->interval );
                        dlog ("%s start interval timer %p\n", ptr->process,  ptr->pt_ptr->tid );
                        statusStageChange ( ptr, STATUS_STAGE__INTERVAL_WAIT );
                    }
                    else
                    {
                        // In here because status failed but start was successful

                        wlog ("%s start command was successful ; here because status had failed\n", ptr->process);
                        statusStageChange ( ptr, STATUS_STAGE__BEGIN );
                    }
                }
                else
                {
                    // Start was successful
                    wlog ("%s start command was successful\n", ptr->process);
                    statusStageChange ( ptr, STATUS_STAGE__BEGIN );
                }

                ptr->child_pid = 0;
                ptr->sigchld_rxed = false;
            }
            break;
        }

        default:
        {
            elog ("%s invalid status_stage (%d)\n", ptr->process, ptr->status_stage );

            /* Default to first state for invalid case. there is an issue then it will be detected */
            statusStageChange ( ptr, STATUS_STAGE__BEGIN );
        }
    }
    return (PASS);
}
