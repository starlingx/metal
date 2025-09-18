/*
 * Copyright (c) 2013-2020, 2025 Wind River Systems, Inc.
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
#include "jsonUtil.h"      /* for ... jsonUtil_get_key_val               */

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

#define MAX_LEN 1000

/* Stores the MONOTONIC time the last SM heartbeat was received
 * Heartbeat metrics
 * SM heartbeat period definition */
static struct timespec sm_heartbeat_timestamp_last = { 0 , 0 } ;
static struct timespec sm_heartbeat_timestamp_restart = { 0 , 0 } ;
static int sm_heartbeat_count_b2b_misses = 0 ;
static int sm_heartbeat_count            = 0 ;
const int SM_HEARTBEAT_PULSE_INTERVAL_MSEC = 100;
const int SM_HEARTBEAT_PULSE_PERIOD_MSECS  = 800;
const int SM_HEARTBEAT_PULSE_RECOVER_DURATION_MSEC = SM_HEARTBEAT_PULSE_PERIOD_MSECS * 2;
const int SM_HEARTBEAT_PULSE_CONTINUE_BEEP_TO_RECOVER =
    SM_HEARTBEAT_PULSE_RECOVER_DURATION_MSEC / SM_HEARTBEAT_PULSE_INTERVAL_MSEC;

/* Historical String data for mem_logs */
static string unexpected_pulse_list[MAX_IFACES] = { "" , "" } ;
static string arrival_histogram[MAX_IFACES]     = { "" , "" } ;
static string mtcAgent_ip = "" ;
static std::list<string> hostname_inventory ;

/* Used to throttle warning messages that report
 * an error transmitting the pulse request */
static int pulse_request_fail_log_counter[MAX_IFACES] ;

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

/**
 * Module Control Struct - The allocated struct
 * @see hbsBase.h for hbs_ctrl_type struct format.
 */
static hbs_ctrl_type hbs_ctrl ;
hbs_ctrl_type * get_hbs_ctrl_ptr () { return &hbs_ctrl ; }


#define SCHED_MONITOR__MAIN_LOOP ((const char *) "---> scheduling latency : main loop :")
#define SCHED_MONITOR__RECEIVER  ((const char *) "---> scheduling latency : rx pulses :")
void monitor_scheduling ( unsigned long long & this_time, unsigned long long & prev_time , int data, const char * label_ptr )
{
    this_time = gettime_monotonic_nsec () ;
    if ( label_ptr && strncmp ( label_ptr, NODEUTIL_LATENCY_MON_START, strlen(NODEUTIL_LATENCY_MON_START)))
    {
        if ( ! strcmp (SCHED_MONITOR__RECEIVER, label_ptr ) && ( data > (int)hostname_inventory.size() ))
        {
            wlog ("===> receive latency : batch of %d pulses in under scheduling threshold of %d msec\n", data , hbs_config.latency_thld );
        }
        else if ( this_time > (prev_time + (NSEC_TO_MSEC*(hbs_config.latency_thld))))
        {
            llog ("%4llu.%-4llu msec %s at line %d\n",
                 ((this_time-prev_time) > NSEC_TO_MSEC) ? ((this_time-prev_time)/NSEC_TO_MSEC) : 0,
                 ((this_time-prev_time) > NSEC_TO_MSEC) ? ((this_time-prev_time)%NSEC_TO_MSEC) : 0,
                 label_ptr, data);
        }
    }
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

    /* Close the alarm socket */
    if ( hbs_sock.alarm_sock )
       delete (hbs_sock.alarm_sock);

    /* Close the SM sockets */
    if ( hbs_sock.sm_server_sock )
       delete (hbs_sock.sm_server_sock);
    if ( hbs_sock.sm_client_sock )
       delete (hbs_sock.sm_client_sock);

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
#define HBS_MIN_PERIOD     (100)
#define HBS_MAX_PERIOD     (1000)
#define HBS_DEF_PERIOD     (HBS_MAX_PERIOD)
#define HBS_VIRT_PERIOD    (HBS_DEF_PERIOD)
#define HBS_BACKOFF_FACTOR (4) /* period during backoff */

/** Control Config Mask */
#define CONFIG_AGENT_MASK   (CONFIG_AGENT_HBS_PERIOD      |\
                             CONFIG_AGENT_HBS_DEGRADE     |\
                             CONFIG_AGENT_HBS_FAILURE     |\
                             CONFIG_AGENT_MULTICAST       |\
                             CONFIG_SCHED_PRIORITY        |\
                             CONFIG_MTC_TO_HBS_CMD_PORT   |\
                             CONFIG_HBS_TO_MTC_EVENT_PORT |\
                             CONFIG_AGENT_HBS_MGMNT_PORT  |\
                             CONFIG_AGENT_HBS_CLSTR_PORT  |\
                             CONFIG_CLIENT_HBS_MGMNT_PORT |\
                             CONFIG_CLIENT_MTCALARM_PORT  |\
                             CONFIG_CLIENT_HBS_CLSTR_PORT |\
                             CONFIG_AGENT_SM_SERVER_PORT      |\
                             CONFIG_AGENT_SM_CLIENT_PORT)

