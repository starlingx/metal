/*
 * Copyright (c) 2013-2020, 2023-2025 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/****************************************************************************
 * @file
 * Wind River CGTS Platform Node "Handlers" Implementation
 *
 * Description: This file contains the handlers that implement the X.731 FSM.

 * Interfaces:
 *
 *  nodeLinkClass::timer_handler
 *  nodeLinkClass::enable_handler
 *  nodeLinkClass::disable_handler
 *  nodeLinkClass::delete_handler
 *  nodeLinkClass::degrade_handler
 *  nodeLinkClass::reset_handler
 *  nodeLinkClass::reinstall_handler
 *  nodeLinkClass::event_handler
 *  nodeLinkClass::power_handler
 *  nodeLinkClass::recovery_handler
 *  nodeLinkClass::cfg_handler

 ****************************************************************************/

using namespace std;

#define __AREA__ "hdl"

#include "nodeBase.h"     /* for ... basic definitions       */
#include "bmcUtil.h"      /* for ... mtce-common board mgmt  */
#include "mtcAlarm.h"     /* for ... mtcAlarm_<severity>     */
#include "nodeTimers.h"   /* for ... mtcTimer_start/stop     */
#include "jsonUtil.h"     /* for ... jsonApi_array_value     */
#include "tokenUtil.h"
#include "secretUtil.h"
#include "regexUtil.h"    /* for ... regexUtil_pattern_match */

#include "nodeClass.h"    /* All base stuff                  */

#include "mtcNodeMsg.h"   /* for ... send_mtc_cmd            */
#include "mtcInvApi.h"    /* for ... SYSINV API              */
#include "mtcSmgrApi.h"   /* for ... SM API                  */
#include "mtcVimApi.h"    /* for ... VIm API                 */

#include "daemon_ini.h"   /* for ... ini_parse               */
#include "daemon_common.h"


#define LOAD_NODETYPE_TIMERS                              \
    if ( is_controller(node_ptr) )                        \
    {                                                     \
       node_ptr->mtcalive_timeout = daemon_get_cfg_ptr()->controller_mtcalive_timeout ;   \
    }                                                     \
    else                                                  \
    {                                                     \
       node_ptr->mtcalive_timeout = daemon_get_cfg_ptr()->compute_mtcalive_timeout ;      \
    }                                                     \
    this->goenabled_timeout = daemon_get_cfg_ptr()->goenabled_timeout + 3 ; \
    // Adding 3 seconds to the timeout so that the agent timeout is a
    // little longer than the client.

/*************************************************************
 *
 * Name    : calc_reset_prog_timeout
 *
 * Purpose : Calculate the overall reset progression timeout
 *
 * Note    : Needs to take into account the bmc_reset_delay
 *           for nodes that have the bmc provisioned.
 *
 * ***********************************************************/
int nodeLinkClass::calc_reset_prog_timeout ( struct nodeLinkClass::node * node_ptr,
                                                                    int   retries )
{
    /* for the management interface */
    int to = MTC_RESET_PROG_OFFLINE_TIMEOUT ;

    /* and add on for the bmc interface if its provisioned */
    if ( node_ptr->bmc_provisioned == true )
        to += MTC_RESET_PROG_OFFLINE_TIMEOUT ;

    /* add a small buffer */
    to += (MTC_ENABLED_TIMER*4) ;

    /* factor in the bmc reset delay */
    to += nodeLinkClass::bmc_reset_delay ;

    /* factor in the number of retries */
    to *= (retries+1) ;

    ilog ("%s Reboot/Reset progression has %d sec 'wait for offline' timeout\n",
              node_ptr->hostname.c_str(), to );
    ilog ("%s ... sources - mgmnt:Yes  clstr:%s  bmc:%s\n",
              node_ptr->hostname.c_str(),
              clstr_network_provisioned ? "Yes" : "No",
              node_ptr->bmc_provisioned ? "Yes" : "No" );
    return (to);
}

void mtcTimer_handler ( int sig, siginfo_t *si, void *uc);

/* Looks up the timer ID and asserts the corresponding node's ringer */
void nodeLinkClass::timer_handler ( int sig, siginfo_t *si, void *uc)
{
    struct nodeLinkClass::node * node_ptr ;
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    sig=sig ; uc = uc ;

    if ( !(*tid_ptr) )
    {
        // tlog ("Called with a NULL Timer ID\n");
        return ;
    }

    /* Is this an offline timer */
    node_ptr = get_offline_timer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s offline timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->offline_timer );
        node_ptr->offline_timer.ring = true ;
        return ;
    }

   /* Is this TID a online timer TID ? */
    node_ptr = get_online_timer ( *tid_ptr );
    if ( node_ptr )
    {
        mtcTimer_stop_int_safe ( node_ptr->online_timer );
        node_ptr->online_timer.ring = true ;
        return ;
    }

    /* Is this TID a mtcAlive timer TID ? */
    node_ptr = get_mtcAlive_timer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s MtcAlive 'offline' timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->mtcAlive_timer );
        node_ptr->mtcAlive_timer.ring = true ;
        return ;
    }

    /* Is this TID a for the command FSM */
    node_ptr = get_mtcCmd_timer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s Mtc Command FSM timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->mtcCmd_timer );
        node_ptr->mtcCmd_timer.ring = true ;
        return ;
    }

    /* Is this TID a inservice test timer TID ? */
    node_ptr = get_insvTestTimer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s Insv Test timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->insvTestTimer );
        node_ptr->insvTestTimer.ring = true ;
        return ;
    }

    /* Is this TID a out-of-service test timer TID ? */
    node_ptr = get_oosTestTimer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s Oos Test timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->oosTestTimer );
        node_ptr->oosTestTimer.ring = true ;
        return ;
    }

    /* Is this TID a swact timer TID ? */
    node_ptr = get_mtcSwact_timer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s Swact Timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->mtcSwact_timer );
        node_ptr->mtcSwact_timer.ring = true ;
        return ;
    }

    /* Dead Office Recovery Mode Timer */
    if ( *tid_ptr == mtcTimer_dor.tid )
    {
        mtcTimer_stop_int_safe ( mtcTimer_dor );
        mtcTimer_dor.ring = true ;
        this->dor_mode_active_log_throttle = 0 ;
        return ;
    }

    /* Multi-Node Failure Avoidance Timer ? */
    if ( *tid_ptr == mtcTimer_mnfa.tid )
    {
        // tlog ("%s Mnfa timer ring\n", mtcTimer_mnfa.hostname.c_str());
        mtcTimer_stop_int_safe ( mtcTimer_mnfa );
        mtcTimer_mnfa.ring = true ;
        return ;
    }

    /* is base mtc timer */
    if ( *tid_ptr == mtcTimer.tid )
    {
        // tlog ("%s Mtc timer ring\n", mtcTimer.hostname.c_str());
        mtcTimer_stop_int_safe ( mtcTimer );
        mtcTimer.ring = true ;
        return ;
    }

    /* is uptime refresh timer ? */
    if ( *tid_ptr == mtcTimer_uptime.tid )
    {
        // tlog ("%s Uptime 'refresh' timer ring\n", mtcTimer_uptime.hostname.c_str());
        mtcTimer_stop_int_safe ( mtcTimer_uptime );
        mtcTimer_uptime.ring = true ;

        /* This timer provides self corrective action handler as a secondary service
         * Currently it looks for the following ...
         *
         * 1. Stuck libevent smgrEvent.mutex gate and frees it after 5 uptime intervals
         *
         **/
        if ( smgrEvent.mutex )
        {
            daemon_config_type * cfg_ptr = daemon_get_cfg_ptr();

            /* Clear this mutex flag if stuck for more than 5 minutes */
            if ( ++smgrEvent.stuck > ((cfg_ptr->swact_timeout/60)+1))
            {
                // wlog ("Swact Mutex found stuck and has been auto cleared\n");
                smgrEvent.stuck = 0     ;
                smgrEvent.mutex = false ;
            }
        }
        else
        {
            /* Clear the stuck count */
            smgrEvent.stuck = 0 ;
        }
        return ;
    }
    /* is keystone token refresh timer ? */
    if (( *tid_ptr == mtcTimer_token.tid ) )
    {
        // tlog ("%s Token 'refresh' timer ring\n", mtcTimer_token.hostname.c_str());
        mtcTimer_stop_int_safe ( mtcTimer_token );
        mtcTimer_token.ring = true ;
        return ;
    }

    /* daemon main loop timer */
    if ( *tid_ptr == mtcTimer_loop.tid )
    {
        mtcTimer_stop_int_safe ( mtcTimer_loop );
        mtcTimer_loop.ring = true ;
        return ;
    }

    /* is the http request timer ? */
    node_ptr = get_http_timer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s Http timer ring\n", node_ptr->http_timer.hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->http_timer );
        node_ptr->http_timer.ring = true ;

        if ( node_ptr->http_timer.mutex == true )
            node_ptr->http_timer.error = true ;

        return ;
    }

    /* get the node */
    node_ptr = get_mtcTimer_timer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s Timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->mtcTimer );
        node_ptr->mtcTimer.ring = true ;
        return ;
    }

    /* Is this TID a config timer TID ? */
    node_ptr = get_mtcConfig_timer ( *tid_ptr );
    if ( node_ptr )
    {
        // tlog ("%s Config Timer ring\n", node_ptr->hostname.c_str());
        mtcTimer_stop_int_safe ( node_ptr->mtcConfig_timer );
        node_ptr->mtcConfig_timer.ring = true ;
        return ;
    }

    /* is the thread timer ? */
    node_ptr = get_thread_timer ( *tid_ptr );
    if ( node_ptr )
    {
        mtcTimer_stop_int_safe ( node_ptr->bmc_thread_ctrl.timer );
        node_ptr->bmc_thread_ctrl.timer.ring = true ;
        return ;
    }

    /* is the ping timer ? */
    node_ptr = get_ping_timer ( *tid_ptr );
    if ( node_ptr )
    {
        /* is this the bm ping timer */
        if ( *tid_ptr == node_ptr->bm_ping_info.timer.tid )
        {
            mtcTimer_stop_int_safe ( node_ptr->bm_ping_info.timer );
            node_ptr->bm_ping_info.timer.ring = true ;
            return ;
        }
        /* there may be other ping timers introduced later */
    }

    /* is the bmc handler timer ? */
    node_ptr = get_bm_timer ( *tid_ptr );
    if ( node_ptr )
    {
        /* is this the bm ping timer */
        if ( *tid_ptr == node_ptr->bm_timer.tid )
        {
            mtcTimer_stop_int_safe ( node_ptr->bm_timer );
            node_ptr->bm_timer.ring = true ;
            return ;
        }
    }

    /* is the bmc handler timer ? */
    node_ptr = get_bmc_access_timer ( *tid_ptr );
    if ( node_ptr )
    {
        /* is this the bm ping timer */
        if ( *tid_ptr == node_ptr->bmc_access_timer.tid )
        {
            mtcTimer_stop_int_safe ( node_ptr->bmc_access_timer );
            node_ptr->bmc_access_timer.ring = true ;
            return ;
        }
    }

    /* is the bmc audit timer ? */
    node_ptr = get_bmc_audit_timer ( *tid_ptr );
    if ( node_ptr )
    {
        /* is this the bm ping timer */
        if ( *tid_ptr == node_ptr->bmc_audit_timer.tid )
        {
            mtcTimer_stop_int_safe ( node_ptr->bmc_audit_timer );
            node_ptr->bmc_audit_timer.ring = true ;
            return ;
        }
    }

    /* is the host services handler timer ? */
    node_ptr = get_host_services_timer ( *tid_ptr );
    if ( node_ptr )
    {
        /* is this the bm ping timer */
        if ( *tid_ptr == node_ptr->host_services_timer.tid )
        {
            mtcTimer_stop_int_safe ( node_ptr->host_services_timer );
            node_ptr->host_services_timer.ring = true ;
            return ;
        }
    }

    node_ptr = get_powercycle_recovery_timer ( *tid_ptr );
    if ( node_ptr )
    {
        if (( *tid_ptr == node_ptr->hwmon_powercycle.recovery_timer.tid ) )
        {
            if ( node_ptr->hwmon_powercycle.attempts )
            {
                tlog ("%s powercycle monitor completed successfully after attempt %d\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->hwmon_powercycle.attempts);
            }

            recovery_ctrl_init ( node_ptr->hwmon_powercycle );

            if (( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE ) &&
                ( node_ptr->availStatus != MTC_AVAIL_STATUS__POWERED_OFF ))
            {
               node_ptr->clear_task = true ;
            }

            /* cancel the timer */
            mtcTimer_stop_int_safe ( node_ptr->hwmon_powercycle.recovery_timer );

            node_ptr->hwmon_powercycle.recovery_timer.ring = true ;

            return ;
        }
    }

    node_ptr = get_powercycle_control_timer ( *tid_ptr );
    if ( node_ptr )
    {
        if (( *tid_ptr == node_ptr->hwmon_powercycle.control_timer.tid ) )
        {
            /* cancel the timer */
            mtcTimer_stop_int_safe ( node_ptr->hwmon_powercycle.control_timer );

            node_ptr->hwmon_powercycle.control_timer.ring = true ;

            return ;
        }
    }

    /* Is this TID a reset recovery timer TID ? */
    node_ptr = get_reset_recovery_timer ( *tid_ptr );
    if ( node_ptr )
    {
        if (( *tid_ptr == node_ptr->hwmon_reset.recovery_timer.tid ) )
        {
            tlog ("%s clearing hwmon reset holdoff timer\n",
                      node_ptr->hostname.c_str());

            recovery_ctrl_init ( node_ptr->hwmon_reset );

            mtcTimer_stop_int_safe ( node_ptr->hwmon_reset.recovery_timer );

            node_ptr->hwmon_reset.recovery_timer.ring = true ;
            return ;
        }
    }

    /* Is this TID a reset control timer TID ? */
    node_ptr = get_reset_control_timer ( *tid_ptr );
    if ( node_ptr )
    {
        if (( *tid_ptr == node_ptr->hwmon_reset.control_timer.tid ) )
        {
            tlog ("%s ringing hwmon reset control timer\n",
                      node_ptr->hostname.c_str());

            mtcTimer_stop_int_safe ( node_ptr->hwmon_reset.control_timer );

            node_ptr->hwmon_reset.control_timer.ring = true ;

            return ;
        }
    }

    /* cancel the timer by tid */
    mtcTimer_stop_tid_int_safe ( tid_ptr );
}

/* Inventory Object wrapper - does a node lookup and calls the timer handler */
void mtcTimer_handler ( int sig, siginfo_t *si, void *uc)
{
    nodeLinkClass * object_ptr = get_mtcInv_ptr() ;
    object_ptr->timer_handler ( sig, si, uc );
}

/** Responsible for recovering a host into its enabled state
 *
 * Steps: availibility is either unavailable or failed or intest if previous enable failed
 *  1. enable Start
 *     operational = disabled
 *  2. Notify VM Manager                            (signal)
 *  3. send disabled message to heartbeat service   (message)
 *  4. reboot host                                  (message)
 *     availability = intest
 *  5. wait for mtc alive                           (timer)
 *  6. wait for go enabled                          (timer)
 *  7. send enabled message to heartbeat service    (message)
 *  8. change state to enabled
 *     availability - available
 */

int nodeLinkClass::enable_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    if ( node_ptr->ar_disabled == true )
    {
        wlog_throttled ( node_ptr->ar_log_throttle,
                         AR_LOG_THROTTLE_THRESHOLD,
                         "%s auto recovery disabled cause:%d",
                         node_ptr->hostname.c_str(), node_ptr->ar_cause );
         return (RETRY);
    }

    if ( THIS_HOST )
    {
        /******************************************************************
         *
         * Intercept the unlock action for self.
         *    1. change the admin state to unlocked,
         *    2. send a lazy reboot and
         *    3. wait for the reboot
         *
         ******************************************************************/
        if ( node_ptr->adminAction == MTC_ADMIN_ACTION__UNLOCK )
        {
            bool aio = false ;
            if ( SIMPLEX_AIO_SYSTEM )
                aio = true ;
            else
                aio = false ;

            if (( this->hosts == 1 ) &&
                ( daemon_is_file_present (PLATFORM_SIMPLEX_MODE) == true ))
            {
                /* Check for first pass through case where we need to
                 * start the timer */
                if ( this->unlock_ready_wait == false )
                {
                    if ( daemon_is_file_present(UNLOCK_READY_FILE) == false )
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_8 );
                        mtcInvApi_update_task_now   ( node_ptr, MTC_TASK_MANIFEST_APPLY );
                        this->unlock_ready_wait = true ;
                        return (PASS);
                    }
                }
                else
                {
                    if ( daemon_is_file_present(UNLOCK_READY_FILE) == true )
                    {
                        mtcTimer_reset(node_ptr->mtcTimer);

                        /* fall through to proceed with self reboot */
                    }
                    else if ( node_ptr->mtcTimer.ring == true )
                    {
                        this->unlock_ready_wait = false ;
                        mtcInvApi_update_task_now   ( node_ptr, "Manifest apply timeout ; Unlock to retry" );
                        mtcInvApi_update_states_now ( node_ptr, "locked", "disabled" , "online", "disabled", "offline" );
                        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                        return (PASS);
                    }
                    else
                    {
                        /* wait longer */
                        return (RETRY);
                    }
                }
            }

            daemon_remove_file (NODE_LOCKED_FILE_BACKUP);
            mtcInvApi_update_states_now ( node_ptr, "unlocked", "disabled" , "offline", "disabled", "offline" );
            mtcInvApi_update_task_now   ( node_ptr, aio ? MTC_TASK_AIO_SX_UNLOCK_MSG : MTC_TASK_SELF_UNLOCK_MSG );

            wlog ("%s unlocking %s with reboot\n",
                      my_hostname.c_str(),
                      aio ? "Simplex System" : "Active Controller" );

            /* should not return */
            return ( lazy_graceful_fs_reboot ( node_ptr ));
        }
    }

    switch ( (int)node_ptr->enableStage )
    {
        case MTC_ENABLE__FAILURE:
        {
            /**************************************************************
             * Failure of thr active controller has special handling.
             *
             * Condition 1: While there is no in-service backup controller
             *              to swact to. In this case the ctive controller
             *              - is only degraded to avoid a system outage.
             *              - the AIO subfunction is failed
             *              - worker SubFunction Alarm is raised
             *              - Enable alarm is raised
             *              - A process monitor alarm may also be raised if
             *                the failure was that of a critical process.
             *
             * Condition 2: While there is another controller to Swact to.
             *              In this case the active conroller is failed
             *              and maintenance will trigger SM to Swact and
             *              the failing active controller will get
             *              auto-recovered by the takeover controller.
             *
             * Condition 3: AIO Simplex failures can request thresholded
             *              auto-recovery. In doing so maintenance will
             *              increment the count in an auto recovery counter
             *              file and self reboot if that count does not exceed
             *              the auto recovery threshold. After 3 retries the
             *              threshold is exceeded and then maiantenance stops
             *              self rebooting and enters the state specified by
             *              condition 1 above.
             *
             ***************************************************************/
            bool degrade_only = false ;

            elog ("%s Main Enable FSM (from failed)\n", node_ptr->hostname.c_str());

            mtcTimer_reset ( node_ptr->mtcTimer );

            /* Stop heartbeat */
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_STOP_HOST  );
            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                hbs_minor_clear ( node_ptr, (iface_enum)iface );
            }

            node_ptr->cmdReq        = MTC_CMD_NONE ;
            node_ptr->cmdRsp        = MTC_CMD_NONE ;
            node_ptr->cmdRsp_status = 0 ;

            /* Raise Critical Enable Alarm */
            alarm_enabled_failure ( node_ptr, true );

            /* Handle active controller failures */
            if ( THIS_HOST )
            {
                /* Don't fail the only controller, degrade instead */
                degrade_only = true ;

                /* If the inactive controller is enabled then tru to swact to it.
                 * SM will reject till its eady, until then just run degraded */
                if ( is_inactive_controller_main_insv() == true )
                {
                    wlog ("%s has critical failure\n", node_ptr->hostname.c_str());
                    wlog ("%s ... requesting swact to peer controller",
                              node_ptr->hostname.c_str());

                    mtcInvApi_update_task_now ( node_ptr, MTC_TASK_FAILED_SWACT_REQ );

                    /* Inform the VIM of the failure */
                    mtcVimApi_state_change ( node_ptr, VIM_HOST_FAILED, 3 );

                    /* ask SM to swact to the backup controller */
                    mtcSmgrApi_request ( node_ptr, CONTROLLER_SWACT, 0 );

                    for ( int i = 0 ; i < SMGR_MAX_RETRIES ; i++ )
                    {
                        daemon_signal_hdlr ();
                        sleep (1);

                        /* Try and receive the response */
                        if ( mtcHttpUtil_receive ( nodeLinkClass::smgrEvent ) != RETRY )
                        {
                            wlog ("%s SM Swact Request Response: %s\n",
                                      node_ptr->hostname.c_str(),
                                      smgrEvent.response.c_str());
                            break ;
                        }
                    }
                    if ( nodeLinkClass::smgrEvent.active == true )
                    {
                        slog ("%s freeing smgrEvent activity state\n", node_ptr->hostname.c_str());
                        nodeLinkClass::smgrEvent.active = false ;
                    }

                    /* if we get here then proceed to delay for another swact attempt */
                    enableStageChange ( node_ptr, MTC_ENABLE__FAILURE_SWACT_WAIT );

                    /* force ourselves into the enable handler */
                    if (( node_ptr->adminAction != MTC_ADMIN_ACTION__ENABLE) &&
                        ( node_ptr->adminAction != MTC_ADMIN_ACTION__SWACT) &&
                        ( node_ptr->adminAction != MTC_ADMIN_ACTION__LOCK) &&
                        ( node_ptr->adminAction != MTC_ADMIN_ACTION__FORCE_LOCK))
                    {
                        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__ENABLE );
                    }

                    /* Wait 30 seconds before trying the Swact again */
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_SECS_30 );
                    break ;
                }
                else
                {
                    if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
                    {
                        /* Raise Critical Compute Function Alarm */
                        alarm_compute_failure ( node_ptr , FM_ALARM_SEVERITY_CRITICAL );
                    }
                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                }
            }

            /* Start fresh the next time we enter graceful recovery handler */
            node_ptr->graceful_recovery_counter = 0 ;
            node_ptr->health_threshold_counter  = 0 ;

            if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                node_ptr->inservice_failed_subf = true ;
                subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                            MTC_AVAIL_STATUS__FAILED );
            }

            /* if we get here in controller simplex mode then go degraded
             * if we are not already degraded. Otherwise, fail. */
            if ( THIS_HOST && ( is_inactive_controller_main_insv() == false ))
            {
                if (( node_ptr->adminState  != MTC_ADMIN_STATE__UNLOCKED ) ||
                    ( node_ptr->operState   != MTC_OPER_STATE__ENABLED   ) ||
                    ( node_ptr->availStatus != MTC_AVAIL_STATUS__DEGRADED))
                {
                    allStateChange  ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                                MTC_OPER_STATE__ENABLED,
                                                MTC_AVAIL_STATUS__DEGRADED );
                }
                /* adminAction state is already changed to NONE. */
            }

            else if ( degrade_only == true )
            {
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__ENABLED,
                                           MTC_AVAIL_STATUS__DEGRADED );
            }
            else
            {
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__DISABLED,
                                           MTC_AVAIL_STATUS__FAILED );
            }

            /* Inform the VIM of the failure */
            mtcVimApi_state_change ( node_ptr, VIM_HOST_FAILED, 3 );

            /* handle thresholded auto recovery retry delay interval */
            if ( node_ptr->ar_cause < MTC_AR_DISABLE_CAUSE__LAST )
            {
                unsigned int interval = this->ar_interval[node_ptr->ar_cause] ;
                if ( interval )
                {
                    /* Wait this failure cause's retry delay */
                    mtcTimer_start ( node_ptr->mtcTimer,
                                     mtcTimer_handler,
                                     interval );

                    wlog ("%s waiting %d secs before enable sequence retry (%d)",
                              node_ptr->hostname.c_str(),
                              interval, node_ptr->ar_cause );
                }
                else
                    node_ptr->mtcTimer.ring = true ;
            }
            else
                node_ptr->mtcTimer.ring = true ;

            enableStageChange ( node_ptr, MTC_ENABLE__FAILURE_WAIT );

            break;
        }
        case MTC_ENABLE__FAILURE_SWACT_WAIT:
        {
            if (( node_ptr->operState != MTC_OPER_STATE__ENABLED ) ||
                ( node_ptr->availStatus != MTC_AVAIL_STATUS__DEGRADED ))
            {
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__ENABLED,
                                           MTC_AVAIL_STATUS__DEGRADED );
            }

            /* wait for the swact or to re-try MTC_ENABLE_FAILURE and likely
             * try the swact request again */
            if ( node_ptr->mtcTimer.ring == true )
            {
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
            }
            break ;
        }
        case MTC_ENABLE__FAILURE_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) == false )
            {
                break ;
            }
            /* Stop the enable sequence if the locked now;
             * this might occur if the unlock failed from inventory */
            if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
            {
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                mtcInvApi_update_task ( node_ptr, "" );
            }
            enableStageChange ( node_ptr, MTC_ENABLE__START );
            node_ptr->mtcTimer.ring = false ;
            break ;
            /* Fall through */
        }
        case MTC_ENABLE__START:
        {

            plog ("%s Main Enable FSM (from start)%s\n",
                      node_ptr->hostname.c_str(),
                      this->dor_mode_active ? " (DOR active)" : "" );

            /* clear all the past enable failure bools */
            clear_main_failed_bools ( node_ptr );
            clear_subf_failed_bools ( node_ptr );

            /* Clear all degrade flags except for the HWMON one */
            clear_host_degrade_causes ( node_ptr->degrade_mask );

            /* Purge this hosts work and done queues */
            workQueue_purge    ( node_ptr );
            doneQueue_purge    ( node_ptr );
            mtcCmd_workQ_purge ( node_ptr );
            mtcCmd_doneQ_purge ( node_ptr );

            node_ptr->mtce_flags = 0 ;

            /* Assert the mtc alive gate */
            this->ctl_mtcAlive_gate ( node_ptr, true ) ;

            node_ptr->mtcAlive_online  = false ;
            node_ptr->mtcAlive_offline = true  ;
            node_ptr->health_threshold_counter  = 0 ;
            node_ptr->graceful_recovery_counter = 0 ;
            node_ptr->http_retries_cur          = 0 ;
            node_ptr->insv_test_count           = 0 ;
            node_ptr->mnfa_graceful_recovery    = false ;

            node_ptr->goEnabled      = false ;
            node_ptr->goEnabled_subf = false ;

            mtc_nodeAvailStatus_enum availStatus_temp = node_ptr->availStatus ;
            switch ( node_ptr->availStatus )
            {
                case MTC_AVAIL_STATUS__INTEST:
                case MTC_AVAIL_STATUS__FAILED:

                    /* fall through */

                case MTC_AVAIL_STATUS__DEGRADED:
                case MTC_AVAIL_STATUS__AVAILABLE:
                {
                    if ( ( NOT_SIMPLEX ) && ( is_active_controller ( node_ptr->hostname )) &&
                        ( is_inactive_controller_main_insv() == false ))
                    {
                        wlog ("%s recovering active controller from %s-%s-%s\n",
                                  node_ptr->hostname.c_str(),
                                  get_adminState_str(node_ptr->adminState).c_str(),
                                  get_operState_str(node_ptr->operState).c_str(),
                                  get_availStatus_str(node_ptr->availStatus).c_str());

                        mtcInvApi_update_task ( node_ptr, "" );
                    }
                    else
                    {
                        alarm_enabled_failure ( node_ptr , true );

                        if ( node_ptr->availStatus != MTC_AVAIL_STATUS__FAILED )
                        {
                            if ( node_ptr->operState != MTC_OPER_STATE__DISABLED )
                            {
                                mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_FAILED );
                            }
                            allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                                       MTC_OPER_STATE__DISABLED,
                                                       MTC_AVAIL_STATUS__FAILED );
                        }
                    }
                    break ;
                }
                /* Lets make any availability state corrections */

                case MTC_AVAIL_STATUS__OFFDUTY:
                case MTC_AVAIL_STATUS__ONLINE:
                    availStatus_temp = MTC_AVAIL_STATUS__ONLINE;
                    break ;
                case MTC_AVAIL_STATUS__OFFLINE:
                case MTC_AVAIL_STATUS__NOT_INSTALLED:
                    availStatus_temp = MTC_AVAIL_STATUS__OFFLINE;
                    break ;

                default:
                    slog ("Unknown availability state (%d)\n", availStatus_temp);
                    break ;
            }

            /* Never send a disable request to SM for this controller
             * or SM will shut us down. */
            if ( is_controller ( node_ptr ) && NOT_THIS_HOST )
            {
                mtcSmgrApi_request ( node_ptr,
                                     CONTROLLER_DISABLED,
                                     SMGR_MAX_RETRIES );
            }
            rc = allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                  MTC_OPER_STATE__DISABLED,
                                  availStatus_temp );

            if (( rc != PASS ) && ( node_ptr->adminAction == MTC_ADMIN_ACTION__UNLOCK ))
            {
                allStateChange ( node_ptr, MTC_ADMIN_STATE__LOCKED,
                                            MTC_OPER_STATE__DISABLED,
                                            availStatus_temp );

                mtcInvApi_update_task ( node_ptr, MTC_TASK_UNLOCK_FAILED );

                elog ("%s 'unlock' failed by System Inventory (rc:%d)\n",
                          node_ptr->hostname.c_str(), rc ) ;

                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, 15 );
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE_WAIT );
                break ;
            }

            if ( NOT_THIS_HOST )
            {
                /* lets stop heartbeat */
                enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_STOP_CMD );
            }
            else
            {
                /* skip over the reset part as that was taken care and we are
                 * in the reboot recovery phase now. Look for the mtcAlive */

                /* In self-enable we don't need to purge mtcAlive just need
                 * to wait for one more. Assume offline, not online and open
                 * the mtcAlive gate. */
                this->ctl_mtcAlive_gate ( node_ptr, false ) ;
                node_ptr->mtcAlive_online  = false ;
                node_ptr->mtcAlive_offline = true  ;
                /* set mtcAlive timeout */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_SECS_30 ) ;

                /* timer is started ok so we can do the stage transition */
                enableStageChange ( node_ptr, MTC_ENABLE__MTCALIVE_WAIT );
            }
            break ;
        }

        case MTC_ENABLE__HEARTBEAT_STOP_CMD:
        {
            /* Stop heartbeat */
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_STOP_HOST  );

            /* Clear the minor and failkure flags if it is set for this host */
            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                hbs_minor_clear ( node_ptr, (iface_enum)iface );
                node_ptr->heartbeat_failed[iface] = false ;
            }

            /* now reset/reboot the node by running reset progression */
            enableStageChange ( node_ptr, MTC_ENABLE__RESET_PROGRESSION );

            break ;
        }

        case MTC_ENABLE__RECOVERY_TIMER:
        {
            /* start the recovery wait timer */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RECOVERY_TIMEOUT );
            ilog ("%s Delaying Recovery for %d seconds\n",
                      node_ptr->hostname.c_str(),MTC_RECOVERY_TIMEOUT);

            enableStageChange ( node_ptr, MTC_ENABLE__RECOVERY_WAIT );

            break ;
        }

        case MTC_ENABLE__RECOVERY_WAIT:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                enableStageChange ( node_ptr, MTC_ENABLE__RESET_PROGRESSION );

                node_ptr->mtcTimer.ring = false ;
            }
            if ( node_ptr->availStatus != MTC_AVAIL_STATUS__FAILED )
            {
                availStatusChange ( node_ptr, MTC_AVAIL_STATUS__FAILED );
            }
            break;
        }
        case MTC_ENABLE__RESET_PROGRESSION:
        {
            int overall_timeout = 0 ;

            plog ("%s reboot\n", node_ptr->hostname.c_str() );

            /* Health will get updated in the first
             * mtcAlive message after reset */
            node_ptr->health = NODE_HEALTH_UNKNOWN ;

            node_ptr->mtcCmd_work_fifo.clear();
            mtcCmd_init ( node_ptr->cmd );
            node_ptr->cmd.stage = MTC_CMD_STAGE__START ;
            node_ptr->cmd.cmd   = MTC_OPER__RESET_PROGRESSION ;
            node_ptr->cmd_retries = 0  ; /* init fsm retries count */
            node_ptr->cmd.parm1 = 0    ; /* set progression retries */
            node_ptr->cmd.task  = true ; /* send task updates */
            node_ptr->mtcCmd_work_fifo.push_front(node_ptr->cmd);

            /* calculate the overall timeout period taking into account
             * all the reboot/reset sources that will be tried */
            overall_timeout = calc_reset_prog_timeout ( node_ptr , node_ptr->cmd.parm1 ) ;
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, overall_timeout ) ;
            enableStageChange ( node_ptr, MTC_ENABLE__RESET_WAIT );

            break ;
        }
        case MTC_ENABLE__RESET_WAIT:
        {
            /* Wait or reset progression FSM to complete */
            if ( node_ptr->mtcTimer.ring == true )
            {
                wlog ("%s Reset Progression Timeout\n", node_ptr->hostname.c_str());

                /* trigger some delay before another attempt */
                enableStageChange ( node_ptr, MTC_ENABLE__RECOVERY_TIMER );

                /* if we timeout then remove the reset progression command
                 * and cleanup the done queue ; just in case */
                if ( node_ptr->mtcCmd_done_fifo.size() )
                    node_ptr->mtcCmd_done_fifo.pop_front();
                if ( node_ptr->mtcCmd_work_fifo.size() )
                    node_ptr->mtcCmd_work_fifo.pop_front();
            }
            else if ( node_ptr->mtcCmd_done_fifo.size() )
            {
                mtcTimer_reset ( node_ptr->mtcTimer );

                node_ptr->mtcCmd_done_fifo_ptr =
                node_ptr->mtcCmd_done_fifo.begin();
                if ( node_ptr->mtcCmd_done_fifo_ptr->status != PASS )
                {
                    wlog ("%s Reset Unsuccessful (retries:%d) (rc:%d)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->cmd.parm1,
                              node_ptr->mtcCmd_done_fifo_ptr->status );

                    /* trigger some delay before another attempt */
                    enableStageChange ( node_ptr, MTC_ENABLE__RECOVERY_TIMER );
                }
                else /* ... we got the reset or reboot */
                {
                    /* Set the FSM task state to booting */
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_BOOTING );
                    enableStageChange ( node_ptr, MTC_ENABLE__INTEST_START );
                }
                /* Remove the reset progression command now that it is done */
                node_ptr->mtcCmd_done_fifo.pop_front();
            }
            break ;
        }

        case MTC_ENABLE__INTEST_START:
        {
            plog ("%s Booting (timeout: %d secs) (%d)\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->mtcalive_timeout,
                      node_ptr->node_unlocked_counter);

            node_ptr->cmdReq    = MTC_CMD_NONE ;
            node_ptr->cmdRsp    = MTC_CMD_NONE ;
            node_ptr->unknown_health_reported = false ;
            node_ptr->mtcAlive_online         = false ;
            node_ptr->mtcAlive_offline        = true  ;
            node_ptr->goEnabled               = false ;
            node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__NONE ;

            if ( node_ptr->forcing_full_enable == true )
            {
                ilog ("%s clearing force full enable recursion prevention flag", node_ptr->hostname.c_str());
                node_ptr->forcing_full_enable = false ;
            }

            /* Set uptime to zero in mtce and in the database */
            node_ptr->uptime_save = 0 ;
            set_uptime ( node_ptr, 0 , false );

            /* start the timer that waits for MTC READY */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, node_ptr->mtcalive_timeout );

            node_ptr->mtcAlive_purge = 0 ;

            /* timer is started ok so we can do the stage transition */
            enableStageChange ( node_ptr, MTC_ENABLE__MTCALIVE_PURGE );

            break ;
        }
        case MTC_ENABLE__MTCALIVE_PURGE:
        {
            node_ptr->mtcAlive_purge += 1 ;

            if ( node_ptr->mtcAlive_purge >= 20 )
            {
               /* open gate */
               this->ctl_mtcAlive_gate ( node_ptr, false ) ;

               node_ptr->mtcAlive_purge = 0 ;
               /* timer is started ok so we can do the stage transition */
               enableStageChange ( node_ptr, MTC_ENABLE__MTCALIVE_WAIT );
            }
#ifdef WANT_PURGE_LOG
            else
            {
                dlog2 ("%s purging (%d) ...\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->mtcAlive_purge );
            }
#endif
            /* Clear out any mtcAlive messages that may
             * have come in while we were purging */
            node_ptr->mtcAlive_online  = false ;
            node_ptr->mtcAlive_offline = true  ;
            clear_service_readies ( node_ptr );
            break ;
        }
        case MTC_ENABLE__MTCALIVE_WAIT:
        {
            /* search for the mtc alive message */
            if ( node_ptr->mtcAlive_online == true )
            {
                node_ptr->hbsClient_ready = false ;
                mtcTimer_reset ( node_ptr->mtcTimer );

                /* Check for LUKS volume availability */
                if ( node_ptr->mtce_flags & MTC_FLAG__LUKS_VOL_FAILED )
                {
                    elog ("%s LUKS volume failure (oob:%x)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->mtce_flags)

                    /* raise an alarm for the failure of the config */
                    alarm_luks_failure ( node_ptr );

                    mtcInvApi_update_task ( node_ptr, MTC_TASK_MAIN_CONFIG_FAIL );
                    enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );

                    /* handle auto recovery for this failure */
                    if ( ar_manage ( node_ptr,
                                     MTC_AR_DISABLE_CAUSE__LUKS,
                                     MTC_TASK_AR_DISABLED_LUKS ) != PASS )
                        break ;
                }
                /* Check to see if the host is/got configured correctly */
                else if ((( !node_ptr->mtce_flags & MTC_FLAG__I_AM_CONFIGURED )) ||
                    ((  node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY )))
                {
                    elog ("%s configuration failed or incomplete (oob:%x)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->mtce_flags)

                    /* raise an alarm for the failure of the config */
                    alarm_config_failure ( node_ptr );

                    mtcInvApi_update_task ( node_ptr, MTC_TASK_MAIN_CONFIG_FAIL );
                    enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );

                    /* handle auto recovery for this failure */
                    if ( ar_manage ( node_ptr,
                                     MTC_AR_DISABLE_CAUSE__CONFIG,
                                     MTC_TASK_AR_DISABLED_CONFIG ) != PASS )
                        break ;
                }
                else
                {
                    plog ("%s is MTCALIVE (uptime:%d secs) (oob:%08X)",
                              node_ptr->hostname.c_str(), node_ptr->uptime, node_ptr->mtce_flags );
                    if ((NOT_THIS_HOST) &&
                        ( node_ptr->uptime > ((unsigned int)(node_ptr->mtcalive_timeout*2))))
                    {
                        elog ("%s uptime is more than %d seconds ; host did not reboot\n",
                                  node_ptr->hostname.c_str(),
                                  (node_ptr->mtcalive_timeout*2));
                        elog ("%s ... enable failed ; host needs to reboot\n",
                                  node_ptr->hostname.c_str());
                        enableStageChange(node_ptr, MTC_ENABLE__FAILURE);
                        break ;
                    }

                    else if (( is_controller(node_ptr) == true ) &&
                             ( node_ptr->mtce_flags & MTC_FLAG__SM_UNHEALTHY ))
                    {
                        elog ("%s is SM UNHEALTHY",
                                  node_ptr->hostname.c_str() );
                        elog ("%s ... enable failed ; controller needs to reboot\n",
                                  node_ptr->hostname.c_str());
                        enableStageChange(node_ptr, MTC_ENABLE__FAILURE);
                        break ;
                    }

                    /* Set the node mtcAlive timer to configured value.
                     * This will revert bact to normal timeout after any first
                     * unlock value that may be in effect. */
                    LOAD_NODETYPE_TIMERS ;

                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_ONLINE );
                    node_ptr->offline_log_reported = false ;
                    node_ptr->online_log_reported  = true ;

                    /* This is a redundant / backup message to the call in
                     * admin_state_change telling the node it is unlocked. */
                    node_ptr->unlock_cmd_ack = false ;
                    send_mtc_cmd ( node_ptr->hostname , MTC_MSG_UNLOCKED, MGMNT_INTERFACE );

                    /* Request Out-Of-Service test execution */
                    send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MAIN_GOENABLED, MGMNT_INTERFACE );

                    /* now officially in the In-Test state */
                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__INTEST  );

                    /* O.K. Clear the alive */
                    node_ptr->mtcAlive_online = false ;

                    /* Go to the goEnabled stage */
                    enableStageChange ( node_ptr, MTC_ENABLE__GOENABLED_TIMER );

                    mtcInvApi_update_task ( node_ptr, MTC_TASK_TESTING );
                }
                break ;
            }
            else if ( mtcTimer_expired ( node_ptr->mtcTimer) )
            {
                elog ("%s Timeout waiting for MTCALIVE\n", node_ptr->hostname.c_str());

                /* raise an alarm for the enable failure */
                alarm_enabled_failure ( node_ptr , true );

                /* go back and issue reboot again */
                enableStageChange ( node_ptr, MTC_ENABLE__RESET_PROGRESSION );

                if ( node_ptr->availStatus != MTC_AVAIL_STATUS__FAILED )
                {
                    /* no longer In-Test ; we are 'Failed' again" */
                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__FAILED  );
                }

                /* Set the FSM task state to init failed */
                mtcInvApi_update_task ( node_ptr, MTC_TASK_BOOT_FAIL );

                break ;
            }
            else if ( this->get_mtcAlive_gate (node_ptr) == true )
            {
                slog ("%s mtcAlive gate unexpectedly set, correcting ...\n",
                        node_ptr->hostname.c_str());

                this->ctl_mtcAlive_gate ( node_ptr, false ) ;
            }

            /* wait some more */
            break ;
        }

        case MTC_ENABLE__GOENABLED_TIMER:
        {
            mtcTimer_start ( node_ptr->mtcTimer,
                             mtcTimer_handler, this->goenabled_timeout);

            ilog ("%s waiting for GOENABLED (timeout: %d secs)\n",
                      node_ptr->hostname.c_str(), this->goenabled_timeout );

            node_ptr->goEnabled = false ;

            /* start waiting for the ENABLE READY message */
            enableStageChange ( node_ptr, MTC_ENABLE__GOENABLED_WAIT );

            break ;
        }
        case MTC_ENABLE__GOENABLED_WAIT:
        {
            bool goenable_failed = false ;
            /* The healthy code comes from the host in the mtcAlive message.
             * This 'if' clause was introduced to detected failure of host
             * without having to wait for the GOENABLED phase to timeout.
             *
             * This case is particularly important in the DOR case where
             * workers may have come up and fail to run their manifests
             * and sit there in an unconfigured state. We don't want them to
             * be gracefully recovered to enabled in that case. Instead
             * we want to recover the card through a reset as quickly as
             * possible. */

            /* search for the Go Enable message */
            if (( node_ptr->health == NODE_UNHEALTHY ) ||
                (( node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY)) ||
                 ( node_ptr->goEnabled_failed == true ))
            {
                elog ("%s got GOENABLED Failed\n", node_ptr->hostname.c_str());
                mtcTimer_reset ( node_ptr->mtcTimer );
                goenable_failed = true ;
                mtcInvApi_update_task ( node_ptr, MTC_TASK_MAIN_INTEST_FAIL );
            }
            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled == true )
            {
                mtcTimer_reset ( node_ptr->mtcTimer );
                plog ("%s got GOENABLED\n", node_ptr->hostname.c_str());

                /* O.K. clearing the state now that we got it */
                node_ptr->goEnabled = false ;

                mtcInvApi_update_task ( node_ptr, MTC_TASK_INITIALIZING );

                /* ok. great, got the go-enabled message, lets move on */

                /* Don't start the self heartbeat for the active controller.
                 * Also, in AIO , hosts that have a controller function also
                 * have a worker function and the heartbeat for those hosts
                 * are started at the end of the subfunction handler. */
                if (( THIS_HOST ) ||
                   (( AIO_SYSTEM ) && ( is_controller(node_ptr)) ))
                {
                    enableStageChange ( node_ptr, MTC_ENABLE__STATE_CHANGE );
                }
                else
                {
                    /* allow the fsm to wait for up to 1 minute for the
                     * hbsClient's ready event before starting heartbeat
                     * test. */
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_1 );
                    enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_WAIT );
                }
            }
            else if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                elog ("%s has GOENABLED Timeout", node_ptr->hostname.c_str());
                node_ptr->mtcTimer.ring = false ;
                goenable_failed = true ;
                mtcInvApi_update_task ( node_ptr, MTC_TASK_MAIN_INTEST_TO );
            }
            else
            {
                ; /* wait some more */
            }

            if ( goenable_failed )
            {
                alarm_enabled_failure ( node_ptr, true );

                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );

                /* handle auto recovery for this failure */
                if ( ar_manage ( node_ptr,
                                 MTC_AR_DISABLE_CAUSE__GOENABLE,
                                 MTC_TASK_AR_DISABLED_GOENABLE ) != PASS )
                    break ;
            }
            break ;
        }


        case MTC_ENABLE__HEARTBEAT_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                wlog ("%s hbsClient ready event timeout\n", node_ptr->hostname.c_str());
            }
            else if ( node_ptr->hbsClient_ready == false )
            {
                 break ;
            }
            else
            {
                mtcTimer_reset ( node_ptr->mtcTimer );
            }


            if ( this->hbs_failure_action == HBS_FAILURE_ACTION__NONE )
            {
                /* Skip over the heartbeat soak if the failuer handlig is
                 * none because in that case heartbeating is disabled and
                 * would just be a waste of startup time. */
                enableStageChange ( node_ptr, MTC_ENABLE__STATE_CHANGE );
            }
            else
            {
                plog ("%s Starting %d sec Heartbeat Soak (with%s)\n",
                          node_ptr->hostname.c_str(),
                          MTC_HEARTBEAT_SOAK_BEFORE_ENABLE,
                          node_ptr->hbsClient_ready ? " ready event" : "out ready event"  );

                /* allow heartbeat to run for MTC_HEARTBEAT_SOAK_BEFORE_ENABLE
                 * seconds before we declare enable */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_HEARTBEAT_SOAK_BEFORE_ENABLE );
                enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_SOAK );

                /* Start Monitoring Services - heartbeat, process and hardware */
                send_hbs_command ( node_ptr->hostname, MTC_CMD_START_HOST );
            }
            break ;
        }
        case MTC_ENABLE__HEARTBEAT_SOAK:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                plog ("%s heartbeating\n", node_ptr->hostname.c_str() );

                /* handle auto recovery ear for thsi potential cause */
                node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__NONE ;
                node_ptr->ar_count[MTC_AR_DISABLE_CAUSE__HEARTBEAT] = 0 ;

                /* if heartbeat is not working then we will
                 * never get here and enable the host */
                enableStageChange ( node_ptr, MTC_ENABLE__STATE_CHANGE );
            }
            break ;
        }
        case MTC_ENABLE__STATE_CHANGE:
        {
            /* Check the work queue complete and done status's */
            mtcInvApi_force_task ( node_ptr, "" );

            if ( node_ptr->unlock_cmd_ack )
            {
                ilog ("%s acknowledged unlock", node_ptr->hostname.c_str());
            }
            else
            {
                wlog ("%s has not acknowledged unlock", node_ptr->hostname.c_str());
            }

            if ( node_ptr->degrade_mask )
            {
                /* Allow host to enable in the degraded state */
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__ENABLED,
                                           MTC_AVAIL_STATUS__DEGRADED );
            }
            else
            {
                /* Set node as unlocked-enabled */
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__ENABLED,
                                           MTC_AVAIL_STATUS__AVAILABLE );
            }

            /* Now that we have posted the unlocked-enabled-available state we need
             * to force the final part of the enable sequence through */
            if ( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE )
            {
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__ENABLE );
            }

            /* Start a timer that failed enable if the work queue
             * does not empty or if commands in the done queue have failed */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, work_queue_timeout );

            enableStageChange ( node_ptr, MTC_ENABLE__WORKQUEUE_WAIT );

            break ;
        }
        case MTC_ENABLE__WORKQUEUE_WAIT:
        {
            bool fail = false ;
            rc = workQueue_done ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait longer */
                break ;
            }
            else if ( rc == FAIL_WORKQ_TIMEOUT )
            {
                elog ("%s enable failed ; Enable workQueue timeout, purging ...\n", node_ptr->hostname.c_str());
                mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_WORK_TO );
                fail = true ;
            }
            else if ( rc != PASS )
            {
                elog ("%s Enable failed ; Enable doneQueue has failed commands\n", node_ptr->hostname.c_str());
                mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_WORK_FAIL );
                fail = true ;
            }
            else if ( NOT_THIS_HOST )
            {
                /* Loop over the heartbeat interfaces and fail the Enable if any of them are failing */
                for ( int i = 0 ; i < MAX_IFACES ; i++ )
                {
                    if ( node_ptr->heartbeat_failed[i] == true )
                    {
                        elog ("%s Enable failure due to %s Network *** Heartbeat Loss ***\n",
                                  node_ptr->hostname.c_str(),
                                  get_iface_name_str ((iface_enum)i));

                        fail = true ;
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_FAIL_HB );
                    }
                }
            }

            if ( fail == false )
            {
                /* Go enabled */
                enableStageChange ( node_ptr, MTC_ENABLE__ENABLED );
            }
            else
            {
                workQueue_purge ( node_ptr );
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
            }

            mtcTimer_reset ( node_ptr->mtcTimer );

            break ;
        }
        case MTC_ENABLE__ENABLED:
        {
            if ( is_controller(node_ptr) )
            {
                /* Defer telling SM the controller state if
                 * this is a AIO and this is the only controller */
                if ( AIO_SYSTEM && ( num_controllers_enabled() > 0 ))
                {
                    wlog ("%s deferring SM enable notification till subfunction-enable complete\n",
                              node_ptr->hostname.c_str());
                }
                else
                {
                    mtc_cmd_enum cmd = CONTROLLER_ENABLED ;

                    /* Override cmd of ENABLED if action is UNLOCK */
                    if ( node_ptr->adminAction == MTC_ADMIN_ACTION__UNLOCK )
                    {
                        cmd = CONTROLLER_UNLOCKED ;
                    }

                    if ( mtcSmgrApi_request ( node_ptr, cmd, SMGR_MAX_RETRIES ) != PASS )
                    {
                        wlog ("%s Failed to send 'unlocked-enabled' to HA Service Manager (%d) ; enabling anyway\n",
                                  node_ptr->hostname.c_str(), cmd );
                    }
                }
            }

            alarm_enabled_clear ( node_ptr, false );

            mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__CONFIG );
            node_ptr->alarms[MTC_ALARM_ID__CONFIG] = FM_ALARM_SEVERITY_CLEAR ;
            node_ptr->degrade_mask &= ~DEGRADE_MASK_CONFIG ;

            enableStageChange ( node_ptr, MTC_ENABLE__START );

            if (( AIO_SYSTEM ) && ( is_controller(node_ptr)))
            {
                ilog ("%s running worker sub-function enable handler\n", node_ptr->hostname.c_str());
                mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLING_SUBF );
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__ENABLE_SUBF );
            }
            else
            {
                node_ptr->enabled_count++ ;

                /* Inform the VIM that this host is enabled */
                mtcVimApi_state_change ( node_ptr, VIM_HOST_ENABLED, 3 );

                plog ("%s is ENABLED", node_ptr->hostname.c_str());
                node_ptr->http_retries_cur = 0 ;

                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );

                node_ptr->health_threshold_counter = 0 ;

                ar_enable ( node_ptr );
            }

            break ;
        }

        default:
            rc = FAIL_BAD_CASE ;
    }
    return (rc);
}

