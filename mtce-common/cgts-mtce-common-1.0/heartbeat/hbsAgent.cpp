/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Nodal Health Check Agent Daemon
  */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>           /* for ... hostent */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>           /* for ... close and usleep   */
#include <sys/stat.h>
#include <linux/rtnetlink.h>  /* for ... RTMGRP_LINK         */

using namespace std;

#include "nodeBase.h"
#include "nodeUtil.h"      /* for ... get_ip_addresses                   */
#include "nodeMacro.h"     /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET */
#include "daemon_ini.h"    /* Ini Parser Header                          */
#include "daemon_common.h" /* Common definitions and types for daemons   */
#include "daemon_option.h" /* Common options  for daemons                */
#include "nodeClass.h"     /* The main link class                        */
#include "nodeTimers.h"    /* maintenance timer utilities start/stop     */
#include "nlEvent.h"       /* for ... open_netlink_socket                */
#include "hbsBase.h"       /* Heartbeat Base Header File                 */
#include "hbsAlarm.h"      /* for ... hbsAlarm_clear_all                 */
#include "alarm.h"         /* for ... alarm send message to mtcalarmd    */

/**************************************************************
 *            Implementation Structure
 **************************************************************
 *
 * Call sequence:
 *
 *    daemon_init
 *       daemon_configure
 *       daemon_signal_init
 *       hbs_hostname_read
 *       hbs_message_init
 *       hbs_int_socket_init
 *       hbs_ext_socket_init
 *       forever ( timer_handler )
 *           hbs_pulse_req
 *           hbs_timer_start
 *           _pulse_receive
 *           hbs_timer_stop
 *
 * Note: Interface implementation is in opposite
 *       order of the following call sequence
 */

/* Number of back to back interface errors before the interface is re-initialized. */
#define INTERFACE_ERRORS_FOR_REINIT (8)

/* Historical String data for mem_logs */
static string unexpected_pulse_list[MAX_IFACES] = { "" , "" } ;
static string arrival_histogram[MAX_IFACES]     = { "" , "" } ;

/** This heartbeat service inventory is tracked by
  * the same nodeLinkClass that maintenance uses.
  *
  */
nodeLinkClass   hbsInv ;
nodeLinkClass * get_hbsInv_ptr ( void )
{
    return (&hbsInv);
}

/** Setup the pointer */
int module_init ( void )
{
   return (PASS);
}

static unsigned int    my_nodetype= CGTS_NODE_NULL ;

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

msgSock_type * get_mtclogd_sockPtr ( void )
{
    return (&hbs_sock.mtclogd);
}

#define SCHED_MONITOR__MAIN_LOOP ((const char *) "---> scheduling latency : main loop :")
#define SCHED_MONITOR__RECEIVER  ((const char *) "---> scheduling latency : rx pulses :")
void monitor_scheduling ( unsigned long long & this_time, unsigned long long & prev_time , int data, const char * label_ptr )
{
    this_time = gettime_monotonic_nsec () ;
    if ( this_time > (prev_time + (NSEC_TO_MSEC*(hbs_config.latency_thld)))) /* 10 millisec */
    {
        llog ("%4llu.%-4llu msec - %s at line %d\n",
                 ((this_time-prev_time) > NSEC_TO_MSEC) ? ((this_time-prev_time)/NSEC_TO_MSEC) : 0,
                 ((this_time-prev_time) > NSEC_TO_MSEC) ? ((this_time-prev_time)%NSEC_TO_MSEC) : 0,
                 label_ptr, data);
    }
//    else if ( ! strcmp (SCHED_MONITOR__RECEIVER, label_ptr ) && ( data > 10 ))
//    {
//        ilog ("===> receive latency : batch of %d pulses in under scheduling threshold of %d msec\n", data , hbs_config.latency_thld );
//    }
    prev_time = this_time ;
}

/* Cleanup exit handler */
void daemon_exit ( void )
{
    daemon_dump_info  ();
    daemon_files_fini ();

    /* Close the heatbeat sockets */
    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        if ( hbs_sock.tx_sock[i] )
            delete (hbs_sock.tx_sock[i]);
        if ( hbs_sock.rx_sock[i] )
            delete (hbs_sock.rx_sock[i]);
    }

    /* Close the event socket */
    if ( hbs_sock.hbs_event_tx_sock )
       delete (hbs_sock.hbs_event_tx_sock);

    /* Close the command socket */
    if ( hbs_sock.mtc_to_hbs_sock )
       delete (hbs_sock.mtc_to_hbs_sock);

    exit (0);
}

/* Number of pulse response socket receive  */
/* retries that should occur in a heartbeat */
/* period before we declare a node missing  */
/* Note: Value that needs to be engineered  */
/* once we get time on real hardware        */
#define MAX_PULSE_RETRIES  (3)

#define HBS_SOCKET_MSEC    (5)
#define HBS_SOCKET_NSEC    (HBS_SOCKET_MSEC*1000)
#define HBS_MIN_PERIOD     (50)
#define HBS_MAX_PERIOD     (999)
#define HBS_VIRT_PERIOD    (500)
#define HBS_BACKOFF_FACTOR (4) /* period*this during backoff */

/** Control Config Mask */
#define CONFIG_AGENT_MASK   (CONFIG_AGENT_HBS_PERIOD      |\
                             CONFIG_AGENT_HBS_DEGRADE     |\
                             CONFIG_AGENT_HBS_FAILURE     |\
                             CONFIG_AGENT_MULTICAST       |\
                             CONFIG_SCHED_PRIORITY  |\
                             CONFIG_MTC_TO_HBS_CMD_PORT   |\
                             CONFIG_HBS_TO_MTC_EVENT_PORT |\
                             CONFIG_AGENT_HBS_MGMNT_PORT  |\
                             CONFIG_AGENT_HBS_INFRA_PORT  |\
                             CONFIG_CLIENT_HBS_MGMNT_PORT |\
                             CONFIG_CLIENT_MTCALARM_PORT  |\
                             CONFIG_CLIENT_HBS_INFRA_PORT )

