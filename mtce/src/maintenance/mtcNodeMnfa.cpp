/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance
 * "Multi-Node-Failure Avoidance feature utility implementation"
 *
 */

#include <sys/types.h>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <list>
#include <vector>

using namespace std;

#include "nodeBase.h"
#include "nodeClass.h"
#include "nodeTimers.h"
#include "mtcNodeHdlrs.h"

/* create a log of all the hosts that are in the mnfa pool */
void log_mnfa_pool ( std::list<string> & mnfa_awol_list )
{
    std::list<string>::iterator mnfa_awol_ptr  ;
    string pool_list = "" ;
    if ( mnfa_awol_list.size() )
    {
        for ( mnfa_awol_ptr = mnfa_awol_list.begin() ;
              mnfa_awol_ptr != mnfa_awol_list.end() ;
              mnfa_awol_ptr++ )
        {
            pool_list.append (" ");
            pool_list.append (mnfa_awol_ptr->data());
        }
        ilog ("MNFA POOL:%s\n", pool_list.c_str());
    }
}

/*****************************************************************************
 *
 * Name       : add_host_to_awol_list
 *
 * Description: Add a hostname to the awol list if its not already in the list
 *
 * Returns    : true if added
 *              false if not added because it is already in the list.
 *
 *****************************************************************************/

static bool add_host_to_awol_list ( string hostname, std::list<string> & mnfa_awol_list )
{
    std::list<string>::iterator mnfa_awol_ptr  ;
    for ( mnfa_awol_ptr = mnfa_awol_list.begin() ;
          mnfa_awol_ptr != mnfa_awol_list.end() ;
          mnfa_awol_ptr++ )
    {
        if ( *(mnfa_awol_ptr) == hostname )
        {
            /* already in list */
            return false ;
        }
    }
    mnfa_awol_list.push_back(hostname);
    return true ;
}

/*****************************************************************************
 *
 * Name       : mnfa_add_host
 *
 * Description: Add a failed host the the mnfa count and manage
 *              the failed list
 *
 *****************************************************************************/
void nodeLinkClass::mnfa_add_host ( struct nodeLinkClass::node * node_ptr , iface_enum iface )
{
    if (( this->hbs_failure_action == HBS_FAILURE_ACTION__ALARM ) ||
        ( this->hbs_failure_action == HBS_FAILURE_ACTION__NONE ))
    {
        /* Do nothing for the 'alarm only' or 'none' action.
         * Alarming is handled by the hbsAgent already */
        return ;
    }

    if ( node_ptr->hbs_minor[iface] == false )
    {
        bool enter = false ;
        bool added = false ;

        node_ptr->hbs_minor[iface] = true ;
        node_ptr->hbs_minor_count[iface]++ ;
        mnfa_host_count[iface]++;

        /* if we are active then add the node to the awol list */
        if ( mnfa_active == true )
        {
            /* once we are mnfa_active we need to give all the
             * hbs_minor=true hosts a graceful recovery token
             * mnfa_graceful_recovery = true and add to the awol list */
            node_ptr->mnfa_graceful_recovery = true ;
            added = true ;
            add_host_to_awol_list (node_ptr->hostname, mnfa_awol_list );
            if ( node_ptr->task != MTC_TASK_RECOVERY_WAIT )
                mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_WAIT );
        }
        else if (( mnfa_active == false ) &&
                 ( mnfa_host_count[iface] >= this->mnfa_threshold))
        {
            enter = true ;
        }

        ilog ("%s MNFA %s (%s) %d enabled hosts (threshold:%d) (%d:%s:%d) (%d:%s:%d)\n",
                 node_ptr->hostname.c_str(),
                 added ? "added to pool" : "new candidate",
                 get_iface_name_str(iface),
                 enabled_nodes(),
                 mnfa_threshold,
                 mnfa_host_count[MGMNT_IFACE],
                 get_iface_name_str(MGMNT_IFACE),
                 node_ptr->hbs_minor_count[MGMNT_IFACE],
                 mnfa_host_count[INFRA_IFACE],
                 get_iface_name_str(INFRA_IFACE),
                 node_ptr->hbs_minor_count[INFRA_IFACE]);

        log_mnfa_pool ( mnfa_awol_list );

        if ( enter == true )
        {
            mnfa_enter ();
        }
    }
}

/*****************************************************************************
 *
 * Name       : mnfa_recover_host
 *
 * Description: Recover a host that may or may not be in the mnfa
 *              pool by sending it into the graceful recover FSM.
 *
 *****************************************************************************/
