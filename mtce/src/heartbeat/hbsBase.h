/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Nodal Health Check "Base" Header
  */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <list>
#include "msgClass.h"
#include "mtceHbsCluster.h"
#include "hbsCluster.h"

/**
 * @addtogroup hbs_base
 * @{
 */

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "hbs"

// #define WANT_CLUSTER_DEBUG

#define ALIGN_PACK(x) __attribute__((packed)) x

/** Maximum service fail count before action */
#define MAX_FAIL_COUNT (1)

/** Audit Rate/Count */
#define AUDIT_RATE (9)

/** Heartbeat pulse request/response message header byte size */
#define HBS_HEADER_SIZE (15)

#define HBS_MAX_SILENT_FAULT_LOOP_COUNT (1000)

/** Heartbeat pulse request message header content */
const char req_msg_header   [HBS_HEADER_SIZE+1] = {"cgts pulse req:"};

/** Heartbeat pulse response message header content */
const char rsp_msg_header   [HBS_HEADER_SIZE+1] = {"cgts pulse rsp:"};

#define HBS_MAX_MSG (HBS_HEADER_SIZE+MAX_CHARS_HOSTNAME)

#define HBS_MESSAGE_VERSION   (1) // 0 -> 1 with intro of cluster info

/* Heartbeat control structure */
typedef struct
{
    unsigned int controller   ;
    unsigned int audit        ;
    unsigned int nodetype     ;
    bool         clear_alarms ;
    bool         locked       ;
} hbs_ctrl_type ;
hbs_ctrl_type * get_hbs_ctrl_ptr ( void );

/* A heartbeat service message
 * if this structure is changed then
 * hbs_pulse_request needs to be looked at
 */
typedef struct
{
    /** Message buffer  */
    char           m [HBS_MAX_MSG];

    /** Sequence number */
    unsigned int   s ;

    /* Fast Lookup Clue Info */
    unsigned int c  ;

    /* Status Flags
     * ------------
     * bit 0: Process Monitor Status: 1=running
     * bit 1: Cluster-host Network: 1=provisioned
     *
     * */
    unsigned int   f ;

    /** message version number */
    unsigned int   v ;

    /** Heartbeat cluster information that is put into heartbeat messages.
     *
     *  Pulse Request :   To hbsClient: Only 1 controller with up to 2 network types history.
     *  Pulse Response: From hbsClient: Can include up to 2 controllers with 2 networks each.
     *
     *  This addition requires message verison increment.
     *
     **/
    mtce_hbs_cluster_type cluster ;

} ALIGN_PACK(hbs_message_type) ;


/** Heartbeat service messaging socket control structure */
typedef struct
{
    /** Mtce to Heartbeat Service Cmd Interface - mtcAgent -> hbsAgent      */
    msgClassSock*      mtc_to_hbs_sock;

    /** Heartbeat Service Event Transmit Interface - hbsAgent -> mtcAgent   */
    msgClassSock*      hbs_event_tx_sock;

    /** Heartbeat Service Event Transmit Interface - hbsClient -> mtcAgent  */
    msgClassSock*      hbs_ready_tx_sock;

    /** Heartbeat Service SM Transmit Interface - hbsAgent -> sm  */
    msgClassSock*      sm_client_sock;

    /** Heartbeat Service SM Receive Interface - sm -> hbsAgent  */
    msgClassSock*      sm_server_sock;

    /** PMON Pulse Receive Interface - pmond -> hbsClient                   */
    msgClassSock*      pmon_pulse_sock;

    /** Active monitoring Transmit Interface socket - hbsClient -> pmond    */
    /** The addr and port are stored in the shared libamon.so library       */
    int                 amon_socket     ;

    /** Heartbeat Pulse Receive Constructs                                    */
    msgClassSock*      rx_sock   [MAX_IFACES]; /**< rx socket file descriptor */
    int                rx_port   [MAX_IFACES]; /**< rx pulse port number      */
    hbs_message_type   rx_mesg   [MAX_IFACES]; /**< rx pulse message buffer   */

    /** Heartbeat Pulse Receive Constructs                                    */
    msgClassSock*      tx_sock   [MAX_IFACES]; /**< tx socket file descriptor */
    int                tx_port   [MAX_IFACES]; /**< tx pulse port number      */
    hbs_message_type   tx_mesg   [MAX_IFACES]; /**< tx pulse message buffer   */

    bool fired                   [MAX_IFACES]; /**< true if select fired      */

    msgSock_type       mtclogd ; /* Not used */

    /** Heartbeat Alarms Messaging Constructs / Interface                     */
    msgClassSock*   alarm_sock ;               /**< tx socket file descriptor */
    int             alarm_port ;               /**< tx pulse port number      */


    /* For select dispatch */
    struct timeval waitd  ;
            fd_set readfds;

    int netlink_sock ; /* netlink socket */
    int   ioctl_sock ; /* general ioctl socket */

    bool mgmnt_link_up_and_running ;
    bool clstr_link_up_and_running ;
    bool mgmnt_link_up_and_running_last ;
    bool clstr_link_up_and_running_last ;


} hbs_socket_type ;

typedef struct
{
    string proc   ;
    int    pid    ;
    int    status ;
    int    stalls ;
    int    periods;
    unsigned long long this_count ;
    unsigned long long prev_count ;
} procList ;

typedef struct
{
    unsigned long long this_count ;
    unsigned long long prev_count ;
} schedHist ;