int recovery_state_gate = -1 ;

/* Graceful Recovery handler
 * -------------------------
 * Tries to recover a failed host back in service
 *  - auto recovery if it only disappeared for 5 seconds
 *  - avoiding a double reset if it was gone for longer or was known to reset */
int nodeLinkClass::recovery_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    if ( node_ptr->recoveryStage != recovery_state_gate )
    {
        recovery_state_gate = node_ptr->recoveryStage ;
    }
    switch ( (int)node_ptr->recoveryStage )
    {
        case MTC_RECOVERY__FAILURE:
        {
            if ( node_ptr->mtcTimer.ring == false )
            {
                break ;
            }
            recoveryStageChange ( node_ptr, MTC_RECOVERY__START );
            node_ptr->mtcTimer.ring = false ;

            break ;
        }

        case MTC_RECOVERY__START:
        {
            if ( this->hbs_failure_action != HBS_FAILURE_ACTION__FAIL )
            {
                wlog ("%s heartbeat failure recovery action is not fail\n",
                          node_ptr->hostname.c_str());
                mtcInvApi_update_task ( node_ptr, "" );
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                break ;
            }

           /* Purge this hosts work queues */
            mtcCmd_workQ_purge ( node_ptr );
            mtcCmd_doneQ_purge ( node_ptr );
            this->ctl_mtcAlive_gate  ( node_ptr, false );
            node_ptr->http_retries_cur = 0 ;
            node_ptr->unknown_health_reported = false ;

            plog ("%s %sGraceful Recovery (%d) (uptime was %d)\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->mnfa_graceful_recovery ? "MNFA " : "",
                      node_ptr->graceful_recovery_counter,
                      node_ptr->uptime );

            /* Cancel any outstanding timers */
            mtcTimer_reset ( node_ptr->mtcTimer );

            /* clear all the past enable failure bools */
            clear_main_failed_bools ( node_ptr );
            clear_subf_failed_bools ( node_ptr );

            /* Disable the heartbeat service for Graceful Recovery */
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_STOP_HOST );

            /* Have we reached the maximum allowed fast recovery attempts.
             *
             * If we have then force the full enable by
             *   1. clearing the recovery action
             *   2. Setting the node operational state to Disabled
             *   3. Setting the Enable action
             */
            node_ptr->graceful_recovery_counter++ ;
            if ( node_ptr->graceful_recovery_counter > MTC_MAX_FAST_ENABLES )
            {
                /* gate off further mtcAlive messaging timme the offline
                * handler runs. This prevents stale messages from making it
                * in and prolong the offline detection time */
                this->ctl_mtcAlive_gate ( node_ptr, true ) ;

                elog ("%s Graceful Recovery Failed (retries=%d)\n",
                          node_ptr->hostname.c_str(), node_ptr->graceful_recovery_counter );

                /* This forces exit from the recover handler and entry into the
                 * enable_handler via FAILED availability state and no aciton. */
                nodeLinkClass::force_full_enable ( node_ptr );

                break ;
            }
            else
            {
                wlog ("%s Graceful Recovery (%d of %d)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->graceful_recovery_counter,
                          MTC_MAX_FAST_ENABLES );

                if ( node_ptr->graceful_recovery_counter > 1 )
                    mtcInvApi_update_task ( node_ptr, "Graceful Recovery Retry" );
                else
                    mtcInvApi_update_task ( node_ptr, "Graceful Recovery");
                /* need to force a 2 second wait if we are in the
                 * graceful recovery retry so that we honor the 5
                 * second grace period */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_SECS_2 );
                recoveryStageChange ( node_ptr, MTC_RECOVERY__RETRY_WAIT ) ;
            }
            break ;
        }
        case MTC_RECOVERY__RETRY_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                recoveryStageChange ( node_ptr, MTC_RECOVERY__REQ_MTCALIVE ) ;
            }
            break ;
        }
        case MTC_RECOVERY__REQ_MTCALIVE:
        {
            /* Clear any recent mtcAlive notification ; start a new :) */
            node_ptr->mtcAlive_online = false ;

            /* Clear any recent goEnable notification ; start a new :) */
            node_ptr->goEnabled = false ;

            /* Save the node's last recorded uptime and request mtcAlive from
             * seemingly failed host. Uptime is saved because when the next
             * mtcAlive comes it the uptime will be over written and we need
             * it to compare as a dicision point later on in recovery handling */
            node_ptr->uptime_save = node_ptr->uptime ;

            /* send mtcAlive requests */
            start_offline_handler ( node_ptr );

            /* A host is considered failed if it goes away for more
             * than a Loss Of Communication Recovery Timeout specified as mtc.ini
             * configuration option 'loc_recovery_timeout' time in seconds. */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, loc_recovery_timeout );

            ilog ("%s requesting mtcAlive with %d sec timeout\n",
                      node_ptr->hostname.c_str(), loc_recovery_timeout);

            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, MGMNT_INTERFACE );

            recoveryStageChange ( node_ptr, MTC_RECOVERY__REQ_MTCALIVE_WAIT ) ;

            break ;
        }
        case MTC_RECOVERY__REQ_MTCALIVE_WAIT:
        {
            if ( node_ptr->mtcAlive_online == true )
            {

                mtcTimer_stop ( node_ptr->mtcTimer );

                ilog ("%s got requested mtcAlive%s\n",
                          node_ptr->hostname.c_str(),
                          this->dor_mode_active ? " (DOR mode)" : "" );

                stop_offline_handler ( node_ptr );

                /* Check to see if the host is/got configured correctly */
                if ( (node_ptr->mtce_flags & MTC_FLAG__I_AM_CONFIGURED) == 0 )
                {
                    elog ("%s Not Configured (Graceful Recovery)\n", node_ptr->hostname.c_str());

                    /* raise an alarm for the failure of the config */
                    alarm_config_failure ( node_ptr );
                    force_full_enable ( node_ptr );
                    break ;
                }

                /* Check to see if the host is/got configured correctly */
                else if ( (node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY) )
                {
                    elog ("%s Configuration Failure (Graceful Recovery)\n", node_ptr->hostname.c_str());

                    /* raise an alarm for the failure of the config */
                    alarm_config_failure ( node_ptr );
                    force_full_enable ( node_ptr );
                    break ;
                }

                else if ( node_ptr->mnfa_graceful_recovery == true )
                {
                    if ( node_ptr->uptime > MTC_MINS_15 )
                    {
                        /* did not reboot case */
                        wlog ("%s Connectivity Recovered ; host did not reset (uptime:%d)\n",
                                  node_ptr->hostname.c_str(), node_ptr->uptime);
                        wlog ("%s ... continuing with MNFA graceful recovery\n", node_ptr->hostname.c_str());
                        wlog ("%s ... with no affect to host services\n", node_ptr->hostname.c_str());

                        /* allow the fsm to wait for up to 1 minute for the
                         * hbsClient's ready event before starting heartbeat
                         * test. */
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_1 );
                        recoveryStageChange ( node_ptr, MTC_RECOVERY__HEARTBEAT_START ) ;
                    }
                    else
                    {
                        /* did reboot case */
                        wlog ("%s Connectivity Recovered ; host has reset (uptime:%d)\n",
                                  node_ptr->hostname.c_str(),  node_ptr->uptime);
                        ilog ("%s ... continuing with MNFA graceful recovery\n", node_ptr->hostname.c_str());
                        ilog ("%s ... without additional reboot %s\n",
                                  node_ptr->hostname.c_str(), node_ptr->bm_ip.empty() ? "or reset" : "" );

                        /* now officially in the In-Test state */
                        availStatusChange ( node_ptr, MTC_AVAIL_STATUS__INTEST  );

                        /* O.K. Clear the alive */
                        node_ptr->mtcAlive_online = false ;

                        /* Go to the goEnabled stage */
                        recoveryStageChange ( node_ptr, MTC_RECOVERY__GOENABLED_TIMER );

                        alarm_enabled_failure(node_ptr, true );
                        break ;
                    }
                }
                else if ( node_ptr->uptime > MTC_MINS_15 )
                {
                    /* did not reboot case */
                    wlog ("%s Connectivity Recovered ; host did not reset%s (uptime:%d)",
                              node_ptr->hostname.c_str(),
                              this->dor_mode_active ? " (DOR mode)" : "",
                              node_ptr->uptime);

                    wlog ("%s ... continuing with graceful recovery\n", node_ptr->hostname.c_str());
                    wlog ("%s ... with no affect to host services\n", node_ptr->hostname.c_str());

                    /* allow the fsm to wait for up to 1 minute for the
                     * hbsClient's ready event before starting heartbeat
                     * test. */
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_1 );
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__HEARTBEAT_START ) ;
                }
                else
                {
                    wlog ("%s Connectivity Recovered ; host has reset\n", node_ptr->hostname.c_str());
                    ilog ("%s ... continuing graceful recovery%s ; (OOB: %08x)",
                              node_ptr->hostname.c_str(),
                              this->dor_mode_active ? " (DOR mode)" : "",
                              node_ptr->mtce_flags);
                    ilog ("%s ... without additional reboot %s (uptime:%d)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->bm_ip.empty() ? "or reset" : "",
                              node_ptr->uptime );

                    /* now officially in the In-Test state */
                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__INTEST  );

                    /* Go to the goEnabled stage */
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__GOENABLED_TIMER );

                    alarm_enabled_failure (node_ptr, true );
                }
            }
            /* A timer ring indicates that the host is not up */
            else if ( node_ptr->mtcTimer.ring == true )
            {
                stop_offline_handler ( node_ptr );

               /* So now this means the node is failed
                * we need to stop services and transition into
                * a longer 'waiting' for the asynchronous mtcAlive
                * that should come as part of the automatic reboot
                * Steps are
                *  1. Stop Services
                *  2. Create mtcAlive timer
                *  2a.  MtcAlive indicating reset ; run start services and recover
                *  2b.  MtcAlive indicating no reset ; force full enable
                *  2c   MtcAlive Timeout: force full enable
                */
                wlog ("%s Loss Of Communication for %d seconds ; disabling host%s\n",
                          node_ptr->hostname.c_str(),
                          loc_recovery_timeout,
                          this->dor_mode_active ? " (DOR mode)" : "" );
                wlog ("%s ... stopping host services\n", node_ptr->hostname.c_str());
                wlog ("%s ... continuing with graceful recovery\n", node_ptr->hostname.c_str());

                /* clear all mtc flags. Will be updated on the next/first
                 * mtcAlive message upon recovery */
                node_ptr->mtce_flags = 0 ;

                /* Set node as unlocked-disabled-failed */
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__DISABLED,
                                           MTC_AVAIL_STATUS__FAILED );

                if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
                {
                    subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                               MTC_AVAIL_STATUS__FAILED );
                }

                /* Inform the VIM that this host has failed */
                mtcVimApi_state_change ( node_ptr, VIM_HOST_FAILED, 3 );

                alarm_enabled_failure(node_ptr, true );

                /* Clear all degrade flags except for the HWMON one */
                clear_host_degrade_causes ( node_ptr->degrade_mask );

                if ( is_controller(node_ptr) )
                {
                    if ( mtcSmgrApi_request ( node_ptr, CONTROLLER_DISABLED , SMGR_MAX_RETRIES ) != PASS )
                    {
                        wlog ("%s Failed to send 'unlocked-disabled' to HA Service Manager\n",
                                  node_ptr->hostname.c_str() );
                    }
                }
                recoveryStageChange ( node_ptr, MTC_RECOVERY__MTCALIVE_TIMER );
            }
            break ;
        }
        case MTC_RECOVERY__MTCALIVE_TIMER:
        {
            int timeout = 0 ;

            /* Set the FSM task state to 'Graceful Recovery Wait' */
            node_ptr->uptime = 0 ;
            mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_WAIT );

            start_offline_handler ( node_ptr );

            timeout = node_ptr->mtcalive_timeout ;

            /* Only try and issue in-line recovery reboot or reset if
             * NOT in Dead Office Recovery (DOR) mode. */
            if ( this->dor_mode_active )
            {
                ilog ("%s issuing one time graceful recovery reboot over management network\n",
                          node_ptr->hostname.c_str());
                node_ptr->reboot_cmd_ack_mgmnt = false ;
                node_ptr->reboot_cmd_ack_clstr = false ;
                node_ptr->reboot_cmd_ack_pxeboot = false ;
                send_mtc_cmd ( node_ptr->hostname, MTC_CMD_REBOOT, MGMNT_INTERFACE ) ;
                send_mtc_cmd ( node_ptr->hostname, MTC_CMD_REBOOT, PXEBOOT_INTERFACE ) ;

                /* If the cluster-host network is provisioned then try
                 * and issue a reset over it to expedite the recovery
                 * for the case where the management heartbeat has
                 * failed but the cluster-host has not.
                 * Keeping it simple by just issuing the command and not looping on it */
                if ( node_ptr->clstr_ip.length () > 5 )
                {
                    ilog ("%s issuing one time graceful recovery reboot over cluster-host network\n",
                              node_ptr->hostname.c_str());
                    send_mtc_cmd ( node_ptr->hostname, MTC_CMD_REBOOT, CLSTR_INTERFACE ) ;
                }

                if ( node_ptr->bmc_provisioned )
                {
                    ilog ("%s posting one time board management graceful recovery reset",
                              node_ptr->hostname.c_str());
                    ilog ("%s ... node may be rebooting or running kdump",
                              node_ptr->hostname.c_str());
                    ilog ("%s ... give kdump time to complete ; reset in %d secs",
                              node_ptr->hostname.c_str(), bmc_reset_delay );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, bmc_reset_delay );
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__RESET_SEND_WAIT );
                    break ;
                }
                else
                {
                    wlog ("%s cannot issue Reset\n", node_ptr->hostname.c_str() );
                    wlog ("%s ... board management not provisioned or accessible\n", node_ptr->hostname.c_str() );
                }
            }
            else
            {
                /* Just allow Graceful Recovery to take its course. */
                /* Load configured mtcAlive and goEnabled timers */
                LOAD_NODETYPE_TIMERS ;

                /* load the mtcAlive timeout to accomodate for dor recovery */
                timeout = node_ptr->mtcalive_timeout ;
            }

            /* start the timer that waits for MTCALIVE */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, timeout );

            plog ("%s %s (%d secs)%s(uptime was %d) \n",
                      node_ptr->hostname.c_str(),
                      MTC_TASK_RECOVERY_WAIT,
                      timeout,
                      this->dor_mode_active ? " (DOR) " : " " ,
                      node_ptr->uptime_save );

            clear_service_readies ( node_ptr );

            recoveryStageChange ( node_ptr, MTC_RECOVERY__MTCALIVE_WAIT );
            break ;
        }
        case MTC_RECOVERY__RESET_SEND_WAIT:
        {
            bool reset_aborted = false ;

            /* Abort the reset if we got an acknowledgment from
             * either the mgmnt or clstr reboot requests.
             */
            if ( node_ptr->reboot_cmd_ack_mgmnt )
            {
                reset_aborted = true ;
                ilog ("%s backup bmc reset aborted due to management network reboot request ACK",
                          node_ptr->hostname.c_str());
            }
            else if ( node_ptr->reboot_cmd_ack_pxeboot )
            {
                reset_aborted = true ;
                ilog ("%s backup bmc reset aborted due to pxeboot network reboot request ACK",
                          node_ptr->hostname.c_str());
            }
            else if ( node_ptr->reboot_cmd_ack_clstr )
            {
                reset_aborted = true ;
                ilog ("%s backup bmc reset aborted due to cluster-host network reboot request ACK",
                          node_ptr->hostname.c_str());

            }
            else if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                if ( node_ptr->bmc_accessible )
                {
                    rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_RESET );
                    if ( rc )
                    {
                        wlog ("%s board management reset failed\n", node_ptr->hostname.c_str());
                        wlog ("%s ... aborting one time reset", node_ptr->hostname.c_str());
                        reset_aborted = true ;
                    }
                    else
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                        recoveryStageChange ( node_ptr, MTC_RECOVERY__RESET_RECV_WAIT );
                        break ;
                    }
                }
                else
                {
                    reset_aborted = true ;
                    wlog ("%s bmc is not accessible ; aborting one time reset",
                              node_ptr->hostname.c_str());
                }
            }
            if ( reset_aborted )
            {
                int timeout = node_ptr->mtcalive_timeout ;
                /* start the timer that waits for MTCALIVE */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, timeout );

                plog ("%s %s (%d secs)%s(uptime was %d) \n",
                          node_ptr->hostname.c_str(),
                          MTC_TASK_RECOVERY_WAIT,
                          timeout,
                          this->dor_mode_active ? " (DOR mode) " : " " ,
                          node_ptr->uptime_save );

                clear_service_readies ( node_ptr );
                recoveryStageChange ( node_ptr, MTC_RECOVERY__MTCALIVE_WAIT );
            }
            break ;
        }

        case MTC_RECOVERY__RESET_RECV_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }

                if ( rc )
                {
                    elog ("%s Reset command failed\n", node_ptr->hostname.c_str());
                }
                else
                {
                    ilog ("%s is Resetting\n", node_ptr->hostname.c_str());
                }

                /* start the timer that waits for MTCALIVE */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, node_ptr->mtcalive_timeout );

                plog ("%s %s (%d secs) (uptime was %d)\n",
                          node_ptr->hostname.c_str(),
                          MTC_TASK_RECOVERY_WAIT,
                          node_ptr->mtcalive_timeout,
                          node_ptr->uptime_save );

                clear_service_readies ( node_ptr );

                recoveryStageChange ( node_ptr, MTC_RECOVERY__MTCALIVE_WAIT );
            }
            break ;
        }
        case MTC_RECOVERY__MTCALIVE_WAIT:
        {
            /* search for the mtc alive message */
            if ( node_ptr->mtcAlive_online == true )
            {
                mtcTimer_stop ( node_ptr->mtcTimer );


                /* If the host's uptime is bigger than the saved uptime then
                 * the host has not reset yet we have disabled services
                 * then now we need to reset the host to prevet VM duplication
                 * by forcing a full enable */
                if ((( node_ptr->uptime_save != 0 ) &&
                     ( node_ptr->uptime >= node_ptr->uptime_save )) ||
                    (( node_ptr->uptime_save == 0 ) &&
                     ( node_ptr->uptime > MTC_MINS_20 )))
                {
                    ilog ("%s regained MTCALIVE from host that did not reboot (uptime:%d)\n",
                                  node_ptr->hostname.c_str(), node_ptr->uptime );
                    ilog ("%s ... uptimes before:%d after:%d\n", node_ptr->hostname.c_str(), node_ptr->uptime_save, node_ptr->uptime );
                    ilog ("%s ... exiting graceful recovery\n", node_ptr->hostname.c_str());
                    ilog ("%s ... forcing full enable with reset\n", node_ptr->hostname.c_str());

                    nodeLinkClass::force_full_enable ( node_ptr );
                }
                /* Check to see if the host is/got configured */
                else if ( (node_ptr->mtce_flags & MTC_FLAG__I_AM_CONFIGURED) == 0 )
                {
                    elog ("%s Not Configured (Graceful Recovery)\n", node_ptr->hostname.c_str());

                    /* raise an alarm for the failure of the config */
                    alarm_config_failure ( node_ptr );
                    force_full_enable ( node_ptr );
                    break ;
                }

                /* Check to see if the host is/got configured correctly */
                else if ( (node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY) )
                {
                    elog ("%s Configuration Failure (Graceful Recovery)\n", node_ptr->hostname.c_str());

                    /* raise an alarm for the failure of the config */
                    alarm_config_failure ( node_ptr );
                    force_full_enable ( node_ptr );
                    break ;
                }
                else
                {
                    ilog ("%s regained MTCALIVE from host that has rebooted (uptime curr:%d save:%d)\n",
                                  node_ptr->hostname.c_str(), node_ptr->uptime, node_ptr->uptime_save );
                    ilog ("%s ... continuing with graceful recovery %s\n",
                                  node_ptr->hostname.c_str(),
                                  this->dor_mode_active ? "(DOR mode)" : "");
                    ilog ("%s ... without additional reboot %s\n",
                                  node_ptr->hostname.c_str(), node_ptr->bm_ip.empty() ? "or reset" : "" );

                     /* now officially in the In-Test state */
                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__INTEST  );

                    /* O.K. Clear the alive */
                    node_ptr->mtcAlive_online = false ;

                    /* Go to the goEnabled stage */
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__GOENABLED_TIMER );
                }
                break ;
            }
            else if ( node_ptr->mtcTimer.ring == true )
            {

                /* Set the FSM task state to init failed */
                mtcInvApi_update_task ( node_ptr, "Graceful Recovery Failed" );

                node_ptr->mtcTimer.ring = false ;

                elog ("%s has MTCALIVE Timeout\n", node_ptr->hostname.c_str());

                nodeLinkClass::force_full_enable ( node_ptr );

                break ;
            }
            else if (( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) &&
                     ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                     ( node_ptr->bmc_provisioned == true ) &&
                     ( node_ptr->bmc_accessible == true ) &&
                     ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__INIT ) &&
                     ( thread_idle ( node_ptr->bmc_thread_ctrl )) &&
                     ( node_ptr->bmc_thread_info.command != BMC_THREAD_CMD__POWER_ON ))
            {
                ilog ("%s powering on unlocked powered off host\n",  node_ptr->hostname.c_str());
                if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_ON ) != PASS )
                {
                    node_ptr->bmc_thread_ctrl.done = true ;
                    thread_kill ( node_ptr->bmc_thread_ctrl , node_ptr->bmc_thread_info ) ;
                }
            }
            else if (( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) &&
                     ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                     ( node_ptr->bmc_provisioned == true ) &&
                     ( node_ptr->bmc_accessible == true ) &&
                     ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__INIT ) &&
                     ( thread_done ( node_ptr->bmc_thread_ctrl )) &&
                     ( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_ON ))
            {
                if ( bmc_command_recv ( node_ptr ) == PASS )
                {
                    ilog ("%s powered on\n",  node_ptr->hostname.c_str());
                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__OFFLINE );
                }
            }
            else if ( this->get_mtcAlive_gate ( node_ptr ) == true )
            {
                slog ("%s mtcAlive gate unexpectedly set, auto-correcting ...\n",
                        node_ptr->hostname.c_str());

                 this->ctl_mtcAlive_gate ( node_ptr, false ) ;
            }

            /* wait some more */
            break ;
        }
        case MTC_RECOVERY__GOENABLED_TIMER:
        {
            node_ptr->goEnabled = false ;

            /* See if the host is there and already in the go enabled state */
            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MAIN_GOENABLED, MGMNT_INTERFACE );

            /* start the reboot timer - is cought in the mtc alive case */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, this->goenabled_timeout );

            /* ok time started */
            ilog ("%s waiting for GOENABLED ; with %d sec timeout\n",
                      node_ptr->hostname.c_str(),
                      this->goenabled_timeout );


            /* Default to unknown health */
            node_ptr->health = NODE_HEALTH_UNKNOWN ;

            /* start waiting fhr the ENABLE READY message */
            recoveryStageChange ( node_ptr, MTC_RECOVERY__GOENABLED_WAIT );
            break ;
        }
        case MTC_RECOVERY__GOENABLED_WAIT:
        {
            /* The healthy code comes from the host in the mtcAlive message.
             * This 'if' clause was introduced to detected failure of host
             * without having to wait for the GOENABLED phase to timeout.
             *
             * This case is particularly important in the DOR case where
             * workers may have come up and fail to run their manifests
             * and sit there in an unconfigured state. We don't want them to
             * be gracefully recovered to enabled in that case. Instead
             * we want to recover the card through a reset as quickly as
             * possible. */
            if ( node_ptr->health == NODE_UNHEALTHY )
            {
                elog ("%s is UNHEALTHY\n", node_ptr->hostname.c_str());
                mtcTimer_reset ( node_ptr->mtcTimer );
                this->force_full_enable ( node_ptr );
            }
            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled_failed == true )
            {
                elog ("%s got GOENABLED Failed\n", node_ptr->hostname.c_str());
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcInvApi_update_task ( node_ptr, MTC_TASK_MAIN_INTEST_FAIL );
                this->force_full_enable ( node_ptr );
            }

            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled == true )
            {
                plog ("%s got GOENABLED (Graceful Recovery)\n", node_ptr->hostname.c_str());
                mtcTimer_reset ( node_ptr->mtcTimer );

                /* O.K. clearing the state now that we got it */
                node_ptr->goEnabled = false ;

                /* Manage state change */
                if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
                {
                    /* Here we need to run the sub-fnction goenable and start
                     * host services if this is the other controller in a AIO
                     * system. */
                    if ( NOT_THIS_HOST )
                    {
                        /* start a timer that waits for the /var/run/.worker_config_complete flag */
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_WORKER_CONFIG_TIMEOUT );

                        /* We will come back to MTC_RECOVERY__HEARTBEAT_START
                         * after we enable the worker subfunction */
                        recoveryStageChange ( node_ptr, MTC_RECOVERY__CONFIG_COMPLETE_WAIT );
                    }
                    else
                    {
                        recoveryStageChange ( node_ptr, MTC_RECOVERY__STATE_CHANGE );
                    }
                }
                /* Otherwise in a normal system and not the active controller,
                 * just start the heartbeat soak */
                else if ( NOT_THIS_HOST )
                {
                    /* allow the fsm to wait for up to 1 minute for the
                     * hbsClient's ready event before starting heartbeat
                     * test. */
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_1 );
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__HEARTBEAT_START );
                }
                else
                {
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__STATE_CHANGE );
                }
            }
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s has GOENABLED Timeout\n", node_ptr->hostname.c_str());
                mtcInvApi_update_task ( node_ptr, MTC_TASK_MAIN_INTEST_TO );

                node_ptr->mtcTimer.ring = false ;

                this->force_full_enable ( node_ptr );
            }
            break;
        }
        case MTC_RECOVERY__CONFIG_COMPLETE_WAIT:
        {
            /* look for file */
            if ( node_ptr->mtce_flags & MTC_FLAG__SUBF_CONFIGURED )
            {
                plog ("%s-worker configured\n", node_ptr->hostname.c_str());

                mtcTimer_reset ( node_ptr->mtcTimer );

                recoveryStageChange ( node_ptr, MTC_RECOVERY__SUBF_GOENABLED_TIMER );
            }

            /* timeout handling */
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s-worker configuration timeout\n", node_ptr->hostname.c_str());

                mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_FAIL );
                nodeLinkClass::force_full_enable ( node_ptr );
            }
            else
            {
                ; /* wait longer */
            }
            break ;
        }
        case MTC_RECOVERY__SUBF_GOENABLED_TIMER:
        {
            ilog ("%s-worker running out-of-service tests\n", node_ptr->hostname.c_str());

            /* See if the host is there and already in the go enabled state */
            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_SUBF_GOENABLED, MGMNT_INTERFACE );

            /* start the reboot timer - is cought in the mtc alive case */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, this->goenabled_timeout );

            node_ptr->goEnabled_subf = false ;

            /* start waiting for the GOENABLED message */
            recoveryStageChange ( node_ptr, MTC_RECOVERY__SUBF_GOENABLED_WAIT );

            break ;
        }
        case MTC_RECOVERY__SUBF_GOENABLED_WAIT:
        {
            /* search for the Go Enable message */
            if ( node_ptr->goEnabled_failed_subf == true )
            {
                elog ("%s-worker one or more out-of-service tests failed\n", node_ptr->hostname.c_str());
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_FAIL );
                this->force_full_enable ( node_ptr );
            }

            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled_subf == true )
            {
                /* stop the timer */
                mtcTimer_reset ( node_ptr->mtcTimer );

                plog ("%s-worker passed  out-of-service tests\n", node_ptr->hostname.c_str());

                /* O.K. clearing the state now that we got it */
                node_ptr->goEnabled_subf        = false ;

                /* ok. great, got the go-enabled message, lets move on */
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_WORKER_CONFIG_TIMEOUT );
                recoveryStageChange ( node_ptr, MTC_RECOVERY__HEARTBEAT_START );
            }
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s-worker out-of-service test execution timeout\n", node_ptr->hostname.c_str());
                node_ptr->mtcTimer.ring = false ;
                mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_FAIL );
                this->force_full_enable ( node_ptr );
            }
            else
            {
                ; /* wait some more */
            }
            break ;
        }

        case MTC_RECOVERY__HEARTBEAT_START:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                wlog ("%s hbsClient ready event timeout\n", node_ptr->hostname.c_str());
            }
            else if ( node_ptr->hbsClient_ready == false )
            {
                 break ;
            }
            else
            {
                mtcTimer_reset ( node_ptr->mtcTimer );
            }

            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
               hbs_minor_clear ( node_ptr, (iface_enum)iface );
               node_ptr->heartbeat_failed[iface] = false ;
            }

            /* Enable the heartbeat service for Graceful Recovery */
            send_hbs_command ( node_ptr->hostname, MTC_CMD_START_HOST );

            if ( this->hbs_failure_action == HBS_FAILURE_ACTION__NONE )
            {
                /* Skip over the heartbeat soak if the failuer handlig is
                 * none because in that case heartbeating is disabled and
                 * would just be a waste of recovery time. */
                recoveryStageChange ( node_ptr, MTC_RECOVERY__STATE_CHANGE );
            }
            else
            {
                plog ("%s Starting %d sec Heartbeat Soak (with%s)\n",
                          node_ptr->hostname.c_str(),
                          MTC_HEARTBEAT_SOAK_BEFORE_ENABLE,
                          node_ptr->hbsClient_ready ? " ready event" : "out ready event"  );


                /* allow heartbeat to run for 10 seconds before we declare enable */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_HEARTBEAT_SOAK_BEFORE_ENABLE );

                /* if heartbeat is not working then we will
                 * never get here and enable the host */
                recoveryStageChange ( node_ptr, MTC_RECOVERY__HEARTBEAT_SOAK );
            }
            break ;
        }
        case MTC_RECOVERY__HEARTBEAT_SOAK:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                ilog ("%s heartbeating", node_ptr->hostname.c_str());
                /* if heartbeat is not working then we will
                 * never get here and enable the host */
                recoveryStageChange ( node_ptr, MTC_RECOVERY__STATE_CHANGE );
            }
            break ;
        }
        case MTC_RECOVERY__STATE_CHANGE:
        {
            if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                /* Set node as unlocked-enabled */
                subfStateChange ( node_ptr, MTC_OPER_STATE__ENABLED,
                                            MTC_AVAIL_STATUS__AVAILABLE );
            }

            if ( node_ptr->degrade_mask )
            {
                /* Allow host to enable in the degraded state */
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__ENABLED,
                                           MTC_AVAIL_STATUS__DEGRADED );
            }
            else
            {
                /* Set node as unlocked-enabled */
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__ENABLED,
                                           MTC_AVAIL_STATUS__AVAILABLE );
            }

            /* Inform the VIM that this host is enabled */
            mtcVimApi_state_change ( node_ptr, VIM_HOST_ENABLED, 3 );

            /* Start a timer that failed enable if the work queue
             * does not empty or if commands in the done queue have failed */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, work_queue_timeout );

            mtcInvApi_force_task ( node_ptr, "" );

            recoveryStageChange ( node_ptr, MTC_RECOVERY__WORKQUEUE_WAIT ) ;
            break ;
        }
        case MTC_RECOVERY__WORKQUEUE_WAIT:
        {
            rc = workQueue_done ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait longer */
                break ;
            }
            else if ( rc == PASS )
            {
                /* Start Graceful Recovery */
                recoveryStageChange ( node_ptr, MTC_RECOVERY__ENABLE ) ;
                break ;
            }
            else if ( rc == FAIL_WORKQ_TIMEOUT )
            {
                wlog ("%s Graceful Recovery failed ; workQueue empty timeout, purging ...\n", node_ptr->hostname.c_str());
                workQueue_purge ( node_ptr );
            }
            else if ( rc != PASS )
            {
                wlog ("%s Graceful Recovery failed ; doneQueue contains failed commands\n", node_ptr->hostname.c_str());
            }
            mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_FAIL );
            nodeLinkClass::force_full_enable ( node_ptr );
            break ;
        }
        case MTC_RECOVERY__ENABLE:
        {
            if ( is_controller(node_ptr) )
            {
                if ( mtcSmgrApi_request ( node_ptr,
                                          CONTROLLER_ENABLED,
                                          SMGR_MAX_RETRIES ) != PASS )
                {
                    wlog ("%s Failed to send 'unlocked-enabled' to HA Service Manager ; allowing enable\n",
                          node_ptr->hostname.c_str());
                }
            }
            /* Node Has Recovered */
            node_ptr->graceful_recovery_counter = 0 ;
            recoveryStageChange ( node_ptr, MTC_RECOVERY__START );
            adminActionChange   ( node_ptr, MTC_ADMIN_ACTION__NONE );
            node_ptr->health_threshold_counter = 0 ;
            node_ptr->enabled_count++ ;
            node_ptr->http_retries_cur = 0 ;

            doneQueue_purge ( node_ptr );
            if ( this->dor_mode_active )
            {
                report_dor_recovery (  node_ptr , "is ENABLED", "recovery" );
            }
            plog ("%s is ENABLED (Gracefully Recovered%s)",
                      node_ptr->hostname.c_str(),
                      this->dor_mode_active ? " in DOR mode" : "");
            alarm_enabled_clear ( node_ptr, false );
            break ;
        }
        default:
        {
            rc = FAIL_BAD_CASE ;
            break ;
        }
    }
    return (rc);
}

