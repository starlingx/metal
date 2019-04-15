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

    /* Used to manage the cluster based on this and peer controller state */
    bool peer_controller_enabled ;

    /* Used to prevent log flooding in presence of back to back errors. */
    unsigned int log_throttle ;

    /* Used to log when
     * - peer history goes missing (false -> true change)
     * - peer history starts being received ( true -> false change ) */
    bool peer_history_missing ;

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

    string cluster_change_reason ;

    bool got_peer_controller_history ;

    msgClassSock * sm_socket_ptr ;

} hbs_cluster_ctrl_type ;

/* Cluster control structire construct allocation. */
static hbs_cluster_ctrl_type ctrl ;

#define STORAGE_0_NR_THRESHOLD (4)
#define CLUSTER_CHANGE_THRESHOLD (50000)

/****************************************************************************
 *
 * Name        : hbs_cluster_init
 *
 * Description : Initialize the cluster structure to default values.
 *
 * Assumtions  : Called by hbsAgent.cpp before entering the main loop.
 *
 ***************************************************************************/

void hbs_cluster_init ( unsigned short period, msgClassSock * sm_socket_ptr )
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

    clog ("Cluster Info: v%d.%d sig:%x bytes:%d (%ld)",
             ctrl.cluster.version,
             ctrl.cluster.revision,
             ctrl.cluster.magic_number,
             ctrl.cluster.bytes,
             sizeof(mtce_hbs_cluster_history_type));

    if ( sm_socket_ptr )
    {
        ctrl.sm_socket_ptr = sm_socket_ptr ;
    }
    ctrl.log_throttle = 0 ;
}

void hbs_cluster_ctrl_init ( void )
{
    ctrl.this_controller = 0xffff ;
    ctrl.peer_controller_enabled = false ;
    ctrl.peer_history_missing = true ;
    ctrl.log_throttle = 0 ;
    ctrl.monitored_networks = 0 ;
    ctrl.monitored_hosts = 0 ;
    ctrl.monitored_hostname_list.clear();
    ctrl.cluster_change_reason = "" ;
    ctrl.got_peer_controller_history = false ;
    ctrl.sm_socket_ptr = NULL ;
    memset(&ctrl.storage_0_not_responding_count[0], 0, sizeof(ctrl.storage_0_not_responding_count));
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
 * Name        : hbs_cluster_change
 *
 * Description : Maintain a the cluster change reason.
 *
 *               cleared and printed in hbs_cluster_update.
 *
 ***************************************************************************/

void hbs_cluster_change ( string cluster_change_reason )
{
    if ( ctrl.cluster_change_reason.empty() )
        ctrl.cluster_change_reason = cluster_change_reason ;
    else if ( cluster_change_reason.find ( "peer controller cluster event" ) == std::string::npos )
        ctrl.cluster_change_reason.append(" ; " + cluster_change_reason);
}

/****************************************************************************
 *
 * Name        : cluster_list
 *
 * Description : Log the list of monitored hosts.
 *               Typically done on a list change.
 *
 * Returns     : None
 *
 ***************************************************************************/

void cluster_list ( void )
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
    ilog ("cluster: %s", list.c_str());
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
        hbs_cluster_change ( "storage-0 state change" );
    }
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
    bool already_in_list = false ;
    std::list<string>::iterator hostname_ptr ;
    for ( hostname_ptr  = ctrl.monitored_hostname_list.begin();
          hostname_ptr != ctrl.monitored_hostname_list.end() ;
          hostname_ptr++ )
    {
        if ( hostname_ptr->compare(hostname) == 0 )
        {
            already_in_list = true ;
            break ;
        }
    }

    if ( already_in_list == false )
    {
        ctrl.monitored_hostname_list.push_back(hostname) ;
        ctrl.monitored_hosts = (unsigned short)ctrl.monitored_hostname_list.size();
        ilog ("%s added to cluster", hostname.c_str());
        cluster_list ();
    }

    /* Manage storage-0 state */
    if ( hostname.compare(STORAGE_0) == 0 )
    {
        cluster_storage0_state ( true );
    }

    /* If we get down to 0 monitored hosts then just start fresh */
    if (( ctrl.monitored_hosts ) == 0 )
    {
        hbs_cluster_init ( ctrl.cluster.period_msec, NULL );
    }

    /* Catch enable/provisioning of the peer controller */
    if (( hostname == CONTROLLER_0 ) && ( ctrl.this_controller != 0 ))
        ctrl.peer_controller_enabled = true ;
    if (( hostname == CONTROLLER_1 ) && ( ctrl.this_controller != 1 ))
        ctrl.peer_controller_enabled = true ;
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
    std::list<string>::iterator hostname_ptr ;
    for ( hostname_ptr  = ctrl.monitored_hostname_list.begin();
          hostname_ptr != ctrl.monitored_hostname_list.end() ;
          hostname_ptr++ )
    {
        if ( hostname_ptr->compare(hostname) == 0 )
        {
            ctrl.monitored_hostname_list.remove(hostname) ;
            ctrl.monitored_hosts = (unsigned short)ctrl.monitored_hostname_list.size();

            /* Manage storage-0 state. */
            if ( hostname.compare(STORAGE_0) == 0 )
            {
                cluster_storage0_state ( false );
            }

            /* If we get down to 0 monitored hosts then just start fresh */
            if (( ctrl.monitored_hosts ) == 0 )
            {
                hbs_cluster_init ( ctrl.cluster.period_msec, NULL );
            }

            ilog ("%s deleted from cluster", hostname.c_str());

            cluster_list ();

            hbs_cluster_change ( hostname + " deleted" );

            break ;
        }
    }
}

