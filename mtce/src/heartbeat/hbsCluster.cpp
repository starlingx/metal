/*
 * Copyright (c) 2018 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file Maintenance Heartbeat Agent Cluster Manager Module
 *
 *************************************************************************
 *
 * This module provides the heartbeat cluster implementation member
 * functions that the hbsAgent service calls to collect, store and
 * send heartbeat cluster information to SM upon request.
 *
 * See mtceHbsCluster.h for formal API between SM and Mtce.
 *
 *************************************************************************/

using namespace std;

#include "nodeBase.h"      /* common maintenance constructs and definitions */
#include "daemon_common.h" /* common daemon constructs and definitions      */
#include "hbsBase.h"       /* mtce heartbeat constructs and definitions     */

/* Error log throttle counter. */
#define THROTTLE_COUNT (500)

/* Private Heartbeat Cluster Control Structure. */
typedef struct
{
    /* Contains the controller number (0 or 1) for this controller. */
    unsigned short this_controller ;

    /* Preserves which controllers are enabled. */
    bool controller_0_enabled ;
    bool controller_1_enabled ;
#ifdef THREE_CONTROLLER_SYSTEM
    bool controller_2_enabled ;
#endif

    /* Used to prevent log flooding in presence of back to back errors. */
    unsigned int log_throttle ;

    /* Used to threshold storage-0 not responding state */
    unsigned int storage_0_not_responding_count[MTCE_HBS_NETWORKS];

    /* Contains the number of monitored networks in the system.
     * Management only = 1
     * Management and Inrastructure = 2 */
    unsigned short monitored_networks ;

    /* This contains the current number of heartbeat enabled hosts.
     *
     * Used to improve performance.
     *
     * Performance: This value is included in each history entry so
     * rather than do the size calculation of monitored_hostname_list
     * each time, this variable is updated from monitored_hostname_list
     * after each add/del operation. */
    unsigned short monitored_hosts ;

    /* List of host names being monitored. */
    std::list<string>monitored_hostname_list ;

    /* The working heartbeat cluster data vault. */
    mtce_hbs_cluster_type cluster ;

} hbs_cluster_ctrl_type ;

/* Cluster control structire construct allocation. */
static hbs_cluster_ctrl_type ctrl ;


/****************************************************************************
 *
 * Name        : hbs_cluster_init
 *
 * Description : Initialize the cluster structure to default values.
 *
 * Assumtions  : Called by hbsAgent.cpp before entering the main loop.
 *
 ***************************************************************************/

void hbs_cluster_init ( unsigned short period )
{
    ctrl.monitored_hosts = 0;
    ctrl.monitored_hostname_list.clear();

    /* Init the cluster - header. */
    ctrl.cluster.version  = MTCE_HBS_CLUSTER_VERSION  ;
    ctrl.cluster.revision = MTCE_HBS_CLUSTER_REVISION ;
    ctrl.cluster.magic_number = MTCE_HBS_MAGIC_NUMBER ;

    /* Init the cluster - global / dynamic data. */
    ctrl.cluster.reqid = 0 ;
    ctrl.cluster.period_msec = period ;
    ctrl.cluster.storage0_enabled = false ;
    ctrl.cluster.histories = 0 ;
    ctrl.cluster.bytes = BYTES_IN_CLUSTER_VAULT(ctrl.cluster.histories);

    /* The storage-0 thresholding counter for each network. */
    for ( int n = 0 ; n < MTCE_HBS_NETWORKS ; n++ )
        ctrl.storage_0_not_responding_count[n] = 0 ;

    for ( int h = 0 ; h < MTCE_HBS_MAX_HISTORY_ELEMENTS ; h++ )
        hbs_cluster_history_init ( ctrl.cluster.history[h] );

    ilog ("Cluster Info: v%d.%d sig:%x bytes:%d (%ld)",
             ctrl.cluster.version,
             ctrl.cluster.revision,
             ctrl.cluster.magic_number,
             ctrl.cluster.bytes,
             sizeof(mtce_hbs_cluster_history_type));

    ctrl.log_throttle = 0 ;
}


