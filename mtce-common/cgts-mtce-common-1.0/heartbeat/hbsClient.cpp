/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Nodal Health Check Client Daemon
  */

/**************************************************************
 *            Implementation Structure
 **************************************************************
 *
 * Call sequence:
 *
 *    daemon_init
 *       daemon_files_init
 *       daemon_configure
 *       daemon_signal_init
 *       hbs_message_init
 *       hbs_socket_init
 *
 *    daemon_service_run
 *       forever ( timer_handler )
 *           _service_pulse_request
 *
 * Note: Interface implementation is in opposite
 *       order of the following call sequence
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>        /* for hostent */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>       /* for close and usleep */
#include <sched.h>        /* for realtime scheduling api */
#include <linux/rtnetlink.h>  /* for ... RTMGRP_LINK         */

using namespace std;

#include "nodeBase.h"
#include "nodeUtil.h"      /* for ... common utilities                     */
#include "daemon_ini.h"    /* Ini Parser Header                            */
#include "daemon_common.h" /* Common definitions and types for daemons     */
#include "daemon_option.h" /* Common options  for daemons                  */
#include "nodeTimers.h"    /* for ... maintenance timers                   */
#include "nodeMacro.h"     /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET   */
#include "nlEvent.h"       /* for ... open_netlink_socket                */
#include "hbsBase.h"       /* Heartbeat Base Header File                   */

extern "C"
{
    #include "amon.h"      /* for ... active monitoring utilities      */
}

/* Where to send events */
string mtcAgent_ip = "" ;

/* A boolean that is used to quickly determine if the infrastructure
 * network is provisioned and configured for this daemon to use */
static bool infra_network_provisioned = false ;

/* pmon pulse count clear timer */
struct mtc_timer pmonPulse_timer ;
struct mtc_timer readyEvent_timer ;
static int pmonPulse_counter = 0 ;
typedef struct
{
    struct mtc_timer stallMon_timer ;
    struct mtc_timer stallPol_timer ;
    bool          monitor_mode             ;
    bool          recovery_mode            ;
    unsigned int  b2b_pmond_pulse_misses   ;
    int           monitored_processes      ;
    int           failures                 ;

    /* process monitor list */
    std::list<procList> proc_list ;

    /* process monitor list iterator */
    std::list<procList>::iterator proc_ptr ;
} stallMon_type ;


static char   my_hostname [MAX_HOST_NAME_SIZE+1];
static string my_macaddr = "" ;
static string my_address = "" ;
static unsigned int    my_nodetype= CGTS_NODE_NULL ;
static stallMon_type stallMon ;

void daemon_sigchld_hdlr ( void )
{
    ; /* dlog("Received SIGCHLD ... no action\n"); */
}

/**
 * Daemon Configuration Structure - The allocated struct
 * @see daemon_common.h for daemon_config_type struct format.
 */
static daemon_config_type hbs_config ;
daemon_config_type * daemon_get_cfg_ptr () { return &hbs_config ; }

/**
 * Messaging Socket Control Struct - The allocated struct
 * @see hbsBase.h for hbs_socket_type struct format.
 */
static hbs_socket_type hbs_sock   ;

void _close_pulse_rx_sock ( int iface )
{
    if ( hbs_sock.rx_sock[iface] )
    {
        delete (hbs_sock.rx_sock[iface]);
        hbs_sock.rx_sock[iface] = 0 ;
    }
}

void _close_pulse_tx_sock ( int iface )
{
    if ( hbs_sock.tx_sock[iface] )
    {
        delete (hbs_sock.tx_sock[iface]);
        hbs_sock.tx_sock[iface] = 0 ;
    }
}

/* Cleanup exit handler */
void daemon_exit ( void )
{
    daemon_dump_info  ();
    daemon_files_fini ();

    /* Close the heatbeat sockets */
    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        _close_pulse_tx_sock ( i );
        _close_pulse_rx_sock ( i );
    }

    /* Close the pmond pulse socket */
    if (       hbs_sock.pmon_pulse_sock )
        delete (hbs_sock.pmon_pulse_sock );

    if ( hbs_sock.netlink_sock > 0 )
       close (hbs_sock.netlink_sock);

    exit (0);
}


void timer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    UNUSED(sig);
    UNUSED(uc);

    if ( !(*tid_ptr) )
    {
        // tlog ("Called with a NULL Timer ID\n");
        return ;
    }
    /* is it the pmon pulse counter clear timer */
    else if (( *tid_ptr == pmonPulse_timer.tid ) )
    {
        pmonPulse_counter = 0 ;
    }
    /* is it the ready event timer ring */
    else if (( *tid_ptr == readyEvent_timer.tid ) )
    {
        readyEvent_timer.ring = true ;
    }
    /* is it the monitor interval stats poll timer */
    else if (( *tid_ptr == stallMon.stallPol_timer.tid ) )
    {
        mtcTimer_stop_int_safe ( stallMon.stallPol_timer );
        stallMon.stallPol_timer.ring = true ;
    }
    /* is it the monitor timer */
    else if (( *tid_ptr == stallMon.stallMon_timer.tid ) )
    {
        mtcTimer_stop_int_safe ( stallMon.stallMon_timer );
        stallMon.stallMon_timer.ring = true ;
    }
    else
    {
        mtcTimer_stop_tid_int_safe (tid_ptr);
    }
}