/* Startup config read */
static int hbs_config_handler ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "heartbeat_period"))
    {
        int curr_period = hbsInv.hbs_pulse_period ;

        hbsInv.hbs_pulse_period = atoi(value);
        hbsInv.hbs_state_change = true ;
        hbsInv.hbs_disabled = false ;
        config_ptr->mask |= CONFIG_AGENT_HBS_PERIOD ;

        /* Adjust the heartbeat period in a virtual environment */
        if (( hbsInv.hbs_pulse_period < HBS_MIN_PERIOD )  ||
            ( hbsInv.hbs_pulse_period > HBS_MAX_PERIOD ))
        {
            hbsInv.hbs_pulse_period = HBS_MIN_PERIOD ;
        }

        if ( daemon_get_run_option("Virtual") )
        {
            hbsInv.hbs_pulse_period = HBS_VIRT_PERIOD ;
        }

        hbsInv.hbs_pulse_period_save = hbsInv.hbs_pulse_period ;
        if ( curr_period != hbsInv.hbs_pulse_period )
        {
            /* initialize cluster info */
            hbs_cluster_init ( hbsInv.hbs_pulse_period, hbs_sock.sm_client_sock );
        }
    }

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
    if (MATCH("agent", "heartbeat_failure_action"))
    {
        hbs_failure_action_enum current_action = hbsInv.hbs_failure_action ;
        /*
         * 1. free previous memory from strdup on reconfig
         * 2. get the new value string
         * 3. convert it to an enum
         * 4. if failure action is 'none' then set the clear_alarms audit bool
         *    telling the main loop to clear all heartbeat related alarms.
         * 5. clear all stats if the action is changed from none to other.
         *
         * Note: The none action prevents any new alarms from being raised.
         */
        if ( config_ptr->hbs_failure_action )
            free(config_ptr->hbs_failure_action);
        config_ptr->hbs_failure_action = strdup(value);

        /* get the configured action */
        hbsInv.hbs_failure_action = get_hbs_failure_action(hbs_config);

        if ( current_action != hbsInv.hbs_failure_action )
        {
            hbs_ctrl.clear_alarms = true ;
            hbsInv.hbs_clear_all_stats();
        }
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
    else if (MATCH("agent", "sm_server_port"))
    {
        config_ptr->sm_server_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_SM_SERVER_PORT ;
    }
    else if (MATCH("agent", "sm_client_port"))
    {
        config_ptr->sm_client_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_SM_CLIENT_PORT ;
    }
    else if (MATCH("client", "hbs_client_mgmnt_port"))
    {
        config_ptr->hbs_client_mgmnt_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_HBS_MGMNT_PORT ;
    }
    else if (MATCH("agent", "hbs_agent_clstr_port"))
    {
        config_ptr->hbs_agent_clstr_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_HBS_CLSTR_PORT ;
    }
    else if (MATCH("client", "hbs_client_clstr_port"))
    {
        config_ptr->hbs_client_clstr_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_HBS_CLSTR_PORT ;
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
    get_debug_options ( MTCE_CONF_FILE, &hbs_config );
    if (ini_parse(MTCE_CONF_FILE, hbs_config_handler, &hbs_config) < 0)
    {
        elog("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(MTCE_INI_FILE, hbs_config_handler, &hbs_config) < 0)
    {
        elog("Can't load '%s'\n", MTCE_INI_FILE );
        return (FAIL_LOAD_INI);
    }

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

    /* Log the startup settings */
    ilog("Realtime Pri: RR/%i \n", hbs_config.scheduling_priority );
    ilog("Pulse Period: %i msec\n",   hbsInv.hbs_pulse_period );
    ilog("Minor   Thld: %i misses\n", hbsInv.hbs_minor_threshold );
    ilog("Degrade Thld: %i misses\n", hbsInv.hbs_degrade_threshold );
    ilog("Failure Thld: %i misses\n", hbsInv.hbs_failure_threshold );
    ilog("Multicast   : %s\n", hbs_config.multicast );

    ilog("Mgmnt Name  : %s\n", hbs_config.mgmnt_iface );

    hbs_config.mgmnt_iface = daemon_get_iface_master ( hbs_config.mgmnt_iface );
    ilog("Mgmnt Master: %s\n", hbs_config.mgmnt_iface );
    ilog("Mgmnt Port  : %d (rx)", hbs_config.hbs_agent_mgmnt_port );
    ilog("Mgmnt Port  : %d (tx)\n", hbs_config.hbs_client_mgmnt_port );

    for ( ;; )
    {
        get_ip_addresses ( hbsInv.my_hostname, hbsInv.my_local_ip , hbsInv.my_float_ip );
        if ( hbsInv.my_local_ip.empty() || hbsInv.my_float_ip.empty() )
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
    /* Fetch the cluster-host interface name.
     * calls daemon_get_iface_master inside so the
     * aggrigated name is returned if it exists */
    get_clstr_iface (&hbs_config.clstr_iface );
    if ( strlen(hbs_config.clstr_iface) )
    {
        int rc = get_iface_address ( hbs_config.clstr_iface,
                                     hbsInv.my_clstr_ip, false );
        if ( rc )
        {
            elog ("get Clstr IP address failed '%s' (%d:%d:%m)\n",
                   hbs_config.clstr_iface, rc, errno );
        }
        else
        {
            ilog ("Clstr Addr  : %s\n", hbsInv.my_clstr_ip.c_str());
        }

        /* The cluster host network is considered unprovisioned
         * for heartbeat while ...
         * ... its interface is 'lo' ... */
        if (!strcmp(hbs_config.clstr_iface, LOOPBACK_IF))
        {
            hbsInv.clstr_network_provisioned = false ;
        }
        /* ... or it and the management interface are the same. */
        else if (!strcmp(hbs_config.clstr_iface, hbs_config.mgmnt_iface))
        {
            hbsInv.clstr_network_provisioned = false ;
        }
        else
        {
            hbsInv.clstr_network_provisioned = true ;
            ilog ("Clstr Name  : %s", hbs_config.clstr_iface );
            ilog ("Clstr Port  : %d (rx)", hbs_config.hbs_agent_clstr_port );
            ilog ("Clstr Port  : %d (tx)", hbs_config.hbs_client_clstr_port );
        }
    }
    ilog("Command Port: %d (rx)\n", hbs_config.mtc_to_hbs_cmd_port );
    ilog("Event Port  : %d (tx)\n", hbs_config.hbs_to_mtc_event_port );
    ilog("Alarm Port  : %d (tx)\n", hbs_config.mtcalarm_req_port );

    /* Set Controller Activity State */
    hbs_config.active = daemon_get_run_option ("active") ;
    ilog ("Controller  : %s\n",
        hbs_config.active ? "Active" : "In-Active" );

    /* pust the activity state into inventory (nodeLinkClass) */
    if ( hbs_config.active == true )
        hbsInv.set_activity_state ( true );
    else
        hbsInv.set_activity_state ( false );

    /* Start assuming a change */
    hbsInv.hbs_state_change = true ;

    /* pull in the degrade only config option */
    hbsInv.clstr_degrade_only = hbs_config.clstr_degrade_only ;

    if ( hbsInv.hbs_degrade_threshold >= hbsInv.hbs_failure_threshold )
    {
        wlog ("Degrade threshold should be smaller than Failure threshold\n");
        wlog ("Heartbeat 'degrade' state disabled ; see %s\n", MTCE_CONF_FILE);
    }


    return (PASS);
}

static struct mtc_timer hbsTimer ;
static struct mtc_timer hbsTimer_audit ;

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
    /* is base mtc timer */
    else if (( *tid_ptr == hbsTimer_audit.tid ) )
    {
        mtcTimer_stop_int_safe ( hbsTimer_audit );
        hbsTimer_audit.ring = true ;
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

    pulse_request_fail_log_counter[i] = 0 ;

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

    /* Load up the interface name */
    if ( i == MGMNT_IFACE )
    {
        if ( hbsInv.mgmnt_link_up_and_running == false )
        {
            wlog("Cannot setup Mgmnt pulse messaging when '%s' interface is down", hbs_config.clstr_iface );
            return(FAIL_BAD_STATE);
        }
        else
        {
            iface = hbs_config.mgmnt_iface ;
            if (strcmp(iface, LOOPBACK_IF))
            {
                hbs_sock.tx_sock[i] =
                new msgClassTx(hbs_config.multicast,hbs_sock.tx_port[i],IPPROTO_UDP,iface);
            }
            else
            {
                hbs_sock.tx_sock[i] =
                new msgClassTx(hbsInv.my_local_ip.data(), hbs_sock.tx_port[i],IPPROTO_UDP,iface);
            }
        }
    }
    else if (( i == CLSTR_IFACE ) &&
             ( hbsInv.clstr_network_provisioned == true ) &&
             ( hbs_config.clstr_iface != NULL ))
    {
        if ( hbsInv.clstr_link_up_and_running == false )
        {
            wlog("Cannot setup Clstr pulse messaging when '%s' interface is down", hbs_config.clstr_iface);
            return(FAIL_BAD_STATE);
        }
        else
        {
            iface = hbs_config.clstr_iface ;
            hbs_sock.tx_sock[i] =
                new msgClassTx(hbs_config.multicast,hbs_sock.tx_port[i],IPPROTO_UDP,iface);
        }
    }
    else
    {
        ilog("no heartbeat on %s network", get_iface_name_str(i) );
        return (PASS);
    }

    /* Create transmit socket */
    if ( hbs_sock.tx_sock[i] )
    {
        if ( hbs_sock.tx_sock[i]->return_status != PASS )
        {
            elog("Failed to create %s pulse transmit socket (%d:%d:%m)\n",
                  get_iface_name_str(i),
                  hbs_sock.tx_sock[i]->return_status,
                  errno );
            delete (hbs_sock.tx_sock[i]);
            hbs_sock.tx_sock[i] = 0 ;
            return (FAIL_SOCKET_CREATE);
        }
        else
        {
            hbs_sock.tx_sock[i]->sock_ok(true);
        }
    }
    else
    {
        elog("Failed to create %s pulse transmit socket (%d:%m)\n",
              get_iface_name_str(i), errno );
        return (FAIL_SOCKET_CREATE);
    }

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
        elog("Failed to create %s pulse receive socket (%d:%d:%m)\n",
              get_iface_name_str(i),
              hbs_sock.rx_sock[i]->return_status,
              errno );
        rc = FAIL_SOCKET_CREATE ;
    }
    else
    {
        /* set rx socket buffer size ro rmem_max */
        if (rmem_max != 0 )
            hbs_sock.rx_sock[i]->setSocketMemory ( iface, "rx pulse socket memory", rmem_max );
        else
            wlog ("failed to query rmem_max ; using rmem_default\n");
        hbs_sock.rx_sock[i]->sock_ok(true);
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

/* *********************************************************************
 *
 * Initialize all heartbeat messaging sockets
 *
 * 1. transmit socket to maintenance (ready event)
 * 2. receive socket from maintenance (inventory)
 * 3. alarm socket to alarmd
 * 4. multicast transmit socket
 * 5. unicast receive socket
 *
 * ********************************************************************/
int hbs_socket_init ( void )
{
    int rc = PASS ;

    /******************************************************************/
    /* UDP Tx Message Socket for Heartbeat Events Towards Maintenance */
    /******************************************************************/
    rc = FAIL_SOCKET_CREATE ;
    {
        /* load local variables */
        int port = hbs_config.hbs_to_mtc_event_port ;
        mtcAgent_ip = getipbyname ( CONTROLLER );

        /* Handle re-init case */
        if ( hbs_sock.hbs_event_tx_sock != NULL )
        {
            delete (hbs_sock.hbs_event_tx_sock);
                    hbs_sock.hbs_event_tx_sock = NULL ;
        }

        /* Create the socket */
        hbs_sock.hbs_event_tx_sock =
        new msgClassTx ( mtcAgent_ip.data(),
                         port,
                         IPPROTO_UDP,
                         hbs_config.mgmnt_iface);

        /* Check the socket */
        if ( hbs_sock.hbs_event_tx_sock != NULL )
        {
            if (hbs_sock.hbs_event_tx_sock->return_status == PASS)
            {
                /* success path */
                hbs_sock.hbs_event_tx_sock->sock_ok(true) ;
                rc = PASS ;
            }
        }

        /* Handle errors */
        if ( rc )
        {
            elog ("Failed to setup event transmit socket: %s:%s:%d\n",
                   hbs_config.mgmnt_iface, mtcAgent_ip.c_str(), port );
            return (rc);
        }
    }

    /****************************************************************/
    /* UDP Rx Message Socket for Maintenance Commands and Inventory */
    /****************************************************************/
    rc = FAIL_SOCKET_CREATE ;
    {
        /* load local variables */
        int port = hbs_config.mtc_to_hbs_cmd_port ;
        int rmem_max = daemon_get_rmem_max();

        /* Handle re-init case */
        if ( hbs_sock.mtc_to_hbs_sock != NULL )
        {
            delete (hbs_sock.mtc_to_hbs_sock);
                    hbs_sock.mtc_to_hbs_sock = NULL ;
        }

        /* Create the socket */
        hbs_sock.mtc_to_hbs_sock =
        new msgClassRx ( hbsInv.my_local_ip.data(),
                         port,
                         IPPROTO_UDP);

        /* Set rx socket buffer size to rmem_max.
         * Needed to handle inventory push from mtcAgent over
         * process restart on at-scale system deployments. */
        if (rmem_max > 0)
        {
            hbs_sock.mtc_to_hbs_sock->setSocketMemory(
                hbs_config.mgmnt_iface,
                "mtc command rx socket memory",
                rmem_max );
        }

        /* Check the socket */
        if (hbs_sock.mtc_to_hbs_sock != NULL )
        {
            if (hbs_sock.mtc_to_hbs_sock->return_status == PASS)
            {
                /* success path */
                hbs_sock.mtc_to_hbs_sock->sock_ok(true) ;
                rc = PASS ;
            }
        }

        /* Handle errors */
        if ( rc )
        {
            elog ("Failed to setup mtce command receive socket: %s:%d\n",
                   hbsInv.my_local_ip.c_str(), port );
            return (rc);
        }
    }

    /*****************************************************************/
    /* UDP Tx Message Socket to alarmd for alarm notifications       */
    /*****************************************************************/
    rc = FAIL_SOCKET_CREATE ;
    {
        hbs_sock.alarm_port = daemon_get_cfg_ptr()->mtcalarm_req_port;

        /* Handle re-init case */
        if ( hbs_sock.alarm_sock != NULL )
        {
            delete (hbs_sock.alarm_sock);
                    hbs_sock.alarm_sock = NULL ;
        }

        /* Create the socket */
        hbs_sock.alarm_sock =
        new msgClassTx(LOOPBACK_IP, hbs_sock.alarm_port, IPPROTO_UDP);

        /* Check the socket */
        if ( hbs_sock.alarm_sock )
        {
            if ( hbs_sock.alarm_sock->return_status == PASS )
            {
                hbs_sock.alarm_sock->sock_ok(true);
                alarm_register_user ( hbs_sock.alarm_sock );
                rc = PASS ;
            }
        }

        /* Handle errors */
        if ( rc )
        {
            elog ("Failed to setup alarm socket: LO:%d\n",
                   hbs_sock.alarm_port );
            alarm_unregister_user();
            return (rc );
        }
    }

    /***************************************************************/
    /* UDP RX Message Socket for SM Requests; LO interface only    */
    /***************************************************************/
    rc = FAIL_SOCKET_CREATE ;
    {
        /* Handle re-init case */
        if ( hbs_sock.sm_server_sock != NULL )
        {
            delete (hbs_sock.sm_server_sock);
                    hbs_sock.sm_server_sock = NULL ;
        }

        /* Create the socket */
        hbs_sock.sm_server_sock =
        new msgClassRx(LOOPBACK_IP, hbs_config.sm_server_port, IPPROTO_UDP);

        /* Check the socket */
        if ( hbs_sock.sm_server_sock )
        {
            if ( hbs_sock.sm_server_sock->return_status == PASS )
            {
                hbs_sock.sm_server_sock->sock_ok(true);
                rc = PASS ;
            }
        }

        /* Handle errors */
        if ( rc )
        {
            elog ("Failed to setup SM receive socket: LO:%d",
                   hbs_config.sm_server_port);
            return (rc) ;
        }
    }

    /***************************************************************/
    /* UDP TX Message Socket for SM Requests; LO interface only    */
    /***************************************************************/
    rc = FAIL_SOCKET_CREATE ;
    {
        /* Handle re-init case */
        if ( hbs_sock.sm_client_sock != NULL )
        {
            delete (hbs_sock.sm_client_sock);
                    hbs_sock.sm_client_sock = NULL ;
        }

        /* Create the socket */
        hbs_sock.sm_client_sock =
        new msgClassTx(LOOPBACK_IP, hbs_config.sm_client_port,IPPROTO_UDP);

        /* Check the socket */
        if ( hbs_sock.sm_client_sock )
        {
            if ( hbs_sock.sm_client_sock->return_status == PASS )
            {
                hbs_sock.sm_client_sock->sock_ok(true);

                /* initialize cluster info */
                hbs_cluster_init ( hbsInv.hbs_pulse_period, hbs_sock.sm_client_sock );

                rc = PASS ;
            }
        }

        /* Handle errors */
        if ( rc )
        {
            elog ("Failed to setup SM transmit socket: LO:%d",
                   hbs_config.sm_client_port);
            return (rc) ;
        }
    }

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
    hbs_sock.rx_port[CLSTR_IFACE] = hbs_config.hbs_agent_clstr_port ;
    hbs_sock.tx_port[CLSTR_IFACE] = hbs_config.hbs_client_clstr_port;

    /* Setup the pulse messaging interfaces */
    SETUP_PULSE_MESSAGING ( hbsInv.clstr_network_provisioned, rmem_max ) ;

    /* Handle re-init case */
    close_netlink_socket ( hbs_sock.netlink_sock );
    if (( hbs_sock.netlink_sock = open_netlink_socket ( RTMGRP_LINK )) <= 0 )
    {
        elog ("Failed to create netlink listener socket");
        rc = FAIL_SOCKET_CREATE ;
    }

    return (rc) ;
}


/* Send a multicast heartbeat pulse request message on a */
/* specific port to all listening nodes on the network.  */
int hbs_pulse_request ( iface_enum iface,
                        unsigned int seq_num,
                        string       hostname_clue,
                        unsigned int lookup_clue)
{
    int bytes = 0 ;
    if ( hbs_sock.tx_sock[iface] )
    {
        /* Add message version - 0 -> 1 with the acction of cluster information */
        hbs_sock.tx_mesg[iface].v = HBS_MESSAGE_VERSION ;

        /* Add the sequence number */
        hbs_sock.tx_mesg[iface].s = seq_num ;

        /* Add which controller initiated this pulse */
        if (hbs_ctrl.controller )
            hbs_sock.tx_mesg[iface].f |= ( hbs_ctrl.controller << CTRLX_BIT );

        /* Add this controller's lookup_clue
         * ... aka RRI (Resource Reference Index) */

        /* Fast lookup clue supported for hostnames less than 32 bytes */
        memset ( &hbs_sock.tx_mesg[iface].m[HBS_HEADER_SIZE], 0, MAX_CHARS_HOSTNAME_32 );
        if (( lookup_clue ) &&
            ( hostname_clue.length() < MAX_CHARS_HOSTNAME_32 ))
        {
            hbs_sock.tx_mesg[iface].c = lookup_clue ;
            memcpy ( &hbs_sock.tx_mesg[iface].m[HBS_HEADER_SIZE],
                      hostname_clue.data(),
                      hostname_clue.length());
        }
        else
        {
            hbs_sock.tx_mesg[iface].c = 0;
        }

        /* Append the cluster info to the pulse request */
        hbs_cluster_append(hbs_sock.tx_mesg[iface]) ;

        /* Calculate the total message size */
        bytes = sizeof(hbs_message_type)-hbs_cluster_unused_bytes();

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

        if ( (bytes = hbs_sock.tx_sock[iface]->write((char*)&hbs_sock.tx_mesg[iface], bytes)) < 0 )
        {
            /* Throttle this error log. */
            elog_throttled( pulse_request_fail_log_counter[iface], 100,
                    "Failed to send %s Pulse request: " \
                    "%d:%s to %s.%d (rc:%i ; %d:%s)\n",
                         get_iface_name_str(iface),
                         hbs_sock.tx_mesg[iface].s,
                         &hbs_sock.tx_mesg[iface].m[0],
                         hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
                         hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
                         bytes, errno, strerror(errno) );
            return (FAIL_SOCKET_SENDTO);
        }
    }
    else
    {
        elog_throttled( pulse_request_fail_log_counter[iface], 100,
                "Unable to send %s pulse request on null socket",
                get_iface_name_str(iface));
        return (FAIL_SOCKET_SENDTO);
    }

#ifdef WANT_FIT_TESTING
hbs_pulse_request_out:
#endif
    mlog ( "%s Pulse Req: (%d) %s:%d: s:%u f:%x [%s] RRI:%d\n",
            get_iface_name_str(iface), bytes,
            hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
            hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
            hbs_sock.tx_mesg[iface].s,
            hbs_sock.tx_mesg[iface].f,
            hbs_sock.tx_mesg[iface].m,
            hbs_sock.tx_mesg[iface].c);
#ifdef WANT_HBS_MEM_LOGS
    char str[MAX_LEN] ;
    snprintf ( &str[0], MAX_LEN, "%s Pulse Req: (%d) %s:%d: s:%u f:%x [%s] RRI:%d\n",
                get_iface_name_str(iface), bytes,
                hbs_sock.tx_sock[iface]->get_dst_addr()->toString(),
                hbs_sock.tx_sock[iface]->get_dst_addr()->getPort(),
                hbs_sock.tx_mesg[iface].s,
                hbs_sock.tx_mesg[iface].f,
                hbs_sock.tx_mesg[iface].m,
                hbs_sock.tx_mesg[iface].c);
    mem_log (&str[0]);
#endif

    return (PASS);
}

int _pulse_receive ( iface_enum iface , unsigned int seq_num )
{
    int bytes = 0 ;

    int detected_pulses = 0 ;

    /* get a starting point */
    unsigned long long  after_rx_time ;
    unsigned long long before_rx_time =  gettime_monotonic_nsec ();

    do
    {
        /* Clean the receive buffer */
        memset ( hbs_sock.rx_mesg[iface].m, 0, sizeof(hbs_message_type) );
        hbs_sock.rx_mesg[iface].s = 0 ;
        hbs_sock.rx_mesg[iface].c = 0 ;
        if ( hbs_sock.rx_sock[iface] == NULL )
        {
            elog ("%s cannot receive pulses - null object\n", get_iface_name_str(iface) );
            return (0);
        }
        if ( (bytes = hbs_sock.rx_sock[iface]->read((char*)&hbs_sock.rx_mesg[iface], sizeof(hbs_message_type))) != -1 )
        {
            /* Look for messages that are not for this controller ..... */
            if ( hbs_ctrl.controller !=
               ((hbs_sock.rx_mesg[iface].f & CTRLX_MASK ) >> CTRLX_BIT))
            {
                /* This path has been verified to not get hit during cluster
                 * feature testing. Leaving the check/continue in just in case.
                 * This dlog is left commented out for easy re-enable
                 * for debug but has no runtime impact */
                // dlog ("controller-%d pulse not for this controller ; for controller-%d",
                //        hbs_ctrl.controller,
                //       (hbs_sock.rx_mesg[iface].f & CTRLX_MASK ) >> CTRLX_BIT);
                continue ;
            }
            mlog ("%s Pulse Rsp: (%d) from:%s:%d: s:%d flags:%x [%-27s] RRI:%d\n",
                      get_iface_name_str(iface), bytes,
                      hbs_sock.rx_sock[iface]->get_src_str(),
                      hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                      hbs_sock.rx_mesg[iface].s,
                      hbs_sock.rx_mesg[iface].f,
                      hbs_sock.rx_mesg[iface].m,
                      hbs_sock.rx_mesg[iface].c);

            /* Validate the header */
            if ( strstr ( hbs_sock.rx_mesg[iface].m, rsp_msg_header) )
            {
                int rc = RETRY ;
                string hostname = hbsInv.get_hostname (hbs_sock.rx_sock[iface]->get_src_str());

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

                // mlog ("%s Pulse Rsp from (%s)\n", get_iface_name_str(iface), hostname.c_str());
                if ( hostname == "localhost" )
                {
                    mlog3 ("%s Pulse Rsp (local): %s:%d: s:%d f:%x [%-27s] RRI:%d\n",
                              get_iface_name_str(iface),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                              hbs_sock.rx_mesg[iface].s,
                              hbs_sock.rx_mesg[iface].f,
                              hbs_sock.rx_mesg[iface].m,
                              hbs_sock.rx_mesg[iface].c);
                }
                else if ( hostname == hbsInv.my_hostname)
                {
                    mlog3 ("%s Pulse Rsp: (self ): %s:%d: s:%d f:%x [%-27s] RRI:%d\n",
                              get_iface_name_str(iface),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                              hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                              hbs_sock.rx_mesg[iface].s,
                              hbs_sock.rx_mesg[iface].f,
                              hbs_sock.rx_mesg[iface].m,
                              hbs_sock.rx_mesg[iface].c);

                    hbsInv.manage_pulse_flags ( hostname, hbs_sock.rx_mesg[iface].f );
                }
                else
                {
                    if ( hbsInv.monitored_pulse ( hostname , iface ) == true )
                    {
                        string extra = "Rsp" ;

                        if ( seq_num != hbs_sock.rx_mesg[iface].s )
                        {
                            extra = "SEQ" ;
                        }
                        else
                        {
                            rc = hbsInv.remove_pulse ( hostname, iface, hbs_sock.rx_mesg[iface].c, hbs_sock.rx_mesg[iface].f ) ;
                        }
#ifdef WANT_HBS_MEM_LOGS
                        char str[MAX_LEN] ;
                        snprintf  (&str[0], MAX_LEN, "%s Pulse %s: (%d): %s:%d: %u:%u:%x:%s\n",
                                    get_iface_name_str(iface), extra.c_str(), bytes,
                                    hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                                    hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                                    hbs_sock.rx_mesg[iface].s,
                                    hbs_sock.rx_mesg[iface].c,
                                    hbs_sock.rx_mesg[iface].f,
                                    hbs_sock.rx_mesg[iface].m);
                        // mlog ("%s", &str[0]);
                        mem_log (str);
#endif
                        if ( !extra.compare("Rsp"))
                        {
                            detected_pulses++ ;
                        }
                        /* don't save data from self */
                        if ( hostname != hbsInv.my_hostname )
                        {
                            if (  hbs_sock.rx_mesg[iface].v >= HBS_MESSAGE_VERSION )
                            {
                                if ( iface == MGMNT_IFACE )
                                    hbs_cluster_save ( hostname, MTCE_HBS_NETWORK_MGMT , hbs_sock.rx_mesg[iface]);
                                else
                                    hbs_cluster_save ( hostname, MTCE_HBS_NETWORK_CLSTR , hbs_sock.rx_mesg[iface]);
                            }
                        }
                    }
                    else
                    {
                        mlog3 ("%s Pulse Dis: (%d) %s:%d: seq:%d flag:%x [%-27s] RRI:%d\n",
                                  get_iface_name_str(iface), bytes,
                                  hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                                  hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                                  hbs_sock.rx_mesg[iface].s,
                                  hbs_sock.rx_mesg[iface].f,
                                  hbs_sock.rx_mesg[iface].m,
                                  hbs_sock.rx_mesg[iface].c);
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
                 mlog  ( "Bad %s Msg: %s:%d: %d:%s\n",
                                get_iface_name_str(iface),
                                hbs_sock.rx_sock[iface]->get_dst_addr()->toString(),
                                hbs_sock.rx_sock[iface]->get_dst_addr()->getPort(),
                                  hbs_sock.rx_mesg[iface].s,
                                  hbs_sock.rx_mesg[iface].m) ;
            }
        }
    } while ( bytes > 0 ) ;
    monitor_scheduling ( after_rx_time, before_rx_time, detected_pulses, SCHED_MONITOR__RECEIVER );
    return (detected_pulses);
}

int send_event ( string & hostname, unsigned int event_cmd, iface_enum iface )
{
    if ((hbs_sock.hbs_event_tx_sock == NULL ) ||
        (hbs_sock.hbs_event_tx_sock->sock_ok() == false ))
    {
        elog ("send event socket not healthy");
        return (FAIL_OPERATION);
    }

    mtc_message_type event ;
    memset (&event, 0 , sizeof(mtc_message_type));
    if ( event_cmd == MTC_EVENT_HEARTBEAT_LOSS )
    {
        if ( hbs_config.debug_state == 2 )
        {
            hbsInv.print_node_info ();
        }
        hbs_cluster_log ( hbsInv.my_hostname, "event", true );
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_heartbeat_loss_header());
    }
    else if (( event_cmd == MTC_EVENT_HEARTBEAT_MINOR_SET )   ||
             ( event_cmd == MTC_EVENT_HEARTBEAT_MINOR_CLR )   ||
             ( event_cmd == MTC_EVENT_HEARTBEAT_DEGRADE_SET ) ||
             ( event_cmd == MTC_EVENT_HEARTBEAT_DEGRADE_CLR ))
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

    snprintf ( &event.hdr[MSG_HEADER_SIZE] , MAX_CHARS_HOSTNAME_32, "%s", hostname.data());

    event.cmd = event_cmd ;
    event.num = 1 ;
    event.parm[0] = iface ;

    /* Support for 64 byte hostnames */
    event.ver = MTC_CMD_FEATURE_VER__KEYVALUE_IN_BUF ;

    /* Json string starts at the beginning of the buffer */
    event.res = 0 ;

    string buf_info = "{\"hostname\":\"" ;
    buf_info.append(hostname);
    buf_info.append("\",\"service\":\"");
    buf_info.append(MTC_SERVICE_HBSAGENT_NAME);
    buf_info.append("\"}");

    /* copy the string into the buffer and add one to the length to
     * accomodate for the null terminator snprintf automatically adds */
    snprintf ( &event.buf[event.res], buf_info.length()+1, "%s", buf_info.data());
    print_mtc_message ( hostname, MTC_CMD_TX, event, get_iface_name_str(iface) , false );
    if ( hbs_sock.hbs_event_tx_sock->write((char*)&event, sizeof(mtc_message_type)) <= 0 )
    {
        elog ("%s failed to send event to maintenance (%d:%m)", hostname.c_str(), errno );
        return ( FAIL_TO_TRANSMIT ) ;
    }
    return PASS ;
}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodetype )
{
    int rc = PASS ;

    /* Not used by this service */
    UNUSED(nodetype);

    /* Initialize socket construct and pointer to it */
    MEMSET_ZERO ( hbs_sock );

    /* Initialize the hbs control struct */
    MEMSET_ZERO ( hbs_ctrl );

    /* init the utility module */
    hbs_utils_init ();

    /* init the cluster control structure */
    hbs_cluster_ctrl_init ();

    /* initialize the timer */
    mtcTimer_init ( hbsTimer, "controller", "heartbeat" );
    mtcTimer_init ( hbsTimer_audit, "controller", "state audit" );

    /* start with no inventory */
    hostname_inventory.clear();

    /* Assign interface to config */
    hbs_config.mgmnt_iface = (char*)iface.data() ;

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return FAIL_FILES_INIT ;
    }

    hbsInv.system_type = daemon_system_type ();

    /* convert node type to integer */
    hbs_ctrl.nodetype = get_host_function_mask ( nodetype ) ;
    ilog ("Node Type   : %s (%d)\n", nodetype.c_str(), hbs_ctrl.nodetype );

    /* Bind signal handlers */
    if ( daemon_signal_init () != PASS )
    {
        elog ("daemon_signal_init failed\n");
        rc = FAIL_SIGNAL_INIT ;
    }

#ifdef WANT_EARLY_CONFIG
    /* Configure the agent */
    else if ( (rc = daemon_configure ( )) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%i)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }
#endif

    /* Init the heartbeat request message */
    else if ( hbs_message_init ( ) != PASS )
    {
        elog ("Failed to initialize pulse request message\n");
        rc = FAIL_MESSAGE_INIT;
    }

    if ( daemon_is_file_present ( NODE_LOCKED_FILE ))
    {
        hbs_ctrl.locked = true ;
    }


    daemon_init_fit();
    return (rc);
}

/*****************************************************************************
 *
 * Name       : hbs_sm_handler
 *
 * Description: Try and receive a Service Management request from sm_server_sock
 *
 *              Expecting request in the following form:
 *                   ~66 bytes with moderate spacing
 *
 *              {
 *                  "origin" :"sm",
 *                  "service":"heartbeat",
 *                  "request":"cluster_info"
 *                  "req_id" : number
 *              }
 *
 *              Successfully parsed request results in a call to
 *              hbs_cluser_send which sends the latest snapshot of
 *              the heartbeat cluser info to SM.
 *
 * Assumptions: log flooding is avoided.
 *
 * Returns    : Nothing
 *
 ****************************************************************************/
static int _hbs_sm_handler_log_throttle = 0 ;
static int _hbs_sm_heartbeat_log_throttle = 0 ;
void hbs_sm_handler ( void )
{
    #define _MAX_MSG_LEN           (80)
    #define _MAX_LOG_CNT         (1000)

    #define PRIMARY_LABEL      "origin"
    #define SERVICE_LABEL     "service"
    #define REQUEST_LABEL     "request"
    #define REQID_LABEL       "reqid"

    #define SUPPORTED_ORIGIN  "sm"
    #define SUPPERTED_SERVICE "heartbeat"
    #define SUPPORTED_REQUEST "cluster_info"

    char sm_mesg[_MAX_MSG_LEN] ;
    MEMSET_ZERO(sm_mesg);
    int bytes = hbs_sock.sm_server_sock->read((char*)&sm_mesg, _MAX_MSG_LEN);
    if ( bytes )
    {
        /* Expecting request in the following form:
         * { "origin":"sm" ... } */
        if ( sm_mesg[0] == '{' )
        {
            int reqid = 0 ;
            string origin  = "" ;
            string service = "" ;
            string request = "" ;
            if ( jsonUtil_get_key_val ( sm_mesg, PRIMARY_LABEL, origin ) != PASS )
            {
                 wlog_throttled ( _hbs_sm_handler_log_throttle, _MAX_LOG_CNT,
                                  "missing primary label 'origin' in request.");
            }
            else if (( origin == SUPPORTED_ORIGIN ) &&
                     ( jsonUtil_get_key_val ( sm_mesg, SERVICE_LABEL, service ) == PASS ) &&
                     ( jsonUtil_get_key_val ( sm_mesg, REQUEST_LABEL, request ) == PASS ) &&
                     ( jsonUtil_get_key_val_int ( sm_mesg, REQID_LABEL, reqid ) == PASS ))
            {
                if (( service == SUPPERTED_SERVICE ) &&
                    ( request == SUPPORTED_REQUEST ))
                {
                    /* SM heartbeat pulses have a reqid = 0 and do not require a response */
                    if ( reqid == 0 )
                    {
                        time_delta_type delta ;
                        struct timespec ts = sm_heartbeat_timestamp_last ;
                        clock_gettime (CLOCK_MONOTONIC, &sm_heartbeat_timestamp_last );
                        if(sm_heartbeat_count_b2b_misses && sm_heartbeat_timestamp_restart.tv_sec == 0)
                        {
                            sm_heartbeat_timestamp_restart = sm_heartbeat_timestamp_last;
                        }
                        timedelta (ts, sm_heartbeat_timestamp_last, delta );
                        sm_heartbeat_count++ ;
                        ilog_throttled(_hbs_sm_heartbeat_log_throttle, 100, "SM Heartbeat %d (%ld.%03ld secs)",
                              sm_heartbeat_count, delta.secs, delta.msecs);
                    }
                    else
                    {
                        /* success path ... */
                        hbs_cluster_send( hbs_sock.sm_client_sock, reqid, "query" );
                    }
                    /* reset log throttle */
                   _hbs_sm_handler_log_throttle = 0 ;
                }
                else
                {
                    wlog_throttled ( _hbs_sm_handler_log_throttle, _MAX_LOG_CNT,
                                     "missing service or request labels in request.");
                }
            }
            else
            {
                wlog_throttled ( _hbs_sm_handler_log_throttle, _MAX_LOG_CNT,
                                 "failed to parse one or more request labels.");
            }
        }
        else
        {
            wlog_throttled ( _hbs_sm_handler_log_throttle, _MAX_LOG_CNT,
                             "improperly formatted json string request.");
        }
    }
    else if ( bytes == -1 )
    {
        wlog_throttled ( _hbs_sm_handler_log_throttle, _MAX_LOG_CNT,
                         "message receive error (%d:%s)",
                         errno, strerror(errno));
    }
    else
    {
        wlog_throttled ( _hbs_sm_handler_log_throttle, _MAX_LOG_CNT,
                         "unknown error Error (rc:%d)", bytes );
    }
    dlog ("... %s", sm_mesg );
}

/****************************************************************************
 *
 * Name       ; manage_sm_heartbeat
 *
 * Purpose    : Determine if we received an SM heartbeat message within
 *              the last SM_HEARTBEAT_PULSE_PERIOD_MSECS
 *
 * Description: Compare the monotonic now time to the monotonic time
 *              of the last received  SM heartbeat pulse.
 *
 * Returns    : True if time dela is less than SM_HEARTBEAT_PULSE_PERIOD_MSECS
 *              False if time delta is greater
 *
 ***************************************************************************/
#define HUGE_NUMBER_B2B_SM_HEARTBEAT_MISSES (10000)
bool manage_sm_heartbeat ( void )
{
    struct timespec ts ;
    time_delta_type delta ;
    clock_gettime (CLOCK_MONOTONIC, &ts );
    timedelta (sm_heartbeat_timestamp_last, ts, delta );
    int64_t delta_in_ms = delta.secs * 1000 + delta.msecs;
    bool heartbeat_ok;
    if ( delta_in_ms > SM_HEARTBEAT_PULSE_PERIOD_MSECS )
    {
        sm_heartbeat_count = 0;
        if ((( ++sm_heartbeat_count_b2b_misses < 20 ) ||
            (!( sm_heartbeat_count_b2b_misses % 1000 ))) &&
            ( sm_heartbeat_count_b2b_misses < HUGE_NUMBER_B2B_SM_HEARTBEAT_MISSES ))
        {
            wlog("SM Heartbeat missing since %ld.%03ld secs ago ; HBS Period Misses:%3d ; Running HB Count:%4d",
                  delta.secs, delta.msecs,
                  sm_heartbeat_count_b2b_misses,
                  sm_heartbeat_count);
        }
        heartbeat_ok = false;
    }
    else
    {
        if(sm_heartbeat_count_b2b_misses)
        {
            int expected_beeps = delta_in_ms / SM_HEARTBEAT_PULSE_INTERVAL_MSEC - 1;

            if(sm_heartbeat_count >= expected_beeps)
            {
                if(sm_heartbeat_count >= SM_HEARTBEAT_PULSE_CONTINUE_BEEP_TO_RECOVER)
                {
                    ilog("SM Heartbeat recovered (%d:%dbeeps/%ldms) after %d missing",
                          sm_heartbeat_count, SM_HEARTBEAT_PULSE_CONTINUE_BEEP_TO_RECOVER,
                          delta_in_ms,
                          sm_heartbeat_count_b2b_misses);

                    sm_heartbeat_count_b2b_misses = 0;
                    sm_heartbeat_count = 0;
                    sm_heartbeat_timestamp_restart.tv_sec = 0;
                    sm_heartbeat_timestamp_restart.tv_nsec = 0;
                    heartbeat_ok = true;
                }else
                {
                    ilog("SM Heartbeat recover continue (%d:%dbeeps/%ldms) after %d missing",
                          sm_heartbeat_count, SM_HEARTBEAT_PULSE_CONTINUE_BEEP_TO_RECOVER,
                          delta_in_ms,
                          sm_heartbeat_count_b2b_misses);
                    heartbeat_ok = false; // not good enough to declare recovered yet
                }
            }else
            {
                ilog("SM Heartbeat recover is interrupted after %ldms, missing %d beeps. "
                     "Counting will restart.",
                    delta_in_ms, expected_beeps - sm_heartbeat_count);
                sm_heartbeat_timestamp_restart = ts;
                sm_heartbeat_count = 1;
                heartbeat_ok = false; // recover is interrupted by further missing beep
            }
        }else
        {
            if(delta_in_ms >= SM_HEARTBEAT_PULSE_INTERVAL_MSEC * 2)
            {
                ilog("SM Heartbeat missing for %ldms:%dms. Not yet declare stall.",
                    delta_in_ms, SM_HEARTBEAT_PULSE_PERIOD_MSECS
                );
            }
            heartbeat_ok = true; // not bad enough to declare heartbeat failed yet
        }
    }
    return heartbeat_ok;
}

/****************************************************************************
 *
 * Name       : daemon_service_run
 *
 * Description: Daemon's main loop
 *
 ***************************************************************************/

void daemon_service_run ( void )
{
#ifdef WANT_HBS_MEM_LOGS
    int exp_pulses[MAX_IFACES] ;
#endif
    int rc = PASS ;
    int counter = 0 ;

    /* staged initialization gates */
    bool goenabled = false ;
    bool sockets_init = false ;

    /* log throttles */

    /* A variable that throttles socket init failure retries and
     * ultimately triggers an exit if that retry count gets too big */
    int socket_init_fail_count = 0 ;

    /* throttle initialization wait logs */
    int  wait_log_throttle = 0 ;


    /* get a starting point */
    unsigned long long prev_time =  gettime_monotonic_nsec ();
    unsigned long long this_time =  prev_time ;

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


    /* Not monitoring address changes RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR */
    if (( hbs_sock.ioctl_sock = open_ioctl_socket ( )) <= 0 )
    {
        elog ("Failed to create ioctl socket");
        daemon_exit ();
    }

    /* Run heartbeat service forever or until stop condition */
    for ( hbsTimer.ring = false , hbsTimer_audit.ring = false ; ; )
    {
        daemon_signal_hdlr ();

        if ( hbsTimer_audit.ring == true )
        {
            /* the state dump is only important after daemon init */
            if ( sockets_init == true )
            {
                hbsInv.print_node_info();
                hbs_state_audit ();
            }

            /* The first audit was run after 30 seconds but then the
             * continuous rate is every hour */
            mtcTimer_start ( hbsTimer_audit, hbsTimer_handler, MTC_HRS_1 );
        }

        /* handle staged initialization */
        if ( sockets_init == false )
        {
            if ( goenabled == false )
            {
                if ( hbsInv.system_type == SYSTEM_TYPE__NORMAL )
                {
                    if ( daemon_is_file_present ( GOENABLED_MAIN_PASS ))
                    {
                        ilog ("GOENABLE (large system)\n");
                        goenabled = true ;
                        wait_log_throttle = 0 ;
                    }
                }
                else
                {
                    if ( daemon_is_file_present ( GOENABLED_SUBF_PASS ))
                    {
                        ilog ("GOENABLE (small system)\n");
                        goenabled = true ;
                        wait_log_throttle = 0 ;
                    }
                }

                if ( goenabled == false )
                {
                    ilog_throttled ( wait_log_throttle, MTC_MINS_5, "GOENABLE wait ...\n");
                    sleep (1);
                    continue ;
                }
            }
            else // ( sockets_init == false )
            {
                string mgmnt_iface = daemon_mgmnt_iface ();
                if ( mgmnt_iface.empty() || ( mgmnt_iface == "none" ))
                {
                    ilog_throttled ( wait_log_throttle, 5, "MGMNT wait ...");
                    sleep (5);
                    continue ;
                }

                /* update management interface name pointer with the master
                 * name if different from current config name */
                if ( hbs_config.mgmnt_iface != mgmnt_iface )
                {
                    if ( hbs_config.mgmnt_iface )
                        free ( hbs_config.mgmnt_iface );
                    hbs_config.mgmnt_iface = strdup((char*)mgmnt_iface.data());
                }
                if ( (rc = daemon_configure ( )) != PASS )
                {
                    elog ("Daemon service configuration failed (rc:%i)\n", rc );
                    daemon_exit();
                }

                if ( get_link_state ( hbs_sock.ioctl_sock, hbs_config.mgmnt_iface, &hbsInv.mgmnt_link_up_and_running ) )
                {
                    hbsInv.mgmnt_link_up_and_running = false ;
                    wlog ("Failed to query %s operational state ; defaulting to down\n", hbs_config.mgmnt_iface );
                }
                else
                {
                    ilog ("Mgmnt %s link is %s\n", hbs_config.mgmnt_iface, hbsInv.mgmnt_link_up_and_running ? "Up" : "Down" );
                }

                if ( hbsInv.clstr_network_provisioned == true )
                {
                    if ( get_link_state ( hbs_sock.ioctl_sock, hbs_config.clstr_iface, &hbsInv.clstr_link_up_and_running ) )
                    {
                        hbsInv.clstr_link_up_and_running = false ;
                        wlog ("Failed to query %s operational state ; defaulting to down\n", hbs_config.clstr_iface );
                    }
                    else
                    {
                        ilog ("Cluster-host %s link is %s\n", hbs_config.clstr_iface, hbsInv.clstr_link_up_and_running ? "Up" : "Down" );
                    }
                }

                /* Setup the heartbeat sockets */
                if ( (rc = hbs_socket_init ()) != PASS )
                {
                    #define HBS_SOCKET_INIT_RETRY_THRESHOLD (3)
                    #define HBS_SOCKET_INIT_RETRY_INTERVAL  (2)
                    if ( socket_init_fail_count++ == HBS_SOCKET_INIT_RETRY_THRESHOLD )
                    {
                        elog ("Failed socket initialization (rc:%d) "
                              "max retries ; exiting ...", rc );
                        daemon_exit ();
                    }
                    else
                    {
                        elog ("Failed socket initialization (rc:%d) ; "
                              "will retry in %d secs ...\n",
                              rc, HBS_SOCKET_INIT_RETRY_INTERVAL);
                        sleep (HBS_SOCKET_INIT_RETRY_INTERVAL);
                    }
                }
                else
                {
                    ilog ("Sending ready event to maintenance\n");
                    do
                    {
                        /* Wait for maintenance */
                        rc = send_event ( hbsInv.my_hostname, MTC_EVENT_HEARTBEAT_READY, MGMNT_IFACE ) ;
                        if ( rc == RETRY )
                        {
                            // TODO: Threshold this loop and exit or this
                            //       could be a silent process failure loop.
                            mtcWait_secs ( 3 );
                        }
                    } while ( rc == RETRY ) ;
                    if ( rc == FAIL )
                    {
                        elog ("Unrecoverable heartbeat startup error (rc=%d)\n", rc );
                        daemon_exit ();
                    }

                    /* Make the main loop schedule in real-time */
                    {
                        struct sched_param param ;
                        memset ( &param, 0, sizeof(struct sched_param));
                        param.sched_priority = hbs_config.scheduling_priority ;
                        if ( sched_setscheduler(0, SCHED_RR, &param) )
                        {
                            elog ("sched_setscheduler (0, SCHED_RR, %d ) returned error (%d:%s)\n",
                            param.sched_priority, errno, strerror(errno));
                        }
                    }

                    /* add this host as inventory to hbsAgent
                     * Although this host is not monitored for heartbeat,
                     * there are OOB flags in the heartbeat message that
                     * are needed to be extracted and locally updated */
                    {
                        /* Scoping this so that the inv variable is freed after the add.
                         * No need saving it around on the stack all the time */
                        node_inv_type inv ;

                        /* init the inv variable */
                        node_inv_init ( inv );
                        inv.name = hbsInv.my_hostname ;
                        inv.nodetype = CONTROLLER_TYPE ;
                        hbsInv.add_heartbeat_host ( inv );

                        /* add this host to local inventory */
                        hostname_inventory.push_front(hbsInv.my_hostname);
                        ilog ("%s added to inventory (self)", hbsInv.my_hostname.c_str());
                    }

                    /* enable the base level signal handler latency monitor */
                    daemon_latency_monitor (true);

                    /* load this controller index number - used for cluster stuff */
                    if ( hbsInv.my_hostname == CONTROLLER_0 )
                        hbs_ctrl.controller = 0 ;
                    else
                        hbs_ctrl.controller = 1 ;

                    /* tell the cluster which controller this is and
                     * how many networks are being monitored */
                    hbs_cluster_nums (hbs_ctrl.controller,hbsInv.clstr_network_provisioned ?2:1);

                    socket_init_fail_count = 0 ;
                    wait_log_throttle = 0 ;
                    sockets_init = true ;
                    monitor_scheduling ( this_time, prev_time, 0, NODEUTIL_LATENCY_MON_START );

                    /* Update Sm heartbeat time to now time */
                    clock_gettime (CLOCK_MONOTONIC, &sm_heartbeat_timestamp_last );

                    /* no need for the heartbeat audit in a simplex system */
                    if ( hbsInv.system_type != SYSTEM_TYPE__AIO__SIMPLEX )
                    {
                        /* start the state audit */
                        /* run the first audit in 30 seconds */
                        mtcTimer_start ( hbsTimer_audit, hbsTimer_handler, MTC_SECS_30 );
                    }
                }
            }
        }

        /* Bypass link management & heartbeat handling prior
         * to sockets being initialized */
        if ( sockets_init == false )
            continue ;

        /* audit for forced alarms clear due to ...
         *
         * 1. first initialization or
         * 2. heartbeat failure action being set to none
         *
         */
        if ( hbs_ctrl.clear_alarms == true )
        {
            if ( hbsInv.active_controller )
            {
                std::list<string>::iterator hostname_ptr ;
                ilog ("clearing all heartbeat alarms");
                for ( hostname_ptr  = hostname_inventory.begin();
                      hostname_ptr != hostname_inventory.end() ;
                      hostname_ptr++ )
                {
                    hbsAlarm_clear_all ( hostname_ptr->data(), hbsInv.clstr_network_provisioned );
                    hbsInv.manage_heartbeat_clear ( hostname_ptr->data(), MAX_IFACES );
                }
            }
            hbs_ctrl.clear_alarms = false ;
        }

        /***************** Service Sockets ********************/
        if ( hbs_ctrl.audit++ == AUDIT_RATE )
        {
            hbs_ctrl.audit = 0 ;
            if ( daemon_is_file_present ( NODE_LOCKED_FILE ))
            {
                hbs_ctrl.locked = true ;
                if ( hbsInv.hbs_disabled == false )
                {
                    hbsInv.hbs_disabled = true ;
                    hbsInv.hbs_state_change = true ;
                    hbs_controller_lock ();
                    ilog ("heartbeat service going disabled (locked)");

                    /* force the throttle 'still disabled' log to wait for
                     * the throttled count before the first log */
                    counter = 1 ;
                }
            }
            else if (( hbsInv.hbs_disabled == true ) &&
                     ( hbsInv.mgmnt_link_up_and_running == true ))
            {
                hbs_ctrl.locked = false ;
                hbsInv.hbs_disabled = false;
                hbsInv.hbs_state_change = true ;
                ilog ("heartbeat service going enabled");
            }
        }

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

        if ( sockets_init )
        {
            /* Add the netlink event listener to the select list */
            if ( hbs_sock.netlink_sock )
            {
                socks.push_back (hbs_sock.netlink_sock);
                FD_SET(hbs_sock.netlink_sock, &hbs_sock.readfds);
            }

            if ( ! hbsInv.hbs_disabled )
            {
                /* Add the management interface to the select list */
                if (( hbs_sock.rx_sock[MGMNT_INTERFACE] ) &&
                    ( hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD()))
                {
                    socks.push_back (hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD());
                    FD_SET(hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD(), &hbs_sock.readfds );
                }

                /* Add the cluster-host network pulse rx socket if its provisioned and have a valid socket */
                if (( hbsInv.clstr_network_provisioned == true ) &&
                    ( hbs_sock.rx_sock[CLSTR_INTERFACE] ) &&
                    ( hbs_sock.rx_sock[CLSTR_INTERFACE]->getFD()))
                {
                    socks.push_back  (hbs_sock.rx_sock[CLSTR_INTERFACE]->getFD());
                    FD_SET(hbs_sock.rx_sock[CLSTR_INTERFACE]->getFD(), &hbs_sock.readfds );
                }
            }

            /* Add the SM receiver to the socket select list */
            if (( hbs_sock.sm_server_sock ) &&
                ( hbs_sock.sm_server_sock->getFD()))
            {
                socks.push_back  (hbs_sock.sm_server_sock->getFD());
                FD_SET(hbs_sock.sm_server_sock->getFD(), &hbs_sock.readfds );
            }
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
                    if ( !strncmp ( get_hbs_cmd_req_header(), &msg.hdr[0], MSG_HEADER_SIZE ))
                    {
                        string hostname ;
                        node_inv_type inv ;
                        node_inv_init(inv);

                        /* 64 byte hostname support adds a json string to
                         * the message buffer containing the hostname as a
                         * key/value pair. */
                        if (( msg.ver >= MTC_CMD_FEATURE_VER__KEYVALUE_IN_BUF ) &&
                            ( msg.buf[msg.res] == '{' ))
                        {
                            if ( jsonUtil_get_key_val(&msg.buf[msg.res],
                                        MTC_JSON_INV_NAME, hostname) == PASS )
                            {
                                inv.name = hostname ;
                                if (( msg.cmd == MTC_CMD_ADD_HOST ) ||
                                    ( msg.cmd == MTC_CMD_MOD_HOST ))
                                {
                                    jsonUtil_get_key_val(&msg.buf[msg.res], MTC_JSON_INV_HOSTIP, inv.ip);
                                    if ( hbsInv.clstr_network_provisioned == true )
                                    {
                                        jsonUtil_get_key_val(&msg.buf[msg.res], MTC_JSON_INV_CLSTRIP, inv.clstr_ip);
                                    }
                                }
                            }
                        }
                        else if ( msg.hdr[MSG_HEADER_SIZE] != '\0' )
                        {
                            /* get hostname by legacy method,
                             * ... from the header */
                            hostname = &msg.hdr[MSG_HEADER_SIZE] ;
                        }
                        if ( hostname.empty() )
                        {
                            /* no hostname ; no action to take */
                            wlog ("unable to get hostname from %s command",
                                   get_mtcNodeCommand_str(msg.cmd));
                        }

                        else if ( msg.cmd == MTC_CMD_ACTIVE_CTRL )
                        {
                            bool logit = false ;
                            if ( hostname == hbsInv.my_hostname )
                            {
                                if ( hbsInv.active_controller == false )
                                {
                                    logit = true ;
                                    hbs_ctrl.clear_alarms = true ;
                                }
                                hbsInv.active_controller = true ;
                            }
                            else
                            {
                                if ( hbsInv.active_controller == true )
                                    logit = true ;
                                hbsInv.active_controller = false ;
                            }
                            if ( logit == true )
                            {
                                ilog ("%s is %sactive",
                                          hbsInv.my_hostname.c_str(),
                                          hbsInv.active_controller ? "" : "in" );

                                /* no need for the heartbeat audit in a simplex system */
                                if ( hbsInv.system_type != SYSTEM_TYPE__AIO__SIMPLEX )
                                {
                                    /* Due to activity state change we will dump
                                     * the heartbeat cluster state at now time
                                     * and then again in 5 seconds only to get
                                     * the regular audit dump restarted at
                                     * regular interval after that. */
                                    hbs_state_audit ();
                                    mtcTimer_reset ( hbsTimer_audit);
                                    mtcTimer_start ( hbsTimer_audit, hbsTimer_handler, MTC_SECS_5 );
                                }
                            }
                        }
                        else if ( msg.cmd == MTC_CMD_ADD_HOST )
                        {
                            inv.nodetype = msg.parm[0];
                            hbsInv.add_heartbeat_host ( inv ) ;
                            hostname_inventory.push_back ( inv.name );
                            hostname_inventory.unique(); // avoid duplicates
                            ilog ("%s added to heartbeat service (%d)\n",
                                      inv.name.c_str(),
                                      inv.nodetype);

                            /* clear any outstanding alarms on the ADD */
                            if (( hbsInv.hbs_failure_action != HBS_FAILURE_ACTION__NONE ) &&
                                ( hbsInv.active_controller == true ))
                            {
                                hbsAlarm_clear_all ( hostname,
                                hbsInv.clstr_network_provisioned );
                            }
                        }
                        else if ( msg.cmd == MTC_CMD_MOD_HOST )
                        {
                            inv.nodetype = msg.parm[0];
                            hbsInv.add_heartbeat_host ( inv ) ;
                            ilog ("%s modified heartbeat info [%d]\n",
                                      inv.name.c_str(),
                                      inv.nodetype );

                            /* clear any outstanding alarms on the ADD */
                            if ( hbsInv.hbs_failure_action != HBS_FAILURE_ACTION__NONE )
                            {
                                hbsAlarm_clear_all ( hostname, hbsInv.clstr_network_provisioned );
                            }
                        }
                        else if ( msg.cmd == MTC_CMD_DEL_HOST )
                        {
                            hbsInv.mon_host ( hostname, false, false );
                            hostname_inventory.remove ( hostname );
                            hbsInv.del_host ( hostname );
                            ilog ("%s deleted from heartbeat service\n", hostname.c_str());

                            /* clear any outstanding alarms on the DEL */
                            if (( hbsInv.hbs_failure_action != HBS_FAILURE_ACTION__NONE ) &&
                                ( hbsInv.active_controller == true ))
                            {
                                hbsAlarm_clear_all ( hostname,
                                hbsInv.clstr_network_provisioned );
                            }
                        }
                        else if ( msg.cmd == MTC_CMD_STOP_HOST )
                        {
                            if ( hostname != hbsInv.my_hostname )
                            {
                                hbsInv.mon_host ( hostname, false, false );
                                hbs_cluster_del ( hostname );
                                ilog ("%s heartbeat service disabled by stop command",
                                          hostname.c_str());
                            }
                        }
                        else if ( msg.cmd == MTC_CMD_START_HOST )
                        {
                            if ( hostname == hbsInv.my_hostname )
                            {
                                dlog ("%s stopping heartbeat of self\n",
                                          hostname.c_str());

                                hbsInv.mon_host ( hostname, false, true );
                                hbs_cluster_del ( hostname );

                            }
                            else
                            {
                                hbs_cluster_add ( hostname );
                                hbsInv.mon_host ( hostname, true, true );
                            }
                        }
                        else if (( msg.cmd == MTC_RESTART_HBS ) &&
                                 ( hostname != hbsInv.my_hostname ))
                        {
                            hbsInv.mon_host ( hostname, true, true  );
                            ilog ("%s heartbeat restart", hostname.c_str());
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
                            hbs_cluster_change ( "backoff" );
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

            if ( ! hbsInv.hbs_disabled )
            {
                if (( hbs_sock.rx_sock[MGMNT_INTERFACE] ) &&
                    ( FD_ISSET(hbs_sock.rx_sock[MGMNT_INTERFACE]->getFD(), &hbs_sock.readfds)))
                {
                    hbs_sock.fired[MGMNT_INTERFACE] = true ;
                }

                if (( hbsInv.clstr_network_provisioned == true ) &&
                    ( hbs_sock.rx_sock[CLSTR_INTERFACE] ) &&
                    (  hbs_sock.rx_sock[CLSTR_INTERFACE]->getFD()) &&
                    ( FD_ISSET(hbs_sock.rx_sock[CLSTR_INTERFACE]->getFD(), &hbs_sock.readfds)))
                {
                    hbs_sock.fired[CLSTR_INTERFACE] = true ;
                }
            }

            if ((hbs_sock.sm_server_sock != NULL ) &&
                ( FD_ISSET(hbs_sock.sm_server_sock->getFD(), &hbs_sock.readfds)))
            {
                hbs_sm_handler();
            }

            if (FD_ISSET( hbs_sock.netlink_sock, &hbs_sock.readfds))
            {
                dlog ("netlink socket fired\n");
                rc = hbsInv.service_netlink_events ( hbs_sock.netlink_sock, hbs_sock.ioctl_sock );
                if ( rc )
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

        if ( hbs_ctrl.locked == false )
        {
            /* Manage enabling and disabling the heartbeat service based on
             * the state of the management link.
             * link up = run heartbeat service
             * link down = disable heatbeat service and monitor the link up to re-enable
             */
            if (( hbsInv.mgmnt_link_up_and_running == false ) &&
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

            else if ( hbsInv.hbs_failure_action == HBS_FAILURE_ACTION__NONE )
            {
                wlog_throttled (counter, 100000, "Heartbeat disabled with action=none\n");
                usleep (50000) ;
                continue ;
            }
        }

        /* go to sleep if disabled */
        else if ( hbsInv.hbs_disabled == true )
        {
            wlog_throttled (counter, 100000,
                            "Heartbeat service still disabled %s",
                            hbs_ctrl.locked ? "(locked)" : "");

            hbsInv.hbs_state_change = false ;
            usleep (50000) ;
            continue ;
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
                    if ( hbsInv.hbs_silent_fault_logged == true )
                    {
                        ilog ("Heartbeat service is now making forward process again");
                        hbsInv.hbs_silent_fault_logged   = false ;
                        hbsInv.hbs_silent_fault_detector = 0     ;
                    }
                }
            }
            if ( some_progress == false )
            {
                if ( hbsInv.hbs_silent_fault_logged == false )
                {
                    hbsInv.hbs_silent_fault_logged = true;

                    /* Generate a customer log that indicates the heartbeat service
                     * is not making forward progress. This is an event, not an alarm.
                     * The data of the log indicates the time the issue was detected.
                     * There is no automatic recovery method. If the issue does not
                     * resolve on its own then manually restarting the hbsAgent OR
                     * or lock/unlock the named controller may resolve. */
                    alarm_warning_log ( hbsInv.my_hostname, SERVICESTATUS_LOG_ID,
                            "maintenance heartbeat service is not making forward progress", "service=heartbeat");
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

#ifdef WANT_HBS_MEM_LOGS
                mem_log ((char*)lf.data());
#endif

                /* Get the next Resource Reference Identifier
                 * and its Resourvce Identifier. These values
                 * are updated by reference */
                hbsInv.get_rris ( ri, rri );

                /* Load the expected pulses and zero detected */
                for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
                {
                    /* Don't service the cluster-host network if it is not provisioned */
                    if (( iface == CLSTR_IFACE ) && ( hbsInv.clstr_network_provisioned == false ))
                        continue ;

#ifdef WANT_HBS_MEM_LOGS
                    exp_pulses[iface] =
#endif
                    hbsInv.hbs_expected_pulses[iface] =
                    hbsInv.create_pulse_list((iface_enum)iface);

                    arrival_histogram[iface] = "" ;
                    unexpected_pulse_list[iface] = "" ;


                    rc = hbs_pulse_request ( (iface_enum)iface, seq_num, ri, rri );
                    if ( rc != 0 )
                    {
                        /* TODO: Fix this with an alarm */
                        if ( pulse_request_fail_log_counter[iface] > INTERFACE_ERRORS_FOR_REINIT )
                        {
                            rc = _setup_pulse_messaging ( (iface_enum)iface , daemon_get_rmem_max ()) ;
                            if ( rc )
                                continue ;
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

                /* tell cluster module that a new pulse period has started */
                hbs_cluster_period_start();

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
                /* Do not service the cluster-host interface if it is not provisioned
                 * We won't get here anyway ... gate above prevents it */
                if (( iface == CLSTR_IFACE ) && ( hbsInv.clstr_network_provisioned != true ))
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
            bool heartbeat_ok = manage_sm_heartbeat();

            /* manage vault wrt peer controller */
            hbs_cluster_peer();

            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                /* Do not service the cluster-host interface if it is not provisioned */
                if (( iface == CLSTR_IFACE ) && ( hbsInv.clstr_network_provisioned != true ))
                    continue ;

#ifdef WANT_HBS_MEM_LOGS
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
#endif
                /*
                 * Assume storage-0 is responding until otherwise proven
                 * its not. Keep in mind that the 'lost_pulses' interface
                 * only counts nodes that have not responded.
                 */
                bool storage_0_responding = true ;
                int lost = hbsInv.lost_pulses ((iface_enum)iface, storage_0_responding);
                if ( !hbs_ctrl.locked && !hbsInv.hbs_disabled )
                {
                    hbs_cluster_update ((iface_enum)iface, lost, storage_0_responding, heartbeat_ok );
                }
            }
            /* log cluster throttled */
            if ((( heartbeat_ok == false ) &&
                ( !( sm_heartbeat_count_b2b_misses % 1000 ))) &&
                ( sm_heartbeat_count_b2b_misses < HUGE_NUMBER_B2B_SM_HEARTBEAT_MISSES ))
            {
                hbs_state_audit ( );
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
    hbs_state_audit();
    hbsInv.memDumpAllState ();

#ifdef WANT_HBS_MEM_LOGS
    daemon_dump_membuf (); /* write mem_logs to log file and clear log list */
#endif
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
    delete(hbsInv_testhead_ptr);

    printf  (TESTHEAD_BAR);
    printf  ("| Heartbeat Service Test Head\n");
    printf  (TESTHEAD_BAR);

    printf  (TESTHEAD_BAR);
    return (rc);
}
