/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
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
#include "mtcAlarm.h"     /* for ... mtcAlarm_<severity>     */
#include "nodeTimers.h"   /* for ... mtcTimer_start/stop     */

#include "jsonUtil.h"     /* for ... jsonApi_array_value     */
#include "tokenUtil.h"
#include "regexUtil.h"    /* for ... regexUtil_pattern_match */

#include "nodeClass.h"    /* All base stuff                  */
#include "ipmiUtil.h"     /* for ... power and reset support */

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
 * ***********************************************************/
int nodeLinkClass::calc_reset_prog_timeout ( struct nodeLinkClass::node * node_ptr,
                                                                    int   retries )
{
    /* for the management interface */
    int to = MTC_RESET_PROG_OFFLINE_TIMEOUT ;

    /* and add on for the bmc interface if its provisioned */
    if ( node_ptr->bm_provisioned == true )
        to += MTC_RESET_PROG_OFFLINE_TIMEOUT ;

    /* add a small buffer */
    to += (MTC_ENABLED_TIMER*4) ;

    /* factor in the number of retries */
    to *= (retries+1) ;

    ilog ("%s Reboot/Reset progression has %d sec 'wait for offline' timeout\n",
              node_ptr->hostname.c_str(), to );
    ilog ("%s ... sources - mgmnt:Yes  infra:%s  bmc:%s\n",
              node_ptr->hostname.c_str(),
              infra_network_provisioned ? "Yes" : "No",
              node_ptr->bm_provisioned ? "Yes" : "No" );
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
        mtcTimer_stop_int_safe ( node_ptr->ipmitool_thread_ctrl.timer );
        node_ptr->ipmitool_thread_ctrl.timer.ring = true ;
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
            if ( SIMPLEX_CPE_SYSTEM )
                aio = true ;
            else
                aio = false ;

            mtcInvApi_update_states_now ( node_ptr, "unlocked", "disabled" , "offline", "disabled", "offline" );
            mtcInvApi_update_task_now   ( node_ptr, aio ? MTC_TASK_CPE_SX_UNLOCK_MSG : MTC_TASK_SELF_UNLOCK_MSG );

            wlog ("%s unlocking %s with reboot\n",
                      my_hostname.c_str(),
                      aio ? "Simplex System" : "Active Controller" );

            /* should not return */
            return ( lazy_graceful_fs_reboot ( node_ptr ));
        }
    }

    switch ( (int)node_ptr->handlerStage.enable )
    {
        case MTC_ENABLE__FAILURE:
        {
            /**************************************************************
             * Failure of thr active controller has special handling.
             *
             * Condition 1: While there is no in-service backup controller
             *              to swact to. In this case the ctive controller
             *              - is only degraded to avoid a system outage.
             *              - the CPE subfunction is failed
             *              - compute SubFunction Alarm is raised
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
            alarm_enabled_failure ( node_ptr );

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
                    wlog ("%s ... requesting swact to in-service inactive controller\n", node_ptr->hostname.c_str());

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
                    this->autorecovery_enabled = true ;

                    /* use thresholded auto recovery for simplext failure case */
                    manage_autorecovery ( node_ptr );

                    if ( this->autorecovery_disabled == false )
                    {
                        wlog ("%s has critical failure.\n", node_ptr->hostname.c_str());
                        wlog ("%s ... downgrading to degrade with auto recovery disabled\n", node_ptr->hostname.c_str());
                        wlog ("%s ... to avoid disabling only enabled controller\n", node_ptr->hostname.c_str());
                        this->autorecovery_disabled = true ;
                    }

                    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
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

            if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                node_ptr->inservice_failed_subf = true ;
                subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                            MTC_AVAIL_STATUS__FAILED );
            }

            if ( degrade_only == true )
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

            /* if we get here in controller simplex mode then go degraded
             * if we are not already degraded. Otherwise, fail. */
            if ( THIS_HOST && ( is_inactive_controller_main_insv() == false ))
            {
                /* autorecovery must be disabled */
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
            else
            {
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE_WAIT );
            }

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
            manage_dor_recovery ( node_ptr, FM_ALARM_SEVERITY_CLEAR );

            plog ("%s Main Enable FSM (from start)%s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->was_dor_recovery_mode ? " (from DOR)" : "" );

            /* clear all the past enable failure bools */
            clear_main_failed_bools ( node_ptr );
            clear_subf_failed_bools ( node_ptr );
            clear_hostservices_ctls ( node_ptr );

            /* Clear all degrade flags except for the HWMON one */
            clear_host_degrade_causes ( node_ptr->degrade_mask );
            node_ptr->degraded_resources_list.clear();

            /* Purge this hosts work and done queues */
            workQueue_purge    ( node_ptr );
            doneQueue_purge    ( node_ptr );
            mtcCmd_workQ_purge ( node_ptr );
            mtcCmd_doneQ_purge ( node_ptr );

            /* Assert the mtc alive gate */
            node_ptr->mtcAlive_gate = true ;

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

                   /* enable auto recovery if the inactive controller
                    * is out of service */
                   if (( is_controller (node_ptr) ) && ( NOT_THIS_HOST ))
                       this->autorecovery_enabled = true ;

                    /* fall through */

                case MTC_AVAIL_STATUS__DEGRADED:
                case MTC_AVAIL_STATUS__AVAILABLE:
                {
                    if (( is_active_controller ( node_ptr->hostname )) &&
                        ( is_inactive_controller_main_insv() == false ))
                    {
                        wlog ("%s recovering active controller from %s-%s-%s\n",
                                  node_ptr->hostname.c_str(),
                                  get_adminState_str(node_ptr->adminState).c_str(),
                                  get_operState_str(node_ptr->operState).c_str(),
                                  get_availStatus_str(node_ptr->availStatus).c_str());

                        mtcInvApi_update_task ( node_ptr, "" );

                        /* Special case */
                        // alarm_enabled_clear ( node_ptr, false );

                        //mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__CONFIG );
                        //node_ptr->alarms[MTC_ALARM_ID__CONFIG] = FM_ALARM_SEVERITY_CLEAR ;

                        //allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                        //                           MTC_OPER_STATE__ENABLED,
                        //                           MTC_AVAIL_STATUS__DEGRADED );

                        // adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );

                        // return (PASS);
                    }
                    else
                    {
                        alarm_enabled_failure ( node_ptr );

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
                 * to wait for one more. Assum,e offline, not online and open
                 * the mtcAlive gate. */
                node_ptr->mtcAlive_gate = false ;
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
            node_ptr->cmd.parm1 = 0    ; /* retries */
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

            clear_service_readies ( node_ptr );

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
               node_ptr->mtcAlive_gate = false ;

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
            break ;
        }
        case MTC_ENABLE__MTCALIVE_WAIT:
        {
            /* search for the mtc alive message */
            if ( node_ptr->mtcAlive_online == true )
            {
                mtcTimer_reset ( node_ptr->mtcTimer );

                /* Check to see if the host is/got configured correctly */
                if ( (node_ptr->mtce_flags & MTC_FLAG__I_AM_CONFIGURED) == 0 )
                {
                    elog ("%s configuration incomplete or failed (oob:%x:%x)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->mtce_flags,
                              MTC_FLAG__I_AM_CONFIGURED);

                    /* raise an alarm for the failure of the config */
                    alarm_config_failure ( node_ptr );
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_MAIN_CONFIG_FAIL );
                    enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
                }
                else
                {
                    plog ("%s is MTCALIVE (uptime:%d secs)\n",
                              node_ptr->hostname.c_str(), node_ptr->uptime );
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
                    /* Set the node mtcAlive timer to configured value.
                     * This will revert bact to normal timeout after any first
                     * unlock value that may be in effect. */
                    LOAD_NODETYPE_TIMERS ;

                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_ONLINE );
                    node_ptr->offline_log_reported = false ;
                    node_ptr->online_log_reported  = true ;

                    /* Request Out-Of--Service test execution */
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
                alarm_enabled_failure ( node_ptr );

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
            else if ( node_ptr->mtcAlive_gate == true )
            {
                slog ("%s mtcAlive gate unexpectedly set, correcting ...\n",
                        node_ptr->hostname.c_str());

                 node_ptr->mtcAlive_gate = false ;
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

            /* start waiting fhr the ENABLE READY message */
            enableStageChange ( node_ptr, MTC_ENABLE__GOENABLED_WAIT );

            break ;
        }
        case MTC_ENABLE__GOENABLED_WAIT:
        {
            /* The healthy code comes from the host in the mtcAlive message.
             * This 'if' clause was introduced to detected failure of host
             * without having to wait for the GOENABLED phase to timeout.
             *
             * This case is particularly important in the DOR case where
             * computes may have come up and fail to run their manifests
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
                mtcInvApi_update_task ( node_ptr, MTC_TASK_INTEST_FAIL );
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
            }
            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled == true )
            {
                mtcTimer_reset ( node_ptr->mtcTimer );
                plog ("%s got GOENABLED\n", node_ptr->hostname.c_str());
                // plog ("%s main configured OK\n", node_ptr->hostname.c_str());

                /* O.K. clearing the state now that we got it */
                node_ptr->goEnabled = false ;

                mtcInvApi_update_task ( node_ptr, MTC_TASK_INITIALIZING );

                /* ok. great, got the go-enabled message, lets move on */
                enableStageChange ( node_ptr, MTC_ENABLE__HOST_SERVICES_START );
            }
            else if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                elog ("%s has GOENABLED Timeout\n", node_ptr->hostname.c_str());
                ilog ("%s ... the out-of-service tests took too long to complete\n",
                          node_ptr->hostname.c_str());

                mtcInvApi_update_task ( node_ptr, MTC_TASK_INTEST_FAIL_TO_ );
                node_ptr->mtcTimer.ring = false ;

                /* raise an alarm for the enable failure */
                alarm_enabled_failure ( node_ptr );

                /* go back and issue reboot again */
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );

                /* no longer In-Test ; we are 'Failed' again" */
                availStatusChange ( node_ptr, MTC_AVAIL_STATUS__FAILED  );
            }
            else
            {
                ; /* wait some more */
            }
            break ;
        }

        case  MTC_ENABLE__HOST_SERVICES_START:
        {
            bool start = true ;

            plog ("%s Starting Host Services\n", node_ptr->hostname.c_str());
            if ( this->launch_host_services_cmd ( node_ptr, start ) != PASS )
            {
                node_ptr->hostservices_failed = true ;

                elog ("%s %s failed ; launch\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.name.c_str());

                mtcInvApi_update_task ( node_ptr, MTC_TASK_START_SERVICE_FAIL );
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
            }
            else
            {
                mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLING );

                /* Only run hardware monitor if board management is provisioned */
                if ( node_ptr->bm_provisioned == true )
                {
                    send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );
                }

                enableStageChange ( node_ptr, MTC_ENABLE__HOST_SERVICES_WAIT );
            }
            break ;
        }

        case MTC_ENABLE__HOST_SERVICES_WAIT:
        {
            /* Wait for host services to complete - pass or fail.
             * The host_services_handler manages timeout. */
            rc = this->host_services_handler ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait for the mtcClient's response ... */
                break ;
            }
            else if ( rc != PASS )
            {
                node_ptr->hostservices_failed = true ;
                /* distinguish 'timeout' from other 'execution' failures */
                if ( rc == FAIL_TIMEOUT )
                {
                    elog ("%s %s failed ; timeout\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str());

                    mtcInvApi_update_task ( node_ptr,
                                            MTC_TASK_START_SERVICE_TO );
                }
                else
                {
                    elog ("%s %s failed ; rc:%d\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str(),
                              rc);

                    mtcInvApi_update_task ( node_ptr,
                                            MTC_TASK_START_SERVICE_FAIL );
                }
                enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
            }
            else /* success path */
            {
                /* Don't start the self heartbeat for the active controller.
                 * Also, in AIO , hosts that have a controller function also
                 * have a compute function and the heartbeat for those hosts
                 * are started at the end of the subfunction handler. */
                if (( THIS_HOST ) ||
                   (( CPE_SYSTEM ) && ( is_controller(node_ptr)) ))
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

            plog ("%s Starting %d sec Heartbeat Soak (with%s)\n",
                      node_ptr->hostname.c_str(),
                      MTC_HEARTBEAT_SOAK_BEFORE_ENABLE,
                      node_ptr->hbsClient_ready ? " ready event" : "out ready event"  );

            /* Start Monitoring Services - heartbeat, process and hardware */
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_START_HOST );

            /* allow heartbeat to run for 10 seconds before we declare enable */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_HEARTBEAT_SOAK_BEFORE_ENABLE );
            enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_SOAK );

            break ;
        }
        case MTC_ENABLE__HEARTBEAT_SOAK:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                plog ("%s heartbeating\n", node_ptr->hostname.c_str() );
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
                 * this is a CPE and this is the only controller */
                if ( CPE_SYSTEM && ( num_controllers_enabled() > 0 ))
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

            if (( CPE_SYSTEM ) && ( is_controller(node_ptr)))
            {
                ilog ("%s running compute sub-function enable handler\n", node_ptr->hostname.c_str());
                mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLING_SUBF );
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__ENABLE_SUBF );
            }
            else
            {

                node_ptr->enabled_count++ ;

                /* Inform the VIM that this host is enabled */
                mtcVimApi_state_change ( node_ptr, VIM_HOST_ENABLED, 3 );

                plog ("%s is ENABLED%s\n", node_ptr->hostname.c_str(),
                          node_ptr->was_dor_recovery_mode ? " (from DOR)" : "");
                node_ptr->dor_recovery_mode = false ;
                node_ptr->was_dor_recovery_mode = false ;
                node_ptr->http_retries_cur = 0 ;

                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );

                node_ptr->health_threshold_counter = 0 ;
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
           /* Purge this hosts work queues */
            mtcCmd_workQ_purge ( node_ptr );
            mtcCmd_doneQ_purge ( node_ptr );
            node_ptr->mtcAlive_gate = false ;
            node_ptr->http_retries_cur = 0 ;
            node_ptr->unknown_health_reported = false ;

            plog ("%s %sGraceful Recovery (uptime was %d)\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->mnfa_graceful_recovery ? "MNFA " : "",
                      node_ptr->uptime );

            /* Cancel any outstanding timers */
            mtcTimer_reset ( node_ptr->mtcTimer );

            /* clear all the past enable failure bools */
            clear_main_failed_bools ( node_ptr );
            clear_subf_failed_bools ( node_ptr );
            clear_hostservices_ctls ( node_ptr );

            /* Disable the heartbeat service for Graceful Recovery */
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_STOP_HOST );

            /* Clear the minor and failure flags if it is set for this host */
            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                hbs_minor_clear ( node_ptr, (iface_enum)iface );
                node_ptr->heartbeat_failed[iface] = false ;
            }

            /* Have we reached the maximum allowed fast recovery attempts.
             *
             * If we have then force the full enable by
             *   1. clearing the recovery action
             *   2. Setting the node operational state to Disabled
             *   3. Setting the Enable action
             */
            if ( ++node_ptr->graceful_recovery_counter > MTC_MAX_FAST_ENABLES )
            {
                 /* gate off further mtcAlive messaging timme the offline
                 * handler runs. This prevents stale messages from making it
                 * in and prolong the offline detection time */
                 node_ptr->mtcAlive_gate = true ;

                elog ("%s Graceful Recovery Failed (retries=%d)\n",
                          node_ptr->hostname.c_str(), node_ptr->graceful_recovery_counter );

                /* This forces exit from the recover handler and entry into the
                 * enable_handler via FAILED availability state and no aciton. */
                nodeLinkClass::force_full_enable ( node_ptr );

                break ;
            }
            else
            {
                /* TODO: Consider taking this log out as writing to the database
                 *       during a fast graceful recovery might no be the best idea */
                if ( node_ptr->graceful_recovery_counter > 1 )
                    mtcInvApi_update_task ( node_ptr, "Graceful Recovery Retry" );
                else
                    mtcInvApi_update_task ( node_ptr, "Graceful Recovery");

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
                manage_dor_recovery ( node_ptr, FM_ALARM_SEVERITY_CLEAR );

                mtcTimer_stop ( node_ptr->mtcTimer );

                ilog ("%s got requested mtcAlive%s\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->was_dor_recovery_mode ? " (DOR)" : "" );

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
                    if ( node_ptr->uptime > MTC_MINS_10 )
                    {
                        /* did not reboot case */
                        wlog ("%s Connectivity Recovered ; host did not reset\n", node_ptr->hostname.c_str());
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
                        wlog ("%s Connectivity Recovered ; host has reset\n", node_ptr->hostname.c_str());
                        ilog ("%s ... continuing with MNFA graceful recovery\n", node_ptr->hostname.c_str());
                        ilog ("%s ... without additional reboot %s\n",
                                  node_ptr->hostname.c_str(), node_ptr->bm_ip.empty() ? "or reset" : "" );

                        /* now officially in the In-Test state */
                        availStatusChange ( node_ptr, MTC_AVAIL_STATUS__INTEST  );

                        /* O.K. Clear the alive */
                        node_ptr->mtcAlive_online = false ;

                        /* Go to the goEnabled stage */
                        recoveryStageChange ( node_ptr, MTC_RECOVERY__GOENABLED_TIMER );

                        alarm_enabled_failure(node_ptr);
                        break ;
                    }
                }
                else if (( node_ptr->uptime_save ) && ( node_ptr->uptime >= node_ptr->uptime_save ))
                {
                    /* did not reboot case */
                    wlog ("%s Connectivity Recovered ; host did not reset%s\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->was_dor_recovery_mode ? " (DOR)" : "" );

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
                    ilog ("%s ... continuing%sgraceful recovery ; (OOB: %08x)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->was_dor_recovery_mode ? " (DOR) " : " ",
                              node_ptr->mtce_flags);
                    ilog ("%s ... without additional reboot %s (uptime:%d)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->bm_ip.empty() ? "or reset" : "",
                              node_ptr->uptime );

                    /* now officially in the In-Test state */
                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__INTEST  );

                    /* Go to the goEnabled stage */
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__GOENABLED_TIMER );

                    alarm_enabled_failure (node_ptr);
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
                          node_ptr->dor_recovery_mode ? " (DOR)" : "" );
                wlog ("%s ... stopping host services\n", node_ptr->hostname.c_str());
                wlog ("%s ... continuing with graceful recovery\n", node_ptr->hostname.c_str());

                /* clear all mtc flags. Will be updated on the next/first
                 * mtcAlive message upon recovery */
                node_ptr->mtce_flags = 0 ;

                /* Set node as unlocked-disabled-failed */
                allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                           MTC_OPER_STATE__DISABLED,
                                           MTC_AVAIL_STATUS__FAILED );

                if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                {
                    subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                               MTC_AVAIL_STATUS__FAILED );
                }

                /* Inform the VIM that this host has failed */
                mtcVimApi_state_change ( node_ptr, VIM_HOST_FAILED, 3 );

                alarm_enabled_failure(node_ptr);

                /* Clear all degrade flags except for the HWMON one */
                clear_host_degrade_causes ( node_ptr->degrade_mask );
                node_ptr->degraded_resources_list.clear();

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

            /* Set the FSM task state to booting */
            node_ptr->uptime = 0 ;
            mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_WAIT );

            start_offline_handler ( node_ptr );

            timeout = node_ptr->mtcalive_timeout ;

            /* Only try and issue in-line recovery reboot or reset if
             * NOT in Dead Office Recovery (DOR) mode. */
            if ( node_ptr->dor_recovery_mode == false )
            {
                /* If the infrastructure network is provisioned then try
                 * and issue a reset over it to expedite the recovery
                 * for the case where the management heartbeat has
                 * failed but the infra has not.
                 * Keeping it simple by just issing the command and not looping on it */
                if (( node_ptr->infra_ip.length () > 5 ) &&
                    ( node_ptr->heartbeat_failed[MGMNT_IFACE] == true ) &&
                    ( node_ptr->heartbeat_failed[INFRA_IFACE] == false ))
                {
                    ilog ("%s issuing one time graceful recovery reboot over infra network\n", node_ptr->hostname.c_str());
                    send_mtc_cmd ( node_ptr->hostname, MTC_CMD_REBOOT, INFRA_INTERFACE ) ;
                }

                if ((node_ptr->bm_provisioned) && (node_ptr->bm_accessible))
                {
                    ilog ("%s issuing one time board management graceful recovery reset\n", node_ptr->hostname.c_str());

                    rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_RESET );
                    if ( rc )
                    {
                        wlog ("%s board management reset failed\n", node_ptr->hostname.c_str());
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
                timeout = node_ptr->mtcalive_timeout + daemon_get_cfg_ptr()->dor_recovery_timeout_ext ;
            }

            /* start the timer that waits for MTCALIVE */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, timeout );

            plog ("%s %s (%d secs)%s(uptime was %d) \n",
                      node_ptr->hostname.c_str(),
                      MTC_TASK_RECOVERY_WAIT,
                      timeout,
                      node_ptr->dor_recovery_mode ? " (DOR) " : " " ,
                      node_ptr->uptime_save );

            clear_service_readies ( node_ptr );

            recoveryStageChange ( node_ptr, MTC_RECOVERY__MTCALIVE_WAIT );
            break ;
        }

        case MTC_RECOVERY__RESET_RECV_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                    rc = ipmi_command_recv ( node_ptr );
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

                manage_dor_recovery ( node_ptr, FM_ALARM_SEVERITY_CLEAR );

                /* If the host's uptime is bigger than the saved uptime then
                 * the host has not reset yet we have disabled services
                 * then now we need to reset the host to prevet VM duplication
                 * by forcing a full enable */
                if (( node_ptr->uptime_save != 0 ) &&
                    ( node_ptr->uptime >= node_ptr->uptime_save ))
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
                                  node_ptr->dor_recovery_mode ? "(DOR)" : " ");
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
                manage_dor_recovery ( node_ptr, FM_ALARM_SEVERITY_CLEAR );

                /* Set the FSM task state to init failed */
                mtcInvApi_update_task ( node_ptr, "Graceful Recovery Failed" );

                node_ptr->mtcTimer.ring = false ;

                elog ("%s has MTCALIVE Timeout\n", node_ptr->hostname.c_str());

                nodeLinkClass::force_full_enable ( node_ptr );

                break ;
            }
            else if (( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) &&
                     ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                     ( node_ptr->bm_provisioned == true ) &&
                     ( node_ptr->bm_accessible == true ) &&
                     ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__INIT ) &&
                     ( thread_idle ( node_ptr->ipmitool_thread_ctrl )) &&
                     ( node_ptr->ipmitool_thread_info.command != IPMITOOL_THREAD_CMD__POWER_ON ))
            {
                ilog ("%s powering on unlocked powered off host\n",  node_ptr->hostname.c_str());
                if ( ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_ON ) != PASS )
                {
                    node_ptr->ipmitool_thread_ctrl.done = true ;
                    thread_kill ( node_ptr->ipmitool_thread_ctrl , node_ptr->ipmitool_thread_info ) ;
                }
            }
            else if (( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) &&
                     ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                     ( node_ptr->bm_provisioned == true ) &&
                     ( node_ptr->bm_accessible == true ) &&
                     ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__INIT ) &&
                     ( thread_done ( node_ptr->ipmitool_thread_ctrl )) &&
                     ( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_ON ))
            {
                if ( ipmi_command_recv ( node_ptr ) == PASS )
                {
                    ilog ("%s powered on\n",  node_ptr->hostname.c_str());
                    availStatusChange ( node_ptr, MTC_AVAIL_STATUS__OFFLINE );
                }
            }
            else if ( node_ptr->mtcAlive_gate == true )
            {
                slog ("%s mtcAlive gate unexpectedly set, auto-correcting ...\n",
                        node_ptr->hostname.c_str());

                 node_ptr->mtcAlive_gate = false ;
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
             * computes may have come up and fail to run their manifests
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
                mtcInvApi_update_task ( node_ptr, MTC_TASK_INTEST_FAIL );
                this->force_full_enable ( node_ptr );
            }

            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled == true )
            {
                plog ("%s got GOENABLED (Graceful Recovery)\n", node_ptr->hostname.c_str());
                mtcTimer_reset ( node_ptr->mtcTimer );

                /* O.K. clearing the state now that we got it */
                node_ptr->goEnabled = false ;

                recoveryStageChange ( node_ptr, MTC_RECOVERY__HOST_SERVICES_START );
            }
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s has GOENABLED Timeout\n", node_ptr->hostname.c_str());

                node_ptr->mtcTimer.ring = false ;

                this->force_full_enable ( node_ptr );
            }
            break;
        }

        case MTC_RECOVERY__HOST_SERVICES_START:
        {
            bool start = true ;

            plog ("%s Starting Host Services\n", node_ptr->hostname.c_str());
            if ( this->launch_host_services_cmd ( node_ptr, start ) != PASS )
            {
                elog ("%s %s failed ; launch\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.name.c_str());
                node_ptr->hostservices_failed = true ;
                this->force_full_enable ( node_ptr );
            }
            else
            {
                recoveryStageChange ( node_ptr, MTC_RECOVERY__HOST_SERVICES_WAIT );
            }
            break ;
        }
        case MTC_RECOVERY__HOST_SERVICES_WAIT:
        {
            /* Wait for host services to complete - pass or fail.
             * The host_services_handler manages timeout. */
            rc = this->host_services_handler ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait for the mtcClient's response ... */
                break ;
            }
            else if ( rc != PASS )
            {
                node_ptr->hostservices_failed = true ;
                if ( rc == FAIL_TIMEOUT )
                {
                    elog ("%s %s failed ; timeout\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str());

                    mtcInvApi_update_task ( node_ptr,
                                            MTC_TASK_START_SERVICE_TO );
                }
                else
                {
                    elog ("%s %s failed ; rc=%d\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str(),
                              rc);

                    mtcInvApi_update_task ( node_ptr,
                                            MTC_TASK_START_SERVICE_FAIL );
                }
                this->force_full_enable ( node_ptr );
            }
            else /* success path */
            {
                /* The active controller would never get/be here but
                 * if it did then just fall through to change state. */
                if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                {
                    /* Here we need to run the sub-fnction goenable and start
                     * host services if this is the other controller in a AIO
                     * system. */
                    if ( NOT_THIS_HOST )
                    {
                        /* start a timer that waits for the /var/run/.compute_config_complete flag */
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_COMPUTE_CONFIG_TIMEOUT );

                        /* We will come back to MTC_RECOVERY__HEARTBEAT_START
                         * after we enable the compute subfunction */
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
            break ;
        }
        case MTC_RECOVERY__CONFIG_COMPLETE_WAIT:
        {
            /* look for file */
            if ( node_ptr->mtce_flags & MTC_FLAG__SUBF_CONFIGURED )
            {
                plog ("%s-compute configured\n", node_ptr->hostname.c_str());

                mtcTimer_reset ( node_ptr->mtcTimer );

                recoveryStageChange ( node_ptr, MTC_RECOVERY__SUBF_GOENABLED_TIMER );
            }

            /* timeout handling */
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s-compute configuration timeout\n", node_ptr->hostname.c_str());

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
            ilog ("%s-compute running out-of-service tests\n", node_ptr->hostname.c_str());

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
                elog ("%s-compute one or more out-of-service tests failed\n", node_ptr->hostname.c_str());
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_FAIL );
                this->force_full_enable ( node_ptr );
            }

            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled_subf == true )
            {
                /* stop the timer */
                mtcTimer_reset ( node_ptr->mtcTimer );

                plog ("%s-compute passed  out-of-service tests\n", node_ptr->hostname.c_str());

                /* O.K. clearing the state now that we got it */
                node_ptr->goEnabled_subf        = false ;

                /* ok. great, got the go-enabled message, lets move on */
                recoveryStageChange ( node_ptr, MTC_RECOVERY__SUBF_SERVICES_START );
            }
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s-compute out-of-service test execution timeout\n", node_ptr->hostname.c_str());
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

        case MTC_RECOVERY__SUBF_SERVICES_START:
        {
            bool start = true ;
            bool subf  = true ;

            plog ("%s-compute Starting Host Services\n", node_ptr->hostname.c_str());

            if ( this->launch_host_services_cmd ( node_ptr, start, subf ) != PASS )
            {
                elog ("%s-compute %s failed ; launch\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.name.c_str());
                node_ptr->hostservices_failed_subf = true ;
                mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_FAIL );
                this->force_full_enable ( node_ptr );
            }
            else
            {
                recoveryStageChange ( node_ptr, MTC_RECOVERY__SUBF_SERVICES_WAIT );
            }
            break ;
        }
        case MTC_RECOVERY__SUBF_SERVICES_WAIT:
        {
            /* Wait for host services to complete - pass or fail.
             * The host_services_handler manages timeout. */
            rc = this->host_services_handler ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait for the mtcClient's response ... */
                break ;
            }
            else if ( rc != PASS )
            {
                node_ptr->hostservices_failed_subf = true ;
                if ( rc == FAIL_TIMEOUT )
                {
                    elog ("%s-compute %s failed ; timeout\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str());

                    mtcInvApi_update_task ( node_ptr,
                                            MTC_TASK_START_SERVICE_TO );
                }
                else
                {
                    elog ("%s-compute %s failed ; rc=%d\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.name.c_str(),
                              rc);

                    mtcInvApi_update_task ( node_ptr,
                                            MTC_TASK_START_SERVICE_FAIL );
                }
                this->force_full_enable ( node_ptr );
            }
            else /* success path */
            {
                /* allow the fsm to wait for up to 1 minute for the
                 * hbsClient's ready event before starting heartbeat
                 * test. */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_1 );
                recoveryStageChange ( node_ptr, MTC_RECOVERY__HEARTBEAT_START );
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

            plog ("%s Starting %d sec Heartbeat Soak (with%s)\n",
                      node_ptr->hostname.c_str(),
                      MTC_HEARTBEAT_SOAK_BEFORE_ENABLE,
                      node_ptr->hbsClient_ready ? " ready event" : "out ready event"  );

            /* Enable the heartbeat service for Graceful Recovery */
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_START_HOST );

            /* allow heartbeat to run for 10 seconds before we declare enable */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_HEARTBEAT_SOAK_BEFORE_ENABLE );

            /* if heartbeat is not working then we will
             * never get here and enable the host */
            recoveryStageChange ( node_ptr, MTC_RECOVERY__HEARTBEAT_SOAK );

            break ;
        }
        case MTC_RECOVERY__HEARTBEAT_SOAK:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                /* if heartbeat is not working then we will
                 * never get here and enable the host */
                recoveryStageChange ( node_ptr, MTC_RECOVERY__STATE_CHANGE );
            }
            break ;
        }
        case MTC_RECOVERY__STATE_CHANGE:
        {
            if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
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

            /* Only run hardware monitor board management is provisioned */
            if ( node_ptr->bm_provisioned == true )
            {
                send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );
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
                recoveryStageChange ( node_ptr, MTC_RECOVERY__ENABLE_START ) ;
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
        case MTC_RECOVERY__ENABLE_START:
        {
            /* Create the recovery enable timer. This timer is short.
             * A node need to stay enabled with the hartbeat service
             * running for a period of time before declaring it enabled */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_HEARTBEAT_SOAK_BEFORE_ENABLE );

            recoveryStageChange ( node_ptr, MTC_RECOVERY__ENABLE_WAIT ) ;
            break;
        }
        case MTC_RECOVERY__ENABLE_WAIT:
        {
            /* When this timer fires the host has been up for enough time */
            if ( node_ptr->mtcTimer.ring == true )
            {
                if ( is_controller(node_ptr) )
                {
                    if ( mtcSmgrApi_request ( node_ptr,
                                              CONTROLLER_ENABLED,
                                              SMGR_MAX_RETRIES ) != PASS )
                    {
                        wlog ("%s Failed to send 'unlocked-disabled' to HA Service Manager ; allowing enable\n",
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
                if ( node_ptr->was_dor_recovery_mode )
                {
                    report_dor_recovery (  node_ptr , "is ENABLED" );
                }
                else
                {
                    plog ("%s is ENABLED (Gracefully Recovered)\n",
                              node_ptr->hostname.c_str());
                }
                alarm_enabled_clear ( node_ptr, false );
            }
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

    switch ( (int)node_ptr->handlerStage.disable )
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
            clear_hostservices_ctls ( node_ptr );

            disableStageChange ( node_ptr, MTC_DISABLE__DIS_SERVICES_WAIT) ;

            stop_offline_handler ( node_ptr );

            if (( node_ptr->bm_provisioned == true ) &&
                ( node_ptr->bm_accessible == true ) &&
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ))
            {
                    rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_ON );
                    if ( rc )
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                        disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_POWERON_SEND) ;
                    }
                    else
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY );
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

                if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
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
            node_ptr->degraded_resources_list.clear();

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
            if ( node_ptr->handlerStage.disable == MTC_DISABLE__DIS_SERVICES_WAIT )
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
                rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_ON );
                if ( rc )
                {
                    elog ("%s failed to send Power On request\n", node_ptr->hostname.c_str());
                    disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_FORCE_LOCK) ;
                }
                else
                {
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY );
                    disableStageChange ( node_ptr, MTC_DISABLE__HANDLE_POWERON_RECV) ;
                }
            }
            break ;
        }
        case MTC_DISABLE__HANDLE_POWERON_RECV:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ))
            {
                rc = ipmi_command_recv ( node_ptr );
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
            /* If this is a force lock against a compute then we have to reset it */
            if (( node_ptr->adminAction == MTC_ADMIN_ACTION__FORCE_LOCK ))
            {
                /* Stop the timer if it is active coming into this case */
                mtcTimer_reset ( node_ptr->mtcTimer );

                /* purge in support of retries */
                mtcCmd_doneQ_purge ( node_ptr );
                mtcCmd_workQ_purge ( node_ptr );

                ilog ("%s Issuing Force-Lock Reset\n", node_ptr->hostname.c_str());
                mtcCmd_init ( node_ptr->cmd );
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
            if ( infra_network_provisioned )
            {
                send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, INFRA_INTERFACE );
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
            node_ptr->mtcAlive_gate = false ;

            disableStageChange( node_ptr, MTC_DISABLE__START );
            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );

            node_ptr->mtcCmd_work_fifo.clear();
            node_ptr->mtcCmd_done_fifo.clear();
            node_ptr->http_retries_cur = 0 ;

            /***** Powercycle FSM Stuff *****/

            recovery_ctrl_init ( node_ptr->hwmon_reset );
            recovery_ctrl_init ( node_ptr->hwmon_powercycle );

            /* Load configured mtcAlive and goEnabled timers */
            LOAD_NODETYPE_TIMERS ;

            mtcInvApi_force_task ( node_ptr, "" );

            plog ("%s Disable Complete\n", node_ptr->hostname.c_str());

            break ;
        }

        default:
        {
            elog ("%s Bad Case (%d)\n", node_ptr->hostname.c_str(),
                                        node_ptr->handlerStage.disable );
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
            node_ptr->mtcAlive_mgmnt = false ;
            node_ptr->mtcAlive_infra = false ;
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
        }
        case MTC_OFFLINE__SEND_MTCALIVE:
        {
            alog2 ("%s searching for offline (%s-%s)\n",
                      node_ptr->hostname.c_str(),
                      operState_enum_to_str(node_ptr->operState).c_str(),
                      availStatus_enum_to_str(node_ptr->availStatus).c_str());

            node_ptr->mtcAlive_gate  = false ;
            node_ptr->mtcAlive_mgmnt = false ;
            node_ptr->mtcAlive_infra = false ;

            /* Request a mtcAlive from host from Mgmnt and Infra (if provisioned) */
            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, MGMNT_INTERFACE );
            if ( infra_network_provisioned )
            {
                send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, INFRA_INTERFACE );
            }

            /* reload the timer */
            mtcTimer_start_msec ( node_ptr->offline_timer, mtcTimer_handler, offline_period );

            node_ptr->offlineStage = MTC_OFFLINE__WAIT ;

            break ;
        }
        case MTC_OFFLINE__WAIT:
        {
            /* be sure the mtcAlive gate is open */
            node_ptr->mtcAlive_gate = false ;
            if ( mtcTimer_expired ( node_ptr->offline_timer ) == true )
            {
                if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
                {
                    plog ("%s offline (external)\n", node_ptr->hostname.c_str());
                    node_ptr->offlineStage = MTC_OFFLINE__IDLE ;
                }
                else if ( !node_ptr->mtcAlive_mgmnt && !node_ptr->mtcAlive_infra )
                {
                    if ( ++node_ptr->offline_search_count > offline_threshold )
                    {
                        node_ptr->mtcAlive_online = false ;

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
                else
                {
                    node_ptr->mtcAlive_online = true ;
                    if ( node_ptr->mtcAlive_mgmnt || node_ptr->mtcAlive_infra )
                    {
                        ilog_throttled ( node_ptr->offline_log_throttle, 10,
                                         "%s still seeing mtcAlive (%c:%c)\n",
                                         node_ptr->hostname.c_str(),
                                         node_ptr->mtcAlive_mgmnt ? 'Y' : 'n',
                                         node_ptr->mtcAlive_infra ? 'Y' : 'n');
                    }
                    else
                    {
                        alog ("%s still seeing mtcAlive (%c:%c)\n",
                                  node_ptr->hostname.c_str(),
                                  node_ptr->mtcAlive_mgmnt ? 'Y' : 'n',
                                  node_ptr->mtcAlive_infra ? 'Y' : 'n');
                    }
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
     * for the following availability states */
    if (( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED  )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFDUTY   )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__INTEST    )   ||
        ( node_ptr->availStatus == MTC_AVAIL_STATUS__NOT_INSTALLED ))
    {
        return (PASS);
    }

    switch ( (int)node_ptr->onlineStage )
    {
        case MTC_ONLINE__START:
        {
            alog3 ("%s Offline Handler (%d)\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->onlineStage );

            if ( node_ptr->mtcAlive_gate == true )
            {
                alog ("%s mtcAlive gate unexpectedly set, correcting ...\n",
                        node_ptr->hostname.c_str());

                node_ptr->mtcAlive_gate = false ;
            }

            /* Start with a zero count. This counter is incremented every
             * time we get a mtc alive message from that host */
            node_ptr->mtcAlive_online = false ;
            node_ptr->mtcAlive_misses = 0 ;

            /* Start mtcAlive message timer */
            mtcTimer_start ( node_ptr->mtcAlive_timer, mtcTimer_handler, online_period );
            node_ptr->onlineStage = MTC_ONLINE__WAITING ;
            break ;
        }
        case MTC_ONLINE__RETRYING:
        {
            /* Start mtcAlive message timer */
            mtcTimer_start ( node_ptr->mtcAlive_timer, mtcTimer_handler, online_period );
            node_ptr->onlineStage = MTC_ONLINE__WAITING ;
            break ;
        }
        case MTC_ONLINE__WAITING:
        {
            if ( node_ptr->mtcAlive_timer.ring == false )
                break ;

            alog ("%s mtcAlive [%s]  [ misses:%d]\n",
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
                        if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
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
                    node_ptr->mtcAlive_timer.ring = false ;
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
                        if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
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
                // send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, INFRA_INTERFACE );
            }

            /* Start over */
            node_ptr->mtcAlive_timer.ring = false ;
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
#define SWACT_RECV_MSEC_DELAY  (50)
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

            /* Post a user message 'Swact: Request' and
             * then delay to allow it to be displayed */
            mtcInvApi_force_task ( node_ptr, MTC_TASK_SWACT_REQUEST );
            mtcTimer_start ( node_ptr->mtcSwact_timer, mtcTimer_handler, (MTC_TASK_UPDATE_DELAY/2) );
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
                    mtcTimer_start_msec ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_MSEC_DELAY );
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
                        mtcTimer_start_msec ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_MSEC_DELAY );
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
                mtcTimer_start_msec ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_MSEC_DELAY );
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
                        mtcTimer_start_msec ( node_ptr->mtcSwact_timer, mtcTimer_handler, SWACT_RECV_MSEC_DELAY );
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
        }
        case MTC_RESET__REQ_SEND:
        {
            node_ptr->power_action_retries--;

            /* Handle loss of connectivity over retries  */
            if ( node_ptr->bm_provisioned == false )
            {
                elog ("%s BMC not provisioned\n", node_ptr->hostname.c_str() );
                mtcInvApi_force_task ( node_ptr, MTC_TASK_BMC_NOT_PROV );
                resetStageChange ( node_ptr , MTC_RESET__FAIL );
                break ;
            }

            if ( node_ptr->bm_accessible == false )
            {
                wlog ("%s Power Off request rejected ; BMC not accessible ; retry in %d seconds \n",
                          node_ptr->hostname.c_str(),
                          MTC_POWER_ACTION_RETRY_DELAY);

                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                resetStageChange ( node_ptr , MTC_RESET__QUEUE );
                break ;
            }

            else
            {
                    rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_RESET );

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
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
            }
            break ;
        }

        case MTC_RESET__RESP_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                    rc = ipmi_command_recv ( node_ptr );
                    if ( rc == RETRY )
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                        break ;
                    }

                if ( rc )
                {
                    elog ("%s Reset command failed (rc:%d)\n", node_ptr->hostname.c_str(), rc );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
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
                if ( node_ptr->power_action_retries > 0 )
                {
                    char buffer[64] ;
                    int attempts = MTC_RESET_ACTION_RETRY_COUNT - node_ptr->power_action_retries ;
                    snprintf ( buffer, 64, MTC_TASK_RESET_QUEUE, attempts, MTC_RESET_ACTION_RETRY_COUNT);
                    mtcInvApi_update_task ( node_ptr, buffer);

                    /* check the thread error status if thetre is one */
                    if ( node_ptr->ipmitool_thread_info.status )
                    {
                        wlog ("%s ... %s (rc:%d)\n", node_ptr->hostname.c_str(),
                                                     node_ptr->ipmitool_thread_info.status_string.c_str(),
                                                     node_ptr->ipmitool_thread_info.status );
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

/* Reinstall handler
 * --------------
 * Manage reinstall operations for a locked-disabled host */
int nodeLinkClass::reinstall_handler ( struct nodeLinkClass::node * node_ptr )
{
    switch ( node_ptr->reinstallStage )
    {
        case MTC_REINSTALL__START:
        {
            int host_reinstall_wait_timer = node_ptr->mtcalive_timeout + node_reinstall_timeout ;
            node_ptr->retries = host_reinstall_wait_timer / MTC_REINSTALL_WAIT_TIMER ;

            start_offline_handler ( node_ptr );

            node_ptr->cmdReq = MTC_CMD_WIPEDISK ;

            plog ("%s Administrative Reinstall Requested\n", node_ptr->hostname.c_str());
            if ( send_mtc_cmd ( node_ptr->hostname, MTC_CMD_WIPEDISK, MGMNT_INTERFACE ) != PASS )
            {
                elog ("Failed to send 'reinstall' request to %s\n", node_ptr->hostname.c_str());
                reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
            }
            else
            {
                node_ptr->cmdRsp = MTC_CMD_NONE ;

                if ( node_ptr->mtcTimer.tid )
                {
                    mtcTimer_stop ( node_ptr->mtcTimer );
                }

                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_CMD_RSP_TIMEOUT );

                ilog ("%s waiting for REINSTALL ACK \n", node_ptr->hostname.c_str() );

                reinstallStageChange ( node_ptr , MTC_REINSTALL__RESP_WAIT );
            }
            break ;
        }
        case MTC_REINSTALL__RESP_WAIT:
        {
            if ( node_ptr->cmdRsp != MTC_CMD_WIPEDISK )
            {
                if ( node_ptr->mtcTimer.ring == true )
                {
                    elog ("%s REINSTALL ACK Timeout\n",
                        node_ptr->hostname.c_str());

                    reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
                }
            }
            else
            {
                /* declare successful reinstall request */
                plog ("%s REINSTALL Request Succeeded\n", node_ptr->hostname.c_str());

                mtcTimer_stop ( node_ptr->mtcTimer );

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

                ilog ("%s Reinstall Progress: host is offline ; waiting for host to come back\n", node_ptr->hostname.c_str());

                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_REINSTALL_WAIT_TIMER );
                reinstallStageChange ( node_ptr , MTC_REINSTALL__ONLINE_WAIT );
            }
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s offline timeout - reinstall failed\n", node_ptr->hostname.c_str());
                reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
            }
            break ;
        }
        case MTC_REINSTALL__ONLINE_WAIT:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                if ( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE )
                {
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_SUCCESS);
                    mtcTimer_stop ( node_ptr->mtcTimer );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
                    reinstallStageChange ( node_ptr , MTC_REINSTALL__MSG_DISPLAY );
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_REINSTALL_COMPLETE );
                }
                else
                {
                    if ( --node_ptr->retries < 0 )
                    {
                        elog ("%s online timeout - reinstall failed\n", node_ptr->hostname.c_str());
                        reinstallStageChange ( node_ptr , MTC_REINSTALL__FAIL );
                    }
                    else
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_REINSTALL_WAIT_TIMER );
                    }
                }
            }
            break;
        }
        case MTC_REINSTALL__FAIL:
        {
            mtcInvApi_update_task ( node_ptr, MTC_TASK_REINSTALL_FAIL);
            mtcTimer_stop ( node_ptr->mtcTimer );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
            reinstallStageChange ( node_ptr , MTC_REINSTALL__MSG_DISPLAY );
            mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_REINSTALL_FAILED );
            break ;
        }
        case MTC_REINSTALL__MSG_DISPLAY:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                node_ptr->mtcTimer.ring = false ;
                reinstallStageChange ( node_ptr , MTC_REINSTALL__DONE );
            }
            break ;
        }
        case MTC_REINSTALL__DONE:
        default:
        {
            plog ("%s Reinstall Completed\n",  node_ptr->hostname.c_str());

            /* Default timeout values */
            LOAD_NODETYPE_TIMERS ;

            mtcTimer_stop ( node_ptr->mtcTimer );

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
            else
            {
                ; // send_hwmon_command ( node_ptr->hostname, MTC_CMD_STOP_HOST );
            }

            node_ptr->power_action_retries = MTC_POWER_ACTION_RETRY_COUNT ;
            //the fall through to MTC_POWEROFF__REQ_SEND is intentional
        }
        case MTC_POWEROFF__REQ_SEND:
        {
            node_ptr->power_action_retries--;

            /* Handle loss of connectivity over retries  */
            if ( node_ptr->bm_provisioned == false )
            {
                elog ("%s BMC not provisioned\n", node_ptr->hostname.c_str());
                mtcInvApi_force_task ( node_ptr, MTC_TASK_BMC_NOT_PROV );
                powerStageChange ( node_ptr , MTC_POWEROFF__FAIL );
                break ;
            }

            if ( node_ptr->bm_accessible == false )
            {
                wlog ("%s Power Off request rejected ; BMC not accessible ; retry in %d seconds\n",
                          node_ptr->hostname.c_str(),
                          MTC_POWER_ACTION_RETRY_DELAY);

                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                break ;
            }

            else
            {
                    rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_OFF );
                if ( rc )
                {
                    wlog ("%s Power-Off request failed (%d)\n", node_ptr->hostname.c_str(), rc );
                    powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                }
                else
                {
                    blog ("%s Power-Off requested\n", node_ptr->hostname.c_str());
                    powerStageChange ( node_ptr , MTC_POWEROFF__RESP_WAIT );
                }
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
            }
            break ;
        }

        case MTC_POWEROFF__RESP_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                    rc = ipmi_command_recv ( node_ptr );
                    if ( rc == RETRY )
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                        break ;
                    }

                if ( rc )
                {
                    elog ("%s Power-Off command failed\n", node_ptr->hostname.c_str());
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                    powerStageChange ( node_ptr , MTC_POWEROFF__QUEUE );
                }
                else
                {
                    ilog ("%s is Powering Off\n", node_ptr->hostname.c_str() );
                    mtcInvApi_update_task ( node_ptr, "Powering Off" );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
                    powerStageChange ( node_ptr , MTC_POWEROFF__DONE );
                    node_ptr->power_on = false ;
                }
            }
            break ;
        }
        case MTC_POWEROFF__QUEUE:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                node_ptr->mtcTimer.ring = false ;
                if ( node_ptr->power_action_retries > 0 )
                {
                    char buffer[255] ;
                    int attempts = MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries ;
                    snprintf ( buffer, 255, MTC_TASK_POWEROFF_QUEUE, attempts, MTC_POWER_ACTION_RETRY_COUNT);
                    mtcInvApi_update_task ( node_ptr, buffer);

                    /* check the thread error status if thetre is one */
                    if ( node_ptr->ipmitool_thread_info.status )
                    {
                        wlog ("%s ... %s (rc:%d)\n", node_ptr->hostname.c_str(),
                                                     node_ptr->ipmitool_thread_info.status_string.c_str(),
                                                     node_ptr->ipmitool_thread_info.status );
                    }
                    powerStageChange ( node_ptr , MTC_POWEROFF__REQ_SEND );
                }
                else
                {
                    powerStageChange ( node_ptr , MTC_POWEROFF__FAIL );
                }
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
            plog ("%s Administrative 'Power-On' Action\n", node_ptr->hostname.c_str());
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
        }
        case MTC_POWERON__POWER_STATUS:
        {
            if ( node_ptr->bm_accessible == false )
            {
                wlog ("%s Power On request rejected ; BMC not accessible ; retry in %d seconds\n",
                          node_ptr->hostname.c_str(),
                          MTC_POWER_ACTION_RETRY_DELAY);

                node_ptr->power_action_retries-- ;
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
                break ;
            }

                rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_STATUS ) ;
                if ( rc )
                {
                    node_ptr->power_action_retries-- ;
                    powerStageChange ( node_ptr , MTC_POWERON__QUEUE );
                }
                else
                {
                    powerStageChange ( node_ptr , MTC_POWERON__POWER_STATUS_WAIT );
                }
                mtcTimer_reset ( node_ptr->mtcTimer );
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY );
                break ;
            }
        case MTC_POWERON__POWER_STATUS_WAIT:
        {
                if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
                {
                    rc = ipmi_command_recv ( node_ptr );
                    if ( rc == RETRY )
                    {
                        mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_RETRY_WAIT );
                    }
                    else if ( rc == PASS )
                    {
                        if ( node_ptr->ipmitool_thread_info.data.find (IPMITOOL_POWER_ON_STATUS) != std::string::npos )
                        {
                            ilog ("%s power is already on ; no action required\n", node_ptr->hostname.c_str());
                            node_ptr->power_on = true ;
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
                        powerStageChange ( node_ptr , MTC_POWERON__POWER_STATUS );
                    }
                }
            break ;
        }
        case MTC_POWERON__REQ_SEND:
        {
            node_ptr->power_action_retries--;

            /* Ensure that mtce is updated with the latest board
             * management ip address for this host */
            if ( node_ptr->bm_provisioned == false )
            {
                elog ("%s BMC not provisioned or accessible (%d:%d)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->bm_provisioned,
                          node_ptr->bm_accessible );

                powerStageChange ( node_ptr , MTC_POWERON__FAIL );
                break ;
            }

            if ( node_ptr->bm_accessible == false )
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
                    rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_ON );
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

                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY );

                    powerStageChange ( node_ptr , MTC_POWERON__RESP_WAIT );
                }
            }
            break ;
        }
        case MTC_POWERON__RESP_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                    rc = ipmi_command_recv ( node_ptr );
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
                    mtcInvApi_update_task ( node_ptr, "Powering On" );
                    mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_TASK_UPDATE_DELAY );
                    powerStageChange ( node_ptr , MTC_POWERON__DONE );
                    node_ptr->power_on = true ;
                }
            }
            break ;
        }
        case MTC_POWERON__QUEUE:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                node_ptr->mtcTimer.ring = false ;
                if ( node_ptr->power_action_retries > 0 )
                {
                    char buffer[64] ;
                    int attempts = MTC_POWER_ACTION_RETRY_COUNT - node_ptr->power_action_retries ;
                    snprintf ( buffer, 64, MTC_TASK_POWERON_QUEUE, attempts, MTC_POWER_ACTION_RETRY_COUNT);
                    mtcInvApi_update_task ( node_ptr, buffer);

                    /* check the thread error status if thetre is one */
                    if ( node_ptr->ipmitool_thread_info.status )
                    {
                        wlog ("%s ... %s (rc:%d)\n", node_ptr->hostname.c_str(),
                                                     node_ptr->ipmitool_thread_info.status_string.c_str(),
                                                     node_ptr->ipmitool_thread_info.status );
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

                // send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );

                powerStageChange ( node_ptr , MTC_POWER__DONE );
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

            mtcInvApi_force_task ( node_ptr, "" );
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

    if ( node_ptr->bm_accessible == false )
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

            /* Note: hwmon will continue to send powercycle requests to restart once it is accessible */

            /* TODO: RELEASE NOTE: Node may be left in the disabled state
             *  - need to track power state and raise logs or alarms if host is stuck in power off state.
             *  - The ipmitool update does add tracking of the power state but does not introduce the alarm */

            // send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );

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
                        alarm_enabled_failure ( node_ptr );

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

                        // send_hwmon_command    ( node_ptr->hostname, MTC_CMD_STOP_HOST);
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
                        int delay = MTC_IPMITOOL_REQUEST_DELAY ;
                        ilog ("%s querying current power state\n", node_ptr->hostname.c_str());

                            rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_STATUS );
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
                            rc = ipmi_command_recv ( node_ptr );
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
                            bool on = false ;

                                ilog ("%s Power Status: %s\n",
                                           node_ptr->hostname.c_str(),
                                           node_ptr->ipmitool_thread_info.data.c_str());

                                if ( node_ptr->ipmitool_thread_info.data.find ( IPMITOOL_POWER_ON_STATUS ) != std::string::npos )
                                {
                                    on = true ;
                                }
                            if ( rc == PASS )
                            {
                                /* maintain current power state */
                                node_ptr->power_on = on ;

                                if ( on == true )
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
                                /* TODO: use FAIL handler */
                                node_ptr->hwmon_powercycle.retries = MAX_POWERCYCLE_STAGE_RETRIES+1 ;
                                // powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
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
            int delay = MTC_IPMITOOL_REQUEST_DELAY ;

            /* Stop heartbeat if we are powering off the host */
            send_hbs_command  ( node_ptr->hostname, MTC_CMD_STOP_HOST );

            rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_OFF );
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
                rc = ipmi_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }

                if ( rc )
                {
                    elog ("%s power-off command failed (rc:%d:%d)\n",
                              node_ptr->hostname.c_str(),
                              rc , node_ptr->ipmitool_thread_info.status);

                    if ( node_ptr->ipmitool_thread_info.status )
                    {
                        wlog ("%s ... %s\n",
                                  node_ptr->hostname.c_str(),
                                  node_ptr->ipmitool_thread_info.status_string.c_str());
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
                int delay = MTC_IPMITOOL_REQUEST_DELAY ;
                clog ("%s %s stage\n", node_ptr->hostname.c_str(),
                      get_powercycleStages_str(node_ptr->powercycleStage).c_str());

                if ( node_ptr->bm_accessible == false )
                {
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer,
                                     mtcTimer_handler,
                                     MTC_POWERCYCLE_COOLDOWN_DELAY );

                    wlog ("%s not accessible ; waiting another %d seconds before power-on\n",
                              node_ptr->hostname.c_str(),
                              MTC_POWERCYCLE_COOLDOWN_DELAY );
                }
                rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_ON );
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
                rc = ipmi_command_recv ( node_ptr );
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
                              node_ptr->ipmitool_thread_info.data.c_str() );

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
                    rc = ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_STATUS );
                if ( rc )
                {
                    wlog ("%s Power-On command failed (rc:%d)\n", node_ptr->hostname.c_str(), rc );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__FAIL );
                }
                else
                {
                    wlog ("%s power status query requested\n", node_ptr->hostname.c_str() );
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY  );
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__POWERON_VERIFY_WAIT );
                }
            }
            break ;
        }
        case MTC_POWERCYCLE__POWERON_VERIFY_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->hwmon_powercycle.control_timer ) )
            {
                bool on = false ;

                rc = ipmi_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                    mtcTimer_start ( node_ptr->hwmon_powercycle.control_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                    break ;
                }
                if ( rc == PASS )
                {
                    if ( node_ptr->ipmitool_thread_info.data.find (IPMITOOL_POWER_ON_STATUS) != std::string::npos )
                    {
                        on = true ;
                    }
                }

                ilog ("%s power state query result: %s\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->ipmitool_thread_info.data.c_str() );

                if (( rc == PASS ) && ( on == true ))
                {
                    node_ptr->power_on = true ;
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
                    wlog ("%s Power-On failed or did not occur ; retrying (rc:%d:%d)\n", node_ptr->hostname.c_str(), rc, on );
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

            if ( node_ptr->bm_provisioned == true )
            {
                set_bm_prov ( node_ptr, false);
            }

            if ( node_ptr->ipmitool_thread_ctrl.stage != THREAD_STAGE__IDLE )
            {
                int delay = THREAD_POST_KILL_WAIT ;
                thread_kill ( node_ptr->ipmitool_thread_ctrl , node_ptr->ipmitool_thread_info ) ;

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
                if ( node_ptr->ipmitool_thread_ctrl.stage != THREAD_STAGE__IDLE )
                {
                    if ( node_ptr->retries++ < 3 )
                    {
                        wlog ("%s still waiting on active thread ; sending another kill signal (try %d or %d)\n",
                                  node_ptr->hostname.c_str(), node_ptr->retries, 3 );

                        thread_kill ( node_ptr->ipmitool_thread_ctrl, node_ptr->ipmitool_thread_info ) ;
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
        case MTC_ADD__START:
        case MTC_ADD__START_DELAY:
        {
            bool timer_set = false ;
            plog ("%s Host Add\n", node_ptr->hostname.c_str());

            /* Request a mtcAlive message ; gives us uptime ; don't trust what is in the database */
            node_ptr->uptime = 0 ;
            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_MTCALIVE, MGMNT_INTERFACE );

            ilog ("%s %s %s-%s-%s (%s)\n",
                node_ptr->hostname.c_str(),
                node_ptr->ip.c_str(),
                adminState_enum_to_str (node_ptr->adminState).c_str(),
                operState_enum_to_str  (node_ptr->operState).c_str(),
                availStatus_enum_to_str(node_ptr->availStatus).c_str(),

                node_ptr->uuid.length() ? node_ptr->uuid.c_str() : "" );

            if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                if ( daemon_is_file_present ( CONFIG_COMPLETE_COMPUTE ) == false )
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

            /* handle other cases */
            EFmAlarmSeverityT sev = mtcAlarm_state ( node_ptr->hostname,
                                                     MTC_ALARM_ID__ENABLE);

            if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
            {
                node_ptr->alarms[MTC_ALARM_ID__LOCK] = FM_ALARM_SEVERITY_WARNING ;

                /* If the node is locked then the Enable alarm
                 * should not be present */
                if ( sev != FM_ALARM_SEVERITY_CLEAR )
                {
                    mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
                    sev = FM_ALARM_SEVERITY_CLEAR ;
                }
            }

            /* Manage enable alarm over process restart.
             *
             * - clear the alarm in the active controller case
             * - maintain the alarm, set degrade state in MAJOR and CRIT cases
             * - clear alarm for all other severities.
             */
            if ( THIS_HOST )
            {
                if ( sev != FM_ALARM_SEVERITY_CLEAR )
                {
                    mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
                }
            }
            else
            {
                if (( sev == FM_ALARM_SEVERITY_CRITICAL ) ||
                    ( sev == FM_ALARM_SEVERITY_MAJOR ))
                {
                    node_ptr->alarms[MTC_ALARM_ID__ENABLE] = sev ;
                    node_ptr->degrade_mask |= DEGRADE_MASK_ENABLE ;
                }
                else if ( sev != FM_ALARM_SEVERITY_CLEAR )
                {
                    mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
                }
            }

            if ( is_controller(node_ptr) )
            {
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

                        /* Work Around for issue: */
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
            if ( is_controller(node_ptr) )
            {
                if ( node_ptr->mtcTimer.ring == true )
                {
                    if ( !node_ptr->task.empty () )
                    {
                        mtcInvApi_force_task ( node_ptr, "" );
                    }
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
            /* default retries counter to zero before START_SERVICES */
            node_ptr->retries = 0 ;
            node_ptr->addStage = MTC_ADD__START_SERVICES ;
            break ;
        }

        case MTC_ADD__START_SERVICES:
        {
            if (( node_ptr->adminState   == MTC_ADMIN_STATE__UNLOCKED ) &&
                ( node_ptr->operState    == MTC_OPER_STATE__ENABLED ) &&
                (( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
                 ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )))
            {
                ilog ("%s scheduling start host services\n",
                          node_ptr->hostname.c_str());

                node_ptr->start_services_needed  = true ;
                node_ptr->start_services_retries = 0    ;
            }

            node_ptr->addStage = MTC_ADD__MTC_SERVICES ;
            break ;
        }
        case MTC_ADD__MTC_SERVICES:
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

            send_hbs_command   ( node_ptr->hostname, MTC_CMD_ADD_HOST );

            /* Add this host to other maintenance services */
            if (( ! SIMPLEX_CPE_SYSTEM ) && ( node_ptr->bm_provisioned ))
            {
                send_hwmon_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );
            }
            if ( ( CPE_SYSTEM ) || ( is_compute (node_ptr) == true ))
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

            /* Only start it on this add operation if host is
             * already unlocked and enabled and not the active controller */
            if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                ( node_ptr->operState  == MTC_OPER_STATE__ENABLED ))
            {
                /* start the heartbeat service in all cases except for
                 * THIS host and CPE controller hosts */
                if ( NOT_THIS_HOST )
                {
                    if (( LARGE_SYSTEM ) ||
                        (( CPE_SYSTEM ) && ( this->dor_mode_active == false )))
                    {
                        send_hbs_command ( node_ptr->hostname, MTC_CMD_START_HOST );
                    }
                }
            }
            /* Only run hardware monitor if the bm ip is provisioned */
            if (( hostUtil_is_valid_bm_type  ( node_ptr->bm_type )) &&
                ( hostUtil_is_valid_ip_addr  ( node_ptr->bm_ip )))
            {
                set_bm_prov ( node_ptr, true ) ;
                send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );
            }

            node_ptr->mtcAlive_gate = false ;
            node_ptr->addStage = MTC_ADD__DONE ;
            break;
        }
        case MTC_ADD__DONE:
        default:
        {
            adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );

            /* Send sysinv the wrsroot password hash
             * and aging data as an install command */
            if ( SIMPLEX && THIS_HOST &&
                ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED ))
            {
                node_ptr->configStage  = MTC_CONFIG__START ;
                node_ptr->configAction = MTC_CONFIG_ACTION__INSTALL_PASSWD ;
            }

            if (( ! SIMPLEX_CPE_SYSTEM ) &&
                ( node_ptr->bm_provisioned == true ))
            {
                mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__BM );
                node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_CLEAR ;
            }

            /* Special Add handling for the AIO system */
            if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
            {
                if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
                    ( node_ptr->operState  == MTC_OPER_STATE__ENABLED ))
                {
                    /* In AIO if in DOR mode and the host is unlocked enabled
                     * we need to run the subfunction handler and request
                     * to start host services. */
                    if ( this->dor_mode_active )
                    {
                        node_ptr->start_services_needed_subf = true ;
                        adminActionChange ( node_ptr , MTC_ADMIN_ACTION__ENABLE_SUBF );
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

int nodeLinkClass::bm_handler ( struct nodeLinkClass::node * node_ptr )
{
    /* Call the bmc ssh connection monitor if this node's bm is provisioned */
    if ( node_ptr->bm_provisioned == true )
    {
                if (( node_ptr->bm_accessible == true ) && ( node_ptr->bm_ping_info.ok == false ))
                {
                    wlog ("%s bmc access lost\n", node_ptr->hostname.c_str());

                    /* remove the mc info file in case there is a firmware
                     * upgrade in progress. hwmond reads it and get
                     * the bmc fw version from it */
                    string mc_info_filename = IPMITOOL_OUTPUT_DIR ;
                    mc_info_filename.append(node_ptr->hostname);
                    mc_info_filename.append(IPMITOOL_MC_INFO_FILE_SUFFIX);
                    daemon_remove_file ( mc_info_filename.data() );

                    thread_kill ( node_ptr->ipmitool_thread_ctrl, node_ptr->ipmitool_thread_info );

                    bmc_access_data_init ( node_ptr );

                    ipmiUtil_mc_info_init ( node_ptr->mc_info );

                    node_ptr->bm_ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;

                    /* start a timer that will raise the BM Access alarm
                     * if we are not accessible by the time it expires */
                    plog ("%s bmc access timer started (%d secs)\n", node_ptr->hostname.c_str(), MTC_MINS_2);
                    mtcTimer_reset ( node_ptr->bmc_access_timer );
                    mtcTimer_start ( node_ptr->bmc_access_timer, mtcTimer_handler, MTC_MINS_2 );
                }

                /* This block queries and logs BMC Info and last Reset Cause */
                if (( node_ptr->bm_accessible == false ) &&
                    ( node_ptr->bm_ping_info.ok == true ) &&
                    (( node_ptr->mc_info_query_done == false ) ||
                     ( node_ptr->reset_cause_query_done == false ) ||
                     ( node_ptr->power_status_query_done == false )) &&
                     ( mtcTimer_expired (node_ptr->bm_timer ) == true ))
                {
                    int rc = PASS ;
                    if (( node_ptr->mc_info_query_active == false ) &&
                        ( node_ptr->mc_info_query_done   == false ))
                    {
                        if ( ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__MC_INFO ) != PASS )
                        {
                            elog ("%s %s send failed\n",
                                      node_ptr->hostname.c_str(),
                                      getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                        }
                        else
                        {
                            dlog ("%s %s\n", node_ptr->hostname.c_str(),
                                                            getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                            node_ptr->mc_info_query_active = true ;
                        }
                    }
                    else if (( node_ptr->mc_info_query_active == true ) &&
                             ( node_ptr->mc_info_query_done   == false))
                    {
                        if ( ( rc = ipmi_command_recv ( node_ptr ) ) == RETRY )
                        {
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                        }
                        else if ( rc != PASS )
                        {
                            /* this error is reported by the ipmi receive driver ...
                             * blog ("%s %s command failed\n", node_ptr->hostname.c_str(),
                             *                               getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                             */
                            node_ptr->mc_info_query_active = false ;
                            node_ptr->ipmitool_thread_ctrl.done = true ;
                            mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                        }
                        else
                        {
                            node_ptr->mc_info_query_active = false ;
                            node_ptr->mc_info_query_done = true ;
                            node_ptr->ipmitool_thread_ctrl.done = true ;
                            ipmiUtil_mc_info_load (  node_ptr->hostname, node_ptr->ipmitool_thread_info.data.data(), node_ptr->mc_info );
                        }
                    }
                    else if (( node_ptr->mc_info_query_active == false ) &&
                             ( node_ptr->mc_info_query_done   == true  ))
                    {
                        if (( node_ptr->reset_cause_query_active == false ) &&
                            ( node_ptr->reset_cause_query_done   == false ))
                        {
                            if ( ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__RESTART_CAUSE ) != PASS )
                            {
                                elog ("%s %s send failed\n", node_ptr->hostname.c_str(),
                                                             getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                                mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                            }
                            else
                            {
                                dlog ("%s %s\n", node_ptr->hostname.c_str(),
                                                                getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                                mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                                node_ptr->reset_cause_query_active = true ;
                            }
                        }
                        else if (( node_ptr->reset_cause_query_active == true ) &&
                                 ( node_ptr->reset_cause_query_done   == false ))
                        {
                            if ( ( rc = ipmi_command_recv ( node_ptr ) ) == RETRY )
                            {
                                mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                            }
                            else if ( rc != PASS )
                            {
                                elog ("%s %s command failed\n", node_ptr->hostname.c_str(),
                                                                getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                                node_ptr->reset_cause_query_active = false ;
                                mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                                node_ptr->ipmitool_thread_ctrl.done = true ;
                            }
                            else
                            {
                                node_ptr->reset_cause_query_active = false ;
                                node_ptr->reset_cause_query_done   = true ;
                                node_ptr->ipmitool_thread_ctrl.done = true ;
                                ilog ("%s %s\n", node_ptr->hostname.c_str(),
                                                 node_ptr->ipmitool_thread_info.data.c_str());
                            }
                            node_ptr->ipmitool_thread_ctrl.done = true ;
                        }
                        else if (( node_ptr->mc_info_query_done      == true ) &&
                                 ( node_ptr->reset_cause_query_done  == true ) &&
                                 ( node_ptr->power_status_query_done == false ))
                        {
                            if ( node_ptr->power_status_query_active == false )
                            {
                                if ( ipmi_command_send ( node_ptr, IPMITOOL_THREAD_CMD__POWER_STATUS ) != PASS )
                                {
                                    elog ("%s %s send failed\n", node_ptr->hostname.c_str(),
                                                                 getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                                }
                                else
                                {
                                    dlog ("%s %s\n", node_ptr->hostname.c_str(),
                                                     getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
                                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                                    node_ptr->power_status_query_active = true ;
                                }
                            }
                            else if ( node_ptr->power_status_query_done == false )
                            {
                                if ( ( rc = ipmi_command_recv ( node_ptr ) ) == RETRY )
                                {
                                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_RETRY_WAIT );
                                }
                                else if ( rc )
                                {
                                    node_ptr->power_status_query_active = false ;
                                    mtcTimer_start ( node_ptr->bm_timer, mtcTimer_handler, MTC_POWER_ACTION_RETRY_DELAY );
                                    node_ptr->ipmitool_thread_ctrl.done = true ;
                                }
                                else
                                {
                                    node_ptr->power_status_query_active = false ;
                                    node_ptr->power_status_query_done   = true  ;
                                    node_ptr->ipmitool_thread_ctrl.done = true  ;
                                    node_ptr->ipmitool_thread_info.command = 0  ;
                                node_ptr->bm_accessible = true ;
                                    node_ptr->bm_accessible = true ;
                                    mtcTimer_reset ( node_ptr->bmc_access_timer );

                                    ilog ("%s %s\n", node_ptr->hostname.c_str(),
                                                     node_ptr->ipmitool_thread_info.data.c_str());
                                    plog ("%s bmc is accessible\n", node_ptr->hostname.c_str());

                                    if ( node_ptr->ipmitool_thread_info.data.find (IPMITOOL_POWER_OFF_STATUS) != std::string::npos )
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
                                    }
                                }
                                node_ptr->ipmitool_thread_ctrl.done = true ;
                            }
                        }
                    }
                }
                if ( node_ptr->bm_ping_info.ok == false )
                {
                    /* Auto correct key ping information ; should ever occur but if it does ... */
                    if (( node_ptr->bm_ping_info.hostname.empty()) || ( node_ptr->bm_ping_info.ip.empty()))
                    {
                        /* if the bm ip is not yet learned then this log will flood */
                        //slog ("%s host ping info missing ; (%d:%d)\n",
                        //          node_ptr->hostname.c_str(),
                        //          node_ptr->bm_ping_info.hostname.empty(),
                        //          node_ptr->bm_ping_info.ip.empty());
                         node_ptr->bm_ping_info.hostname = node_ptr->hostname ;
                         node_ptr->bm_ping_info.ip       = node_ptr->bm_ip    ;
                    }
                }

                /* don't run the ping monitor if the ip address is invalid */
                if ( hostUtil_is_valid_ip_addr ( node_ptr->bm_ping_info.ip ) == true )
                {
                    pingUtil_acc_monitor ( node_ptr->bm_ping_info );
                }

                /* Manage the Board Management Access Alarm */
                if (( node_ptr->bm_accessible == false ) &&
                    ( mtcTimer_expired ( node_ptr->bmc_access_timer ) == true ))
                {
                    node_ptr->bm_ping_info.ok = false ;

                    node_ptr->bm_ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;

                    /* start a timer that will raise the BM Access alarm
                     * if we are not accessible by the time it expires */
                    plog ("%s bmc access timer started (%d secs)\n", node_ptr->hostname.c_str(), MTC_MINS_2);
                    mtcTimer_reset ( node_ptr->bmc_access_timer );
                    mtcTimer_start ( node_ptr->bmc_access_timer, mtcTimer_handler, MTC_MINS_2 );

                    if ( node_ptr->alarms[MTC_ALARM_ID__BM] == FM_ALARM_SEVERITY_CLEAR )
                    {
                        mtcAlarm_warning ( node_ptr->hostname, MTC_ALARM_ID__BM );
                        node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_WARNING ;
                    }
                }

                /* if BMs are accessible then see if we need to clear the Major BM Alarm. */
                else if (( node_ptr->alarms[MTC_ALARM_ID__BM] != FM_ALARM_SEVERITY_CLEAR ) &&
                         ( node_ptr->mc_info_query_done == true ) &&
                         ( node_ptr->reset_cause_query_done == true ) &&
                         ( node_ptr->power_status_query_done == true ))
                {
                    mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__BM );
                    node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_CLEAR ;
                }
    }
    else
    {
        if ( node_ptr->alarms[MTC_ALARM_ID__BM] != FM_ALARM_SEVERITY_CLEAR )
        {
            mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__BM );
            node_ptr->alarms[MTC_ALARM_ID__BM] = FM_ALARM_SEVERITY_CLEAR ;
        }
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
            else if ( daemon_want_fit ( FIT_CODE__START_HOST_SERVICES, node_ptr->hostname ))
            {
                if (( node_ptr->start_services_needed == false ) &&
                    ( node_ptr->start_services_running_main == false ))
                {
                    node_ptr->start_services_needed  = true ;
                    node_ptr->start_services_retries = 0    ;
                }
                else
                {
                    ilog ("%s start host services (FIT) rejected (%d:%d)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->start_services_needed,
                              node_ptr->start_services_running_main);
                }
            }
#endif


            /* Avoid forcing the states to the database when on the first & second pass.
             * This is because it is likely we just read all the states and
             * if coming out of a DOR or a SWACT we don't need to un-necessarily
             * produce that extra sysinv traffic.
             * Also, no point forcing the states while there is an admin action
             * or enable or graceful recovery going on as well because state changes
             * are being done in the FSM already */
            if (( node_ptr->oos_test_count > 1 ) &&
                ( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE ) &&
                ( !node_ptr->handlerStage.raw ) &&
                ( !node_ptr->recoveryStage ))
            {
                /* Change the oper and avail states in the database */
                allStateChange ( node_ptr, node_ptr->adminState,
                                           node_ptr->operState,
                                           node_ptr->availStatus );
            }

#ifdef WANT_CLEAR_ALARM_AUDIT

            /* TODO: Obsolete with new Alarm Strategy */
            /* Self Correct Stuck Failure Alarms */
            if (( node_ptr->adminState   == MTC_ADMIN_STATE__UNLOCKED  ) &&
                ( node_ptr->operState    == MTC_OPER_STATE__ENABLED ) &&
                (( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
                 ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )))
            {
                if ( node_ptr->alarms[MTC_ALARM_ID__CONFIG] != FM_ALARM_SEVERITY_CLEAR )
                {
                    mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__CONFIG );
                    node_ptr->alarms[MTC_ALARM_ID__CONFIG] = FM_ALARM_SEVERITY_CLEAR ;
                }
                alarm_enabled_clear ( node_ptr , false);
            }
#endif
            /* Make sure the locked status on the host itself is set */
            if (( node_ptr->adminState  == MTC_ADMIN_STATE__LOCKED  ) &&
                ( node_ptr->operState   == MTC_OPER_STATE__DISABLED ) &&
                ( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE ) &&
                ( !(node_ptr->mtce_flags & MTC_FLAG__I_AM_LOCKED)    ))
            {
                ilog ("%s setting 'locked' status\n", node_ptr->hostname.c_str());

                /* Tell the host that it is locked */
                send_mtc_cmd ( node_ptr->hostname , MTC_MSG_LOCKED, MGMNT_INTERFACE);
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



int local_counter = 0 ;

int nodeLinkClass::insv_test_handler ( struct nodeLinkClass::node * node_ptr )
{
    switch (node_ptr->insvTestStage)
    {
        case MTC_INSV_TEST__START:
        {
            mtcTimer_reset ( node_ptr->insvTestTimer );

            /* Run the inservice test more frequently while
             * start_services_needed is true and we are not
             * in failure retry mode */
            if (( node_ptr->start_services_needed == true ) &&
                ( node_ptr->hostservices_failed == false ) &&
                ( node_ptr->hostservices_failed_subf == false ))
            {
                mtcTimer_start ( node_ptr->insvTestTimer, mtcTimer_handler, MTC_SECS_2 );
            }
            else
            {
                mtcTimer_start ( node_ptr->insvTestTimer, mtcTimer_handler, insv_test_period );
            }
            insvTestStageChange ( node_ptr, MTC_INSV_TEST__WAIT );
            break ;
        }
        case MTC_INSV_TEST__WAIT:
        {
            if ( node_ptr->insvTestTimer.ring == true )
            {
                insvTestStageChange ( node_ptr, MTC_INSV_TEST__RUN );
            }
            /* manage degrade state and alarms */
            if ((  node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
                (  node_ptr->operState   == MTC_OPER_STATE__ENABLED   ))
            {
                /************************************************************
                 *               Manage In-Service Alarms                   *
                 ***********************************************************/

                /* Manage Inservice Enable Alarm */
                if ( node_ptr->hostservices_failed )
                {
                    alarm_insv_failure ( node_ptr );
                }
                else
                {
                    alarm_insv_clear ( node_ptr, false );
                }

                /* Manage Compute Subfunction Failure Alarm */
                if ( node_ptr->hostservices_failed_subf )
                {
                    alarm_compute_failure ( node_ptr, FM_ALARM_SEVERITY_MAJOR );
                }
                else
                {
                    alarm_compute_clear ( node_ptr, false );
                }
            }
            break ;
        }
        case MTC_INSV_TEST__RUN:
        {

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
                 node_ptr->ipmitool_thread_ctrl.stage = THREAD_STAGE__IGNORE ;
                 node_ptr->ipmitool_thread_ctrl.id = true ;
                 node_ptr->ipmitool_thread_info.id = true ;
                 node_ptr->ipmitool_thread_info.command = IPMITOOL_THREAD_CMD__POWER_STATUS ;

                 /* Update / Setup the BMC access credentials */
                 node_ptr->thread_extra_info.bm_ip   = node_ptr->bm_ip   ;
                 node_ptr->thread_extra_info.bm_un   = node_ptr->bm_un   ;
                 node_ptr->thread_extra_info.bm_pw   = node_ptr->bm_pw   ;
                 node_ptr->thread_extra_info.bm_type = node_ptr->bm_type ;
                 node_ptr->ipmitool_thread_info.extra_info_ptr = &node_ptr->thread_extra_info ;
                 if ( thread_launch_thread ( mtcThread_ipmitool, &node_ptr->ipmitool_thread_info ) == 0 )
                 {
                     slog ("%s FIT launching mtcThread_ipmitool power query thread ; launch failed\n", node_ptr->hostname.c_str());
                 }
                 else
                 {
                     slog ("%s FIT launching mtcThread_ipmitool power query thread\n", node_ptr->hostname.c_str());
                 }
                 node_ptr->ipmitool_thread_ctrl.done = true ;
            }

#endif

            /* Manage active controller auto recovery bool.
             * If the inactive controller is inservice then disable
             * controller autorecovery. Otherwise enable it but in this case
             * don't change the disable bool as that is used to gate auto
             * recovery once the threshoild is reached */
            if ( is_controller ( node_ptr ) && NOT_THIS_HOST )
            {
                if (( this->autorecovery_enabled == true ) &&
                    ( node_ptr->operState == MTC_OPER_STATE__ENABLED ))
                {
                    autorecovery_clear ( CONTROLLER_0 );
                    autorecovery_clear ( CONTROLLER_1 );
                    this->autorecovery_enabled  = false ;
                    this->autorecovery_disabled = false ;
                }
                else if (( this->autorecovery_enabled == false ) &&
                         ( node_ptr->operState != MTC_OPER_STATE__ENABLED ))
                {
                    this->autorecovery_enabled = true ;
                }
            }

            /* Monitor the health of the host - no pass file */
            if ((  node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
                (  node_ptr->operState   == MTC_OPER_STATE__ENABLED   ))
            {
                /************************************************************
                 * Prevent the start host services from running while in DOR
                 ***********************************************************/
                if ( node_ptr->dor_recovery_mode == true )
                {
                    /* wait longer for the host to boot up */
                    wlog ("%s DOR recovery active ; waiting on host\n",
                              node_ptr->hostname.c_str());
                }
                else if ( this->dor_mode_active == true )
                {
                    ilog_throttled ( this->dor_mode_active_log_throttle, 20,
                                     "DOR mode active\n");
                }

               /*************************************************************
                * Handle Start Host Services if its posted for execution
                ************************************************************/
                else if ( node_ptr->start_services_needed == true )
                {
                    /* If Main Start Host Services is not already running then launch it */
                    if (( node_ptr->start_services_running_main == false ) &&
                        ( node_ptr->start_services_running_subf == false ))
                    {
                        bool start = true ;
                        if ( this->launch_host_services_cmd ( node_ptr , start ) != PASS )
                        {
                            node_ptr->hostservices_failed = true ;
                            node_ptr->start_services_retries++ ;
                        }
                        else
                        {
                            node_ptr->start_services_running_main = true ;
                        }
                    }
                    /* Handle start host services response for both main and
                     * subfunction levels */
                    else
                    {
                        /* Wait for host services to complete - pass or fail.
                         * The host_services_handler manages timeout. */
                        int rc = this->host_services_handler ( node_ptr );
                        if ( rc == RETRY )
                        {
                            /* wait for the mtcClient's response ... */
                            break ;
                        }

                        node_ptr->start_services_running_main = false ;

                        if ( rc != PASS )
                        {

                            /* set the correct failed flag */
                            if ( node_ptr->start_services_needed_subf == true )
                            {
                                node_ptr->start_services_running_subf = false ;
                                node_ptr->hostservices_failed_subf = true ;
                            }
                            else
                            {
                                node_ptr->hostservices_failed = true ;
                            }

                            node_ptr->start_services_retries++ ;

                            wlog ("%s %s request failed ; (retry %d)\n",
                                      node_ptr->hostname.c_str(),
                                      node_ptr->host_services_req.name.c_str(),
                                      node_ptr->start_services_retries);
                        }
                        else /* success path */
                        {
                            /* clear the correct fail flag */
                            if (( node_ptr->start_services_needed_subf == true ) &&
                                ( node_ptr->start_services_running_subf == true ))
                            {
                                node_ptr->start_services_needed_subf  = false ;
                                node_ptr->start_services_running_subf = false ;
                                node_ptr->hostservices_failed_subf    = false ;
                            }
                            else
                            {
                                node_ptr->hostservices_failed = false ;
                            }

                            /*************************************************
                             * Handle running the subfunction start compute
                             * host services command as a background operation
                             * after the controller start result has come in
                             * as a PASS.
                             ************************************************/
                            if ( node_ptr->start_services_needed_subf == true )
                            {
                                bool start = true ;
                                bool subf  = node_ptr->start_services_needed_subf ;
                                if ( this->launch_host_services_cmd ( node_ptr, start, subf ) != PASS )
                                {
                                    node_ptr->hostservices_failed_subf = true ;

                                    /* try again on next audit */
                                    node_ptr->start_services_retries++ ;
                                }
                                else
                                {
                                    node_ptr->start_services_running_subf = true ;
                                }
                            }
                            else
                            {
                                /* All host service scripts pass ; done */
                                clear_hostservices_ctls ( node_ptr );
                                node_ptr->hostservices_failed_subf = false ;
                                node_ptr->hostservices_failed = false ;
                            }
                        }
                    }
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
             *  Only in CPE type
             *   - clear the alarm if the issue goes away -
             *     i.e. the goenabled tests eventually pass. Today
             *     hey are not re-run in the background but someday they may be
             *   - raise the alarm and go degraded if the goEnabled_subf flag is not set
             *     and we have only a single enabled controller (which must be this one)
             *     and the alarm is not already raised.
             **/
            if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
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
                        ( this->autorecovery_disabled == false ) &&
                        ( node_ptr->start_services_needed == false ))
                    {
                        if (( node_ptr->adminAction != MTC_ADMIN_ACTION__ENABLE_SUBF ) &&
                            ( node_ptr->adminAction != MTC_ADMIN_ACTION__ENABLE ))
                        {
                            if (( node_ptr->inservice_failed_subf == false ) &&
                                ( node_ptr->hostservices_failed_subf == false ))
                            {
                                ilog ("%s-compute ... running recovery enable\n", node_ptr->hostname.c_str());

                                alarm_compute_clear ( node_ptr, true );

                                enableStageChange ( node_ptr, MTC_ENABLE__START );
                                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__ENABLE_SUBF );
                            }
                            else
                            {
                                ilog ("%s-compute subfunction is unlocked-disabled (non-operational)\n",
                                          node_ptr->hostname.c_str());
                            }
                        }
                        else
                        {
                            ilog ("%s-compute ... waiting on current goEnable completion\n", node_ptr->hostname.c_str() );
                        }
                    }
                }
                /* Only raise this alarm while in simplex */
                if (( num_controllers_enabled() < 2 ) &&
                    (( node_ptr->goEnabled_failed_subf == true ) ||
                     ( node_ptr->inservice_failed_subf == true ) ||
                     ( node_ptr->hostservices_failed_subf == true )))
                {
                    if ( node_ptr->alarms[MTC_ALARM_ID__CH_COMP] == FM_ALARM_SEVERITY_CLEAR )
                    {
                        wlog ("%s insv test detected subfunction failure ; degrading host\n",
                                  node_ptr->hostname.c_str());

                        alarm_compute_failure ( node_ptr , FM_ALARM_SEVERITY_MAJOR );

                        allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                                   MTC_OPER_STATE__ENABLED,
                                                   MTC_AVAIL_STATUS__DEGRADED );

                        subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                                    MTC_AVAIL_STATUS__FAILED );

                    }
                }
            }

            /* Monitor the health of the host - no pass file */
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

                if ( node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY)
                {
                    /* not healthy .... */
                    if ( THIS_HOST )
                    {
                        /* initial config is complete and last manifest apply failed ... */
                        if (( daemon_is_file_present ( CONFIG_COMPLETE_FILE )) &&
                            ( daemon_is_file_present ( CONFIG_FAIL_FILE )))
                        {
                            wlog_throttled ( node_ptr->health_threshold_counter, (MTC_UNHEALTHY_THRESHOLD*3), "%s is UNHEALTHY\n", node_ptr->hostname.c_str());
                            if ( node_ptr->health_threshold_counter >= MTC_UNHEALTHY_THRESHOLD )
                            {
                                node_ptr->degrade_mask |= DEGRADE_MASK_CONFIG ;

                                /* threshold is reached so raise the config alarm if it is not already raised */
                                if ( node_ptr->alarms[MTC_ALARM_ID__CONFIG] != FM_ALARM_SEVERITY_CRITICAL )
                                {
                                    mtcAlarm_critical ( node_ptr->hostname, MTC_ALARM_ID__CONFIG );
                                    node_ptr->alarms[MTC_ALARM_ID__CONFIG] = FM_ALARM_SEVERITY_CRITICAL ;
                                }
                            }
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
                else
                {
                    node_ptr->health_threshold_counter = 0 ;
                }
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
