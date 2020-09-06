/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/***************************************************************************
 *
 * @file
 * Wind River CGTS Platform Node Maintenance "Finite State Machine"
 *
 * Description: This FSM follows the X.731 specification.
 *
 * The FSM manages nodes based on the following three perspectives
 *
 * Administrative: action taken on node (mtc_nodeAdministrative_action_type)
 * Operational   : state of the node mtc_nodeOperational_state_type)
 * Availability  : status of current node state (mtc_nodeAvailability_status_type)
 *
 */

using namespace std;

#define __AREA__ "fsm"

#include "nodeClass.h"
#include "tokenUtil.h"
#include "mtcNodeFsm.h"
#include "mtcInvApi.h"
#include "mtcNodeMsg.h"
#include "mtcNodeHdlrs.h"   /* for ... mtcTimer_handl                  */

int nodeLinkClass::fsm ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    if ( node_ptr == NULL )
    {
        slog ("Null Node Pointer\n");
        return FAIL ;
    }

    /* handle clear task request */
    if ( node_ptr->clear_task == true )
    {
        mtcInvApi_update_task ( node_ptr, "" );
        node_ptr->clear_task = false ;
    }

    /* Service the libEvent work queue */
    workQueue_process ( node_ptr ) ;

    /* Service the maintenance command queue if there are commands waiting */
    if ( node_ptr->mtcCmd_work_fifo.size())
    {
         rc = nodeLinkClass::cmd_handler ( node_ptr );
         if ( rc == RETRY )
         {
             return (rc);
         }
    }

    /* Monitor and Manage active threads */
    thread_handler ( node_ptr->bmc_thread_ctrl, node_ptr->bmc_thread_info );

    /* manage the host connected state and board management alarms */
    nodeLinkClass::bmc_handler ( node_ptr );

    /* manage host's degrade state */
    nodeLinkClass::degrade_handler ( node_ptr );

    /*
     * Always run the offline handler
     *
     * - does nothing unless in fault handling mode
     * - looks for offline state during fault handling
     */
    nodeLinkClass::offline_handler ( node_ptr );

    /*
     * Always run the online handler.
     *
     * - handles offline/online state transitions based on periodic audit
     *   with mtcAlive debouncing
     */
    nodeLinkClass::online_handler ( node_ptr );

    if ( node_ptr->adminAction == MTC_ADMIN_ACTION__DELETE )
    {
        flog ("%s -> Delete Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::delete_handler ( node_ptr );
        return (PASS);
    }


    /* Run the config FSM if the configAction bool is set.
     * We keep this as a separate action unto itself so that
     * mtce can continue to service all other actions for the
     * same host while it handles configuration commands */
    if (( node_ptr->configAction == MTC_CONFIG_ACTION__INSTALL_PASSWD ) ||
        ( node_ptr->configAction == MTC_CONFIG_ACTION__CHANGE_PASSWD ) ||
        ( node_ptr->configAction == MTC_CONFIG_ACTION__CHANGE_PASSWD_AGAIN ))
    {
        nodeLinkClass::cfg_handler ( node_ptr );
    }

    /****************************************************************************
     * No Op: Do nothing for this Healthy Enabled Running Host
     * This block of code was added to resolve an issue.  With this change:
     * the insv_test_handler gets run as soon as a host's main function is enabled.
     ****************************************************************************
     */
    if (( node_ptr->ar_disabled == false ) &&
        ( node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
        ( node_ptr->operState   == MTC_OPER_STATE__ENABLED )   &&
        ((node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
         (node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )))
    {
        // flog ("%s -> insv_test_handler\n", node_ptr->hostname.c_str());
        nodeLinkClass::insv_test_handler ( node_ptr );
    }

    /****************************************************************************
     * Add Host Services:
     ****************************************************************************
     */
    if ( node_ptr->adminAction == MTC_ADMIN_ACTION__ADD )
    {
        flog ("%s -> Add Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::add_handler ( node_ptr );
    }


    /****************************************************************************
     * No Op: Do nothing for this Healthy Enabled Running Host
     ****************************************************************************
     */
    else if (( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE )    &&
             ( node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
             ( node_ptr->operState   == MTC_OPER_STATE__ENABLED )   &&
             ((node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
              (node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )))
    {
        // flog ("%s -> oos_test_handler\n", node_ptr->hostname.c_str());
        nodeLinkClass::oos_test_handler  ( node_ptr );
    }

    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWERCYCLE )
    {
        nodeLinkClass::powercycle_handler ( node_ptr );
    }

    /****************************************************************************
     * Reset Host: Run the Reset handler for this Reset Action on Locked Host
     ****************************************************************************
     */
    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__RESET )
    {
        flog ("%s -> Reset Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::reset_handler ( node_ptr );
        nodeLinkClass::oos_test_handler ( node_ptr );
    }

    /****************************************************************************
     * Reboot Host: Run the Reboot handler for this Reboot Action on Locked Host
     ****************************************************************************
     */
    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__REBOOT )
    {
        flog ("%s -> Reboot Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::reboot_handler ( node_ptr );
    }

    /****************************************************************************
     * Recovering Host: Run Enable handler for failed or recovering host
     ****************************************************************************
     */
    else if ((( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE )   &&
              ( node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
              ( node_ptr->operState  == MTC_OPER_STATE__ENABLED )    &&
              ( node_ptr->availStatus == MTC_AVAIL_STATUS__FAILED )) ||
             ( node_ptr->adminAction == MTC_ADMIN_ACTION__ENABLE))
    {
        flog ("%s -> Run Enable Handler\n", node_ptr->hostname.c_str());
        nodeLinkClass::enable_handler ( node_ptr );
    }

        /* Do nothing with locked disabled offline state */
    else if (( node_ptr->adminAction  == MTC_ADMIN_ACTION__NONE ) &&
             ( node_ptr->adminState   == MTC_ADMIN_STATE__LOCKED ) &&
             ( node_ptr->operState    == MTC_OPER_STATE__DISABLED ) &&
             (( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE ) ||
              ( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE ) ||
              ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFDUTY ) ||
              ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF )))
    {
       flog ("%s -> Run OOS Test Handler\n", node_ptr->hostname.c_str());
        nodeLinkClass::oos_test_handler ( node_ptr );
    }

    /****************************************************************************
     * Recovering Host: Run Recovery handler for failed or recovering host
     ****************************************************************************
     */
    else if (( node_ptr->adminAction == MTC_ADMIN_ACTION__RECOVER ) &&
             ( node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ))
    {
        flog ("%s -> Run Recovery\n", node_ptr->hostname.c_str());
        nodeLinkClass::recovery_handler ( node_ptr );
    }

    /****************************************************************************
     * Recovering Host: Run Enable handler for failed or recovering host
     ****************************************************************************
     */
    else if ( ( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE ) &&
              ( node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
              ( node_ptr->operState  == MTC_OPER_STATE__DISABLED ) &&
             (( node_ptr->availStatus == MTC_AVAIL_STATUS__FAILED )  ||
              ( node_ptr->availStatus == MTC_AVAIL_STATUS__INTEST )  ||
              ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE ) ||
              ( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE )))
    {
        flog ("%s -> Run Enable\n", node_ptr->hostname.c_str());
        nodeLinkClass::enable_handler ( node_ptr );
    }

    /* Try and recover an accidentally powered of host */
    else if (( node_ptr->adminAction  == MTC_ADMIN_ACTION__NONE ) &&
             ( node_ptr->adminState   == MTC_ADMIN_STATE__UNLOCKED ) &&
             ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) &&
             ( node_ptr->hwmon_powercycle.attempts == 0 ) &&
             ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__INIT ))
    {
        ilog ("%s auto-poweron for unlocked host\n", node_ptr->hostname.c_str());
        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__POWERON );

        /* FSM sanity check below will reject this operation, need exit now */
        return (PASS);
    }

    /****************************************************************************
     * Unlock Host: Run Enable handler for the Unlock Action
     ***************************************************************************/
    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__UNLOCK )
    {
        flog ("%s -> Unlock Action\n", node_ptr->hostname.c_str());

        /* Proceed to unlock host */
        nodeLinkClass::enable_handler ( node_ptr );
    }

    /****************************************************************************
     * Run the Subfunction FSM, usually after the ADD or at the end of the enable
     * in a small system.
     ****************************************************************************/
    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__ENABLE_SUBF )
    {
        flog ("%s -> Running SubFunction Enable handler (%d)\n",
                     node_ptr->hostname.c_str(),
                     node_ptr->enableStage );

        nodeLinkClass::enable_subf_handler ( node_ptr );
    }

    /****************************************************************************
     * Lock Host: Run Disable handler for the Lock Action
     ****************************************************************************
     */
    else if (( node_ptr->adminAction == MTC_ADMIN_ACTION__LOCK ) ||
             ( node_ptr->adminAction == MTC_ADMIN_ACTION__FORCE_LOCK ))
    {
        // flog ("%s -> Lock Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::disable_handler ( node_ptr );
    }

    /****************************************************************************
     * Semantic Handling: Reject Recovery Actions Against In-Service Host
     ****************************************************************************
     */
    else if ((  node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
             (( node_ptr->adminAction == MTC_ADMIN_ACTION__POWEROFF ) ||
              ( node_ptr->adminAction == MTC_ADMIN_ACTION__RESET    ) ||
              ( node_ptr->adminAction == MTC_ADMIN_ACTION__REBOOT   ) ||
              ( node_ptr->adminAction == MTC_ADMIN_ACTION__REINSTALL   )))
    {
        flog ("%s -> OOS Action Check\n", node_ptr->hostname.c_str());

        elog ("%s Administrative '%s' Operation Rejected\n",
              node_ptr->hostname.c_str(),
              get_adminAction_str (node_ptr->adminAction) );

        elog ("%s Cannot perform out-of-service action against in-service host\n",
              node_ptr->hostname.c_str());
        adminActionChange ( node_ptr , MTC_ADMIN_ACTION__NONE );

        /* Clear the UI task since we are not really taking this action */
        mtcInvApi_update_task ( node_ptr, "" );
    }

    /****************************************************************************
     * Reload Host: Run the Reload handler to Nuke the disk on Locked Host
     ****************************************************************************
     */
    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__REINSTALL )
    {
        flog ("%s -> Reload Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::reinstall_handler ( node_ptr );
    }

    /****************************************************************************
     * No Op: Do nothing for this Healthy Enabled Locked CPE Simplex Host
     ****************************************************************************
     */
    else if (( this->system_type == SYSTEM_TYPE__CPE_MODE__SIMPLEX ) &&
             ( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE ) &&
             ( node_ptr->adminState  == MTC_ADMIN_STATE__LOCKED ))
    {
        nodeLinkClass::insv_test_handler ( node_ptr );
        nodeLinkClass::oos_test_handler  ( node_ptr );
    }

    /****************************************************************************
     * Power-Off Host:
     ****************************************************************************
     */
    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWEROFF )
    {
        flog ("%s -> Power-Off Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::power_handler ( node_ptr );
        nodeLinkClass::oos_test_handler ( node_ptr );
    }

    /****************************************************************************
     * Power-On Host:
     ****************************************************************************
     */
    else if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWERON )
    {
        flog ("%s -> Power-On Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::power_handler ( node_ptr );
        nodeLinkClass::oos_test_handler ( node_ptr );
    }

    /****************************************************************************
     * Swact Host Services:
     ****************************************************************************
     */
    else if (( node_ptr->adminAction == MTC_ADMIN_ACTION__SWACT ) ||
             ( node_ptr->adminAction == MTC_ADMIN_ACTION__FORCE_SWACT ))

    {
        flog ("%s -> Swact Action\n", node_ptr->hostname.c_str());
        nodeLinkClass::swact_handler ( node_ptr );
    }

    /***** DEGRADED Cases *******/

    /* Handle the degrade action */
    else if (( node_ptr->adminAction == MTC_ADMIN_ACTION__NONE ) &&
             ( node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
             ( node_ptr->operState   == MTC_OPER_STATE__ENABLED ) &&
             ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED ))
    {
        /* We do nothing, the in service test catches this */
        // flog ("%s -> Degrade Recovery\n", node_ptr->hostname.c_str());
        ; // nodeLinkClass::degrade_handler ( node_ptr );
    }

    else
    {
        if (( node_ptr->adminState  >= MTC_ADMIN_STATES ) ||
            ( node_ptr->operState   >= MTC_OPER_STATES  ) ||
            ( node_ptr->availStatus >= MTC_AVAIL_STATUS ))
        {
            elog ("Unhandled FSM Case: %s %d-%d-%d\n",
                    node_ptr->hostname.c_str(),
                    node_ptr->adminState,
                    node_ptr->operState,
                    node_ptr->availStatus );
        }
        else
        {
            wlog ("Unsupported FSM State: %s Action:%s %s-%s-%s ; auto-correcting ...\n",
                    node_ptr->hostname.c_str(),
                    get_adminAction_str ( node_ptr->adminAction ),
                    adminState_enum_to_str (node_ptr->adminState).c_str(),
                    operState_enum_to_str (node_ptr->operState).c_str(),
                    availStatus_enum_to_str (node_ptr->availStatus).c_str());

        }
        /* Unlocked state overrides unsupported oper-avail states
         * Try to recover the host */
        if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
        {
            /* Reset the state in the database for these error states */
            node_ptr->adminState = MTC_ADMIN_STATE__UNLOCKED ;
            node_ptr->operState  = MTC_OPER_STATE__DISABLED ;
            node_ptr->availStatus = MTC_AVAIL_STATUS__ONLINE ;
            mtcInvApi_update_states ( node_ptr, "unlocked", "disabled" , "online" );

            /* Force the action */
            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__UNLOCK );

        }
        else
        {
            /* Reset the state in the database for these error states */
            node_ptr->adminState = MTC_ADMIN_STATE__LOCKED ;
            node_ptr->operState = MTC_OPER_STATE__DISABLED ;
            node_ptr->availStatus = MTC_AVAIL_STATUS__OFFLINE ;
            mtcInvApi_update_states ( node_ptr, "locked", "disabled" , "offline" );

            /* Force the action */
            adminActionChange ( node_ptr , MTC_ADMIN_ACTION__FORCE_LOCK );
        }
        return (PASS);
    }

    return (rc) ;
}