/****************************************************************************
 *
 * Name        : hbs_cluster_period_start
 *
 * Description : The following things need to be done at the start of
 *               every pulse period ...
 *
 *               - set 'got_peer_controller_history' to false only to get
 *                 set true when one at least one hbsClient response
 *                 contains history from the other controller.
 *
 ***************************************************************************/

void hbs_cluster_period_start ( void )
{
    clog3 ("Pulse Period Start ; waiting on responses (last:%d)",
            ctrl.got_peer_controller_history );
    if ( ctrl.got_peer_controller_history )
        ctrl.got_peer_controller_history = false ;
}

/****************************************************************************
 *
 * Name        : hbs_cluster_update
 *
 * Description : Update this controller's cluster info for the specified
 *               network with ...
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
    else if ( iface == CLSTR_IFACE )
        n = MTCE_HBS_NETWORK_CLSTR ;
#ifdef MONITORED_OAM_NETWORK
    else if ( iface == OAM_IFACE )
        n = MTCE_HBS_NETWORK_OAM ;
#endif
    else
        return ;

    if ( not_responding_hosts )
    {
        clog ("controller-%d %s enabled:%d not responding:%d",
               ctrl.this_controller,
               hbs_cluster_network_name(n).c_str(),
               ctrl.monitored_hosts,
               not_responding_hosts);
    }
    else
    {
        clog ("controller-%d %s has %d monitored hosts and all are responding",
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
            ilog ("controller-%d added new controller-%d:%s history to vault ; now have %d network views",
                   ctrl.this_controller,
                   ctrl.this_controller,
                   hbs_cluster_network_name(n).c_str(),
                   ctrl.cluster.histories);
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

    /* Increment the entries count till it reaches the max. */
    if ( history_ptr->entries < MTCE_HBS_HISTORY_ENTRIES )
        history_ptr->entries++ ;

    /* Update the history with this data. */
    history_ptr->entry[history_ptr->oldest_entry_index].hosts_enabled = ctrl.monitored_hosts ;
    history_ptr->entry[history_ptr->oldest_entry_index].hosts_responding = ctrl.monitored_hosts - not_responding_hosts ;

    /* Manage the next entry update index ; aka the oldest index.
     * - handle not full case ; oldest entry is the first entry
     * - handle the full case ; wrap around */
    if (( history_ptr->entries == 0 ) ||
        ( history_ptr->oldest_entry_index == (MTCE_HBS_HISTORY_ENTRIES-1)))
        history_ptr->oldest_entry_index = 0 ;
    else
        history_ptr->oldest_entry_index++ ;

    /* send SM an update if the cluster has changed which is indicated
     * by string content in ctrl.cluster_change_reason. */
    if ( ! ctrl.cluster_change_reason.empty() )
    {
        hbs_cluster_send( ctrl.sm_socket_ptr, 0, ctrl.cluster_change_reason );
        ctrl.cluster_change_reason = "" ;
    }

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
    CHECK_CTRL_NTWK_PARMS(ctrl.this_controller, ctrl.monitored_networks);

    msg.cluster.version          = ctrl.cluster.version ;
    msg.cluster.revision         = ctrl.cluster.revision ;
    msg.cluster.magic_number     = ctrl.cluster.magic_number ;
    msg.cluster.period_msec      = ctrl.cluster.period_msec ;
    msg.cluster.storage0_enabled = ctrl.cluster.storage0_enabled ;
    msg.cluster.histories        = 0 ;

    /* Copy this controller's cluster history into the broadcast request. */
    for ( int h = 0 ; h < ctrl.cluster.histories ; h++ )
    {
        if ( ctrl.cluster.history[h].controller == ctrl.this_controller )
        {
            memcpy( &msg.cluster.history[msg.cluster.histories],
                    &ctrl.cluster.history[h],
                    sizeof(mtce_hbs_cluster_history_type));

            msg.cluster.histories++ ;
        }
    }
    msg.cluster.bytes = BYTES_IN_CLUSTER_VAULT(msg.cluster.histories);

    clog1 ("controller-%d appending cluster info to heartbeat message (%d:%d:%d)",
            ctrl.this_controller, ctrl.monitored_networks, ctrl.cluster.histories, msg.cluster.bytes );
}