int  hbs_refresh_pids    ( std::list<procList> & proc_list );
int  hbs_process_monitor ( std::list<procList> & pmon_list );
int  hbs_self_recovery   ( unsigned int cmd );

/* returns this controller's number ; 0 or 1 */
unsigned int hbs_get_controller_number ( void );

/* Setup the pulse messaging interfaces
 * 'p' is a bool that indicates if the cluster-host network is provisioned
 * 'p' = true means it is provisioned */
#define SETUP_PULSE_MESSAGING(p,g) \
{ \
    if ( ( rc = _setup_pulse_messaging ( MGMNT_IFACE , g)) != PASS ) \
    { \
        elog ("Failed to setup 'Mgmnt' network pulse messaging (rc:%d)\n", rc ); \
    } \
    if ( p == true ) \
    { \
        if (( rc = _setup_pulse_messaging ( CLSTR_IFACE , g)) != PASS ) \
        { \
            elog ("Failed to setup 'Cluster-host' network pulse messaging (rc:%d)\n", rc ); \
        } \
    } \
}

/*********** Common Heartbeat Utilities in hbsUtil.cpp ***************/

/* module init */
void   hbs_utils_init           ( void );

/* network enum to name lookup */
string hbs_cluster_network_name ( mtce_hbs_network_enum network );

/* Initialize the specified history array */
void   hbs_cluster_history_init ( mtce_hbs_cluster_history_type & history );

/* Clear all history in the cluster vault */
void   hbs_cluster_history_clear( mtce_hbs_cluster_type & cluster );

/******** Heartbeat Agent Cluster Functions in hbsCluster.cpp ********/

/* Init the control structure */
void hbs_cluster_ctrl_init ( void );

/* Set the cluster vault to default state.
 * Called upon daemon init or heartbeat period change. */
void hbs_cluster_init ( unsigned short period , msgClassSock * sm_socket_ptr );

/* Calculate number of bytes that is unused in the cluster data structure.
 * Primarily to know how many history elements are missing. */
unsigned short hbs_cluster_unused_bytes ( void );

/* Inform the cluster module that there was a change to the cluster */
void hbs_cluster_change ( string cluster_change_reason );

/* Add and delete hosts from the monitored list.
 * Automatically adjusts the numbers in the cluster vault. */
void hbs_cluster_add  ( string & hostname );
void hbs_cluster_del  ( string & hostname );

/* do actions when this controller is detected as locked */
void hbs_controller_lock ( void );

/* Do stuff in preparation for another pulse period start */
void hbs_cluster_period_start ( void );

/* Report status of storgate-0 */
void hbs_cluster_storage0_status ( iface_enum iface , bool responding );

/* Compare 2 histories */
int hbs_cluster_cmp( mtce_hbs_cluster_history_type h1,
                     mtce_hbs_cluster_history_type h2 );

/* Set the number of monitored hosts and this controller's
 * number in the cluster vault. */
void hbs_cluster_nums ( unsigned short this_controller,
                        unsigned short monitored_networks );

/* Copy/Save the peer controller's cluster info from the hbsClient's
 * pulse response into the cluster vault so its there and ready for
 * an SM cluster_info request. */
int  hbs_cluster_save (               string & hostname,
                        mtce_hbs_network_enum  network,
                            hbs_message_type & msg );

/* Manage peer controller vault history. */
void hbs_cluster_peer ( void );

/*
 * Called by the hbsAgent pulse receiver to create a network specific
 * history update entry consisting of
 *
 *  1. the number of monitored hosts
 *  2. how many of those that responded in the last heartbeat period.
 *  3. threshold storage-0 responding count and manage that state in that
 *     networks history header.
 */
void hbs_cluster_update ( iface_enum iface,
                          unsigned short not_responding_hosts,
                          bool storage_0_responding );

/* Called by the hbsAgent pulse transmitter to append this controllers
 * running cluster view in the next multicast pulse request.
 * The hbsClient is expected to loop this data and any other like data from
 * the other controller back in its response. */
void hbs_cluster_append ( hbs_message_type & msg );

/* Inject a history entry at the next position for all networks of the
 * specified controller.
 *
 * This is used to add a 0:0 entry into the vault history of the specified
 * controller as indication that that no host for this pulse period
 * provided history for this controller.
 *
 * Procedure was made generic so that it 'could' be used to add history
 * of any values for fault insertion or other potential future purposes
 *
 * Returns true if data was injected ;
 *  ... as an indication that the cluster had a state change.
 *
 */
bool hbs_cluster_inject ( unsigned short controller, unsigned short hosts_enabled, unsigned short hosts_responding );


/* Produce formatted clog's that characterize current and changing cluster
 * history for a given network. Each log is controller/network specific. */
void hbs_cluster_log  ( string & hostname,                                  string prefix, bool force=false );
void hbs_cluster_log  ( string & hostname, mtce_hbs_cluster_type & cluster, string prefix, bool force=false );


/* Service SM cluster info request */
void hbs_sm_handler ( void );

/* send the cluster vault to SM */
void hbs_cluster_send ( msgClassSock * sm_client_sock, int reqid , string reason );

/* copy cluster data from src to dst */
void hbs_cluster_copy ( mtce_hbs_cluster_type & src, mtce_hbs_cluster_type & dst );

/* print the contents of the vault */
void hbs_cluster_dump ( mtce_hbs_cluster_history_type & history, bool storage0_enabled );
void hbs_cluster_dump ( mtce_hbs_cluster_type & vault );

/* Heartbeat service state audit */
void hbs_state_audit ( void );

/**
 * @} hbs_base
 */