/****************************************************************************
 *
 * Name        : hbs_cluster_nums
 *
 * Description : Set this controller number and the number of monitored
 *               networks in this system.
 *
 *               These values do not change without a process restart.
 *
 * Assumtions  : Called by hbsAgent.cpp before entering the main loop.
 *
 * Returns     : None
 *
 ***************************************************************************/

void hbs_cluster_nums ( unsigned short this_controller,
                        unsigned short monitored_networks )
{
   ctrl.this_controller = this_controller ;
   ctrl.monitored_networks = monitored_networks ;
}


/****************************************************************************
 *
 * Name        : log_monitored_hosts_list
 *
 * Description : Log the list of monitored hosts.
 *               Typically done on a list change.
 *
 * Returns     : None
 *
 ***************************************************************************/

void log_monitored_hosts_list ( void )
{
    std::list<string>::iterator iter_ptr  ;
    string list = "" ;
    for ( iter_ptr = ctrl.monitored_hostname_list.begin() ;
          iter_ptr != ctrl.monitored_hostname_list.end() ;
          iter_ptr++ )
    {
        list.append (*(iter_ptr));
        list.append (" ");
    }
    ilog ("cluster of %ld: %s",
           ctrl.monitored_hostname_list.size(),
           list.c_str());
}


/****************************************************************************
 *
 * Name        : cluster_storage0_state
 *
 * Description : Record the heartbeat monitoring state of storage-0.
 *
 * Parameters  : true  if storage-0 heartbeating is in the 'started' state.
 *               false if storage-0 heartbeating is in the 'stopped' state.
 *
 * Returns     : None
 *
 ***************************************************************************/

void cluster_storage0_state ( bool enabled )
{
    if ( ctrl.cluster.storage0_enabled != enabled )
    {
        ctrl.cluster.storage0_enabled = enabled ;
        ilog ("storage-0 heartbeat state changed to %s",
                enabled ? "enabled" : "disabled" );
    }
}


/****************************************************************************
 *
 * Name        : hbs_manage_controller_state
 *
 * Description : Track the monitored enabled state of the controllers.
 *
 ***************************************************************************/

void hbs_manage_controller_state ( string & hostname, bool enabled )
{
    /* track controller state */
    if ( hostname == CONTROLLER_0 )
    {
        ctrl.controller_0_enabled = enabled ;
    }
    else if ( hostname == CONTROLLER_1 )
    {
        ctrl.controller_1_enabled = enabled ;
    }
#ifdef THREE_CONTROLLER_SYSTEM
    else if ( hostname == CONTROLLER_2 )
    {
        ctrl.controller_2_enabled = enabled ;
    }
#endif
}


/****************************************************************************
 *
 * Name        : hbs_cluster_add
 *
 * Description : Add the specified hostname to the enabled hosts list.
 *
 * Updates     : hostname is added to monitored_hostname_list
 *
 *               If added host is storage-0 then update its enabled status.
 *               if added host is a controller then update controller state.
 *
 * Parameters  : hostname string
 *
 * Updates     : monitored_hostname_list
 *
 ***************************************************************************/

void hbs_cluster_add ( string & hostname )
{
    /* Consider using 'unique' after instead of remove before update. */
    ctrl.monitored_hostname_list.remove(hostname) ;
    ctrl.monitored_hostname_list.push_back(hostname) ;
    ctrl.monitored_hosts = (unsigned short)ctrl.monitored_hostname_list.size();

    /* Manage storage-0 state */
    if ( hostname == STORAGE_0 )
    {
        cluster_storage0_state ( true );
    }

    /* If we get down to 0 monitored hosts then just start fresh */
    if (( ctrl.monitored_hosts ) == 0 )
    {
        hbs_cluster_init ( ctrl.cluster.period_msec );
    }

    /* Manage controller state ; true means enabled in this case. */
    hbs_manage_controller_state ( hostname, true );

    ilog ("%s added to cluster", hostname.c_str());

    log_monitored_hosts_list ();
}

/****************************************************************************
 *
 * Name        : hbs_cluster_del
 *
 * Description : Delete the specified hostname from the enabled hosts list.
 *
 * Updates     : hostname is removed from monitored_hostname_list
 *
 *               If added host is storage-0 then update its enabled status.
 *               if added host is a controller then update controller count.
 *
 * Parameters  : hostname string
 *
 * Updates     : monitored_hostname_list
 *
 ***************************************************************************/

