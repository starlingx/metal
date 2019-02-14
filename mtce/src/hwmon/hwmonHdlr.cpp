/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Process Monitor Service Handler
  */

#include "daemon_ini.h"

#include "nodeBase.h"     /* for ... mtce common definitions          */
#include "jsonUtil.h"     /* for ... json utilities                   */
#include "regexUtil.h"    /* for ... regexUtil_pattern_match          */
#include "tokenUtil.h"    /* for ... tokenUtil_new_token              */
#include "nodeUtil.h"     /* for ... mtce common utilities            */
#include "ipmiUtil.h"     /* for ... IPMI utilties                    */
#include "hwmon.h"        /* for ... service module header            */
#include "hwmonUtil.h"    /* for ... utilities, ie clear_logged_state */
#include "hwmonClass.h"   /* for ... service class definition         */
#include "hwmonIpmi.h"    /* for ... QUANTA_SENSOR_PROFILE_CHECKSUM  */
#include "hwmonSensor.h"  /* for ... this mpodule header              */
#include "hwmonHttp.h"    /* for ... hwmonHttp_mod_group              */
#include "hwmonAlarm.h"   /* for ... hwmonAlarm_major                 */

/* Declare the Hardware Monitor Inventory Object */
hwmonHostClass hostInv ;

/* Public interface to get the Hardware Monitor Inventory object */
hwmonHostClass * get_hwmonHostClass_ptr ( void )
{
    return (&hostInv);
}

/* Preserve a local copy of a pointer to the control struct to
 * avoid having to publish a get utility prototype into hwmon.h */
static hwmon_ctrl_type * _hwmon_ctrl_ptr = NULL ;

/* hwmonTimer_audit - get_events periodic audit timer */
static struct mtc_timer hwmonTimer_audit ;
static struct mtc_timer hwmonTimer_token ;

/** List of server profile files */
std::list<string> profile_files ;
std::list<string>::iterator string_iter_ptr ;


 /*****************************************************************************
 *
 * Name       : _stage_change
 *
 * Description: Change the sensor monitor FSM stage.
 *
 ****************************************************************************/

static std::string monitorStages_str[HWMON_SENSOR_MONITOR__STAGES+1];
void _stage_change ( string hostname, monitor_ctrl_stage_enum & nowStage, monitor_ctrl_stage_enum newStage )
{
    if ( newStage < HWMON_SENSOR_MONITOR__STAGES )
    {
        clog ("%s sensor monitor stage change from %s -> %s\n",
                  hostname.c_str(),
                  monitorStages_str[nowStage].c_str(),
                  monitorStages_str[newStage].c_str());
        nowStage = newStage ;
    }
    else
    {
        slog ("%s sensor monitor stage change to '%d' is invalid ; switching to START\n",
                  hostname.c_str(),
                  newStage );
        nowStage = HWMON_SENSOR_MONITOR__START ;
    }
}

/*******************************************************************
 *          Module Initialize and Finalizes Interfaces             *
 *******************************************************************/

/* Initial init of timers. */
/* Not run on a sighup     */
void hwmon_timer_init ( void )
{
    mtcTimer_init ( hwmonTimer_audit, "controller", "audit timer" ) ;
    mtcTimer_init ( hwmonTimer_token, "controller", "token timer") ;
}

/* Register realtime signal handler with the kernel */
int signal_hdlr_init ( int sig_num )
{
    int rc ;
    UNUSED(sig_num) ;

#ifdef WANT_MORE_SIGNAL_HANDLING
    memset (&_pmon_ctrl_ptr->info, 0, sizeof(_pmon_ctrl_ptr->info));
    memset (&_pmon_ctrl_ptr->prev, 0, sizeof(_pmon_ctrl_ptr->info));

    _pmon_ctrl_ptr->info.sa_sigaction = _process_death_hdlr ;
    _pmon_ctrl_ptr->info.sa_flags = (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_SIGINFO) ;

    rc = sigaction ( sig_num, &_pmon_ctrl_ptr->info , &_pmon_ctrl_ptr->prev );
    if ( rc )
    {
        elog("Registering Realtime Signal %d - (%d) (%s)\n",
              sig_num, errno, strerror(errno));
        rc = FAIL_SIGNAL_INIT ;
    }
    else
    {
        ilog("Registering Realtime Signal %d\n", sig_num);
    }
#else
    rc = PASS ;
#endif
    return (rc) ;
}

/*
 * Init the handler
 *    - Must support re-init that might occur over a SIGHUP
 **/
int hwmon_hdlr_init ( hwmon_ctrl_type * ctrl_ptr )
{
    int rc = PASS ;

    /* Save the control pointer */
    _hwmon_ctrl_ptr = ctrl_ptr ;

    monitorStages_str[HWMON_SENSOR_MONITOR__START]   = "Start" ;
    monitorStages_str[HWMON_SENSOR_MONITOR__DELAY]   = "Delay" ;
    monitorStages_str[HWMON_SENSOR_MONITOR__READ]    = "Read"  ;
    monitorStages_str[HWMON_SENSOR_MONITOR__PARSE]   = "Parse" ;
    monitorStages_str[HWMON_SENSOR_MONITOR__CHECK]   = "Check" ;
    monitorStages_str[HWMON_SENSOR_MONITOR__UPDATE]  = "Update";
    monitorStages_str[HWMON_SENSOR_MONITOR__HANDLE]  = "Handle";
    monitorStages_str[HWMON_SENSOR_MONITOR__FAIL]    = "Fail"  ;
    monitorStages_str[HWMON_SENSOR_MONITOR__POWER]   = "Power Query" ;
    monitorStages_str[HWMON_SENSOR_MONITOR__RESTART] = "Restart" ;
    monitorStages_str[HWMON_SENSOR_MONITOR__IDLE]    = "Idle"  ;

    return (rc) ;
}


/* Module Cleanup */
void hwmon_hdlr_fini ( hwmon_ctrl_type * ctrl_ptr )
{
    UNUSED(ctrl_ptr) ;
}

/*******************************************************************
 *                   Module Utilities                              *
 ******************************************************************/
/* SIGCHLD handler support - for waitpid */
void daemon_sigchld_hdlr ( void )
{
    dlog3 ("Received SIGCHLD ...\n");
}


/* Looks up the timer ID and asserts the corresponding ringer */
void hwmonHostClass::timer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;
    struct hwmonHostClass::hwmon_host * hwmon_host_ptr ;

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    UNUSED(sig);
    UNUSED(uc);

    if ( tid_ptr == NULL )
    {
        return ;
    }
    else if ( *tid_ptr == NULL )
    {
        return ;
    }

    /* Audit Timer */
    else if ( *tid_ptr == hwmonTimer_audit.tid )
    {
        hwmonTimer_audit.ring = true ;
        return ;
    }
    /* Token refresh Timer */
    else if ( *tid_ptr == hwmonTimer_token.tid )
    {
        mtcTimer_stop_int_safe ( hwmonTimer_token );
        hwmonTimer_token.ring = true ;
        return ;
    }
    else
    {
        hwmon_host_ptr = getHost_timer ( *tid_ptr ) ;
        if ( hwmon_host_ptr )
        {
            if (( *tid_ptr == hwmon_host_ptr->monitor_ctrl.timer.tid ) )
            {
                mtcTimer_stop_int_safe ( hwmon_host_ptr->monitor_ctrl.timer );
                hwmon_host_ptr->monitor_ctrl.timer.ring = true ;
                return ;
            }
            else if (( *tid_ptr == hwmon_host_ptr->ipmitool_thread_ctrl.timer.tid ) )
            {
                mtcTimer_stop_int_safe ( hwmon_host_ptr->ipmitool_thread_ctrl.timer );
                hwmon_host_ptr->ipmitool_thread_ctrl.timer.ring = true ;
                return ;
            }
            else if (( *tid_ptr == hwmon_host_ptr->ping_info.timer.tid ) )
            {
                mtcTimer_stop_int_safe ( hwmon_host_ptr->ping_info.timer );
                hwmon_host_ptr->ping_info.timer.ring = true ;
                return ;
            }
            else if (( *tid_ptr == hwmon_host_ptr->hostTimer.tid ) )
            {
                mtcTimer_stop_int_safe ( hwmon_host_ptr->hostTimer );
                hwmon_host_ptr->hostTimer.ring = true ;
                return ;
            }
            else if (( *tid_ptr == hwmon_host_ptr->addTimer.tid ) )
            {
                mtcTimer_stop_int_safe ( hwmon_host_ptr->addTimer );
                hwmon_host_ptr->addTimer.ring = true ;
                return ;
            }
            else if (( *tid_ptr == hwmon_host_ptr->relearnTimer.tid ) )
            {
                mtcTimer_stop_int_safe ( hwmon_host_ptr->relearnTimer );
                hwmon_host_ptr->relearnTimer.ring = true ;
                hwmon_host_ptr->relearn = false ;
                return ;
            }
        }
    }
    mtcTimer_stop_tid_int_safe (tid_ptr);
}

#ifdef WANT_SENSOR_TOGGLE
bool toggle = false ;
#endif

void hwmon_service ( hwmon_ctrl_type * ctrl_ptr )
{
    std::list<int> socks ;
    struct timeval waitd;
    fd_set readfds;

    daemon_config_type * config_ptr = daemon_get_cfg_ptr();
    hwmon_socket_type  * sock_ptr   = getSock_ptr();

    hostInv.hostBase.my_hostname = ctrl_ptr->my_hostname ;
    hostInv.hostBase.my_local_ip = ctrl_ptr->my_local_ip ;
    hostInv.hostBase.my_float_ip = ctrl_ptr->my_float_ip ;

    if ( config_ptr->token_refresh_rate )
    {
        if ( config_ptr->token_refresh_rate < 300 )
        {
            ilog ("Starting 'Token' Refresh timer (%d seconds)\n",
                  (config_ptr->token_refresh_rate) );
        }
        else
        {
            ilog ("Starting 'Token' Refresh timer (%d minutes)\n",
                  (config_ptr->token_refresh_rate/60) );
        }
        if ( mtcTimer_start ( hwmonTimer_token,
                              hwmonTimer_handler,
                              config_ptr->token_refresh_rate ) != PASS )
        {
            elog ("Failed to start 'Token' Refresh Timer\n");
            daemon_exit ( ) ;
        }
    }

    // client_len = sizeof(client_addr);

    socks.clear();
    if ( sock_ptr->cmd_sock )
    {
        socks.push_front (sock_ptr->cmd_sock->getFD());
    }
    else
    {
        elog ("cannot service Null cmd_sock\n");
    }

    socks.sort();

    ilog ("Starting 'Audit' timer (%d secs)\n", ctrl_ptr->audit_period );
    mtcTimer_start ( hwmonTimer_audit, hwmonTimer_handler, ctrl_ptr->audit_period );

    for ( ; ; )
    {
        /* Initialize the master fd_set */
        FD_ZERO(&readfds);

        /* add the command receiver socket ro the FD set mask */
        if ( sock_ptr->cmd_sock )
        {
            if ( sock_ptr->cmd_sock->getFD())
            {
                FD_SET(sock_ptr->cmd_sock->getFD(), &readfds);
            }
            else
            {
                /* force a re-init if we have no FD */
                sock_ptr->cmd_sock->sock_ok(false);
            }
        } /* Null sockts are auto recovered below */

        waitd.tv_sec  = 0;
        waitd.tv_usec = (SOCKET_WAIT*3) ;

        /* This is used as a delay up to select_timeout */
        int rc = select( socks.back()+1, &readfds, NULL, NULL, &waitd);

        /* If the select time out expired then  */
        if (( rc < 0 ) || ( rc == 0 ))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                elog ( "Select Failed (rc:%d) %s \n", errno, strerror(errno));
            }
        }
        else if ( FD_ISSET(sock_ptr->cmd_sock->getFD(), &readfds))
        {
            rc = hwmon_service_inbox ();
            if ( rc > RETRY )
            {
                elog ("Failure servicing inbox (rc:%d)\n", rc);
            }
        }
        else
        {
            wlog ("unexpected select (%d)\n", rc );
        }

        if ( hwmonTimer_audit.ring == true )
        {
            mtcTimer_dump_data ();
            hostInv.set_degrade_audit();
            hwmonTimer_audit.ring = false ;

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__HWMON__AVOID_TOKEN_REFRESH ))
            {
                if ( hwmonTimer_token.ring == true )
                    hwmonTimer_token.ring = false ;
            }
