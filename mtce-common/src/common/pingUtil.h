#ifndef __INCLUDE_PINGUTIL_H__
#define __INCLUDE_PINGUTIL_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Ping Utility Header
  */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <resolv.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>      // struct ip6_hdr
#include <netinet/icmp6.h>    // struct icmp6_hdr and ICMP6_ECHO_REQUEST
#include <netinet/ip.h>       // IP_MAXPACKET (which is 65535)

using namespace std;

#include "nodeBase.h"
#include "msgClass.h"
#include "nodeUtil.h"
#include "nodeTimers.h"

// Define some constants
#define IP6_HDRLEN 40         // IPv6 header length
#define ICMP_HDRLEN 8         // ICMP header length for echo request, excludes data


#define PING_MAX_RETRIES           (5)
#define PING_MAX_FLUSH_RETRIES   (100)
#define PING_MAX_RECV_RETRIES      (20)
#define PING_MAX_SEND_RETRIES      (5)
#define PING_WAIT_TIMER_MSEC     (200)
#define PING_RETRY_DELAY_MSECS   (200)
#define PING_MONITOR_INTERVAL     (60)
#define PING_MISS_RETRY_DELAY     (5)

#define PING_MESSAGE_LEN         (80)

typedef enum
{
    PINGUTIL_MONITOR_STAGE__IDLE = 0,
    PINGUTIL_MONITOR_STAGE__OPEN,
    PINGUTIL_MONITOR_STAGE__SEND,
    PINGUTIL_MONITOR_STAGE__RECV,
    PINGUTIL_MONITOR_STAGE__WAIT,
    PINGUTIL_MONITOR_STAGE__CLOSE,
    PINGUTIL_MONITOR_STAGE__FAIL,
    PINGUTIL_MONITOR_STAGES,
} pingUtil_stage_type ;

typedef struct
{
    string hostname         ;
    string ip               ;
    msgClassSock * sock     ;

    unsigned short identity ;
    unsigned short sequence ;
    int    send_retries     ;
    int    recv_retries     ;
    bool   ipv6_mode        ;
    bool   received         ;
    bool   requested        ;
    int recv_flush_highwater;
    /* for monitor FSM */
    bool                ok    ;
    bool           monitoring ;
    pingUtil_stage_type stage ;
    struct mtc_timer    timer ;
    void (*timer_handler) ( int, siginfo_t*, void* );
    char message [PING_MESSAGE_LEN];
} ping_info_type ;

/*******************************************************************************
 *
 * Name    : pingUtil_init
 *
 * Purpose : Setup a ping socket
 *
 * Returns : PASS : non-blocking ping socket towards specified ip address setup ok
 *           FAIL : send failed
 *
 ******************************************************************************/
int pingUtil_init ( string hostname, ping_info_type & ping_info, const char * ip_address );

/*******************************************************************************
 *
 * Name    : pingUtil_send
 *
 * Purpose : Send an ICMP ECHO ping request to the specified socket
 *
 * Returns : PASS : send was ok
 *           FAIL : send failed
 *
 ******************************************************************************/

int pingUtil_send ( ping_info_type & ping_info );

/*******************************************************************************
 *
 * Name    : pingUtil_recv
 *
 * Purpose : Receive an ICMP ping response and compare the suggested sequence
 *           and identifier numbers.
 *
 * Returns : PASS : got the response with the correct id and seq codes
 *           RETRY: got response but with one or mode bad codes
 *           FAIL : got no ping reply
 *
 ********************************************************************************/

int pingUtil_recv ( ping_info_type & ping_info, /* sequence in the ping request  */
                             bool   loud );    /* print log  no data received   */

/********************************************************************************
 *
 * Name    : pingUtil_fini
 *
 * Purpose : Close an ping socket
 *
 *******************************************************************************/
void pingUtil_fini ( ping_info_type & ping_info ); /* the preopened ping socket */

/********************************************************************************
 *
 * Name    : pingUtil_acc_monitor
 *
 * Purpose : FSM used to monitor ping access to specific ip address
 *
 *******************************************************************************/

int pingUtil_acc_monitor ( ping_info_type & ping_info );

/********************************************************************************
 *
 * Name    : pingUtil_restart
 *
 * Purpose : Restart the ping monitor
 *
 *******************************************************************************/

void pingUtil_restart ( ping_info_type & ping_info );

#endif