void stallMon_init ( void )
{
    procList temp ;

    dlog ("initializing Stall Monitor\n");

    /* Stop all the timers if they are running */
    if ( stallMon.stallMon_timer.tid )
        mtcTimer_stop ( stallMon.stallMon_timer );
    if ( stallMon.stallPol_timer.tid )
        mtcTimer_stop ( stallMon.stallPol_timer );

    /* process monitor constructs and controls */
    stallMon.b2b_pmond_pulse_misses = 0 ;
    stallMon.monitor_mode  = false ;
    stallMon.recovery_mode = false ;
    stallMon.failures = 0 ;
    stallMon.proc_list.clear();
    stallMon.monitored_processes = stallMon.proc_list.size() ;
 
    temp.status     = RETRY ;
    temp.pid        = 0 ;
    temp.stalls     = 0 ;
    temp.periods    = 0 ;
    temp.this_count = 0 ;
    temp.prev_count = 0 ;

    /* only support stall monitor on computes */
    if ( (my_nodetype & COMPUTE_TYPE) != COMPUTE_TYPE )
        return ;

    if (( hbs_config.mon_process_1 != NULL ) &&
        ( strncmp ( hbs_config.mon_process_1, "none" , 4 )))
    {
        temp.proc = hbs_config.mon_process_1;
        stallMon.proc_list.push_back(temp);
    }
    if (( hbs_config.mon_process_2 != NULL ) &&
        ( strncmp ( hbs_config.mon_process_2, "none" , 4 )))
    {
        temp.proc = hbs_config.mon_process_2;
        stallMon.proc_list.push_back(temp);
    }
    if (( hbs_config.mon_process_3 != NULL ) &&
        ( strncmp ( hbs_config.mon_process_3, "none" , 4 )))
    {
        temp.proc = hbs_config.mon_process_3;
        stallMon.proc_list.push_back(temp);
    }
    if (( hbs_config.mon_process_4 != NULL ) &&
        ( strncmp ( hbs_config.mon_process_4, "none" , 4 )))
    {
        temp.proc = hbs_config.mon_process_4;
        stallMon.proc_list.push_back(temp);
    }
    if (( hbs_config.mon_process_5 != NULL ) &&
        ( strncmp ( hbs_config.mon_process_5, "none" , 4 )))
    {
        temp.proc = hbs_config.mon_process_5;
        stallMon.proc_list.push_back(temp);
    }
    if (( hbs_config.mon_process_6 != NULL ) &&
        ( strncmp ( hbs_config.mon_process_6, "none" , 4 )))
    {
        temp.proc = hbs_config.mon_process_6;
        stallMon.proc_list.push_back(temp);
    }
    if (( hbs_config.mon_process_7 != NULL ) &&
        ( strncmp ( hbs_config.mon_process_7, "none" , 4 )))
    {
        temp.proc = hbs_config.mon_process_7;
        stallMon.proc_list.push_back(temp);
    }

    for ( stallMon.proc_ptr  = stallMon.proc_list.begin();
          stallMon.proc_ptr != stallMon.proc_list.end();
          stallMon.proc_ptr++ )
    {
        ilog ("Monitor Proc: %s\n", stallMon.proc_ptr->proc.c_str());
    }

    stallMon.monitored_processes = stallMon.proc_list.size() ;
}

/** Client Config mask */
#define CONFIG_CLIENT_MASK      (CONFIG_AGENT_MULTICAST       |\
                                 CONFIG_AGENT_HBS_INFRA_PORT  |\
                                 CONFIG_AGENT_HBS_MGMNT_PORT  |\
                                 CONFIG_CLIENT_HBS_INFRA_PORT |\
                                 CONFIG_CLIENT_HBS_MGMNT_PORT |\
                                 CONFIG_CLIENT_PULSE_PORT     |\
                                 CONFIG_SCHED_PRIORITY)

/* Startup config read */
static int hbs_config_handler ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "multicast"))
    {
        config_ptr->multicast = strdup(value);
        config_ptr->mask |= CONFIG_AGENT_MULTICAST ;
    }
    else if (MATCH("agent", "hbs_agent_mgmnt_port"))
    {
        config_ptr->hbs_agent_mgmnt_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_HBS_MGMNT_PORT ;
    }
    else if (MATCH("client", "hbs_client_mgmnt_port"))
    {
        config_ptr->hbs_client_mgmnt_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_HBS_MGMNT_PORT ;
    }
    else if (MATCH("agent", "mtc_rx_mgmnt_port"))
    {
        config_ptr->mtc_rx_mgmnt_port = atoi(value);
    }
    else if (MATCH("debug", "stall_mon_start_delay"))
    {
        config_ptr->start_delay = atoi(value);
    }
    else if (MATCH("agent", "hbs_agent_infra_port"))
    {
        config_ptr->hbs_agent_infra_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_HBS_INFRA_PORT ;
    }
    else if (MATCH("client", "hbs_client_infra_port"))
    {
        config_ptr->hbs_client_infra_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_HBS_INFRA_PORT ;
    }
    else if (MATCH("client", "scheduling_priority"))
    {
        int max = sched_get_priority_max(SCHED_FIFO);
        int min = sched_get_priority_min(SCHED_FIFO);

        config_ptr->scheduling_priority = atoi(value);
        config_ptr->mask |= CONFIG_SCHED_PRIORITY ;

        if (( config_ptr->scheduling_priority < min) ||
            ( config_ptr->scheduling_priority > max))
        {
            wlog ("Invalid scheduling priority, overriding to min of %d\n", min );
            wlog ("Specified value of %d is out of acceptable range (%d-%d)\n",
                   config_ptr->scheduling_priority, min, max );
            config_ptr->scheduling_priority = min ;
        }
    }
    else if (MATCH("client", "pmon_pulse_port"))
    {
        config_ptr->pmon_pulse_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_PULSE_PORT ;
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
}

/* Read the hbs.ini file and load agent    */
/* settings into the daemon configuration  */
int daemon_configure ( void )
{
    int rc = FAIL ;

    hbs_config.start_delay = 300 ;

    /* Read the ini */
    hbs_config.mask = 0 ;
    if (ini_parse(MTCE_CONF_FILE, hbs_config_handler, &hbs_config) < 0)
    {
        elog("Failed to load '%s'\n", MTCE_CONF_FILE );
        return(FAIL_LOAD_INI);
    }
    if (ini_parse(MTCE_INI_FILE, hbs_config_handler, &hbs_config) < 0)
    {
        elog("Failed to load '%s'\n", MTCE_INI_FILE );
        return(FAIL_LOAD_INI);
    }

    get_debug_options ( MTCE_CONF_FILE, &hbs_config );

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( hbs_config.mask != CONFIG_CLIENT_MASK )
    {
        elog ("Client configuration failed (%x)\n",
             (( -1 ^ hbs_config.mask ) & CONFIG_CLIENT_MASK) );
        rc = FAIL_INI_CONFIG ;
    }
    else
    {
        ilog("Realtime Pri: FIFO/%i \n", hbs_config.scheduling_priority );
        ilog("Multicast: %s\n", hbs_config.multicast );

        hbs_config.mgmnt_iface = daemon_get_iface_master ( hbs_config.mgmnt_iface );
        ilog("Mgmnt iface : %s\n", hbs_config.mgmnt_iface );
        ilog("Mgmnt RxPort: %d\n", hbs_config.hbs_client_mgmnt_port );
        ilog("Mgmnt TxPort: %d\n", hbs_config.hbs_agent_mgmnt_port );

        get_iface_macaddr  ( hbs_config.mgmnt_iface, my_macaddr );
        get_iface_address  ( hbs_config.mgmnt_iface, my_address, true );
        get_hostname       ( &my_hostname[0], MAX_HOST_NAME_SIZE );

        /* Fetch the infrastructure interface name.
         * calls daemon_get_iface_master inside so the
         * aggrigated name is returned if it exists */
        get_infra_iface (&hbs_config.infra_iface );
        if ( strlen(hbs_config.infra_iface)  )
        {
            infra_network_provisioned = true ;
            ilog ("Infra iface : %s\n", hbs_config.infra_iface );
        }
        ilog("Infra RxPort: %d\n", hbs_config.hbs_client_infra_port );
        ilog("Infra TxPort: %d\n", hbs_config.hbs_agent_infra_port );

        /* initialize the stall detection monitor */
        stallMon_init ();

        ilog("Procmon Thld: %d pmond pulse misses\n", hbs_config.stall_pmon_thld );
        ilog("Recover Thld: %d process stalls\n", hbs_config.stall_rec_thld );
        ilog("Monitor |--|: %d secs\n", hbs_config.stall_mon_period );
        ilog("Monitor Poll: %d secs\n", hbs_config.stall_poll_period );
        ilog("Monitor Dlay: %d secs\n", hbs_config.start_delay );

        rc = PASS ;
    }

    return (rc);
}