#endif
        }

        /* Handle refreshing the authentication token */
        tokenUtil_log_refresh ();
        tokenUtil_manage_token ( ctrl_ptr->httpEvent,
                                 ctrl_ptr->my_hostname,
                                 config_ptr->token_refresh_rate,
                                 hwmonTimer_token,
                                 hwmonTimer_handler );

        /* Run the FSM */
        hostInv.hwmon_fsm ( ) ;

        daemon_signal_hdlr ();

        daemon_load_fit ( );
    }
}

/* Add Host Handler
 * ---------------------------*/
int hwmonHostClass::add_host_handler ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    switch ( host_ptr->addStage )
    {
        case HWMON_ADD__WAIT:
        {
            if ( mtcTimer_expired ( host_ptr->addTimer ))
            {
                host_ptr->addTimer.ring = false ;
                addStageChange ( host_ptr , HWMON_ADD__START );
            }
            break ;
        }
        case HWMON_ADD__START:
        {
            /* force load of sensors from database if sensors = 0 and they exist */
            int rc = hwmonHostClass::ipmi_load_sensor_model ( host_ptr ) ;
            if ( rc == PASS )
            {
                mtcTimer_start ( host_ptr->addTimer, hwmonTimer_handler, 1);
                addStageChange (host_ptr, HWMON_ADD__STATES);
            }
            else
            {
                /* there might be issue accessing the sysinv database */
                int delay = (rand()%30)+1 ;
                wlog ("%s ipmi_load_sensor_model failed (rc:%d) ; retrying in %d secs\n", host_ptr->hostname.c_str(), rc , delay);
                mtcTimer_start ( host_ptr->addTimer, hwmonTimer_handler, delay );
                addStageChange ( host_ptr , HWMON_ADD__WAIT );
            }
            break ;
        }
        case HWMON_ADD__STATES:
        {
            if ( mtcTimer_expired ( host_ptr->addTimer ))
            {
                if ( host_ptr->sensors )
                {
                    int rc ;
                    /* manage the alarm and degrade states of all the sensors over process
                     * startup when the sensor model is already found in the database ;
                     * typical case over process restart. */
                    if (( rc = manage_startup_states ( host_ptr ) ) == PASS )
                    {
                        /* run the audit right away just to update the host degrade state
                         * if it needs it ; like over a SWACT */
                        degrade_state_audit ( host_ptr ) ;

                        ilog ("%s add complete (groups:%d sensors:%d)\n", host_ptr->hostname.c_str(), host_ptr->groups, host_ptr->sensors );
                    }
                    else
                    {
                        int delay = (rand()%30)+1 ;
                        if ( host_ptr->alarmed_config == false )
                        {
                            host_ptr->alarmed_config = true ;
                            hwmonAlarm_minor ( host_ptr->hostname, HWMON_ALARM_ID__SENSORCFG, "profile", REASON_DEGRADED );
                        }
                        wlog ("%s manage_startup_states failed (rc:%d) ; retrying in %d secs\n", host_ptr->hostname.c_str(), rc, delay );
                        mtcTimer_start ( host_ptr->addTimer, hwmonTimer_handler, delay );
                        break ;
                    }
                }
                else
                {
                    ilog ("%s no sensor model in database ; must be learned\n",
                              host_ptr->hostname.c_str());
                }
                addStageChange ( host_ptr , HWMON_ADD__DONE );
            }
            break ;
        }
        case HWMON_ADD__DONE:
        {
            ilog ("%s add complete ; %d sensors %d groups\n", host_ptr->hostname.c_str(), host_ptr->sensors, host_ptr->groups );
            break ;
        }
        default:
        {
            slog ("%s invalid 'add' stage\n", host_ptr->hostname.c_str() );
            if ( host_ptr->addTimer.tid ) mtcTimer_stop ( host_ptr->addTimer );
            mtcTimer_start ( host_ptr->addTimer, hwmonTimer_handler, (rand()%10)+1);
            addStageChange ( host_ptr , HWMON_ADD__DONE );
            break ;
        }
    }
    return (PASS);
}

/* Inventory Object wrapper - does a node lookup and calls the timer handler */
void hwmonTimer_handler ( int sig, siginfo_t *si, void *uc)
{
    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr() ;
    obj_ptr->timer_handler ( sig, si, uc );
}

/*****************************************************************************
 *
 * Name       : interval_change_handler
 *
 * Purpose:   : Handles setting the monitoring audit interval.
 *
 * Description: The following conditions are handled.
 *
 *              if  host_ptr->interval is zero then it and all the groups
 *              are set to the default value.
 *
 *              If there is existing inventory then host_ptr->interval
 *              is set to the shortest group interval.
 *
 *              With no existing inventory all groups are set to
 *              HWMON_DEFAULT_AUDIT_INTERVAL
 *
 *              if host_ptr->interval is not zero then all the group intervals
 *              are set to that value.
 *
 *****************************************************************************/

int hwmonHostClass::interval_change_handler ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = RETRY ;

    dlog ("%s interval change handler\n", host_ptr->hostname.c_str());

    /* Don't issue a request if there is one active already */
    if ( host_ptr->event.base == NULL )
    {
        rc = PASS ;

        if ( host_ptr->interval < HWMON_MIN_AUDIT_INTERVAL )
        {
            ilog ("%s setting audit interval\n", host_ptr->hostname.c_str());
            if ( host_ptr->groups )
            {
                int smallest = HWMON_DEFAULT_LARGE_INTERVAL ;

                /* get the smallest interval */
                for ( int g = 0 ; g < host_ptr->groups ; ++g )
                {
                    if ( smallest > host_ptr->group[g].group_interval )
                    {
                        smallest = host_ptr->group[g].group_interval ;
                    }
                }
                /* Should be no bigger than the smallest group interval setting. */
                host_ptr->interval = smallest ;
            }
            else
            {
                /* default first 'learning' audit interval */
                host_ptr->interval = 5 ;
            }
        }

        if (( host_ptr->relearn == true ) &&
            ( host_ptr->model_attributes_preserved.interval != host_ptr->interval ))
        {
            host_ptr->interval = host_ptr->model_attributes_preserved.interval ;
            ilog ("%s audit interval restored to %d seconds\n",
                         host_ptr->hostname.c_str(),
                         host_ptr->interval);
        }

        string interval_string = itos(host_ptr->interval) ;

        for ( int g = 0 ; g < host_ptr->groups ; ++g )
        {
            daemon_signal_hdlr();


            if ( host_ptr->interval != host_ptr->group[g].group_interval )
            {
                /* only updat the group if they differ */
                if ( host_ptr->group[g].group_interval != host_ptr->interval )
                {
                    /* update the group interval. Even though ipmi
                     * montoring does not need it, we need to be
                     * backwards compatible.
                     *
                     * ipmi monitors all groups at the same interval */
                    int old = host_ptr->group[g].group_interval ;
                    host_ptr->group[g].group_interval = host_ptr->interval ;

                    rc = hwmonHttp_mod_group ( host_ptr->hostname,
                                               host_ptr->event,
                                               host_ptr->group[g].group_uuid,
                                               "audit_interval_group",
                                               interval_string );

                    if ( rc )
                    {
                         elog ("%s failed to update '%s' group audit interval (%d of %d); will retry later\n",
                                   host_ptr->hostname.c_str(),
                                   host_ptr->group[g].group_name.c_str(),
                                   g, host_ptr->groups );
                         break ;
                    }
                    else
                    {
                        char str [100] ;
                        snprintf ( &str[0], 100, "audit interval changed from %d to %d seconds",
                                   old,
                                   host_ptr->group[g].group_interval);

                        hwmonLog ( host_ptr->hostname,
                                   HWMON_ALARM_ID__SENSORGROUP,
                                   FM_ALARM_SEVERITY_CLEAR,
                                   host_ptr->group[g].group_name, str );
                    }
                }
            }
        }
        /* retry until pass - retries are spaced by audit interval */
        if ( rc == PASS )
        {
            /* TODO: remove error detection and correction */
            if ( host_ptr->interval == 0 )
            {
                slog ("%s failed to set interval correctly\n",host_ptr->hostname.c_str());

                host_ptr->interval = HWMON_DEFAULT_AUDIT_INTERVAL ;
            }

            host_ptr->interval_changed = false ;
        }
    }

    ilog ("%s sensor monitoring period is %d seconds\n",
              host_ptr->hostname.c_str(),
              host_ptr->interval );

    return (rc);
}


/* Hardware Monitor Handler
 * --------------------------
 *
 * TODO: Need grouping to enable the groups in the database
 *     group_ptr->group_state = "enabled" ;
 *     hwmonHttp_mod_group ( host_ptr->hostname, host_ptr->event , group_ptr->group_uuid, "state" , group_ptr->group_state );
 *     if ( group_ptr->group_state.compare("enabled") )
 * TODO: Need grouping disabled on state transition from monitoring enabled to disabled
 *
 *
 *  */
