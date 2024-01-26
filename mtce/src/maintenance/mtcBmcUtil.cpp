/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Wind River Titanium Cloud Maintenance BMC Utilities
 */
#include <stdio.h>
#include <iostream>
#include <string.h>

using namespace std;

#include "nodeBase.h"      /* for ... mtce common definitions          */
#include "nodeClass.h"     /* for ...                                  */
#include "bmcUtil.h"       /* for ... mtce-common bmc utility header   */

/*****************************************************************************
 *
 * Name       : bmc_command_send
 *
 * Description: This utility starts the bmc command handling thread
 *              with the specified command.
 *
 * Returns    : PASS if all the pre-start semantic checks pass and the
 *              thread was started.
 *
 *              Otherwise the thread was not started and some non zero
 *              FAIL_xxxx code is returned after a representative design
 *              log is generated.
 *
 *****************************************************************************/

int nodeLinkClass::bmc_command_send ( struct nodeLinkClass::node * node_ptr,
                                      int command )
{
    int rc = PASS ;

    /* handle 'kill of in-progress' thread or 'done but not consumed' thread */
    if ( ! thread_idle ( node_ptr->bmc_thread_ctrl ))
    {
        if ( ! thread_done ( node_ptr->bmc_thread_ctrl ))
        {
            thread_kill ( node_ptr->bmc_thread_ctrl,
                          node_ptr->bmc_thread_info );
            return (RETRY);
        }
        else
        {
             mtcTimer_reset ( node_ptr->bmc_thread_ctrl.timer );
             if ( thread_done_consume ( node_ptr->bmc_thread_ctrl,
                                        node_ptr->bmc_thread_info ) != PASS )
             {
                 return (RETRY);
             }
        }
    }

    node_ptr->bmc_thread_info.command = command ;

    /* Update / Setup the BMC access credentials */
    node_ptr->thread_extra_info.bm_ip   = node_ptr->bm_ip   ;
    node_ptr->thread_extra_info.bm_un   = node_ptr->bm_un   ;
    node_ptr->thread_extra_info.bm_pw   = node_ptr->bm_pw   ;

    /* Special case handling for Redfish Root (BMC) Query command.
     * Current protocol override for this command that only applies
     * to redfish and used for the bmc protocol learning process. */
    if ( command == BMC_THREAD_CMD__BMC_QUERY )
        node_ptr->bmc_thread_info.proto = BMC_PROTOCOL__REDFISHTOOL ;
    else
        node_ptr->bmc_thread_info.proto = node_ptr->bmc_protocol ;

    if ( node_ptr->bmc_thread_info.proto == BMC_PROTOCOL__REDFISHTOOL )
    {
        /* set the command specific redfishtool base command string */
        if ( command == BMC_THREAD_CMD__RAW_GET )
            node_ptr->bm_cmd = REDFISHTOOL_RAW_GET_CMD ;
        else
            node_ptr->bm_cmd = REDFISHTOOL_POWER_RESET_CMD ;

        /* append to the reset/power control or raw get command string */
        switch (command)
        {
            case BMC_THREAD_CMD__RAW_GET:
            {
                if ( ! node_ptr->bmc_info.power_ctrl.raw_target_path.empty() )
                {
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.raw_target_path);
                }
                else
                {
                    elog("%s is missing the raw get target", node_ptr->hostname.c_str());
                    return(FAIL_NOT_SUPPORTED);
                }
                break ;
            }
            case BMC_THREAD_CMD__POWER_RESET:
            {
                /* Use graceful for the first half of the retry countdown
                 * and immediate for the remaining retries. */
                if ((!node_ptr->bmc_info.power_ctrl.reset.immediate.empty()) &&
                    ( node_ptr->power_action_retries < MTC_RESET_ACTION_SWITCH_THRESHOLD))
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.reset.immediate);

                /* Unfaulted graceful if it exists */
                else if (!node_ptr->bmc_info.power_ctrl.reset.graceful.empty())
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.reset.graceful);

                /* Unfaulted immediate if graceful does not exist */
                else if (!node_ptr->bmc_info.power_ctrl.reset.immediate.empty())
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.reset.immediate);
                else
                {
                    elog("%s offers no supported reset commands", node_ptr->hostname.c_str());
                    return(FAIL_NOT_SUPPORTED);
                }
                break ;
            }
            case BMC_THREAD_CMD__POWER_ON:
            {
                /* Use graceful for the first half of the retry countdown
                 * and immediate for the remaining retries. */
                if ((!node_ptr->bmc_info.power_ctrl.poweron.immediate.empty()) &&
                    ( node_ptr->power_action_retries < MTC_POWER_ACTION_SWITCH_THRESHOLD))
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.poweron.immediate);

                /* Unfaulted graceful if it exists */
                else if (!node_ptr->bmc_info.power_ctrl.poweron.graceful.empty())
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.poweron.graceful);

                /* Unfaulted immediate if graceful does not exist */
                else if (!node_ptr->bmc_info.power_ctrl.poweron.immediate.empty())
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.poweron.immediate);
                else
                {
                    elog("%s offers no supported poweron commands", node_ptr->hostname.c_str());
                    return(FAIL_NOT_SUPPORTED);
                }
                break ;
            }
            case BMC_THREAD_CMD__POWER_OFF:
            {
                /* Use graceful for the first half of the retry countdown
                 * and immediate for the remaining retries. */
                if ((!node_ptr->bmc_info.power_ctrl.poweroff.immediate.empty() ) &&
                    ( node_ptr->power_action_retries < MTC_POWER_ACTION_SWITCH_THRESHOLD))
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.poweroff.immediate);

                /* Unfaulted graceful if it exists */
                else if (!node_ptr->bmc_info.power_ctrl.poweroff.graceful.empty() )
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.poweroff.graceful);

                /* Unfaulted immediate if graceful does not exist */
                else if (!node_ptr->bmc_info.power_ctrl.poweroff.immediate.empty())
                    node_ptr->bm_cmd.append(node_ptr->bmc_info.power_ctrl.poweroff.immediate);
                else
                {
                    elog("%s offers no supported poweroff commands", node_ptr->hostname.c_str());
                    return(FAIL_NOT_SUPPORTED);
                }
                break ;
            }
        }
        node_ptr->thread_extra_info.bm_cmd  = node_ptr->bm_cmd  ;
    }