/* Manage peer controller vault history. */
void hbs_cluster_peer ( void )
{
    /* Manage updating the local peer controller history data with 0:0
     * for this pulse period if there was no response from the peer
     * controller for this pulse period. */
    if (( ctrl.got_peer_controller_history == false ) &&
        ( ctrl.peer_controller_enabled == true ))
    {
        if ( ctrl.peer_history_missing == false )
        {
            wlog ( "missing peer controller cluster view" );
            ctrl.peer_history_missing = true ;
        }
        /* if no nodes have reported peer controller history then inject
         * a 0:0 value in for this pulse period for that controller. */
        hbs_cluster_inject ( ctrl.this_controller?0:1, 0, 0 );
    }
    else if (( ctrl.got_peer_controller_history == true ) &&
             ( ctrl.peer_controller_enabled == true ) &&
             ( ctrl.peer_history_missing == true ))
    {
        wlog ( "receiving peer controller cluster view" );
        ctrl.peer_history_missing = false ;
    }
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

void hbs_cluster_send ( msgClassSock * sm_client_sock, int reqid , string reason )
{
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
        hbs_cluster_dump ( ctrl.cluster, reason );
    }
    else
    {
        wlog ("cannot send cluster info due to socket error");
    }
}

/****************************************************************************
 *
 * Name        : hbs_history_save
 *
 * Descrition  : Copy the history sample to the vault.
 *
 * Returns     : Nothing.
 *
 ***************************************************************************/

void hbs_history_save ( string hostname,
                        mtce_hbs_network_enum network,
                        mtce_hbs_cluster_history_type & sample )
{
    for ( int h = 0 ; h < ctrl.cluster.histories ; h++ )
    {
        if (( ctrl.cluster.history[h].controller ==  sample.controller ) &&
            ( ctrl.cluster.history[h].network == sample.network ))
        {
            if ( hbs_cluster_cmp( sample, ctrl.cluster.history[h] ) )
            {
                 hbs_cluster_change ("peer controller cluster event " +
                 hbs_cluster_network_name((mtce_hbs_network_enum)sample.network));
            }

            memcpy( &ctrl.cluster.history[h], &sample,
                    sizeof(mtce_hbs_cluster_history_type));

            clog1 ("controller-%d vault update from controller-%d %s reply with %d histories (this:%s)",
                   ctrl.this_controller,
                   sample.controller,
                   hbs_cluster_network_name(network).c_str(),
                   ctrl.cluster.histories,
                   hbs_cluster_network_name((mtce_hbs_network_enum)sample.network).c_str());
            return ;
        }
    }

    hbs_cluster_change ( "peer controller cluster " +
    hbs_cluster_network_name((mtce_hbs_network_enum)sample.network));

    /* not found ? Add a new one */
    memcpy( &ctrl.cluster.history[ctrl.cluster.histories], &sample,
            sizeof(mtce_hbs_cluster_history_type));

    ctrl.cluster.histories++ ;
    ctrl.cluster.bytes = BYTES_IN_CLUSTER_VAULT(ctrl.cluster.histories);

    ilog ("controller-%d added new %s:%s history to vault ; now have %d network views",
              ctrl.this_controller,
              hostname.c_str(),
              hbs_cluster_network_name((mtce_hbs_network_enum)sample.network).c_str(),
              ctrl.cluster.histories);
}

void hbs_state_audit ( void )
{
    if ( ctrl.monitored_hosts )
        hbs_cluster_dump ( ctrl.cluster, "Audit" );
}


void hbs_cluster_log ( string & hostname, string prefix )
{
    hbs_cluster_log ( hostname, ctrl.cluster, prefix );
}