/*
 * Start Stop Host Services Handler
 * --------------------------------
 * Waits for the specified host services command to complete.
 *
 * Returns PASS      - command completed successfully
 *         RETRY     - command still running
 *         FAIL_xxxx - command failure for reason
 *
 */
int nodeLinkClass::host_services_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = FAIL ;

    if ( node_ptr && ( is_host_services_cmd ( node_ptr->host_services_req.cmd ) == true ))
    {
        /* Handle command overall umbrella timeout */
        if ( mtcTimer_expired ( node_ptr->host_services_timer ) )
        {
            elog ("%s %s timeout\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->host_services_req.name.c_str());

            /* treat as command failure */
            mtcCmd_workQ_purge ( node_ptr );
            mtcCmd_doneQ_purge ( node_ptr );
            rc = FAIL_TIMEOUT ;
        }

        /* Handle the case where both the done and work fifo's are empty.
         * ... yet this is the state while we are waiting for */
        else if (( node_ptr->mtcCmd_done_fifo.size() == 0 ) &&
                 ( node_ptr->mtcCmd_work_fifo.size() == 0 ))
        {
            mtcTimer_reset ( node_ptr->host_services_timer );
            slog ("%s %s command missing\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->host_services_req.name.c_str());
            rc = FAIL_BAD_STATE ;
        }

        /* look for 'done' case - pass and failed */
        else if (( node_ptr->mtcCmd_done_fifo.size() != 0 ) &&
                 ( node_ptr->mtcCmd_work_fifo.size() == 0 ))
        {
            mtcTimer_reset ( node_ptr->host_services_timer );
            if ( node_ptr->host_services_req.status == PASS )
            {
                ilog ("%s %s completed\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.name.c_str());
                rc = PASS ;
            }
            else
            {
                wlog ("%s %s ; rc:%d\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.status_string.c_str(),
                          node_ptr->host_services_req.status);

                rc = FAIL_OPERATION ;
            }
            /* Purge the done command fifo now that we have consumed the result.
             * The work fifo is already empty or we would not be in this case */
            mtcCmd_doneQ_purge ( node_ptr );
        }
        /* still working ... */
        else
        {
            /* wait longer */
            rc = RETRY ;
        }
    }
    else
    {
        slog ("%s invalid host services command (%d)\n",
                  node_ptr->hostname.c_str(),
                  node_ptr->cmd.parm1 );

        rc = FAIL_BAD_PARM ;
    }

    return (rc);
}


/* Disable handler
 * ---------------
 * Algorithm that puts a node into the operationally disabled state */
int nodeLinkClass::disable_handler  ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    switch ( (int)node_ptr->disableStage )
    {
        case MTC_DISABLE__START:
        {
            mtcTimer_reset ( node_ptr->mtcTimer );

            /* Purge this hosts work and done queues */
            workQueue_purge    ( node_ptr );
            doneQueue_purge    ( node_ptr );
            mtcCmd_workQ_purge ( node_ptr );
            mtcCmd_doneQ_purge ( node_ptr );

            /* clear all the enable failure bools */
            clear_main_failed_bools ( node_ptr );
            clear_subf_failed_bools ( node_ptr );

            enableStageChange  ( node_ptr, MTC_ENABLE__START ) ;
            disableStageChange ( node_ptr, MTC_DISABLE__DIS_SERVICES_WAIT) ;

            stop_offline_handler ( node_ptr );

            if (( node_ptr->bmc_provisioned == true ) &&
                ( node_ptr->bmc_accessible == true ) &&
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ))
            {
                    rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_ON );
                    if ( rc )
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                        disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_POWERON_SEND) ;
                    }
                    else
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                        disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_POWERON_RECV) ;
                    }

                if ( rc == PASS )
                {
                    ilog ("%s Power On request sent\n", node_ptr->hostname.c_str());
                }
            }

            if ( node_ptr->adminAction == MTC_ADMIN_ACTION__FORCE_LOCK )
            {
                mtc_nodeAvailStatus_enum locked_status = MTC_AVAIL_STATUS__OFFLINE ;
                plog ("%s Administrative 'force-lock' Operation\n", node_ptr->hostname.c_str());

                /* If the host was inservice then set its locked state as ONLINE for now.
                 * Otherwise its defaulted to offline */
                if (( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
                    ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED ) ||
                    ( node_ptr->availStatus == MTC_AVAIL_STATUS__INTEST ) ||
                    ( node_ptr->availStatus == MTC_AVAIL_STATUS__FAILED ))
                {
                    locked_status = MTC_AVAIL_STATUS__ONLINE ;
                }

                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__DISABLED,
                                           locked_status );

                if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
                {
                    subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                                locked_status );
                }
            }
            else
            {
                plog ("%s Administrative 'lock' Operation\n", node_ptr->hostname.c_str());
            }

            /* reset retries counter in prep for next stage */
            node_ptr->retries = 0 ;
            node_ptr->http_retries_cur = 0 ;
            node_ptr->pmond_ready = false ;

            /* Clear all degrade flags except for the HWMON one */
            clear_host_degrade_causes ( node_ptr->degrade_mask );

            if ( is_controller(node_ptr) )
            {
                mtcInvApi_update_task ( node_ptr, MTC_TASK_DISABLE_CONTROL );
            }
            // else
            // {
            //    consider putting in the host type
            // }

            if ( NOT_THIS_HOST )
            {
                /* Disable path for Controllers */
                if ( is_controller(node_ptr) )
                {
                    if ( mtcSmgrApi_request ( node_ptr,
                                              CONTROLLER_LOCKED,
                                              SMGR_MAX_RETRIES ) != PASS )
                    {
                        wlog ("%s Failed to send 'locked-disabled' to HA Service Manager\n",
                                  node_ptr->hostname.c_str() );
                    }
                }

                /* Clear the minor flag if it is set for this host */
                for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
                {
                    hbs_minor_clear ( node_ptr, (iface_enum)iface );
                }

                /* Turn off Heartbeat to that host */
                send_hbs_command ( node_ptr->hostname, MTC_CMD_STOP_HOST );
            }

            /* If the stage is still MTC_DISABLE__DIS_SERVICES_WAIT then the
             * host should already be powered on so lets send the stop
             * services command */
            if ( node_ptr->disableStage == MTC_DISABLE__DIS_SERVICES_WAIT )
            {
                bool start = false ;
                if ( this->launch_host_services_cmd ( node_ptr, start ) != PASS )
                {
                    wlog ("%s %s failed ; launch\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str());

                    /* proceed to handle force lock if the launch fails */
                    disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK );
                }
                else
                {
                    ilog ("%s %s launched",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str())
                }
            }
            break ;
        }
        case MTC_DISABLE__DIS_SERVICES_WAIT:
        {
            /* manage host services stop command to this target */
            rc = this->host_services_handler ( node_ptr );
            if ( rc == RETRY )
            {
                break ;
            }
            else if ( rc != PASS )
            {
                if ( rc == FAIL_TIMEOUT )
                {
                    wlog ("%s %s failed ; timeout\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str());
                }
                else
                {
                    wlog ("%s %s failed ; rc:%d\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str(),
                              rc);
                }
            }
            disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK) ;
            break ;
        }
        case MTC_DISABLE__HANDLE_POWERON_SEND:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_ON );
                if ( rc )
                {
                    elog ("%s failed to send Power On request\n", node_ptr->hostname.c_str());
                    disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK) ;
                }
                else
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                    disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_POWERON_RECV) ;
                }
            }
            break ;
        }
        case MTC_DISABLE__HANDLE_POWERON_RECV:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }
                if ( rc )
                {
                    elog ("%s auto power-on failed\n", node_ptr->hostname.c_str());
                }
                else
                {
                    ilog ("%s is Powering On\n", node_ptr->hostname.c_str());
                }
                disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK) ;
            }
            break ;
        }
        case MTC_DISABLE__HANDLE_FORCE_LOCK:
        {
            /* If this is a force lock against a worker then we have to reset it */
            if (( node_ptr->adminAction == MTC_ADMIN_ACTION__FORCE_LOCK ))
            {
                /* Stop the timer if it is active coming into this case */
                mtcTimer_reset ( node_ptr->mtcTimer );

                /* purge in support of retries */
                mtcCmd_doneQ_purge ( node_ptr );
                mtcCmd_workQ_purge ( node_ptr );

                ilog ("%s Issuing Force-Lock Reset\n", node_ptr->hostname.c_str());
                mtcCmd_init ( node_ptr->cmd );
                node_ptr->cmd_retries = 0 ;
                node_ptr->cmd.stage = MTC_CMD_STAGE__START ;
                node_ptr->cmd.cmd   = MTC_OPER__RESET_PROGRESSION ;
                node_ptr->cmd.parm1 = 2 ; /* 2 retries */
                node_ptr->mtcCmd_work_fifo.push_back(node_ptr->cmd);

                int timeout = ((MTC_RESET_PROG_TIMEOUT*(node_ptr->cmd.parm1+1))*2) ;
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, timeout ) ;

                mtcInvApi_update_task ( node_ptr, MTC_TASK_DISABLE_FORCE );

                /* Force instance evacuation */
                disableStageChange ( node_ptr, MTC_DISABLE__RESET_HOST_WAIT );
            }
            else
            {
                disableStageChange ( node_ptr, MTC_DISABLE__TASK_STATE_UPDATE ) ;
            }
            break ;
        }
        case MTC_DISABLE__RESET_HOST_WAIT:
        {
            /* Check for the operation timeout - should not occur */
            if ( node_ptr->mtcTimer.ring == true )
            {
                wlog ("%s Reset Progression Timeout ; aborting ...\n", node_ptr->hostname.c_str());

                /* Purge this hosts work and done queues */
                mtcCmd_doneQ_purge ( node_ptr );
                mtcCmd_workQ_purge ( node_ptr );

                /* aborting after timeout ; need to avoid a stuck FSM
                 * reset progression already did retries */
                mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOT_TIMEOUT );

                disableStageChange ( node_ptr, MTC_DISABLE__TASK_STATE_UPDATE );
            }

            /* Handle the case where the done fifo is empty ; avoid the segfault */
            else if ( node_ptr->mtcCmd_done_fifo.size() == 0 )
            {
                /* Should never get here but .....
                 * Handle the case where the work queue is also empty.
                 * Avoid stuck FSM */
                if ( node_ptr->mtcCmd_work_fifo.size() == 0 )
                {
                    slog ("%s unexpected empty work queue ; trying reboot/reset again\n",
                              node_ptr->hostname.c_str() );

                    /* reset progression failed so try again */
                    disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK );
                }
                else
                {
                    ; /* typical wait path - wait some more */
                }
            }
            else
            {
                /* TODO: Future: get the specific command rather than just the head */
                node_ptr->mtcCmd_done_fifo_ptr = node_ptr->mtcCmd_done_fifo.begin();

                /* defensive programming */
                if ( node_ptr->mtcCmd_done_fifo_ptr != node_ptr->mtcCmd_work_fifo.end())
                {
                    /* exit reset progression and any retries once the host is offline */
                    if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
                    {
                        mtcTimer_stop ( node_ptr->mtcTimer );
                        stop_offline_handler ( node_ptr );
                        disableStageChange ( node_ptr, MTC_DISABLE__TASK_STATE_UPDATE ) ;
                    }
                    else if ( node_ptr->mtcCmd_done_fifo_ptr->cmd != MTC_OPER__RESET_PROGRESSION )
                    {
                        slog ("%s purging front entry of done cmdQueue\n",
                                  node_ptr->hostname.c_str());

                        /* reset progression failed so try again */
                        disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK );
                    }
                    else
                    {
                        ilog ("%s host still not offline ; trying reboot/reset again ....\n", node_ptr->hostname.c_str() );

                        /* reset progression failed so try again */
                        disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK );
                    }
                }
                else
                {
                    slog ("%s unexpected empty work queue ; trying force lock\n", node_ptr->hostname.c_str() );

                    /* reset progression failed so try again */
                    disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK );
                }
            }
            break ;
        }

        case MTC_DISABLE__TASK_STATE_UPDATE:
        {
            mtc_nodeAvailStatus_enum avail ;

            /* Tell the host that it is locked */
            send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, MGMNT_INTERFACE );
            if ( clstr_network_provisioned )
            {
                send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, CLSTR_INTERFACE );
            }

            /* Change the oper and avail states in the database */
            if (( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE ) ||
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__FAILED ) ||
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ))
            {
                avail = MTC_AVAIL_STATUS__OFFLINE ;
            }
            else
            {
                avail = MTC_AVAIL_STATUS__ONLINE ;
            }
            allStateChange ( node_ptr, MTC_ADMIN_STATE__LOCKED, MTC_OPER_STATE__DISABLED, avail );
            mtcInvApi_subf_states (node_ptr,"disabled",get_availStatus_str(avail));

            /* Inform the VIM that this host is disabled */
            mtcVimApi_state_change ( node_ptr, VIM_HOST_DISABLED, 3 );

            /* Inform the VIM that the dataports are offline */
            update_dport_states (node_ptr, MTC_EVENT_AVS_OFFLINE );
            mtcVimApi_state_change ( node_ptr, VIM_DPORT_OFFLINE, 3 );

            /* Start a timer that waits for the work queue to complete */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, work_queue_timeout );
            disableStageChange( node_ptr, MTC_DISABLE__WORKQUEUE_WAIT );

            break ;
        }
        case MTC_DISABLE__WORKQUEUE_WAIT:
        {
            rc = workQueue_done ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait longer */
                break ;
            }
            else if ( rc == FAIL_WORKQ_TIMEOUT )
            {
                wlog ("%s Disable warning ; workQueue empty timeout, purging ...\n", node_ptr->hostname.c_str());
                workQueue_purge ( node_ptr );
            }
            else if ( rc != PASS )
            {
                wlog ("%s Disable warning ; doneQueue contained failed commands\n", node_ptr->hostname.c_str());
            }
            disableStageChange( node_ptr, MTC_DISABLE__DISABLED );
            break ;
        }
        case MTC_DISABLE__DISABLED:
        {
            /* Stop the timer if it is active coming into this case */
            mtcTimer_reset ( node_ptr->mtcTimer );

            /* This will get updated during the next
             * mtcLive message from this blade */
            node_ptr->health      = NODE_HEALTH_UNKNOWN       ;

            /* Set the lock alarm */
            if (( node_ptr->adminAction == MTC_ADMIN_ACTION__LOCK ) ||
                ( node_ptr->adminAction == MTC_ADMIN_ACTION__FORCE_LOCK ))
            {
                mtcAlarm_warning ( node_ptr->hostname, MTC_ALARM_ID__LOCK );
                node_ptr->alarms[MTC_ALARM_ID__LOCK] = FM_ALARM_SEVERITY_WARNING ;
            }

            /* open the mtcAlive gate while we are disabled */
            this->ctl_mtcAlive_gate ( node_ptr, false ) ;

            disableStageChange( node_ptr, MTC_DISABLE__START );
            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );

            node_ptr->mtcCmd_work_fifo.clear();
            node_ptr->mtcCmd_done_fifo.clear();
            node_ptr->http_retries_cur = 0 ;

            /***** Powercycle FSM Stuff *****/

            recovery_ctrl_init ( node_ptr->hwmon_reset );
            recovery_ctrl_init ( node_ptr->hwmon_powercycle );

            /* re-enable auto recovery */
            ar_enable ( node_ptr );

            /* Load configured mtcAlive and goEnabled timers */
            LOAD_NODETYPE_TIMERS ;

            mtcInvApi_force_task ( node_ptr, "" );

            plog ("%s Disable Complete\n", node_ptr->hostname.c_str());

            break ;
        }

        default:
        {
            elog ("%s Bad Case (%d)\n", node_ptr->hostname.c_str(),
                                        node_ptr->disableStage );
            rc = FAIL_BAD_CASE ;
        }
    }
    return (rc);
}

/* Uptime handler
 * ---------------*/
int nodeLinkClass::uptime_handler ( void )
{
    /* Service uptime refresh timer */
    if ( this->mtcTimer_uptime.ring == true )
    {
        int rc = PASS ;
        unsigned int uptime = 0;

        /* Send uptime valies to inventory */
        for ( this->host  = this->hostname_inventory.begin () ;
              this->host != this->hostname_inventory.end () ;
              this->host++ )
        {
            bool do_uptime_update = false ;
            string hostname = "" ;

            hostname.append( this->host->c_str()) ;

            /* only update every 5 minutes after being up for an hour */
            uptime = this->get_uptime ( hostname ) ;
            if ( uptime < 3600 )
            {
                do_uptime_update = true ;
            }
            else
            {
                int ctr = this->get_uptime_refresh_ctr ( hostname );

                /* Update uptime only every 5 minutes after the
                 * host has been up for more than one hour */
                if (( uptime > 3600 ) && ( (ctr*(this->uptime_period)) >= MTC_MINS_5 ))
                {
                    do_uptime_update = true ;
                }
                else
                {
                    this->set_uptime_refresh_ctr ( hostname , (ctr+1) ) ;
                }
            }
            /* Handle update if required */
            if (( rc != PASS ) && ( do_uptime_update == true ))
            {
                wlog ("%s Uptime refresh bypassed due to previous error\n", hostname.c_str());
            }
            else if (( do_uptime_update == true ) || ( uptime == 0 ))
            {
                /* Sent uptime update request.
                 * But exit this iteration if we get an error as we
                 * don't want to stall mtce for all hosts on such a
                 * simple operation */

                // ilog ("%s - %d\n", hostname.c_str(), uptime );
                if ( uptime == 0 )
                {
                    this->set_uptime ( hostname, uptime , false ) ;
                }
                else
                {
                    this->set_uptime ( hostname, uptime , true ) ;
                }
            }
        }
        /* Re-Start the uptime timer */
        mtcTimer_start ( this->mtcTimer_uptime, mtcTimer_handler,
                        (this->uptime_period+(rand()%10)));
    }
    return PASS ;
}

/* Offline handler
 * ---------------
 * Algorithm that manages offline/online state for a locked host */
int nodeLinkClass::offline_handler ( struct nodeLinkClass::node * node_ptr )
{
    switch ( (int)node_ptr->offlineStage )
    {
        case MTC_OFFLINE__IDLE:
        {
            return (PASS) ; /* typical path */
        }
        case MTC_OFFLINE__START:
        {
            node_ptr->mtcAlive_count = 0 ;
            node_ptr->mtcAlive_mgmnt = false ;
            node_ptr->mtcAlive_clstr = false ;
            node_ptr->mtcAlive_pxeboot = false ;
            node_ptr->offline_log_throttle = 0 ;
            node_ptr->offline_search_count = 0 ;

            mtcTimer_reset ( node_ptr->offline_timer );
            ilog ("%s starting %d msec offline audit (%s-%s)\n",
                      node_ptr->hostname.c_str(),
                      offline_period,
                      operState_enum_to_str(node_ptr->operState).c_str(),
                      availStatus_enum_to_str(node_ptr->availStatus).c_str());

            node_ptr->offlineStage = MTC_OFFLINE__SEND_MTCALIVE ;
            /* fall through on start */
            MTCE_FALLTHROUGH;
        }
        case MTC_OFFLINE__SEND_MTCALIVE:
        {
            alog2 ("%s searching for offline (%s-%s)\n",
                      node_ptr->hostname.c_str(),
                      operState_enum_to_str(node_ptr->operState).c_str(),
                      availStatus_enum_to_str(node_ptr->availStatus).c_str());

            this->ctl_mtcAlive_gate ( node_ptr, false ) ;

            /**
             * Handle the race condition case where the
             * mtcAlive was received after the last check
             * while in MTC_OFFLINE__WAIT below and here when
             * the node_ptr->mtcAlive_<iface> state variables
             * are cleared. Need to also clear the
             * offline_search_count here as well.
             **/
            if (( node_ptr->mtcAlive_mgmnt || node_ptr->mtcAlive_clstr || node_ptr->mtcAlive_pxeboot ) && node_ptr->offline_search_count )
            {
                node_ptr->mtcAlive_online = true ;
                ilog ("%s still seeing mtcAlive (%d) (Mgmt:%c:%d Clstr:%c:%d Pxeboot:%c:%d) ; restart offline_search_count=%d of %d\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->mtcAlive_count,
                          node_ptr->mtcAlive_mgmnt ? 'Y' : 'n',
                          node_ptr->mtcAlive_mgmnt_count,
                          node_ptr->mtcAlive_clstr ? 'Y' : 'n',
                          node_ptr->mtcAlive_clstr_count,
                          node_ptr->mtcAlive_pxeboot ? 'Y' : 'n',
                          node_ptr->mtcAlive_pxeboot_count,
                          node_ptr->offline_search_count,
                          offline_threshold );
                node_ptr->offline_search_count = 0 ; /* reset the count */
            }
            node_ptr->mtcAlive_mgmnt = false ;
            node_ptr->mtcAlive_clstr = false ;
            node_ptr->mtcAlive_pxeboot = false ;

            /* Request a mtcAlive from host from Mgmnt and Clstr (if provisioned) */
            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, MGMNT_INTERFACE );
            if ( clstr_network_provisioned )
            {
                send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, CLSTR_INTERFACE );
            }

            /* reload the timer */
            mtcTimer_start_msec ( node_ptr->offline_timer, mtcTimer_handler, offline_period );

            node_ptr->offlineStage = MTC_OFFLINE__WAIT ;

            break ;
        }
        case MTC_OFFLINE__WAIT:
        {
            /* be sure the mtcAlive gate is open */
            this->ctl_mtcAlive_gate (node_ptr, false ) ;
            if ( mtcTimer_expired ( node_ptr->offline_timer ) == true )
            {
                if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
                {
                    plog ("%s offline (external)\n", node_ptr->hostname.c_str());
                    node_ptr->offlineStage = MTC_OFFLINE__IDLE ;
                }
                else if ( !node_ptr->mtcAlive_mgmnt && !node_ptr->mtcAlive_clstr && !node_ptr->mtcAlive_pxeboot )
                {
                    if ( ++node_ptr->offline_search_count > offline_threshold )
                    {
                        node_ptr->mtcAlive_online = false ;
                        node_ptr->mtcClient_ready = false ;


                        // Clear all the mtcAlive counts and sequence numbers
                        node_ptr->mtcAlive_mgmnt_count = 0 ;
                        node_ptr->mtcAlive_clstr_count = 0 ;
                        node_ptr->mtcAlive_pxeboot_count = 0 ;
                        for (int i = 0 ; i < MTCALIVE_INTERFACES_MAX ; i++)
                            node_ptr->mtcAlive_sequence[i] = 0;

                        plog ("%s going offline ; (threshold (%d msec * %d)\n",
                                  node_ptr->hostname.c_str(),
                                  offline_period,
                                  offline_threshold );

                        availStatusChange ( node_ptr, MTC_AVAIL_STATUS__OFFLINE );

                        /* Inform the VIM that this host is offline */
                        mtcVimApi_state_change ( node_ptr, VIM_HOST_OFFLINE, 1 );

                        node_ptr->offlineStage = MTC_OFFLINE__IDLE ;
                    }
                    else
                    {
                        alog ("%s missed mtcAlive %d of %d times\n",
                                  node_ptr->hostname.c_str(),
                                  node_ptr->offline_search_count,
                                  offline_threshold );
                    }
                }
                else if ( node_ptr->offline_search_count )
                {
                   /**
                    * This algorithm was assuming the node is offline after
                    * offline_search_count reached offline_threshold count.
                    *
                    * Note: The mtcClient sends periodic mtcAlive messages
                    * until it is shutdown.
                    * This algorithm also explicitely 'requests' the message.
                    * The algorithm depends on not receving the message, even
                    * when requested for offline_threshold counts 'in a row'.
                    *
                    * When shutdown is slowed or delayed, a late mtcAlive
                    * can trick this FSM into seeing the node as recovered
                    * when in fact its still shuttingdown.
                    *
                    * To maintain the intended absence of mtcAlive messages
                    * count 'in a row', this check resets the search count
                    * if a mtcAlive is seen during the search.
                    **/

                    node_ptr->mtcAlive_online = true ;
                    ilog ("%s still seeing mtcAlive (%d) (Mgmt:%c:%d Clstr:%c:%d Pxeboot:%c:%d) ; restart offline_search_count=%d of %d\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->mtcAlive_count,
                              node_ptr->mtcAlive_mgmnt ? 'Y' : 'n',
                              node_ptr->mtcAlive_mgmnt_count,
                              node_ptr->mtcAlive_clstr ? 'Y' : 'n',
                              node_ptr->mtcAlive_clstr_count,
                              node_ptr->mtcAlive_pxeboot ? 'Y' : 'n',
                              node_ptr->mtcAlive_pxeboot_count,
                              node_ptr->offline_search_count,
                              offline_threshold );
                    node_ptr->offline_search_count = 0 ; /* reset the search count */
                }

                if ( node_ptr->offlineStage == MTC_OFFLINE__IDLE )
                {
                    ilog ("%s exiting offline handling\n", node_ptr->hostname.c_str());
                }
                else
                {
                    node_ptr->offlineStage = MTC_OFFLINE__SEND_MTCALIVE ;
                }
            }
            break ;
        }
        default:
        {
            slog ("%s unexpected stage ; correcting to idle\n",
                      node_ptr->hostname.c_str());

            node_ptr->offlineStage = MTC_OFFLINE__IDLE ;
        }
    }
    return (PASS);
}