void nodeLinkClass::mnfa_recover_host ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )
    {
        if ( node_ptr->degrade_mask == 0 )
        {
            availStatusChange ( node_ptr, MTC_AVAIL_STATUS__AVAILABLE );
        }
    }

    if ( node_ptr->mnfa_graceful_recovery == true )
    {
        /* Restart the heartbeat for this recovered host */
        // send_hbs_command ( node_ptr->hostname, MTC_RESTART_HBS );

        if ( node_ptr->adminAction != MTC_ADMIN_ACTION__RECOVER )
        {
            ilog ("%s graceful recovery from MNFA\n", node_ptr->hostname.c_str());
            recoveryStageChange ( node_ptr, MTC_RECOVERY__START );
            adminActionChange   ( node_ptr, MTC_ADMIN_ACTION__RECOVER );
        }
        else
        {
            wlog ("%s already gracefully recovering\n", node_ptr->hostname.c_str() );
        }
    }
}

/****************************************************************************
 *
 * Name       : mnfa_enter
 *
 * Description: Perform the operations required to enter mnfa mode
 *
 * These include ...
 *
 *  1. Send the backoff command to heartbeat service. This tells the
 *     heartbeat service to send heartbeat requests less frequently.
 *
 *  2. Set mode active
 *
 *  3. Store all the hosts that have failed into the mnfa_awol_list
 *
 *  4. Give each enabled host with hbs_minor=true the
 *     mnfa_graceful_recovery token
 *
 *  5. Start the MNFA Auto-Recovery timer with time based on the config
 *     setting mnfa_timeout
 *
 ****************************************************************************/
void nodeLinkClass::mnfa_enter ( void )
{
     wlog ("MNFA ENTER --> Entering Multi-Node Failure Avoidance\n");
     mtcAlarm_log ( active_controller_hostname , MTC_LOG_ID__EVENT_MNFA_ENTER );
     mnfa_active = true ;

     send_hbs_command ( my_hostname, MTC_BACKOFF_HBS );

     /* Handle the case where we are already trying to recover from a
      * previous mnfa but the failure case occurs again. If that
      * happens we need to cancel the timer that will issue
      * the period recovery command. */
     mtcTimer_reset ( mtcTimer_mnfa );

     /* Loop through inventory and recover each host that
      * remains in the hbs_minor state.
      * Clear heartbeat degrades */
     for ( struct node * ptr = head ;  ; ptr = ptr->next )
     {
         if ((( ptr->hbs_minor[MGMNT_IFACE] == true ) ||
              ( ptr->hbs_minor[INFRA_IFACE] == true )) &&
              ( ptr->operState == MTC_OPER_STATE__ENABLED ))
         {
             /* Give all the hosts in the mnfa list a graceful
              * recovery token mnfa_graceful_recovery = true
              * basically a get out of double reset free card */
             ptr->mnfa_graceful_recovery = true ;
             add_host_to_awol_list (ptr->hostname, mnfa_awol_list );
             if ( ptr->task != MTC_TASK_RECOVERY_WAIT )
                mtcInvApi_update_task ( ptr, MTC_TASK_RECOVERY_WAIT );
         }
         if (( ptr->next == NULL ) || ( ptr == tail ))
             break ;
     }

     if ( this->mnfa_timeout )
     {
         wlog ("MNFA Auto-Recovery in %d seconds\n",       this->mnfa_timeout);
         mtcTimer_start ( mtcTimer_mnfa, mtcTimer_handler, this->mnfa_timeout);
     }
     log_mnfa_pool ( mnfa_awol_list );
}

/****************************************************************************
 *
 * Name       : mnfa_exit
 *
 * Description: Perform the operations required to exit mnfa mode
 * These include ...
 *
 *  1. manage mnfa counters/oms
 *
 *  2. disable mnfa mode (mnfa_active = false)
 *
 *  3. Start the heartbeat recovery timer. This is a timer that
 *     adds a bit of debounce to the recovery.
 *     After MTC_MNFA_RECOVERY_TIMER time period mtce will send
 *     a command to the heartbeat service commanding it to
 *     re-instate the default/runtime heartbeat period.
 *
 *  4. Loop through all the enabled inventory and clear the heartbeat
 *     degrade conditions and issue a heartbeat restart to any
 *     hosts that remain in the hbs_minor state.
 *
 * if ( force == true )
 *    The mnfa_timeout has expired
 *    All hosts in the awol list are forced failed and into the
 *       enable_handler FSM.
 * else
 *    The mnfa recovery threshold has crossed
 *    Send all enabled hosts in the hbs_minor=true state into the
 *       graceful recovery FSM
 *
 ****************************************************************************/