int hwmonHostClass::ipmi_sensor_monitor ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = RETRY ;

    if ( host_ptr )
    {
        /* Check the stage */
        if ( host_ptr->monitor_ctrl.stage < HWMON_SENSOR_MONITOR__STAGES )
        {
            flog ("%s sensor monitor stage (%s)\n",
                       host_ptr->hostname.c_str(),
                       monitorStages_str[host_ptr->monitor_ctrl.stage].c_str());
        }
        else
        {
            slog ("%s bad sensor monitor state (%d) - forcing into IDLE\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->monitor_ctrl.stage);

            _stage_change ( host_ptr->hostname,
                            host_ptr->monitor_ctrl.stage,
                            HWMON_SENSOR_MONITOR__START );
        }

        /* check for a new model relearn request */
        if ( host_ptr->relearn_request == true )
        {
            int relearn_time = MTC_MINS_1 ;

            /* gracefully handle delete model failure retry.
             * if there is a relearn timer running then wait for it
             * to expire. This way previously failed relear request
             * retries are throttled. */
            if ( mtcTimer_expired ( host_ptr->relearnTimer ) == false )
            {
                /* TODO: test FIT */
                return (RETRY);
            }

            ilog ("%s handling sensor model relearn request\n",
                      host_ptr->hostname.c_str());

            rc = ipmi_delete_sensor_model ( host_ptr );
            if ( rc != PASS )
            {
                elog ("%s delete model failure ; retry in %d seconds\n",
                          host_ptr->hostname.c_str(), relearn_time );

                /* If we got an error then wait relearn_time
                 * before trying again */
                mtcTimer_start ( host_ptr->relearnTimer,
                                 hwmonTimer_handler,
                                 relearn_time );
                return (RETRY);
            }

            relearn_time = MTC_MINS_5 ;

            /* enter relearn mode */
            host_ptr->relearn = true ;

            /* exit relearn request mode.
             * allow the relearn operation to proceed */
            host_ptr->relearn_request = false ;

            host_ptr->relearn_done_date = future_time ( relearn_time );
            ilog ("%s next relearn permitted after %s\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->relearn_done_date.c_str());

           this->monitor_soon ( host_ptr );

           /* start the relearn timer */
           mtcTimer_start ( host_ptr->relearnTimer,
                            hwmonTimer_handler,
                            relearn_time );
        }

        switch ( host_ptr->monitor_ctrl.stage )
        {
            /******************************************************************
             *
             * The IDLE stage is the default start and do nothing stage while
             * monitoring is disabled.
             *
             * Stage Transition: external
             *
             ******************************************************************/
            case HWMON_SENSOR_MONITOR__IDLE:
            {
                break ;
            }

            /******************************************************************
             *
             * A delayed START
             *
             *****************************************************************/
            case HWMON_SENSOR_MONITOR__RESTART:
            {
                if ( mtcTimer_expired ( host_ptr->monitor_ctrl.timer ) )
                {
                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__START );
                }
                break ;
            }


            /******************************************************************
             *
             * The START stage is the default stage and starts sensor
             * monitoring if enabled for this host.
             *
             * The start process begins with adding a small randomized delay
             * before the first READ so that over a process (re)start we don't
             * jolt the process by trying to read sensors from all hosts at the
             * same time.
             *
             * Stage Transition:
             *
             *  Success path -> HWMON_SENSOR_MONITOR__DELAY
             *  Failure Path -> HWMON_SENSOR_MONITOR__IDLE
             *
             ******************************************************************/
            case HWMON_SENSOR_MONITOR__START:
            {
                mtcTimer_reset ( host_ptr->monitor_ctrl.timer );

                if ( host_ptr->monitor )
                {
                   /* Handle Audit Interval Change */
                    if ( host_ptr->interval_changed )
                    {
                        interval_change_handler ( host_ptr );
                    }

                    /* Handle power state query
                     * - don't depend on poweron if in relearn mode.
                     * - otherwise we need to ensure the model is learned
                     *   while the host power is on.
                     * See comments in HWMON_SENSOR_MONITOR__POWER for details */
                    if (( host_ptr->sensors == 0 ) &&
                        ( host_ptr->poweron == false ) &&
                        ( host_ptr->relearn == false ))
                    {
                        if ( host_ptr->ipmitool_thread_ctrl.id )
                        {
                            wlog ("%s sensor monitor thread is unexpectedly active ; retry soon\n", host_ptr->hostname.c_str());
                            thread_kill ( host_ptr->ipmitool_thread_ctrl, host_ptr->ipmitool_thread_info );
                            sleep (1);
                            break ;
                        }

                        host_ptr->accounting_bad_count = 0 ;
                        host_ptr->ipmitool_thread_ctrl.id = 0       ;
                        host_ptr->ipmitool_thread_ctrl.done = false ;

                        host_ptr->ipmitool_thread_info.data.clear() ;
                        host_ptr->ipmitool_thread_info.status_string.clear();
                        host_ptr->ipmitool_thread_info.status = -1  ;
                        host_ptr->ipmitool_thread_info.progress = 0 ;
                        host_ptr->ipmitool_thread_info.id = 0       ;
                        host_ptr->ipmitool_thread_info.signal = 0   ;
                        host_ptr->ipmitool_thread_info.command = IPMITOOL_THREAD_CMD__POWER_STATUS ;

                        /* Update / Setup the BMC query credentials */
                        host_ptr->thread_extra_info.bm_ip = host_ptr->bm_ip ;
                        host_ptr->thread_extra_info.bm_un = host_ptr->bm_un ;
                        host_ptr->thread_extra_info.bm_pw = host_ptr->bm_pw ;

                        rc = thread_launch ( host_ptr->ipmitool_thread_ctrl,
                                             host_ptr->ipmitool_thread_info ) ;
                        if ( rc != PASS )
                        {
                            host_ptr->ipmitool_thread_info.status = rc ;
                            host_ptr->ipmitool_thread_info.status_string =
                            "failed to launch power query thread" ;

                            _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__FAIL );
                        }
                        else
                        {
                            /* Assign the extra data pointer */
                            host_ptr->ipmitool_thread_info.extra_info_ptr = (void*)&host_ptr->thread_extra_info ;

                            /* start an umbrella timer 5 seconds longer than
                             * the default thread FSM timout */
                            mtcTimer_start ( host_ptr->monitor_ctrl.timer,
                                             hwmonTimer_handler,
                                             (DEFAULT_THREAD_TIMEOUT_SECS+5) );

                            _stage_change ( host_ptr->hostname,
                                            host_ptr->monitor_ctrl.stage,
                                            HWMON_SENSOR_MONITOR__POWER );
                        }
                        break ;
                    }
                    else if ( host_ptr->interval )
                    {
                        /* Assign the extra data pointer */
                        host_ptr->ipmitool_thread_info.extra_info_ptr = (void*)&host_ptr->thread_extra_info ;

                        /* randomize the first audit a little so that over a swact we don't spike hwmond */
                        int r = (rand() % host_ptr->interval) + 1 ;

                        /* poll all the sensors right away - between 1 and 10 seconds */
                        ilog ("%s sensor monitoring begins in %d seconds\n",
                                  host_ptr->hostname.c_str(), r );

                        mtcTimer_start ( host_ptr->monitor_ctrl.timer, hwmonTimer_handler, r );
                        _stage_change  ( host_ptr->hostname,
                                         host_ptr->monitor_ctrl.stage,
                                         HWMON_SENSOR_MONITOR__DELAY );
                        break ;
                    }
                    else
                    {
                        host_ptr->interval_changed = true ;
                        wlog ("%s audit interval is zero ; auto correcting\n", host_ptr->hostname.c_str());
                        break ;
                    }
                }
                else
                {
                    ilog ("%s sensor monitoring disabled\n", host_ptr->hostname.c_str());
                }

                _stage_change ( host_ptr->hostname,
                                host_ptr->monitor_ctrl.stage,
                                HWMON_SENSOR_MONITOR__IDLE );
                break ;
            }


            /******************************************************************
             *
             * The POWER stage handles a power query response.
             *
             * The START is re-invoked if the power query fails or
             * shows that the power is off.
             *
             * Stage Transition:
             *
             *  Success path -> HWMON_SENSOR_MONITOR__DELAY
             *  Failure Path -> HWMON_SENSOR_MONITOR__START
             *
             ******************************************************************/
            case HWMON_SENSOR_MONITOR__POWER:
            {
                /* handle thread execution umbrella timeout */
                if ( mtcTimer_expired ( host_ptr->monitor_ctrl.timer ) )
                {
                    host_ptr->monitor_ctrl.timer.ring = false ;

                    wlog ("%s power query thread timeout\n",
                              host_ptr->hostname.c_str());

                    thread_kill ( host_ptr->ipmitool_thread_ctrl, host_ptr->ipmitool_thread_info );
                }

                /* check for 'thread done' completion */
                else if ( thread_done( host_ptr->ipmitool_thread_ctrl ) )
                {
                    /* Consume done results */
                    mtcTimer_reset ( host_ptr->monitor_ctrl.timer );

                    if ( host_ptr->ipmitool_thread_info.status )
                    {
                        elog ("%s %s thread %2d failed (rc:%d) (%d:%d)\n",
                                  host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                  host_ptr->ipmitool_thread_ctrl.name.c_str(),
                                  host_ptr->ipmitool_thread_info.command,
                                  host_ptr->ipmitool_thread_info.status,
                                  host_ptr->ipmitool_thread_info.progress,
                                  host_ptr->ipmitool_thread_info.runcount);

                        wlog ("%s ... %s\n",
                                  host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                  host_ptr->ipmitool_thread_info.status_string.c_str());
                    }
                    else
                    {
                        dlog ("%s '%s' thread '%d' command is done ; (%d:%d) (rc:%d)\n",
                                  host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                  host_ptr->ipmitool_thread_ctrl.name.c_str(),
                                  host_ptr->ipmitool_thread_info.command,
                                  host_ptr->ipmitool_thread_info.progress,
                                  host_ptr->ipmitool_thread_info.runcount,
                                  host_ptr->ipmitool_thread_info.status);

                        blog2("%s ... status: %s\n",
                                  host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                  host_ptr->ipmitool_thread_info.status_string.c_str());

#ifdef WANT_FIT_TESTING
                        if ( daemon_want_fit ( FIT_CODE__HWMON__NO_DATA, host_ptr->hostname ))
                        {
                            host_ptr->ipmitool_thread_info.data.clear ();
                            host_ptr->ipmitool_thread_info.status = 0 ;
                            host_ptr->ipmitool_thread_info.status_string.clear ();
                            slog ("%s FIT No Power Status Data\n", host_ptr->hostname.c_str());
                        }
#endif

                        if ( host_ptr->ipmitool_thread_info.data.empty())
                        {
                            wlog ("%s power query status empty ; retrying query\n",
                                      host_ptr->hostname.c_str());
                        }
                        else if ( host_ptr->ipmitool_thread_info.data.find (IPMITOOL_POWER_ON_STATUS) == string::npos )
                        {
                            ilog ("%s %s\n", host_ptr->hostname.c_str(),
                                             host_ptr->ipmitool_thread_info.data.c_str());

                            wlog ("%s sensor learning delayed ; need power on\n",
                                      host_ptr->hostname.c_str());
                        }
                        else
                        {
                            ilog ("%s %s\n", host_ptr->hostname.c_str(),
                                             host_ptr->ipmitool_thread_info.data.c_str());

                            /* OK, this is what we have been waiting for */
                            host_ptr->poweron = true ;
                        }
                    }

                    host_ptr->ipmitool_thread_ctrl.done = true ;

                    if ( host_ptr->poweron == false )
                    {
                        mtcTimer_start ( host_ptr->monitor_ctrl.timer,
                                         hwmonTimer_handler, MTC_MINS_1 );

                        _stage_change ( host_ptr->hostname,
                                        host_ptr->monitor_ctrl.stage,
                                        HWMON_SENSOR_MONITOR__RESTART );
                    }
                    else
                    {
                        mtcTimer_start ( host_ptr->monitor_ctrl.timer,
                                         hwmonTimer_handler, MTC_MINS_2 );

                        _stage_change ( host_ptr->hostname,
                                        host_ptr->monitor_ctrl.stage,
                                        HWMON_SENSOR_MONITOR__RESTART );
                    }
                }
                break ;
            }

            /******************************************************************
             *
             * The DELAY stage inserts time after a failure recovery or
             * between successive sensor READ intervals.
             *
             * The failure path is invoked if the 'thread' stage is not IDLE
             * when the DELAY period expires.
             *
             * Stage Transition:
             *
             *  Success path -> HWMON_SENSOR_MONITOR__READ
             *  Failure Path -> HWMON_SENSOR_MONITOR__FAIL
             *
             ******************************************************************/
            case HWMON_SENSOR_MONITOR__DELAY:
            {
                if ( mtcTimer_expired ( host_ptr->monitor_ctrl.timer ) )
                {
                    host_ptr->monitor_ctrl.timer.ring = false ;

                    /* if there was a previous connection failure being handled
                     * then give it time to resolve */
                    if ( !thread_idle ( host_ptr->ipmitool_thread_ctrl ) )
                    {
                        wlog ("%s rejecting thread run stage change ; FSM not IDLE (thread stage:%s)\n",
                                  host_ptr->hostname.c_str(),
                                  thread_stage(host_ptr->ipmitool_thread_ctrl).c_str());

                        _stage_change ( host_ptr->hostname,
                                        host_ptr->monitor_ctrl.stage,
                                        HWMON_SENSOR_MONITOR__FAIL );
                    }
                    else
                    {
                        _stage_change ( host_ptr->hostname,
                                        host_ptr->monitor_ctrl.stage,
                                        HWMON_SENSOR_MONITOR__READ );
                    }
                }
                /* Handle Audit Interval Change ...
                 * While we are waiting for the next audit check to see if we have received
                 * an monitor interval change. If we have then update the database with the
                 * new data, force this interval to finish and on the next audit the new
                 * interval will be loaded */
                else if ( host_ptr->interval_changed )
                {
                    interval_change_handler ( host_ptr );

                    /* force this audit interval to expire but don't include this in the
                     * pass case only. Give sysinv it some time before the next retry */
                    mtcTimer_stop ( host_ptr->monitor_ctrl.timer );
                    host_ptr->monitor_ctrl.timer.ring = true ;
                }
                break ;
            }


            /******************************************************************
             *
             * The READ stage requests the launch of the hwmonThread_ipmitool
             * thread that will read the sensor data from the specified host.
             *
             * An umbrella timeout timer is started on behalf of the PARSE
             * stage to detect threadUtil FSM not completing.
             *
             * Launch will fail if attempted if the thread is already running
             * or if the launch request returns a failure.
             *
             * Stage Transition:
             *
             *  Success path -> HWMON_SENSOR_MONITOR__PARSE
             *  Failure Path -> HWMON_SENSOR_MONITOR__FAIL
             *
             ******************************************************************/
            case HWMON_SENSOR_MONITOR__READ:
            {
                if ( host_ptr->ipmitool_thread_ctrl.id )
                {
                    host_ptr->ipmitool_thread_info.status = FAIL_THREAD_RUNNING ;
                    host_ptr->ipmitool_thread_info.status_string =
                    "sensor monitor thread is unexpectedly active ; handling as failure" ;
                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__FAIL );
                    break ;
                }

                host_ptr->accounting_bad_count = 0 ;
                host_ptr->ipmitool_thread_ctrl.id = 0       ;
                host_ptr->ipmitool_thread_ctrl.done = false ;

                host_ptr->ipmitool_thread_info.data.clear() ;
                host_ptr->ipmitool_thread_info.status_string.clear();
                host_ptr->ipmitool_thread_info.status = -1  ;
                host_ptr->ipmitool_thread_info.progress = 0 ;
                host_ptr->ipmitool_thread_info.id = 0       ;
                host_ptr->ipmitool_thread_info.signal = 0   ;
                host_ptr->ipmitool_thread_info.command = IPMITOOL_THREAD_CMD__READ_SENSORS ;

                /* Update / Setup the BMC query credentials */
                host_ptr->thread_extra_info.bm_ip = host_ptr->bm_ip ;
                host_ptr->thread_extra_info.bm_un = host_ptr->bm_un ;
                host_ptr->thread_extra_info.bm_pw = host_ptr->bm_pw ;


                rc = thread_launch ( host_ptr->ipmitool_thread_ctrl, host_ptr->ipmitool_thread_info ) ;
                if ( rc != PASS )
                {
                    host_ptr->ipmitool_thread_info.status = rc ;
                    host_ptr->ipmitool_thread_info.status_string =
                    "failed to launch sensor monitoring thread" ;

                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__FAIL );
                }
                else
                {
                    /* start an umbrella timer 5 seconds longer than
                     * the default thread FSM timout */
                    mtcTimer_start ( host_ptr->monitor_ctrl.timer,
                                     hwmonTimer_handler,
                                     (DEFAULT_THREAD_TIMEOUT_SECS+5) );

                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__PARSE );
                }
                break ;
            }

            /******************************************************************
             * The PARSE stage has 2 main functions
             *
             *  1. Wait for the ipmitool command completion from the READ stage
             *     while monitoring for and handling the unbrella timeout case.
             *
             *  2. PARSE the sensor data json string into the sample list
             *
             *          sample[MAX_HOST_SENSORS]
             *
             *     The number of sensors read by thread is specified in
             *
             *         thread_extra_info.samples
             *
             * Failure case is invoked for
             *  - thread completion umbrella timeout.
             *  - thread completion error
             *  - sensor data parse error
             *
             * Stage Transition:
             *
             *  Success path -> HWMON_SENSOR_MONITOR__CHECK
             *  Failure Path -> HWMON_SENSOR_MONITOR__FAIL
             *
             ******************************************************************/
            case HWMON_SENSOR_MONITOR__PARSE:
            {
                daemon_signal_hdlr ();

                /* Unbrella timeout timer check */
                if ( mtcTimer_expired ( host_ptr->monitor_ctrl.timer ) )
                {
                    host_ptr->monitor_ctrl.timer.ring = false ;
                    host_ptr->ipmitool_thread_info.status = FAIL_TIMEOUT ;
                    host_ptr->ipmitool_thread_info.status_string =
                    "timeout waiting for sensor read data" ;

                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__FAIL );
                }

                /* check for 'thread done' completion */
                else if ( thread_done( host_ptr->ipmitool_thread_ctrl ) )
                {
                    /* Consume done results */
                    mtcTimer_stop ( host_ptr->monitor_ctrl.timer );

                    if ( host_ptr->ipmitool_thread_info.status ) // == FAIL_SYSTEM_CALL )
                    {
                        if ( ++host_ptr->ipmitool_thread_ctrl.retries < MAX_THREAD_RETRIES )
                        {
                            elog ("%s %s thread %2d failed (rc:%d) (try %d of %d) (%d:%d)\n",
                                      host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                      host_ptr->ipmitool_thread_ctrl.name.c_str(),
                                      host_ptr->ipmitool_thread_info.command,
                                      host_ptr->ipmitool_thread_info.status,
                                      host_ptr->ipmitool_thread_ctrl.retries,
                                      MAX_THREAD_RETRIES,
                                      host_ptr->ipmitool_thread_info.progress,
                                      host_ptr->ipmitool_thread_info.runcount);

                            /* don't flood the logs with the same error data over and over */
                            if ( host_ptr->ipmitool_thread_ctrl.retries == 1 )
                            {
                                blog ("%s ... %s\n",
                                          host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                          host_ptr->ipmitool_thread_info.status_string.c_str());
                            }

                            host_ptr->ipmitool_thread_ctrl.done = true ;
                            mtcTimer_start ( host_ptr->monitor_ctrl.timer, hwmonTimer_handler, THREAD_RETRY_DELAY_SECS );
                            _stage_change ( host_ptr->hostname,
                                            host_ptr->monitor_ctrl.stage,
                                            HWMON_SENSOR_MONITOR__DELAY );
                            break ;
                        }
#ifdef WANT_THIS
                        /* don't flood the logs with the same error data over and over */
                        if ( host_ptr->ipmitool_thread_ctrl.retries > 1 )
                        {
                            wlog ("%s %s thread '%d' command is done ; (%d:%d) (rc:%d)\n",
                                      host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                      host_ptr->ipmitool_thread_ctrl.name.c_str(),
                                      host_ptr->ipmitool_thread_info.command,
                                      host_ptr->ipmitool_thread_info.progress,
                                      host_ptr->ipmitool_thread_info.runcount,
                                      host_ptr->ipmitool_thread_info.status);
                            blog ("%s ... data: %s\n",
                                      host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                      host_ptr->ipmitool_thread_info.status_string.c_str());
                        }
#endif
                    }
                    else
                    {
                        dlog ("%s '%s' thread '%d' command is done ; (%d:%d) (rc:%d)\n",
                                  host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                  host_ptr->ipmitool_thread_ctrl.name.c_str(),
                                  host_ptr->ipmitool_thread_info.command,
                                  host_ptr->ipmitool_thread_info.progress,
                                  host_ptr->ipmitool_thread_info.runcount,
                                  host_ptr->ipmitool_thread_info.status);
                        blog2 ("%s ... data: %s\n",
                                  host_ptr->ipmitool_thread_ctrl.hostname.c_str(),
                                  host_ptr->ipmitool_thread_info.status_string.c_str());
                    }
                    host_ptr->ipmitool_thread_ctrl.done = true ;
                    host_ptr->ipmitool_thread_ctrl.retries = 0 ;

#ifdef WANT_FIT_TESTING
                    if ( daemon_want_fit ( FIT_CODE__HWMON__NO_DATA, host_ptr->hostname ))
                    {
                        host_ptr->ipmitool_thread_info.data.clear ();
                        host_ptr->ipmitool_thread_info.status = 0 ;
                        host_ptr->ipmitool_thread_info.status_string.clear ();
                    }
#endif

                    if ( host_ptr->ipmitool_thread_info.status == PASS )
                    {
                        /* NOTE: This parsing method is not leaking memory ; verified ! */

                        json_bool status ;
                        struct json_object * req_obj = (struct json_object *)(NULL) ;
                        struct json_object * raw_obj = json_tokener_parse( host_ptr->ipmitool_thread_info.data.data() );
                        if ( raw_obj )
                        {
                            /* Look for ... IPMITOOL_JSON__SENSOR_DATA_MESSAGE_HEADER */
                            status = json_object_object_get_ex ( raw_obj, IPMITOOL_JSON__SENSOR_DATA_MESSAGE_HEADER, &req_obj );
                            if (( status == TRUE ) && req_obj )
                            {
                                char * msg_ptr = (char*)json_object_to_json_string(req_obj) ;
                                host_ptr->json_ipmi_sensors = msg_ptr ;
                                if ( msg_ptr )
                                {
                                    host_ptr->ipmitool_thread_info.status = ipmi_load_sensor_samples ( host_ptr , msg_ptr);
                                    if ( host_ptr->ipmitool_thread_info.status == PASS )
                                    {
                                        if ( host_ptr->samples != host_ptr->sensors )
                                        {
                                            if ( host_ptr->quanta_server == false )
                                            {
                                                ilog ("%s read %d sensor samples but expected %d\n",
                                                          host_ptr->hostname.c_str(),
                                                          host_ptr->samples,
                                                          host_ptr->sensors );
                                            }
                                        }
                                        _stage_change ( host_ptr->hostname, host_ptr->monitor_ctrl.stage, HWMON_SENSOR_MONITOR__CHECK );

                                    }
                                    else
                                    {
                                        host_ptr->ipmitool_thread_info.status_string = "failed to load sensor data" ;
                                    }
                                }
                                else
                                {
                                    host_ptr->ipmitool_thread_info.status_string = "failed to get json message after header" ;
                                    host_ptr->ipmitool_thread_info.status = FAIL_JSON_PARSE ;
                                }
                            }
                            else
                            {
                                host_ptr->ipmitool_thread_info.status_string = "failed to find '" ;
                                host_ptr->ipmitool_thread_info.status_string.append(IPMITOOL_JSON__SENSOR_DATA_MESSAGE_HEADER);
                                host_ptr->ipmitool_thread_info.status_string.append("' label") ;
                                host_ptr->ipmitool_thread_info.status = FAIL_JSON_PARSE ;
                            }
                        }
                        else
                        {
                            host_ptr->ipmitool_thread_info.status_string = "failed to parse ipmitool sensor data string" ;
                            host_ptr->ipmitool_thread_info.status = FAIL_JSON_PARSE ;
                        }

                        if (raw_obj) json_object_put(raw_obj);
                        if (req_obj) json_object_put(req_obj);
                    }

                    if ( host_ptr->ipmitool_thread_info.status )
                    {
                        /* Handle thread error status */
                        if ( host_ptr->groups == 0 )
                        {
                            if ( host_ptr->alarmed_config == false )
                            {
                                host_ptr->alarmed_config = true ;
                                hwmonAlarm_minor ( host_ptr->hostname, HWMON_ALARM_ID__SENSORCFG, "profile", REASON_DEGRADED );
                            }
                        }
                        else
                        {
                            ipmi_set_group_state ( host_ptr, "failed" );
                        }

                        _stage_change ( host_ptr->hostname,
                                        host_ptr->monitor_ctrl.stage,
                                        HWMON_SENSOR_MONITOR__FAIL );
                    }
                } /* end handling of done command */
                break ;
            }

            /******************************************************************
             *
             * The CHECK stage is run on the last parsed sample data loaded
             * into the temporary sample sensor data list ...
             *
             *      host_ptr->sample[MAX_HOST_SENSORS]
             *
             *  The number of samples loaded into the sample is
             *  specified in
             *
             *      host_ptr->samples
             *
             *  The CHECK is intended to identify sensor data corruption or
             *  model changes that might occur over a BMC firmware upgrade.
             *
             *  The CHECK involves performing a checksum of all the sensor
             *  names in each list and comparing that checksum to the last
             *  time the sensors were read.
             *
             *  A stored checksum of zero indicates the first sample read.
             *  If at that time host_ptr->sensors == 0 then a call to
             *  ipmi_create_sensor_model is made to create a new sensor
             *  model based on these last sample readings.
             *
             *  If the stored checksums do not match the current checksums
             *  then that constitutes a sensor mismatch with a design log.
             *  The mismatch counter is incremented. If the mismatch
             *  counter exceeds its threshold then the current sensor model
             *  is deleted and re-created using the new data.
             *
             *  A customer log is created whenever a host's sensor model
             *  is created or re-created.
             *
             * Stage Transition:
             *
             *  Success path -> HWMON_SENSOR_MONITOR__UPDATE
             *  Failure Path -> HWMON_SENSOR_MONITOR__FAIL
             *
             *********************************************************************/
            case HWMON_SENSOR_MONITOR__CHECK:
            {
                unsigned short temp_checksum ;

                daemon_signal_hdlr ();

                /* Handle cases where we got an incomplete sensor reading */
                if ( host_ptr->thread_extra_info.samples == 0 )
                {
                    if ( host_ptr->ipmitool_thread_info.status == PASS )
                    {
                        host_ptr->ipmitool_thread_info.status        = FAIL_INVALID_DATA ;
                        host_ptr->ipmitool_thread_info.status_string = "incomplete sensor data reading" ;
                    }
                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__FAIL );
                    break ;
                }

                /* get the checksum for this sample set */
                temp_checksum =
                checksum_sample_profile ( host_ptr->hostname,
                                          host_ptr->thread_extra_info.samples,
                                         &host_ptr->sample[0]);

                blog1 ("%s samples profile checksum : %04x:%04x (%d:%d:%d)\n",
                           host_ptr->hostname.c_str(),
                           temp_checksum,
                           host_ptr->sample_sensor_checksum,
                           host_ptr->samples,
                           host_ptr->sensors,
                           host_ptr->thread_extra_info.samples);

                /* Initialize the sample checksums and counts for the first reading case */
                if ( host_ptr->sample_sensor_checksum == 0 )
                {
                    // host_ptr->samples = host_ptr->thread_extra_info.samples ;
                    host_ptr->sample_sensor_checksum = temp_checksum ;
                }

                /* look for first sensor reading case with an empty database profile.
                 * This can occur over a fresh provisioning or a model recreation */
                if ( host_ptr->sensors == 0 )
                {
                    ilog ("%s samples profile checksum : %04x (%d sensors) (%d samples)\n",
                              host_ptr->hostname.c_str(),
                              host_ptr->sample_sensor_checksum,
                              host_ptr->sensors,
                              host_ptr->samples);

                    /* check the sample model against known Quanta Server profile checksums and sensor numbers */
                    if (((( host_ptr->sample_sensor_checksum  == QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_53 ) || ( host_ptr->sample_sensor_checksum ==  QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_50 )) &&
                         (( host_ptr->samples == QUANTA_SAMPLE_PROFILE_SENSORS_VER_13_53) || (QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_50 ))) ||
                        (( host_ptr->sample_sensor_checksum == QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13___ )) ||
                        (( host_ptr->sample_sensor_checksum == QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_53b )) ||
                        (( host_ptr->sample_sensor_checksum == QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_47 ) && ( host_ptr->samples == QUANTA_SAMPLE_PROFILE_SENSORS_VER_13_47 )) ||
                        (( host_ptr->sample_sensor_checksum == QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_42 ) && ( host_ptr->samples == QUANTA_SAMPLE_PROFILE_SENSORS_VER_13_42 )) ||
                        (( host_ptr->sample_sensor_checksum == QUANTA_SAMPLE_PROFILE_CHECKSUM_VER__3_29 ) && ( host_ptr->samples == QUANTA_SAMPLE_PROFILE_SENSORS_VER__3_29 )))
                    {
                        /* TODO: can also add search for missing sensors */
                        ilog ("%s -----------------------------------------------\n", host_ptr->hostname.c_str());
                        ilog ("%s is a Quanta server based on sensor sample data\n", host_ptr->hostname.c_str());
                        ilog ("%s -----------------------------------------------\n", host_ptr->hostname.c_str());
                        host_ptr->quanta_server = true ;
                    }

                    /* Create a sensor model from 'this' sample data */
                    if ( ipmi_create_sensor_model ( host_ptr ) != PASS )
                    {
                        elog ("%s failed to create sensor model (in sysinv)\n",
                                  host_ptr->hostname.c_str());
                    }
                }

                if ( host_ptr->profile_sensor_checksum == 0 )
                {
                    host_ptr->profile_sensor_checksum =
                    checksum_sensor_profile ( host_ptr->hostname,
                                              host_ptr->sensors,
                                             &host_ptr->sensor[0]);
                }

                if (( host_ptr->sensors == 0 ) || ( host_ptr->groups == 0 ))
                {
                    elog ("%s has read %d sensors but cannot process with no sensor model (%d:%d)\n",
                              host_ptr->hostname.c_str(),
                              host_ptr->thread_extra_info.samples,
                              host_ptr->sensors,
                              host_ptr->groups);

                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__START );
                }
                else
                {
                    blog ("%s has read %d sensors ... processing results\n",
                              host_ptr->hostname.c_str(), host_ptr->samples);

                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__UPDATE );
                }
                break ;
            }

            /******************************************************************
             *
             * The UPDATE stage translates the string based sensor sample
             * data's 'status' to a severity and adds that to the sensors'
             * sample_severity member in the sensor list.
             *
             *     host_ptr->sensor[MAX_SENSORS].sample_severity
             *
             * Stage Transition:
             *
             *  Success path -> HWMON_SENSOR_MONITOR__HANDLE
             *  Failure Path -> HWMON_SENSOR_MONITOR__FAIL
             *
             *****************************************************************/
            case HWMON_SENSOR_MONITOR__UPDATE:
            {
                if ( host_ptr->sensor_query_count++ == START_DEBOUCE_COUNT )
                {
                    /* onetime log showing debounce mode started */
                    ilog ("%s sensor status deboucing enabled\n", host_ptr->hostname.c_str());
                }

                daemon_signal_hdlr ();

                /* handle clearing the config alarm if its raised but we are
                 * now at a point where the sensors are readable */
                if ( host_ptr->alarmed_config == true )
                {
                    host_ptr->alarmed_config = false ;
                    hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORCFG, "profile", REASON_OK );
                }

                if ( ipmi_update_sensors ( host_ptr ) == PASS )
                {
                    if ( ( rc = ipmi_set_group_state ( host_ptr, "enabled" ) ) == PASS )
                    {
                        _stage_change ( host_ptr->hostname,
                                        host_ptr->monitor_ctrl.stage,
                                        HWMON_SENSOR_MONITOR__HANDLE );
                    }
                    else
                    {
                        elog ("%s failed to set group state to 'enabled' (in sysinv) (rc:%d)\n",
                                  host_ptr->hostname.c_str(), rc);

                        _stage_change  ( host_ptr->hostname,
                                         host_ptr->monitor_ctrl.stage,
                                         HWMON_SENSOR_MONITOR__FAIL );
                    }
                }
                else
                {
                    elog ("%s failed to update sensor data (in hwmon) (rc:%d)\n",
                              host_ptr->hostname.c_str(), rc);

                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__FAIL );
                }
                break ;
            }
            case HWMON_SENSOR_MONITOR__HANDLE:
            {
                /**************************************************************
                 *
                 * Loop over all the sensors handling their current severity.
                 *
                 * At this point the new severities are in
                 * sensor_ptr->sample_severity.
                 *
                 * After a sensor is serviced in this loop that
                 * sensor_ptr->sample_severity is copied to ptr->severity
                 * to be compared against on the next audit interval.
                 *
                 *************************************************************/
                for ( int i = 0 ; i < host_ptr->sensors ; i++ )
                {
                    /*
                     *   This variable controls whether status change actions
                     *   need to be taken at the end of this loop for sensor
                     *   in context. Assume sensor status is not changed.
                     */
                    bool mod_status = false ;

                    /* lets use a local pointer to make the code easier to read */
                    sensor_type * ptr = &host_ptr->sensor[i] ;

                    /* Local copy of new severity */
                    sensor_severity_enum severity = ptr->sample_severity ;

                    /* Things can get a little busy so lets make sure we
                     * service the signal handler and incoming http requests
                     * from sysinv.
                     */
                    daemon_signal_hdlr ();
                    hwmonHttp_server_look ();

                    /* Internasl error checking ; never seen but just in case.
                     * Skip over and swerr about null sensor name */
                    if ( ptr->sensorname.empty() )
                    {
                        slog ("%s %d sensor name is empty\n", host_ptr->hostname.c_str(), i );
                        continue ;
                    }

                    if ( ptr->updated == false )
                    {
                        host_ptr->accounting_bad_count++ ;

                        /*
                         * Force a sensor MINOR if we fail to get status from
                         * it NOT_FOUND_COUNT_BEFORE_MINOR or more times in a row
                         *
                         * This debounces the one of sensor update misses but the
                         * log above at least shows if/when this is happening.
                         */
                        if ( ++ptr->not_updated_status_change_count >= NOT_FOUND_COUNT_BEFORE_MINOR )
                        {
                            severity = HWMON_SEVERITY_MINOR ;
                        }
                    }
                    else
                    {
                        ptr->not_updated_status_change_count = 0 ;
                    }

                    if ( severity != ptr->severity)
                    {
                        blog ("%s %s status change ; %s:%s -> %s\n",
                                  host_ptr->hostname.c_str(),
                                  ptr->sensorname.c_str(),
                                  get_severity(ptr->severity).c_str(),
                                  ptr->status.c_str(),
                                  get_severity(severity).c_str());

                       /* debounce of the transient 'na' case is debounced
                        * if ( host_ptr->sensor_query_count > 5 )
                        *    log_sensor_data ( host_ptr, ptr->sensorname,  ptr->status, get_ipmi_severity(ptr->sample_severity));
                        */
                    }

                    blog1 ("%s %s curr:%s this:%s last:%s\n",
                               host_ptr->hostname.c_str(),
                               ptr->sensorname.c_str(),
                               ptr->status.c_str(),
                               ptr->sample_status.c_str(),
                               ptr->sample_status_last.c_str());

                    if ( severity == HWMON_SEVERITY_GOOD )
                    {
                        if ( ptr->status.compare("ok") )
                        {
                            /* don't bother printing a log for sensors that
                             * go from offline to ok */
                            if ( ptr->status != "offline" )
                            {
                                ilog ("%s %s is ok (was %s)\n",
                                          host_ptr->hostname.c_str(),
                                          ptr->sensorname.c_str(),
                                          ptr->status.c_str());
                            }

                            /* last state was not 'ok' */
                            mod_status  = true ;
                            ptr->status = "ok" ;
                            clear_ignored_state (ptr );
                            clear_logged_state (ptr );
                        }

                        /* TODO: verify clearing sensor that has cleared over a process restart */
                        if ((( ptr->suppress == false ) && ( ptr->severity != HWMON_SEVERITY_GOOD )) ||
                             ((ptr->alarmed  == true ) || ( ptr->degraded == true )))
                        {
                            hwmonHostClass::manage_sensor_state ( host_ptr->hostname, ptr , HWMON_SEVERITY_GOOD );
                        }
                    }
                    else
                    {
                        /* Handle transition from offline to online
                         *  - clear any alarm that exhists for a sensor
                         *    coming out of the offline state is no longer
                         *    offline.
                         **/
                        if (( severity != HWMON_SEVERITY_OFFLINE ) && ( !ptr->status.compare("offline") ))
                        {
                            wlog ("%s %s sensor returned from '%s' with '%s' severity [alarmed:%s]\n",
                                      host_ptr->hostname.c_str(),
                                      ptr->sensorname.c_str(),
                                      ptr->status.c_str(),
                                      get_severity(severity).c_str(),
                                      ptr->alarmed ? "Yes" : "No");

                            /* Clear the alarm and allow it to be re-raised if the issue exists */
                            clear_asserted_alarm ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, ptr, REASON_ONLINE );
                        }

                        if ( severity == HWMON_SEVERITY_OFFLINE )
                        {
                            if ( ptr->status.compare("offline"))
                            {
                                if ( ptr->alarmed == true )
                                {
                                    hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, ptr->sensorname, REASON_OFFLINE );
                                    ptr->alarmed  = false ;
                                }
                                ptr->degraded = false ;

                                if  ( ptr->critl.logged || ptr->major.logged || ptr->minor.logged )
                                {
                                     hwmonLog_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, ptr->sensorname, REASON_OFFLINE );
                                     ptr->critl.logged = ptr->major.logged = ptr->minor.logged = false ;
                                }
                                mod_status  = true      ;
                                blog ("%s %s sensor status change '%s' -> 'offline'\n",
                                          host_ptr->hostname.c_str(),
                                          ptr->status.c_str(),
                                          ptr->sensorname.c_str());
                                ptr->status = "offline" ;
                            }
                        }
                        else if ( severity == HWMON_SEVERITY_MINOR )
                        {
                            /* logs and alarms state changes are handled when the ignore
                             * action is set in the modify handler so there is no need
                             * to call the manager in the ignore case */
                            if (( ptr->suppress == false ) && ( ptr->actions_minor.compare (HWMON_ACTION_IGNORE)))
                            {
                                hwmonHostClass::manage_sensor_state ( host_ptr->hostname, ptr, HWMON_SEVERITY_MINOR );
                            }
                            else
                            {
                                if ( ptr->alarmed == true )
                                {
                                    /* We may have transitioned to ignore from an alarm state so check and clear if an alarm exists */
                                    clear_asserted_alarm ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, ptr, REASON_IGNORED );
                                }
                                clear_logged_state  ( ptr ) ;
                            }

                            /* still maintain the status
                             * ... if not minor then set it to minor */
                            if ( ptr->status.compare("minor") )
                            {
                                ptr->status = "minor" ;
                                mod_status = true ;
                            }
                        }
                        else if ( severity == HWMON_SEVERITY_MAJOR )
                        {
                            /* logs and alarms state changes are handled when the ignore
                             * action is set in the modify handler so there is no need
                             * to call the manager in the ignore case */
                            if (( ptr->suppress == false ) && ( ptr->actions_major.compare (HWMON_ACTION_IGNORE)))
                            {
                                hwmonHostClass::manage_sensor_state ( host_ptr->hostname, ptr, HWMON_SEVERITY_MAJOR );
                            }
                            else
                            {
                                if ( ptr->alarmed == true )
                                {
                                    /* We may have transitioned to ignore from an alarm state so check and clear if an alarm exists */
                                    clear_asserted_alarm ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, ptr, REASON_IGNORED );
                                }
                                clear_logged_state  ( ptr ) ;
                            }

                            /* if not major then set it to major */
                            if ( ptr->status.compare("major") )
                            {
                                ptr->status = "major" ;
                                mod_status = true ;
                            }
                        }
                        else if (( severity == HWMON_SEVERITY_CRITICAL ) ||
                                 ( severity == HWMON_SEVERITY_NONRECOVERABLE ))
                        {
                            /* log and alarm state changes are handled when the ignore
                             * action is set in the modify handler so there is no need
                             * to call the manager in the ignore case */
                            if (( ptr->suppress == false ) && ( ptr->actions_critl.compare (HWMON_ACTION_IGNORE)))
                            {
                                if ( !ptr->actions_critl.compare (HWMON_ACTION_RESET))
                                {
                                    if ( host_ptr->monitor == false )
                                    {
                                        /* Ignore event while we are not monitoring */
                                        ilog ("%s %s ignoring 'reset action' while not monitoring\n",
                                                  host_ptr->hostname.c_str(),
                                                  ptr->sensorname.c_str());
                                    }
                                    else
                                    {
                                        if ( ptr->critl.alarmed == false )
                                        {
                                            hwmonAlarm_critical ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR,
                                                                  ptr->sensorname, REASON_RESETTING ) ;
                                        }
                                        clear_alarmed_state ( ptr );
                                        set_alarmed_severity ( ptr, FM_ALARM_SEVERITY_CRITICAL );

                                        if ( ptr->degraded == false )
                                        {
                                            ptr->degraded = true ;
                                        }

                                        clear_ignored_state ( ptr );
                                        clear_logged_state ( ptr );

                                        /* Send reset request to mtcAgent */
                                        wlog ("%s requesting 'reset' due to critical '%s' sensor\n",
                                                  host_ptr->hostname.c_str(),
                                                  ptr->sensorname.c_str());

                                        hwmon_send_event ( host_ptr->hostname,
                                                           MTC_EVENT_HWMON_RESET,
                                                           ptr->sensorname.data());
                                    }
                                }
                                else if ( !ptr->actions_critl.compare (HWMON_ACTION_POWERCYCLE))
                                {
                                    if ( host_ptr->monitor == false )
                                    {
                                        /* Ignore event while we are not monitoring */
                                        ilog ("%s %s ignoring 'power-cycle action' while not monitoring\n",
                                                  host_ptr->hostname.c_str(),
                                                  ptr->sensorname.c_str());
                                    }
                                    else
                                    {
                                        if ( ptr->critl.alarmed == false )
                                        {
                                            hwmonAlarm_critical ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR,
                                                                  ptr->sensorname, REASON_POWERCYCLING ) ;
                                        }
                                        clear_alarmed_state ( ptr );

                                        set_alarmed_severity ( ptr, FM_ALARM_SEVERITY_CRITICAL );

                                        if ( ptr->degraded == false )
                                        {
                                            ptr->degraded = true ;
                                        }

                                        clear_ignored_state ( ptr );
                                        clear_logged_state  ( ptr );

                                        wlog ("%s requesting 'powercycle' due to critical '%s' sensor\n",
                                                  host_ptr->hostname.c_str(),
                                                  ptr->sensorname.c_str());

                                        /* Send reset request to mtcAgent */
                                        hwmon_send_event ( host_ptr->hostname,
                                                           MTC_EVENT_HWMON_POWERCYCLE,
                                                           ptr->sensorname.data());
                                    }
                                }
                                else
                                {
                                    hwmonHostClass::manage_sensor_state ( host_ptr->hostname, ptr, HWMON_SEVERITY_CRITICAL );
                                }
                            }
                            else
                            {
                                if ( ptr->alarmed == true )
                                {
                                    /* We may have transitioned to ignore from an alarm state so check and clear if an alarm exists */
                                    clear_asserted_alarm ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, ptr, REASON_IGNORED );
                                }
                                else
                                {
                                    blog2 ("%s %s is not alarmed\n", host_ptr->hostname.c_str(), ptr->sensorname.c_str() );
                                }
                                clear_logged_state  ( ptr ) ;
                            }

                            /* if not critical then set it to critical */
                            if ( ptr->status.compare("critical") )
                            {
                                ptr->status = "critical" ;
                                mod_status = true ;
                            }
                        }
                        else
                        {
                             slog ("%s unknown severity (%d)\n",  host_ptr->hostname.c_str(), severity );
                        }
                    } /* end else that look at non-good severities */

                    if ( mod_status == true )
                    {
                        hwmonHttp_mod_sensor ( host_ptr->hostname, host_ptr->event , ptr->uuid, "status" , ptr->status );
                    }
                    ptr->severity = severity ;

                } /* end for loop over all sensors */
                if ( host_ptr->bmc_fw_version.empty() )
                {
                    string fn = (IPMITOOL_OUTPUT_DIR + host_ptr->hostname + "_mc_info") ;
                    if ( daemon_is_file_present ( fn.data() ) )
                    {
                        host_ptr->bmc_fw_version =
                            get_bmc_version_string ( host_ptr->hostname,
                                                     fn.data() );
                    }
                    if ( !host_ptr->bmc_fw_version.empty() )
                    {
                        ilog ("%s bmc fw version: %s\n",
                                  host_ptr->hostname.c_str(),
                                  host_ptr->bmc_fw_version.c_str());
                    }
                }

                /* Start the next group interval timer */
                if ( host_ptr->interval < HWMON_MIN_AUDIT_INTERVAL )
                {
                    ilog ("%s monitor interval set to a %d secs cadence (%d)\n",
                              host_ptr->hostname.c_str(),
                              HWMON_DEFAULT_AUDIT_INTERVAL,
                              host_ptr->interval);
                    host_ptr->interval = HWMON_DEFAULT_AUDIT_INTERVAL ;
                    interval_change_handler ( host_ptr );
                }

                /* exit sensor model relearn mode if we have sensors and groups */
                if (( host_ptr->relearn == true ) &&
                    ( host_ptr->sensors ) && ( host_ptr->groups ))
                {
                    mtcTimer_reset ( host_ptr->relearnTimer );
                    host_ptr->relearn_done_date.clear();
                    host_ptr->relearn = false ;
                    plog ("%s sensor model relearn complete\n",
                              host_ptr->hostname.c_str());
                }

                mtcTimer_start ( host_ptr->monitor_ctrl.timer,
                                 hwmonTimer_handler,
                                 host_ptr->interval );

                _stage_change ( host_ptr->hostname,
                                host_ptr->monitor_ctrl.stage,
                                HWMON_SENSOR_MONITOR__DELAY );
                break ;
            }

            case HWMON_SENSOR_MONITOR__FAIL:
            {
                host_ptr->ping_info.ok = false ;
                host_ptr->ipmitool_thread_ctrl.retries = 0 ;

                mtcTimer_reset ( host_ptr->monitor_ctrl.timer );

                if ( host_ptr->ipmitool_thread_info.status )
                {
                    elog ("%s sensor monitoring failure (rc:%d)\n",
                              host_ptr->hostname.c_str(),
                              host_ptr->ipmitool_thread_info.status );
                    if ( host_ptr->ipmitool_thread_info.data.length() )
                    {
                        string _temp = host_ptr->ipmitool_thread_info.status_string ;
                        size_t pos = _temp.find ("-f", 0) ;

                        if ( pos != std::string::npos )
                        {
                            /* don't log the password filename */
                            elog ("%s ... %s\n",
                                      host_ptr->hostname.c_str(),
                                      _temp.substr(0,pos).c_str());
                        }
                        else
                        {
                            elog ("%s ... %s\n",
                                      host_ptr->hostname.c_str(),
                                      host_ptr->ipmitool_thread_info.status_string.c_str());
                        }
                    }
                }

                if ( host_ptr->ipmitool_thread_ctrl.id )
                {
                    slog ("%s sensor monitor thread is unexpectedly active ; handling as failure\n",
                              host_ptr->hostname.c_str());

                    thread_kill ( host_ptr->ipmitool_thread_ctrl, host_ptr->ipmitool_thread_info );
                }

                if ( host_ptr->interval )
                {
                    ipmi_set_group_state ( host_ptr, "failed" ) ;

                    _stage_change  ( host_ptr->hostname,
                                     host_ptr->monitor_ctrl.stage,
                                     HWMON_SENSOR_MONITOR__START );
                }
                else
                {
                    /* TODO: Error case that should not happen ; need to force reprovision */
                    _stage_change ( host_ptr->hostname,
                                    host_ptr->monitor_ctrl.stage,
                                    HWMON_SENSOR_MONITOR__IDLE );
                }
                break ;
            }
            case HWMON_SENSOR_MONITOR__STAGES:
            default:
            {
                slog ("%s Invalid stage (%d)\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->monitor_ctrl.stage );

                _stage_change ( host_ptr->hostname,
                                host_ptr->monitor_ctrl.stage,
                                HWMON_SENSOR_MONITOR__START );
            }
        }
    }
    return (rc);
}