/* Online handler
 * ---------------
 * Algorithm that manages offline/online state for a locked host */
int nodeLinkClass::online_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    /* don't need to manage the offline or online state
     * for the following states
     *  ... auto recovery state
     *  ... enable stages
     *  ... availability states */
    if (( node_ptr->ar_disabled == true ) ||
        ( node_ptr->enableStage == MTC_ENABLE__FAILURE ) ||
        ( node_ptr->enableStage == MTC_ENABLE__FAILURE_WAIT ) ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED  )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFDUTY   )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__INTEST    )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__NOT_INSTALLED ))
    {
        return (rc);
    }

    switch ( (int)node_ptr->onlineStage )
    {
        case MTC_ONLINE__START:
        {
            alog3 ("%s Offline Handler (%d)\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->onlineStage );

            if ( this->get_mtcAlive_gate ( node_ptr ) == true )
            {
                alog ("%s mtcAlive gate unexpectedly set, correcting ...\n",
                        node_ptr->hostname.c_str());

                this->ctl_mtcAlive_gate (node_ptr, false ) ;
            }

            /* Start with a zero count. This counter is incremented every
             * time we get a mtc alive message from that host */
            node_ptr->mtcAlive_online = false ;
            node_ptr->mtcAlive_misses = 0 ;

            /* Start mtcAlive message timer */
            mtcTimer_start ( node_ptr->online_timer, mtcTimer_handler, online_period );
            node_ptr->onlineStage = MTC_ONLINE__WAITING ;
            break ;
        }
        case MTC_ONLINE__RETRYING:
        {
            /* Start mtcAlive message timer */
            mtcTimer_start ( node_ptr->online_timer, mtcTimer_handler, online_period );
            node_ptr->onlineStage = MTC_ONLINE__WAITING ;
            break ;
        }
        case MTC_ONLINE__WAITING:
        {
            if ( node_ptr->online_timer.ring == false )
                break ;

            alog2 ("%s mtcAlive [%s]  [ misses:%d]\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->mtcAlive_online ? "Yes" : "No",
                      node_ptr->mtcAlive_misses );

            if ( node_ptr->mtcAlive_online == false )
            {
                node_ptr->mtcAlive_hits = 0 ;
                if ( node_ptr->mtcAlive_misses++ > MTC_OFFLINE_MISSES )
                {
                    /* If already online then and no counts then that means the node is not up - go offline */
                    if (( node_ptr->availStatus != MTC_AVAIL_STATUS__OFFLINE ) &&
                        ( node_ptr->availStatus != MTC_AVAIL_STATUS__POWERED_OFF ))
                    {
                        ilog ("%s mtcAlive lost ; going 'offline'\n",
                                  node_ptr->hostname.c_str());

                        clear_service_readies ( node_ptr );

                        /* otherwise change state */
                        mtcInvApi_update_state(node_ptr, MTC_JSON_INV_AVAIL,"offline" );
                        if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
                        {
                            mtcInvApi_update_state(node_ptr, MTC_JSON_INV_AVAIL_SUBF,"offline" );
                        }

                        /* Inform the VIM that this host is offline */
                        mtcVimApi_state_change ( node_ptr, VIM_HOST_OFFLINE, 1 );
                    }
                }
                else
                {
                    /* handle retries < MTC_OFFLINE_MISSES */
                    node_ptr->online_timer.ring = false ;
                    node_ptr->onlineStage = MTC_ONLINE__RETRYING ;
                    break ;
                }
            }
            else
            {
                bool gate_online = false ;

                /* if we are getting counts then the node is up so change status */
                if ( node_ptr->availStatus != MTC_AVAIL_STATUS__ONLINE )
                {
                    node_ptr->mtcAlive_hits++ ;
                    if ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF )
                    {
                        /* need 5 mtcAlive messages befpore we allow a power-off to go online */
                        if ( node_ptr->mtcAlive_hits < MTC_MTCALIVE_HITS_TO_GO_ONLINE )
                        {
                            gate_online = true ;
                            dlog ("%s ... %d\n", node_ptr->hostname.c_str(), node_ptr->mtcAlive_hits );
                        }
                    }

                    if ( gate_online == false )
                    {
                        ilog ("%s mtcAlive ; going 'online'\n",
                                  node_ptr->hostname.c_str());

                        mtcInvApi_update_state ( node_ptr, MTC_JSON_INV_AVAIL, "online" );
                        if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
                        {
                            mtcInvApi_update_state ( node_ptr, MTC_JSON_INV_AVAIL_SUBF, "online" );
                        }
                    }
                }
            }

            /* While the host is locked ... */
            if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
            {
                /* ... keep the 'host locked' file on this host refreshed while in the locked state
                 * ... send it on both interfaces just in case */
                send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, MGMNT_INTERFACE );
                if ( clstr_network_provisioned )
                    send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, CLSTR_INTERFACE );
            }

            /* Start over */
            node_ptr->online_timer.ring = false ;
            node_ptr->onlineStage = MTC_ONLINE__START ;
            break ;
        }
        default:
            node_ptr->onlineStage = MTC_ONLINE__START ;
    }
    return (rc);
}


/* Controller Swact Handler
 * ------------------------
 * Using a REST API into HA Service Manager through Inventory, this handler
 * is responsible for quering for active services on the specified
 * controller and then if services are found to be running , requesting
 * migration of those active services away from that controller */

#define SWACT_DONE \
{ \
    if ( node_ptr->mtcSwact_timer.tid ) \
    { \
        mtcTimer_stop ( node_ptr->mtcSwact_timer ); \
    } \
    mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY ); \
    node_ptr->swactStage = MTC_SWACT__DONE ; \
}

#define SWACT_FAIL_THRESHOLD    (3)
#define SWACT_RETRY_THRESHOLD  (10)
#define SWACT_FAIL_MSEC_DELAY (250)
#define SWACT_RECV_RETRY_DELAY  (1)
#define SWACT_POLL_DELAY       (10)
#define SWACT_TIMEOUT_DELAY    (50)

int nodeLinkClass::swact_handler  ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    if ( daemon_is_file_present ( PLATFORM_SIMPLEX_MODE ) == true )
    {
        slog ("%s rejecting Swact request in simplex mode\n", node_ptr->hostname.c_str());
        node_ptr->swactStage = MTC_SWACT__START ;
        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
        return (PASS);
    }
    switch ( (int)node_ptr->swactStage )
    {
        /* Start / Init Stage */
        case MTC_SWACT__START:
        {
            plog ("%s Administrative SWACT Requested\n", node_ptr->hostname.c_str() );

            /* Cleanup and init the swact timer - start fresh */
            if ( node_ptr->mtcSwact_timer.tid )
            {
                wlog ("%s Cancelling outstanding Swact timer\n", node_ptr->hostname.c_str());
                mtcTimer_stop ( node_ptr->mtcSwact_timer );
            }
            mtcTimer_init ( node_ptr->mtcSwact_timer );

            /* reset error / control Counters to zero */
            nodeLinkClass::smgrEvent.count   = 0 ;
            nodeLinkClass::smgrEvent.fails   = 0 ;
            nodeLinkClass::smgrEvent.cur_retries = 0 ;

            /* Empty the event message strings */
            nodeLinkClass::smgrEvent.payload = "" ;
            nodeLinkClass::smgrEvent.response = "" ;

            /* Post a user message 'Swact: Request' */
            mtcInvApi_force_task ( node_ptr, MTC_TASK_SWACT_REQUEST );
            node_ptr->swactStage = MTC_SWACT__QUERY ;
            break ;
        }

        /* Handle and threshold all Query Failures */
        case MTC_SWACT__QUERY_FAIL:
        {
            if ( ++nodeLinkClass::smgrEvent.fails >= SWACT_FAIL_THRESHOLD )
            {
                wlog ("%s Query Services Failed: Max Retries  (max:%d)\n",
                          node_ptr->hostname.c_str(), nodeLinkClass::smgrEvent.fails);
                mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_FAIL_QUERY);
                SWACT_DONE ;
            }
            else
            {
                wlog ("%s Query Services: Retrying (cnt:%d)\n",
                          node_ptr->hostname.c_str(), nodeLinkClass::smgrEvent.fails);
                mtcTimer_start_msec ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_FAIL_MSEC_DELAY );
                node_ptr->swactStage = MTC_SWACT__QUERY ;
            }

            /* avoid leaks in failure cases */
            mtcHttpUtil_free_conn  ( smgrEvent );
            mtcHttpUtil_free_base  ( smgrEvent );

            break ;
        }

        /* Query Services on this host */
        case MTC_SWACT__QUERY:
        {
            if ( node_ptr->mtcSwact_timer.ring == true )
            {
                rc = mtcSmgrApi_request ( node_ptr, CONTROLLER_QUERY, 0 );
                if ( rc )
                {
                    nodeLinkClass::smgrEvent.status = rc ;
                    node_ptr->swactStage = MTC_SWACT__QUERY_FAIL ;
                }
                else
                {
                    /* Ok, we got a successful send request ;
                     * delay a bit and check for the response */
                    nodeLinkClass::smgrEvent.cur_retries = 0 ;
                    nodeLinkClass::smgrEvent.fails   = 0 ;
                    mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_RETRY_DELAY );
                    node_ptr->swactStage = MTC_SWACT__QUERY_RECV ;
                }
            }
            break ;
        }

        case MTC_SWACT__QUERY_RECV:
        {
            if ( node_ptr->mtcSwact_timer.ring == true )
            {
                /* Try and receive the response */
                rc = mtcHttpUtil_receive ( nodeLinkClass::smgrEvent );
                if ( rc == RETRY )
                {
                    if ( ++nodeLinkClass::smgrEvent.cur_retries > SWACT_RETRY_THRESHOLD )
                    {
                        wlog ("%s Too many receive retries (cnt:%d)\n",
                                node_ptr->hostname.c_str(), nodeLinkClass::smgrEvent.cur_retries );
                        rc = FAIL ;
                    }
                    else
                    {
                        wlog ("%s Swact Query Request Receive Retry (%d of %d)\n",
                                node_ptr->hostname.c_str(),
                                nodeLinkClass::smgrEvent.cur_retries,
                                SWACT_RETRY_THRESHOLD);
                        mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_RETRY_DELAY );
                        break ;
                    }
                }
                if (( rc != PASS ) && ( rc != RETRY ))
                {
                    elog ("%s Service Query Failed: Receive Error (rc:%d)\n",
                              node_ptr->hostname.c_str(), rc );
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_FAILED);
                    SWACT_DONE ;
                }
                else
                {
                    /* Parse through the response - no retries on response string errors */
                    bool active = false ;
                    rc = mtcSmgrApi_service_state ( nodeLinkClass::smgrEvent, active );
                    if ( rc )
                    {
                        /* Setup common error message for the user*/
                        ilog ("%s Swact: Service Query Failed\n", node_ptr->hostname.c_str());
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_FAILED);
                        SWACT_DONE ;
                    }
                    else if ( active == true )
                    {
                        /* O.K. We need to Swact */
                        nodeLinkClass::smgrEvent.fails   = 0 ;
                        nodeLinkClass::smgrEvent.cur_retries = 0 ;
                        node_ptr->swactStage = MTC_SWACT__SWACT ;

                        /* Stop heartbeat of all unlocked-enabled system nodes during swact.
                         * The newly active controller will restart heartbeat on these nodes.
                         *
                         * Avoids transient management network heartbeat loss alarm set/clear
                         * during IPSec policy migration from one controller other */
                        for ( struct node * _ptr = head ; _ptr != NULL ; _ptr = _ptr->next )
                        {
                            if (( _ptr->hostname != this->my_hostname ) &&
                                ( _ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                                ( _ptr->operState == MTC_OPER_STATE__ENABLED ))
                            {
                                ilog ("%s heartbeat stop ; swacting", _ptr->hostname.c_str())
                                send_hbs_command  ( _ptr->hostname, MTC_CMD_STOP_HOST );
                            }
                        }

                        /* Tell the user what we are doing */
                        mtcInvApi_force_task ( node_ptr, MTC_TASK_SWACT_INPROGRESS );
                    }
                    else
                    {
                        /* If not true then somehow we are being asked to
                         * Swact a controller that is not running any services */
                        ilog ("%s %s\n", node_ptr->hostname.c_str(), MTC_TASK_SWACT_NOSERVICE);
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_NOSERVICE);
                        SWACT_DONE ;
                    }
                }
            }
            break ;
        }

        /* Phase 2: Perform Swact */
        case MTC_SWACT__SWACT:
        {
            rc = mtcSmgrApi_request ( node_ptr, CONTROLLER_SWACT, 0 );
            if ( rc )
            {
                /* Abort after SWACT_FAIL_THRESHOLD retries - verified */
                if ( ++nodeLinkClass::smgrEvent.fails >= SWACT_FAIL_THRESHOLD )
                {
                    elog ( "%s Swact: Failed Request (rc:%d) (max:%d)\n",
                               node_ptr->hostname.c_str(), rc,
                               nodeLinkClass::smgrEvent.fails);

                     mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_FAILED );
                     SWACT_DONE ;
                }
                else
                {
                    elog ( "%s Swact: Retrying Request (rc:%d) (cnt:%d)\n",
                               node_ptr->hostname.c_str(), rc,
                               nodeLinkClass::smgrEvent.fails);
                }
            }
            else
            {
                plog ("%s Swact: In Progress\n", node_ptr->hostname.c_str());
                nodeLinkClass::smgrEvent.status  = PASS ;
                nodeLinkClass::smgrEvent.fails   = 0 ;
                nodeLinkClass::smgrEvent.cur_retries = 0 ;
                mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_RETRY_DELAY );
                node_ptr->swactStage = MTC_SWACT__SWACT_RECV ;
            }
            break ;
        }

        case MTC_SWACT__SWACT_RECV:
        {
            if ( node_ptr->mtcSwact_timer.ring == true )
            {
                /* Try and receive the response */
                rc = mtcHttpUtil_receive ( nodeLinkClass::smgrEvent );
                if ( rc == RETRY )
                {
                    if ( ++nodeLinkClass::smgrEvent.cur_retries > SWACT_RETRY_THRESHOLD )
                    {
                        wlog ("%s Too many receive retries (cnt:%d)\n",
                                node_ptr->hostname.c_str(), nodeLinkClass::smgrEvent.cur_retries );
                        rc = FAIL ;
                    }
                    else
                    {
                        wlog ("%s Swact Request Receive Retry (%d of %d)\n",
                                  node_ptr->hostname.c_str(),
                                  nodeLinkClass::smgrEvent.cur_retries,
                                  SWACT_RETRY_THRESHOLD );
                        mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_RETRY_DELAY );
                        break ;
                    }
                }
                if (( rc != PASS ) && ( rc != RETRY ))
                {
                    elog ("%s Swact Failed: Receive Error (rc:%d)\n",
                              node_ptr->hostname.c_str(), rc );
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_FAILED);
                    SWACT_DONE ;
                }
                else
                {
                    mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, MTC_SWACT_POLL_TIMER );
                    mtcSmgrApi_request ( node_ptr, CONTROLLER_QUERY, 0 );
                    node_ptr->swactStage = MTC_SWACT__SWACT_POLL ;
                }
            }
            break ;
        }

        case MTC_SWACT__SWACT_POLL:
        {
            if ( node_ptr->mtcSwact_timer.ring == true )
            {
                if (++nodeLinkClass::smgrEvent.count >=
                   (nodeLinkClass::swact_timeout/MTC_SWACT_POLL_TIMER))
                {
                    elog ("%s Swact Failed: Timeout\n", node_ptr->hostname.c_str());
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_TIMEOUT);
                    SWACT_DONE ;
                }
                rc = mtcHttpUtil_receive ( smgrEvent  );
                if ( rc != RETRY )
                {
                    bool active = true ;
                    mtcSmgrApi_service_state ( smgrEvent, active );
                    if ( active == false )
                    {
                        dlog ("%s %s\n",node_ptr->hostname.c_str(), MTC_TASK_SWACT_COMPLETE );
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_COMPLETE );
                        SWACT_DONE ;
                        break ;
                    }
                    else
                    {
                        mtcSmgrApi_request ( node_ptr, CONTROLLER_QUERY, 0 );
                    }
                }
                else
                {
                    plog ("%s Swact: In-Progress\n", node_ptr->hostname.c_str());
                }
                mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, MTC_SWACT_POLL_TIMER );
            }
            break ;
        }
        case MTC_SWACT__DONE:
        {
            /* Wait for the done timer to expire.
             * When it does ; exit the SWACT FSM after clearing
             * the task and setting it back to the start. */
            if ( node_ptr->mtcSwact_timer.ring == true )
            {
                mtcInvApi_force_task ( node_ptr, "");
                nodeLinkClass::smgrEvent.active = false ;
                nodeLinkClass::smgrEvent.mutex  = false ;
                node_ptr->mtcSwact_timer.ring   = false ;
                node_ptr->swactStage = MTC_SWACT__START ;
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                if ( smgrEvent.status )
                {
                    wlog ("%s Swact: Failed\n", node_ptr->hostname.c_str());

                }
                else
                {
                    plog ("%s Swact: Completed\n", node_ptr->hostname.c_str());
                }
            }
            break;
        }

        default:
            node_ptr->swactStage = MTC_SWACT__START ;
    }
    return (rc);
}

/* Reset Handler
 *  ------------
 * Issue a reset to a host */
int nodeLinkClass::reset_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;
    switch ( node_ptr->resetStage )
    {
        case MTC_RESET__FAIL:
        {
            elog ("%s Reset failed ; aborting after max retries\n", node_ptr->hostname.c_str());
            stop_offline_handler ( node_ptr );
            mtcInvApi_update_task ( node_ptr, MTC_TASK_RESET_FAIL);
            mtcTimer_reset ( node_ptr->mtcTimer );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
            resetStageChange ( node_ptr , MTC_RESET__FAIL_WAIT );
            break ;
        }
        case MTC_RESET__FAIL_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                node_ptr->mtcTimer.ring = false ;
                resetStageChange ( node_ptr , MTC_RESET__DONE );

                recovery_ctrl_init ( node_ptr->hwmon_reset ); ;
                mtcTimer_reset ( node_ptr->hwmon_reset.recovery_timer );
            }
            break ;
        }
        case MTC_RESET__START:
        {
            plog ("%s Administrative 'Reset' Action\n", node_ptr->hostname.c_str());
            mtcInvApi_update_task ( node_ptr, "Reset Requested" );
            node_ptr->retries = 0 ;

            start_offline_handler ( node_ptr );

            if ( hostUtil_is_valid_ip_addr (node_ptr->bm_ip ) == false )
            {
                /**
                 *  New working provisioning is learned by from the
                 *  dnsmasq.bmc_hosts file changes through inotify watch so
                 *  it is entirely possible that the retries in this fsm
                 *  eventually succeed.
                 **/
                wlog ("%s bm_ip (%s) is invalid (%d) \n",
                          node_ptr->hostname.c_str(),
                          node_ptr->bm_ip.c_str(),
                          rc );
                resetStageChange ( node_ptr , MTC_RESET__FAIL );
                break ;
            }
            node_ptr->power_action_retries = MTC_RESET_ACTION_RETRY_COUNT ;
            /* the fall through is intentional */
            MTCE_FALLTHROUGH;
        }
        case MTC_RESET__REQ_SEND:
        {

            /* Handle loss of connectivity over retries  */
            if ( node_ptr->bmc_provisioned == false )
            {
                elog ("%s BMC not provisioned\n", node_ptr->hostname.c_str() );
                mtcInvApi_force_task ( node_ptr, MTC_TASK_BMC_NOT_PROV );
                resetStageChange ( node_ptr , MTC_RESET__FAIL );
                break ;
            }

            if ( node_ptr->bmc_accessible == false )
            {
                wlog ("%s Reset request rejected ; BMC not accessible ; retry in %d seconds \n",
                          node_ptr->hostname.c_str(),
                          MTC_RESET_ACTION_RETRY_DELAY);

                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RESET_ACTION_RETRY_DELAY );
                resetStageChange ( node_ptr , MTC_RESET__QUEUE );
                break ;
            }

            else
            {
                rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_RESET );
                if ( rc )
                {
                    wlog ("%s Reset request failed (%d)\n", node_ptr->hostname.c_str(), rc );
                    resetStageChange ( node_ptr , MTC_RESET__QUEUE );
                }
                else
                {
                    blog ("%s Reset requested\n", node_ptr->hostname.c_str());
                    resetStageChange ( node_ptr , MTC_RESET__RESP_WAIT );
                }
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RESET_ACTION_RETRY_DELAY );
            }
            break ;
        }

        case MTC_RESET__RESP_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }
                else if ( rc )
                {
                    elog ("%s Reset command failed (rc:%d)\n", node_ptr->hostname.c_str(), rc );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RESET_ACTION_RETRY_DELAY );
                    resetStageChange ( node_ptr, MTC_RESET__QUEUE );
                }
                else
                {
                    ilog ("%s is Resetting\n", node_ptr->hostname.c_str());
                    mtcInvApi_update_task ( node_ptr, "Resetting: waiting for offline" );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RESET_TO_OFFLINE_TIMEOUT );
                    resetStageChange ( node_ptr, MTC_RESET__OFFLINE_WAIT );
                }
            }
            break ;
        }

        case MTC_RESET__QUEUE:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                node_ptr->mtcTimer.ring = false ;
                if ( --node_ptr->power_action_retries >= 0 )
                {
                    char buffer[64] ;
                    int attempts = MTC_RESET_ACTION_RETRY_COUNT - node_ptr->power_action_retries ;
                    snprintf ( buffer, 64, MTC_TASK_RESET_QUEUE, attempts, MTC_RESET_ACTION_RETRY_COUNT);
                    mtcInvApi_update_task ( node_ptr, buffer);

                    /* check the thread error status if thetre is one */
                    if ( node_ptr->bmc_thread_info.status )
                    {
                        wlog ("%s ... %s (rc:%d)\n", node_ptr->hostname.c_str(),
                                                     node_ptr->bmc_thread_info.status_string.c_str(),
                                                     node_ptr->bmc_thread_info.status );
                    }

                    resetStageChange ( node_ptr , MTC_RESET__REQ_SEND );
                }
                else
                {
                    resetStageChange ( node_ptr , MTC_RESET__FAIL );
                }
            }
            break ;
        }

        case MTC_RESET__OFFLINE_WAIT:
        {
             if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
             {
                 if (node_ptr->mtcTimer.tid)
                     mtcTimer_stop ( node_ptr->mtcTimer );

                 plog ("%s Reset Successful\n", node_ptr->hostname.c_str());
                 resetStageChange ( node_ptr , MTC_RESET__DONE );
             }
             else if ( node_ptr->mtcTimer.ring == true )
             {
                 elog ("%s Reset operation timeout - host did not go offline\n", node_ptr->hostname.c_str());
                 resetStageChange ( node_ptr , MTC_RESET__FAIL );
             }
             break ;
        }

        case MTC_RESET__DONE:
        default:
        {
            mtcTimer_reset ( node_ptr->mtcTimer );

            hwmon_recovery_monitor ( node_ptr, MTC_EVENT_HWMON_RESET );

            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );

            mtcInvApi_force_task ( node_ptr, "" );

            clear_service_readies ( node_ptr );

            plog ("%s Reset Completed\n", node_ptr->hostname.c_str());
            break ;
        }
    }
    return (PASS);
}

/****************************************************************************
 *
 * Name       : reinstall_handler
 *
 * Purpose    : Perform actions that result in a network boot so that a new
 *              image is installed on the specified node's boot partition.
 *
 * Description: This FSM handles node (re)install with and without
 *              a provisioned Board Management Controller (BMC).
 *
 *              BMC provisioned case: board management commands to BMC ...
 *
 *                  - power off host
 *                  - force network boot on next reset
 *                  - power on host
 *
 *              BMC not provisioned case: mtce messaging to node ...
 *
 *                  - host must be online
 *                  - send mtcClient wipedisk command
 *                       fail reinstall if no ACK
 *                  - send mtcClient reboot command
 *
 *              Both casess:
 *
 *                  - wait for offline
 *                  - wait for online
 *                  - install complete
 *
 * Failure Handling:
 *
 *     BMC provisioned cases:
 *
 *          BMC won't power on
 *          BMC command failure
 *          BMC connectivity lost mid-FSM.
 *          BMC access timeout
 *
 *     BMC not provisioned cases:
 *
 *          no  wipedisk ACK\
 *
 *     failure to go offline after resaet/reboot
 *     timeout waiting for online after reset/reboot
 *
 * Manage reinstall operations for a locked-disabled host */
