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

/**
 * @addtogroup hbs_base
 * @{
 */

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "hbs"

#define ALIGN_PACK(x) __attribute__((packed)) x

/** Maximum service fail count before action */
#define MAX_FAIL_COUNT (1)

/** Heartbeat pulse request/response message header byte size */
#define HBS_HEADER_SIZE (15)

#define HBS_MAX_SILENT_FAULT_LOOP_COUNT (1000)

/** Heartbeat pulse request message header content */
const char req_msg_header   [HBS_HEADER_SIZE+1] = {"cgts pulse req:"};

/** Heartbeat pulse response message header content */
const char rsp_msg_header   [HBS_HEADER_SIZE+1] = {"cgts pulse rsp:"};

#define HBS_MAX_MSG (HBS_HEADER_SIZE+MAX_CHARS_HOSTNAME)

/* A heartbeat service message
 * if this structire is changed then
 * hbs_pulse_request needs to be looked at
 */
typedef struct
{
    /** Message buffer  */
    char           m [HBS_MAX_MSG];

    /** Sequence number */
    unsigned int   s ;

    /* Fast Lookup Clue Info */
    unsigned int   c ;

    /* Status Flags
     * ------------
     * bit 0: Process Monitor Status: 1=running
     * bit 1: Infrastructure Network: 1=provisioned
     *
     * */
    unsigned int   f ;

    /** message version number */
    unsigned int   v ;

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
    bool infra_link_up_and_running ;
    bool mgmnt_link_up_and_running_last ;
    bool infra_link_up_and_running_last ;


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

/* Setup the pulse messaging interfaces
 * 'p' is a boot that indicates if the infrastructure network is provisioned
 * 'p' = true means it is provisioned */
#define SETUP_PULSE_MESSAGING(p,g) \
{ \
    if ( ( rc = _setup_pulse_messaging ( MGMNT_IFACE , g)) != PASS ) \
    { \
        elog ("Failed to setup 'Mgmnt' network pulse messaging (rc:%d)\n", rc ); \
    } \
    if ( p == true ) \
    { \
        if (( rc = _setup_pulse_messaging ( INFRA_IFACE , g)) != PASS ) \
        { \
            elog ("Failed to setup 'Infra' network pulse messaging (rc:%d)\n", rc ); \
        } \
    } \
}

/**
 * @} hbs_base
 */
