/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Wind River Titanium Cloud Maintenance IPMI Utilities
 */
#include <stdio.h>
#include <iostream>
#include <string.h>

using namespace std;

#include "nodeBase.h"      /* for ... mtce common definitions          */
#include "nodeClass.h"     /* for ...                                  */

/* IPMI Command strings */
const char mtc_ipmiRequest_str[IPMITOOL_THREAD_CMD__LAST][20] =
{
    "null",
    "Reset",
    "Power-On",
    "Power-Off",
    "Power-Cycle",
    "Query BMC Info",
    "Query Power Status",
    "Query Reset Reason"
};

const char * getIpmiCmd_str ( int command )
{
    if (( command > IPMITOOL_THREAD_CMD__NULL ) &&
        ( command < IPMITOOL_THREAD_CMD__LAST ))
    {
        return (&mtc_ipmiRequest_str[command][0]);
    }
    slog ("Invalid command (%d)\n", command );
    return (&mtc_ipmiRequest_str[IPMITOOL_THREAD_CMD__NULL][0]);
}

const char mtc_ipmiAction_str[IPMITOOL_THREAD_CMD__LAST][30] =
{
    "null",
    "resetting",
    "powering on",
    "powering off",
    "power cycling",
    "querying bmc info",
    "querying power status",
    "querying reset cause"
};

const char * getIpmiAction_str ( int command )
{
    if (( command > IPMITOOL_THREAD_CMD__NULL ) &&
        ( command < IPMITOOL_THREAD_CMD__LAST ))
    {
        return (&mtc_ipmiAction_str[command][0]);
    }
    slog ("Invalid command (%d)\n", command );
    return (&mtc_ipmiAction_str[IPMITOOL_THREAD_CMD__NULL][0]);
}

/*****************************************************************************
 *
 * Name       : ipmi_command_send
 *
 * Description: This utility starts the ipmitool command handling thread
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

int nodeLinkClass::ipmi_command_send ( struct nodeLinkClass::node * node_ptr, int command )
{
    int rc = PASS ;

    node_ptr->ipmitool_thread_info.command = command ;

    /* Update / Setup the BMC access credentials */
    node_ptr->thread_extra_info.bm_ip   = node_ptr->bm_ip   ;
    node_ptr->thread_extra_info.bm_un   = node_ptr->bm_un   ;
    node_ptr->thread_extra_info.bm_pw   = node_ptr->bm_pw   ;
    node_ptr->thread_extra_info.bm_type = node_ptr->bm_type ;