/****************************/
/* Initialization Utilities */
/****************************/

/* Initialize the unicast pulse response message  */
/* One time thing ; tx same message all the time. */
int hbs_message_init ( void )
{
    /* Build the transmit pulse response message for each interface */
    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        memset ( &hbs_sock.tx_mesg[i], 0, sizeof (hbs_message_type));
        memcpy ( &hbs_sock.tx_mesg[i].m[0], &rsp_msg_header[0], HBS_HEADER_SIZE );
        memcpy ( &hbs_sock.tx_mesg[i].m[HBS_HEADER_SIZE], my_hostname, strlen(my_hostname));
    }
    return (PASS);
}

/* Initialize pulse messaging for the specified interface
 * This is called by a macro defined in hbsBase.h */
int _setup_pulse_messaging ( iface_enum i, int rmem )
{
    int rc = PASS ;
    char * iface = NULL ;

    /* client sockets are not modified */
    UNUSED(rmem);

    /* Load up the interface name */
    if ( i == MGMNT_IFACE )
    {
        iface = hbs_config.mgmnt_iface ;
    }
    else if (( i == INFRA_IFACE ) && ( hbs_config.infra_iface != NULL ))
    {
        iface = hbs_config.infra_iface ;
    }
    else
    {
        wlog ("No Infrastructure Interface\n");
        return (RETRY);
    }

    _close_pulse_rx_sock (i);
    _close_pulse_tx_sock (i);

    /********************************************************************/
    /* Setup multicast Pulse Request Receive Socket                     */
    /********************************************************************/

    hbs_sock.rx_sock[i] =
    new msgClassRx(hbs_config.multicast,hbs_sock.rx_port[i],IPPROTO_UDP,iface,true,true);
    if (hbs_sock.rx_sock[i]->return_status != PASS)
    {
        elog("Cannot create socket (%d) (%d:%m)\n", i, errno );
        _close_pulse_rx_sock (i);
        return (FAIL_SOCKET_CREATE);
    }
    hbs_sock.rx_sock[i]->sock_ok(true);

    /* Setup unicast transmit (reply) socket */
    hbs_sock.tx_sock[i] =
    new msgClassTx(hbs_config.multicast,hbs_sock.tx_port[i],IPPROTO_UDP, iface);
    if (hbs_sock.tx_sock[i]->return_status != PASS)
    {
        elog("Cannot create unicast transmit socket (%d) (%d:%m)\n", i, errno );
        _close_pulse_tx_sock(i);
        return (FAIL_SOCKET_CREATE);
    }
    hbs_sock.tx_sock[i]->sock_ok(true);

    /* set this tx socket interface with priority class messaging */
    hbs_sock.tx_sock[i]->setPriortyMessaging( iface );

    return (rc);
}

void _close_hbs_ready_tx_socket ( void )
{
    if (hbs_sock.hbs_ready_tx_sock)
    {
        delete (hbs_sock.hbs_ready_tx_sock);
        hbs_sock.hbs_ready_tx_sock = 0 ;
    }
}

void setup_ready_tx_socket ( void )
{
    _close_hbs_ready_tx_socket ();
    mtcAgent_ip = getipbyname ( CONTROLLER );
    hbs_sock.hbs_ready_tx_sock = new msgClassTx(mtcAgent_ip.c_str(), hbs_config.mtc_rx_mgmnt_port, IPPROTO_UDP, hbs_config.mgmnt_iface);

    if ( hbs_sock.hbs_ready_tx_sock )
    {
        /* look for fault insertion request */
        if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_TXSOCK ) )
            hbs_sock.hbs_ready_tx_sock->return_status = FAIL ;

        if ( hbs_sock.hbs_ready_tx_sock->return_status == PASS )
        {
            hbs_sock.hbs_ready_tx_sock->sock_ok(true);
            // ilog ("Ready Event TX Socket setup Ok \n");
        }
        else
        {
            elog ("failed to init 'ready event tx' socket (rc:%d)\n",
            hbs_sock.hbs_ready_tx_sock->return_status );
            hbs_sock.hbs_ready_tx_sock->sock_ok(false);
        }
    }
}

/* Construct the messaging sockets          *
 * 1. multicast receive socket (rx_sock)  *
 * 2. unicast transmit socket (tx_sock)   */