/* Startup config read */
static int hbs_config_handler ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "heartbeat_period"))
    {
        config_ptr->hbs_pulse_period = atoi(value);
        hbsInv.hbs_pulse_period = atoi(value);
        hbsInv.hbs_state_change = true ;
        hbsInv.hbs_disabled = false ;
        config_ptr->mask |= CONFIG_AGENT_HBS_PERIOD ;

        /* Adjust the heartbeat period in a virtual environment */
        if (( hbsInv.hbs_pulse_period >= HBS_MIN_PERIOD )  ||
            ( hbsInv.hbs_pulse_period <= HBS_MAX_PERIOD ))
        {
            struct stat p ;
            p.st_size = 0 ;
            stat ( HOST_IS_VIRTUAL, &p ) ;
            if ( p.st_size )
            {
                if (( hbsInv.hbs_pulse_period != 0 ) &&
                    ( hbsInv.hbs_pulse_period < HBS_VIRT_PERIOD ))
                {
                    config_ptr->hbs_pulse_period = HBS_VIRT_PERIOD ;
                    hbsInv.hbs_pulse_period      = HBS_VIRT_PERIOD ;
                    hbsInv.hbs_pulse_period_save = HBS_VIRT_PERIOD ;
                }
            }
        }
    }

    hbsInv.hbs_pulse_period_save = hbsInv.hbs_pulse_period ;

    if (MATCH("agent", "hbs_minor_threshold"))
    {
        config_ptr->hbs_minor_threshold =
        hbsInv.hbs_minor_threshold = atoi(value);
    }
    if (MATCH("agent", "heartbeat_degrade_threshold"))
    {
        config_ptr->hbs_degrade_threshold =
        hbsInv.hbs_degrade_threshold = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_HBS_DEGRADE ;
    }
    if (MATCH("agent", "heartbeat_failure_threshold"))
    {
        config_ptr->hbs_failure_threshold =
        hbsInv.hbs_failure_threshold = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_HBS_FAILURE ;
    }
    if (MATCH("agent", "hbs_calibrate_threshold"))
    {
        config_ptr->hbs_calibrate_threshold = atoi(value);
    }
    if (MATCH("agent", "hbs_calibrate_period_factor"))
    {
        config_ptr->hbs_calibrate_period_factor = atoi(value);
    }
    if (MATCH("agent", "hbs_calibrate_minor_factor"))
    {
        config_ptr->hbs_calibrate_minor_factor = atoi(value);
    }
    if (MATCH("agent", "hbs_calibrate_degrade_factor"))
    {
        config_ptr->hbs_calibrate_degrade_factor = atoi(value);
    }
    if (MATCH("agent", "hbs_calibrate_fail_factor"))
    {
        config_ptr->hbs_calibrate_fail_factor = atoi(value);
    }
    if (MATCH("agent", "multicast"))
    {
        config_ptr->multicast = strdup(value);
        config_ptr->mask |= CONFIG_AGENT_MULTICAST ;
    }
    else if (MATCH("agent", "mtc_to_hbs_cmd_port"))
    {
        config_ptr->mtc_to_hbs_cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_MTC_TO_HBS_CMD_PORT ;
    }
    else if (MATCH("agent", "hbs_to_mtc_event_port"))
    {
        config_ptr->hbs_to_mtc_event_port = atoi(value);
        config_ptr->mask |= CONFIG_HBS_TO_MTC_EVENT_PORT ;
    }
    else if (MATCH("agent", "scheduling_priority"))
    {
        int max = sched_get_priority_max(SCHED_RR);
        int min = sched_get_priority_min(SCHED_RR);

        config_ptr->scheduling_priority = atoi(value);
        config_ptr->mask |= CONFIG_SCHED_PRIORITY ;

        if (( config_ptr->scheduling_priority < min) ||
            ( config_ptr->scheduling_priority > max))
        {
            wlog ("Invalid scheduling priority (%d), overriding to min of %d\n",
                   config_ptr->scheduling_priority, min );
            wlog ("Specified value of %d is out of acceptable range (%d-%d)\n",
                   config_ptr->scheduling_priority, min, max );
            config_ptr->scheduling_priority = min ;
        }
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
    else if ( MATCH("client", "mtcalarm_req_port") )
    {
        config_ptr->mtcalarm_req_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_MTCALARM_PORT ;
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
}


/* Read the mtc.ini settings into the daemon configuration */
int daemon_configure ( void )
{
    bool waiting_msg = false ;

    /* Read the ini */
    hbs_config.mask = 0 ;
    if (ini_parse(MTCE_CONF_FILE, hbs_config_handler, &hbs_config) < 0)
    {
        elog("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(MTCE_INI_FILE, hbs_config_handler, &hbs_config) < 0)
    {
        elog("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    get_debug_options ( MTCE_CONF_FILE, &hbs_config );

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( hbs_config.mask != CONFIG_AGENT_MASK )
    {
        elog ("Error: Agent configuration failed (%x)\n",
             ((-1 ^ hbs_config.mask) & CONFIG_AGENT_MASK));
        return (FAIL_INI_CONFIG);
    }

    if ( hbsInv.hbs_minor_threshold > hbsInv.hbs_degrade_threshold )
    {
        hbsInv.hbs_minor_threshold = hbsInv.hbs_degrade_threshold ;
    }

    // hbsInv.recalibrate_thresholds ();

    /* Log the startup settings */
    ilog("Realtime Pri: RR/%i \n", hbs_config.scheduling_priority );
    ilog("Pulse Period: %i msec\n",   hbsInv.hbs_pulse_period );
    ilog("Minor   Thld: %i misses\n", hbsInv.hbs_minor_threshold );
    ilog("Degrade Thld: %i misses\n", hbsInv.hbs_degrade_threshold );
    ilog("Failure Thld: %i misses\n", hbsInv.hbs_failure_threshold );
    ilog("Multicast: %s\n", hbs_config.multicast );

    hbs_config.mgmnt_iface = daemon_get_iface_master ( hbs_config.mgmnt_iface );
    ilog("Mgmnt iface : %s\n", hbs_config.mgmnt_iface );
    ilog("Mgmnt RxPort: %d\n", hbs_config.hbs_agent_mgmnt_port );
    ilog("Mgmnt TxPort: %d\n", hbs_config.hbs_client_mgmnt_port );

    /* Fetch the infrastructure interface name.
     * calls daemon_get_iface_master inside so the
     * aggrigated name is returned if it exists */
    get_infra_iface (&hbs_config.infra_iface );
    if ( strlen(hbs_config.infra_iface) )
    {
        hbsInv.infra_network_provisioned = true ;
        ilog ("Infra iface : %s\n", hbs_config.infra_iface );
    }

    ilog("Infra RxPort: %d\n", hbs_config.hbs_agent_infra_port );
    ilog("Infra TxPort: %d\n", hbs_config.hbs_client_infra_port );

    ilog("Command Port: %d (rx)\n", hbs_config.mtc_to_hbs_cmd_port );
    ilog("Event Port  : %d (tx)\n", hbs_config.hbs_to_mtc_event_port );
    ilog("Alarm Port  : %d (tx)\n", hbs_config.mtcalarm_req_port );

    hbsInv.hbs_state_change = true ;

    /* pull in the degrade only config option */
    hbsInv.infra_degrade_only = hbs_config.infra_degrade_only ;

    if ( hbsInv.hbs_degrade_threshold >= hbsInv.hbs_failure_threshold )
    {
        wlog ("Degrade threshold should be larger than Failure threshold\n");
        wlog ("Heartbeat 'degrade' state disabled ; see %s\n", MTCE_CONF_FILE);
    }
    for ( ;; )
    {
        get_ip_addresses ( hbsInv.my_hostname, hbsInv.my_local_ip , hbsInv.my_float_ip );
        if ( hbsInv.my_float_ip.empty() || hbsInv.my_float_ip.empty() )
        {
            if ( waiting_msg == false )
            {
                ilog ("Waiting on ip address config ...\n");
                waiting_msg = true ;
            }
            mtcWait_secs (3);
        }
        else
        {
            break ;
        }
    }

    /* Set Controller Activity State */
    hbs_config.active = daemon_get_run_option ("active") ;
    ilog ("Controller  : %s\n",
        hbs_config.active ? "Active" : "In-Active" );

    /* pust the activity state into inventory (nodeLinkClass) */
    if ( hbs_config.active == true )
        hbsInv.set_activity_state ( true );
    else
        hbsInv.set_activity_state ( false );

    return (PASS);
}

static struct mtc_timer hbsTimer ;

void hbsTimer_handler ( int sig, siginfo_t *si, void *uc)
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
    /* is base mtc timer */
    else if (( *tid_ptr == hbsTimer.tid ) )
    {
        mtcTimer_stop_int_safe ( hbsTimer );
        hbsTimer.ring = true ;
    }
    else
    {
        // wlog ("Unexpected timer - %p", *tid_ptr );
        mtcTimer_stop_tid_int_safe ( tid_ptr );
    }
}

/****************************/
/* Initialization Utilities */
/****************************/

/* Initialize the multicast pulse request message */
/* One time thing ; tx same message all the time. */
int hbs_message_init ( void )
{
    /* Build the transmit pulse response message for each interface */
    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        memset ( &hbs_sock.tx_mesg[i], 0, sizeof(hbs_message_type));
        memcpy ( &hbs_sock.tx_mesg[i].m[0], &req_msg_header[0], HBS_HEADER_SIZE );
    }
    return (PASS);
}

/* initialize pulse messaging for the specified interface */
int _setup_pulse_messaging ( iface_enum i, int rmem_max )
{
    int rc  = PASS ;
    char * iface = NULL ;

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

    /* Start by closing existing sockets just in case this is a (re)initialization */
    if ( hbs_sock.rx_sock[i] )
    {
        delete (hbs_sock.rx_sock[i]);
        hbs_sock.rx_sock[i] = 0 ;
    }

    if ( hbs_sock.tx_sock[i] )
    {
        delete (hbs_sock.tx_sock[i]);
        hbs_sock.tx_sock[i] = 0 ;
    }

    /* Create transmit socket */
    hbs_sock.tx_sock[i] = new msgClassTx(hbs_config.multicast,hbs_sock.tx_port[i],IPPROTO_UDP,iface);
    if ( hbs_sock.tx_sock[i] )
    {
        if ( hbs_sock.tx_sock[i]->return_status != PASS )
        {
            elog("Cannot open multicast transmit socket - rc:%d (%d:%m)\n", hbs_sock.tx_sock[i]->return_status, errno );
            delete (hbs_sock.tx_sock[i]);
            hbs_sock.tx_sock[i] = 0 ;
            return (FAIL_SOCKET_CREATE);
        }
    }
    else
    {
        elog("Cannot open multicast transmit socket - null object (%d:%m)\n", errno );
        return (FAIL_SOCKET_CREATE);
    }
    dlog("Opened multicast transmit socket\n" );

    /* In order to avoid multicast packets being routed wrong, force sending from that socket */
    hbs_sock.tx_sock[i]->interfaceBind();

    /* set this tx socket interface with priority class messaging */
    hbs_sock.tx_sock[i]->setPriortyMessaging( iface );

    /***********************************************************/
    /* Setup the Pulse response receive socket                 */
    /***********************************************************/
    hbs_sock.rx_sock[i] = new msgClassRx(hbs_config.multicast,hbs_sock.rx_port[i],IPPROTO_UDP,iface,true);
    if (( hbs_sock.rx_sock[i] == NULL ) || (hbs_sock.rx_sock[i]->return_status != PASS ))
    {
        elog("Failed opening pulse receive socket (%d:%s)\n",
              errno, strerror (errno));
        rc = FAIL_SOCKET_CREATE ;
    }
    else
    {
        /* set rx socket buffer size ro rmem_max */
        if (rmem_max != 0 )
            hbs_sock.rx_sock[i]->setSocketMemory ( iface, "rx pulse socket memory", rmem_max );
        else
            wlog ("failed to query rmem_max ; using rmem_default\n");
    }

    /* handle failure path */
    if ( rc != PASS )
    {
        if ( hbs_sock.rx_sock[i] )
        {
            delete (hbs_sock.rx_sock[i]);
            hbs_sock.rx_sock[i] = 0 ;
        }
        if ( hbs_sock.tx_sock[i] )
        {
            delete (hbs_sock.tx_sock[i]);
            hbs_sock.tx_sock[i] = 0 ;
        }
        return (rc);
    }

    return (rc);
}

/* Setup the Unix Domain Transmit Pulse Socket */
int alarm_port_init ( void )
{
    hbs_sock.alarm_port = daemon_get_cfg_ptr()->mtcalarm_req_port;
    hbs_sock.alarm_sock = new msgClassTx(LOOPBACK_IP, hbs_sock.alarm_port, IPPROTO_UDP);
    if ( hbs_sock.alarm_sock )
    {
        if ( hbs_sock.alarm_sock->return_status == PASS )
        {
            hbs_sock.alarm_sock->sock_ok(true);
            alarm_register_user ( hbs_sock.alarm_sock );
        }
        else
        {
            elog ("alarm_port_init failed socket setup (rc:%d)\n",
                   hbs_sock.alarm_sock->return_status );
        }
    }
    return ( hbs_sock.alarm_sock->return_status ) ;
}

/* Init the internal/local sockets ; the ones that will no change.
 * This way we don't miss add and start commands from maintenance. */

int hbs_int_socket_init ( void )
{
    int rc = PASS ;

    ilog ("internal sockets init ...\n");

    /******************************************************************/
    /* UDP Tx Message Socket for Heartbeat Events Towards Maintenance */
    /******************************************************************/

    int port = hbs_config.hbs_to_mtc_event_port ;
    hbs_sock.hbs_event_tx_sock = new msgClassTx(LOOPBACK_IP, port, IPPROTO_UDP);
    if (hbs_sock.hbs_event_tx_sock->return_status != PASS)
    {
        elog ("Failed to setup hbs event transmit port %d\n", port );
        return (hbs_sock.hbs_event_tx_sock->return_status) ;
    }

    /***************************************************************/
    /* Non-Blocking UDP Rx Message Socket for Maintenance Commands */
    /***************************************************************/

    port = hbs_config.mtc_to_hbs_cmd_port ;
    hbs_sock.mtc_to_hbs_sock = new msgClassRx(LOOPBACK_IP, port, IPPROTO_UDP);
    if (hbs_sock.mtc_to_hbs_sock->return_status != PASS)
    {
        elog ("Failed to setup mtce command receive port %d\n", port );
        return (hbs_sock.mtc_to_hbs_sock->return_status) ;
    }

    if ( ( rc = alarm_port_init ()) != PASS )
    {
        elog ("Alarm port setup or registration failed (rc:%d)\n", rc );
    }
    return (rc);
}

/* Construct the messaging sockets *
 * 1. multicast transmit socket    *
 * 2. unicast receive socket       */
int hbs_ext_socket_init ( void )
{
    int rc = PASS ;

    ilog ("external sockets init ...\n");

    /* set rx socket buffer size ro rmem_max */
    int rmem_max = daemon_get_rmem_max () ;

    /* Read the port config strings into the socket struct
     *
     * These ports are swapped compared to the hbsClient
     *
     * From the agent perspective
     *     rx_port is the hbs_agent_..._port
     *     tx_port is the hbs_client_..._port
     *
     */
    hbs_sock.rx_port[MGMNT_IFACE] = hbs_config.hbs_agent_mgmnt_port ;
    hbs_sock.tx_port[MGMNT_IFACE] = hbs_config.hbs_client_mgmnt_port;
    hbs_sock.rx_port[INFRA_IFACE] = hbs_config.hbs_agent_infra_port ;
    hbs_sock.tx_port[INFRA_IFACE] = hbs_config.hbs_client_infra_port;

    /* Setup the pulse messaging interfaces */
    SETUP_PULSE_MESSAGING ( hbsInv.infra_network_provisioned, rmem_max ) ;

    return (rc) ;
}


/* Send a multicast heartbeat pulse request message on a */
/* specific port to all listening nodes on the network.  */
int hbs_pulse_request ( iface_enum iface,
                        unsigned int seq_num,
                        string       hostname_clue,
                        unsigned int lookup_clue)
{
    int rc = PASS ;
    #define MAX_LEN 1000
    char str[MAX_LEN] ;

    /* Add the sequence number */
    hbs_sock.tx_mesg[iface].s = seq_num ;
    memset ( &hbs_sock.tx_mesg[iface].m[HBS_HEADER_SIZE], 0, MAX_CHARS_HOSTNAME );
    if (( lookup_clue ) &&
        ( hostname_clue.length() <= MAX_CHARS_HOSTNAME ))
    {
        hbs_sock.tx_mesg[iface].c = lookup_clue ;
        memcpy ( &hbs_sock.tx_mesg[iface].m[HBS_HEADER_SIZE],
                  hostname_clue.data(),
                  hostname_clue.length());
    }
    /* Message length is the size of the sequence number, the clue and the buffer */

    int msg_len = (HBS_MAX_MSG+(sizeof(unsigned int)*2)) ;
    if ( hbs_sock.tx_sock[iface] )
    {
#ifdef WANT_FIT_TESTING
        if ( daemon_want_fit ( FIT_CODE__NO_PULSE_REQUEST, "any" , get_iface_name_str(iface) ) )
        {
            goto hbs_pulse_request_out ;
        }
        else if ( daemon_want_fit ( FIT_CODE__NO_PULSE_REQUEST, "any" , "any" ) )
        {
            goto hbs_pulse_request_out ;
        }
#endif
        if ( (rc = hbs_sock.tx_sock[iface]->write((char*)&hbs_sock.tx_mesg[iface], msg_len)) < 0 )
        {
            elog("Failed to send Pulse request: %d:%s to %s.%d (rc:%i ; %d:%s)\n",
                         hbs_sock.tx_mesg[iface].s,
                         &hbs_sock.tx_mesg[iface].m[0],
                         hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
                         hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
                         rc, errno, strerror(errno) );
            return (FAIL_SOCKET_SENDTO);
        }
    }
    else
    {
        wlog("Unable to send pulse request - null tx object - auto re-init pending\n");
        return (FAIL_SOCKET_SENDTO);
    }

#ifdef WANT_FIT_TESTING
hbs_pulse_request_out:
#endif

    mlog1("%s Pulse Req: (%5d): %17s:%5d: %d:%d:%x:%s\n",
              get_iface_name_str(iface), rc,
              hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
              hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
                hbs_sock.tx_mesg[iface].s,
                hbs_sock.tx_mesg[iface].c,
                hbs_sock.tx_mesg[iface].f,
                hbs_sock.tx_mesg[iface].m);


    snprintf ( &str[0], MAX_LEN, "%s Pulse Req: %17s:%5d: %u:%u:%s\n",
            get_iface_name_str(iface),
            hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
            hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
            hbs_sock.tx_mesg[iface].s,
            hbs_sock.tx_mesg[iface].c,
            hbs_sock.tx_mesg[iface].m);
    mem_log (&str[0]);

    return (PASS);
}

string get_hostname_from_pulse ( char * msg_ptr )
{
    char temp [MAX_HOST_NAME_SIZE];
    string hostname ;

    char * str_ptr = strstr ( msg_ptr, ":" );
    memset ( temp, 0 , MAX_HOST_NAME_SIZE );

    sscanf ( ++str_ptr, "%31s", &temp[0] );
    hostname = temp ;
    return (hostname);
}

int _pulse_receive ( iface_enum iface , unsigned int seq_num )
{
    int n = 0 ;

    int detected_pulses = 0 ;

    /* get a starting point */
    unsigned long long  after_rx_time ;
    unsigned long long before_rx_time =  gettime_monotonic_nsec ();

    do
    {
        /* Clean the receive buffer */
        memset ( hbs_sock.rx_mesg[iface].m, 0, HBS_MAX_MSG );
        hbs_sock.rx_mesg[iface].s = 0 ;
        hbs_sock.rx_mesg[iface].c = 0 ;
        if ( hbs_sock.rx_sock[iface] == NULL )
        {
            elog ("%s cannot receive pulses - null object\n", get_iface_name_str(iface) );
            return (0);
        }
        if ( (n = hbs_sock.rx_sock[iface]->read((char*)&hbs_sock.rx_mesg[iface], sizeof(hbs_message_type))) != -1 )
        {
           mlog1 ("%s Pulse Rsp: (%5d): %17s:%5d: %d:%d:%x:%s\n",
                       get_iface_name_str(iface), n,
                       hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                       hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                       hbs_sock.rx_mesg[iface].s,
                       hbs_sock.rx_mesg[iface].c,
                       hbs_sock.rx_mesg[iface].f,
                       hbs_sock.rx_mesg[iface].m);

            /* Validate the header */
            if ( strstr ( hbs_sock.rx_mesg[iface].m, rsp_msg_header) )
            {
                int rc = RETRY ;
                string hostname = get_hostname_from_pulse (&hbs_sock.rx_mesg[iface].m[0]);

#ifdef WANT_FIT_TESTING
                if ( hbs_config.testmode == 1 )
                {
                    if ( daemon_want_fit ( FIT_CODE__NO_PULSE_RESPONSE, hostname, get_iface_name_str(iface) ) )
                    {
                        continue ;
                    }
                    else if ( daemon_want_fit ( FIT_CODE__NO_PULSE_RESPONSE, hostname, "any" ) )
                    {
                        continue ;
                    }
                    else if ( daemon_want_fit ( FIT_CODE__NO_PULSE_RESPONSE, "any", "any" ) )
                    {
                        continue ;
                    }
                }
#endif

                mlog ("%s Pulse Rsp from (%s)\n", get_iface_name_str(iface), hostname.c_str());
                if ( !hostname.compare("localhost") )
                {
                    mlog3 ("%s Pulse Rsp (local): %17s:%5d: %d:%d:%x:%s\n",
                              get_iface_name_str(iface),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                              hbs_sock.rx_mesg[iface].s,
                              hbs_sock.rx_mesg[iface].c,
                              hbs_sock.rx_mesg[iface].f,
                              hbs_sock.rx_mesg[iface].m);
                }
                else if ( !hostname.compare(hbsInv.my_hostname))
                {
                    mlog3 ("%s Pulse Rsp: (self ): %17s:%5d: %d:%d:%x:%s\n",
                              get_iface_name_str(iface),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                              hbs_sock.rx_mesg[iface].s,
                              hbs_sock.rx_mesg[iface].c,
                              hbs_sock.rx_mesg[iface].f,
                              hbs_sock.rx_mesg[iface].m);

                    hbsInv.manage_pulse_flags ( hostname, hbs_sock.rx_mesg[iface].f );
                }
                else
                {
                    if ( hbsInv.monitored_pulse ( hostname , iface ) == true )
                    {
                        #define MAX_LEN 1000
                        char str[MAX_LEN] ;
                        string extra = "Rsp" ;

                        if ( seq_num != hbs_sock.rx_mesg[iface].s )
                        {
                            extra = "SEQ" ;
                        }
                        else
                        {
                            rc = hbsInv.remove_pulse ( hostname, iface, hbs_sock.rx_mesg[iface].c, hbs_sock.rx_mesg[iface].f ) ;
                        }
                        snprintf  (&str[0], MAX_LEN, "%s Pulse %s: (%5d): %17s:%5d: %u:%u:%x:%s\n",
                                    get_iface_name_str(iface), extra.c_str(), n,
                                    hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                                    hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                                    hbs_sock.rx_mesg[iface].s,
                                    hbs_sock.rx_mesg[iface].c,
                                    hbs_sock.rx_mesg[iface].f,
                                    hbs_sock.rx_mesg[iface].m);
                        mlog1 ("%s", &str[0]);
                        mem_log (str);
                        if ( extra.empty())
                        {
                            detected_pulses++ ;
                        }
                    }
                    else
                    {
                        mlog3 ("%s Pulse Dis: (%5d): %17s:%5d: %d:%d:%x:%s\n",
                                  get_iface_name_str(iface), n,
                                  hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                                  hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                                  hbs_sock.rx_mesg[iface].s,
                                  hbs_sock.rx_mesg[iface].c,
                                  hbs_sock.rx_mesg[iface].f,
                                  hbs_sock.rx_mesg[iface].m);
                    }

                }

                if ( rc == ENXIO )
                {
                    mlog3 ("Unexpected %s Pulse: <%s>\n", get_iface_name_str(iface),
                                                          &hbs_sock.rx_mesg[iface].m[0] );
                    unexpected_pulse_list[iface].append ( hostname.c_str());
                    unexpected_pulse_list[iface].append ( " " );
                }
                /* Empty list rc - do nothing */
                else if ( rc == -ENODEV )
                {
                    /* This error occurs when the active controller is the only enabled host */
                    mlog3 ("Remove Pulse Failed due to empty pulse list\n");
                }
            }
            else
            {
                 wlog ( "Badly formed message\n" );
                 mlog  ( "Bad %s Msg: %14s:%5d: %d:%s\n",
                                get_iface_name_str(iface),
                                hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                                hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                                  hbs_sock.rx_mesg[iface].s,
                                  hbs_sock.rx_mesg[iface].m) ;
            }
        }
    } while ( n > 0 ) ;
    monitor_scheduling ( after_rx_time, before_rx_time, detected_pulses, SCHED_MONITOR__RECEIVER );
    return (detected_pulses);
}

int send_event ( string & hostname, unsigned int event_cmd, iface_enum iface )
{
    int bytes ;
    int bytes_to_send ;
    int rc = PASS ;
    int retries = 0 ;

    mtc_message_type event ;
    memset (&event, 0 , sizeof(mtc_message_type));
    if ( event_cmd == MTC_EVENT_HEARTBEAT_LOSS )
    {
        daemon_dump_membuf_banner ();
        daemon_dump_membuf ();
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_loss_header());
    }
    else if ( event_cmd == MTC_EVENT_LOOPBACK )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_event_header());
    }
    else if ( event_cmd == MTC_EVENT_HEARTBEAT_MINOR_SET )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_event_header());
    }
    else if ( event_cmd == MTC_EVENT_HEARTBEAT_MINOR_CLR )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_event_header());
    }
    else if ( event_cmd == MTC_EVENT_HEARTBEAT_DEGRADE_SET )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_event_header());
    }
    else if ( event_cmd == MTC_EVENT_HEARTBEAT_DEGRADE_CLR )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_event_header());
    }
    else if ( event_cmd == MTC_EVENT_HEARTBEAT_READY )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_ready_header());
    }
    else if (( event_cmd == MTC_EVENT_PMOND_CLEAR ) ||
             ( event_cmd == MTC_EVENT_PMOND_RAISE ) ||
             ( event_cmd == MTC_EVENT_HOST_STALLED ))
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_mtce_event_header());
    }
    else
    {
        elog ("Unsupported heartbeat event (%d)\n", event_cmd );
        return ( FAIL_BAD_CASE );
    }

    /* Put the hostname in the buffer - as well */
    snprintf ( &event.buf[0] , MAX_CHARS_HOSTNAME, "%s", hostname.data());

    /* TODO: obsolete this method in the future as it limits the host name lenth to 32 */
    snprintf ( &event.hdr[MSG_HEADER_SIZE] , MAX_CHARS_HOSTNAME, "%s", hostname.data());

    event.cmd = event_cmd ;
    event.num = 1 ;
    event.parm[0] = iface ;

    print_mtc_message ( LOCALHOST, MTC_CMD_TX, event, LOCALHOST , false );

    /* remove the buffer as it is not needed for this message */
    bytes_to_send = ((sizeof(mtc_message_type))-(BUF_SIZE-hostname.length())) ;
    do
    {
        bytes = hbs_sock.hbs_event_tx_sock->write((char*)&event,bytes_to_send);
        if ( bytes <= 0 )
        {
            rc = FAIL_TO_TRANSMIT ;

            if ( retries++ > 3 )
            {
                elog ("Cannot communicate with maintenance\n");
                return (RETRY);
            }
        }
        else
            rc = PASS ;
    } while ( bytes <= 0 ) ;

    return rc ;
}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodetype )
{
    int rc = 10 ;

    /* Not used by this service */
    UNUSED(nodetype);
    /* Initialize socket construct and pointer to it */
    memset ( &hbs_sock,   0, sizeof(hbs_sock));

    /* initialize the timer */
    mtcTimer_init ( hbsTimer, "controller", "heartbeat" );

    /* Assign interface to config */
    hbs_config.mgmnt_iface = (char*)iface.data() ;

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return FAIL_FILES_INIT ;
    }

    hbsInv.system_type = daemon_system_type ();

    /* convert node type to integer */
    my_nodetype = get_host_function_mask ( nodetype ) ;
    ilog ("Node Type   : %s (%d)\n", nodetype.c_str(), my_nodetype );

    /* Bind signal handlers */
    if ( daemon_signal_init () != PASS )
    {
        elog ("daemon_signal_init failed\n");
        rc = FAIL_SIGNAL_INIT ;
    }

    /* Configure the agent */
    else if ( (rc = daemon_configure ( )) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%i)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }

    /* Init the heartbeat request message */
    else if ( hbs_message_init ( ) != PASS )
    {
        elog ("Failed to initialize pulse request message\n");
        rc = FAIL_MESSAGE_INIT;
    }

    /* Setup the heartbeat service messaging sockets */
    else if ((rc = hbs_int_socket_init ( )) != PASS )
    {
        elog ("internal socket initialization failed (rc:%d)\n", rc );
        return ( FAIL_SOCKET_INIT ) ;
    }

    daemon_init_fit();
    return (rc);
}