void hbs_cluster_del ( string & hostname )
{
    ctrl.monitored_hostname_list.remove(hostname) ;
    ctrl.monitored_hosts = (unsigned short)ctrl.monitored_hostname_list.size();

    /* Manage storage-0 state. */
    if ( hostname == STORAGE_0 )
    {
        cluster_storage0_state ( false );
    }

    /* If we get down to 0 monitored hosts then just start fresh */
    if (( ctrl.monitored_hosts ) == 0 )
    {
        hbs_cluster_init ( ctrl.cluster.period_msec );
    }

    /* Manage controller state ; false means not enabled in this case. */
    hbs_manage_controller_state ( hostname , false );

    ilog ("%s deleted from cluster", hostname.c_str());

    log_monitored_hosts_list ();
}

/****************************************************************************
 *
 * Name        : hbs_cluster_update
 *
 * Description : Update this controller's cluster info for the specified
 *               network with
 *
 *               1. The number of enabled hosts.
 *               2. The number of responding hosts.
 *               3. The oldest history index in the rotational history fifo.
 *               4. Maintain a back to back non-responding count for storage-0.
 *                  Once the count reaches the minimum threshold of
 *                  STORAGE_0_NR_THRESHOLD then the specific network history
 *                  is updated to indicate storgae-0 is not responding. Once
 *                  storage-0 starts responding again with a single response
 *                  then that network history is updated to indicate storage-0
 *                  is responding.
 *
 * Assumptions : Converts heartbeat interface number to cluster network number.
 *
 * Parameters  : heartbeat interface number ( iface_enum )
 *               network index
 *               number of not responding hosts for this interval
 *
 * Updates     : This and last history as well as storage-0 not responding
 *               count.
 *
 ***************************************************************************/

#define STORAGE_0_NR_THRESHOLD (4)