void nodeLinkClass::mnfa_exit ( bool force )
{
    if ( mnfa_active == true )
    {
        mnfa_occurances++ ;
        mnfa_active = false ;
        if ( force == true )
        {
            elog ("... MNFA %d sec timeout - forcing full enable on ... \n",
                       this->mnfa_timeout);
        }

        wlog ("MNFA EXIT <-- Exiting Multi-Node Failure Avoidance %s\n",
                     force ? "(Auto-Recover)" : "");
        mtcAlarm_log ( active_controller_hostname , MTC_LOG_ID__EVENT_MNFA_EXIT );

        log_mnfa_pool ( mnfa_awol_list );

        /* Loop through inventory and recover each host that
         * remains in the hbs_minor state.
         * Clear heartbeat degrades */
        for ( struct node * ptr = head ;  ; ptr = ptr->next )
        {
            if ((( ptr->hbs_minor[INFRA_IFACE] == true ) ||
                 ( ptr->hbs_minor[MGMNT_IFACE] == true )) &&
                 ( ptr->operState == MTC_OPER_STATE__ENABLED ))
            {
                ptr->hbs_minor[MGMNT_IFACE] = false ;
                ptr->hbs_minor[INFRA_IFACE] = false ;

                if ( force == true )
                {
                    elog ("... %s failed ; auto-recovering\n",
                               ptr->hostname.c_str());

                    /* Set node as failed */
                    availStatusChange ( ptr, MTC_AVAIL_STATUS__FAILED );
                    enableStageChange ( ptr, MTC_ENABLE__START );
                    adminActionChange ( ptr, MTC_ADMIN_ACTION__NONE );
                }
                else
                {
                    if ( ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )
                    {
                        if ( ptr->degrade_mask == 0 )
                        {
                            availStatusChange ( ptr, MTC_AVAIL_STATUS__AVAILABLE );
                        }
                    }

                    if ( ptr->adminAction != MTC_ADMIN_ACTION__RECOVER )
                    {
                        recoveryStageChange ( ptr, MTC_RECOVERY__START );
                        adminActionChange   ( ptr, MTC_ADMIN_ACTION__RECOVER );
                    }
                    else
                    {
                        wlog ("%s already gracefully recovering\n", ptr->hostname.c_str() );
                    }
                }
            }
            if (( ptr->next == NULL ) || ( ptr == tail ))
                break ;
        }

        /* Stop the ... failure -> full enable ... window timer if it is active */
        mtcTimer_reset ( mtcTimer_mnfa );

        /* Start the timer that will eventually send the MTC_RECOVER_HBS command */
        mtcTimer_start ( mtcTimer_mnfa, mtcTimer_handler, MTC_MNFA_RECOVERY_TIMER );
    }
    else
    {
        send_hbs_command ( my_hostname, MTC_RECOVER_HBS );
    }

    mnfa_host_count[MGMNT_IFACE] = 0 ;
    mnfa_host_count[INFRA_IFACE] = 0 ;
    mnfa_awol_list.clear();
}

/****************************************************************************
 *
 * Name       : mnfa_cancel
 *
 * Description: Cancel MNFA if its active.
 *
 ****************************************************************************/
void nodeLinkClass::mnfa_cancel ( void )
{
    if ( this->mnfa_active )
    {
        wlog ("MNFA CANCEL --> Cancelling Multi-Node Failure Avoidance\n");

        mtcTimer_reset ( this->mtcTimer_mnfa );

        /* Loop through MNFA Pool.
         * Clear MNFA attributes from hosts in the pool. */
        std::list<string>::iterator mnfa_awol_ptr  ;
        for ( mnfa_awol_ptr = mnfa_awol_list.begin() ;
              mnfa_awol_ptr != mnfa_awol_list.end() ;
              mnfa_awol_ptr++ )
        {
            struct node * node_ptr = nodeLinkClass::getNode ( *(mnfa_awol_ptr) );
            if ( node_ptr != NULL )
            {
                node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_MGMNT ;
                node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_INFRA ;
                node_ptr->hbs_minor[INFRA_IFACE] = false ;
                node_ptr->hbs_minor[MGMNT_IFACE] = false ;
                node_ptr->mnfa_graceful_recovery = false ;
                mtcInvApi_update_task ( node_ptr, "" );
            }
        }
        send_hbs_command ( this->my_hostname, MTC_RECOVER_HBS );
        this->mnfa_host_count[MGMNT_IFACE] = 0 ;
        this->mnfa_host_count[INFRA_IFACE] = 0 ;
        this->mnfa_active = false ;
    }
    mnfa_awol_list.clear();
}