#ifdef WANT_FIT_TESTING
    {
        bool want_fit = false ;
        int fit = FIT_CODE__BMC_COMMAND_SEND ;
        if ( daemon_want_fit ( fit, node_ptr->hostname, "root_query" ) == true )
        {
            want_fit = true ;
        }
        else if ( daemon_want_fit ( fit, node_ptr->hostname, "bmc_info" ) == true )
        {
            want_fit = true ;
        }
        else if (( command == BMC_THREAD_CMD__POWER_STATUS ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_status" ) == true ))
        {
            want_fit = true ;
        }
        else if ( daemon_want_fit ( fit, node_ptr->hostname, "reset_cause" ) == true )
        {
            want_fit = true ;
        }
        else if (( command == BMC_THREAD_CMD__POWER_RESET ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "reset" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == BMC_THREAD_CMD__POWER_ON ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_on" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == BMC_THREAD_CMD__POWER_ON ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_none" ) == true ))
        {
            /* Just change the command to query status */
            command = BMC_THREAD_CMD__POWER_STATUS ;
        }
        else if (( command == BMC_THREAD_CMD__POWER_OFF ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_off" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == BMC_THREAD_CMD__POWER_OFF ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_none" ) == true ))
        {
            /* Just change the command to query status */
            command = BMC_THREAD_CMD__POWER_STATUS ;
        }
        else if (( command == BMC_THREAD_CMD__POWER_CYCLE ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_cycle" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == BMC_THREAD_CMD__BOOTDEV_PXE ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "netboot_pxe" ) == true ))
        {
            want_fit = true ;
        }

        if ( want_fit == true )
        {
            slog ("%s FIT %s\n", node_ptr->hostname.c_str(), bmcUtil_getCmd_str(command).c_str() );
            node_ptr->bmc_thread_info.status = node_ptr->bmc_thread_ctrl.status = rc = FAIL_FIT ;
            node_ptr->bmc_thread_info.status_string = "bmc_command_send fault insertion failure" ;
            return ( rc );
        }
    }
#endif

    if (( hostUtil_is_valid_ip_addr ( node_ptr->thread_extra_info.bm_ip ) == true ) &&
        ( !node_ptr->thread_extra_info.bm_un.empty() ) &&
        ( !node_ptr->thread_extra_info.bm_pw.empty ()))
    {
        node_ptr->bmc_thread_ctrl.status = rc =
        thread_launch ( node_ptr->bmc_thread_ctrl,
                        node_ptr->bmc_thread_info ) ;
        if ( rc != PASS )
        {
            elog ("%s failed to launch power control thread (rc:%d)\n",
                      node_ptr->hostname.c_str(), rc );
        }
        else
        {
            ilog ("%s %s send '%s' command (%s)",
                      node_ptr->hostname.c_str(),
                      node_ptr->bmc_thread_ctrl.name.c_str(),
                      bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                      bmcUtil_getProtocol_str(node_ptr->bmc_protocol).c_str());
        }
        node_ptr->bmc_thread_ctrl.retries = 0 ;
    }
    else
    {
        node_ptr->bmc_thread_ctrl.status = rc =
        node_ptr->bmc_thread_info.status = FAIL_INVALID_DATA ;
        node_ptr->bmc_thread_info.status_string = "one or more bmc credentials are invalid" ;

        wlog ("%s %s %s %s\n", node_ptr->hostname.c_str(),
                               hostUtil_is_valid_ip_addr (
                               node_ptr->thread_extra_info.bm_ip  ) ? "" : "bm_ip:invalid",
                               node_ptr->thread_extra_info.bm_un.empty() ? "bm_un:empty" : "",
                               node_ptr->thread_extra_info.bm_pw.empty() ? "bm_pw:empty" : "");
    }

    return (rc);
}

/*****************************************************************************
 *
 * Name       : bmc_command_recv
 *
 * Description: This utility will check for bmc command thread completion.
 *
 * Returns    : PASS       is returned if the thread reports done.
 *              RETRY      is returned if the thread has not completed.
 *              FAIL_RETRY is returned after 10 back-to-back calls return RETRY.
 *
 *****************************************************************************/

int nodeLinkClass::bmc_command_recv ( struct nodeLinkClass::node * node_ptr )
{
    int rc = RETRY ;

    /* check for 'thread done' completion */
    if ( thread_done( node_ptr->bmc_thread_ctrl ) == true )
    {
        if ( node_ptr->bmc_protocol == BMC_PROTOCOL__REDFISHTOOL )
        {
            if (( rc = node_ptr->bmc_thread_info.status ) != PASS )
            {
                /* handle the redfishtool root query as a special case because
                 * it is likely to fail and we don't want un-necessary error logs */
                if ((( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__BMC_QUERY ) ||
                     ( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__BMC_INFO )) &&
                    (( rc == FAIL_SYSTEM_CALL ) || ( rc == FAIL_NOT_ACTIVE )))
                {
                    blog ("%s bmc redfish %s failed",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str());
                }
                else
                {
                    elog ("%s bmc redfish %s command failed (%s) (data:%s) (rc:%d:%d:%s)\n",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                              bmcUtil_getProtocol_str(node_ptr->bmc_protocol).c_str(),
                              node_ptr->bmc_thread_info.data.c_str(),
                              rc,
                              node_ptr->bmc_thread_info.status,
                              node_ptr->bmc_thread_info.status_string.c_str());
                }
                goto bmc_command_recv_cleanup;
            }
            else
            {
                rc = PASS ;
            }
        }
        else /* default is ipmi */
        {
            if (( rc = node_ptr->bmc_thread_info.status ) != PASS )
            {
                /* Don't log an error if this is just the BMC Query failure
                 * used for protocol learning */
                if ( node_ptr->bmc_thread_info.command != BMC_THREAD_CMD__BMC_QUERY )
                {
                    elog ("%s %s command failed (%s) (data:%s) (rc:%d:%d:%s)\n",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                              bmcUtil_getProtocol_str(node_ptr->bmc_protocol).c_str(),
                              node_ptr->bmc_thread_info.data.c_str(),
                              rc,
                              node_ptr->bmc_thread_info.status,
                              node_ptr->bmc_thread_info.status_string.c_str());
                }
            }
            else
            {
                if ( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_RESET )
                {
                    if ( node_ptr->bmc_thread_info.data.find(IPMITOOL_POWER_RESET_RESP) == std::string::npos )
                        rc = FAIL_RESET_CONTROL ;
                }
                else if ( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_OFF )
                {
                    if ( node_ptr->bmc_thread_info.data.find(IPMITOOL_POWER_OFF_RESP) == std::string::npos )
                        rc = FAIL_POWER_CONTROL ;
                }
                else if ( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_ON )
                {
                    if ( node_ptr->bmc_thread_info.data.find(IPMITOOL_POWER_ON_RESP) == std::string::npos )
                        rc = FAIL_POWER_CONTROL ;
                }
                else if ( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_CYCLE )
                {
                    if ( node_ptr->bmc_thread_info.data.find(IPMITOOL_POWER_CYCLE_RESP) == std::string::npos )
                        rc = FAIL_POWER_CONTROL ;
                }

                if ( rc )
                {
                    node_ptr->bmc_thread_info.status = rc ;
                    node_ptr->bmc_thread_info.status_string = ("power command failed");
                    wlog ("%s %s Response: %s\n",
                              node_ptr->hostname.c_str(),
                              bmcUtil_getCmd_str(
                              node_ptr->bmc_thread_info.command).c_str(),
                              node_ptr->bmc_thread_info.data.c_str());
                }
                else
                {
                     blog1 ("%s %s Response: %s\n",
                                node_ptr->hostname.c_str(),
                                bmcUtil_getCmd_str(
                                node_ptr->bmc_thread_info.command).c_str(),
                                node_ptr->bmc_thread_info.data.c_str());
                }
            }
        }

#ifdef WANT_FIT_TESTING
        if ( rc == PASS )
        {
            bool want_fit = false ;
            int fit = FIT_CODE__BMC_COMMAND_RECV ;
            if ( daemon_want_fit ( fit, node_ptr->hostname, "root_query" ) == true )
            {
                want_fit = true ;
            }
            if ( daemon_want_fit ( fit, node_ptr->hostname, "bmc_info" ) == true )
            {
                want_fit = true ;
            }
            else if ( daemon_want_fit ( fit, node_ptr->hostname, "reset_cause" ) == true )
            {
                want_fit = true ;
            }
            else if (( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_RESET ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "reset" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_ON ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "power_on" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_OFF ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "power_off" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_CYCLE ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "power_cycle" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__POWER_STATUS ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "power_status" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->bmc_thread_info.command == BMC_THREAD_CMD__BOOTDEV_PXE ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "netboot_pxe" ) == true ))
            {
                want_fit = true ;
            }

            if ( want_fit == true )
            {
                node_ptr->bmc_thread_info.status = rc = FAIL_FIT ;
                node_ptr->bmc_thread_info.status_string = "bmc_command_recv fault insertion failure" ;
            }
        }
#endif
    }

    /* handle max retries reached */
    if ( rc == PASS )
    {
        ;
    }
    else if ( node_ptr->bmc_thread_ctrl.retries++ >= BMC__MAX_RECV_RETRIES )
    {
        wlog ("%s %s command timeout (%d of %d)\n",
                  node_ptr->hostname.c_str(),
                  bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                  node_ptr->bmc_thread_ctrl.retries,
                  BMC__MAX_RECV_RETRIES);

        rc = FAIL_RETRY;
    }

    /* handle progressive retry */
    else
    {
        if ( node_ptr->bmc_thread_ctrl.id == 0 )
        {
            wlog ("%s %s command not-running\n",
                      node_ptr->hostname.c_str(),
                      bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str());
            rc = FAIL_NOT_ACTIVE ;
        }
        else
        {
            /* The BMC is sometimes slow,
             * No need to log till we reach half of the retry threshold */
            if ( node_ptr->bmc_thread_ctrl.retries > (BMC__MAX_RECV_RETRIES/2) )
            {
                ilog ("%s %s command in-progress (polling %d of %d)\n",
                          node_ptr->hostname.c_str(),
                          bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                          node_ptr->bmc_thread_ctrl.retries,
                          BMC__MAX_RECV_RETRIES);
            }
            rc = RETRY ;
        }
    }

bmc_command_recv_cleanup:

    if ( rc != RETRY )
    {
        if ( rc != PASS )
        {
            ilog ("%s %s recv '%s' command (%s) (rc:%d)",
                      node_ptr->hostname.c_str(),
                      node_ptr->bmc_thread_ctrl.name.c_str(),
                      bmcUtil_getCmd_str(node_ptr->bmc_thread_info.command).c_str(),
                      bmcUtil_getProtocol_str(node_ptr->bmc_protocol).c_str(),
                      rc);
        }
        node_ptr->bmc_thread_ctrl.done = true ;
        node_ptr->bmc_thread_ctrl.retries = 0 ;
        node_ptr->bmc_thread_ctrl.id = 0 ;
        node_ptr->bmc_thread_info.id = 0 ;
        node_ptr->bmc_thread_info.command = 0 ;
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : bmc_command_done
 *
 * Description: This utility frees the bmc command thread for next execution.
 *
 *****************************************************************************/

void nodeLinkClass::bmc_command_done ( struct nodeLinkClass::node * node_ptr )
{
    node_ptr->bmc_thread_ctrl.done = true ;
}