int nodeLinkClass::reinstall_handler ( struct nodeLinkClass::node * node_ptr )
{
    /* Handle 'lost BMC connectivity during the install' case */
    if (( node_ptr->bmc_provisioned == true ) &&
        ( node_ptr->bmc_accessible == false ))
    {
        if (( node_ptr->reinstallStage != MTC_REINSTALL__START )       &&
            ( node_ptr->reinstallStage != MTC_REINSTALL__START_WAIT )  &&
            ( node_ptr->reinstallStage != MTC_REINSTALL__FAIL )        &&
            ( node_ptr->reinstallStage != MTC_REINSTALL__MSG_DISPLAY ) &&
            ( node_ptr->reinstallStage != MTC_REINSTALL__DONE ))
        {
            mtcTimer_reset ( node_ptr->mtcTimer );

            elog ("%s Reinstall lost bmc connection",
                      node_ptr->hostname.c_str());

            mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_CL );
            reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
        }

        /* fall into switch to ...
         *  - handle failure
         *  - finish the FSM
         */
    }
    switch ( node_ptr->reinstallStage )
    {
        case MTC_REINSTALL__START:
        {
            LOAD_NODETYPE_TIMERS ;
            mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL );
            node_ptr->retries = ( node_ptr->mtcalive_timeout +
                                  this->node_reinstall_timeout) /
                                  MTC_REINSTALL_WAIT_TIMER ;
            mtcTimer_reset ( node_ptr->mtcTimer );
            if ( node_ptr->bmc_provisioned == true )
            {
                if ( node_ptr->bmc_accessible == false )
                {
                    /* Handle 'lost BMC connectivity during the install' case */
                    wlog ("%s Reinstall wait for BMC access ; %d second timeout",
                              node_ptr->hostname.c_str(),
                              MTC_REINSTALL_TIMEOUT_BMC_ACC);

                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_WAIT_NA );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_REINSTALL_TIMEOUT_BMC_ACC );
                    reinstallStageChange ( node_ptr, MTC_REINSTALL__START_WAIT );
                }
                else
                {
                    node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__POWERQRY );
                }
            }
            else
            {
                /* If the BMC is not provisioned coming into this handler
                 * then service the install by mtce commands by starting
                 * the install by wipedisk. */
                reinstallStageChange ( node_ptr, MTC_REINSTALL__WIPEDISK );
            }
            break ;
        }
        /* BMC provisioned but bmc_handler has not reported accessability yet.
         * Need to wait ... */
        case MTC_REINSTALL__START_WAIT:
        {
            if ( node_ptr->bmc_provisioned == true )
            {
                if ( node_ptr->bmc_accessible == false )
                {
                    if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
                    {
                        /* wait period has timed out ; fail the install */
                        elog ("%s %s", node_ptr->hostname.c_str(), MTC_TASK_REINSTALL_FAIL_BA);
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_BA );
                        reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
                    }
                    else
                    {
                        ; /* ... wait longer */
                    }
                }
                else
                {
                    /* the BMC is not accessible to start the install over */
                    plog ("%s BMC access established ; starting install",
                              node_ptr->hostname.c_str());
                    mtcTimer_reset ( node_ptr->mtcTimer );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__START );
                }
            }
            else
            {
                /*
                 * Handle case where BMC gets deprovisioned
                 * while waiting for accessibility.
                 *
                 * Restart the install in that case after a monitored
                 * wait period for reprovision.
                 *
                 * Has the side effect of allowing the admin to
                 * reprovision the BMC during a re-install.
                 */

                mtcTimer_reset ( node_ptr->mtcTimer );
                wlog ("%s %s", node_ptr->hostname.c_str(), MTC_TASK_REINSTALL_RTRY_PC );

                mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_RTRY_PC );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_5 );
                reinstallStageChange ( node_ptr, MTC_REINSTALL__RESTART_WAIT );
            }
            break ;
        }
        case MTC_REINSTALL__RESTART_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                reinstallStageChange ( node_ptr , MTC_REINSTALL__START );
            }
            else if ( node_ptr->bmc_provisioned == true )
            {
                mtcTimer_reset ( node_ptr->mtcTimer );
                wlog ("%s %s", node_ptr->hostname.c_str(), MTC_TASK_REINSTALL_RTRY_PC );
                reinstallStageChange ( node_ptr , MTC_REINSTALL__START );
            }
            else
            {
                ; /* ... wait longer */
            }
            break ;
        }
        case MTC_REINSTALL__POWERQRY:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) == false )
                ; // wait for time to expire
            else if ( node_ptr->bmc_thread_ctrl.done )
            {
                /* Query Host Power Status */
                int rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_STATUS );
                if ( rc != PASS )
                {
                    if ( --node_ptr->power_action_retries <= 0 )
                    {
                        elog ("%s Reinstall power query send failed ; max retries (rc:%d)",
                                  node_ptr->hostname.c_str(), rc );
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_NB );
                        reinstallStageChange ( node_ptr, MTC_REINSTALL__FAIL );
                        pingUtil_restart ( node_ptr->bm_ping_info );
                    }
                    else
                    {
                        elog ("%s Reinstall power query send failed ; retry %d of %d in %d seconds (rc:%d)",
                                  node_ptr->hostname.c_str(),
                                  MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries,
                                  MTC_POWER_ACTION_RETRY_COUNT,
                                  MTC_RETRY_WAIT, rc );
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    }
                }
                else
                {
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__POWERQRY_WAIT );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
            }
            else
            {
                thread_kill ( node_ptr->bmc_thread_ctrl , node_ptr->bmc_thread_info ) ;
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
            }
            break ;
        }
        case MTC_REINSTALL__POWERQRY_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                bool retry = false ; /* force retry on any failure */

                int rc = bmc_command_recv ( node_ptr ) ;
                if ( rc == RETRY )
                {
                    dlog ("%s power query receive retry in %d seconds",
                              node_ptr->hostname.c_str(), MTC_RETRY_WAIT);
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
                else if ( rc )
                {
                    retry = true ;
                }
                else if ( node_ptr->bmc_thread_info.data.empty() )
                {
                    retry = true ;
                    wlog ("%s Reinstall power query failed ; no response data",
                              node_ptr->hostname.c_str());
                }
                else
                {
                    int rc =
                    bmcUtil_is_power_on ( node_ptr->hostname,
                                          node_ptr->bmc_protocol,
                                          node_ptr->bmc_thread_info.data,
                                          node_ptr->power_on);
                    if ( rc == PASS )
                    {
                        if ( node_ptr->power_on == true )
                        {
                            ilog ("%s Reinstall power-off required",
                                      node_ptr->hostname.c_str());
                            reinstallStageChange ( node_ptr , MTC_REINSTALL__POWEROFF );
                        }
                        else
                        {
                            ilog ("%s Reinstall power-off already",
                                      node_ptr->hostname.c_str());
                            reinstallStageChange ( node_ptr , MTC_REINSTALL__NETBOOT );
                            node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;
                        }
                        break ;
                    }
                    else
                    {
                        retry = true ;
                        elog ("%s Reinstall power query failed (rc:%d)",
                                  node_ptr->hostname.c_str(), rc );
                    }
                }
                if ( retry == true )
                {
                    if ( --node_ptr->power_action_retries <= 0 )
                    {
                        elog ("%s Reinstall power query receive failed ; max retries (rc:%d)",
                                  node_ptr->hostname.c_str(), rc );
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_PQ );
                        reinstallStageChange ( node_ptr, MTC_REINSTALL__FAIL );
                    }
                    else
                    {
                        wlog ("%s Reinstall power query receive failed ; retry %d of %d in %d seconds (rc:%d)",
                                  node_ptr->hostname.c_str(),
                                  MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries,
                                  MTC_POWER_ACTION_RETRY_COUNT,
                                  MTC_RETRY_WAIT, rc );
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                        reinstallStageChange ( node_ptr , MTC_REINSTALL__POWERQRY );
                        /* stay in case ; send retry in MTC_RETRY_WAIT seconds */
                    }
                    if ( ! node_ptr->bmc_thread_ctrl.done )
                    {
                        thread_kill ( node_ptr->bmc_thread_ctrl , node_ptr->bmc_thread_info ) ;
                    }
                }
            }
            else
            {
                ; /* wait longer */
            }
            break ;
        }

        case MTC_REINSTALL__POWEROFF:
        {
            node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;
            mtcTimer_reset ( node_ptr->mtcTimer ) ;
            powerStageChange ( node_ptr, MTC_POWEROFF__REQ_SEND );
            reinstallStageChange ( node_ptr , MTC_REINSTALL__POWEROFF_WAIT );
            break ;
        }
        case MTC_REINSTALL__POWEROFF_WAIT:
        {
            /* The power handler manages timeout */
            if ( node_ptr->powerStage == MTC_POWER__DONE )
            {
                if ( node_ptr->power_on == false )
                {
                    if ( node_ptr->task != MTC_TASK_REINSTALL )
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL );

                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__NETBOOT );
                    node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;
                }
                else
                {
                    elog ("%s %s", node_ptr->hostname.c_str(), MTC_TASK_REINSTALL_FAIL_PO);

                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_PO );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
                }
            }
            else
            {
                /* run the power handler till the host's power is on or
                 * the power-on handler times out */
                power_handler ( node_ptr );
            }
            break ;
        }
        case MTC_REINSTALL__NETBOOT:
        {
            /* Issue netboot command after timed delay */
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                int rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__BOOTDEV_PXE );
                if ( rc )
                {
                    /* handle max retries */
                    if ( --node_ptr->power_action_retries <= 0 )
                    {
                        elog ("%s Reinstall netboot send failed ; max retries (rc:%d)",
                                  node_ptr->hostname.c_str(), rc );
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_NB );
                        reinstallStageChange ( node_ptr, MTC_REINSTALL__FAIL );
                    }
                    else
                    {
                        wlog ("%s netboot request send failed ; retry %d of %d in %d seconds (rc:%d)",
                                  node_ptr->hostname.c_str(),
                                  MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries,
                                  MTC_POWER_ACTION_RETRY_COUNT,
                                  MTC_RETRY_WAIT, rc );
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                        /* stay in case, retry in 5 seconds */
                    }
                }
                else
                {
                    dlog ("%s Reinstall netboot request sent", node_ptr->hostname.c_str() );
                    reinstallStageChange ( node_ptr, MTC_REINSTALL__NETBOOT_WAIT );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_SECS_2 );
                }
            }
            break ;
        }
        case MTC_REINSTALL__NETBOOT_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                bool retry = false ;

                int rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    dlog ("%s netboot receive retry in %d seconds",
                              node_ptr->hostname.c_str(), MTC_SECS_2);
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_SECS_2 );
                }
                else if ( rc )
                {
                    retry = true ;
                }
                else
                {
                    ilog ("%s Reinstall netboot request completed", node_ptr->hostname.c_str());
                    reinstallStageChange ( node_ptr, MTC_REINSTALL__POWERON);
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                }

                if ( retry == true )
                {
                    if ( --node_ptr->power_action_retries <= 0 )
                    {
                        elog ("%s Reinstall netboot receive failed ; max retries (rc:%d)",
                                  node_ptr->hostname.c_str(), rc );
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_NB );
                        reinstallStageChange ( node_ptr, MTC_REINSTALL__FAIL );
                    }
                    else
                    {
                        wlog ("%s Reinstall netboot receive failed ; retry %d of %d in %d seconds (rc:%d)",
                                  node_ptr->hostname.c_str(),
                                  MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries,
                                  MTC_POWER_ACTION_RETRY_COUNT,
                                  MTC_RETRY_WAIT, rc );
                        reinstallStageChange ( node_ptr , MTC_REINSTALL__NETBOOT );
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    }
                    if ( ! node_ptr->bmc_thread_ctrl.done )
                    {
                        thread_kill ( node_ptr->bmc_thread_ctrl , node_ptr->bmc_thread_info ) ;
                    }
                }
            }
            break ;
        }
        case MTC_REINSTALL__POWERON:
        {
            if ( ! mtcTimer_expired ( node_ptr->mtcTimer ))
                break ;

            node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;
            powerStageChange ( node_ptr , MTC_POWERON__REQ_SEND );
            reinstallStageChange ( node_ptr , MTC_REINSTALL__POWERON_WAIT );
            break ;
        }
        case MTC_REINSTALL__POWERON_WAIT:
        {
            /* The power handler manages timeout */
            if ( node_ptr->powerStage == MTC_POWER__DONE )
            {
                if ( node_ptr->power_on == true )
                {
                    if ( node_ptr->task != MTC_TASK_REINSTALL )
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL );

                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__OFFLINE_WAIT );
                }
                else
                {
                    elog ("%s %s", node_ptr->hostname.c_str(), MTC_TASK_REINSTALL_FAIL_PU);

                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_PU );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
                }
            }
            else
            {
                /* run the power handler till the host's power is on or
                 * the power-on handler times out */
                power_handler ( node_ptr );
            }
            break ;
        }
        /* BMC not provisioned case */
        case MTC_REINSTALL__WIPEDISK:
        {
            node_ptr->cmdReq = MTC_CMD_WIPEDISK ;

            plog ("%s Reinstall wipedisk requested", node_ptr->hostname.c_str());
            if ( send_mtc_cmd ( node_ptr->hostname, MTC_CMD_WIPEDISK, MGMNT_INTERFACE ) != PASS )
            {
                elog ("%s Reinstall request send failed", node_ptr->hostname.c_str());
                mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL );
                reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
            }
            else
            {
                node_ptr->cmdRsp = MTC_CMD_NONE ;

                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_CMD_RSP_TIMEOUT );

                reinstallStageChange ( node_ptr , MTC_REINSTALL__WIPEDISK_WAIT );
            }
            break ;
        }
        case MTC_REINSTALL__WIPEDISK_WAIT:
        {
            if ( node_ptr->cmdRsp != MTC_CMD_WIPEDISK )
            {
                if ( node_ptr->mtcTimer.ring == true )
                {
                    elog ("%s Reinstall wipedisk ACK timeout",
                        node_ptr->hostname.c_str());
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
                }
            }
            else
            {
                /* declare successful reinstall request */
                plog ("%s Reinstall request succeeded", node_ptr->hostname.c_str());

                mtcTimer_reset ( node_ptr->mtcTimer );

                start_offline_handler ( node_ptr );

                /* We need to wait for the host to go offline */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RESET_TO_OFFLINE_TIMEOUT );

                /* Wait for the host to go offline */
                reinstallStageChange ( node_ptr , MTC_REINSTALL__OFFLINE_WAIT );
            }
            break ;
        }
        case MTC_REINSTALL__OFFLINE_WAIT:
        {
            if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
            {
                mtcTimer_stop ( node_ptr->mtcTimer );

                clear_service_readies ( node_ptr );

                ilog ("%s Reinstall in-progress ; waiting for 'online' state",
                          node_ptr->hostname.c_str());

                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_REINSTALL_WAIT_TIMER );
                reinstallStageChange ( node_ptr , MTC_REINSTALL__ONLINE_WAIT );
            }
            else if ( mtcTimer_expired (  node_ptr->mtcTimer ) )
            {
                elog ("%s failed to go offline ; timeout", node_ptr->hostname.c_str());
                stop_offline_handler ( node_ptr );
                mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_OL );
                reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
            }
            else
            {
                ; // wait longer ...
            }
            break ;
        }
        case MTC_REINSTALL__ONLINE_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                if ( --node_ptr->retries < 0 )
                {
                    elog ("%s %s", node_ptr->hostname.c_str(), MTC_TASK_REINSTALL_FAIL_TO);
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL_TO );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
                }
                else
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_REINSTALL_WAIT_TIMER );
                }
            }
            else if ( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE )
            {
                mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_SUCCESS);
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
                reinstallStageChange ( node_ptr , MTC_REINSTALL__MSG_DISPLAY );
                mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_REINSTALL_COMPLETE );
            }
            else
            {
                ; // wait longer ...
            }
            break;
        }
        case MTC_REINSTALL__FAIL:
        {
            mtcTimer_reset ( node_ptr->mtcTimer );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
            reinstallStageChange ( node_ptr , MTC_REINSTALL__MSG_DISPLAY );
            mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_REINSTALL_FAILED );
            break ;
        }
        case MTC_REINSTALL__MSG_DISPLAY:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                reinstallStageChange ( node_ptr , MTC_REINSTALL__DONE );
            }
            else
            {
                ; // wait longer ...
            }
            break ;
        }
        case MTC_REINSTALL__DONE:
        default:
        {
            if ( node_ptr->task == MTC_TASK_REINSTALL_SUCCESS )
            {
                plog ("%s Reinstall completed successfully",
                          node_ptr->hostname.c_str());
            }
            else
            {
                plog ("%s Reinstall complete ; operation failure",
                          node_ptr->hostname.c_str());
            }

            /* Default timeout values */
            LOAD_NODETYPE_TIMERS ;

            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );

            recovery_ctrl_init ( node_ptr->hwmon_reset );
            recovery_ctrl_init ( node_ptr->hwmon_powercycle );

            mtcInvApi_force_task ( node_ptr, "" );
            break ;
        }
    }
    return (PASS);
}

/* Reboot handler
 * --------------
 * Manage reinstall operations for a disabled host */
int nodeLinkClass::reboot_handler ( struct nodeLinkClass::node * node_ptr )
{
    // ilog ("%s Administrative 'reboot' Action (%d)\n", node_ptr->hostname.c_str(), node_ptr->resetProgStage );

    switch ( node_ptr->resetProgStage )
    {
        case MTC_RESETPROG__START:
        {
            plog ("%s Administrative Reboot Requested\n",  node_ptr->hostname.c_str() );

            /* start with a clean command slate */
            mtcCmd_doneQ_purge ( node_ptr );
            mtcCmd_workQ_purge ( node_ptr );
            mtcInvApi_update_task ( node_ptr, MTC_TASK_RESET_PROG );
            if ( node_ptr->adminAction != MTC_ADMIN_ACTION__REBOOT )
            {
                mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_AUTO_REBOOT );
            }
            node_ptr->retries = 0 ;

            /* If this is a simplex all-in-one system then issue the lazy reboot and just wait */
            if ( THIS_HOST )
            {
                mtcInvApi_update_task_now ( node_ptr, "Please stand-by while the active controller gracefully reboots" );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_2 ) ;
                node_ptr->resetProgStage = MTC_RESETPROG__WAIT ;

                /* Launch a backup sysreq thread */
                fork_sysreq_reboot ( daemon_get_cfg_ptr()->failsafe_shutdown_delay );

                /* Tell SM we are unhealthy so that it shuts down all its services */
                daemon_log ( SMGMT_UNHEALTHY_FILE, "Active Controller Reboot request" );
                send_mtc_cmd ( node_ptr->hostname, MTC_CMD_LAZY_REBOOT, MGMNT_INTERFACE ) ;
            }
            else
            {
                node_ptr->resetProgStage = MTC_RESETPROG__REBOOT ;
            }
            break ;
        }
        case MTC_RESETPROG__REBOOT:
        {
            #define REBOOT_RETRIES (0)
            node_ptr->cmd_retries = 0 ;
            node_ptr->mtcCmd_work_fifo.clear();
            mtcCmd_init ( node_ptr->cmd );
            node_ptr->cmd.stage = MTC_CMD_STAGE__START ;
            node_ptr->cmd.cmd   = MTC_OPER__RESET_PROGRESSION ;
            node_ptr->cmd.parm1 = REBOOT_RETRIES ; /* retries */
            node_ptr->cmd.task  = false ; /* send task updates */
            node_ptr->mtcCmd_work_fifo.push_front(node_ptr->cmd);

            mtcTimer_reset ( node_ptr->mtcTimer );

            /* calculate the overall timeout period taking into account
             * all the reboot/reset sources that will be tried */
            int overall_timeout = calc_reset_prog_timeout ( node_ptr, REBOOT_RETRIES ) ;
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, overall_timeout ) ;
            node_ptr->resetProgStage = MTC_RESETPROG__WAIT ;

            break ;
        }
        case MTC_RESETPROG__WAIT:
        {
            /* Look for the command handler FSM timeout and abor in that case */
            if ( node_ptr->mtcTimer.ring == true )
            {
                ilog ("%s reboot (progression) timeout\n", node_ptr->hostname.c_str());

                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY ) ;
                mtcInvApi_force_task ( node_ptr, MTC_TASK_REBOOT_ABORT );
                node_ptr->resetProgStage = MTC_RESETPROG__FAIL ;
            }
            else if ( THIS_HOST )
            {
                ; /* wait for the reboot or FSM timeout */
            }
            else if ( node_ptr->mtcCmd_work_fifo.empty())
            {
                slog ("%s unexpected empty cmd queue\n", node_ptr->hostname.c_str());
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY ) ;
                mtcInvApi_force_task ( node_ptr, MTC_TASK_REBOOT_ABORT );
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                node_ptr->resetProgStage = MTC_RESETPROG__FAIL ;
            }
            else
            {
                node_ptr->mtcCmd_work_fifo_ptr = node_ptr->mtcCmd_work_fifo.begin() ;
                if ( node_ptr->mtcCmd_work_fifo_ptr->stage == MTC_CMD_STAGE__DONE )
                {
                    if ( node_ptr->mtcTimer.tid )
                        mtcTimer_stop ( node_ptr->mtcTimer );

                    if ( node_ptr->mtcCmd_work_fifo_ptr->status == PASS )
                    {
                        plog ("%s Reboot Completed\n", node_ptr->hostname.c_str() );
                        node_ptr->mtcTimer.ring = true ;
                        node_ptr->resetProgStage = MTC_RESETPROG__FAIL ; /* not really fail but use its clean up function */
                    }
                    else if ( ++node_ptr->retries <= 5 )
                    {
                        char buffer[255] ;
                        snprintf ( buffer, 255, MTC_TASK_REBOOT_FAIL_RETRY, node_ptr->retries, 5 );
                        wlog ("%s %s\n", node_ptr->hostname.c_str(), buffer );
                        mtcInvApi_update_task ( node_ptr, buffer );
                        if ( node_ptr->mtcCmd_done_fifo.size() )
                            node_ptr->mtcCmd_done_fifo.pop_front();
                        node_ptr->resetProgStage = MTC_RESETPROG__REBOOT ;
                    }
                    else
                    {
                        wlog ("%s %s\n", node_ptr->hostname.c_str(), MTC_TASK_REBOOT_ABORT );
                        if ( node_ptr->mtcCmd_done_fifo.size() )
                            node_ptr->mtcCmd_done_fifo.pop_front();
                        mtcInvApi_force_task ( node_ptr, MTC_TASK_REBOOT_ABORT );
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY ) ;
                        node_ptr->resetProgStage = MTC_RESETPROG__FAIL ;
                    }
                }
                break ;
            }
            case MTC_RESETPROG__FAIL:
            {
                if ( node_ptr->mtcTimer.ring == true )
                {
                    if ( !node_ptr->mtcCmd_work_fifo.empty() )
                        node_ptr->mtcCmd_work_fifo.pop_front();
                    if ( !node_ptr->mtcCmd_work_fifo.empty() )
                        mtcCmd_workQ_purge ( node_ptr );

                    if ( !node_ptr->mtcCmd_done_fifo.empty() )
                        node_ptr->mtcCmd_done_fifo.pop_front();
                    if ( !node_ptr->mtcCmd_done_fifo.empty() )
                        mtcCmd_doneQ_purge ( node_ptr );

                    mtcInvApi_force_task ( node_ptr, "" );
                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                    node_ptr->resetProgStage = MTC_RESETPROG__START ;
                }
            }
            break ;
        }
        default:
        {
            slog ("%s unsupported reboot stage (%d) ; clearing action\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->resetProgStage );

            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );
        }
    }

    return (PASS);
}

/* Power Handler
 * ----------------- */
int nodeLinkClass::power_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;
    switch ( node_ptr->powerStage )
    {
        case MTC_POWEROFF__FAIL:
        {
            elog ("%s Power-Off failed ; aborting after max retries\n", node_ptr->hostname.c_str());
            mtcInvApi_update_task ( node_ptr, MTC_TASK_POWEROFF_FAIL);
            mtcTimer_reset ( node_ptr->mtcTimer ) ;
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
            powerStageChange ( node_ptr , MTC_POWEROFF__FAIL_WAIT );
            break ;
        }
        case MTC_POWEROFF__FAIL_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                node_ptr->mtcTimer.ring = false ;
                powerStageChange ( node_ptr , MTC_POWER__DONE );
            }
            break ;
        }
        case MTC_POWEROFF__START:
        {
            plog ("%s Administrative 'Power-Off' Action\n", node_ptr->hostname.c_str());
            mtcInvApi_force_task ( node_ptr, "Power-Off Requested" );

            start_offline_handler ( node_ptr );

            if ( hostUtil_is_valid_ip_addr (node_ptr->bm_ip ) == false )
            {
                /**
                 *  New working provisioning is learned by from the
                 *  dnsmasq.bmc_hosts file changes through inotify watch so
                 *  it is entirely possible that the retries in this fsm
                 *  eventually succeed.
                 **/
                wlog ("%s bm_ip (%s) is invalid (%d) \n",
                          node_ptr->hostname.c_str(),
                          node_ptr->bm_ip.c_str(),
                          rc );
            }

            node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;

            /* don't allow a timeout of zero to be passed in */
            if ( power_off_retry_wait == 0 )
                power_off_retry_wait = DEFAULT_POWER_OFF_RETRY_WAIT ;

            ilog ("%s power off retry wait is %d seconds",
                node_ptr->hostname.c_str(), power_off_retry_wait);

            mtcTimer_reset ( node_ptr->mtcTimer ) ;
            powerStageChange ( node_ptr , MTC_POWEROFF__REQ_SEND );
            break ;
        }
        case MTC_POWEROFF__REQ_SEND:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                /* Handle loss of connectivity over retries  */
                if ( node_ptr->bmc_provisioned == false )
                {
                    elog ("%s BMC not provisioned\n", node_ptr->hostname.c_str());
                    mtcInvApi_force_task ( node_ptr, MTC_TASK_BMC_NOT_PROV );
                    powerStageChange ( node_ptr , MTC_POWEROFF__FAIL );
                    break ;
                }

                if ( node_ptr->bmc_accessible == false )
                {
                    wlog ("%s Power Off request rejected ; BMC not accessible",
                              node_ptr->hostname.c_str());
                    powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                    break ;
                }

                else
                {
                    rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_OFF );
                    if ( rc )
                    {
                        wlog ("%s Power-Off request failed (%d)", node_ptr->hostname.c_str(), rc );
                        powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                    }
                    else
                    {
                        ilog ("%s Power-Off requested", node_ptr->hostname.c_str());
                        powerStageChange ( node_ptr , MTC_POWEROFF__RESP_WAIT );
                    }
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RECV_WAIT );
                }
            }
            break ;
        }

        case MTC_POWEROFF__RESP_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RECV_RETRY_WAIT );
                    break ;
                }
                else if ( rc )
                {
                    elog ("%s Power-Off command failed\n", node_ptr->hostname.c_str());
                    powerStageChange ( node_ptr , MTC_POWEROFF__POWERQRY );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_QUERY_WAIT );
                }
                else
                {
                    ilog ("%s is Powering Off ; waiting for offline\n", node_ptr->hostname.c_str() );
                    if ( node_ptr->adminAction != MTC_ADMIN_ACTION__REINSTALL )
                    {
                        mtcInvApi_update_task ( node_ptr, "Powering Off" );
                    }
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_BM_POWEROFF_TIMEOUT );
                    powerStageChange ( node_ptr , MTC_POWEROFF__OFFLINE_WAIT );
                }
            }
            break ;
        }
        case MTC_POWEROFF__OFFLINE_WAIT:
        {
             if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
             {
                 mtcTimer_reset ( node_ptr->mtcTimer );

                 plog ("%s is now offline\n", node_ptr->hostname.c_str());
                 powerStageChange ( node_ptr , MTC_POWEROFF__POWERQRY );
                 mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_QUERY_WAIT );
             }
             else if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
             {
                 elog ("%s Power-Off operation timeout - host did not go offline\n", node_ptr->hostname.c_str());
                 powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
             }
             break ;
        }
        case MTC_POWEROFF__POWERQRY:
        {
            /* give the power off action some time to complete */
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                if ( node_ptr->bmc_thread_ctrl.done )
                {
                    /* Query Host Power Status */
                    if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_STATUS ) != PASS )
                    {
                        elog ("%s '%s' send failed",
                                  node_ptr->hostname.c_str(),
                                  bmcUtil_getCmd_str(
                                  node_ptr->bmc_thread_info.command).c_str());
                        pingUtil_restart ( node_ptr->bm_ping_info );
                        powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                    }
                    else
                    {
                        powerStageChange ( node_ptr , MTC_POWEROFF__POWERQRY_WAIT );
                    }
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RECV_WAIT );
                }
                else
                {
                    thread_kill ( node_ptr->bmc_thread_ctrl , node_ptr->bmc_thread_info ) ;
                }
            }
            break ;
        }
        case MTC_POWEROFF__POWERQRY_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                int rc = bmc_command_recv ( node_ptr ) ;
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RECV_RETRY_WAIT );
                    break ;
                }
                else if ( rc != PASS )
                {
                    wlog ("%s '%s' failed receive (rc:%d)",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str(),
                              rc );
                    powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                }
                else if ( node_ptr->bmc_thread_info.data.empty() )
                {
                    wlog ("%s '%s' request yielded no response data",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str());
                    powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                }
                else
                {
                    int rc =
                    bmcUtil_is_power_on ( node_ptr->hostname,
                                          node_ptr->bmc_protocol,
                                          node_ptr->bmc_thread_info.data,
                                          node_ptr->power_on);
                    if ( rc == PASS )
                    {
                        if ( node_ptr->power_on == true )
                        {
                            ilog ("%s Power not Off ; retry power-off ",
                                      node_ptr->hostname.c_str());
                            powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                        }
                        else
                        {
                            ilog ("%s Power-Off Verified",
                                      node_ptr->hostname.c_str());
                            powerStageChange ( node_ptr , MTC_POWEROFF__DONE );
                            mtcTimer_reset ( node_ptr->mtcTimer );
                            break ;
                        }
                    }
                    else
                    {
                        elog ("%s Power query failed (rc:%d)",
                                  node_ptr->hostname.c_str(), rc );
                        powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                    }
                }
            }
            break ;
        }
        case MTC_POWEROFF__QUEUE:
        {
            if ( --node_ptr->power_action_retries >= 0 )
            {
                char buffer[255] ;
                int attempts = MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries ;
                snprintf ( buffer, 255, MTC_TASK_POWEROFF_QUEUE, attempts, MTC_POWER_ACTION_RETRY_COUNT);
                mtcInvApi_update_task ( node_ptr, buffer);

                /* Check the thread error status if there is one. Skip the
                 * typical system call log which just floods the log file.
                 * The failure is reported in the update task log above. */
                if (( node_ptr->bmc_thread_info.status ) &&
                    ( node_ptr->bmc_thread_info.status != FAIL_SYSTEM_CALL))
                {
                    wlog ("%s ... %s (rc:%d)", node_ptr->hostname.c_str(),
                                               node_ptr->bmc_thread_info.status_string.c_str(),
                                               node_ptr->bmc_thread_info.status );
                }
                powerStageChange ( node_ptr , MTC_POWEROFF__REQ_SEND );
                ilog ("%s waiting %d seconds before next power off retry",
                          node_ptr->hostname.c_str(), power_off_retry_wait);
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, power_off_retry_wait );
            }
            else
            {
                powerStageChange ( node_ptr , MTC_POWEROFF__FAIL );
            }
            break ;
        }
        case MTC_POWEROFF__DONE:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                plog ("%s Power-Off Completed\n", node_ptr->hostname.c_str());

                stop_offline_handler ( node_ptr );

                availStatusChange ( node_ptr, MTC_AVAIL_STATUS__POWERED_OFF );

                powerStageChange ( node_ptr , MTC_POWER__DONE );
                node_ptr->power_on = false ;
            }
            break ;
        }

        /* ----------------------- */
        /* POWER ON Group of Cases */
        /* ----------------------- */

        case MTC_POWERON__FAIL:
        {
            elog ("%s Power-On failed ; aborting after max retries\n", node_ptr->hostname.c_str());
            mtcInvApi_update_task ( node_ptr, MTC_TASK_POWERON_FAIL);
            mtcTimer_reset ( node_ptr->mtcTimer ) ;
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
            powerStageChange ( node_ptr , MTC_POWERON__FAIL_WAIT );
            break ;
        }
        case MTC_POWERON__FAIL_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                node_ptr->mtcTimer.ring = false ;
                powerStageChange ( node_ptr , MTC_POWER__DONE );
            }
            break ;
        }
        case MTC_POWERON__START:
        {
            plog ("%s Administrative 'Power-On' Action (%d:%d:%lu:%lu:%d:idle:%s)",
                      node_ptr->hostname.c_str(),
                      node_ptr->bmc_thread_ctrl.done,
                      node_ptr->bmc_thread_ctrl.retries,
                      node_ptr->bmc_thread_ctrl.id,
                      node_ptr->bmc_thread_info.id,
                      node_ptr->bmc_thread_info.command,
                      node_ptr->bmc_thread_ctrl.idle ? "Yes":"No");
            mtcInvApi_update_task ( node_ptr, "Power-On Requested" );

            if ( hostUtil_is_valid_ip_addr ( node_ptr->bm_ip ) == false )
            {
                /**
                 *  New working provisioning is learned by from the
                 *  dnsmasq.bmc_hosts file changes through inotify watch so
                 *  it is entirely possible that the retries in this fsm
                 *  eventually succeed.
                 **/
                wlog ("%s bm_ip (%s) is invalid (%d) \n",
                          node_ptr->hostname.c_str(),
                          node_ptr->bm_ip.c_str(),
                          rc );
            }

            node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;
            powerStageChange ( node_ptr , MTC_POWERON__POWER_STATUS );
            //the fall through to MTC_POWERON__REQ_SEND is intentional
            MTCE_FALLTHROUGH;
        }
        case MTC_POWERON__POWER_STATUS:
        {
            if ( node_ptr->bmc_accessible == false )
            {
                wlog ("%s Power On request rejected ; BMC not accessible ; retry in %d seconds\n",
                          node_ptr->hostname.c_str(),
                          MTC_POWER_ACTION_RETRY_DELAY);

                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
                break ;
            }

            rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_STATUS ) ;
            if ( rc )
            {
                powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
            }
            else
            {
                powerStageChange ( node_ptr , MTC_POWERON__POWER_STATUS_WAIT );
            }
            mtcTimer_reset ( node_ptr->mtcTimer );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
            break ;
        }
        case MTC_POWERON__POWER_STATUS_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
                else if ( rc == PASS )
                {
                    rc = bmcUtil_is_power_on ( node_ptr->hostname,
                                               node_ptr->bmc_protocol,
                                               node_ptr->bmc_thread_info.data,
                                               node_ptr->power_on);

                    /* If there was an error in querying the power state,
                     * assume the power is off so that it will be powered on. */
                    if ( rc )
                        node_ptr->power_on = false ;

                    if ( node_ptr->power_on )
                    {
                        ilog ("%s power is already on ; no action required\n", node_ptr->hostname.c_str());
                        mtcInvApi_update_task ( node_ptr, "Power Already On" );
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
                        powerStageChange ( node_ptr , MTC_POWERON__DONE );
                    }
                    else
                    {
                        ilog ("%s power is off ; powering on ...\n", node_ptr->hostname.c_str() );
                        powerStageChange ( node_ptr , MTC_POWERON__REQ_SEND );
                    }
                }
                else
                {
                    wlog ("%s power state query failed",
                              node_ptr->hostname.c_str());
                    powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
                }
            }
            break ;
        }
        case MTC_POWERON__REQ_SEND:
        {

            /* Ensure that mtce is updated with the latest board
             * management ip address for this host */
            if ( node_ptr->bmc_provisioned == false )
            {
                elog ("%s BMC not provisioned or accessible (%d:%d)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->bmc_provisioned,
                          node_ptr->bmc_accessible );

                powerStageChange ( node_ptr , MTC_POWERON__FAIL );
                break ;
            }

            if ( node_ptr->bmc_accessible == false )
            {
                wlog ("%s Power-On will fail ; not accessible to BMC ; retry in %d seconds \n",
                          node_ptr->hostname.c_str(), MTC_POWER_ACTION_RETRY_DELAY);

                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
                break ;
            }
            else
            {
                rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_ON );
                if ( rc )
                {
                    wlog ("%s Power-On request failed (%d)\n",
                              node_ptr->hostname.c_str(), rc );

                    mtcTimer_reset ( node_ptr->mtcTimer );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                    powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
                }
                else
                {
                    blog ("%s Power-On requested\n", node_ptr->hostname.c_str());

                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );

                    powerStageChange ( node_ptr , MTC_POWERON__RESP_WAIT );
                }
            }
            break ;
        }
        case MTC_POWERON__RESP_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                    rc = bmc_command_recv ( node_ptr );
                    if ( rc == RETRY )
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                        break ;
                    }

                if ( rc )
                {
                    elog ("%s Power-On command failed\n", node_ptr->hostname.c_str());
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                    powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
                }
                else
                {
                    ilog ("%s is Powering On\n", node_ptr->hostname.c_str() );
                    if ( node_ptr->adminAction != MTC_ADMIN_ACTION__REINSTALL )
                    {
                        mtcInvApi_update_task ( node_ptr, "Powering On" );
                    }
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
                    powerStageChange ( node_ptr , MTC_POWERON__DONE );
                }
            }
            break ;
        }
        case MTC_POWERON__QUEUE:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                node_ptr->mtcTimer.ring = false ;
                if ( --node_ptr->power_action_retries >= 0 )
                {
                    char buffer[64] ;
                    int attempts = MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries ;
                    snprintf ( buffer, 64, MTC_TASK_POWERON_QUEUE, attempts, MTC_POWER_ACTION_RETRY_COUNT);
                    mtcInvApi_update_task ( node_ptr, buffer);

                    /* check the thread error status if thetre is one */
                    if ( node_ptr->bmc_thread_info.status )
                    {
                        wlog ("%s ... %s (rc:%d)\n", node_ptr->hostname.c_str(),
                                                     node_ptr->bmc_thread_info.status_string.c_str(),
                                                     node_ptr->bmc_thread_info.status );
                    }

                    powerStageChange ( node_ptr , MTC_POWERON__POWER_STATUS );
                }
                else
                {
                    powerStageChange ( node_ptr , MTC_POWERON__FAIL );
                }
            }
            break ;
        }
        case MTC_POWERON__DONE:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                plog ("%s Power-On Completed\n", node_ptr->hostname.c_str());

                availStatusChange ( node_ptr, MTC_AVAIL_STATUS__OFFLINE );

                powerStageChange ( node_ptr , MTC_POWER__DONE );
                node_ptr->power_on = true ;
            }
            break ;
        }

        case MTC_POWER__DONE:
        default:
        {
            mtcTimer_reset ( node_ptr->mtcTimer );

            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );

            recovery_ctrl_init ( node_ptr->hwmon_reset );
            recovery_ctrl_init ( node_ptr->hwmon_powercycle );

            ar_enable ( node_ptr );

            if ( node_ptr->adminAction != MTC_ADMIN_ACTION__REINSTALL )
            {
                mtcInvApi_force_task ( node_ptr, "" );
            }
            break ;
        }
    }
    return (PASS);
}


/* Power Cycle Handler
 * ------------------- */