int hbs_socket_init ( void )
{
    int    rc = PASS ;
    int    on = 1 ;

    /* set rx socket buffer size to rmem_max */
    int rmem_max = daemon_get_rmem_max () ;

    setup_ready_tx_socket ();

    /* Read the port config strings into the socket struct
     *
     * These ports are swapped compared to the hbsAgent
     *
     * From the client perspective
     *     rx_port is the hbs_client_..._port
     *     tx_port is the hbs_agent_..._port
     *
     */
    hbs_sock.rx_port[MGMNT_IFACE] = hbs_config.hbs_client_mgmnt_port;
    hbs_sock.tx_port[MGMNT_IFACE] = hbs_config.hbs_agent_mgmnt_port ;
    hbs_sock.rx_port[INFRA_IFACE] = hbs_config.hbs_client_infra_port;
    hbs_sock.tx_port[INFRA_IFACE] = hbs_config.hbs_agent_infra_port ;

    /* Setup the pulse messaging interfaces */
    SETUP_PULSE_MESSAGING(infra_network_provisioned, rmem_max ) ;

    /***********************************************************/
    /* Setup the PMON I'm Alive Pulse Receive Socket           */
    /***********************************************************/

    hbs_sock.pmon_pulse_sock = new msgClassRx(LOOPBACK_IP,hbs_config.pmon_pulse_port,IPPROTO_UDP);
    if ( rc ) return (rc) ;
    hbs_sock.pmon_pulse_sock->sock_ok(true);

    /***************************************************
     * Open the active monitoring socket
     ***************************************************/

    char filename [MAX_FILENAME_LEN] ;
    string port_string ;

    snprintf ( filename , MAX_FILENAME_LEN, "%s/%s.conf", PMON_CONF_FILE_DIR, program_invocation_short_name ) ;

    if ( ini_get_config_value ( filename, "process", "port", port_string , false ) != PASS )
    {
        elog ("failed to get active monitor port from %s\n", filename );
        hbs_sock.amon_socket = 0 ;
        return (FAIL_SOCKET_CREATE);
    }

    hbs_sock.amon_socket = active_monitor_initialize ( program_invocation_short_name, atoi(port_string.data()));
    ilog ("Active Monitor Socket %d\n", hbs_sock.amon_socket );
    if ( 0 > hbs_sock.amon_socket )
        hbs_sock.amon_socket = 0 ;

    /* Make the active monitor socket non-blocking */
    rc = ioctl(hbs_sock.amon_socket, FIONBIO, (char *)&on);
    if ( 0 > rc )
    {
        elog ("Failed to set amon socket non-blocking (%d:%m)\n", errno);
        return (FAIL_SOCKET_NOBLOCK);
    }

    return (PASS);
}


/* Get Process Monitor Pulse message */
int get_pmon_pulses ( void )
{
    mtc_message_type msg ;
    int bytes = 0 ;
    int count = 0 ;
    int expected_bytes = ((sizeof(mtc_message_type))-(BUF_SIZE)) ;
    #define MAX_ERRORS 20

    /* Default to no pulse received */
    int pulses = 0 ;

    /* Empty the receive buffer. */
    do
    {
        /* Receive event messages */
        memset ( &msg , 0, sizeof(mtc_message_type));
        bytes = hbs_sock.pmon_pulse_sock->read((char*)&msg, sizeof(mtc_message_type));
        if ( bytes == expected_bytes )
        {
            if ( !strncmp ( &msg.hdr[0] , get_pmond_pulse_header(), MSG_HEADER_SIZE ))
            {
                pulses++ ;
                mlog ("Pmon Pulse (%s) (%d)\n", msg.hdr, pulses );
            }
            else
            {
                /* gracefully deal with error case - "count"
                 * is incremented in this macro */
                wlog_throttled ( count, MAX_ERRORS,
                        "Invalid pmon pulse message (bytes=%d)\n", bytes );
            }

            /* get out if we are seeing a bunch of errors */
            if ( count > MAX_ERRORS )
                return (pulses);
        }
    } while ( bytes == expected_bytes ) ;
    return (pulses);
}

static unsigned int my_rri = 0 ;

/*************************************************************
 *
 * Name       : _service_pulse_request
 *
 * Receive the controller's multicast pulse request messages
 * and send a unicast reply to the sender on the same network.
 *
 * This utility supports the following networks
 *
 *  - management network
 *  - infrastructure network (if configured)
 *
 * For each message, look inside the message for
 *
 *  1. the header key
 *  2. the hostname key.
 *       if the hostname key matches this hosts's name
 *       then cache the clue key (RRI: resource reference
 *       identifier) and sent it back in every response.
 *       If a different clue key is found in a later message
 *       update the cached one with the new one and use it
 *       instead.
 *
 *       The clue key can change at any time without prior
 *       notice.
 *
 *       Just send back zero until the first hostname key
 *       match is found.
 *
 * Receive message Format:
 *
 * "cgts pulse req:xxx.xxx.xxx <clue_hostname> <clue key>
 *
 * Construct a response message containing a response header
 * key, this node's hostname and he cached clue key (RRI)
 * and send the response back to the controller that sent it
 *
 **************************************************************/

static int rx_error_count[MAX_IFACES] = {0,0} ;
static int tx_error_count[MAX_IFACES] = {0,0} ;

