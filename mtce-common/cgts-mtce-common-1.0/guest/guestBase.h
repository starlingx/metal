#ifndef __INCLUDE_GUESTBASE_H__
#define __INCLUDE_GUESTBASE_H__

/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Guest Services "Base" Header
  */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <limits.h>
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

#include <guest-client/guest_heartbeat_msg_defs.h>

using namespace std;

#include "msgClass.h"
#include "nodeBase.h"
#include "httpUtil.h"
#include "nodeTimers.h"

#define WANT_NEW

/**
 * @addtogroup guest_services_base
 * @{
 */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "gst"

#define CONFIG_CLIENT_RX_PORT    (0x00000001)
#define CONFIG_MTC_EVENT_PORT    (0x00000002)
#define CONFIG_MTC_CMD_PORT      (0x00000004)
#define CONFIG_AGENT_RX_PORT     (0x00000008)
#define CONFIG_VIM_CMD_RX_PORT   (0x00000010)
#define CONFIG_VIM_EVENT_RX_PORT (0x00000020)

#define HB_DEFAULT_FIRST_MS      2000
#define HB_DEFAULT_INTERVAL_MS   1000
#define HB_DEFAULT_REBOOT_MS    10000
#define HB_DEFAULT_VOTE_MS      10000
#define HB_DEFAULT_SHUTDOWN_MS  10000
#define HB_DEFAULT_SUSPEND_MS   10000
#define HB_DEFAULT_RESUME_MS    10000
#define HB_DEFAULT_RESTART_MS  120000

/* Directory where libvirt creates the serial I/O pipe channel sockets into the guest 
 * We monitor this directory with inotify for file changes */
#define QEMU_CHANNEL_DIR   ((const char *)"/var/lib/libvirt/qemu")

#define ARRAY_SIZE(x) ((int)(sizeof(x)/sizeof(*x)))

#define MAX_INSTANCES (100)
#define MAX_MESSAGES  (10)

/* The socket select timeout */
#define GUEST_SOCKET_TO (10000)

#define DEFAULT_CONNECT_WAIT     (1)

#define CONNECT_TIMOUT           (60)
#define WAIT_FOR_INIT_TIMEOUT    (60)
#define HEARTBEAT_START_TIMEOUT (120)
#define    SEARCH_AUDIT_TIME    (180)

void guestTimer_handler ( int sig, siginfo_t *si, void *uc);

const char * get_guest_msg_hdr (void) ;

typedef struct
{
    char buffer [256];
} gst_message_type ;

typedef enum 
{
    hbs_invalid,
    hbs_server_waiting_init,
    hbs_server_waiting_challenge,
    hbs_server_waiting_response,
    hbs_server_paused,       // heartbeat paused at request of vm
    hbs_server_nova_paused,  // heartbeat paused at request of nova
    hbs_server_migrating,    // heartbeat paused while migrate in progress
    hbs_server_corrective_action,
    hbs_client_waiting_init_ack,
    hbs_client_waiting_challenge,
    hbs_client_waiting_pause_ack,
    hbs_client_waiting_resume_ack,
    hbs_client_paused,
    hbs_client_waiting_shutdown_ack,
    hbs_client_waiting_shutdown_response,
    hbs_client_shutdown_response_recieved,
    hbs_client_exiting,
    hbs_state_max
} hb_state_t;

/** Guest service control messaging socket control structure */
typedef struct
{
    /** Guest Services Messaging Agent Receive (from guestServer) Socket
     *
     * Note: This socket supports receiving from the computes specifying
     *       either the floating or local IP */
    int                agent_rx_port       ;
    msgClassSock*      agent_rx_float_sock ;
    msgClassSock*     agent_rx_local_sock ;
    
    /** Guest Services Messaging Agent Transmit (to guestServer) Socket
     *
     * Note: This transmit socket can be used for any port
     *       specified at send time */
    msgClassSock*        agent_tx_sock       ;


    /** Guest Services Messaging Socket mtcAgent commands are received on */
    msgClassSock*      mtc_cmd_sock        ;
    int                mtc_cmd_port        ;

    /** Guest Services Messaging Server Receive (from guestAgent) Socket */
    msgClassSock*      server_rx_sock      ;
    int                server_rx_port      ;

    /** Guest Services Messaging Server Transmit (to guestAgent) Socket  */
    msgClassSock*                server_tx_sock      ;
    struct sockaddr_in server_tx_addr      ;

    /** Socket used to transmit READY status and Events to Maintenance   */
    int                mtc_event_tx_port   ;
    msgClassSock*      mtc_event_tx_sock   ;

    int                netlink_sock   ; /* netlink socket */
    int                ioctl_sock     ; /* general ioctl socket */

    msgSock_type       mtclogd        ;
} guest_services_socket_type ;