int nodeLinkClass::powercycle_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    if ( node_ptr->bmc_accessible == false )
    {
        wlog ("%s 'powercycle' abort ; not accessible to BMC\n", node_ptr->hostname.c_str() );
        powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
    }

    /* Manage max retries */
    if ( node_ptr->hwmon_powercycle.retries  >= MAX_POWERCYCLE_STAGE_RETRIES )
    {
        wlog ("%s 'powercycle' abort ; max retries reached\n", node_ptr->hostname.c_str() );
        powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
    }

    /* Manage max retries */
    if ( node_ptr->hwmon_powercycle.queries >= MAX_POWERCYCLE_QUERY_RETRIES )
    {
        wlog ("%s power state query retries exceeded ; failing current iteration\n", node_ptr->hostname.c_str());
        powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
    }

    switch ( node_ptr->powercycleStage )
    {
        case MTC_POWERCYCLE__FAIL:
        {
            mtcTimer_reset ( node_ptr->hwmon_powercycle.control_timer );

            wlog ("%s entering 'powercycle' failed stage ATTEMPT: %d\n",
                      node_ptr->hostname.c_str() ,
                      node_ptr->hwmon_powercycle.attempts );

            /* Let the next event perform anothe rpower-cycle retry */
            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );
            powercycleStageChange ( node_ptr, MTC_POWERCYCLE__DONE );

            mtcInvApi_update_task ( node_ptr, MTC_TASK_POWERCYCLE_FAIL , node_ptr->hwmon_powercycle.attempts );

            hwmon_recovery_monitor ( node_ptr, MTC_EVENT_HWMON_POWERCYCLE );

            break ;
        }
        case MTC_POWERCYCLE__START:
        {
            switch ( node_ptr->subStage )
            {
                case MTC_SUBSTAGE__START:
                {
                    if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
                    {
                        ilog ("%s failing host for powercycle\n", node_ptr->hostname.c_str() );
                        alarm_enabled_failure ( node_ptr , true );

                        /* Set node as unlocked-disabled-failed */
                        allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                                   MTC_OPER_STATE__DISABLED,
                                                   MTC_AVAIL_STATUS__FAILED );
                    }
                    ilog ("%s is %s-%s-%s\n", node_ptr->hostname.c_str(),
                                              get_adminState_str (node_ptr->adminState).c_str(),
                                              get_operState_str  (node_ptr->operState).c_str(),
                                              get_availStatus_str(node_ptr->availStatus).c_str());

                    node_ptr->hwmon_powercycle.state = RECOVERY_STATE__ACTION ;

                    node_ptr->hwmon_powercycle.attempts++ ;

                    mtcTimer_reset ( node_ptr->hwmon_powercycle.control_timer );

                    /***********************************************************************************
                     *
                     * Perminent Power-Down Case
                     * -------------------------
                     * If we exceed the maximum power cycle attempt retries then we
                     * give up and power the unit down and leave it that way.
                     *
                     ***********************************************************************************/
                    if ( node_ptr->hwmon_powercycle.attempts > MAX_POWERCYCLE_ATTEMPT_RETRIES )
                    {
                        ilog ("%s -------------------------------------------------------------\n", node_ptr->hostname.c_str());
                        wlog ("%s critical event is persistent ; too many failed attempts (%d)\n",
                                  node_ptr->hostname.c_str(), node_ptr->hwmon_powercycle.attempts );
                        ilog ("%s -------------------------------------------------------------\n", node_ptr->hostname.c_str());

                        /* terminate any in progress work, likely auto recovery if unlocked, for this host */
                        mtcTimer_reset ( node_ptr->mtcCmd_timer );
                        mtcCmd_workQ_purge ( node_ptr );
                        mtcCmd_doneQ_purge ( node_ptr );

                        // node_ptr->powercycle_completed  = true ;
                        node_ptr->hwmon_powercycle.retries = 0 ;
                        node_ptr->hwmon_powercycle.queries = 0 ;
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_POWERCYCLE_DOWN, node_ptr->hwmon_powercycle.attempts );
                        powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWEROFF );
                    }
                    else
                    {
                        wlog ("%s starting 'powercycle' recovery ATTEMPT: %d\n",
                                  node_ptr->hostname.c_str(),
                                  node_ptr->hwmon_powercycle.attempts );

                        mtcInvApi_update_task ( node_ptr, MTC_TASK_POWERCYCLE_HOST, node_ptr->hwmon_powercycle.attempts );

                        node_ptr->hwmon_powercycle.retries = 0 ; /* remove for back to back power cycles */
                        mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, 1 );
                        subStageChange ( node_ptr, MTC_SUBSTAGE__SEND );
                    }
                    break ;
                }

                /* Query current power state */
                case MTC_SUBSTAGE__SEND:
                {
                    if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
                    {
                        int delay = MTC_BMC_REQUEST_DELAY ;
                        ilog ("%s querying current power state\n", node_ptr->hostname.c_str());

                            rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_STATUS );
                        if ( rc )
                        {
                            node_ptr->hwmon_powercycle.retries++ ;
                            wlog ("%s failed to send 'power state query' ; retrying %d of %d\n",
                                      node_ptr->hostname.c_str(),
                                      node_ptr->hwmon_powercycle.retries,
                                      MAX_POWERCYCLE_STAGE_RETRIES );

                            node_ptr->hwmon_powercycle.queries++ ;

                            /* Retry the send */
                            mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                        }
                        else
                        {
                            node_ptr->hwmon_powercycle.queries = 0 ;
                            subStageChange ( node_ptr, MTC_SUBSTAGE__RECV );
                            mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, delay );
                        }
                    }
                    break ;
                }

                /* Interpret current power state query */
                case MTC_SUBSTAGE__RECV:
                {
                    if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
                    {
                            rc = bmc_command_recv ( node_ptr );
                            if ( rc == RETRY )
                            {
                                mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                                break ;
                            }

                        if ( rc )
                        {
                            node_ptr->hwmon_powercycle.retries++ ;
                            elog ("%s 'power query' command failed ; retrying %d or %d\n",
                                      node_ptr->hostname.c_str(),
                                      node_ptr->hwmon_powercycle.retries,
                                      MAX_POWERCYCLE_STAGE_RETRIES );

                            mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                            subStageChange ( node_ptr, MTC_SUBSTAGE__SEND );
                        }
                        else
                        {
                            int status =
                            bmcUtil_is_power_on ( node_ptr->hostname,
                                                  node_ptr->bmc_protocol,
                                                  node_ptr->bmc_thread_info.data,
                                                  node_ptr->power_on);
                            if ( status == PASS )
                            {
                                if ( node_ptr->power_on )
                                {
                                    ilog ("%s invoking 'powerdown' phase\n", node_ptr->hostname.c_str());

                                    subStageChange ( node_ptr, MTC_SUBSTAGE__DONE );
                                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWEROFF );
                                }
                                else
                                {
                                    wlog ("%s is already powered-off ; starting powercycle with power-on\n", node_ptr->hostname.c_str() );
                                    subStageChange ( node_ptr, MTC_SUBSTAGE__DONE );
                                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON );
                                }
                            }
                            else
                            {
                                node_ptr->hwmon_powercycle.retries = MAX_POWERCYCLE_STAGE_RETRIES+1 ;
                                elog ("%s failed to query power status ; aborting powercycle action\n",
                                          node_ptr->hostname.c_str());
                            }
                        }
                    }
                    break ;
                }
                default:
                {
                    slog ("%s %s.%s stage\n", node_ptr->hostname.c_str(),
                              get_powercycleStages_str(node_ptr->powercycleStage).c_str(),
                              get_subStages_str(node_ptr->subStage).c_str());

                    subStageChange ( node_ptr, MTC_SUBSTAGE__DONE );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__START );
                    break ;
                }
            }
            break ;
        }

        case MTC_POWERCYCLE__POWEROFF:
        {
            int delay = MTC_BMC_REQUEST_DELAY ;

            /* Stop heartbeat if we are powering off the host */
            send_hbs_command  ( node_ptr->hostname, MTC_CMD_STOP_HOST );

            rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_OFF );
            if ( rc )
            {
                elog ("%s failed to send power-off command to BMC (%d)\n",
                          node_ptr->hostname.c_str(),
                          rc );

                powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
            }
            else
            {
                mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, delay );
                powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWEROFF_CMND_WAIT );
            }
            break ;
        }
        case MTC_POWERCYCLE__POWEROFF_CMND_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }

                if ( rc )
                {
                    elog ("%s power-off command failed (rc:%d:%d)\n",
                              node_ptr->hostname.c_str(),
                              rc , node_ptr->bmc_thread_info.status);

                    if ( node_ptr->bmc_thread_info.status )
                    {
                        wlog ("%s ... %s\n",
                                  node_ptr->hostname.c_str(),
                                  node_ptr->bmc_thread_info.status_string.c_str());
                    }
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
                }
                else
                {
                    ilog ("%s waiting up to %d seconds for 'offline'\n", node_ptr->hostname.c_str(), MTC_POWEROFF_TO_OFFLINE_TIMEOUT );

                    /* Set the power-off timeout */
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer,
                                     mtcTimer_handler,
                                     MTC_POWEROFF_TO_OFFLINE_TIMEOUT );

                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWEROFF_WAIT );
                }
            }
            break ;
        }
        case MTC_POWERCYCLE__POWEROFF_WAIT:
        {
            if (( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) ||
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE ))
            {
                /* since the host is powered down lets reflect that in the database */
                node_ptr->uptime = 0 ;
                mtcInvApi_update_uptime ( node_ptr, node_ptr->uptime );

                clear_service_readies ( node_ptr );

                mtcTimer_reset ( node_ptr->hwmon_powercycle.control_timer );

                if ( node_ptr->hwmon_powercycle.attempts > MAX_POWERCYCLE_ATTEMPT_RETRIES )
                {
                    wlog ("%s -------------------------------------------------------------------\n",
                                  node_ptr->hostname.c_str() );
                    wlog ("%s ... Leaving server POWERED DOWN to protect hardware from damage ...\n",
                                  node_ptr->hostname.c_str() );
                    wlog ("%s -------------------------------------------------------------------\n",
                                  node_ptr->hostname.c_str() );

                    /* Cancelling the recovery timer prevents auto-recovery.
                     * Recovery must be through manual actions. */
                    mtcTimer_reset ( node_ptr->hwmon_powercycle.recovery_timer );
                    node_ptr->hwmon_powercycle.state = RECOVERY_STATE__BLOCKED ;

                    /* Block Auto-Recovery Path
                     * ------------------------
                     * If we have reached the max retries and are unlocked then
                     * leave the powercycle action active so that the enable
                     * and graceful recovery handlers don't recover this host.
                     * -------------------------
                     * Manual action is required to recover a host that has
                     * exceeded the maximum powercycle retries */
                    if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
                    {
                        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                    }

                    /* While the node_ptr->hwmon_powercycle.control_timer is
                     * inactive the MTC_POWERCYCLE__DONE stagwe is a NOOP
                     * thereby keeping us doing nothing till the next manual
                     * action */
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__DONE );
                }
                else if ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF )
                {
                    ilog ("%s already powered-off, skipping cool-off\n", node_ptr->hostname.c_str());
                    mtcTimer_reset ( node_ptr->hwmon_powercycle.control_timer );
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, 10 );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON );
                }
                else
                {
                    ilog ("%s waiting %d seconds before power-on ; cool down time\n",
                              node_ptr->hostname.c_str(),
                              MTC_POWERCYCLE_COOLDOWN_DELAY );

                    node_ptr->hwmon_powercycle.holdoff = MTC_POWERCYCLE_COOLDOWN_DELAY/60 ;

                    /* Set the power-off timeout */
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, COMMAND_DELAY );

                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__COOLOFF );
                    node_ptr->hwmon_powercycle.state = RECOVERY_STATE__COOLOFF ;
                }
                availStatusChange     ( node_ptr, MTC_AVAIL_STATUS__POWERED_OFF );
            }

            /* handle timeout case */
            else if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                /* TODO: manage the retry count */
                elog ("%s timeout waiting for 'offline' state ; retrying ...\n", node_ptr->hostname.c_str() );

                powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWEROFF );
            }
            break ;
        }

        case MTC_POWERCYCLE__COOLOFF:
        {
            if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                mtcInvApi_update_task ( node_ptr,
                                        MTC_TASK_POWERCYCLE_COOL,
                                        node_ptr->hwmon_powercycle.attempts,
                                        node_ptr->hwmon_powercycle.holdoff);
                ilog ("%s Power-Cycle cool-off (%d minutes remaining)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->hwmon_powercycle.holdoff );

                if ( node_ptr->hwmon_powercycle.holdoff > 1 )
                {
                    node_ptr->hwmon_powercycle.holdoff-- ;
                }
                else
                {
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON );
                    node_ptr->hwmon_powercycle.state = RECOVERY_STATE__ACTION ;
                }
                mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_MINS_1 );
            }
            break ;
        }

        case MTC_POWERCYCLE__POWERON:
        {
            if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                int delay = MTC_BMC_REQUEST_DELAY ;
                clog ("%s %s stage\n", node_ptr->hostname.c_str(),
                      get_powercycleStages_str(node_ptr->powercycleStage).c_str());

                if ( node_ptr->bmc_accessible == false )
                {
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer,
                                     mtcTimer_handler,
                                     MTC_POWERCYCLE_COOLDOWN_DELAY );

                    wlog ("%s not accessible ; waiting another %d seconds before power-on\n",
                              node_ptr->hostname.c_str(),
                              MTC_POWERCYCLE_COOLDOWN_DELAY );
                }
                rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_ON );
                if ( rc )
                {
                    elog ("%s failed to send power-on command to BMC (%d)\n",
                              node_ptr->hostname.c_str(),
                              rc );

                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
                }
                else
                {
                    ilog ("%s Power-On requested\n", node_ptr->hostname.c_str() );
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_POWERCYCLE_ON, node_ptr->hwmon_powercycle.attempts );
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, delay );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON_CMND_WAIT );
                }
            }
            break ;
        }
        case MTC_POWERCYCLE__POWERON_CMND_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }

                if ( rc )
                {
                    wlog ("%s Power-On request failed (rc:%d)\n", node_ptr->hostname.c_str(), rc );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
                }
                else
                {
                    ilog ("%s Power-On response: %s\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->bmc_thread_info.data.c_str() );

                    /* Give the power on request time to execute */
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_CMD_RSP_TIMEOUT );

                    availStatusChange     ( node_ptr, MTC_AVAIL_STATUS__OFFLINE );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON_VERIFY );
                }
            }
            break ;
        }
        case MTC_POWERCYCLE__POWERON_VERIFY:
        {
            if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                    rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_STATUS );
                if ( rc )
                {
                    wlog ("%s Power-On command failed (rc:%d)\n", node_ptr->hostname.c_str(), rc );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
                }
                else
                {
                    wlog ("%s power status query requested\n", node_ptr->hostname.c_str() );
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY  );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON_VERIFY_WAIT );
                }
            }
            break ;
        }
        case MTC_POWERCYCLE__POWERON_VERIFY_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }
                if ( rc == PASS )
                {
                    rc = bmcUtil_is_power_on ( node_ptr->hostname,
                                               node_ptr->bmc_protocol,
                                               node_ptr->bmc_thread_info.data,
                                               node_ptr->power_on);
                }

                if (( rc == PASS ) && ( node_ptr->power_on ))
                {
                    ilog ("%s is Powered On - waiting for 'online' (%d sec timeout)\n",
                              node_ptr->hostname.c_str(),
                              MTC_POWERON_TO_ONLINE_TIMEOUT);

                    mtcInvApi_update_task ( node_ptr, MTC_TASK_POWERCYCLE_BOOT, node_ptr->hwmon_powercycle.attempts );
                    /* Set the online timeout */
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_POWERON_TO_ONLINE_TIMEOUT );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON_WAIT );
                }
                else
                {
                    wlog ("%s Power-On failed or did not occur ; retrying (rc:%d)\n", node_ptr->hostname.c_str(), rc );
                    node_ptr->power_on = false ;
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_POWERCYCLE_RETRY, node_ptr->hwmon_powercycle.attempts );
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_BM_POWERON_TIMEOUT );
                    node_ptr->hwmon_powercycle.queries++ ;
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON );
                    break ;
                }
            }
            break ;
        }
        case MTC_POWERCYCLE__POWERON_WAIT:
        {
            if ( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE )
            {
                ilog ("%s online (after powercycle)\n", node_ptr->hostname.c_str());

                node_ptr->hwmon_powercycle.holdoff = MTC_POWERCYCLE_BACK2BACK_DELAY/60 ;

                mtcTimer_reset ( node_ptr->hwmon_powercycle.control_timer );
                mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, 1 );
                node_ptr->hwmon_powercycle.state = RECOVERY_STATE__HOLDOFF ;
                powercycleStageChange ( node_ptr, MTC_POWERCYCLE__HOLDOFF );
            }
            else if ( node_ptr->hwmon_powercycle.control_timer.ring == true )
            {
                elog ("%s timeout waiting for 'online' state\n", node_ptr->hostname.c_str() );
                powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
            }
            break ;
        }
        case MTC_POWERCYCLE__HOLDOFF:
        {
            if ( node_ptr->hwmon_powercycle.control_timer.ring == true )
            {
                mtcInvApi_update_task ( node_ptr,
                                        MTC_TASK_POWERCYCLE_HOLD,
                                        node_ptr->hwmon_powercycle.attempts,
                                        node_ptr->hwmon_powercycle.holdoff);
                ilog ("%s Power-Cycle hold-off (%d minutes remaining) (uptime:%d)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->hwmon_powercycle.holdoff,
                          node_ptr->uptime );

                if ( node_ptr->hwmon_powercycle.holdoff > 1 )
                {
                    node_ptr->hwmon_powercycle.holdoff--;
                }
                else
                {
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__DONE );
                }
                mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_MINS_1 );
            }
            break ;
        }
        case MTC_POWERCYCLE__DONE:
        {
            if ( node_ptr->hwmon_powercycle.control_timer.ring == true )
            {
                mtcInvApi_update_task ( node_ptr, "" );

                adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );
                node_ptr->addStage = MTC_ADD__START ;

                hwmon_recovery_monitor ( node_ptr, MTC_EVENT_HWMON_POWERCYCLE );

                enableStageChange   ( node_ptr, MTC_ENABLE__START  );
                recoveryStageChange ( node_ptr, MTC_RECOVERY__START); /* reset the fsm */
                disableStageChange  ( node_ptr, MTC_DISABLE__START); /* reset the fsm */

                plog ("%s Power-Cycle Completed (uptime:%d)\n", node_ptr->hostname.c_str(), node_ptr->uptime );
            }
            break ;
        }

        default:
        {
            powercycleStageChange ( node_ptr, MTC_POWERCYCLE__DONE );
            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );
            break ;
        }
    }
    return (rc);
}

/* Delete Handler
 * ----------------- */
int nodeLinkClass::delete_handler ( struct nodeLinkClass::node * node_ptr )
{
    switch ( node_ptr->delStage )
    {
        case MTC_DEL__START:
        {
            ilog ("%s Delete Operation Started (%s)\n", node_ptr->hostname.c_str(), node_ptr->uuid.c_str());
            node_ptr->retries = 0 ;
            send_mtc_cmd ( node_ptr->hostname, MTC_CMD_WIPEDISK, MGMNT_INTERFACE ) ;

            if ( node_ptr->bmc_provisioned == true )
            {
                set_bm_prov ( node_ptr, false);
            }

            if ( node_ptr->bmc_thread_ctrl.stage != THREAD_STAGE__IDLE )
            {
                int delay = THREAD_POST_KILL_WAIT ;
                thread_kill ( node_ptr->bmc_thread_ctrl , node_ptr->bmc_thread_info ) ;

                ilog ("%s thread active ; sending kill ; waiting %d seconds\n",
                          node_ptr->hostname.c_str(), delay );
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, delay );
                node_ptr->delStage = MTC_DEL__WAIT ;
            }
            else
            {
                node_ptr->delStage = MTC_DEL__DONE ;
            }


            /* Send delete commands to monitor services */
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_DEL_HOST );
            send_hwmon_command ( node_ptr->hostname, MTC_CMD_DEL_HOST );
            send_guest_command ( node_ptr->hostname, MTC_CMD_DEL_HOST );

            /* Clear all the alarms for this host and generate a costomer delete log */
            alarmUtil_clear_all ( node_ptr->hostname );
            mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_DELETE );

            break ;
        }
        case MTC_DEL__WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                if ( node_ptr->bmc_thread_ctrl.stage != THREAD_STAGE__IDLE )
                {
                    if ( node_ptr->retries++ < 3 )
                    {
                        wlog ("%s still waiting on active thread ; sending another kill signal (try %d or %d)\n",
                                  node_ptr->hostname.c_str(), node_ptr->retries, 3 );

                        thread_kill ( node_ptr->bmc_thread_ctrl, node_ptr->bmc_thread_info ) ;
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, THREAD_POST_KILL_WAIT );
                        break ;
                    }
                    else
                    {
                        elog ("%s thread refuses to stop ; giving up ...\n",
                                  node_ptr->hostname.c_str());
                    }
                }
                node_ptr->delStage = MTC_DEL__DONE ;
            }
            break ;
        }
        default:
        case MTC_DEL__DONE:
        {
            dlog ("%s delete almost done !!\n", node_ptr->hostname.c_str());
            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );
            del_host ( node_ptr->hostname );
            this->host_deleted = true ;
            break ;
        }
    }
    return (PASS);
}


/* Add Handler
 * ----------------- */
int nodeLinkClass::add_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    switch ( node_ptr->addStage )
    {
        case MTC_ADD__START_DELAY:
        {
            if ( mtcTimer_expired (node_ptr->mtcTimer) )
                node_ptr->addStage = MTC_ADD__START ;
            break ;
        }
        case MTC_ADD__START:
        {
            bool timer_set = false ;
            if ( THIS_HOST )
            {
                struct timespec ts ;
                clock_gettime (CLOCK_MONOTONIC, &ts );
                node_ptr->uptime = ts.tv_sec ;
            }
            else if ( ! node_ptr->mtcClient_ready )
            {
                /* If we have not received a mtcAlive event from the
                 * mtcClient already then lets request it since that
                 * is how we get its uptime.
                 * Don't trust what is in the database since it will
                 * be stale. Best to default to zero so the logs will
                 * show that there has been no mtcAlive received */
                node_ptr->uptime = 0 ;
                send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, MGMNT_INTERFACE );
                send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, PXEBOOT_INTERFACE );
            }

            plog ("%s Host Add (uptime:%d)", node_ptr->hostname.c_str(), node_ptr->uptime );

            ilog ("%s %s %s-%s-%s (%s)\n",
                node_ptr->hostname.c_str(),
                node_ptr->ip.c_str(),
                adminState_enum_to_str (node_ptr->adminState).c_str(),
                operState_enum_to_str  (node_ptr->operState).c_str(),
                availStatus_enum_to_str(node_ptr->availStatus).c_str(),

                node_ptr->uuid.length() ? node_ptr->uuid.c_str() : "" );

            mtcInfo_log(node_ptr);

            if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                if ( daemon_is_file_present ( CONFIG_COMPLETE_WORKER ) == false )
                {
                    if ( node_ptr->operState_subf != MTC_OPER_STATE__DISABLED )
                    {
                        subfStateChange  ( node_ptr, MTC_OPER_STATE__DISABLED, MTC_AVAIL_STATUS__OFFLINE );
                    }
                }
                ilog ("%s-%s %s-%s-%s\n",
                    node_ptr->hostname.c_str(),
                    node_ptr->subfunction_str.c_str(),
                    adminState_enum_to_str (node_ptr->adminState).c_str(),
                    operState_enum_to_str  (node_ptr->operState_subf).c_str(),
                    availStatus_enum_to_str(node_ptr->availStatus_subf).c_str());
            }

            if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                ( node_ptr->operState == MTC_OPER_STATE__ENABLED ) &&
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED ))
            {
                wlog ("%s Add with availability status 'unlocked-enabled-%s' ; overriding to 'available'\n",
                          node_ptr->hostname.c_str(),
                          availStatus_enum_to_str(node_ptr->availStatus).c_str());
                mtcInvApi_update_state ( node_ptr, "availability", "available" );
            }

            /* Query FM for existing Enable and Config alarm status */
            EFmAlarmSeverityT enable_alarm_severity =
                mtcAlarm_state ( node_ptr->hostname, MTC_ALARM_ID__ENABLE);
            EFmAlarmSeverityT config_alarm_severity =
                mtcAlarm_state ( node_ptr->hostname, MTC_ALARM_ID__CONFIG);
            EFmAlarmSeverityT mtcAlive_alarm_severity =
                mtcAlarm_state ( node_ptr->hostname, MTC_ALARM_ID__MTCALIVE);

            /* Manage an existing enable alarm */
            if ( enable_alarm_severity != FM_ALARM_SEVERITY_CLEAR )
            {
                /* Added the unlocked-disabled check to avoid clearing the
                 * enabled alarm when the node is found to be unlocked-disabled
                 * with the enable alarm already asserted.
                 * We don't want to clear it in that case. */
                if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                    ( node_ptr->operState == MTC_OPER_STATE__DISABLED ))
                {
                    node_ptr->degrade_mask |= DEGRADE_MASK_ENABLE ;
                    node_ptr->alarms[MTC_ALARM_ID__ENABLE] = enable_alarm_severity ;
                    wlog ("%s found enable alarm while unlocked-disabled ; loaded %s",
                          node_ptr->hostname.c_str(),
                           alarmUtil_getSev_str(enable_alarm_severity).c_str());
                }
                else
                {
                    ilog ("%s found enable alarm while %s-%s ; clearing %s",
                              node_ptr->hostname.c_str(),
                              adminState_enum_to_str (node_ptr->adminState).c_str(),
                              operState_enum_to_str  (node_ptr->operState_subf).c_str(),
                              alarmUtil_getSev_str(enable_alarm_severity).c_str());
                    mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
                }
            }

            /* The config alarm is maintained if it exists.
             * The in-service test handler will clear the alarm
             * if the config failure is gone */
            if ( config_alarm_severity != FM_ALARM_SEVERITY_CLEAR )
            {
                node_ptr->degrade_mask |= DEGRADE_MASK_CONFIG ;
                node_ptr->alarms[MTC_ALARM_ID__CONFIG] = config_alarm_severity ;
                ilog ("%s found config alarm ; loaded %s",
                          node_ptr->hostname.c_str(),
                          alarmUtil_getSev_str(config_alarm_severity).c_str());
            }

            /* The mtcAlive alarm is maintained if it exists.
             * The pxeboot_mtcAlive_monitor will clear the alarm
             * if it exists and the pxeboot mtcAlive messaging works. */
            if ( mtcAlive_alarm_severity != FM_ALARM_SEVERITY_CLEAR )
            {
                node_ptr->alarms[MTC_ALARM_ID__MTCALIVE] = mtcAlive_alarm_severity ;
                ilog ("%s found mtcAlive alarm ; loaded %s",
                          node_ptr->hostname.c_str(),
                          alarmUtil_getSev_str(mtcAlive_alarm_severity).c_str());

                // Load up the miss and loss counts used for recovery
                node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE] = PXEBOOT_MTCALIVE_LOSS_ALARM_THRESHOLD ;
                node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] = PXEBOOT_MTCALIVE_LOSS_THRESHOLD ;
            }

            if ( is_controller(node_ptr) )
            {
                this->controllers++ ;

                mtc_cmd_enum state = CONTROLLER_DISABLED ;

                if (( node_ptr->adminState   == MTC_ADMIN_STATE__UNLOCKED ) &&
                    ( node_ptr->operState    == MTC_OPER_STATE__ENABLED ) &&
                    (( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
                     ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )))
                {
                    state = CONTROLLER_UNLOCKED ;
                }
                else if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
                {
                    state = CONTROLLER_LOCKED ;
                }

                if ( THIS_HOST )
                {
                    nodeLinkClass::set_active_controller_hostname(node_ptr->hostname);
                    if ( !node_ptr->task.compare(MTC_TASK_SWACT_INPROGRESS) )
                    {
                        ilog ("%s %s\n",node_ptr->hostname.c_str(), MTC_TASK_SWACT_NO_COMPLETE);
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_NO_COMPLETE);
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, 20 );
                        timer_set = true ;
                    }
                }
                else
                {
                    nodeLinkClass::set_inactive_controller_hostname(node_ptr->hostname);

                    if ( !node_ptr->task.compare(MTC_TASK_SWACT_INPROGRESS) )
                    {
                        ilog ("%s %s\n",node_ptr->hostname.c_str(), MTC_TASK_SWACT_COMPLETE );

                        mtcInvApi_update_uptime ( node_ptr, node_ptr->uptime );

                        mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_COMPLETE );
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, 10 );
                        timer_set = true ;
                    }
                }

                /*************************************************************
                 * Don't send a disable to SM if we are in simplex and locked.
                 * This will cause SM to shut down all services.
                 *
                 * Including a hostname check just in case simplex mode
                 * is ever or still true with a second controller provisioned
                 * but not unlocked. Defensive code.
                 *
                 * TODO: This should exist in AIO. Without it services will
                 *       not be running if you lock controller and then
                 *       reboot while this controller is disabled.
                 */
                if (( THIS_HOST ) &&
                    ( is_inactive_controller_main_insv() == false ) &&
                    ( node_ptr->operState == MTC_OPER_STATE__DISABLED ))
                {
                    ilog ("%s recovering from %s-disabled\n",
                              node_ptr->hostname.c_str(),
                              get_adminState_str (node_ptr->adminState).c_str());
                }
                else
                {
                    mtcSmgrApi_request ( node_ptr, state , SWACT_FAIL_THRESHOLD );
                }
            }
            if ( daemon_get_cfg_ptr()->debug_level & 1 )
                nodeLinkClass::host_print (node_ptr);

            if ( timer_set == false )
            {
                node_ptr->mtcTimer.ring = true ;
            }
            node_ptr->addStage = MTC_ADD__CLEAR_TASK ;
            break ;
        }

        case MTC_ADD__CLEAR_TASK:
        {
            /* Check for hosts that were in the auto recovery disabled state */
            if ( !node_ptr->task.empty () )
            {
                if (( node_ptr->adminState   == MTC_ADMIN_STATE__UNLOCKED ) &&
                   (( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_CONFIG))  ||
                    ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_GOENABLE))||
                    ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_SERVICES))||
                    ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_HEARTBEAT))||
                    (!node_ptr->task.compare(MTC_TASK_AR_DISABLED_LUKS))))
                {
                    if ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_CONFIG ))
                    {
                        node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__CONFIG ;
                        alarm_config_failure ( node_ptr );
                    }
                    else if ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_GOENABLE ))
                    {
                        node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__GOENABLE ;
                        alarm_enabled_failure ( node_ptr, true );
                    }
                    else if ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_SERVICES ))
                    {
                        node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__HOST_SERVICES ;
                        alarm_enabled_failure ( node_ptr, true );
                    }
                    else if ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_HEARTBEAT ))
                    {
                        node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__HEARTBEAT ;
                    }
                    else if ( !node_ptr->task.compare(MTC_TASK_AR_DISABLED_LUKS ))
                    {
                        node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__LUKS ;
                        alarm_luks_failure ( node_ptr );
                    }
                    node_ptr->ar_disabled = true ;
                    this->report_dor_recovery ( node_ptr, "is DISABLED" , "auto recovery disabled");

                    if ( THIS_HOST )
                        mtcInvApi_update_states ( node_ptr, "unlocked", "enabled", "degraded" );
                    else
                        mtcInvApi_update_states ( node_ptr, "unlocked", "disabled", "failed" );

                    node_ptr->addStage = MTC_ADD__START;
                    node_ptr->add_completed = true ;

                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                    plog ("%s Host Add Completed ; auto recovery disabled state (uptime:%d)\n",
                              node_ptr->hostname.c_str(), node_ptr->uptime );
                    break ;
                }
                /* Handle catching and recovering/restoring hosts that might
                 * have been in the Graceful Recovery Wait state.
                 *
                 * Prevents an extra reboot for hosts that might be in
                 * Graceful Recovery over a maintenance process restart. */
                else if (( NOT_THIS_HOST ) &&
                         ( !node_ptr->task.compare(MTC_TASK_RECOVERY_WAIT)))
                {
                    ilog ("%s is in %s ; restoring state",
                              node_ptr->hostname.c_str(),
                              MTC_TASK_RECOVERY_WAIT);

                    /* Complete necessary add operations before switching
                     * to Recovery */
                    LOAD_NODETYPE_TIMERS ;
                    workQueue_purge ( node_ptr );
                    if (( hostUtil_is_valid_bm_type  ( node_ptr->bm_type )) &&
                        ( hostUtil_is_valid_ip_addr  ( node_ptr->bm_ip )) &&
                        ( hostUtil_is_valid_username ( node_ptr->bm_un )))
                    {
                        set_bm_prov ( node_ptr, true ) ;
                    }
                    mtcTimer_reset ( node_ptr->mtcTimer );
                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                    node_ptr->addStage = MTC_ADD__START;

                    /* Switch into recovery_handler's Graceful Recovery Wait
                     * state with the Graceful Recovery Wait timeout */
                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__RECOVER );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler,
                                     node_ptr->mtcalive_timeout );
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__MTCALIVE_WAIT );
                    break ;
                }
                else
                {
                    if ( is_controller(node_ptr) )
                    {
                        if ( node_ptr->mtcTimer.ring == true )
                        {
                            mtcInvApi_force_task ( node_ptr, "" );
                        }
                        else
                        {
                            break ;
                        }
                    }
                    else
                    {
                        /* do it immediately for all otyher server types */
                        mtcInvApi_force_task ( node_ptr, "" );
                    }
                }
            }
            /* default retries counter to zero before MTC_SERVICES */
            node_ptr->retries = 0 ;
            node_ptr->addStage = MTC_ADD__MTC_SERVICES ;
            break ;
        }
        case MTC_ADD__MTC_SERVICES:
        {
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_ADD_HOST );

            if ( ( AIO_SYSTEM ) || ( is_worker (node_ptr) == true ))
            {
                send_guest_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );
            }

            /* Start a timer that failed enable if the work queue
             * does not empty or if commands in the done queue have failed */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, work_queue_timeout );

            node_ptr->addStage = MTC_ADD__WORKQUEUE_WAIT ;
            break ;
        }
        case MTC_ADD__WORKQUEUE_WAIT:
        {

            rc = workQueue_done ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait longer */
                break ;
            }
            else if ( rc == FAIL_WORKQ_TIMEOUT )
            {
                wlog ("%s Add failed ; workQueue empty timeout, purging ...\n", node_ptr->hostname.c_str());
                workQueue_purge ( node_ptr );
            }
            else if ( rc != PASS )
            {
                wlog ("%s Add failed ; doneQueue contains failed commands\n", node_ptr->hostname.c_str());
            }

            /* Stop the work queue wait timer */
            mtcTimer_reset ( node_ptr->mtcTimer );


            /* Only run hardware monitor if the bm ip is provisioned */
            if (( hostUtil_is_valid_bm_type  ( node_ptr->bm_type )) &&
                ( hostUtil_is_valid_ip_addr  ( node_ptr->bm_ip )) &&
                ( hostUtil_is_valid_username ( node_ptr->bm_un )))
            {
                set_bm_prov ( node_ptr, true ) ;
            }

            this->ctl_mtcAlive_gate(node_ptr, false) ;
            if (( NOT_THIS_HOST ) &&
                ((( AIO_SYSTEM ) && ( is_controller(node_ptr) == false )) || ( LARGE_SYSTEM )) &&
                ( this->hbs_failure_action != HBS_FAILURE_ACTION__NONE ) &&
                ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                ( node_ptr->operState  == MTC_OPER_STATE__ENABLED ))
            {
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_5 );
                if ( ! node_ptr->hbsClient_ready )
                {
                    ilog ("%s waiting for hbsClient ready event (%d secs)", node_ptr->hostname.c_str(), MTC_MINS_5);
                }
                node_ptr->addStage = MTC_ADD__HEARTBEAT_WAIT ;
            }
            else
            {
                node_ptr->addStage = MTC_ADD__DONE ;
            }
            break;
        }
        case MTC_ADD__HEARTBEAT_WAIT:
        {
            /* Wait for hbsClient ready event */
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                wlog ("%s hbsClient ready event timeout\n", node_ptr->hostname.c_str());
            }
            else if ( node_ptr->hbsClient_ready == false )
            {
                break ;
            }
            else
            {
                mtcTimer_reset ( node_ptr->mtcTimer );
            }
            plog ("%s Starting %d sec Heartbeat Soak (with%s)\n",
                        node_ptr->hostname.c_str(),
                        MTC_HEARTBEAT_SOAK_DURING_ADD,
                        node_ptr->hbsClient_ready ? " ready event" : "out ready event" );

            /* allow heartbeat to run for MTC_HEARTBEAT_SOAK_DURING_ADD
             * seconds before we declare enable */
            send_hbs_command ( node_ptr->hostname, MTC_CMD_START_HOST );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_HEARTBEAT_SOAK_DURING_ADD );
            node_ptr->addStage = MTC_ADD__HEARTBEAT_SOAK ;
            break ;
        }
        case MTC_ADD__HEARTBEAT_SOAK:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                plog ("%s heartbeating", node_ptr->hostname.c_str());
                /* if heartbeat is not working then we will
                 * never get here */
                node_ptr->addStage = MTC_ADD__DONE ;
            }
            break ;
        }
        case MTC_ADD__DONE:
        default:
        {
            adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );

            /* Send sysinv the sysadmin password hash
             * and aging data as an install command */
            if ( SIMPLEX && THIS_HOST &&
                ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED ))
            {
                node_ptr->configStage  = MTC_CONFIG__START ;
                node_ptr->configAction = MTC_CONFIG_ACTION__INSTALL_PASSWD ;
            }

            if ( node_ptr->bmc_provisioned == true )
            {
                mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__BM );
                node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_CLEAR ;
            }

            /* Special Add handling for the AIO system */
            if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                    ( node_ptr->operState  == MTC_OPER_STATE__ENABLED ))
                {
                    /* Need to run the subfunction enable handler
                     * for AIO controllers while in DOR mode */
                    if ( this->dor_mode_active )
                    {
                        ilog ("%s running subfunction enable for unlocked-enabled AIO controller (DOR mode)", node_ptr->hostname.c_str());
                        adminActionChange ( node_ptr , MTC_ADMIN_ACTION__ENABLE_SUBF );
                    }
                }
            }

            else if ( this->dor_mode_active )
            {
                /* The Enable SUBF handler will do this so lets not do it twice */
                if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
                {
                    string state_str = "" ;
                    if ( node_ptr->operState  == MTC_OPER_STATE__ENABLED )
                    {
                        state_str = "is ENABLED" ;
                        if ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )
                            state_str = "is DEGRADED" ;
                    }
                    else if ( node_ptr->availStatus == MTC_AVAIL_STATUS__FAILED )
                    {
                        state_str = "is FAILED" ;
                    }
                    else if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
                    {
                        state_str = "is OFFLINE" ;
                    }
                    if ( ! state_str.empty() )
                    {
                        report_dor_recovery ( node_ptr , state_str, "" ) ;
                    }
                    else
                    {
                        ilog ("%-12s is waiting ; DOR Recovery ; %s-%s-%s ; mtcClient:%c hbsClient:%c uptime:%3d task:%s",
                                 node_ptr->hostname.c_str(),
                                 adminState_enum_to_str (node_ptr->adminState).c_str(),
                                 operState_enum_to_str  (node_ptr->operState).c_str(),
                                 availStatus_enum_to_str(node_ptr->availStatus).c_str(),
                                 node_ptr->mtcClient_ready ? 'Y':'N',
                                 node_ptr->hbsClient_ready ? 'Y':'N',
                                 node_ptr->uptime,
                                 node_ptr->task.empty() ? "empty" : node_ptr->task.c_str());
                    }
                }
            }
            node_ptr->addStage = MTC_ADD__START;

            plog ("%s Host Add Completed (uptime:%d)\n", node_ptr->hostname.c_str(), node_ptr->uptime );
            node_ptr->add_completed = true ;
            break ;
        }
    }
    return (rc);
}

