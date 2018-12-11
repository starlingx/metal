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
 *  nodeLinkClass::subf_enable_handler
 *  nodeLinkClass::     disable_handler
 *  nodeLinkClass::     delete_handler
 *  nodeLinkClass::degrade_handler
 *  nodeLinkClass::reset_handler
 *  nodeLinkClass::event_handler
 *  nodeLinkClass::recovery_handler

 ****************************************************************************/

using namespace std;

#define __AREA__ "hdl"

#include "nodeClass.h"    /* All base stuff                  */
#include "mtcAlarm.h"     /* for ... mtcAlarm_<severity>     */
#include "mtcNodeMsg.h"   /* for ... send_mtc_cmd            */
#include "nodeTimers.h"   /* for ... mtcTimer_start/stop     */
#include "jsonUtil.h"     /* for ... jsonApi_array_value     */
#include "mtcNodeHdlrs.h" /* for ... mtcTimer_handler        */
#include "mtcInvApi.h"    /* for ... SYSINV API              */
#include "mtcSmgrApi.h"   /* for ... SM API                  */
#include "mtcVimApi.h"    /* for ... VIm API                 */

#include "daemon_common.h"

int nodeLinkClass::enable_subf_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    if ( node_ptr->ar_disabled == true )
    {
        enableStageChange ( node_ptr, MTC_ENABLE__START );
        return (rc);
    }

    /* Setup the log prefix */
    string name = node_ptr->hostname ;
    name.append("-worker");

    switch ( (int)node_ptr->enableStage )
    {
        case MTC_ENABLE__FAILURE_WAIT:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                wlog ("%s workQueue empty timeout, purging ...\n", name.c_str());
            }
            else
            {
                /* give the work queues some time to complete */
                rc = workQueue_done ( node_ptr );
                if ( rc == RETRY )
                {
                    /* wait longer */
                    break ;
                }
            }
            workQueue_purge ( node_ptr );
            doneQueue_purge ( node_ptr );

            force_full_enable ( node_ptr ) ;
            break ;
        }

        case MTC_ENABLE__START:
        {
            plog ("%s Subf Enable FSM (from start)\n", name.c_str());

            node_ptr->unknown_health_reported = false ;
            node_ptr->goEnabled_failed_subf   = false ;

            /* load worker subfunciton alarm state */
            EFmAlarmSeverityT sev = mtcAlarm_state ( node_ptr->hostname,
                                                 MTC_ALARM_ID__CH_COMP);
            if ( sev != FM_ALARM_SEVERITY_CLEAR )
            {
                node_ptr->alarms[MTC_ALARM_ID__CH_COMP] = sev ;
                node_ptr->degrade_mask |= DEGRADE_MASK_SUBF;
            }

            /* start a timer that waits for the /var/run/.worker_config_complete flag */
            mtcTimer_reset ( node_ptr->mtcTimer );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_WORKER_CONFIG_TIMEOUT );

            enableStageChange ( node_ptr, MTC_ENABLE__CONFIG_COMPLETE_WAIT );
            break ;
        }

        /* Wait for the CONFIG_WORKER_COMPUTE flag file that indicates
         * that the worker part of the combo-blade init is finished */
        case MTC_ENABLE__CONFIG_COMPLETE_WAIT:
        {
            /* look for file */
            if ( node_ptr->mtce_flags & MTC_FLAG__SUBF_CONFIGURED )
            {
                mtcTimer_reset (node_ptr->mtcTimer);
                plog ("%s Subf Configured OK\n", name.c_str());
                enableStageChange ( node_ptr, MTC_ENABLE__GOENABLED_TIMER );
                alarm_config_clear ( node_ptr );
            }

            if ((( !node_ptr->mtce_flags & MTC_FLAG__I_AM_CONFIGURED )) ||
                ((  node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY )))
            {
                mtcTimer_reset (node_ptr->mtcTimer);

                elog ("%s configuration failed or incomplete (oob:%x)\n",
                          name.c_str(), node_ptr->mtce_flags);

                alarm_config_failure ( node_ptr );

                mtcInvApi_update_task ( node_ptr, MTC_TASK_SUBF_CONFIG_FAIL );

                enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );

                /* handle auto recovery for this failure */
                if ( ar_manage ( node_ptr,
                                 MTC_AR_DISABLE_CAUSE__CONFIG,
                                 MTC_TASK_AR_DISABLED_CONFIG ) != PASS )
                    break ;
            }

            /* timeout handling */
            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s configuration timeout (%d secs)\n",
                          name.c_str(),
                          MTC_WORKER_CONFIG_TIMEOUT );

                alarm_config_failure ( node_ptr );

                mtcInvApi_update_task ( node_ptr, MTC_TASK_SUBF_CONFIG_TO );

                enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );

                /* handle auto recovery for this failure */
                if ( ar_manage ( node_ptr,
                                 MTC_AR_DISABLE_CAUSE__CONFIG,
                                 MTC_TASK_AR_DISABLED_CONFIG ) != PASS )
                    break ;
            }
            else
            {
                ; /* wait longer */
            }
            break ;
        }

        case MTC_ENABLE__GOENABLED_TIMER:
        {
            /*****************************************************************
             *
             * issue: subfunction go-enable patching script fails and
             * maintenance reboots the active controller when no-reboot
             * patching maintenance in CPE.
             *
             * The fix is to avoid running the subfunction go-enabled tests
             * on self while patching.
             *
             ****************************************************************/
            if (( THIS_HOST ) &&
                (( daemon_is_file_present ( PATCHING_IN_PROG_FILE )) ||
                 ( daemon_is_file_present ( NODE_IS_PATCHED_FILE ))))
            {
                ilog ("%s skipping out-of-service tests while self patching\n", name.c_str());

                /* set the goenabled complete flag */
                daemon_log ( GOENABLED_SUBF_PASS, "out-of-service tests skipped due to patching");
                node_ptr->goEnabled_failed_subf = false ;

                alarm_compute_clear ( node_ptr, true );

                /* ok. great, got the go-enabled message, lets move on */
                enableStageChange ( node_ptr, MTC_ENABLE__HOST_SERVICES_START );
                break ;
            }
            ilog ("%s running out-of-service tests\n", name.c_str());

            /* See if the host is there and already in the go enabled state */
            send_mtc_cmd ( node_ptr->hostname, MTC_REQ_SUBF_GOENABLED, MGMNT_INTERFACE );

            /* start the reboot timer - is cought in the mtc alive case */
            mtcTimer_reset ( node_ptr->mtcTimer );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, this->goenabled_timeout );

            /* start waiting fhr the ENABLE READY message */
            enableStageChange ( node_ptr, MTC_ENABLE__GOENABLED_WAIT );

            node_ptr->goEnabled_subf = false ;
            node_ptr->goEnabled_failed_subf = false ;

            break ;
        }
        case MTC_ENABLE__GOENABLED_WAIT:
        {
            bool goenable_failed = false ;

            /* search for the Go Enable message */
            if (( node_ptr->health == NODE_UNHEALTHY ) ||
                ( node_ptr->mtce_flags & MTC_FLAG__I_AM_NOT_HEALTHY) ||
                ( node_ptr->goEnabled_failed_subf == true ))
            {
                mtcTimer_reset ( node_ptr->mtcTimer );
                elog ("%s one or more out-of-service tests failed\n", name.c_str());

                mtcInvApi_update_task ( node_ptr, MTC_TASK_SUBF_INTEST_FAIL );
                enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );
                goenable_failed = true ;
            }

            /* search for the Go Enable message */
            else if ( node_ptr->goEnabled_subf == true )
            {
                mtcTimer_reset ( node_ptr->mtcTimer );

                alarm_enabled_clear ( node_ptr, false );
                alarm_compute_clear ( node_ptr, true );

                plog ("%s passed  out-of-service tests\n", name.c_str());

                /* O.K. clearing the state now that we got it */
                // node_ptr->goEnabled_subf        = true ;
                node_ptr->goEnabled_failed_subf = false ;

                /* ok. great, got the go-enabled message, lets move on */

                if ( node_ptr->start_services_needed_subf == true )
                {
                    /* If the add_handler set start_services_needed_subf to
                     * true then we bypass inline execution and allow it to
                     * be serviced as a scheduled background operation. */
                    enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_CHECK );
                }
                else
                {
                    enableStageChange ( node_ptr, MTC_ENABLE__HOST_SERVICES_START );
                }
                break ;
            }

            else if ( node_ptr->mtcTimer.ring == true )
            {
                elog ("%s out-of-service test execution timeout\n", name.c_str());

                mtcInvApi_update_task ( node_ptr, MTC_TASK_SUBF_INTEST_TO );
                enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );
                goenable_failed = true ;
            }
            else
            {
                ; /* wait some more */
            }

            if ( goenable_failed == true )
            {
                alarm_compute_failure ( node_ptr, FM_ALARM_SEVERITY_CRITICAL );

                /* handle auto recovery for this failure */
                if ( ar_manage ( node_ptr,
                                 MTC_AR_DISABLE_CAUSE__GOENABLE,
                                 MTC_TASK_AR_DISABLED_GOENABLE ) != PASS )
                    break ;
            }
            break ;
        }
        case  MTC_ENABLE__HOST_SERVICES_START:
        {
            bool start = true ;
            bool subf  = true ;

            plog ("%s %s host services\n",
                      name.c_str(),
                      node_ptr->start_services_needed_subf ? "scheduling start compute" :
                                                             "starting compute");

            if ( node_ptr->start_services_needed_subf == true )
            {
                bool force = true ;

                /* If the add_handler set start_services_needed_subf to
                 * true then we bypass inline execution and allow it to
                 * be serviced as a scheduled background operation. */
                enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_CHECK );
                alarm_compute_clear ( node_ptr, force );
            }

            else if ( launch_host_services_cmd ( node_ptr, start, subf ) != PASS )
            {
                wlog ("%s %s failed ; launch\n",
                          name.c_str(),
                          node_ptr->host_services_req.name.c_str());

                node_ptr->hostservices_failed_subf = true ;
                alarm_compute_failure ( node_ptr, FM_ALARM_SEVERITY_CRITICAL );
                enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );
                mtcInvApi_update_task ( node_ptr, MTC_TASK_SUBF_SERVICE_FAIL );

                /* handle auto recovery for this failure */
                if ( ar_manage ( node_ptr,
                                 MTC_AR_DISABLE_CAUSE__HOST_SERVICES,
                                 MTC_TASK_AR_DISABLED_SERVICES ) != PASS )
                    break ;
            }
            else
            {
                enableStageChange ( node_ptr, MTC_ENABLE__HOST_SERVICES_WAIT );
            }
            break ;
        }

        case MTC_ENABLE__HOST_SERVICES_WAIT:
        {
            /* Wait for host services to complete - pass or fail.
             * The host_services_handler manages timeout. */
            rc = host_services_handler ( node_ptr );
            if ( rc == RETRY )
            {
                /* wait for the mtcClient's response ... */
                break ;
            }
            else if ( rc != PASS )
            {
                node_ptr->hostservices_failed_subf = true ;
                alarm_compute_failure ( node_ptr, FM_ALARM_SEVERITY_CRITICAL );

                enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );


                if ( rc == FAIL_TIMEOUT )
                {
                    elog ("%s %s failed ; timeout\n",
                              name.c_str(),
                              node_ptr->host_services_req.name.c_str());

                    /* Report "Enabling Compute Service Timeout" to sysinv/horizon */
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_SUBF_SERVICE_TO );
                }
                else
                {
                    elog ("%s %s failed ; rc:%d\n",
                              name.c_str(),
                              node_ptr->host_services_req.name.c_str(),
                              rc);

                    /* Report "Enabling Compute Service Failed" to sysinv/horizon */
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_SUBF_SERVICE_FAIL );
                }

                /* handle auto recovery for this failure */
                if ( ar_manage ( node_ptr,
                                 MTC_AR_DISABLE_CAUSE__HOST_SERVICES,
                                 MTC_TASK_AR_DISABLED_SERVICES ) != PASS )
                    break ;
            }
            else /* success path */
            {
                alarm_compute_clear ( node_ptr, true );
                node_ptr->hostservices_failed_subf = false ;
                enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_CHECK );
            }
            break ;
        }
        case MTC_ENABLE__HEARTBEAT_CHECK:
        {
            if ( THIS_HOST )
            {
                enableStageChange ( node_ptr, MTC_ENABLE__STATE_CHANGE );
            }
            else
            {
                /* allow the fsm to wait for up to 1 minute for the
                 * hbsClient's ready event before starting heartberat
                 * test. */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_MINS_1 );
                enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_WAIT );
            }

            break ;
        }
        case MTC_ENABLE__HEARTBEAT_WAIT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcTimer ) )
            {
                wlog ("%s hbsClient ready event timeout\n", name.c_str());
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
                enableStageChange ( node_ptr, MTC_ENABLE__STATE_CHANGE );
            }
            else
            {
                plog ("%s Starting %d sec Heartbeat Soak (with%s)\n",
                      name.c_str(),
                      MTC_HEARTBEAT_SOAK_BEFORE_ENABLE,
                      node_ptr->hbsClient_ready ? " ready event" : "out ready event"  );

                /* allow heartbeat to run for 10 seconds before we declare enable */
                mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, MTC_HEARTBEAT_SOAK_BEFORE_ENABLE );
                enableStageChange ( node_ptr, MTC_ENABLE__HEARTBEAT_SOAK );

                /* Start Monitoring heartbeat */
                send_hbs_command ( node_ptr->hostname, MTC_CMD_START_HOST );
            }
            break ;
        }
        case MTC_ENABLE__HEARTBEAT_SOAK:
        {
            if ( node_ptr->mtcTimer.ring == true )
            {
                plog ("%s heartbeating\n", name.c_str() );

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

            /* Now that we have posted the unlocked-enabled-available state we need
             * to force the final part of the enable sequence through */
            if ( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE )
            {
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__ENABLE );
            }
            enableStageChange ( node_ptr, MTC_ENABLE__WORKQUEUE_WAIT );

            /* Start a timer that failed enable if the work queue
             * does not empty or if commands in the done queue have failed */
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, work_queue_timeout );

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
                elog ("%s enable failed ; Enable workQueue timeout, purging ...\n", name.c_str());

                mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_WORK_TO );

                fail = true ;
            }
            else if ( rc != PASS )
            {
                elog ("%s enable failed ; Enable doneQueue has failed commands\n", name.c_str());

                mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_WORK_FAIL );

                fail = true ;
            }
            else if ( this->system_type != SYSTEM_TYPE__CPE_MODE__SIMPLEX )
            {
                /* Loop over the heartbeat interfaces and fail the Enable if any of them are failing */
                for ( int i = 0 ; i < MAX_IFACES ; i++ )
                {
                    if ( node_ptr->heartbeat_failed[i] == true )
                    {
                        elog ("%s Enable failure due to %s Network *** Heartbeat Loss ***\n",
                                  name.c_str(),
                                  get_iface_name_str ((iface_enum)i));

                        mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_FAIL_HB );
                        fail = true ;
                    }
                }
            }
            if ( fail == true )
            {
                enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );
                break ;
            }
            else
            {
                if ( node_ptr->dor_recovery_mode || node_ptr->was_dor_recovery_mode )
                {
                    node_ptr->dor_recovery_mode = false ;
                    node_ptr->was_dor_recovery_mode = true ;
                }

                if (( node_ptr->alarms[MTC_ALARM_ID__CH_COMP] != FM_ALARM_SEVERITY_CLEAR ) ||
                    ( node_ptr->alarms[MTC_ALARM_ID__ENABLE] != FM_ALARM_SEVERITY_CLEAR ) ||
                    ( node_ptr->alarms[MTC_ALARM_ID__CONFIG] != FM_ALARM_SEVERITY_CLEAR ))
                {
                    wlog ("%s enable to degraded migration due to alarm [%d:%d:%d]\n",
                              name.c_str(),
                              node_ptr->alarms[MTC_ALARM_ID__CH_COMP],
                              node_ptr->alarms[MTC_ALARM_ID__ENABLE],
                              node_ptr->alarms[MTC_ALARM_ID__CONFIG] );

                    enableStageChange ( node_ptr, MTC_ENABLE__SUBF_FAILED );
                }
                else if ( node_ptr->degrade_mask )
                {
                    enableStageChange ( node_ptr, MTC_ENABLE__DEGRADED );
                }
                else
                {
                    enableStageChange ( node_ptr, MTC_ENABLE__ENABLED );
                }
            }
            break ;
        }
        case MTC_ENABLE__ENABLED:
        {
            bool force = true ;

            /* Set node as unlocked-enabled */
            allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                       MTC_OPER_STATE__ENABLED,
                                       MTC_AVAIL_STATUS__AVAILABLE );

            subfStateChange ( node_ptr, MTC_OPER_STATE__ENABLED,
                                        MTC_AVAIL_STATUS__AVAILABLE );

            node_ptr->subf_enabled = true ;
            node_ptr->inservice_failed_subf    = false ;
            if ( node_ptr->was_dor_recovery_mode )
            {
                report_dor_recovery (  node_ptr , "is ENABLED" );
            }
            else
            {
                plog ("%s is ENABLED\n", name.c_str());
            }

            /* already cleared if true so no need to do it again */
            if ( node_ptr->start_services_needed_subf != true )
            {
                alarm_compute_clear ( node_ptr, force );
            }

            enableStageChange ( node_ptr, MTC_ENABLE__DONE );

            break ;
        }
        /* Allow the host to come up in the degraded state */
        case MTC_ENABLE__DEGRADED:
        {
            if ( node_ptr->alarms[MTC_ALARM_ID__CH_COMP] == FM_ALARM_SEVERITY_CLEAR )
            {
                subfStateChange ( node_ptr, MTC_OPER_STATE__ENABLED,
                                            MTC_AVAIL_STATUS__AVAILABLE );
            }
            else
            {
                subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                            MTC_AVAIL_STATUS__FAILED );
            }

            /* Set node as unlocked-enabled */
            allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                       MTC_OPER_STATE__ENABLED,
                                       MTC_AVAIL_STATUS__DEGRADED );

            if ( node_ptr->was_dor_recovery_mode )
            {
                report_dor_recovery (  node_ptr , "is ENABLED-degraded" );
            }
            else
            {
                wlog ("%s is ENABLED-degraded\n", name.c_str());
            }
            enableStageChange ( node_ptr, MTC_ENABLE__DONE );

            break ;
        }
        /* Allow the host to come up in the degraded state */
        case MTC_ENABLE__SUBF_FAILED:
        {
            subfStateChange ( node_ptr, MTC_OPER_STATE__DISABLED,
                                        MTC_AVAIL_STATUS__FAILED );

            /* Set node as unlocked-enabled */
            allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                       MTC_OPER_STATE__ENABLED,
                                       MTC_AVAIL_STATUS__DEGRADED );

            if ( node_ptr->was_dor_recovery_mode )
            {
                report_dor_recovery (  node_ptr , "is DISABLED-failed" );
            }
            else
            {
                elog ("%s is DISABLED-failed (subfunction failed)\n",
                          name.c_str() );
            }
            this->dor_mode_active = false ;

            alarm_compute_failure ( node_ptr , FM_ALARM_SEVERITY_CRITICAL ) ;

            /* Start a timer that failed enable if the work queue
             * does not empty or if commands in the done queue have failed */
            mtcTimer_reset ( node_ptr->mtcTimer );
            mtcTimer_start ( node_ptr->mtcTimer, mtcTimer_handler, work_queue_timeout );
            enableStageChange ( node_ptr, MTC_ENABLE__FAILURE_WAIT );

            break ;
        }
        case MTC_ENABLE__DONE:
        {
            mtcTimer_reset ( node_ptr->mtcTimer );

            /* Override cmd of ENABLED if action is UNLOCK */
            mtc_cmd_enum cmd = CONTROLLER_ENABLED ;
            if ( node_ptr->adminAction == MTC_ADMIN_ACTION__UNLOCK )
            {
                cmd = CONTROLLER_UNLOCKED ;
            }

            mtcSmgrApi_request     ( node_ptr, cmd, SMGR_MAX_RETRIES );
            mtcVimApi_state_change ( node_ptr, VIM_HOST_ENABLED, 3 );

            adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
            enableStageChange ( node_ptr, MTC_ENABLE__START );

            node_ptr->enabled_count++ ;
            node_ptr->health_threshold_counter = 0 ;

            node_ptr->was_dor_recovery_mode = false ;
            node_ptr->dor_recovery_mode = false ;
            this->dor_mode_active = false ;

            ar_enable ( node_ptr );

            mtcInvApi_force_task ( node_ptr, "" );
            break ;
        }
        default:
            rc = FAIL_BAD_CASE ;
    }
    return (rc);
}