int _service_pulse_request ( iface_enum iface , unsigned int flags )
{
    unsigned int s  = 0 ; /* Sequence number       */
    int          n  = 0 ; /* message size          */
    int          rc = 0 ;

    if (( iface != MGMNT_IFACE ) && ( iface != INFRA_IFACE ))
        return (FAIL_BAD_CASE);

    memset (   (char*) &hbs_sock.rx_mesg[iface], 0, sizeof(hbs_message_type));
    if ( ! hbs_sock.rx_sock[iface] )
    {
        elog ("cannot receive from null rx_mesg[%s] socket\n", get_iface_name_str(iface) );
        return (FAIL_TO_RECEIVE);
    }
    else if ( hbs_sock.rx_sock[iface]->sock_ok() == false )
    {
        elog ("cannot receive from failed rx_mesg[%s] socket\n", get_iface_name_str(iface) );
        return (FAIL_TO_RECEIVE);
    }

    n = hbs_sock.rx_sock[iface]->read((char*)&hbs_sock.rx_mesg[iface], sizeof(hbs_message_type));

    if( n < HBS_HEADER_SIZE )
    {
        rx_error_count[iface]++ ;

        /* throtle the log so that if they come back-to-back we avoid flooding */
        if ( n == -1 )
        {
            if ( rx_error_count[iface] > 1 )
            {
                wlog_throttled ( rx_error_count[iface], 500, "%s receive error (%d:%m)\n", get_iface_name_str(iface), errno );
            }
        }
        else
        {
            wlog_throttled ( rx_error_count[iface], 500, "%s message underrun (expected %ld but got %d)\n",
                             get_iface_name_str(iface), sizeof(hbs_message_type), n );
        }
        if ( rx_error_count[iface] == 100 )
        {
            wlog ( "%s is getting a lot of receive errors (%d:%m)\n", get_iface_name_str(iface), errno );
        }
        return (FAIL_TO_RECEIVE);
    }

    /* Clear the error count since we got a good receive */
    rx_error_count[iface] = 0 ;

#ifdef WANT_NO_SELF_HEARTBEAT_REPLY
    /* Don't reply to the heartbeat if the request came from myself */
    if ( ! strncmp ( my_address.data(),
                     hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                     MAX_CHARS_IN_IP_ADDR ))
    {
        ilog ("%s Refusing to send heartbeat response to self\n", hbs_sock.rx_sock[iface]->get_dst_addr()->toString());
        return (PASS);
    }
#else
    /* We use this to monitor pmond on active controller */
#endif

    /* Save the sequence number */
    s = hbs_sock.rx_mesg[iface].s ;

    mlog ("\n");
    mlog ("%s Pulse Req: %s:%5d: %d: :%s RRI:%d\n", get_iface_name_str(iface),
              hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
              hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                      hbs_sock.rx_mesg[iface].s,
                      hbs_sock.rx_mesg[iface].m,
                      hbs_sock.rx_mesg[iface].c);

    if ( strncmp ( (const char *)&hbs_sock.rx_mesg[iface].m, (const char *)&req_msg_header, HBS_HEADER_SIZE ))
    {
        wlog_throttled ( rx_error_count[iface], 200, "%s Invalid header (%d:%s)\n",
                get_iface_name_str(iface),
                hbs_sock.rx_mesg[iface].s,
                hbs_sock.rx_mesg[iface].m );

        mlog ("Detected: %d <%s>\n", HBS_HEADER_SIZE,hbs_sock.rx_mesg[iface].m);
        mlog ("Expected: %d <%s>\n", HBS_HEADER_SIZE,req_msg_header);
        return (FAIL_MSG_HEADER) ;
    }

    /* Manage the Resource Reference Index (RRI) "lookup clue" */
    if ( ! strncmp ( &hbs_sock.rx_mesg[iface].m[HBS_HEADER_SIZE], &my_hostname[0], MAX_CHARS_HOSTNAME ))
    {
        if( my_rri!= hbs_sock.rx_mesg[iface].c )
        {
            my_rri = hbs_sock.rx_mesg[iface].c ;
            ilog ("%s Caching New RRI: %d\n", &my_hostname[0], my_rri );
        }
    }

    /* Add my RRI to the response message */
    hbs_sock.tx_mesg[iface].c = my_rri ;

    /* Clear struct */
    hbs_sock.tx_mesg[iface].s = s     ;
    hbs_sock.tx_mesg[iface].f = flags ;
    if ( pmonPulse_counter )
    {
        hbs_sock.tx_mesg[iface].f |= ( PMOND_FLAG ) ;
    }
    if ( infra_network_provisioned == true )
    {
        hbs_sock.tx_mesg[iface].f |= INFRA_FLAG ;
    }

    n = (int)sizeof(hbs_message_type) ;

    if ( ! hbs_sock.tx_sock[iface] )
    {
        elog ("cannot send to null tx_mesg[%s] socket\n", get_iface_name_str(iface) );
        return (FAIL_TO_TRANSMIT);
    }
    else if ( hbs_sock.tx_sock[iface]->sock_ok() == false )
    {
        elog ("cannot send to failed tx_mesg[%s] socket\n", get_iface_name_str(iface) );
        return (FAIL_TO_TRANSMIT);
    }

#ifdef WANT_PULSE_RESPONSE_FIT
    if (( iface == INFRA_IFACE ) && ( daemon_is_file_present ( MTC_CMD_FIT__NO_INFRA_RSP )))
    {
        wlog ("refusing to send %s pulse reply ; due to FIT\n", get_iface_name_str(iface));
        return PASS ;
    }

    if (( iface == MGMNT_IFACE ) && ( daemon_is_file_present ( MTC_CMD_FIT__NO_MGMNT_RSP )))
    {
        wlog ("refusing to send %s pulse reply ; due to FIT\n", get_iface_name_str(iface));
        return PASS ;
    }
#endif

    /* Send pulse response message with sequence number, flags and resource referecen index */
    rc = hbs_sock.tx_sock[iface]->reply(hbs_sock.rx_sock[iface],(char*)&hbs_sock.tx_mesg[iface], n);
    if ( rc == -1 )
    {
        elog ("Failed to sendto socket %d through %s:%d len:%d (%s) (%d:%s)\n",
                    hbs_sock.tx_sock[iface]->getFD(),
                    hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
                    hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
                    hbs_sock.tx_sock[iface]->get_dst_addr()->getSockLen(),
                    get_iface_name_str(iface), errno, strerror(errno));
    }
    else if ( rc != n)
    {
        /* Avoid log flooding
        elog ("unicast send failed. (%d)\n", rc); */
        wlog_throttled ( tx_error_count[iface], 200,
                         "%s Pulse Rsp: %d:%d bytes < %d:%s > to <%s>\n",
                         get_iface_name_str(iface), n, rc,
                         hbs_sock.tx_mesg[iface].s,
                        &hbs_sock.tx_mesg[iface].m[0],
                        &hbs_sock.rx_mesg[iface].m[0]);
        return (rc);
    }
    else
    {
        mlog ("%s Pulse Rsp: %s:%5d: %d:%d:%s RRI:%d (%d)\n",
                get_iface_name_str(iface),
                hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
                hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
                             hbs_sock.tx_mesg[iface].s,
                             hbs_sock.tx_mesg[iface].f,
                             hbs_sock.tx_mesg[iface].m,
                             hbs_sock.tx_mesg[iface].c,
                             pmonPulse_counter);
        /* Clear the error count since we got a good transmit */
        tx_error_count[iface] = 0 ;
    }
    return PASS;
}

#ifdef WANT_FIT_TESTING
static int fit_log_count = 0 ;
#endif