int nodeLinkClass::bmc_handler ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->bmc_provisioned == true )
    {
#ifdef WANT_FIT_TESTING
        if (( node_ptr->bmc_accessible == true ) &&
            ( node_ptr->bm_ping_info.ok == true ) &&
            ( daemon_is_file_present ( MTC_CMD_FIT__JSON_LEAK_SOAK ) == true ))
        {
            pingUtil_restart ( node_ptr->bm_ping_info );
        }
#endif

        /*****************************************************************
         * Run the ping monitor if BMC provisioned and ip address is valid
         *****************************************************************/
        if (( node_ptr->bmc_provisioned ) &&
            ( hostUtil_is_valid_ip_addr ( node_ptr->bm_ping_info.ip )))
        {
            pingUtil_acc_monitor ( node_ptr->bm_ping_info );
        }

        /*****************************************************************
         * Manage getting the bm password but only when ping is ok
         ****************************************************************/
        if ( node_ptr->bm_pw.empty() )
        {
            barbicanSecret_type * secret = secretUtil_manage_secret( node_ptr->secretEvent,
                                                                     node_ptr->hostname,
                                                                     node_ptr->uuid,
                                                                     node_ptr->bm_timer,
                                                                     mtcTimer_handler );
            if ( secret->stage == MTC_SECRET__GET_PWD_RECV )
            {
                httpUtil_free_conn ( node_ptr->secretEvent );
                httpUtil_free_base ( node_ptr->secretEvent );
                if ( secret->payload.empty() )
                {
                    wlog ("%s failed to acquire bmc password", node_ptr->hostname.c_str());
                    secret->stage = MTC_SECRET__GET_PWD_FAIL ;
                }
                else
                {
                    node_ptr->bm_pw = secret->payload ;
                    ilog ("%s bmc password received", node_ptr->hostname.c_str());
                    secret->stage = MTC_SECRET__START ;
                }
            }
            else
            {
                ilog_throttled (node_ptr->bm_pw_wait_log_throttle, 10000,
                                "%s bmc handler is waiting on bmc password",
                                node_ptr->hostname.c_str());
            }
        }

        else if (( node_ptr->bmc_accessible == true ) &&
                 ( node_ptr->bm_ping_info.ok == false ))
        {
            wlog ("%s bmc access lost\n", node_ptr->hostname.c_str());

            /* Be sure the BMC info file is removed.
             * The 'hwmond' reads it and gets the bmc fw version from it. */
            string bmc_info_filename = "" ;
            if ( node_ptr->bmc_protocol == BMC_PROTOCOL__REDFISHTOOL )
            {
                bmc_info_filename.append(REDFISHTOOL_OUTPUT_DIR) ;
            }
            else
            {
                bmc_info_filename.append(IPMITOOL_OUTPUT_DIR) ;
            }
            bmc_info_filename.append(node_ptr->hostname);
            bmc_info_filename.append(BMC_INFO_FILE_SUFFIX);
            daemon_remove_file ( bmc_info_filename.data() );

            thread_kill ( node_ptr->bmc_thread_ctrl, node_ptr->bmc_thread_info );

            bmc_access_data_init ( node_ptr );

            pingUtil_restart ( node_ptr->bm_ping_info );

            /* start a timer that will raise the BM Access alarm
             * if we are not accessible by the time it expires */
            plog ("%s bmc access timer started (%d secs)\n", node_ptr->hostname.c_str(), MTC_MINS_2);
            mtcTimer_reset ( node_ptr->bmc_access_timer );
            mtcTimer_start ( node_ptr->bmc_access_timer, mtcTimer_handler, MTC_MINS_2 );
            mtcTimer_reset ( node_ptr->bmc_audit_timer );
        }

        /* If the BMC protocol has not yet been learned then do so.
         * Default is ipmi unless the target host responds to a
         * redfish root query with a minimum version number ; 1.0 */
        else if (( node_ptr->bm_ping_info.ok == true ) &&
                 ( node_ptr->bmc_protocol == BMC_PROTOCOL__DYNAMIC ))
        {
            if ( node_ptr->bmc_protocol_learning == false )
            {
                mtcTimer_reset ( node_ptr->bm_timer );
                ilog("%s BMC Re-Connect Start", node_ptr->hostname.c_str());

                /* send the BMC Query request ; redfish 'root' request */
                if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__BMC_QUERY ) != PASS )
                {
                    wlog ("%s %s send failed",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str());
                    return ( bmc_default_to_ipmi ( node_ptr ) );
                }
                else
                {
                    ilog ("%s bmc communication protocol discovery\n",
                              node_ptr->hostname.c_str());

                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_FIRST_WAIT );
                    node_ptr->bmc_protocol_learning = true ;
                }
            }
            else if ( mtcTimer_expired ( node_ptr->bm_timer ) )
            {
                int rc ;

                /* try and receive the response */
                if ( ( rc = bmc_command_recv ( node_ptr ) ) == RETRY )
                {
                    wlog ("%s %s recv retry in %d secs",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                              MTC_RETRY_WAIT);
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
                else if ( rc != PASS )
                {
                    if (( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__BMC_QUERY ) &&
                        (( rc == FAIL_SYSTEM_CALL ) || ( rc == FAIL_NOT_ACTIVE )))
                    {
                        ilog("%s BMC Re-Connect End ; ipmi", node_ptr->hostname.c_str());
                        /* TODO: may need retries */
                        plog ("%s bmc does not support Redfish",
                                  node_ptr->hostname.c_str());
                    }
                    else
                    {
                        wlog ("%s %s recv failed (rc:%d:%d:%s)",
                                  node_ptr->hostname.c_str(),
                                  bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                                  rc,
                                  node_ptr->bmc_thread_info.status,
                                  node_ptr->bmc_thread_info.status_string.c_str());
                    }
                    return ( bmc_default_to_ipmi ( node_ptr ) );
                }
                else
                {
                    ilog("%s BMC Re-Connect End", node_ptr->hostname.c_str());
                    mtcTimer_reset ( node_ptr->bm_timer );

                    /* check response for redfish support */
                    blog ("%s %s recv ; checking for redfish support",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str());

                    if ( redfishUtil_is_supported ( node_ptr->hostname,
                            node_ptr->bmc_thread_info.data) == true )
                    {
                        mtcInfo_set ( node_ptr, MTCE_INFO_KEY__BMC_PROTOCOL, BMC_PROTOCOL__REDFISH_STR );
                        node_ptr->bmc_protocol = BMC_PROTOCOL__REDFISHTOOL ;
                    }
                    else
                    {
                        mtcInfo_set ( node_ptr, MTCE_INFO_KEY__BMC_PROTOCOL, BMC_PROTOCOL__IPMI_STR );
                        node_ptr->bmc_protocol = BMC_PROTOCOL__IPMITOOL ;
                    }
                    /* store mtcInfo, which specifies the selected BMC protocol,
                     * into the sysinv database */
                    mtcInvApi_update_mtcInfo ( node_ptr );

                    ilog ("%s bmc control using %s:%s",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getProtocol_str(node_ptr->bmc_protocol).c_str(),
                              node_ptr->bm_ip.c_str());

                    node_ptr->bmc_thread_ctrl.done = true ;
                }
            }
        } /* end bmc learning */

        /*****************************************************************
         *             Handle Redfish BMC Info Query
         *
         * This block issues a 'Redfish Systems get to acquire and log
         * BMC info and if successful declare BMC accessible.
         *
         *****************************************************************/
        else if (( node_ptr->bmc_protocol == BMC_PROTOCOL__REDFISHTOOL ) &&
                 ( node_ptr->bmc_accessible == false ) &&
                 ( node_ptr->bm_ping_info.ok == true ) &&
                 ( node_ptr->bmc_info_query_done == false ) &&
                 ( mtcTimer_expired (node_ptr->bm_timer ) == true ))
        {
            if ( node_ptr->bmc_info_query_active == false )
            {
                if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__BMC_INFO ) != PASS )
                {
                    elog ("%s bmc redfish '%s' send failed\n",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str());
                    if ( node_ptr->bmc_protocol_learning )
                    {
                        return ( bmc_default_to_ipmi ( node_ptr ) );
                    }
                }
                else
                {
                    node_ptr->bmc_info_query_active = true ;
                    blog ("%s bmc redfish '%s' in progress",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str());
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_FIRST_WAIT );
                }
            }
            else
            {
                int rc ;
                if ( ( rc = bmc_command_recv ( node_ptr ) ) == RETRY )
                {
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
                else if ( rc != PASS )
                {
                    if ( node_ptr->bmc_protocol_learning )
                        bmc_default_to_ipmi ( node_ptr );
                    else
                    {
                        /* If not in learning mode then force the retry
                         * from start in MTC_BMC_REQUEST_DELAY seconds */
                        bmc_default_query_controls ( node_ptr );
                        node_ptr->bmc_thread_ctrl.done = true ;
                        mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                    }
                }
                else
                {
                    node_ptr->bmc_thread_ctrl.done = true ;

                    /* get the bmc info string the read thread provided */
                    if ( redfishUtil_get_bmc_info (
                                node_ptr->hostname,
                                node_ptr->bmc_thread_info.data,
                                node_ptr->bmc_info ) != PASS )
                    {
                        elog ("%s bmc redfish %s or get bmc info failed",
                                  node_ptr->hostname.c_str(),
                                  bmcUtil_getCmd_str(
                                  node_ptr->bmc_thread_info.command).c_str());
                        if ( node_ptr->bmc_protocol_learning )
                            bmc_default_to_ipmi ( node_ptr );
                        else
                        {
                            /* If not in learning mode then force the retry
                             * from start in MTC_BMC_REQUEST_DELAY seconds */
                            bmc_default_query_controls ( node_ptr );
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                        }
                    }
                    else
                    {
                        node_ptr->bmc_info_query_done = true ;
                        node_ptr->bmc_info_query_active = false ;
                    }
                }
            }
        }

        /* Handle Redfish BMC reset/power command query using the redfishtool
         * raw GET command.
         * This is the last operation before declaring the BMC accessible */
        else if (( node_ptr->bmc_protocol == BMC_PROTOCOL__REDFISHTOOL ) &&
                 ( node_ptr->bmc_accessible == false ) &&
                 ( node_ptr->bm_ping_info.ok == true ) &&
                 ( node_ptr->bmc_info_query_done == true ) &&
                 ( node_ptr->bmc_actions_query_done == false ) &&
                 ( mtcTimer_expired (node_ptr->bm_timer ) == true ))
        {
            if ( node_ptr->bmc_info.power_ctrl.raw_target_path.empty() )
            {
                node_ptr->bmc_actions_query_done = true ;
            }
            else if ( node_ptr->bmc_actions_query_active == false )
            {
                blog ("%s bmc action info target: %s",
                          node_ptr->hostname.c_str(),
                          node_ptr->bmc_info.power_ctrl.raw_target_path.c_str());

                if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__RAW_GET ) != PASS )
                {
                    elog ("%s bmc redfish '%s' send failed\n",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str());
                    if ( node_ptr->bmc_protocol_learning )
                        bmc_default_to_ipmi ( node_ptr );
                    else
                    {
                        /* If not in learning mode then force the retry
                         * from start in MTC_BMC_REQUEST_DELAY seconds */
                        bmc_default_query_controls ( node_ptr );
                        mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                    }
                }
                else
                {
                    node_ptr->bmc_actions_query_active = true ;
                    blog ("%s bmc redfish '%s' in progress",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str());
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_FIRST_WAIT );
                }
            }
            else
            {
                int rc ;
                bool default_to_ipmi = false ;
                if ( ( rc = bmc_command_recv ( node_ptr ) ) == RETRY )
                {
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
                else if ( rc != PASS )
                {
                    if ( node_ptr->bmc_protocol_learning )
                        bmc_default_to_ipmi ( node_ptr );
                    else
                    {
                        /* If not in learning mode then force the retry
                         * from start in MTC_BMC_REQUEST_DELAY seconds */
                        bmc_default_query_controls ( node_ptr );
                        node_ptr->bmc_thread_ctrl.done = true ;
                        mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                    }
                }
                else
                {
                    node_ptr->bmc_thread_ctrl.done = true ;

                    blog ("%s bmc thread info cmd: %s data:\n%s",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str(),
                              node_ptr->bmc_thread_info.data.c_str() );

                    /* Look for Parameters as list */
                    std::list<string> param_list ;
                    if ( jsonUtil_get_list ((char*)node_ptr->bmc_thread_info.data.data(),
                                            REDFISH_LABEL__PARAMETERS,
                                            param_list ) == PASS )
                    {
                        /* Walk through the host action list looking for and updating
                         * this host's bmc_info supported actions lists */
                        int index = 0 ;
                        bool actions_found = false ;
                        std::list<string>::iterator param_list_ptr ;
                        for ( param_list_ptr  = param_list.begin();
                              param_list_ptr != param_list.end() ;
                              param_list_ptr++, ++index )
                        {
                            std::list<string> action_list ;
                            string param_list_str = *param_list_ptr ;
                            blog ("%s %s element %d:%s",
                                       node_ptr->hostname.c_str(),
                                       REDFISH_LABEL__PARAMETERS,
                                       index, param_list_str.c_str());

                            if ( jsonUtil_get_list ((char*)param_list_str.data(),
                                                    REDFISH_LABEL__ALLOWABLE_VALUES,
                                                    action_list ) == PASS )
                            {
                                actions_found = true ;
                                redfishUtil_load_actions ( node_ptr->hostname,
                                                           node_ptr->bmc_info,
                                                           action_list );
                                break ;
                            }
                        }
                        if ( actions_found == false )
                        {
                            elog ("%s failed to find '%s' in:\n%s",
                                      node_ptr->hostname.c_str(),
                                      REDFISH_LABEL__ALLOWABLE_VALUES,
                                      node_ptr->bmc_thread_info.data.c_str());
                            default_to_ipmi = true ;
                        }
                    }
                    else
                    {
                        elog ("%s failed to get Action '%s' list from %s",
                                  node_ptr->hostname.c_str(),
                                  REDFISH_LABEL__PARAMETERS,
                                  node_ptr->bmc_thread_info.data.c_str());
                        default_to_ipmi = true ;
                    }

                    /* force failover to use IPMI */
                    if ( default_to_ipmi == true )
                    {
                        if ( node_ptr->bmc_protocol_learning )
                            bmc_default_to_ipmi ( node_ptr );
                        else
                        {
                            bmc_default_query_controls ( node_ptr );
                            node_ptr->bmc_thread_ctrl.done = true ;
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                        }
                    }
                    else
                    {
                        node_ptr->bmc_actions_query_done = true ;
                        node_ptr->bmc_actions_query_active = false ;
                    }
                }
            }

            /* finish up when the actions query is done */
            if ( node_ptr->bmc_actions_query_done == true )
            {
                mtcTimer_reset ( node_ptr->bm_timer );
                mtcTimer_reset ( node_ptr->bmc_audit_timer );

                int bmc_audit_period = daemon_get_cfg_ptr()->bmc_audit_period ;
                if ( bmc_audit_period )
                {
                    /* the time for the first audit is twice the configured period */
                    mtcTimer_start ( node_ptr->bmc_audit_timer, mtcTimer_handler, bmc_audit_period*2 );
                    plog ("%s bmc audit timer started (%d secs)", node_ptr->hostname.c_str(), bmc_audit_period*2);
                }
                else
                {
                    ilog("%s bmc audit disabled", node_ptr->hostname.c_str());
                }

                /* success path */
                node_ptr->bmc_accessible = true ;
                node_ptr->bmc_info_query_done = true ;
                node_ptr->bmc_info_query_active = false ;
                node_ptr->bmc_actions_query_done = true ;
                node_ptr->bmc_actions_query_active = false ;
                node_ptr->bmc_protocol_learning = false ;

                mtcInfo_set ( node_ptr, MTCE_INFO_KEY__BMC_PROTOCOL, BMC_PROTOCOL__REDFISH_STR );

                mtcTimer_reset ( node_ptr->bmc_access_timer );

                /* save the host's power state */
                node_ptr->power_on = node_ptr->bmc_info.power_on ;

                plog ("%s bmc is accessible using redfish",
                          node_ptr->hostname.c_str());

                node_ptr->bmc_thread_ctrl.done = true  ;
                node_ptr->bmc_thread_info.command = 0  ;

                /* store mtcInfo, which specifies the selected BMC protocol,
                 * into the sysinv database */
                mtcInvApi_update_mtcInfo ( node_ptr );

                /* push the BMC access info out to the mtcClient when
                 * a controller's BMC connection is established/verified */
                if ( node_ptr->nodetype & CONTROLLER_TYPE )
                    this->want_mtcInfo_push = true ;

                send_hwmon_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );
                send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );
            }
        }

        /*****************************************************************
         *             Handle IPMI BMC Info Query
         *
         * This block queries and logs
         *
         *  - BMC info
         *  - BMC last Reset Cause
         *  - BMC power state
         *
         * and if successful declares BMC accessible.
         *
         *****************************************************************/
        else if ((node_ptr->bmc_protocol == BMC_PROTOCOL__IPMITOOL ) &&
                 ( node_ptr->bmc_accessible == false ) &&
                 ( node_ptr->bm_ping_info.ok == true ) &&
                 (( node_ptr->bmc_info_query_done == false ) ||
                  ( node_ptr->reset_cause_query_done == false ) ||
                  ( node_ptr->power_status_query_done == false )) &&
                  ( mtcTimer_expired (node_ptr->bm_timer ) == true ))
        {
            int rc = PASS ;
            if (( node_ptr->bmc_info_query_active == false ) &&
                ( node_ptr->bmc_info_query_done   == false ))
            {
                if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__BMC_INFO ) != PASS )
                {
                    elog ("%s %s send failed\n",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str());
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                }
                else
                {
                    blog ("%s %s\n", node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str());
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_FIRST_WAIT );
                    node_ptr->bmc_info_query_active = true ;
                }
            }
            else if (( node_ptr->bmc_info_query_active == true ) &&
                     ( node_ptr->bmc_info_query_done   == false))
            {
                if ( ( rc = bmc_command_recv ( node_ptr ) ) == RETRY )
                {
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
                else if ( rc != PASS )
                {
                    /* this error is reported by the bmc receive driver */
                    node_ptr->bmc_info_query_active = false ;
                    node_ptr->bmc_thread_ctrl.done = true ;
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                }
                else
                {
                    node_ptr->bmc_info_query_active = false ;
                    node_ptr->bmc_info_query_done = true ;
                    node_ptr->bmc_thread_ctrl.done = true ;
                    ipmiUtil_bmc_info_load ( node_ptr->hostname,
                                             node_ptr->bmc_thread_info.data.data(),
                                             node_ptr->bmc_info );
                }
            }
            else if (( node_ptr->bmc_info_query_active == false ) &&
                     ( node_ptr->bmc_info_query_done   == true  ))
            {
                if (( node_ptr->reset_cause_query_active == false ) &&
                    ( node_ptr->reset_cause_query_done   == false ))
                {
                    if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__RESTART_CAUSE ) != PASS )
                    {
                        elog ("%s %s send failed\n",
                                  node_ptr->hostname.c_str(),
                                  bmcUtil_getCmd_str(
                                  node_ptr->bmc_thread_info.command).c_str());

                        mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                    }
                    else
                    {
                        blog ("%s %s\n", node_ptr->hostname.c_str(),
                                  bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str());
                        mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_FIRST_WAIT );
                        node_ptr->reset_cause_query_active = true ;
                    }
                }
                else if (( node_ptr->reset_cause_query_active == true ) &&
                         ( node_ptr->reset_cause_query_done   == false ))
                {
                    if ( ( rc = bmc_command_recv ( node_ptr ) ) == RETRY )
                    {
                        mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                    }
                    else if ( rc != PASS )
                    {
                        elog ("%s %s command failed\n",
                                  node_ptr->hostname.c_str(),
                                  bmcUtil_getCmd_str(
                                  node_ptr->bmc_thread_info.command).c_str());
                        node_ptr->reset_cause_query_active = false ;
                        mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                        node_ptr->bmc_thread_ctrl.done = true ;
                    }
                    else
                    {
                        mtcTimer_reset ( node_ptr->bm_timer );
                        node_ptr->reset_cause_query_active = false ;
                        node_ptr->reset_cause_query_done   = true ;
                        node_ptr->bmc_thread_ctrl.done = true ;
                        ilog ("%s %s\n", node_ptr->hostname.c_str(),
                                         node_ptr->bmc_thread_info.data.c_str());
                    }
                }
                else if (( node_ptr->bmc_info_query_done     == true ) &&
                         ( node_ptr->reset_cause_query_done  == true ) &&
                         ( node_ptr->power_status_query_done == false ))
                {
                    if ( node_ptr->power_status_query_active == false )
                    {
                        if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_STATUS ) != PASS )
                        {
                            elog ("%s %s send failed\n",
                                      node_ptr->hostname.c_str(),
                                      bmcUtil_getCmd_str(
                                      node_ptr->bmc_thread_info.command).c_str());
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                        }
                        else
                        {
                            dlog ("%s %s\n",
                                      node_ptr->hostname.c_str(),
                                      bmcUtil_getCmd_str(
                                      node_ptr->bmc_thread_info.command).c_str());
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_FIRST_WAIT );
                            node_ptr->power_status_query_active = true ;
                        }
                    }
                    else if ( node_ptr->power_status_query_done == false )
                    {
                        if ( ( rc = bmc_command_recv ( node_ptr ) ) == RETRY )
                        {
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                        }
                        else if ( rc )
                        {
                            node_ptr->power_status_query_active = false ;
                            node_ptr->bmc_thread_ctrl.done = true ;
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                        }
                        else
                        {
                            mtcTimer_reset ( node_ptr->bm_timer );
                            mtcTimer_reset ( node_ptr->bmc_access_timer );
                            node_ptr->power_status_query_active = false ;
                            node_ptr->power_status_query_done   = true  ;
                            node_ptr->bmc_thread_ctrl.done = true  ;
                            node_ptr->bmc_thread_info.command = 0  ;
                            node_ptr->bmc_accessible = true ;
                            node_ptr->bm_ping_info.ok = true;

                            ilog ("%s %s\n", node_ptr->hostname.c_str(),
                                             node_ptr->bmc_thread_info.data.c_str());
                            plog ("%s bmc is accessible using ipmi\n", node_ptr->hostname.c_str());

                            /* set host power state ; on or off */
                            if ( node_ptr->bmc_thread_info.data.find (IPMITOOL_POWER_ON_STATUS) != std::string::npos )
                                node_ptr->power_on = true ;
                            else
                                node_ptr->power_on = false ;
                            if ( node_ptr->bmc_thread_info.data.find (IPMITOOL_POWER_OFF_STATUS) != std::string::npos )
                            {
                                if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
                                {
                                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__POWERED_OFF );
                                }
                                else
                                {
                                    wlog ("%s is powered off while in the unlocked state\n", node_ptr->hostname.c_str());
                                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__POWERED_OFF );
                                }
                            } /* end power off detection handling     */

                            /* push the BMC access info out to the mtcClient when
                             * a controller's BMC connection is established/verified */
                            if ( node_ptr->nodetype & CONTROLLER_TYPE )
                                this->want_mtcInfo_push = true ;

                            send_hwmon_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );
                            send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );

                        } /* end query handling success path               */
                    } /* end power status query handling                   */
                } /* end query info stages handling                        */
            } /* end handling ipmi query, info, restart cause, power state */
        } /* end main condition handling                                   */

        /* BMC Access Audit for Redfish.
         *  - used to refresh the host power state */
        else if (( node_ptr->bmc_protocol == BMC_PROTOCOL__REDFISHTOOL ) &&
                 ( node_ptr->bmc_provisioned ) &&
                 ( node_ptr->bmc_accessible ) &&
                 ( mtcTimer_expired ( node_ptr->bmc_audit_timer ) == true ) &&
                 ( mtcTimer_expired ( node_ptr->bm_timer ) == true ) &&
                 ( daemon_get_cfg_ptr()->bmc_audit_period != 0))
        {
            if ( node_ptr->bmc_thread_ctrl.done )
            {
                ilog("%s BMC Audit Start", node_ptr->hostname.c_str());
                /* send the BMC Query command */
                if ( bmc_command_send ( node_ptr, BMC_THREAD_CMD__BMC_INFO ) != PASS )
                {
                    elog ("%s bmc redfish '%s' send failed\n",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str());
                    pingUtil_restart ( node_ptr->bm_ping_info );
                }
                else
                {
                    blog1 ("%s bmc redfish '%s' audit in progress",
                               node_ptr->hostname.c_str(),
                               bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str());
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
            }
            else if ( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__BMC_INFO )
            {
                int rc ;
                if ( ( rc = bmc_command_recv ( node_ptr ) ) == RETRY )
                {
                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                }
                else if ( rc != PASS )
                {
                    wlog ("%s bmc audit failed receive (rc:%d)",
                              node_ptr->hostname.c_str(), rc );
                    pingUtil_restart ( node_ptr->bm_ping_info );
                }
                else if ( node_ptr->bmc_thread_info.data.empty())
                {
                    wlog ("%s bmc audit failed get bmc query response data",
                              node_ptr->hostname.c_str());
                    pingUtil_restart ( node_ptr->bm_ping_info );
                }
                else
                {
                    string filedata = daemon_read_file (node_ptr->bmc_thread_info.data.data()) ;
                    struct json_object *json_obj =
                    json_tokener_parse((char*)filedata.data());
                    ilog("%s BMC Audit End", node_ptr->hostname.c_str());
                    if ( json_obj )
                    {
                        /* load the power state */
                        bool power_on ;
                        string power_state =
                        tolowercase(jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__POWER_STATE));
                        if ( power_state == BMC_POWER_ON_STATUS )
                            power_on = true ;
                        else if ( power_state == BMC_POWER_OFF_STATUS )
                            power_on = false ;
                        else
                        {
                            wlog ("%s bmc audit failed to get power state",
                                      node_ptr->hostname.c_str());
                            pingUtil_restart ( node_ptr->bm_ping_info );
                            rc = FAIL_JSON_PARSE ;
                        }
                        if ( rc == PASS )
                        {
                            if ( power_on != node_ptr->power_on )
                            {
                                ilog ("%s power state changed to %s",
                                          node_ptr->hostname.c_str(),
                                          power_state.c_str());
                            }
                            node_ptr->power_on = power_on ;
                            mtcTimer_start ( node_ptr->bmc_audit_timer,
                                             mtcTimer_handler,
                                             daemon_get_cfg_ptr()->bmc_audit_period );
                            blog ("%s bmc audit timer re-started (%d secs)",
                                      node_ptr->hostname.c_str(),
                                      daemon_get_cfg_ptr()->bmc_audit_period);
                        }
                        json_object_put(json_obj);
                    }
                    else
                    {
                        pingUtil_restart ( node_ptr->bm_ping_info );
                        wlog ("%s bmc audit failed parse bmc query response",
                                  node_ptr->hostname.c_str());
                    }
                }
            }
        }

        /******************************************************************
         *        Manage the Board Management Access Alarm
         ******************************************************************/
        if (( node_ptr->bmc_accessible == false ) &&
            ( mtcTimer_expired ( node_ptr->bmc_access_timer ) == true ))
        {
            pingUtil_restart ( node_ptr->bm_ping_info );

            /* start a timer that will raise the BM Access alarm
             * if we are not accessible by the time it expires */
            mtcTimer_start ( node_ptr->bmc_access_timer, mtcTimer_handler, MTC_MINS_2 );

            if ( node_ptr->alarms[MTC_ALARM_ID__BM] == FM_ALARM_SEVERITY_CLEAR )
            {
                plog ("%s bmc access timer started (%d secs)\n", node_ptr->hostname.c_str(), MTC_MINS_2);
                mtcAlarm_warning ( node_ptr->hostname, MTC_ALARM_ID__BM );
                node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_WARNING ;
            }
        }

        /****************************************************************
         * Manage BM access alarm clear
         *
         * ... if BMs are accessible then see if we need to clear the
         *     major BM Alarm.
         *****************************************************************/
        if (( node_ptr->bmc_accessible == true ) &&
            ( node_ptr->alarms[MTC_ALARM_ID__BM] != FM_ALARM_SEVERITY_CLEAR ) &&
             ((( node_ptr->bmc_protocol == BMC_PROTOCOL__IPMITOOL ) &&
               ( node_ptr->bmc_info_query_done == true )            &&
               ( node_ptr->reset_cause_query_done == true )         &&
               ( node_ptr->power_status_query_done == true )) ||
              (( node_ptr->bmc_protocol == BMC_PROTOCOL__REDFISHTOOL ) &&
               ( node_ptr->bmc_protocol_learning == false ))))
        {
            mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__BM );
            node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_CLEAR ;
        } /* else alarms already cleared */
    } /* end if bmc_provisioned */
    else if ( node_ptr->alarms[MTC_ALARM_ID__BM] != FM_ALARM_SEVERITY_CLEAR )
    {
        mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__BM );
        node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_CLEAR ;
    }
    return (PASS);
}


int nodeLinkClass::oos_test_handler ( struct nodeLinkClass::node * node_ptr )
{
    switch (node_ptr->oosTestStage)
    {
        case MTC_OOS_TEST__LOAD_NEXT_TEST:
        {
            oosTestStageChange ( node_ptr, MTC_OOS_TEST__START_WAIT );
            break ;
        }
        case MTC_OOS_TEST__START_WAIT:
        {
            /* Monitor timer errors */
            mtcTimer_dump_data ();

            // blog ("%s Inservice Test Period %d secs\n", node_ptr->hostname.c_str(), oos_test_period);
            mtcTimer_start ( node_ptr->oosTestTimer, mtcTimer_handler, oos_test_period );
            oosTestStageChange ( node_ptr, MTC_OOS_TEST__WAIT );

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__CORRUPT_TOKEN, node_ptr->hostname ))
                tokenUtil_fail_token ();

            else if ( daemon_want_fit ( FIT_CODE__STUCK_TASK, node_ptr->hostname ))
                mtcInvApi_update_task ( node_ptr, MTC_TASK_SWACT_INPROGRESS);

            else if ( daemon_want_fit ( FIT_CODE__STOP_HOST_SERVICES, node_ptr->hostname ))
            {
                bool start = false ;
                this->launch_host_services_cmd ( node_ptr, start );
            }

            if (( daemon_is_file_present ( MTC_CMD_FIT__GOENABLE_AUDIT )) &&
                ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                ( node_ptr->operState  == MTC_OPER_STATE__ENABLED ))
            {
                /* Request Out-Of--Service test execution */
                send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MAIN_GOENABLED, MGMNT_INTERFACE );
                if ( node_ptr->operState_subf == MTC_OPER_STATE__ENABLED)
                {
                    send_mtc_cmd ( node_ptr->hostname, MTC_REQ_SUBF_GOENABLED, MGMNT_INTERFACE );
                }
            }
#endif

            if ( node_ptr->ar_disabled == true )
            {
                elog ( "%s auto recovery disabled cause:%d",
                           node_ptr->hostname.c_str(),
                           node_ptr->ar_cause );
            }

            /* Avoid forcing the states to the database when on the first & second pass.
             * This is because it is likely we just read all the states and
             * if coming out of a DOR or a SWACT we don't need to un-necessarily
             * produce that extra sysinv traffic.
             * Also, no point forcing the states while there is an admin action
             * or enable or graceful recovery going on as well because state changes
             * are being done in the FSM already */
            if (( node_ptr->oos_test_count > 1 ) &&
                ( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE ) &&
                ( !node_ptr->enableStage ) && ( !node_ptr->recoveryStage ))
            {
                /* Change the oper and avail states in the database */
                allStateChange ( node_ptr, node_ptr->adminState,
                                           node_ptr->operState,
                                           node_ptr->availStatus );
            }

            /* Make sure the locked status on the host itself is set */
            if (( node_ptr->adminState  == MTC_ADMIN_STATE__LOCKED  ) &&
                ( node_ptr->operState   == MTC_OPER_STATE__DISABLED ) &&
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE ) &&
                ( !(node_ptr->mtce_flags & MTC_FLAG__I_AM_LOCKED)    ))
            {
                ilog ("%s setting 'locked' status\n", node_ptr->hostname.c_str());

                /* Tell the host that it is locked */
                send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, MGMNT_INTERFACE);
                if ( clstr_network_provisioned )
                {
                    ilog ("%s Sending Lock Cluster", node_ptr->hostname.c_str() );
                    send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, CLSTR_INTERFACE );
                }
            }

            /* audit alarms */
            mtcAlarm_audit (node_ptr );

            break ;
        }
        case MTC_OOS_TEST__WAIT:
        {
            if ( node_ptr->oosTestTimer.ring == true )
            {
                oosTestStageChange ( node_ptr, MTC_OOS_TEST__DONE );
            }
            break ;
        }
        case MTC_OOS_TEST__DONE:
        default:
        {
            node_ptr->oos_test_count++ ;
            oosTestStageChange ( node_ptr, MTC_OOS_TEST__LOAD_NEXT_TEST );

            /* clear out the retry counter periodically */
            node_ptr->http_retries_cur = 0 ;

            break ;
        }
    }
    return (PASS);
}