void hbs_cluster_update ( iface_enum iface,
                      unsigned short not_responding_hosts,
                                bool storage_0_responding )
{
    if ( ctrl.monitored_hosts == 0 )
        return ;

    /* convert heartbeat iface enum to cluster network enum. */
    mtce_hbs_network_enum n ;
    if ( iface == MGMNT_IFACE )
        n = MTCE_HBS_NETWORK_MGMT ;
    else if ( iface == INFRA_IFACE )
        n = MTCE_HBS_NETWORK_INFRA ;
#ifdef MONITORED_OAM_NETWORK
    else if ( iface == OAM_IFACE )
        n = MTCE_HBS_NETWORK_OAM ;
#endif
    else
        return ;

    if ( not_responding_hosts )
    {
        clog1 ("controller-%d %s enabled:%d not responding:%d",
               ctrl.this_controller,
               hbs_cluster_network_name(n).c_str(),
               ctrl.monitored_hosts,
               not_responding_hosts);
    }
    else
    {
        clog1 ("controller-%d %s has %d monitored hosts and all are responding",
               ctrl.this_controller,
               hbs_cluster_network_name(n).c_str(),
               ctrl.monitored_hosts);
    }

    /* Look-up active history array for this network combination */
    mtce_hbs_cluster_history_type * history_ptr = NULL ;
    GET_CLUSTER_HISTORY_PTR(ctrl.cluster, ctrl.this_controller ,n);
    if ( history_ptr == NULL )
    {
        if ( ctrl.cluster.histories >= MTCE_HBS_MAX_HISTORY_ELEMENTS )
        {
            /* Should never happen but if it does then log without floooding */
            wlog_throttled ( ctrl.log_throttle, THROTTLE_COUNT,
                             "Unable to store history beyond %d ",
                             ctrl.cluster.histories );
            return ;
        }
        else
        {
            /* Adding a new history slot. */
            history_ptr = &ctrl.cluster.history[ctrl.cluster.histories] ;
            ctrl.cluster.histories++ ;
            ctrl.cluster.bytes = BYTES_IN_CLUSTER_VAULT(ctrl.cluster.histories);
            history_ptr->controller = ctrl.this_controller ;
            history_ptr->network = n ;

            /* Log new network history as its being started. */
            ilog ("controller-%d %s network history add",
                   ctrl.this_controller,
                   hbs_cluster_network_name(n).c_str());
        }
    }

    /* Manage storage-0 status. */
    if ( ctrl.cluster.storage0_enabled )
    {
        /* Handle storage-0 status change from not responding to responding. */
        if ( storage_0_responding == true )
        {
            if (history_ptr->storage0_responding == false)
            {
                history_ptr->storage0_responding = true ;
                ilog ("controller-%d %s heartbeat ; storage-0 is ok",
                   ctrl.this_controller,
                   hbs_cluster_network_name(n).c_str());
            }
            if (ctrl.storage_0_not_responding_count[n])
                ctrl.storage_0_not_responding_count[n] = 0 ;
        }
        /* Count the storage-0 not responding case for this network. */
        else
        {
            ctrl.storage_0_not_responding_count[n]++ ;
            if ( ctrl.storage_0_not_responding_count[n] == 2 )
            {
                ilog ("controller-%d %s heartbeat ; storage-0 has 2 misses",
                       ctrl.this_controller,
                       hbs_cluster_network_name(n).c_str() );
            }
        }

        /* Handle storage-0 status change from responding to not responding. */
        if (( history_ptr->storage0_responding == true ) &&
            ( ctrl.storage_0_not_responding_count[n] >= STORAGE_0_NR_THRESHOLD ))
        {
            history_ptr->storage0_responding = false ;
            ilog ("controller-%d %s heartbeat ; storage-0 is not responding",
                   ctrl.this_controller,
                   hbs_cluster_network_name(n).c_str() );
        }
    }
    else
    {
        /* Typical path for storage-0 disabled or normal non-storage system case */
        if ( history_ptr->storage0_responding == true )
            history_ptr->storage0_responding = false ;

        /* Handle clearing threshold count when storage-0 is not enabled. */
        if ( ctrl.storage_0_not_responding_count[n] )
            ctrl.storage_0_not_responding_count[n] = 0 ;
    }

    /*
     * Manage the history entry index.
     *
     * Get the previous entry index ...
     * ... which is the one before the oldest index.
     * ... which is the index for the next entry.
     */
    unsigned short last_entry_index ;
    if ( history_ptr->oldest_entry_index == 0 )
    {
        /* Go to the end of the array. */
        last_entry_index = MTCE_HBS_HISTORY_ENTRIES-1 ;
    }
    else
    {
        /* Otherwise, the previous index in the array */
        last_entry_index = history_ptr->oldest_entry_index - 1 ;
    }

    /* Update the history with this data. */
    history_ptr->entry[history_ptr->oldest_entry_index].hosts_enabled = ctrl.monitored_hosts ;
    history_ptr->entry[history_ptr->oldest_entry_index].hosts_responding = ctrl.monitored_hosts - not_responding_hosts ;

    if (( history_ptr->entry[history_ptr->oldest_entry_index].hosts_enabled !=
          history_ptr->entry[               last_entry_index].hosts_enabled ) ||
        ( history_ptr->entry[history_ptr->oldest_entry_index].hosts_responding !=
          history_ptr->entry[               last_entry_index].hosts_responding))
    {
        /* Only log on change events. */
        if ( history_ptr->entry[history_ptr->oldest_entry_index].hosts_enabled ==
             history_ptr->entry[history_ptr->oldest_entry_index].hosts_responding )
        {
            ilog ("controller-%d %s cluster of %d is healthy",
                   ctrl.this_controller,
                   hbs_cluster_network_name(n).c_str(),
                   history_ptr->entry[history_ptr->oldest_entry_index].hosts_enabled);
        }
        else
        {
            ilog ("controller-%d %s cluster of %d with %d responding",
                   ctrl.this_controller,
                   hbs_cluster_network_name(n).c_str(),
                   history_ptr->entry[history_ptr->oldest_entry_index].hosts_enabled,
                   history_ptr->entry[history_ptr->oldest_entry_index].hosts_responding);
        }
    }

    /* Increment the entries count till it reaches the max. */
    if ( history_ptr->entries < MTCE_HBS_HISTORY_ENTRIES )
        history_ptr->entries++ ;

    /* Manage the next entry update index ; aka the oldest index. */
    if ( history_ptr->oldest_entry_index == (MTCE_HBS_HISTORY_ENTRIES-1))
        history_ptr->oldest_entry_index = 0 ;
    else
        history_ptr->oldest_entry_index++ ;

    /* clear the log throttle if we are updating history ok. */
    ctrl.log_throttle = 0 ;
}