void hbs_cluster_log ( string & hostname,
                       string log_prefix,
                       bool force )
{
    hbs_cluster_log (hostname,  ctrl.cluster, log_prefix, force );
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
 * Descrition  : Compare 2 histories
 *
 * Returns     : 0 - when number of enabled hosts and responding
 *                      hosts are the same for all the entries.
 *               # - the number of entries that are different.
 *
 ***************************************************************************/

int hbs_cluster_cmp( mtce_hbs_cluster_history_type h1,
                     mtce_hbs_cluster_history_type h2 )
{
    int h1_delta = 0 ;
    int h2_delta = 0 ;
    int    delta = 0 ;

    for ( int e = 0 ; e < h1.entries ; e++ )
        if ( h1.entry[e].hosts_enabled != h1.entry[e].hosts_responding )
            h1_delta++ ;

    for ( int e = 0 ; e < h2.entries ; e++ )
        if ( h2.entry[e].hosts_enabled != h2.entry[e].hosts_responding )
            h2_delta++ ;

    if ( h1_delta > h2_delta )
        delta = h1_delta-h2_delta ;
    else if ( h2_delta > h1_delta )
        delta = h2_delta-h1_delta ;

    if ( delta )
    {
        clog3 ("peer controller reporting %d deltas", delta );
    }
    return(delta);
}

/****************************************************************************
 *
 * Name        : hbs_cluster_save
 *
 * Descrition  : Copies the other controllers information from msg into
 *               the cluster.
 *
 * Returns     : PASS or FAIL
 *
 ***************************************************************************/

int hbs_cluster_save ( string & hostname,
                       mtce_hbs_network_enum network,
                       hbs_message_type & msg )
{
    /* cluster info is only supported in HBS_MESSAGE_VERSION 1 */
    if ( msg.v < HBS_MESSAGE_VERSION )
        return FAIL_NOT_SUPPORTED ;

    if ( ! ctrl.monitored_hosts )
        return RETRY ;

    if ( ! msg.cluster.histories )
    {
        wlog_throttled ( ctrl.log_throttle, THROTTLE_COUNT,
                         "%s %s ; no peer controller history",
                         hostname.c_str(),
                         hbs_cluster_network_name(network).c_str());
    }

    if ( ctrl.peer_controller_enabled )
    {
        /* Should only contain the other controllers history */
        for ( int h = 0 ; h < msg.cluster.histories ; h++ )
        {
            if ( msg.cluster.history[h].network >= MTCE_HBS_MAX_NETWORKS )
            {
                elog ("Invalid network id (%d:%d:%d)",
                       h,
                       msg.cluster.history[h].controller,
                       msg.cluster.history[h].network );
            }
            else if ( msg.cluster.history[h].controller != ctrl.this_controller )
            {
                /* set that we got some history and save it */
                ctrl.got_peer_controller_history = true ;
                hbs_history_save ( hostname, network, msg.cluster.history[h] );
            }
            hbs_cluster_log( hostname, ctrl.cluster, hbs_cluster_network_name(network) );
        }
    }
    return (PASS);
}


void hbs_cluster_inject ( unsigned short controller, unsigned short hosts_enabled, unsigned short hosts_responding )
{
    for ( int h = 0 ; h < ctrl.cluster.histories ; h++ )
    {
        if ( ctrl.cluster.history[h].controller == controller )
        {
            bool dumpit = false ;
            if (( ctrl.cluster.history[h].entry[ctrl.cluster.history[h].oldest_entry_index].hosts_enabled ) ||
                ( ctrl.cluster.history[h].entry[ctrl.cluster.history[h].oldest_entry_index].hosts_responding ))
            {
                /* Inject requested data for all networks of specified controller */
                ctrl.cluster.history[h].entry[ctrl.cluster.history[h].oldest_entry_index].hosts_enabled = hosts_enabled ;
                ctrl.cluster.history[h].entry[ctrl.cluster.history[h].oldest_entry_index].hosts_responding = hosts_responding ;

                wlog ("controller-%d injected %d:%d into controller-%d %s history (entry %d)",
                       controller?0:1,
                       hosts_enabled,
                       hosts_responding,
                       controller,
                       hbs_cluster_network_name((mtce_hbs_network_enum)ctrl.cluster.history[h].network).c_str(),
                       ctrl.cluster.history[h].oldest_entry_index  );
                dumpit = true ;
            }
            /* manage the oldest index */
            if ( ++ctrl.cluster.history[h].oldest_entry_index == MTCE_HBS_HISTORY_ENTRIES )
                ctrl.cluster.history[h].oldest_entry_index = 0 ;

            /* DEBUG: */
            if ( dumpit )
                hbs_cluster_dump( ctrl.cluster.history[h], ctrl.cluster.storage0_enabled );
        }
    }
}

/****************************************************************************
 *
 * Name        : hbs_controller_lock
 *
 * Description : Clear all history for this controller.
 *               Called when this controller is detected as locked.
 *
 ***************************************************************************/

void hbs_controller_lock ( void )
{
    if ( ctrl.cluster.histories )
    {
        ilog ("controller-%d locked ; clearing all cluster info", ctrl.this_controller );
        for ( int h = 0 ; h < ctrl.cluster.histories ; h++ )
        {
            memset ( &ctrl.cluster.history[h], 0, sizeof(mtce_hbs_cluster_history_type));
        }
        ctrl.cluster.histories = 0 ;
        hbs_cluster_change ( "this controller locked" ) ;
    }
}