///////////////////////////////////////////////////////////////////////////////
//
// Name       : pxeboot_mtcAlive_monitor
//
// Purpose    : Monitor pxeboot network mtcAlive and manage associated alarm.
//
// Description: Monitor pxeboot mtcAlive messages.
//              Request mtcAlive when not receiving mtcAlive messages.
//              Debounce mtcAlive messaging and manage alarm accordingly.
//
// Parameters : nodeLinkClass::node struct pointer - node_ptr
//
// Returns    : PASS
//
///////////////////////////////////////////////////////////////////////////////
int nodeLinkClass::pxeboot_mtcAlive_monitor ( struct nodeLinkClass::node * node_ptr )
{
    flog ("%s pxeboot mtcAlive fsm stage: %s",
           node_ptr->hostname.c_str(),
           get_mtcAliveStages_str(node_ptr->mtcAliveStage).c_str());


    // Don't monitor pxeboot mtcAlive messaging while the node is
    // locked, disabled or in the following administrative action states.
    if (( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED ) ||
        ( node_ptr->operState == MTC_OPER_STATE__DISABLED ) ||
        ( node_ptr->adminAction == MTC_ADMIN_ACTION__UNLOCK ) ||
        ( node_ptr->adminAction == MTC_ADMIN_ACTION__ENABLE ) ||
        ( node_ptr->adminAction == MTC_ADMIN_ACTION__RECOVER ) ||
        ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWERCYCLE ))
    {
        // Clear the alarm if the node is locked
        if (( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED ) &&
            ( node_ptr->alarms[MTC_ALARM_ID__MTCALIVE] != FM_ALARM_SEVERITY_CLEAR ))
            alarm_mtcAlive_clear (node_ptr, PXEBOOT_INTERFACE);
        // Switch to START if not already there
        if ( node_ptr->mtcAliveStage != MTC_MTCALIVE__START )
            mtcAliveStageChange (node_ptr, MTC_MTCALIVE__START);
        return PASS ;
    }

    switch (node_ptr->mtcAliveStage)
    {
        // Starts from scratch. Clears timer and counts but not alarm.
        case MTC_MTCALIVE__START:
        {
            alog2 ("%s mtcAlive start", node_ptr->hostname.c_str());
            if ( ! mtcTimer_expired (node_ptr->mtcAlive_timer) )
                mtcTimer_reset (node_ptr->mtcAlive_timer);
            node_ptr->mtcAlive_sequence[PXEBOOT_INTERFACE] = 0 ;
            node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE] = 0 ;
            mtcAliveStageChange (node_ptr, MTC_MTCALIVE__SEND);
            return PASS ;
        }
        // Reloads the controller's pxeboot info and sends it with a mtcAlive request
        // telling the remote node to send send mtcAlive to the active controller.
        case MTC_MTCALIVE__SEND:
        {
            /* pxeboot info refresh audit */
            if ( node_ptr->hostname == my_hostname )
                pxebootInfo_loader ();
            alog2 ("%s mtcAlive send", node_ptr->hostname.c_str());
            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, PXEBOOT_INTERFACE );
            mtcAliveStageChange (node_ptr, MTC_MTCALIVE__MONITOR);
            return PASS ;
        }
        // Start the Wait timer 2x longer than the expected mtcAlive cadence
        case MTC_MTCALIVE__MONITOR:
        {
            alog2 ("%s mtcAlive monitor", node_ptr->hostname.c_str());
            mtcTimer_start ( node_ptr->mtcAlive_timer, mtcTimer_handler,
                             PXEBOOT_MTCALIVE_MONITOR_RATE_SECS );
            mtcAliveStageChange (node_ptr, MTC_MTCALIVE__WAIT);
            return PASS ;
        }
        // Wait for the timer to expire
        case MTC_MTCALIVE__WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcAlive_timer ) )
                mtcAliveStageChange (node_ptr, MTC_MTCALIVE__CHECK);
            return PASS ;
        }
        // Check the mtcAlive sequence numbers and handle each possible case
        // success         - mtcAlive sequence number is greater than the last one - may clear alarm
        // out-of-sequence - mtcAlive sequence number is less than the last one    - may assert alarm
        // miss            - mtcAlive sequence number is equal to the last one     - count misses
        // loss            - mtcAlive messaging miss count exceeded threshold      - assert alarm
        // not seen        - waiting for first mtcAlive following reboot           - request mtcAlive
        case MTC_MTCALIVE__CHECK:
        {
            if ( node_ptr->mtcAlive_sequence[PXEBOOT_INTERFACE] > node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE] )
            {
                // Typical success path
                alog2 ("%s pxeboot mtcAlive received %d messages since last audit ; this:%d  last:%d",
                           node_ptr->hostname.c_str(),
                           node_ptr->mtcAlive_sequence[PXEBOOT_INTERFACE] - node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE],
                           node_ptr->mtcAlive_sequence[PXEBOOT_INTERFACE],
                           node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE]);

                // Now that we received a message we can dec the missed count
                // and clear the alarm if it exists
                if ( node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] )
                {
                    // Set miss count to max if we are have reached at least one loss but no alarm yet
                    if (( node_ptr->alarms[MTC_ALARM_ID__MTCALIVE] == FM_ALARM_SEVERITY_CLEAR ) &&
                        ( node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE] ))
                    {
                        node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] = PXEBOOT_MTCALIVE_LOSS_THRESHOLD ;
                        node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE] = 0 ;
                    }

                    ilog ("%s pxeboot mtcAlive miss count %d ; decrement %s; recovery",
                              node_ptr->hostname.c_str(),
                              node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE],
                              node_ptr->alarms[MTC_ALARM_ID__MTCALIVE] ? "; alarm clear when 0 " : "");
                    node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE]-- ;
                }
                else
                {
                    // Clear alarm and start with a clean loss slate. miss's is already zero
                    node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE] = 0 ;
                    alarm_mtcAlive_clear ( node_ptr, PXEBOOT_INTERFACE );
                }

                // Clear the log throttles now that we have received a message
                if ( node_ptr->pxeboot_mtcAlive_not_seen_log_throttle || node_ptr->pxeboot_mtcAlive_loss_log_throttle )
                {
                    node_ptr->pxeboot_mtcAlive_not_seen_log_throttle = 0 ;
                    node_ptr->pxeboot_mtcAlive_loss_log_throttle = 0 ;
                }

                mtcAliveStageChange (node_ptr, MTC_MTCALIVE__MONITOR);
            }
            else if ( node_ptr->mtcAlive_sequence[PXEBOOT_INTERFACE] < node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE] )
            {
                // mtcClient restart case
                if ( ++node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] < PXEBOOT_MTCALIVE_LOSS_THRESHOLD )
                {
                    // The mtcClient on this host may have been restarted
                    mtcAliveStageChange (node_ptr, MTC_MTCALIVE__SEND);
                }
                else
                    mtcAliveStageChange (node_ptr, MTC_MTCALIVE__FAIL);

                wlog ("%s pxeboot mtcAlive miss count %d ; loss count %d ; out-of-sequence ; this:%d  last:%d",
                          node_ptr->hostname.c_str(),
                          node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE],
                          node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE],
                          node_ptr->mtcAlive_sequence[PXEBOOT_INTERFACE],
                          node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE]);
            }
            else if ( ++node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] < PXEBOOT_MTCALIVE_LOSS_THRESHOLD )
            {
                // Missing pxeboot mtcAlive
                wlog ("%s pxeboot mtcAlive miss count %d ; loss count %d ; sending request",
                          node_ptr->hostname.c_str(),
                          node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE],
                          node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE]);
                // The mtcClient on this host may have been restarted
                mtcAliveStageChange (node_ptr, MTC_MTCALIVE__SEND);
            }
            else
            {
                if ( node_ptr->mtcAlive_pxeboot == true )
                {
                    // If we get there its a loss
                    wlog_throttled (node_ptr->pxeboot_mtcAlive_loss_log_throttle,
                                    PXEBOOT_MTCALIVE_LOSS_LOG_THROTTLE,
                                    "%s pxeboot mtcAlive lost ; missed: %d ; last: count:%d seq: %d ; sending request",
                                    node_ptr->hostname.c_str(),
                                    node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE],
                                    node_ptr->mtcAlive_pxeboot_count,
                                    node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE]);
                }
                else
                {
                    // Otherwise still searching beyond threshold for the first mtcAlive after reboot or graceful recovery
                    ilog_throttled (node_ptr->pxeboot_mtcAlive_not_seen_log_throttle,
                                    PXEBOOT_MTCALIVE_NOT_SEEN_LOG_THROTTLE,
                                    "%s pxeboot mtcAlive not seen yet ; sending request",
                                    node_ptr->hostname.c_str());
                }
                mtcAliveStageChange (node_ptr, MTC_MTCALIVE__FAIL);
            }

            // Prevent the miss count from being larger than the loss, and therfore the alarm clear recovery, threshold.
            if (node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] > PXEBOOT_MTCALIVE_LOSS_THRESHOLD)
                node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] = PXEBOOT_MTCALIVE_LOSS_THRESHOLD;

            node_ptr->mtcAlive_sequence_save[PXEBOOT_INTERFACE] = node_ptr->mtcAlive_sequence[PXEBOOT_INTERFACE] ;
            break ;
        }
        case MTC_MTCALIVE__FAIL:
        default:
        {
            alog2 ("%s mtcAlive fail", node_ptr->hostname.c_str());
            if ( node_ptr->alarms[MTC_ALARM_ID__MTCALIVE] == FM_ALARM_SEVERITY_CLEAR )
            {
                if ( ++node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE] < PXEBOOT_MTCALIVE_LOSS_ALARM_THRESHOLD )
                {
                    wlog ("%s pxeboot mtcAlive lost ; %d more loss before alarm assertion",
                              node_ptr->hostname.c_str(),
                              PXEBOOT_MTCALIVE_LOSS_ALARM_THRESHOLD - node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE] );

                    // Start the misses counter over again after each loss debounce
                    node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] = 0 ;
                }
                else
                {
                    ilog ("%s pxeboot mtcAlive alarm assert (%d)",
                              node_ptr->hostname.c_str(),
                              node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE]);
                    alarm_mtcAlive_failure ( node_ptr, PXEBOOT_INTERFACE );
                }
            }
            mtcAliveStageChange (node_ptr, MTC_MTCALIVE__START);
            break ;
        }
    }
    if ( node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE] || node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE] )
    {
        alog2 ("%s pxeboot mtcAlive: Miss: %d of %d  ,  Loss: %d of %d",
                  node_ptr->hostname.c_str(),
                  node_ptr->mtcAlive_miss_count[PXEBOOT_INTERFACE], PXEBOOT_MTCALIVE_LOSS_THRESHOLD,
                  node_ptr->mtcAlive_loss_count[PXEBOOT_INTERFACE], PXEBOOT_MTCALIVE_LOSS_ALARM_THRESHOLD);
    }
    return (PASS);
}

int local_counter = 0 ;

int nodeLinkClass::insv_test_handler ( struct nodeLinkClass::node * node_ptr )
{
    switch (node_ptr->insvTestStage)
    {
        case MTC_INSV_TEST__START:
        {
            mtcTimer_reset ( node_ptr->insvTestTimer );
            mtcTimer_start ( node_ptr->insvTestTimer, mtcTimer_handler, insv_test_period );
            insvTestStageChange ( node_ptr, MTC_INSV_TEST__WAIT );
            break ;
        }
        case MTC_INSV_TEST__WAIT:
        {
            if ( node_ptr->insvTestTimer.ring == true )
            {
                insvTestStageChange ( node_ptr, MTC_INSV_TEST__RUN );
            }
            break ;
        }
        case MTC_INSV_TEST__RUN:
        {
            if ( daemon_is_file_present ( MTC_CMD_FIT__BMC_ACC_FAIL ))
            {
                if ( node_ptr->bm_ping_info.ok )
                {
                    ilog ("%s FIT failing bmc ping monitor", node_ptr->hostname.c_str());
                    pingUtil_restart ( node_ptr->bm_ping_info );
                }
            }

#ifdef WANT_FIT_TESTING

            daemon_load_fit ();

            if ( daemon_want_fit ( FIT_CODE__UNLOCK_HOST, node_ptr->hostname ) )
            {
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__UNLOCK );
            }

            if ( daemon_want_fit ( FIT_CODE__LOCK_HOST, node_ptr->hostname ) )
            {
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__LOCK );
            }

            if ( daemon_want_fit ( FIT_CODE__FORCE_LOCK_HOST, node_ptr->hostname ) )
            {
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__FORCE_LOCK );
            }

            if (( daemon_want_fit ( FIT_CODE__DO_NOTHING_THREAD, node_ptr->hostname )) ||
                ( daemon_want_fit ( FIT_CODE__STRESS_THREAD , node_ptr->hostname )))
            {
                 node_ptr->bmc_thread_ctrl.stage = THREAD_STAGE__IGNORE ;
                 node_ptr->bmc_thread_ctrl.id = true ;
                 node_ptr->bmc_thread_info.id = true ;
                 node_ptr->bmc_thread_info.command = BMC_THREAD_CMD__POWER_STATUS ;

                 /* Update / Setup the BMC access credentials */
                 node_ptr->thread_extra_info.bm_ip   = node_ptr->bm_ip   ;
                 node_ptr->thread_extra_info.bm_un   = node_ptr->bm_un   ;
                 node_ptr->thread_extra_info.bm_pw   = node_ptr->bm_pw   ;
                 node_ptr->bmc_thread_info.extra_info_ptr = &node_ptr->thread_extra_info ;
                 if ( thread_launch_thread ( mtcThread_bmc, &node_ptr->bmc_thread_info ) == 0 )
                 {
                     slog ("%s FIT launching mtcThread_bmc power query thread ; launch failed\n", node_ptr->hostname.c_str());
                 }
                 else
                 {
                     slog ("%s FIT launching mtcThread_bmc power query thread\n", node_ptr->hostname.c_str());
                 }
                 node_ptr->bmc_thread_ctrl.done = true ;
            }
#endif

            /* Audits for this controller host only */
            if ( node_ptr->hostname == this->my_hostname )
            {
                /* Remind the heartbeat service that this is the active ctrl */
                send_hbs_command ( this->my_hostname, MTC_CMD_ACTIVE_CTRL );
            }

            /* Monitor the health of the host - no pass file */
            if ((  node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
                (  node_ptr->operState   == MTC_OPER_STATE__ENABLED   ))
            {
                if ( this->dor_mode_active == true )
                {
                    ilog_throttled ( this->dor_mode_active_log_throttle, 20,
                                     "DOR mode active\n");
                }

                if ( NOT_THIS_HOST )
                {
                    if ((( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
                        (  node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )) &&
                        (!(node_ptr->mtce_flags & MTC_FLAG__I_AM_HEALTHY) &&
                         !(node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY)))
                    {
                        if ( node_ptr->unknown_health_reported == false )
                        {
                            wlog ( "%s has UNKNOWN HEALTH\n", node_ptr->hostname.c_str());
                            node_ptr->unknown_health_reported = true ;
                        }
                    }
                }
            }

            /** Manage the subfunction goenabled alarm over a mtcAgent restart
             *  In the restart case the subfunction fsm enable handler is not run so
             *  we try to detect the missing goenabled_subf flag as an inservice test.
             *
             *  Only in AIO type
             *   - clear the alarm if the issue goes away -
             *     i.e. the goenabled tests eventually pass. Today
             *     hey are not re-run in the background but someday they may be
             *   - raise the alarm and go degraded if the goEnabled_subf flag is not set
             *     and we have only a single enabled controller (which must be this one)
             *     and the alarm is not already raised.
             **/
            if (( AIO_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                    ( node_ptr->operState == MTC_OPER_STATE__ENABLED ) &&
                    ( node_ptr->mtce_flags & MTC_FLAG__SUBF_CONFIGURED )) /* handle initial install case */
                {
                    if (( node_ptr->goEnabled_subf == true ) &&
                        ( node_ptr->inservice_failed_subf == false ) &&
                        ( node_ptr->goEnabled_failed_subf == false ) &&
                        ( node_ptr->hostservices_failed_subf == false ))
                    {
                        if ( node_ptr->alarms[MTC_ALARM_ID__CH_COMP] != FM_ALARM_SEVERITY_CLEAR )
                        {
                            alarm_compute_clear ( node_ptr, false );
                            ilog ("%s cleared alarm %s due to failure recovery (degrade:%x)\n",
                                      node_ptr->hostname.c_str(),
                                      mtcAlarm_getId_str(MTC_ALARM_ID__CH_COMP).c_str(),
                                      node_ptr->degrade_mask);


                            if ( node_ptr->degrade_mask == 0 )
                            {
                                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                                           MTC_OPER_STATE__ENABLED,
                                                           MTC_AVAIL_STATUS__AVAILABLE );

                                subfStateChange ( node_ptr, MTC_OPER_STATE__ENABLED,
                                                            MTC_AVAIL_STATUS__AVAILABLE );

                                /* Inform the VIM that this host is enabled */
                                mtcVimApi_state_change ( node_ptr, VIM_HOST_ENABLED, 3 );
                            }
                        }
                    }
                    /*
                     * Send out-of-service test command and wait for the
                     * next audit interval to see the result.
                     *
                     *  node_ptr->goEnabled_subf        == true is pass
                     *  node_ptr->goEnabled_subf_failed == true is fail
                     *
                     **/
                    if (( node_ptr->operState_subf == MTC_OPER_STATE__DISABLED ) &&
                        ( node_ptr->ar_disabled == false ))
                    {
                        if (( node_ptr->adminAction != MTC_ADMIN_ACTION__ENABLE_SUBF ) &&
                            ( node_ptr->adminAction != MTC_ADMIN_ACTION__ENABLE ))
                        {
                            if (( node_ptr->inservice_failed_subf == false ) &&
                                ( node_ptr->hostservices_failed_subf == false ))
                            {
                                ilog ("%s-worker ... running recovery enable\n", node_ptr->hostname.c_str());

                                alarm_compute_clear ( node_ptr, true );

                                enableStageChange ( node_ptr, MTC_ENABLE__START );
                                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__ENABLE_SUBF );
                            }
                            else
                            {
                                ilog ("%s-worker subfunction is unlocked-disabled (non-operational)\n",
                                          node_ptr->hostname.c_str());
                            }
                        }
                        else
                        {
                            ilog ("%s-worker ... waiting on current goEnable completion\n", node_ptr->hostname.c_str() );
                        }
                    }
                }
            }

            /* Monitor the health of the host */
            if ((  node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
                (  node_ptr->operState   == MTC_OPER_STATE__ENABLED   ) &&
                (( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
                 ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )))
            {
                /* Manage asserting degrade due to Software Management */
                if (( node_ptr->mtce_flags & MTC_FLAG__SM_DEGRADED ) &&
                    ( !(node_ptr->degrade_mask & DEGRADE_MASK_SM)))
                {
                    /* set the SM degrade flag in the mask */
                    node_ptr->degrade_mask |= DEGRADE_MASK_SM ;

                    ilog ("%s sm degrade\n", node_ptr->hostname.c_str());
                }

                /* Manage de-asserting degrade due to Software Management */
                if ((!(node_ptr->mtce_flags & MTC_FLAG__SM_DEGRADED)) &&
                    (node_ptr->degrade_mask & DEGRADE_MASK_SM))
                {
                    /* clear the SM degrade flag */
                    node_ptr->degrade_mask &= ~DEGRADE_MASK_SM ;
                    ilog ("%s sm degrade clear\n", node_ptr->hostname.c_str());
                }

                /* In-service luks volume config failure handling */
                if ( !(node_ptr->mtce_flags & MTC_FLAG__LUKS_VOL_FAILED))
                {
                    alarm_luks_clear ( node_ptr );
                }
                else
                {
                    alarm_luks_failure ( node_ptr );
                }

                /*
                 * In-service Config Failure/Alarm handling
                 */

                /* Detect new config failure condition */
                if ( node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY)
                {
                    /* not healthy .... */
                    if ( THIS_HOST )
                    {
                        /* initial config is complete and last manifest apply failed ... */
                        if (( daemon_is_file_present ( CONFIG_COMPLETE_FILE )) &&
                            ( daemon_is_file_present ( CONFIG_FAIL_FILE )))
                        {
                            wlog_throttled ( node_ptr->health_threshold_counter, (MTC_UNHEALTHY_THRESHOLD*10), "%s is UNHEALTHY\n", node_ptr->hostname.c_str());
                            if ( node_ptr->health_threshold_counter >= MTC_UNHEALTHY_THRESHOLD )
                                alarm_config_failure ( node_ptr );
                        }
                    }
                    else
                    {
                        if ( ++node_ptr->health_threshold_counter >= MTC_UNHEALTHY_THRESHOLD )
                        {
                            elog ( "%s is UNHEALTHY failed ; forcing re-enabled\n",
                                    node_ptr->hostname.c_str());

                            force_full_enable ( node_ptr ) ;
                        }
                        else
                        {
                            wlog ( "%s is UNHEALTHY (cnt:%d)\n",
                                       node_ptr->hostname.c_str(),
                                       node_ptr->health_threshold_counter );
                        }
                    }
                }
                /* or correct an alarmed config failure that has cleared */
                else if ( node_ptr->degrade_mask & DEGRADE_MASK_CONFIG )
                {
                    if ( node_ptr->mtce_flags & MTC_FLAG__I_AM_HEALTHY )
                        alarm_config_clear ( node_ptr );
                }
                else
                {
                    node_ptr->health_threshold_counter = 0 ;
                }
            }

            if ( node_ptr->vim_notified == false )
            {
                if ( node_ptr->operState == MTC_OPER_STATE__ENABLED )
                {
                    /* Inform the VIM that this host is enabled */
                    mtcVimApi_state_change ( node_ptr, VIM_HOST_ENABLED, 3 );
                }
                else
                {
                    if ( node_ptr->availStatus == MTC_AVAIL_STATUS__FAILED )
                    {
                        mtcVimApi_state_change ( node_ptr, VIM_HOST_FAILED, 3 );
                    }
                    else
                    {
                        mtcVimApi_state_change ( node_ptr, VIM_HOST_DISABLED, 3 );
                    }
                }
                node_ptr->vim_notified = true ;
            }

            node_ptr->insv_test_count++ ;
            insvTestStageChange ( node_ptr, MTC_INSV_TEST__START );

            break ;
        }
        default:
        {
            node_ptr->insv_test_count++ ;
            insvTestStageChange ( node_ptr, MTC_INSV_TEST__START );
            break ;
        }
    }
    return (PASS);
}

/************************************************************
 * Manage host degrade state based on degrade mask          *
 * The availability state of degrade only applies when the  *
 * host is unlocked-enabled.                                *
 ***********************************************************/
int nodeLinkClass::degrade_handler ( struct nodeLinkClass::node * node_ptr )
{
    if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
        ( node_ptr->operState == MTC_OPER_STATE__ENABLED ))
    {
        if (( node_ptr->degrade_mask == DEGRADE_MASK_NONE ) &&
            ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED ))
        {
            availStatusChange ( node_ptr, MTC_AVAIL_STATUS__AVAILABLE );
        }

        else if (( node_ptr->degrade_mask ) &&
                 ( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ))
        {
            availStatusChange ( node_ptr, MTC_AVAIL_STATUS__DEGRADED );
        }
    }
    return (PASS);
}

int nodeLinkClass::cfg_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;
    switch (node_ptr->configStage )
    {
        case MTC_CONFIG__START:
        {
            ilog ("%s Starting a %s:%s shadow entry change check\n",
                      node_ptr->hostname.c_str(),
                      SHADOW_FILE,
                      USERNAME_ROOT );

            /* Post the show command with a catch-all timeout timer */
            rc = mtcInvApi_cfg_show ( node_ptr->hostname ) ;
            if ( rc )
            {
                elog ("%s Config SHOW command failed\n", node_ptr->hostname.c_str() );
                configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
            }
            else
            {
                mtcTimer_start ( node_ptr->mtcConfig_timer, mtcTimer_handler, (sysinv_timeout+1) );
                configStageChange ( node_ptr, MTC_CONFIG__SHOW );
            }
            break ;
        }
        case MTC_CONFIG__SHOW:
        {
            /* timeout yet ? */
            if ( node_ptr->mtcConfig_timer.ring == true )
            {
                elog ("%s timeout\n", node_ptr->cfgEvent.log_prefix.c_str());
                configStageChange ( node_ptr, MTC_CONFIG__TIMEOUT );
                break ;
            }

            /* done Yet ? */
            rc = doneQueue_dequeue ( node_ptr->cfgEvent ) ;
            if ( rc == RETRY )
            {
                /* Still waiting */
                break ;
            }
            else if ( rc == PASS )
            {
                string temp_value = "" ;
                mtcTimer_stop ( node_ptr->mtcConfig_timer );
                node_ptr->cfgEvent.value = "" ;
                node_ptr->cfgEvent.uuid  = "" ;
                if (( rc = jsonUtil_get_array_idx ( (char*)node_ptr->cfgEvent.response.data(), "iusers", 0 , temp_value )) == PASS )
                {
                    jsonUtil_get_key_val ( (char*)temp_value.data(), "root_sig", node_ptr->cfgEvent.value);
                }

                if (  node_ptr->cfgEvent.value.empty() ||
                     !node_ptr->cfgEvent.value.compare("null") || rc )
                {
                    elog ("%s null or missing 'root_sig' value (%d:%s)\n",
                              node_ptr->cfgEvent.service.c_str(), rc,
                              node_ptr->cfgEvent.value.empty() ? "empty" : node_ptr->cfgEvent.value.c_str());

                    node_ptr->cfgEvent.status = FAIL_INVALID_DATA ;
                    configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
                    break;
                }

                ilog ("%s root_sig:%s\n", node_ptr->cfgEvent.log_prefix.c_str(),
                                          node_ptr->cfgEvent.value.c_str());

                dlog ("Database Signature: %s\n", node_ptr->cfgEvent.value.c_str());

                /*
                 * generate a md5 signature for this user's Shadow entry.
                 * We will do so for the entire entry as either the password
                 * or the password age may change and we need to track and notify
                 * for both.
                 */
                char cfgInfo[1024] = {0}; 
                node_ptr->cfgEvent.key = get_shadow_signature ( (char*)SHADOW_FILE , (char*)USERNAME_ROOT,
                                                                &cfgInfo[0], sizeof(cfgInfo));
                node_ptr->cfgEvent.information = cfgInfo;

                if ( node_ptr->cfgEvent.key.empty() )
                {
                    elog ("failed to get md5sum of username '%s' from  '%s'\n", USERNAME_ROOT, SHADOW_FILE );
                    node_ptr->cfgEvent.status = FAIL_INVALID_DATA ;
                    configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
                    break ;
                }

                dlog ("File Signature : %s\n", node_ptr->cfgEvent.key.c_str());
                if ( node_ptr->cfgEvent.key.compare(node_ptr->cfgEvent.value))
                {
                    bool install = false ;
                    if ( node_ptr->configAction == MTC_CONFIG_ACTION__INSTALL_PASSWD )
                    {
                        install = true ;
                        ilog ("%s shadow file hash and aging ... install config\n", USERNAME_ROOT );
                    }
                    else
                    {
                        ilog ("%s shadow entry has changed ... updating config\n", USERNAME_ROOT );
                        ilog ("... old signature - %s\n", node_ptr->cfgEvent.value.c_str());
                        ilog ("... new signature - %s\n", node_ptr->cfgEvent.key.c_str());
                    }

                    if ((rc = jsonUtil_get_array_idx ( (char*)node_ptr->cfgEvent.response.data(), "iusers", 0 , temp_value )) == PASS )
                    {
                        jsonUtil_get_key_val ( (char*)temp_value.data(), "uuid", node_ptr->cfgEvent.uuid);
                    }

                    if ( rc || node_ptr->cfgEvent.uuid.empty() || !node_ptr->cfgEvent.uuid.compare("null"))
                    {
                        elog ("%s null or missing reconfig 'uuid' (%d:%s)\n",
                                  node_ptr->cfgEvent.service.c_str(), rc,
                                  node_ptr->cfgEvent.uuid.empty() ? "empty" : node_ptr->cfgEvent.uuid.c_str());
                        return ( FAIL_INVALID_DATA );
                    }
                    ilog ("%s uuid:%s\n",
                              node_ptr->cfgEvent.log_prefix.c_str(),
                              node_ptr->cfgEvent.uuid.c_str());

                    /* Post the modify command */
                    rc = mtcInvApi_cfg_modify ( node_ptr->hostname, install ) ;
                    if ( rc )
                    {
                        elog ("%s Config MODIFY command failed\n", node_ptr->hostname.c_str() );
                        configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
                    }
                    else
                    {
                        mtcTimer_start ( node_ptr->mtcConfig_timer, mtcTimer_handler, (sysinv_timeout+1) );
                        configStageChange ( node_ptr, MTC_CONFIG__MODIFY );
                    }
                }
                else
                {
                    ilog ("%s shadow entry has not changed (%s)\n",
                              USERNAME_ROOT, node_ptr->cfgEvent.key.c_str());
                    configStageChange ( node_ptr, MTC_CONFIG__DONE );
                }
            }
            else
            {
                elog ("%s failed (%d:%d)\n", node_ptr->cfgEvent.log_prefix.c_str(), rc,
                                             node_ptr->cfgEvent.status );
                configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
            }
            break ;
        }

        case MTC_CONFIG__MODIFY:
        {
            /* timeout yet ? */
            if ( node_ptr->mtcConfig_timer.ring == true )
            {
                elog ("%s timeout\n", node_ptr->cfgEvent.log_prefix.c_str());
                configStageChange ( node_ptr, MTC_CONFIG__TIMEOUT );
                break ;
            }

            /* done Yet ? */
            rc = doneQueue_dequeue ( node_ptr->cfgEvent ) ;
            if ( rc == RETRY )
            {
                /* Still waiting */
                break ;
            }
            else if ( rc == PASS )
            {
                mtcTimer_stop ( node_ptr->mtcConfig_timer );
                if ( node_ptr->cfgEvent.response_len )
                {
                    configStageChange ( node_ptr, MTC_CONFIG__VERIFY );
                }
                else
                {
                    elog ("%s modify without response (%d:%d)\n",
                              node_ptr->cfgEvent.log_prefix.c_str(), rc,
                              node_ptr->cfgEvent.status );
                    configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
                }
            }
            else
            {
                elog ("%s modify failed (%d:%d)\n",
                          node_ptr->cfgEvent.log_prefix.c_str(), rc,
                          node_ptr->cfgEvent.status );
                configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
            }
            break ;
        }
        case MTC_CONFIG__VERIFY:
        {
            node_ptr->cfgEvent.value = "" ;
            rc = jsonUtil_get_key_val ( (char*)node_ptr->cfgEvent.response.data(),
                                             "root_sig", node_ptr->cfgEvent.value);
            if (  node_ptr->cfgEvent.value.empty() ||
                 !node_ptr->cfgEvent.value.compare("null") || rc )
            {
                elog ("%s null or missing 'root_sig' value (%d:%s)\n",
                          node_ptr->cfgEvent.service.c_str(), rc,
                          node_ptr->cfgEvent.value.empty() ? "empty" : node_ptr->cfgEvent.value.c_str());

                node_ptr->cfgEvent.status = FAIL_INVALID_DATA ;
                configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
                break;
            }

            if ( node_ptr->cfgEvent.key.compare(node_ptr->cfgEvent.value))
            {
                elog ("%s root_sig modify compare failed\n",
                          node_ptr->cfgEvent.log_prefix.c_str());
                wlog ("... database signature - %s\n", node_ptr->cfgEvent.value.c_str());
                wlog ("... file     signature - %s\n", node_ptr->cfgEvent.key.c_str());

                configStageChange ( node_ptr, MTC_CONFIG__FAILURE );
            }
            else
            {
                ilog ("%s modify succeeded\n", node_ptr->cfgEvent.log_prefix.c_str());
                configStageChange ( node_ptr, MTC_CONFIG__DONE );
            }
            break ;
        }
        case MTC_CONFIG__FAILURE:
        {
            elog ("%s Command Failure\n", node_ptr->cfgEvent.log_prefix.c_str());

            /* Call to remove this command from the work queue ; if it exists */
            workQueue_del_cmd ( node_ptr, node_ptr->cfgEvent.sequence );

            configStageChange ( node_ptr, MTC_CONFIG__DONE );
            break ;
        }
        case MTC_CONFIG__TIMEOUT:
        {
            elog ("%s Command Timeout\n", node_ptr->cfgEvent.log_prefix.c_str());

            /* Call to remove this command from the work queue ; if it exists */
            workQueue_del_cmd ( node_ptr, node_ptr->cfgEvent.sequence );

            node_ptr->oper_failures++ ;
            mtcHttpUtil_free_conn ( node_ptr->cfgEvent );
            mtcHttpUtil_free_base ( node_ptr->cfgEvent );

            configStageChange ( node_ptr, MTC_CONFIG__DONE );
            break ;
        }
        case MTC_CONFIG__DONE:
        default:
        {
            if (( node_ptr->configAction == MTC_CONFIG_ACTION__INSTALL_PASSWD ) ||
                ( node_ptr->configAction == MTC_CONFIG_ACTION__CHANGE_PASSWD ))
            {
                /* We are done */
                node_ptr->configAction = MTC_CONFIG_ACTION__NONE ;
            }
            if ( node_ptr->configAction == MTC_CONFIG_ACTION__CHANGE_PASSWD_AGAIN )
            {
                /* Run the FSM again */
                node_ptr->configAction = MTC_CONFIG_ACTION__CHANGE_PASSWD ;
            }
            node_ptr->configStage = MTC_CONFIG__START ;
            break ;
        }
    }
    return (PASS);
}

/*****************************************************************************
 * Name       : self_fail_handler
 *
 * Purpose    : Handle force failure of self for Fully DX enabled or SX systems
 *
 * Description: Wait for mtcTimer  to expire giving the the active controller
 *              time to flush any outstanding state change updates to the
 *              database. Then trigger a force shutdown of SM services.
 *
 * Simplex System behavior: issue a lazy reboot
 * Duplex System behavior : wait for swact to the enabled standby controller.
 *
 * Assumptions: Only called in a DX system if the standby controller is enabled.
 *              Do a last second check for the enabled standby controller.
 *              Otherwise, abort and revert back to enabled-degraded.
 *
 * Parameters :
 * @param node_ptr - pointed toi this host's nodeLinkClass control structure
 *
 *****************************************************************************/
int nodeLinkClass::self_fail_handler ( struct nodeLinkClass::node * node_ptr )
{
    /* Wait for this Simplex node to lazy reboot */
    if (this->self_reboot_wait)
    {
        ilog_throttled ( node_ptr->ar_log_throttle, AR_HANDLER_LOG_THROTTLE_THRESHOLD,
                         "%s ... waiting on lazy reboot", node_ptr->hostname.c_str());
        return (PASS);
    }
    /* Wait for SM to shut down the mtcAgent */
    else if (this->force_swact_wait)
    {
        ilog_throttled ( node_ptr->ar_log_throttle, AR_HANDLER_LOG_THROTTLE_THRESHOLD,
                         "%s ... waiting on force swact", node_ptr->hostname.c_str());
        return (PASS);
    }
    
    /* Wait for the database update */
    else if ( node_ptr->mtcTimer.ring )
    {
        // Last second check for an active standby controller in a DX system
        if (( NOT_SIMPLEX ) && ( is_inactive_controller_main_insv () == false ))
        {
            // ERIK: TEST ME: Force this test case
            wlog ("%s refusing to self reboot with no enabled standby controller", node_ptr->hostname.c_str());
            wlog ("%s ... critical enable alarm raised", node_ptr->hostname.c_str());
            wlog ("%s ... recommend enabling a standby controller.", node_ptr->hostname.c_str());
            allStateChange ( node_ptr,
                             node_ptr->adminState,
                             MTC_OPER_STATE__ENABLED,
                             MTC_AVAIL_STATUS__DEGRADED );
            alarm_enabled_failure ( node_ptr, true );
            mtcInvApi_update_task ( node_ptr, MTC_TASK_FAILED_NO_BACKUP);
            this->delayed_swact_required = false ;
        }
        else
        {
            /* Force an uncontrolled SWACT to enabled standby controller */
            /* Tell SM we are unhealthy so that it shuts down all its services */
            wlog ("%s forcing SM to shut down services by %s", node_ptr->hostname.c_str(), SMGMT_UNHEALTHY_FILE);
            daemon_log ( SMGMT_UNHEALTHY_FILE, "Maintenance force swact due to self failure");
            node_ptr->ar_log_throttle = 0 ;
            if ( SIMPLEX )
            {
                wlog ("%s commanding lazy reboot", node_ptr->hostname.c_str());

                send_mtc_cmd ( node_ptr->hostname, MTC_CMD_LAZY_REBOOT, MGMNT_INTERFACE) ;

                /* pxeboot network is not currently provisioned in SX
                 * auto handle if that changes in the future */
                if ( this->pxeboot_network_provisioned == true )
                    send_mtc_cmd ( node_ptr->hostname, MTC_CMD_LAZY_REBOOT, PXEBOOT_INTERFACE) ;

                this->self_reboot_wait = true ;
            }
            else
            {
                this->force_swact_wait = true ;
            }
        }
    }
    else
    {
        ilog_throttled (node_ptr->ar_log_throttle, AR_HANDLER_LOG_THROTTLE_THRESHOLD,
                        "%s ... waiting on database update before %s",
                         node_ptr->hostname.c_str(),
                        SIMPLEX ? "lazy reboot of this simplex system" :
                                  "force swact to unlocked-enabled standby controller");
    }
    return (PASS);
}