/****************************************************************************
 *
 * Name        : hbs_cluster_append
 *
 * Description : Add this controller's cluster info to this pulse
 *               request message.
 *
 ***************************************************************************/

void hbs_cluster_append ( hbs_message_type & msg )
{
    unsigned short c = ctrl.this_controller   ;

    CHECK_CTRL_NTWK_PARMS(c, ctrl.monitored_networks);

    msg.cluster.version          = ctrl.cluster.version ;
    msg.cluster.revision         = ctrl.cluster.revision ;
    msg.cluster.magic_number     = ctrl.cluster.magic_number ;
    msg.cluster.period_msec      = ctrl.cluster.period_msec ;
    msg.cluster.storage0_enabled = ctrl.cluster.storage0_enabled ;
    msg.cluster.histories        = ctrl.cluster.histories ;

    int bytes = BYTES_IN_CLUSTER_VAULT(ctrl.monitored_networks);

    clog1 ("controller-%d appending cluster info to heartbeat message (%d:%d:%d)",
            c, ctrl.monitored_networks, ctrl.cluster.histories, bytes );

    /* Copy the cluster into the message. */
    memcpy( &msg.cluster.history[0], &ctrl.cluster.history[c], bytes);
}

/****************************************************************************
 *
 * Name        : hbs_cluster_unused_bytes
 *
 * Descrition  : Used to set how much data to send in the heartbeat pulse
 *               requests.
 *
 * Returns     : The number of bytes that are not used in the full
 *               history array cluster structure.
 *
 ***************************************************************************/

unsigned short hbs_cluster_unused_bytes ( void )
{
    if ( ctrl.cluster.histories <= MTCE_HBS_MAX_HISTORY_ELEMENTS )
    {
        unsigned short tmp = MTCE_HBS_MAX_HISTORY_ELEMENTS - ctrl.cluster.histories ;
        return((unsigned short)(sizeof(mtce_hbs_cluster_history_type)*tmp)) ;
    }
    return 0;
}


/****************************************************************************
 *
 * Name       : hbs_cluster_send
 *
 * Description: Send the cluster vault to SM.
 *
 * Returns    : Nothing
 *
 ***************************************************************************/

/* NOTE: All code wrapped in this directive will be removed once
 *       active/active heartbeating is delivered in next update */
#define WANT_ACTIVE_ACTIVE_HEARTBEAT_RESULTS