/* Delete Handler
 * ----------------- */
int hwmonHostClass::delete_handler ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    if ( host_ptr == NULL )
    {
        slog ("delete handler called with null pointer\n");
        return (FAIL_NULL_POINTER);
    }

    switch ( host_ptr->delStage )
    {
        case HWMON_DEL__START:
        {
            ilog ("%s Delete Operation Started\n", host_ptr->hostname.c_str());
            host_ptr->retries = 0 ;

            if ( host_ptr->bm_provisioned == true )
            {
                set_bm_prov ( host_ptr, false);
            }

            if ( host_ptr->ipmitool_thread_ctrl.stage != THREAD_STAGE__IDLE )
            {
                int delay = THREAD_POST_KILL_WAIT ;
                thread_kill ( host_ptr->ipmitool_thread_ctrl , host_ptr->ipmitool_thread_info) ;

                ilog ("%s thread active ; sending kill ; waiting %d seconds\n",
                          host_ptr->hostname.c_str(), delay );
                mtcTimer_reset ( host_ptr->hostTimer );
                mtcTimer_start ( host_ptr->hostTimer, hwmonTimer_handler, delay );
                host_ptr->delStage = HWMON_DEL__WAIT ;
            }
            else
            {
                host_ptr->delStage = HWMON_DEL__DONE ;
            }


            break ;
        }
        case HWMON_DEL__WAIT:
        {
            if ( mtcTimer_expired ( host_ptr->hostTimer ) )
            {
                if ( host_ptr->ipmitool_thread_ctrl.stage != THREAD_STAGE__IDLE )
                {
                    if ( host_ptr->retries++ < 3 )
                    {
                        wlog ("%s still waiting on active thread ; sending another kill signal (try %d or %d)\n",
                                  host_ptr->hostname.c_str(), host_ptr->retries, 3 );

                        thread_kill ( host_ptr->ipmitool_thread_ctrl, host_ptr->ipmitool_thread_info ) ;
                        mtcTimer_start ( host_ptr->hostTimer, hwmonTimer_handler, THREAD_POST_KILL_WAIT );
                        break ;
                    }
                    else
                    {
                        elog ("%s thread refuses to stop ; giving up ...\n",
                                  host_ptr->hostname.c_str());
                    }
                }
                host_ptr->delStage = HWMON_DEL__DONE ;
            }
            break ;
        }
        case HWMON_DEL__DONE:
        {
            /* ok now delete the host */
            del_host ( host_ptr->hostname );
            this->host_deleted = true ;
            break ;
        }
        default:
        {
            ilog ("%s invalid delete stage (%d) ; correcting ...\n", host_ptr->hostname.c_str(), host_ptr->delStage );
            host_ptr->delStage = HWMON_DEL__START ;
        }
    }
    return (PASS);
}