#ifdef WANT_FIT_TESTING
    {
        bool want_fit = false ;
        int fit = FIT_CODE__IPMI_COMMAND_SEND ;
        int command = node_ptr->ipmitool_thread_info.command ;
        if ( daemon_want_fit ( fit, node_ptr->hostname, "mc_info" ) == true )
        {
            want_fit = true ;
        }
        else if (( command == IPMITOOL_THREAD_CMD__POWER_STATUS ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_status" ) == true ))
        {
            want_fit = true ;
        }
        else if ( daemon_want_fit ( fit, node_ptr->hostname, "reset_cause" ) == true )
        {
            want_fit = true ;
        }
        else if (( command == IPMITOOL_THREAD_CMD__POWER_RESET ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "reset" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == IPMITOOL_THREAD_CMD__POWER_ON ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_on" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == IPMITOOL_THREAD_CMD__POWER_OFF ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_off" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == IPMITOOL_THREAD_CMD__POWER_CYCLE ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "power_cycle" ) == true ))
        {
            want_fit = true ;
        }
        else if (( command == IPMITOOL_THREAD_CMD__BOOTDEV_PXE ) &&
                 ( daemon_want_fit ( fit, node_ptr->hostname, "netboot_pxe" ) == true ))
        {
            want_fit = true ;
        }

        if ( want_fit == true )
        {
            slog ("%s FIT %s\n", node_ptr->hostname.c_str(), getIpmiCmd_str(command) );
            node_ptr->ipmitool_thread_info.status = node_ptr->ipmitool_thread_ctrl.status = rc = FAIL_FIT ;
            node_ptr->ipmitool_thread_info.status_string = "ipmi_command_send fault insertion failure" ;
            return ( rc );
        }
    }
#endif

    if (( hostUtil_is_valid_ip_addr ( node_ptr->thread_extra_info.bm_ip ) == true ) &&
        ( !node_ptr->thread_extra_info.bm_un.empty() ) &&
        ( !node_ptr->thread_extra_info.bm_pw.empty ()))
    {
        node_ptr->ipmitool_thread_ctrl.status = rc =
        thread_launch ( node_ptr->ipmitool_thread_ctrl,
                        node_ptr->ipmitool_thread_info ) ;
        if ( rc != PASS )
        {
            elog ("%s failed to launch power control thread (rc:%d)\n",
                      node_ptr->hostname.c_str(), rc );
        }
        else
        {
            dlog ("%s %s %s thread launched\n", node_ptr->hostname.c_str(),
                                                node_ptr->ipmitool_thread_ctrl.name.c_str(),
                                 getIpmiCmd_str(node_ptr->ipmitool_thread_info.command) );
        }
        node_ptr->ipmitool_thread_ctrl.retries = 0 ;
    }
    else
    {
        node_ptr->ipmitool_thread_ctrl.status = rc =
        node_ptr->ipmitool_thread_info.status = FAIL_INVALID_DATA ;
        node_ptr->ipmitool_thread_info.status_string = "one or more bmc credentials are invalid" ;

        wlog ("%s %s %s %s\n", node_ptr->hostname.c_str(),
                               hostUtil_is_valid_ip_addr ( node_ptr->thread_extra_info.bm_ip ) ? "" : "bm_ip:invalid",
                               node_ptr->thread_extra_info.bm_un.empty() ? "bm_un:empty" : "",
                               node_ptr->thread_extra_info.bm_pw.empty() ? "bm_pw:empty" : "");
    }

    return (rc);
}

/*****************************************************************************
 *
 * Name       : ipmi_command_recv
 *
 * Description: This utility will check for ipmitool command thread completion.
 *
 * Returns    : PASS       is returned if the thread reports done.
 *              RETRY      is returned if the thread has not completed.
 *              FAIL_RETRY is returned after 10 back-to-back calls return RETRY.
 *
 *****************************************************************************/