void daemon_service_run ( void )
{
    int exp_pulses[MAX_IFACES] ;
    int rc = PASS ;
    int counter = 0 ;
    int  goenabled_wait_log_throttle = 0 ;
    bool goenabled = false ;

    /* A variable that throttles external socket init failure retries and
     * ultimately triggers an exit if that retry count gets too big */
    int ext_socket_init_fail_count = 0 ;

    /* get a starting point */
    unsigned long long prev_time =  gettime_monotonic_nsec ();
    unsigned long long this_time =  prev_time ;

    /* Used to throttle warning messages that report
     * an error transmitting the pulse request */
    int pulse_request_fail_log_counter[MAX_IFACES] ;

    bool heartbeat_request = true ;
    unsigned int seq_num = 0 ;

    /* socket descriptor list */
    std::list<int> socks ;

    hbsInv.hbs_state_change = true ;
    hbsInv.hbs_disabled = false ;

    /* Set the mode */
    hbsInv.maintenance = false ;
    hbsInv.heartbeat   = true  ;

    /* Load the expected pulses and zero detected */
    for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
    {
        pulse_request_fail_log_counter[iface] = 0 ;
        hbsInv.pulse_requests[iface] = 0 ;
    }

    /* Make the main loop schedule in real-time */
    struct sched_param param ;
    memset ( &param, 0, sizeof(struct sched_param));
    param.sched_priority = hbs_config.scheduling_priority ;
    if ( sched_setscheduler(0, SCHED_RR, &param) )
    {
        elog ("sched_setscheduler (0, SCHED_RR, %d ) returned error (%d:%s)\n",
               param.sched_priority, errno, strerror(errno));
    }

    /* Not monitoring address changes RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR */
    if (( hbs_sock.ioctl_sock = open_ioctl_socket ( )) <= 0 )
    {
        elog ("Failed to create ioctl socket");
        daemon_exit ();
    }

    if (( hbs_sock.netlink_sock = open_netlink_socket ( RTMGRP_LINK )) <= 0 )
    {
        elog ("Failed to create netlink listener socket");
        daemon_exit ();
    }


    /* CGTS 4114: Small Footprint: Alarm 200.005 remains active after connectivity restored
     *
     * Clear self alarms */
    hbsAlarm_clear_all ( hbsInv.my_hostname );

    /* add this host as inventory to hbsAgent
     * Although this host is not monitored for heartbeat,
     * there are OOB flags in the heartbneat message that
     * are needed to be extracted and locally updated */
    {
        /* Scoping this so that the inv variable is freed after the add.
         * No need sarying it around on the stack all the time */
        node_inv_type inv ;

        /* init the inv variable */
        node_inv_init ( inv );
        inv.name = hbsInv.my_hostname ;
        inv.nodetype = CONTROLLER_TYPE ;
        hbsInv.add_heartbeat_host ( inv );
    }
    ilog ("Sending ready event to maintenance\n");
    do
    {
        /* Wait for maintenance */
        rc = send_event ( hbsInv.my_hostname, MTC_EVENT_HEARTBEAT_READY, MGMNT_IFACE ) ;
        if ( rc == RETRY )
        {
            mtcWait_secs ( 3 );
        }
    } while ( rc == RETRY ) ;

    if ( rc == FAIL )
    {
       elog ("Unrecoverable heartbeat startup error (rc=%d)\n", rc );
       daemon_exit ();
    }

    /* enable the base level signal handler latency monitor */
    daemon_latency_monitor (true);

    /* Run heartbeat service forever or until stop condition */
    for ( hbsTimer.ring = false ; ; )
    {
        daemon_signal_hdlr ();

        /*******************************************************************
         *
         * This handles hbsAgent external socket initialization in the main
         * loop only after the goenabled state is reached.
         *
         *******************************************************************/
        if ( goenabled == false )
        {
            if ( hbsInv.system_type == SYSTEM_TYPE__NORMAL )
            {
                if ( daemon_is_file_present ( GOENABLED_MAIN_PASS ) == true )
                {
                    ilog ("GOENABLE (large system)\n");
                    goenabled = true ;
                }
            }
            else
            {
                if ( daemon_is_file_present ( GOENABLED_SUBF_PASS ) == true )
                {
                    ilog ("GOENABLE (small system)\n");
                    goenabled = true ;
                }
            }

            if ( goenabled == false )
            {
                ilog_throttled ( goenabled_wait_log_throttle, 2000, "GOENABLE wait ...\n");
                usleep (50000); /* 50 msec */
            }

            if ( goenabled == true )
            {
                /* Setup the heartbeat service messaging sockets */
                if ( (rc = hbs_ext_socket_init  ( )) != PASS )
                {
                    goenabled = false ;
                    if ( ext_socket_init_fail_count++ == 30 )
                    {
                        elog ("external socket initialization failed (rc:%d) max retries ; exiting ...\n", rc );
                        daemon_exit ();
                    }
                    else
                    {
                        elog ("external socket initialization failed (rc:%d)\n", rc );
                    }
                }
                else
                {
                    ext_socket_init_fail_count = 0 ;
                    goenabled_wait_log_throttle = 0 ;

                    if ( get_link_state ( hbs_sock.ioctl_sock, hbs_config.mgmnt_iface, &hbsInv.mgmnt_link_up_and_running ) )
                    {
                         hbsInv.mgmnt_link_up_and_running = false ;
                         wlog ("Failed to query %s operational state ; defaulting to down\n", hbs_config.mgmnt_iface );
                    }
                    else
                    {
                        ilog ("Mgmnt %s link is %s\n", hbs_config.mgmnt_iface, hbsInv.mgmnt_link_up_and_running ? "Up" : "Down" );
                    }

                    if ( hbsInv.infra_network_provisioned == true )
                    {
                        if ( get_link_state ( hbs_sock.ioctl_sock, hbs_config.infra_iface, &hbsInv.infra_link_up_and_running ) )
                        {
                            hbsInv.infra_link_up_and_running = false ;
                            wlog ("Failed to query %s operational state ; defaulting to down\n", hbs_config.infra_iface );
                        }
                        else
                        {
                            ilog ("Infra %s link is %s\n", hbs_config.infra_iface, hbsInv.infra_link_up_and_running ? "Up" : "Down" );
                        }
                    }
                }
            }
        }

        /***************** Service Sockets ********************/

        /* Initialize the master fd_set and clear socket list */
        FD_ZERO(&hbs_sock.readfds);
        socks.clear();

        /* Add the mtc command receiver to the select list */
        if (( hbs_sock.mtc_to_hbs_sock  ) &&
            ( hbs_sock.mtc_to_hbs_sock->getFD()))
        {
            socks.push_front (hbs_sock.mtc_to_hbs_sock->getFD());
            FD_SET(hbs_sock.mtc_to_hbs_sock->getFD(), &hbs_sock.readfds);
        }

        /* Add the netlink event listener to the select list */
        if ( hbs_sock.netlink_sock )
        {
            socks.push_back (hbs_sock.netlink_sock);
            FD_SET(hbs_sock.netlink_sock, &hbs_sock.readfds);
        }

        /* Add the management interface to the select list */
        if (( goenabled == true ) &&
            ( hbs_sock.rx_sock[MGMNT_INTERFACE] ) &&
            ( hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD()))
        {
            socks.push_back  (hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD());
            FD_SET(hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD(), &hbs_sock.readfds );
        }

        /* Add the INFRA network pulse rx socket if its provisioned and have a valid socket */
        if (( goenabled == true ) &&
            ( hbsInv.infra_network_provisioned == true ) &&
            ( hbs_sock.rx_sock[INFRA_INTERFACE] ) &&
            ( hbs_sock.rx_sock[INFRA_INTERFACE]->getFD()))
        {
            socks.push_back  (hbs_sock.rx_sock[INFRA_INTERFACE]->getFD());
            FD_SET(hbs_sock.rx_sock[INFRA_INTERFACE]->getFD(), &hbs_sock.readfds );
        }

        monitor_scheduling ( this_time, prev_time, seq_num, SCHED_MONITOR__MAIN_LOOP );

        /* Sort and select() at HBS_SOCKET_NSEC timeout */
        hbs_sock.waitd.tv_sec = 0;
        hbs_sock.waitd.tv_usec = HBS_SOCKET_NSEC;
        socks.sort();

        rc = select( socks.back()+1, &hbs_sock.readfds, NULL, NULL, &hbs_sock.waitd);

        /* If the select time out expired then  */
        if (( rc < 0 ) || ( rc == 0 ))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                elog ("rx_socket select() failed (rc:%d) %s\n",
                    errno, strerror(errno));
            }
        }
        else
        {
            if (( goenabled == true ) &&
                ( hbs_sock.rx_sock[MGMNT_INTERFACE] ) &&
                ( FD_ISSET(hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD(), &hbs_sock.readfds)))
            {
                hbs_sock.fired[MGMNT_INTERFACE] = true ;
            }

            if (( goenabled == true ) &&
                ( hbsInv.infra_network_provisioned == true ) &&
                ( hbs_sock.rx_sock[INFRA_INTERFACE] ) &&
                (  hbs_sock.rx_sock[INFRA_INTERFACE]->getFD()) &&
                ( FD_ISSET(hbs_sock.rx_sock[INFRA_INTERFACE]->getFD(), &hbs_sock.readfds)))
            {
                hbs_sock.fired[INFRA_INTERFACE] = true ;
            }

            if ((hbs_sock.mtc_to_hbs_sock != NULL ) &&
                ( FD_ISSET(hbs_sock.mtc_to_hbs_sock->getFD(), &hbs_sock.readfds)))
            {
                int bytes ;
                mtc_message_type msg ;

                /* Look for maintenance command messages */
                memset (&msg, 0, sizeof(mtc_message_type));
                bytes = hbs_sock.mtc_to_hbs_sock->read((char*)&msg,sizeof(mtc_message_type));
                if ( bytes > 0 )
                {
                    mlog ("Received Maintenance Command (%i)\n", bytes );
                    mlog ("%s - cmd:0x%x\n", &msg.hdr[0], msg.cmd );

                    if ( !strncmp ( get_hbs_cmd_req_header(), &msg.hdr[0], MSG_HEADER_SIZE ))
                    {
                        string hostname = &msg.hdr[MSG_HEADER_SIZE] ;
                        if ( msg.cmd == MTC_CMD_ADD_HOST )
                        {
                            node_inv_type inv ;
                            node_inv_init(inv);
                            inv.name = hostname ;
                            inv.nodetype = msg.parm[0];
                            hbsInv.add_heartbeat_host ( inv ) ;
                            ilog ("%s added to heartbeat service (%d)\n", hostname.c_str(), inv.nodetype );

                            /* clear any outstanding alarms on the ADD */
                            hbsAlarm_clear_all ( hostname );
                        }
                        else if ( msg.cmd == MTC_CMD_DEL_HOST )
                        {
                            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
                            {
                                hbsInv.mon_host ( hostname, (iface_enum)iface, false, false );
                            }

                            hbsInv.del_host ( hostname );
                            ilog ("%s deleted from heartbeat service\n", hostname.c_str());

                            /* clear any outstanding alarms on the DEL */
                            hbsAlarm_clear_all ( hostname );
                        }
                        else if ( msg.cmd == MTC_CMD_STOP_HOST )
                        {
                            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
                            {
                                hbsInv.mon_host ( hostname, (iface_enum)iface, false, true );
                            }
                            ilog ("%s stopping heartbeat service\n", hostname.c_str());
                        }
                        else if ( msg.cmd == MTC_CMD_START_HOST )
                        {
                            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
                            {
                                hbsInv.mon_host ( hostname, (iface_enum)iface, true, true );
                            }
                            ilog ("%s starting heartbeat service\n", hostname.c_str());
                        }
                        else if ( msg.cmd == MTC_RESTART_HBS )
                        {
                            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
                            {
                                hbsInv.mon_host ( hostname, (iface_enum)iface, false, false );
                                hbsInv.mon_host ( hostname, (iface_enum)iface, true, false  );
                            }
                            ilog ("%s restarting heartbeat service\n", hostname.c_str());
                            hbsInv.print_node_info();
                        }
                        else if ( msg.cmd == MTC_RECOVER_HBS )
                        {
                            hbsInv.hbs_pulse_period = hbsInv.hbs_pulse_period_save ;
                            ilog ("%s starting heartbeat recovery (period:%d msec)\n", hostname.c_str(), hbsInv.hbs_pulse_period);
                            hbsInv.print_node_info();
                        }
                        else if ( msg.cmd == MTC_BACKOFF_HBS )
                        {
                            hbsInv.hbs_pulse_period = (hbsInv.hbs_pulse_period_save * HBS_BACKOFF_FACTOR) ;
                            ilog ("%s starting heartbeat backoff (period:%d msecs)\n", hostname.c_str(), hbsInv.hbs_pulse_period );
                            hbsInv.print_node_info();
                        }
                        else
                        {
                            wlog ("Unsupport maintenance command\n");
                        }
                    }
                    else
                    {
                        elog ("Unexpected maintenance message header\n");
                    }
                }
                else
                {
                    elog ("Failed receive from agent domain socket (%i)\n", bytes );
                }
            }
            if (FD_ISSET( hbs_sock.netlink_sock, &hbs_sock.readfds))
            {
                dlog ("netlink socket fired\n");
                if ( hbsInv.service_netlink_events ( hbs_sock.netlink_sock, hbs_sock.ioctl_sock ) != PASS )
                {
                    elog ("service_netlink_events failed (rc:%d)\n", rc );
                }
            }
        }

        /***************************************************************/
        /**************** Manage Heartbeat Service *********************/
        /***************************************************************/

        /* bypass heartbeat if the period is out of accepted / tested range */
        if ( hbsInv.hbs_pulse_period < HBS_MIN_PERIOD )
        {
            if ( hbsInv.hbs_state_change == true )
            {
                wlog ("Heartbeat Disabled by out-of-range period (%d msec)\n",
                   hbsInv.hbs_pulse_period );
                wlog ("Period must be greater than %d msec, see %s\n",
                   HBS_MIN_PERIOD, MTCE_CONF_FILE );

                hbsInv.hbs_disabled = true ;
                hbsInv.hbs_state_change = false ;

                /* print current node inventory to the stdio */
                hbsInv.print_node_info();

            }
        }

        /* Manage enabling and disabling the heartbeat service based on
         * the state of the management link.
         * link up = run heartbeat service
         * link down = disable heatbeat service and monitor the link up to re-enable
         */
        else if (( hbsInv.mgmnt_link_up_and_running == false ) &&
                 ( hbsInv.hbs_disabled == false ))
        {
            hbsInv.hbs_disabled = true ;
            hbsInv.hbs_state_change = true ;
            ilog ("Heartbeat disabled by %s link down event\n", hbs_config.mgmnt_iface );
            counter = 1 ;
        }

        /* Recover heartbeat when link comes back up */
        else if (( hbsInv.mgmnt_link_up_and_running == true ) &&
                 ( hbsInv.hbs_disabled == true ))
        {
            hbsInv.hbs_disabled = false ;
            hbsInv.hbs_state_change = true ;
            ilog ("Heartbeat Enabled by %s link up event\n", hbs_config.mgmnt_iface );
            counter = 1 ;
        }

        /* Send a log indicating the main loop has recognized
         * a state change to enable */
        else if (( hbsInv.hbs_state_change == true ) &&
                 ( hbsInv.hbs_disabled == false ))
        {
            ilog ("Heartbeat Enabled with %d pulse period and %d msec mnfa backoff period\n",
                   hbsInv.hbs_pulse_period, (hbsInv.hbs_pulse_period_save * HBS_BACKOFF_FACTOR) );
            ilog ("Heartbeat Thresholds ; minor:%d degrade:%d failure:%d\n",
                hbsInv.hbs_minor_threshold,
                hbsInv.hbs_degrade_threshold,
                hbsInv.hbs_failure_threshold);

            /* print current node inventory to the stdio */
            hbsInv.print_node_info();
        }

        /* go to sleep if disabled */
        if ( hbsInv.hbs_disabled == true )
        {
            wlog_throttled (counter, 1000, "Heartbeat service still disabled\n");
            usleep (50000) ;
            continue ;
        }

        /* Be sure state change flag is cleared */
        hbsInv.hbs_state_change = false ;
        counter = 0 ;

        /* Silent Fault Detection Monitor - Log only for now */
        if ( hbsInv.hbs_silent_fault_detector++ > HBS_MAX_SILENT_FAULT_LOOP_COUNT )
        {
            bool some_progress = false ;

            /* Load the expected pulses and zero detected */
            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                if ( hbsInv.pulse_requests[iface] > 0 )
                {
                    hbsInv.hbs_silent_fault_detector = 0 ;
                    // if ( daemon_is_file_present ( MTC_CMD_FIT__HBSSILENT ) == false )
                    // {
                        some_progress = true ;
                    // }
                    hbsInv.pulse_requests[iface] = 0 ;
                }
            }
            if ( some_progress == false )
            {
                if ( hbsInv.hbs_silent_fault_logged == false )
                {
                    hbsInv.hbs_silent_fault_logged = true;

                    alarm_warning_log ( hbsInv.my_hostname, SERVICESTATUS_LOG_ID,
                            "maintenance heartbeat service is not making forward progress ; "
                            "recommend process restart by controller switchover "
                            "at earliest convenience" , "service=heartbeat");
                }
                hbsInv.hbs_silent_fault_detector = 0 ;
            }
        }

        if ( hbsTimer.ring == false )
        {
            if ( heartbeat_request == true )
            {
                string ri  = "" ;
                int    rri = 0  ;
                string lf  = "\n" ;

                mem_log ((char*)lf.data());

                /* Get the next Resource Reference Identifier
                 * and its Resourvce Identifier. These values
                 * are updated by reference */
                hbsInv.get_rris ( ri, rri );

                /* Load the expected pulses and zero detected */
                for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
                {
                    /* Don't service the infrastructure network if it is not provisioned */
                    if (( iface == INFRA_IFACE ) && ( hbsInv.infra_network_provisioned == false ))
                        continue ;

                    exp_pulses[iface] =
                    hbsInv.hbs_expected_pulses[iface] =
                    hbsInv.create_pulse_list((iface_enum)iface);

                    arrival_histogram[iface] = "" ;
                    unexpected_pulse_list[iface] = "" ;

                    rc = hbs_pulse_request ( (iface_enum)iface, seq_num, ri, rri );
                    if ( rc != 0 )
                    {
                        /* TODO: Fix this with an alarm */
                        wlog_throttled ( pulse_request_fail_log_counter[iface], 100,
                                         "%s hbs_pulse_request failed - rc:%d\n", get_iface_name_str(iface), rc);

                        if ( pulse_request_fail_log_counter[iface] == INTERFACE_ERRORS_FOR_REINIT )
                        {
                            _setup_pulse_messaging ( (iface_enum)iface , daemon_get_rmem_max ()) ;
                        }
                    }
                    else
                    {
                        hbsInv.pulse_requests[iface]++ ;
                        pulse_request_fail_log_counter[iface] = 0 ;
                    }
                }

                /* Set this semaphore to false which puts the
                 * algorithm into 'receive' mode */
                heartbeat_request = false ;

                /* Start the heartbeat timer.
                 * All nodes are expected to send a
                 *  pulse before this timer expires. */
                if ( hbsInv.hbs_pulse_period >= 1000 )
                {
                    /* Call the 'second' timer for pulse periods that exceed a second */
                    int sec = (hbsInv.hbs_pulse_period/1000) ;
                    mtcTimer_start ( hbsTimer, hbsTimer_handler, sec );
                }
                else
                {
                    /* Otherwise call the msec timer */
                    mtcTimer_start_msec ( hbsTimer, hbsTimer_handler, hbsInv.hbs_pulse_period);
                }
            }

            /* We get here many times while in the audit period. */

            /* Each time ; loop over each interface trying to get all
             * the pulse responses that have come in */
            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                /* Do not service the infrastructure interface if it is not provisioned
                 * We won't get here anyway ... gate above prevents it */
                if (( iface == INFRA_IFACE ) && ( hbsInv.infra_network_provisioned != true ))
                    continue ;

                if ( hbs_sock.fired[iface] == true )
                {
                    hbs_sock.fired[iface] = false ;

                    /* lets start getting the pulse responses from provisioned interfaces */
                    /* Receive and handle heartbeat pulse responses from host             */
                    /* nodes. All responses that come in on specific unicast port.        */
                    rc = _pulse_receive( (iface_enum)iface, seq_num );

                    /* Creates a string that represents the pulse arrival time */
                    /*    .    none
                     *    1..9 pulses on that loop
                     *    a..f is 10 to 15 arrivals on that loop
                     *    *    is more than 15 in one group
                     */
                    if ( rc == 0 )
                        arrival_histogram[iface].append(1,'.');
                    else if ( rc > 15 )
                        arrival_histogram[iface].append(1,'*');
                    else if ( rc > 9 )
                    {
                        char c = (char)(87+rc) ;
                        arrival_histogram[iface].append(1,c) ;
                    }
                    else
                    {
                        char c = (char)(48+rc) ;
                        arrival_histogram[iface].append(1,c) ;
                        // ilog ("Char:%s", );
                    }

                    if ( rc > 0 )
                    {
                        if ( rc <= hbsInv.hbs_expected_pulses[iface] )
                        {
                            hbsInv.hbs_expected_pulses[iface] -= rc ;
                        }
                        else
                        {
                            dlog ("%s more heartbeat responses than expected (exp:%d)\n",
                                      get_iface_name_str(iface),
                                      hbsInv.hbs_expected_pulses[iface] );

                            hbsInv.hbs_expected_pulses[iface] = 0 ;
                        }
#ifdef WANT_PULSE_LIST_EMPTY_WARNING
                        if ( hbsInv.hbs_expected_pulses[iface] == 0 )
                        {
                            if ( hbsInv.pulse_list_empty((iface_enum)iface) != true )
                            {
                                elog ("%s Internal - Pulse list should be empty\n", get_iface_name_str(iface));
                            }
                        }
#endif
                    }
                }
            }
        }
        /*
         * Heartbeat pulse period is over !
         * Time to take attendance.
         * The pulse lists should be empty
         *
         */
        else
        {
            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                /* Do not service the infrastructure interface if it is not provisioned */
                if (( iface == INFRA_IFACE ) && ( hbsInv.infra_network_provisioned != true ))
                    continue ;

                #define MAX_LEN 1000
                char str[MAX_LEN] ;

                snprintf (&str[0], MAX_LEN, "%s Histogram: %d - %s\n",
                           get_iface_name_str(iface),
                           exp_pulses[iface],
                           arrival_histogram[iface].c_str());

                mem_log (str);

                if ( !unexpected_pulse_list[iface].empty() )
                {
                     snprintf ( &str[0], MAX_LEN, "%s Others   : %s\n",
                                get_iface_name_str(iface),
                                unexpected_pulse_list[iface].c_str());

                     mem_log(str);
                }
                hbsInv.lost_pulses ( (iface_enum)iface );
            }
            hbsTimer.ring = false ;
            heartbeat_request = true ;
            seq_num++ ;
        }
        daemon_load_fit ();
    }
    daemon_exit ();
}