/*****************************************************************************
 *
 * Name       : manage_startup_states
 *
 * Description: Manage the sensor startup states.
 *
 *              This means failure log, alarm and degraded states on
 *              startup for groups and sensors
 *
 *****************************************************************************/

bool hwmonHostClass::manage_startup_states ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;
    if ( host_ptr )
    {

        std::list<hwmonAlarm_entity_status_type>::iterator _iter_ptr ;
        std::list<hwmonAlarm_entity_status_type> alarm_list ;
        alarm_list.clear();

        /**********************    Manage Profile Alarms    ***********************/

        /* clear this config alarm as it is not used anymore - handles patchback case.
         * Its cheaper to send a clear than it is to query for it first */
        hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORCFG, "sensor", REASON_OK );

#ifdef WANT_QUERY_SENSOR_CONFIG_ALARM
        /* We don't degrade for sensor config error - this is similar to a
         * BMC access error in mtcAgent where we only raise a minor alarm */
        if ( hwmon_alarm_query ( host_ptr->hostname, HWMON_ALARM_ID__SENSORCFG, "profile" ) != FM_ALARM_SEVERITY_CLEAR )
             host_ptr->alarmed_config = true ;
#endif
        if ( host_ptr->alarmed_config == false )
        {
            hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORCFG, "profile", REASON_OK );
            host_ptr->alarmed_config = false ;
        }

        /**********************    Manage Group Alarms    ***********************/
        string entity = "host=" + host_ptr->hostname + ".sensorgroup=" ;

        /* 1. Query for all group alarms */
        rc = hwmonAlarm_query_entity ( host_ptr->hostname, entity, alarm_list );
        if ( rc != PASS )
        {
            elog ("%s sensorgroup alarm query failed\n", host_ptr->hostname.c_str() );
            return (FAIL_OPERATION);
        }

        /* 2. Search the alarm list for orphan groups
         *    - group alarms that are not in the current group list
         *    - should not occur but is a catch all for stuck group alarms */
        for ( _iter_ptr = alarm_list.begin(); _iter_ptr != alarm_list.end(); ++_iter_ptr )
        {
            bool found = false ;
            for ( int g = 0 ; g < host_ptr->groups ; g++ )
            {
                string _temp = entity + host_ptr->group[g].group_name ;
                if ( _iter_ptr->instance.compare(_temp) == 0 )
                {
                    found = true ;
                    break ;
                }
            }
            if ( found == false )
            {
                string groupname = _iter_ptr->instance.substr (entity.length()) ;
                wlog ("%s found orphan group alarm '%s' ; clearing\n", host_ptr->hostname.c_str(), groupname.c_str() );
                hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, groupname, REASON_DEPROVISIONED );
            }
        }

        /* 3. Look up each alarmed group and then manage that alarm */
        for ( int g = 0 ; g < host_ptr->groups ; g++ )
        {
            struct sensor_group_type * group_ptr = &host_ptr->group[g] ;
            bool found = false ;
            bool raise = false ;
            bool clear = false ;
            daemon_signal_hdlr ();

            if ( alarm_list.size() )
            {
                for ( _iter_ptr = alarm_list.begin(); _iter_ptr != alarm_list.end(); ++_iter_ptr )
                {
                    string _temp = entity + group_ptr->group_name ;
                    if ( _iter_ptr->instance.compare(_temp) == 0 )
                    {
                        ilog ("%s '%s' group '%s' alarm already set\n",
                                  host_ptr->hostname.c_str(),
                                  host_ptr->group[g].group_name.c_str(),
                                  alarmUtil_getSev_str(_iter_ptr->severity).c_str());
                        found = true ;
                        break ;
                    }
                }
            }

            /* Note: if found == true then the group_ptr points to the group that
             *       has the alarm raised and _iter_ptr point to the alarm info */

            /* Determine if this alarm needs to be raised or cleared ... or left alone
             * Database state takes precidence of all */
            if ( group_ptr->group_state.compare("failed") == 0 )
            {
                group_ptr->failed  = true ;
                group_ptr->alarmed = true ;
                if ( found == true )
                {
                    if ( _iter_ptr->severity != FM_ALARM_SEVERITY_MAJOR )
                    {
                        slog ("%s %s group alarm severity incorrect (%d:%s) ; correcting \n",
                                  host_ptr->hostname.c_str(),
                                  _iter_ptr->entity.c_str(),
                                  _iter_ptr->severity,
                                  alarmUtil_getSev_str(_iter_ptr->severity).c_str());
                        raise = true ;
                    }
                }
                else
                {
                    raise = true ;
                }
            }
            else
            {
                group_ptr->failed  = false ;
                group_ptr->alarmed = false ;
                if ( found == true )
                {
                    clear = true ;
                }
            }

            if ( raise == true )
            {
                group_ptr->failed  = true ;
                group_ptr->alarmed = true ;
                hwmonAlarm_major ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, group_ptr->group_name, REASON_DEGRADED );
            }

            if ( clear == true )
            {
                group_ptr->failed  = false ;
                group_ptr->alarmed = false ;
                hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, group_ptr->group_name, REASON_OK );
            }
        }

        /**********************    Manage Sensor Alarms    ***********************/

        /* 1. Query Sensor Alarm States from FM */
        entity = "host=" + host_ptr->hostname + ".sensor=" ;

        rc = hwmonAlarm_query_entity ( host_ptr->hostname, entity, alarm_list );
        if ( rc != PASS )
        {
            elog ("%s sensor alarm query failed\n", host_ptr->hostname.c_str() );
            return (FAIL_OPERATION);
        }

        /* 2. Search the alarm list for orphan sensors
         *    - sensor alarms that are not in the current sensor list
         *    - should not occur but is a catch all for stuck sensor alarms */
        for ( _iter_ptr = alarm_list.begin (); _iter_ptr != alarm_list.end () ; ++_iter_ptr )
        {
            bool found = false ;
            for ( int s = 0 ; s < host_ptr->sensors ; s++ )
            {
                string _temp = entity + host_ptr->sensor[s].sensorname ;
                if ( _iter_ptr->instance.compare(_temp) == 0 )
                {
                    ilog ("%s '%s' sensor '%s' alarm already set\n",
                               host_ptr->hostname.c_str(),
                               host_ptr->sensor[s].sensorname.c_str(),
                               alarmUtil_getSev_str(_iter_ptr->severity).c_str());
                    found = true ;
                    break ;
                }
            }
            if ( found == false )
            {
                string sensorname = _iter_ptr->instance.substr (entity.length()) ;
                wlog ("%s found orphan sensor alarm '%s' ; clearing\n", host_ptr->hostname.c_str(), sensorname.c_str() );
                hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, sensorname, REASON_DEPROVISIONED );
            }
        }

        /* 3. manage the state of sensors alarms */
        for ( int s = 0 ; s < host_ptr->sensors ; s++ )
        {
             std::list<hwmonAlarm_entity_status_type>::iterator _iter_ptr ;
             sensor_type * sensor_ptr = &host_ptr->sensor[s] ;
             string reason = REASON_OK ;
             bool found = false ;
             bool clear = false ;
             bool minor = false ;
             bool major = false ;
             bool critl = false ;

             daemon_signal_hdlr ();

             if ( alarm_list.size() )
             {
                 for ( _iter_ptr  = alarm_list.begin () ;
                       _iter_ptr != alarm_list.end () ;
                     ++_iter_ptr )
                 {
                     string _temp = entity + sensor_ptr->sensorname ;
                     if ( _iter_ptr->instance.compare(_temp) == 0 )
                     {
                         found = true ;
                         break ;
                     }
                 }
             }

             /* Note: if found == true then the sensor_ptr points to the sensor that
              *       has the alarm raised and _iter_ptr point to the alarm info */

             /* Determine if this alarm needs to be raised or cleared ... or left alone
              * Database state takes precidence of all */
             if ( sensor_ptr->status.compare("ok") == 0 )
             {
                 clear_alarmed_state  ( sensor_ptr );
                 clear_degraded_state ( sensor_ptr );
                 if ( found == true )
                 {
                     clear = true ;
                 }
             }
             else if ( sensor_ptr->status.compare("offline") == 0 )
             {
                 clear_alarmed_state  ( sensor_ptr );
                 clear_degraded_state ( sensor_ptr );
                 if ( found == true )
                 {
                     clear = true ;
                 }
             }
             else if ( sensor_ptr->status.compare("minor") == 0 )
             {
                 if ( sensor_ptr->actions_minor.compare("alarm"))
                 {
                     if ( found == true )
                     {
                         clear = true ;
                     }
                     if ( sensor_ptr->actions_minor.compare("log") == 0 )
                     {
                         set_logged_severity ( sensor_ptr, FM_ALARM_SEVERITY_MINOR );
                         reason = REASON_SET_TO_LOG ;
                     }
                     if ( sensor_ptr->actions_major.compare("ignore") == 0 )
                     {
                         set_ignored_severity ( sensor_ptr, FM_ALARM_SEVERITY_MINOR );
                         reason = REASON_IGNORED ;
                     }
                 }
                 else if ( sensor_ptr->suppress == true )
                 {
                     if ( found == true )
                     {
                         reason = REASON_SUPPRESSED ;
                         clear = true ;
                     }
                 }
                 /**
                  *  else this is an alarm case ...
                  *    - if no alarm found then raise the minor alarm
                  *    - if alarm found but not in proper severity then
                  *      raise the minor alarm
                  **/
                 else
                 {
                     set_alarmed_severity ( sensor_ptr , FM_ALARM_SEVERITY_MINOR );
                     clear_degraded_state ( sensor_ptr );
                     if ((  found == false ) ||
                         (( found == true ) && ( _iter_ptr->severity != FM_ALARM_SEVERITY_MINOR )))
                     {
                         /* correct the severity of the alarm */
                         minor = true ;
                     }
                 }
             }
             else if ( sensor_ptr->status.compare("major") == 0 )
             {
                 if ( sensor_ptr->actions_major.compare("alarm"))
                 {
                     if ( found == true )
                     {
                         clear = true ;
                     }
                     if ( sensor_ptr->actions_major.compare("log") == 0 )
                     {
                         set_logged_severity ( sensor_ptr, FM_ALARM_SEVERITY_MAJOR );
                         reason = REASON_SET_TO_LOG ;
                     }
                     if ( sensor_ptr->actions_major.compare("ignore") == 0 )
                     {
                         set_ignored_severity ( sensor_ptr, FM_ALARM_SEVERITY_MAJOR ) ;
                         reason = REASON_IGNORED ;
                     }
                 }
                 else if ( sensor_ptr->suppress == true )
                 {
                     if ( found == true )
                     {
                         reason = REASON_SUPPRESSED ;
                         clear = true ;
                     }
                 }
                 /**
                  *  else this is an alarm case ...
                  *    - if no alarm found then raise the major alarm
                  *    - if alarm found but not in proper severity then
                  *      raise the major alarm
                  **/
                 else
                 {
                     set_alarmed_severity ( sensor_ptr , FM_ALARM_SEVERITY_MAJOR );
                     set_degraded_state   ( sensor_ptr );
                     if ((  found == false ) ||
                         (( found == true ) && ( _iter_ptr->severity != FM_ALARM_SEVERITY_MAJOR )))
                     {
                         /* correct the severity of the alarm */
                         major = true ;
                     }
                 }
             }
             else if ( sensor_ptr->status.compare("critical") == 0 )
             {
                 if ( sensor_ptr->actions_critl.compare("alarm"))
                 {
                     if ( found == true )
                     {
                         clear = true ;
                     }
                     if ( sensor_ptr->actions_critl.compare("log") == 0 )
                     {
                         set_logged_severity ( sensor_ptr, FM_ALARM_SEVERITY_CRITICAL ) ;
                         reason = REASON_SET_TO_LOG ;
                     }
                     if ( sensor_ptr->actions_critl.compare("ignore") == 0 )
                     {
                         set_ignored_severity ( sensor_ptr , FM_ALARM_SEVERITY_CRITICAL ) ;
                         reason = REASON_IGNORED ;
                     }
                 }
                 else if ( sensor_ptr->suppress == true )
                 {
                     if ( found == true )
                     {
                         reason = REASON_SUPPRESSED ;
                         clear = true ;
                     }
                 }
                 /**
                  *  else this is an alarm case ...
                  *    - if no alarm found then raise the critical alarm
                  *    - if alarm found but not in proper severity then
                  *      raise the critical alarm
                  **/
                 else
                 {
                     set_alarmed_severity ( sensor_ptr , FM_ALARM_SEVERITY_CRITICAL );
                     set_degraded_state   ( sensor_ptr );
                     if ((  found == false ) ||
                         (( found == true ) && ( _iter_ptr->severity != FM_ALARM_SEVERITY_CRITICAL )))
                     {
                         /* correct the severity of the alarm */
                         critl = true ;
                     }
                 }
             }

             if ( clear == true )
             {
                 clear_alarmed_state ( sensor_ptr );
                 clear_degraded_state ( sensor_ptr );
                 hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
             }

             else if ( minor == true )
             {
                 clear_degraded_state ( sensor_ptr );
                 set_alarmed_severity ( sensor_ptr, FM_ALARM_SEVERITY_MINOR );
                 hwmonAlarm_minor ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, REASON_DEGRADED );
             }

             else if ( major == true )
             {
                 set_degraded_state   ( sensor_ptr );
                 set_alarmed_severity ( sensor_ptr, FM_ALARM_SEVERITY_MAJOR );
                 hwmonAlarm_major ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, REASON_DEGRADED );
             }

             else if ( critl == true )
             {
                 set_degraded_state ( sensor_ptr );
                 set_alarmed_severity ( sensor_ptr, FM_ALARM_SEVERITY_CRITICAL);
                 hwmonAlarm_critical ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, REASON_DEGRADED );
             }
             // sensorState_print ( host_ptr->hostname, sensor_ptr );
         }
    }
    else
    {
        rc = FAIL_NULL_POINTER ;
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : monitor_now
 *
 * Description: Force monitor to occur immediately.
 *
 ****************************************************************************/

void hwmonHostClass::monitor_now ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    if ( host_ptr )
    {
        if (  host_ptr->monitor_ctrl.stage == HWMON_SENSOR_MONITOR__DELAY )
        {
            mtcTimer_reset ( host_ptr->monitor_ctrl.timer );
            host_ptr->monitor_ctrl.timer.ring = true ;
            dlog ("%s force monitor now\n", host_ptr->hostname.c_str() );
        }
    }
    else
    {
        slog ("null host pointer\n");
    }
}