int hbs_send_event ( unsigned int event )
{
    mtc_message_type msg ;

    int rc    = FAIL_BAD_PARM ;
    int bytes = 0    ;

    memset (&msg, 0 , sizeof(mtc_message_type));

    if ( event != MTC_EVENT_MONITOR_READY)
    {
        slog ("Unsupported event (%08x)\n", event );
        return (rc);
    }

#ifdef WANT_FIT_TESTING
    if (( hbs_config.testmode ) &&
        ( hbs_config.testmask == FIT_CODE__NO_READY_EVENT ))
    {
        slog ("FIT: bypassing 'ready event' send\n");
        return PASS ;
    }

    if ( daemon_is_file_present ( "/tmp/no_ready_event" ) == true )
    {
        ilog_throttled ( fit_log_count, 100, "FIT: bypassing 'ready event' send\n");
        return PASS ;
    }
#endif

    /* build the message */
    snprintf ( &msg.hdr[0], MSG_HEADER_SIZE, "%s", get_mtce_event_header());
    snprintf ( &msg.hdr[MSG_HEADER_SIZE], MAX_CHARS_HOSTNAME, "%s", &my_hostname[0]);

    #define MAX_PROC_NAME_SIZE (64)
    snprintf ( &msg.buf[0], MAX_PROC_NAME_SIZE, "%s", program_invocation_short_name);

    size_t len = strlen(program_invocation_short_name);
    bytes = ((sizeof(mtc_message_type))-(BUF_SIZE-len));
    msg.cmd = event ;

    if (( hbs_sock.hbs_ready_tx_sock ) &&
        ( hbs_sock.hbs_ready_tx_sock->sock_ok() == true ))
    {
        mlog ("Ready message\n");
        if ((rc = hbs_sock.hbs_ready_tx_sock->write((char*)&msg.hdr[0], bytes))!= bytes )
        {
            elog ("Ready message send failed (%d) (%d:%s)\n", rc, errno, strerror(errno) );
            rc = FAIL_SOCKET_SENDTO ;
        }
        else
        {
            mlog2 ("Transmit: %x bytes to %s:%d\n", bytes,
                    hbs_sock.hbs_ready_tx_sock->get_dst_str(),
                    hbs_sock.hbs_ready_tx_sock->get_dst_addr()->getPort());
            print_mtc_message ( &msg );
            rc = PASS ;
        }
    }
    else
    {
       rc = FAIL_NULL_POINTER ;
       elog ("cannot send to null or failed 'hbs_ready_tx_sock'\n");
    }
    return rc ;

}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodeType_str )
{
    int rc = PASS ;

    /* Initialize socket construct and pointer to it */
    memset ( &hbs_sock, 0, sizeof(hbs_sock));

    /* Defaults */
    hbs_config.stall_pmon_thld   = -1 ;
    hbs_config.stall_mon_period  = MTC_HRS_8 ;
    hbs_config.stall_poll_period = MTC_HRS_8 ;
    hbs_config.stall_rec_thld    = 100 ;

    mtcTimer_init ( stallMon.stallMon_timer );
    mtcTimer_init ( stallMon.stallPol_timer );

    /* Assign interface to config */
    hbs_config.mgmnt_iface = (char*)iface.data() ;

    if ( (rc = daemon_files_init ( )) != PASS )
    {
        elog ("Pid, log or other files could not be opened (rc:%d)\n", rc );
        rc = FAIL_FILES_INIT ;
    }

    /* convert node type to integer */
    my_nodetype = get_host_function_mask ( nodeType_str ) ;
    ilog ("Node Type   : %s (%d)\n", nodeType_str.c_str(), my_nodetype );

    /* Bind signal handlers */
    if ( daemon_signal_init () != PASS )
    {
       elog ("daemon_signal_init failed\n");
       return ( FAIL_SIGNAL_INIT );
    }

   /************************************************************************
    * There is no point continuing with init ; i.e. running daemon_configure,
    * initializing sockets and trying to query for an ip address until the
    * daemon's configuration requirements are met. Here we wait for those
    * flag files to be present before continuing.
    ************************************************************************
    * Wait for /etc/platform/.initial_config_complete & /var/run/goenabled */
    daemon_wait_for_file ( CONFIG_COMPLETE_FILE , 0);

    /* Don't initialize messaging till we have the goenabled signal */
    if ( is_combo_system ( my_nodetype ) == true )
    {
        daemon_wait_for_file ( GOENABLED_SUBF_PASS , 0);
        ilog ("GOENABLE (small system)\n");
    }
    else
    {
        daemon_wait_for_file ( GOENABLED_MAIN_PASS , 0);
        ilog ("GOENABLE (large system)\n");
    }

    /* Configure the client */
    if ( (rc = daemon_configure ()) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%d)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }

    /* Init the heartbeat transmit pulse response message */
    else if ( hbs_message_init () != PASS )
    {
         elog ("Failed to initialize pulse response message\n");
         rc = FAIL_MESSAGE_INIT ;
    }
    /* Setup the heartbeat service messaging sockets */
    else if ( hbs_socket_init () != PASS )
    {
        elog ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT;
    }
    return (rc);
}

#define SPACE ' '
#define ARROW '<'