int nodeLinkClass::ipmi_command_recv ( struct nodeLinkClass::node * node_ptr )
{
    int rc = RETRY ;

    /* check for 'thread done' completion */
    if ( thread_done( node_ptr->ipmitool_thread_ctrl ) == true )
    {
        if (( rc = node_ptr->ipmitool_thread_info.status ) != PASS )
        {
            elog ("%s %s command failed (rc:%d)\n",
                      node_ptr->hostname.c_str(),
                      getIpmiCmd_str(node_ptr->ipmitool_thread_info.command),
                      rc );
        }
        else
        {
            if ( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_RESET )
            {
                if ( node_ptr->ipmitool_thread_info.data.find(IPMITOOL_POWER_RESET_RESP) == std::string::npos )
                    rc = FAIL_RESET_CONTROL ;
            }
            else if ( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_OFF )
            {
                if ( node_ptr->ipmitool_thread_info.data.find(IPMITOOL_POWER_OFF_RESP) == std::string::npos )
                    rc = FAIL_POWER_CONTROL ;
            }
            else if ( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_ON )
            {
                if ( node_ptr->ipmitool_thread_info.data.find(IPMITOOL_POWER_ON_RESP) == std::string::npos )
                    rc = FAIL_POWER_CONTROL ;
            }
            else if ( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_CYCLE )
            {
                if ( node_ptr->ipmitool_thread_info.data.find(IPMITOOL_POWER_CYCLE_RESP) == std::string::npos )
                    rc = FAIL_POWER_CONTROL ;
            }

            if ( rc )
            {
                node_ptr->ipmitool_thread_info.status = rc ;
                node_ptr->ipmitool_thread_info.status_string = ("power command failed");
                wlog ("%s %s Response: %s\n", node_ptr->hostname.c_str(),
                                              getIpmiCmd_str(node_ptr->ipmitool_thread_info.command),
                                              node_ptr->ipmitool_thread_info.data.c_str());

            }
            else
            {
                 blog ("%s %s Response: %s\n", node_ptr->hostname.c_str(),
                                               getIpmiCmd_str(node_ptr->ipmitool_thread_info.command),
                                               node_ptr->ipmitool_thread_info.data.c_str());
            }
        }

#ifdef WANT_FIT_TESTING
        if ( rc == PASS )
        {
            bool want_fit = false ;
            int fit = FIT_CODE__IPMI_COMMAND_RECV ;
            if ( daemon_want_fit ( fit, node_ptr->hostname, "mc_info" ) == true )
            {
                want_fit = true ;
            }
            else if ( daemon_want_fit ( fit, node_ptr->hostname, "reset_cause" ) == true )
            {
                want_fit = true ;
            }
            else if (( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_RESET ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "reset" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_ON ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "power_on" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_OFF ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "power_off" ) == true ))
            {
                want_fit = true ;
            }
            else if (( node_ptr->ipmitool_thread_info.command == IPMITOOL_THREAD_CMD__POWER_CYCLE ) &&
                     ( daemon_want_fit ( fit, node_ptr->hostname, "power_cycle" ) == true ))
            {
                want_fit = true ;
            }

            if ( want_fit == true )
            {
                node_ptr->ipmitool_thread_info.status = rc = FAIL_FIT ;
                node_ptr->ipmitool_thread_info.status_string = "ipmi_command_recv fault insertion failure" ;
            }
        }
#endif
    }

    /* handle max retries reached */
    else if ( node_ptr->ipmitool_thread_ctrl.retries++ >= IPMITOOL_MAX_RECV_RETRIES )
    {
        elog ("%s %s command timeout (%d of %d)\n",
                  node_ptr->hostname.c_str(),
                  getIpmiCmd_str(node_ptr->ipmitool_thread_info.command),
                  node_ptr->ipmitool_thread_ctrl.retries,
                  IPMITOOL_MAX_RECV_RETRIES);

        rc = FAIL_RETRY;
    }

    /* handle progressive retry */
    else
    {
        if ( node_ptr->ipmitool_thread_ctrl.id == 0 )
        {
            slog ("%s %s command not-running\n",
                      node_ptr->hostname.c_str(),
                      getIpmiCmd_str(node_ptr->ipmitool_thread_info.command));
            rc = FAIL_NOT_ACTIVE ;
        }
        else
        {
            ilog ("%s %s command in-progress (polling %d of %d)\n",
                      node_ptr->hostname.c_str(),
                      getIpmiCmd_str(node_ptr->ipmitool_thread_info.command),
                      node_ptr->ipmitool_thread_ctrl.retries,
                      IPMITOOL_MAX_RECV_RETRIES);
            rc = RETRY ;
        }
    }

    if ( rc != RETRY )
    {
        node_ptr->ipmitool_thread_ctrl.done = true ;
        node_ptr->ipmitool_thread_ctrl.retries = 0 ;
        node_ptr->ipmitool_thread_ctrl.id = 0 ;
        node_ptr->ipmitool_thread_info.id = 0 ;
        node_ptr->ipmitool_thread_info.command = 0 ;
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : ipmi_command_done
 *
 * Description: This utility frees the ipmitool command thread for next execution.
 *
 *****************************************************************************/

void nodeLinkClass::ipmi_command_done ( struct nodeLinkClass::node * node_ptr )
{
    node_ptr->ipmitool_thread_ctrl.done = true ;
}