/** 
 * The HTTP server supports two URL levels ; 
 * a hosts level and instances level. 
 **/
typedef enum
{
    SERVICE_LEVEL_NONE,
    SERVICE_LEVEL_HOST,
    SERVICE_LEVEL_INST,
} service_level_enum ;

/** common service_type control info */
typedef struct
{
    bool   provisioned ; /* set true once the VIM issues create           */
    string state       ; /* enabled, configured or disabled               */
    bool   reporting   ; /* failue reporting state                        */

    int    failures    ; /* Running count of failures           */
    bool   failed      ; /* true means heartbeating has failed  */
    bool   waiting     ; /* Waiting on a response               */
    int    b2b_misses  ; /* running back-to-back misses         */
} service_type ;

/** A grouping of info extracted from command's url */
typedef struct
{
    service_level_enum service_level ;
    string             uuid          ;
    string             command       ;
    string             temp          ;
} url_info_type ;

/** instance control structure */
typedef struct
{
    string hostname ; /**< The host that this instance is on */

    /* Instance identifiers */
    string name ; /**< the Instance Name as it appears in the GUI          */
    string uuid ; /**< the instance uuid which is unique to the system     */
    string chan ; /**< virtio channel name 'cgcs.heartbeat.<uuid>.sock'    */
    string inst ; /**< the instance part of the channel name               */

    /* Set to true when this channel has been provisioned by the guestAgent */
    // bool provisioned ;

    /* 
     * Full path and name to the detected channel.
     * Used to set inotify file watch.
     */
    string fd_namespace ;

    #define CHAN_FLAGS (SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC )
    int    chan_fd ;
    bool   chan_ok ;

    bool   connecting   ;
    bool   connected    ; /* true = the channel is connected to the guest */
    bool   heartbeating ; /* true = the heartbeating has started          */

    string name_log_prefix ;
    string uuid_log_prefix ;

    int    connect_wait_in_secs ; 

    /* added service bools */
    service_type heartbeat ;
    service_type reserved  ;

    /* 
     * File and watch descriptors used to monitor 
     * specific files in QEMU_CHANNEL_DIR
     */
    int inotify_file_fd ;
    int inotify_file_wd ;

    /* Message header info */
    int version;
    int revision;
    string msg_type;
    uint32_t sequence;

    hb_state_t         hbState ; /* see heartbeat_types.h */
    hb_state_t         vnState ; /* see heartbeat_types.h */

    uint32_t invocation_id ;
    
        // For voting and notification
    string  event_type;             // GuestHeartbeatMsgEventT
    string  notification_type;      // GuestHeartbeatMsgNotifyT

    uint32_t  heartbeat_challenge ;
    uint32_t  heartbeat_interval_ms ;

    uint32_t  vote_secs;
    uint32_t  shutdown_notice_secs;
    uint32_t  suspend_notice_secs;
    uint32_t  resume_notice_secs;
    uint32_t  restart_secs;
    string  corrective_action;

    string  unhealthy_corrective_action;
    bool      unhealthy_failure ;

    /* String versions of the above timeouts - integer portions only */
    string vote_to_str     ; /* vote timeout in seconds as a string value     */
    string shutdown_to_str ; /* shutdown timeout in seconds as a string value */
    string suspend_to_str  ; /* suspend timeout in seconds as a string value  */
    string resume_to_str   ; /* resume timeout in seconds as a string value   */
    string restart_to_str  ; /* restart timeout in seconds as a string value  */

    int select_count  ;
    int message_count ;
    int health_count  ;
    int failure_count ;
    int connect_count ;
    int connect_retry_count ;
    int corrective_action_count ;

    libEvent vimEvent ;

} instInfo ;

/* daemon control structure - used for both guestAgent and guestServer */
typedef struct
{
    bool   init ;
    char   hostname [MAX_HOST_NAME_SIZE+1];
    string address      ;
    string address_peer ; /* used for server only */
    int    nodetype     ; /* used for server only */

    guest_services_socket_type sock  ;
    struct mtc_timer           timer ;

    /* List of instances provisioned on this host */
    list<instInfo>           instance_list    ; /* used for server only */
    list<instInfo>::iterator instance_list_ptr; /* used for server only */

    /* file and watch descriptors used to monitor QEMU_CHANNEL_DIR */
    int inotify_dir_fd ;
    int inotify_dir_wd ;



} ctrl_type ;

ctrl_type * get_ctrl_ptr ( void );



int send_cmd_to_guestServer ( string hostname, unsigned int cmd, string uuid, bool reporting, string event="unknown" );

/**
 * @} guest_services_base
 */

#endif /* __INCLUDE_GUESTBASE_H__ */