/*****************************************************************************
 *
 * Name        : monitor_soon
 *
 * Description: Force monitor to occur in 30 seconds.
 *
 ****************************************************************************/

void hwmonHostClass::monitor_soon ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    if ( host_ptr )
    {
        int delay = MTC_SECS_5 ;

        wlog ("%s sensor monitoring FSM stage (%d) aborted\n",
                  host_ptr->hostname.c_str(),
                  host_ptr->monitor_ctrl.stage);

        if ( host_ptr->ipmitool_thread_ctrl.id )
        {
            ilog ("%s stopping current thread (%lu)\n", host_ptr->hostname.c_str(), host_ptr->ipmitool_thread_ctrl.id );
            thread_kill ( host_ptr->ipmitool_thread_ctrl, host_ptr->ipmitool_thread_info );

            /* have to wait a bit longer than THREAD_POST_KILL_WAIT for the thread kill to happen */
            delay += THREAD_POST_KILL_WAIT ;
        }

        _stage_change ( host_ptr->hostname,
                        host_ptr->monitor_ctrl.stage,
                        HWMON_SENSOR_MONITOR__DELAY) ;

        mtcTimer_reset ( host_ptr->monitor_ctrl.timer );
        mtcTimer_start ( host_ptr->monitor_ctrl.timer,
                         hwmonTimer_handler, delay );

        ilog ("%s sensor monitoring will resume in %d seconds\n",
                  host_ptr->hostname.c_str(), delay );
    }
    else
    {
        slog ("null host pointer\n");
    }
}