int stall_threshold_log = 0 ;
int stall_times_threshold_log = 0 ;
#define MAX_LEN 300
void daemon_service_run ( void )
{
#ifdef WANT_DAEMON_DEBUG
    time_debug_type before ;
    time_debug_type after  ;
    time_delta_type delta  ;
    time_delta_type select_delta ;
    char arrow = SPACE ;
    char str [MAX_LEN] ;
    int  num        = 0 ;
    int  flush_thld = 0 ;
#endif

    bool stall_monitor_ready       = false ;

    unsigned int flags  = 0 ;
             int rc     = 0 ;
             int count  = 0 ;

    /* Make the main loop schedule in real-time */
    struct sched_param param ;
    memset ( &param, 0, sizeof(struct sched_param));
    param.sched_priority = hbs_config.scheduling_priority ;
    if ( sched_setscheduler(0, SCHED_FIFO, &param) )
    {
        elog ("sched_setscheduler (0, SCHED_FIFO, %d ) returned error (%d:%s)\n",
               param.sched_priority, errno, strerror(errno));
    }

    if (( hbs_sock.ioctl_sock = open_ioctl_socket ( )) <= 0 )
    {
        elog ("Failed to create ioctl socket");
        daemon_exit ();
    }

    /* Not monitoring address changes RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR */
    if (( hbs_sock.netlink_sock = open_netlink_socket ( RTMGRP_LINK )) <= 0 )
    {
        elog ("Failed to create netlink listener socket");
        daemon_exit ();
    }

    hbs_sock.amon_socket = active_monitor_get_sel_obj ();
    std::list<int> socks ;
    socks.clear();
    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        if (hbs_sock.rx_sock[i] && hbs_sock.rx_sock[i]->getFD() > 0 )
        {
            socks.push_front (hbs_sock.rx_sock[i]->getFD());
        }
    }
    socks.push_front (hbs_sock.pmon_pulse_sock->getFD());
    socks.push_front (hbs_sock.amon_socket );
    socks.push_front (hbs_sock.netlink_sock);

    socks.sort();

    bool locked = daemon_is_file_present ( NODE_LOCKED_FILE ) ;

    ilog ("Pmon Pulse Counter Timer init with %d seconds timeout\n", hbs_config.start_delay );
    mtcTimer_init  ( pmonPulse_timer , &my_hostname[0], "pmon pulse count clear timer" );
    mtcTimer_start ( pmonPulse_timer, timer_handler, 5 );

    ilog ("Process Stall-Monitor starting in %d seconds\n", hbs_config.start_delay );
    mtcTimer_start ( stallMon.stallMon_timer, timer_handler, hbs_config.start_delay );

    ilog ("Ready Event Period %d seconds\n", MTC_SECS_5 );
    mtcTimer_start ( readyEvent_timer, timer_handler, MTC_SECS_5 );

    ilog ("Sending Heartbeat Ready Event\n");
    hbs_send_event ( MTC_EVENT_MONITOR_READY );

    /* Run heartbeat service forever or until stop condition */
    for ( ;  ; )
    {
        hbs_sock.waitd.tv_sec = 0;
        hbs_sock.waitd.tv_usec = SOCKET_WAIT;

#ifdef WANT_DAEMON_DEBUG
        if ( hbs_config.flush_thld != 0 )
        {
            if ( debug_level ( DEBUG_MEM_LOG ) )
            {
                gettime (before);
            }
        }

        /* Initialize the timeval struct  */
        if ( hbs_config.flush_thld == 0 )
        {
            hbs_sock.waitd.tv_usec = hbs_config.testmask ;
        }
#endif

        /* Initialize the master fd_set */
        FD_ZERO(&hbs_sock.readfds);
        for ( int i = 0 ; i < MAX_IFACES ; i++ )
        {
            if (hbs_sock.rx_sock[i] && hbs_sock.rx_sock[i]->getFD() > 0 )
            {
                FD_SET(hbs_sock.rx_sock[i]->getFD(),&hbs_sock.readfds);
            }
        }
        FD_SET(hbs_sock.pmon_pulse_sock->getFD(),&hbs_sock.readfds);
        FD_SET(hbs_sock.amon_socket,    &hbs_sock.readfds);
        FD_SET(hbs_sock.netlink_sock,   &hbs_sock.readfds);

        rc = select( socks.back()+1,
                     &hbs_sock.readfds, NULL, NULL,
                     &hbs_sock.waitd);

        if ( infra_network_provisioned == true )
        {
            flags |= INFRA_FLAG ;
        }

        /* Select error */
        if ( rc < 0 )
        {
            if ( errno != EINTR )
            {
                wlog_throttled ( count, 100, "select failed (%d:%s)\n",
                                 errno, strerror(errno));
            }
        }

        /* Only service sockets for the rc > 0 case */
        else if ( rc )
        {
            if (hbs_sock.rx_sock[MGMNT_IFACE]&&FD_ISSET(hbs_sock.rx_sock[MGMNT_IFACE]->getFD(), &hbs_sock.readfds))
            {
                /* Receive pulse request and send a response */
                /* Note: The flags are taken from the last round of get_pmon_pulses below */
                int rc = _service_pulse_request ( MGMNT_IFACE, flags );
                if ( rc != PASS )
                {
                    if ( rc == FAIL_TO_RECEIVE )
                    {
                        mlog ("Failed to receive pulse request on management network (rc:%d)\n",rc);
                    }
                    else
                    {
                        wlog_throttled ( count, 200, "Failed to service pulse request on management network (rc:%d)\n",rc);
                    }
                }
                /* Clear 'flags'. If no pmon pulses come in then flags will not be updated
                 * and we will be stuck in the last flags state */
                 flags = 0 ;
            }

            if (hbs_sock.rx_sock[INFRA_IFACE]&&FD_ISSET(hbs_sock.rx_sock[INFRA_IFACE]->getFD(), &hbs_sock.readfds))
            {
                /* Receive pulse request from the infrastructure interface and send a response */
                /* Note: The flags are taken from the last round of get_pmon_pulses below */
                int rc = _service_pulse_request ( INFRA_IFACE, flags );
                if ( rc != PASS )
                {
                    if ( rc == FAIL_TO_RECEIVE )
                    {
                        mlog ("Failed to receive pulse request on infrastructure network (rc:%d)\n",rc);
                    }
                    else
                    {
                        wlog_throttled ( count, 200, "Failed to service pulse request on infrastructure network (rc:%d)\n",rc);
                    }
                }
            }

            if ( FD_ISSET(hbs_sock.pmon_pulse_sock->getFD(), &hbs_sock.readfds))
            {
                pmonPulse_counter += get_pmon_pulses ( );
                if ( pmonPulse_counter )
                {
                    flags |= ( PMOND_FLAG ) ;
                    if ( stallMon.monitor_mode == true )
                    {
                        stallMon_init ();
                    }
                }
            }

            if ( FD_ISSET(hbs_sock.amon_socket, &hbs_sock.readfds))
            {
                dlog3 ("Active Monitor Select Fired\n");
                active_monitor_dispatch ();
            }

            if (FD_ISSET(hbs_sock.netlink_sock, &hbs_sock.readfds))
            {
                log_link_events ( hbs_sock.netlink_sock,
                                  hbs_sock.ioctl_sock,
                                  hbs_config.mgmnt_iface,
                                  hbs_config.infra_iface,
                                  hbs_sock.mgmnt_link_up_and_running,
                                  hbs_sock.infra_link_up_and_running) ;
            }
        }

        count  = 0 ;

        /* This waits for the stall monitor startup delay to expire */
        if (( stall_monitor_ready == false ) &&
            ( stallMon.stallMon_timer.ring == true ))
        {
            ilog ("Process Stall-Monitor started ...\n");
            stall_monitor_ready = true ;
        }


        if (( locked == false ) &&
            (stall_monitor_ready  == true ) &&
            ((my_nodetype & COMPUTE_TYPE) == COMPUTE_TYPE ) &&
            (!(flags & PMOND_FLAG) ))
        {
            /* This is run every 50 msec - the WAIT_SELECT time */
            if (( ++stallMon.b2b_pmond_pulse_misses > hbs_config.stall_pmon_thld ) &&
                (   stallMon.monitored_processes > 0 ))
            {
                flags |= STALL_MON_FLAG ;

                /* If monitor mode is not on now ; turn it on and start the
                 * monitor interval timer as well as the first poll interval
                 * timer */
                if ( stallMon.monitor_mode == false )
                {
                    stallMon.monitor_mode = true ;
                    mtcTimer_start ( stallMon.stallMon_timer, timer_handler, hbs_config.stall_mon_period );
                    mtcTimer_start ( stallMon.stallPol_timer, timer_handler, hbs_config.stall_poll_period );
                }
                else if ( stallMon.stallMon_timer.ring == true )
                {
                    stallMon.failures = 0 ;
                    stallMon.stallMon_timer.ring = false ;

                    /* if we get here then we may have a failure */
                    for ( stallMon.proc_ptr  = stallMon.proc_list.begin();
                          stallMon.proc_ptr != stallMon.proc_list.end();
                          stallMon.proc_ptr++ )
                    {
                        if ( stallMon.proc_ptr->stalls >= (stallMon.proc_ptr->periods-1) )
                        {
                            stallMon.failures++ ;
                        }
                    }

                    if ( stallMon.failures >= hbs_config.stall_rec_thld )
                    {
                        wlog_throttled (stall_threshold_log, 200,
                                "Host Is Stalling !!! (fails:%d thld:%d)\n",
                                stallMon.failures, hbs_config.stall_rec_thld );

                        flags |= STALL_REC_FLAG ;
                        if ( stallMon.recovery_mode == false )
                        {
                            elog ( "Host has Stalled !!! (fails:%d thld:%d)\n",
                                stallMon.failures, hbs_config.stall_rec_thld );

                            stallMon.recovery_mode = true ;
                            if ( hbs_self_recovery ( STALL_REBOOT_CMD ) != PASS )
                            {
                                flags |= STALL_ERR3_FLAG ;
                            }
                            /* Start a longer timer for the sysreq kill */
                            if ( hbs_self_recovery ( STALL_SYSREQ_CMD ) != PASS )
                            {
                                flags |= STALL_ERR4_FLAG ;
                            }
                        }
                    }
                    else
                    {
                        dlog ("Recovery Criteria Not Met\n");
                        dlog ("... only %d of %d processes failed (%d are monitored)\n",
                                   stallMon.failures,
                                   hbs_config.stall_rec_thld ,
                                   stallMon.monitored_processes);
                        dlog ("... restarting stall monitor\n");
                        stallMon_init ();
                    }
                }

                /* We should never get here ; if we do then set the STALL_REC_FAIL_FLAG */
                else if ( stallMon.recovery_mode == true )
                {
                    flags |= STALL_REC_FLAG ;
                }
                if ( stallMon.stallPol_timer.ring == true )
                {
                    int i = 0 ;
                    // stallMon.stallPol_timer.ring = false ;

                    /* TODO: Future ; track pids ans tally any pids that have changed */
                    if ( stallMon.monitored_processes != hbs_refresh_pids ( stallMon.proc_list ))
                    {
                        flags |= STALL_ERR1_FLAG ;
                    }
                    /* Count the audits */
                    hbs_process_monitor ( stallMon.proc_list );

                    /* Look over the scheduling counts and increment
                     * the stall count if they have not changed
                     * or if the status reads FAIL indicating that
                     * there was a problem getting the stat ; which
                     * qualifies as a stall failure */
                    for ( stallMon.proc_ptr  = stallMon.proc_list.begin();
                          stallMon.proc_ptr != stallMon.proc_list.end();
                          stallMon.proc_ptr++ , i++ )
                    {
                        dlog ("%s (pid:%d) counts (%llu:%llu) \n",
                                stallMon.proc_ptr->proc.c_str(),
                                stallMon.proc_ptr->pid,
                                stallMon.proc_ptr->this_count,
                                stallMon.proc_ptr->prev_count);

                        /* Increment the audit count for this process */
                        stallMon.proc_ptr->periods++ ;
                        if (( stallMon.proc_ptr->this_count == stallMon.proc_ptr->prev_count ) ||
                            ( stallMon.proc_ptr->status != PASS ))
                        {
                            /* Distinguish the stat collect failure as a
                             * stat read error compared to a stall */
                            if ( stallMon.proc_ptr->status != PASS )
                            {
                                // ilog ("%s process error\n", stallMon.proc_ptr->proc.c_str());
                                flags |= STALL_ERR2_FLAG ;
                            }
                            /* Increment the stall count for this process */
                            stallMon.proc_ptr->stalls++ ;

                            /* Set this process's stall flag */
                            int x = STALL_PID1_FLAG ;
                            flags |= ( x <<= i ) ;
                            wlog_throttled (stall_times_threshold_log, 100,
                                    "%s stalled %d times in %d periods (flags:%x) (%llu:%llu)\n",
                                    stallMon.proc_ptr->proc.c_str(),
                                    stallMon.proc_ptr->stalls,
                                    stallMon.proc_ptr->periods, flags,
                                    stallMon.proc_ptr->this_count,
                                    stallMon.proc_ptr->prev_count);
                        }
                        /* Save this count in prev for next compare */
                        stallMon.proc_ptr->prev_count = stallMon.proc_ptr->this_count ;
                    }
                    /* restart the monitor audit timer */
                    if ( mtcTimer_start ( stallMon.stallPol_timer, timer_handler, hbs_config.stall_poll_period ) != PASS )
                    {
                        flags |= STALL_REC_FAIL_FLAG ;
                        stallMon_init ();
                    }
                }
            }
        }
        else
        {
            stallMon.b2b_pmond_pulse_misses = 0 ;
            stall_threshold_log = 0 ;
            stall_times_threshold_log = 0 ;
        }

        if ( readyEvent_timer.ring == true )
        {
            hbs_send_event ( MTC_EVENT_MONITOR_READY );
            readyEvent_timer.ring = false ;
        }

        daemon_signal_hdlr ();

#ifdef WANT_DAEMON_DEBUG
        /* Support the log flush config option */
        if ( hbs_config.flush )
        {
            if ( ++flush_thld > hbs_config.flush_thld )
            {
                flush_thld = 0 ;
                fflush (stdout);
                fflush (stderr);
            }
        }
#endif
    }
    daemon_exit ();
}

/* Push daemon state to log file */
void daemon_dump_info ( void )
{
    daemon_dump_membuf_banner ();
    daemon_dump_membuf();
}

const char MY_DATA [100] = { "eieio\n" } ;
const char * daemon_stream_info ( void )
{
    return (&MY_DATA[0]);
}

/***************************************************************************
 *                                                                         *
 *                       Module Test Head                                  *
 *                                                                         *
 ***************************************************************************/

/** Teat Head Entry */
int daemon_run_testhead ( void )
{
    int rc    = PASS;
    return (rc);
}