void hbs_cluster_send ( msgClassSock * sm_client_sock, int reqid )
{

#ifdef  WANT_ACTIVE_ACTIVE_HEARTBEAT_RESULTS

    /* To assist SM with duplex integration ...
     *
     * This code emulates heartbeat redundancy by duplicating
     * controller history up to the number of provisioned
     * controllers until active-active heartbeat is delivered.
     */
    int peer_controller ;
    bool copy_cluster = false ;
    if ( ctrl.this_controller == 0 )
    {
        peer_controller = 1 ;
        if ( ctrl.controller_1_enabled )
        {
            copy_cluster = true ;
        }
    }
    else
    {
        peer_controller = 0 ;
        if ( ctrl.controller_0_enabled )
        {
            copy_cluster = true ;
        }
    }

    int n, networks = ctrl.cluster.histories ;
    if ( copy_cluster )
    {
        for ( n = 0 ; n < networks ; n++ )
        {
            /* copy this controller history to create peer controller */
            ctrl.cluster.history[ctrl.cluster.histories] = ctrl.cluster.history[n] ;

            /* update the controller */
            ctrl.cluster.history[ctrl.cluster.histories].controller = peer_controller ;
            ctrl.cluster.bytes += sizeof(mtce_hbs_cluster_history_type) ;
            ctrl.cluster.histories++ ;
        }
    }

#endif // WANT_ACTIVE_ACTIVE_HEARTBEAT_RESULTS

    ctrl.cluster.reqid = (unsigned short)reqid ;
    if (( sm_client_sock ) && ( sm_client_sock->sock_ok() == true ))
    {
        int len = sizeof(mtce_hbs_cluster_type)-hbs_cluster_unused_bytes();
        int bytes = sm_client_sock->write((char*)&ctrl.cluster, len);
        if ( bytes <= 0 )
        {
             elog ("failed to send cluster vault to SM (bytes=%d) (%d:%s)\n",
                    bytes , errno, strerror(errno));
        }
        else
        {
            ilog ("heartbeat cluster vault sent to SM (%d bytes)", len );
            hbs_cluster_dump ( ctrl.cluster );
        }
    }

#ifdef  WANT_ACTIVE_ACTIVE_HEARTBEAT_RESULTS

    if ( copy_cluster )
    {
        /* Clear out the other controllers data. */
        for ( n = networks ; n > 0 ; n-- )
        {
            /* copy c0 history to another controller */
            hbs_cluster_history_init(ctrl.cluster.history[ctrl.cluster.histories-1]);
            ctrl.cluster.bytes -= sizeof(mtce_hbs_cluster_history_type);
            ctrl.cluster.histories-- ;
        }
    }

#endif // WANT_ACTIVE_ACTIVE_HEARTBEAT_RESULTS

}

void hbs_cluster_log ( string & hostname, string prefix )
{
    hbs_cluster_log ( hostname, ctrl.cluster, prefix );
}

/****************************************************************************
 *
 * Active Active Heartbeating and Debug Member Functions
 *
 ***************************************************************************/

/****************************************************************************
 *
 * Name        : hbs_cluster_cmp
 *
 * Descrition  : Performs a sanity check over the cluster structure.
 *
 * Assumptions : Debug tool, not called at runtime.
 *
 * Returns     : PASS or FAIL
 *
 ***************************************************************************/

int hbs_cluster_cmp( hbs_message_type & msg )
{
    if ( msg.cluster.version < ctrl.cluster.version )
    {
        wlog ("Unexpected version (%d:%d)",
               msg.cluster.version, ctrl.cluster.version );
    }
    else if ( msg.cluster.revision != ctrl.cluster.revision )
    {
        wlog ("Unexpected revision (%d:%d)",
               msg.cluster.revision, ctrl.cluster.revision );
    }
    else if ( msg.cluster.magic_number != ctrl.cluster.magic_number )
    {
        wlog ("Unexpected magic number (%d:%d)",
               msg.cluster.magic_number, ctrl.cluster.magic_number );
    }
    else if ( msg.cluster.period_msec != ctrl.cluster.period_msec )
    {
        wlog ("Cluster Heartbeat period delta (%d:%d)",
               msg.cluster.period_msec, ctrl.cluster.period_msec );
    }
    else if ( msg.cluster.storage0_enabled != ctrl.cluster.storage0_enabled )
    {
        wlog ("Cluster storage0 enabled state delta (%d:%d)",
               msg.cluster.storage0_enabled, ctrl.cluster.storage0_enabled );
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
}

/****************************************************************************
 *
 * Name        : hbs_cluster_save
 *
 * Descrition  : Copies the other controllers information from msg into
 *               the cluster.
 *
 *               NOTE: Does not do that right now.
 *
 * Assumptions : Place holder until active/active heartbeating is implemented.
 *
 * Returns     : PASS or FAIL
 *
 ***************************************************************************/

int hbs_cluster_save ( string & hostname,
                       mtce_hbs_network_enum network,
                       hbs_message_type & msg )
{
    // clog ("Add cluster info from peer controller");
    if ( ctrl.monitored_hosts )
    {
        /* compare cluster info and log deltas */
        // hbs_cluster_cmp( msg );
        UNUSED(msg);
        hbs_cluster_log( hostname, ctrl.cluster, hbs_cluster_network_name(network) );
    }
    return (PASS);
}