/* Push daemon state to log file */
void daemon_dump_info ( void )
{
    daemon_dump_membuf_banner ();

    hbsInv.print_node_info ();
    hbsInv.memDumpAllState ();

    daemon_dump_membuf (); /* write mem_logs to log file and clear log list */
}

const char MY_DATA [100] = { "eieio\n" } ;
const char * daemon_stream_info ( void )
{
    return (&MY_DATA[0]);
}

/** Teat Head Entry */
int daemon_run_testhead ( void )
{
    int rc    = PASS;

    nodeLinkClass * hbsInv_testhead_ptr = new nodeLinkClass ;
    hbsInv_testhead_ptr->testmode = true ;

    printf  ("\n\n");
    printf  (TESTHEAD_BAR);
    printf  ("| Node Class Test Head - Private and Public Member Functions\n");
    printf  (TESTHEAD_BAR);
    for ( int i = 0 ; i < 11 ; i++ )
    {
 	    if ( hbsInv_testhead_ptr->testhead ( i+1 ) )
        {
            FAILED ;
            rc = FAIL ;
        }
        else
           PASSED ;
    }
    printf  (TESTHEAD_BAR);
    printf  ("| Heartbeat Service Test Head\n");
    printf  (TESTHEAD_BAR);

    printf  (TESTHEAD_BAR);
    return (rc);
}
