/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Guest Services Agent Daemon
  * 
  * Services: heartbeat
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

#include "daemon_ini.h"    /* Ini Parser Header                          */
#include "daemon_common.h" /* Common definitions and types for daemons   */
#include "daemon_option.h" /* Common options  for daemons                */

#include "nodeBase.h"
#include "nodeMacro.h"     /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET */
#include "nodeUtil.h"      /* for ... get_ip_addresses                   */
#include "nodeTimers.h"    /* maintenance timer utilities start/stop     */
#include "jsonUtil.h"      /* for ... jsonApi_get_key_value              */
#include "httpUtil.h"      /* for ...                                    */

#include "guestBase.h"     /* Guest services Base Header File            */
#include "guestClass.h"    /*                                            */
#include "guestUtil.h"     /* for ... guestUtil_inst_init                */

#include "guestHttpUtil.h" /* for ... guestHttpUtil_init                 */
#include "guestHttpSvr.h"  /* for ... guestHttpSvr_init/_fini/_look      */
#include "guestVimApi.h"   /* for ... guestVimApi_getHostState           */

/* Where to send events */
string guestAgent_ip = "" ;

/* Process Monitor Control Structure */
static ctrl_type _ctrl ;

/** This heartbeat service inventory is tracked by
  * the same nodeLinkClass that maintenance uses.
  *
  */
guestHostClass   hostInv ;
guestHostClass * get_hostInv_ptr ( void )
{
    return (&hostInv);
}

/** Setup the pointer */
int module_init ( void )
{
   return (PASS);
}

msgSock_type * get_mtclogd_sockPtr ( void )
{
    return (&_ctrl.sock.mtclogd);
}

void daemon_sigchld_hdlr ( void )
{
    ; /* dlog("Received SIGCHLD ... no action\n"); */
}

/**
 * Daemon Configuration Structure - The allocated struct
 * @see daemon_common.h for daemon_config_type struct format.
 */
static daemon_config_type guest_config ; 

#ifdef __cplusplus
extern "C" {
#endif
daemon_config_type * daemon_get_cfg_ptr () { return &guest_config ; }
#ifdef __cplusplus 
}
#endif

/* Cleanup exit handler */
void daemon_exit ( void )
{
    daemon_dump_info  ();
    daemon_files_fini ();

    /* Close the event socket */
    if ( _ctrl.sock.server_rx_sock )
       delete (_ctrl.sock.server_rx_sock);
    
    if ( _ctrl.sock.server_tx_sock )
       delete (_ctrl.sock.server_tx_sock);
    
    if ( _ctrl.sock.agent_rx_local_sock )
       delete (_ctrl.sock.agent_rx_local_sock);
    
    if ( _ctrl.sock.agent_rx_float_sock )
       delete (_ctrl.sock.agent_rx_float_sock);
   
    if ( _ctrl.sock.agent_tx_sock )
       delete (_ctrl.sock.agent_tx_sock);

    guestHttpSvr_fini  ();
    guestHttpUtil_fini ();

    fflush (stdout);
    fflush (stderr);

    exit (0);
}

#define CONFIG_CHALLENGE_PERIOD (1)

int _self_provision ( void )
{
    int rc = PASS ;
    int waiting_msg = false ;

    hostInv.hostBase.my_float_ip.clear();
    hostInv.hostBase.my_local_ip.clear();

    for ( ;; )
    {
        get_ip_addresses ( hostInv.hostBase.my_hostname,
                           hostInv.hostBase.my_local_ip ,
                           hostInv.hostBase.my_float_ip );

        if ( hostInv.hostBase.my_float_ip.empty() || hostInv.hostBase.my_local_ip.empty() )
        {
            if ( waiting_msg == false )
            {
                ilog ("Waiting on ip address config ...\n");
                waiting_msg = true ;

                /* Flush the init data */
                fflush (stdout);
                fflush (stderr);
            }
            mtcWait_secs (3);
        }
        else
        {
            break ;
        }
        daemon_signal_hdlr ();
    }
    return (rc);
}


/** Control Config Mask */
// #define CONFIG_MASK   (CONFIG_CHALLENGE_PERIOD)
/** Client Config mask */
#define CONFIG_MASK (CONFIG_CLIENT_RX_PORT    |\
                     CONFIG_AGENT_RX_PORT     |\
                     CONFIG_MTC_CMD_PORT      |\
                     CONFIG_VIM_CMD_RX_PORT   |\
                     CONFIG_VIM_EVENT_RX_PORT |\
                     CONFIG_MTC_EVENT_PORT)

/* Startup config read */
static int _config_handler ( void * user, 
                       const char * section,
                       const char * name,
                       const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "rx_port"))
    {
        config_ptr->agent_rx_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_RX_PORT ; 
    }
    else if (MATCH("agent", "vim_cmd_port"))
    {
        config_ptr->vim_cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_VIM_CMD_RX_PORT ; 
    }
    else if (MATCH("client", "rx_port"))
    {
        config_ptr->client_rx_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_RX_PORT ; 
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
}

/* Startup config read */
static int mtc_config_handler ( void * user, 
                       const char * section,
                       const char * name,
                       const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "hbs_to_mtc_event_port"))
    {
        config_ptr->hbs_to_mtc_event_port = atoi(value);
        config_ptr->mask |= CONFIG_MTC_EVENT_PORT ; 
    }
    if (MATCH("agent", "mtc_to_guest_cmd_port"))
    {
        config_ptr->mtc_to_guest_cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_MTC_CMD_PORT ; 
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
}

int _nfvi_handler( void * user, 
             const char * section,
             const char * name,
             const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("guest-rest-api", "port"))
    {
        config_ptr->vim_event_port = atoi(value);
        config_ptr->mask |= CONFIG_VIM_EVENT_RX_PORT ; 
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
    /* Read the ini */
    char config_fn[100] ;
    guest_config.mask = 0 ;
    sprintf ( &config_fn[0], "/etc/mtc/%s.ini", program_invocation_short_name );
    if (ini_parse(config_fn, _config_handler, &guest_config) < 0)
    {
        elog("Can't load '%s'\n", config_fn );
        return (FAIL_LOAD_INI);
    }

    get_debug_options ( config_fn, &guest_config );

    if (ini_parse(MTCE_CONF_FILE, mtc_config_handler, &guest_config) < 0)
    {
        elog("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(NFVI_PLUGIN_CFG_FILE, _nfvi_handler, &guest_config) < 0)
    {
        elog ("Can't load '%s'\n", NFVI_PLUGIN_CFG_FILE );
        return (FAIL_LOAD_INI);
    }

    /* Verify loaded config against an expected mask 
     * as an ini file fault detection method */
    if ( guest_config.mask != CONFIG_MASK )
    {
        elog ("Error: Agent configuration failed (%x) (%x:%x)\n",
             ((-1 ^ guest_config.mask) & CONFIG_MASK),
             guest_config.mask, CONFIG_MASK );
        return (FAIL_INI_CONFIG);
    }
    
    guest_config.mgmnt_iface = daemon_get_iface_master ( guest_config.mgmnt_iface );
    ilog("Interface   : %s\n", guest_config.mgmnt_iface );
    ilog("Command Port: %d (rx) from mtcAgent\n",   guest_config.mtc_to_guest_cmd_port );
    ilog("Event   Port: %d (tx)   to mtcAgent\n",   guest_config.hbs_to_mtc_event_port );
    ilog("Command Port: %d (tx)   to guestServer\n",  guest_config.client_rx_port);
    ilog("Event   Port: %d (rx) from guestServer\n",guest_config.agent_rx_port );
    ilog("Command Port: %d (rx) from vim\n",guest_config.vim_cmd_port );
    ilog("Event   Port: %d (rx)   to vim\n",guest_config.vim_event_port );

    /* provision this controller */
    if ( _self_provision () != PASS )
    {
        elog ("Failed to self provision active controller\n");
        daemon_exit ();
    }

    return (PASS);
}

void _timer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;
   
    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    sig=sig ; uc = uc ;
 
    if ( !(*tid_ptr) )
    {
        // tlog ("Called with a NULL Timer ID\n");
        return ;
    }
    /* is base ctrl mtc timer */
    else if (( *tid_ptr == _ctrl.timer.tid ) )
    {
        mtcTimer_stop_int_safe ( _ctrl.timer );
        _ctrl.timer.ring = true ;
    }

    /* is base object mtc timer */
    else if (( *tid_ptr == hostInv.audit_timer.tid ) )
    {
        mtcTimer_stop_int_safe ( hostInv.audit_timer );
        hostInv.audit_timer.ring = true ;
    }
    else
    {
         mtcTimer_stop_tid_int_safe (tid_ptr);
    }
}

int mtclogd_port_init ( ctrl_type * ctrl_ptr )
{
    int rc = PASS ;
    int port = ctrl_ptr->sock.mtclogd.port = daemon_get_cfg_ptr()->daemon_log_port ;
    CREATE_REUSABLE_INET_UDP_TX_SOCKET ( LOOPBACK_IP, 
                                         port, 
                                         ctrl_ptr->sock.mtclogd.sock, 
                                         ctrl_ptr->sock.mtclogd.addr, 
                                         ctrl_ptr->sock.mtclogd.port, 
                                         ctrl_ptr->sock.mtclogd.len, 
                                         "mtc logger message", 
                                         rc );
    if ( rc )
    {
        elog ("Failed to setup messaging to mtclogd on port %d\n", port );
    }
    return (rc);
}

/****************************/
/* Initialization Utilities */
/****************************/

/* Construct the messaging sockets *
 * 1. multicast transmit socket    *
 * 2. unicast receive socket       */
int _socket_init ( void )
{
    int rc = PASS ;

    guestAgent_ip = getipbyname ( CONTROLLER );
    ilog ("ControllerIP: %s\n", guestAgent_ip.c_str());

    /* Read the ports the socket struct */
    _ctrl.sock.agent_rx_port  = guest_config.agent_rx_port  ;
    _ctrl.sock.server_rx_port = guest_config.client_rx_port ;

    /******************************************************************/
    /* UDP Tx Message Socket Towards guestServer                      */
    /******************************************************************/

    _ctrl.sock.agent_tx_sock = new msgClassTx(guestAgent_ip.c_str(), guest_config.client_rx_port, IPPROTO_UDP, guest_config.mgmnt_iface);
    rc = _ctrl.sock.agent_tx_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup 'guestAgent' transmitter\n" );
        return (rc) ;
    }

    /******************************************************************/
    /* UDP Tx Message Socket Towards mtcAgent                         */
    /******************************************************************/

    _ctrl.sock.mtc_event_tx_port = guest_config.hbs_to_mtc_event_port ;
    _ctrl.sock.mtc_event_tx_sock = new msgClassTx(LOOPBACK_IP, guest_config.hbs_to_mtc_event_port, IPPROTO_UDP);
    rc = _ctrl.sock.mtc_event_tx_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup 'mtcAgent' 'lo' transmitter on port (%d)\n",
               _ctrl.sock.mtc_event_tx_port );

        return (rc) ;
    }

    /***************************************************************/
    /* Non-Blocking UDP Rx Message Socket for Maintenance Commands */
    /***************************************************************/

    _ctrl.sock.mtc_cmd_port = guest_config.mtc_to_guest_cmd_port ;
    _ctrl.sock.mtc_cmd_sock = new msgClassRx(LOOPBACK_IP, guest_config.mtc_to_guest_cmd_port, IPPROTO_UDP);
    rc = _ctrl.sock.mtc_cmd_sock->return_status;
    if ( rc )    
    {
        elog ("Failed to setup mtce command receive on port %d\n",
               _ctrl.sock.mtc_cmd_port );
        return (rc) ;
    }

    /* Get a socket that listens to the controller's FLOATING IP */
    /* This is the socket that the guestAgent receives events from 
     * the guestServer from the compute on */
    _ctrl.sock.agent_rx_float_sock = new msgClassRx(hostInv.hostBase.my_float_ip.c_str(), guest_config.agent_rx_port, IPPROTO_UDP);
    rc = _ctrl.sock.agent_rx_float_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup 'guestServer' receiver on port %d\n", 
               _ctrl.sock.server_rx_port );
        return (rc) ;
    }

    /* Get a socket that listens to the controller's LOCAL IP */
    /* This is the socket that the guestAgent receives events from 
     * the guestServer from the compute on */
    _ctrl.sock.agent_rx_local_sock = new msgClassRx(hostInv.hostBase.my_local_ip.c_str(), guest_config.agent_rx_port, IPPROTO_UDP);
    rc = _ctrl.sock.agent_rx_local_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup 'guestServer' receiver on port %d\n", 
               _ctrl.sock.server_rx_port );
        return (rc) ;
    }
      
    /* Don't fail the daemon if the logger port is not working */
    mtclogd_port_init (&_ctrl);

    rc = guestHttpSvr_init ( guest_config.vim_cmd_port );

    return (rc) ;  
}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodetype )
{
    int rc = 10 ;

    /* Not used by this service */
    nodetype = nodetype ;

    /* Initialize socket construct and pointer to it */   
    memset ( &_ctrl.sock,   0, sizeof(_ctrl.sock));

    /* initialize the timer */
    mtcTimer_init ( _ctrl.timer );
    _ctrl.timer.hostname = "guestAgent" ;

    /* Assign interface to config */
    guest_config.mgmnt_iface = (char*)iface.data() ;

    httpUtil_init ();

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        rc = FAIL_FILES_INIT ;
    }

    /* Bind signal handlers */
    else if ( daemon_signal_init () != PASS )
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

    /* Setup the heartbeat service messaging sockets */
    else if ( (rc = _socket_init  ( )) != PASS )
    {
        elog ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT ;
    }
    else
    {
        _ctrl.timer.hostname = hostInv.hostBase.my_hostname ;
    }

    return (rc);
}


/***************************************************************************
 *
 * Name:        send_cmd_to_guestServer
 *
 * Description: Messaging interface capable of building command specific
 *              messages and sending them to the guestServer daemon on
 *              the specified compute host.
 *
 * TODO: setup acknowledge mechanism using guestHost
 *
 ***************************************************************************/
int send_cmd_to_guestServer ( string       hostname, 
                              unsigned int cmd,
                              string       uuid,
                              bool         reporting,
                              string       event)
{
    mtc_message_type msg  ;
    int bytes_sent    = 0 ;
    int bytes_to_send = 0 ;
    int rc = PASS ;
    
    string ip = hostInv.get_host_ip(hostname) ;

    
    memset (&msg,0,sizeof(mtc_message_type));    
    memcpy (&msg.hdr[0], get_guest_msg_hdr(), MSG_HEADER_SIZE );
    
    /* Start creating the json string to he client */
    string payload = "{" ;
    payload.append("\"source\":\"");
    payload.append(hostInv.hostBase.my_float_ip);
    payload.append("\"");

    if ( cmd == MTC_EVENT_LOOPBACK )
    {
       ; /* go with default payload only */
    }
    else if ( cmd == MTC_CMD_ADD_INST )
    {
        ilog ("%s %s 'add' instance ; sent to guestServer\n", hostname.c_str(), uuid.c_str());
        payload.append(",\"uuid\":\"");
        payload.append(uuid);
        payload.append("\",\"service\":\"heartbeat\"");
        payload.append(",\"state\":\"");
        if ( reporting == true )
            payload.append("enabled\"");
        else
            payload.append("disabled\"");
    }
    else if ( cmd == MTC_CMD_DEL_INST )
    {
        ilog ("%s %s 'delete' instance ; sent to guestServer\n", hostname.c_str(), uuid.c_str());
        payload.append(",\"uuid\":\"");
        payload.append(uuid);
        payload.append("\",\"service\":\"heartbeat\"");
        payload.append(",\"state\":\"disabled\"");
    }
    else if ( cmd == MTC_CMD_MOD_INST )
    {
        /* this may be a frequent log so its changed to a message log */
        mlog ("%s %s 'modify' instance ; sent to guestServer\n", hostname.c_str(), uuid.c_str());
        payload.append(",\"uuid\":\"");
        payload.append(uuid);
        payload.append("\",\"service\":\"heartbeat\"");

        payload.append(",\"state\":\"");
        if ( reporting == true )
            payload.append("enabled\"");
        else
            payload.append("disabled\"");
    }
    else if ( cmd == MTC_CMD_MOD_HOST )
    {
        payload.append(",\"uuid\":\"");
        payload.append(uuid);

        /* In this host case , the instance heartbeat member
         * contains the state we want to entire host to be
         * put to */
        payload.append("\",\"heartbeat\":\"");
        if ( reporting == true )
            payload.append("enabled\"");
        else
            payload.append("disabled\"");
        
        ilog ("%s %s 'modify' host (reporting=%s); sent to guestServer\n", 
                     hostname.c_str(), 
                     uuid.c_str(),
                     reporting ? "enabled" : "disabled" );
    }
    else if ( cmd == MTC_CMD_QRY_INST )
    {
        /* setting the query flag so the FSM
         * knows to wait for the response. */
        hostInv.set_query_flag ( hostname );
    }
    else if ( cmd == MTC_CMD_VOTE_INST
           || cmd == MTC_CMD_NOTIFY_INST )
    {
        bool vote ;
        payload.append(",\"uuid\":\"");
        payload.append(uuid);
        payload.append("\",\"event\":\"");
        payload.append(event);
        payload.append("\"");

        if ( cmd == MTC_CMD_VOTE_INST )
            vote = true ;
        else
            vote = false ;

        ilog ("%s %s '%s' host (event=%s); sent to guestServer\n", 
                     hostname.c_str(), 
                     uuid.c_str(),
                     vote ? "vote" : "notify",
                     event.c_str());
    }
    else
    {
        slog ("unsupported command (%d)\n", cmd );
        return (FAIL_BAD_CASE);
    }

    payload.append("}");
    memcpy (&msg.buf[0], payload.data(), payload.length());
    msg.cmd = cmd ;
    bytes_to_send = ((sizeof(mtc_message_type))-(BUF_SIZE))+(strlen(msg.buf)) ;
    print_mtc_message ( &msg );
    bytes_sent = _ctrl.sock.agent_tx_sock->write((char *)&msg, bytes_to_send,ip.data(), _ctrl.sock.server_rx_port);

    if ( 0 > bytes_sent )
    {
        elog("%s failed to send command (rc:%i)\n", hostname.c_str(), rc);
        rc = FAIL_SOCKET_SENDTO ;
    }
    else if ( bytes_to_send != bytes_sent )
    {
        wlog ("%s transmit byte count error (%d:%d)\n", 
                  hostname.c_str(), bytes_to_send, bytes_sent );
        rc = FAIL_TO_TRANSMIT ; 
    }
    else
    {
        mlog1 ("Transmit to %s port %5d\n",
                _ctrl.sock.agent_tx_sock->get_dst_str(),
                _ctrl.sock.agent_tx_sock->get_dst_addr()->getPort());
        rc = PASS ;

        /* Schedule receive ACK mechanism - bind a callback */
    }

    return (rc);
}


/***************************************************************************
 *
 * Name:        send_event_to_mtcAgent
 *
 * Description: Messaging interface capable of building the specified event
 *              messages and sending them to the mtcAgent daemon locally.
 *
 * TODO: setup acknowledge mechanism using guestHost
 *
 ***************************************************************************/
int send_event_to_mtcAgent ( string       hostname, 
                             unsigned int event)
{
    int bytes_sent    = 0 ;
    int bytes_to_send = 0 ;
    int rc = FAIL ;

    mtc_message_type msg ;
    memset (&msg, 0 , sizeof(mtc_message_type));    
    memcpy (&msg.hdr[0], get_guest_msg_hdr(), MSG_HEADER_SIZE );
    memcpy (&msg.hdr[MSG_HEADER_SIZE], "guestAgent", strlen("guestAgent"));
    if ( event == MTC_EVENT_MONITOR_READY )
    {
        if ( event == MTC_EVENT_MONITOR_READY )
        {
            ilog ("%s requesting inventory from mtcAgent\n", hostname.c_str());
        }
        msg.cmd = event ;
        print_mtc_message (&msg );
        bytes_to_send = ((sizeof(mtc_message_type))-(BUF_SIZE))+(strlen(msg.buf)) ;
        bytes_sent = _ctrl.sock.mtc_event_tx_sock->write((char*)&msg, bytes_to_send);
        if ( 0 > bytes_sent )
        {
            elog("%s Failed to send event (%d:%m)\n", hostname.c_str(), errno );
            rc = FAIL_SOCKET_SENDTO ;
        }
        else if ( bytes_to_send != bytes_sent )
        {
            wlog ("%s transmit byte count error (%d:%d)\n", 
                  hostname.c_str(), bytes_to_send, bytes_sent );
           rc = FAIL_TO_TRANSMIT ; 
        }
        else
        {
            mlog1 ("Transmit to %s port %d\n",
                    _ctrl.sock.mtc_event_tx_sock->get_dst_str(),
                    _ctrl.sock.mtc_event_tx_sock->get_dst_addr()->getPort());
            rc = PASS ;
            
            /* Schedule receive ACK mechanism - bind a callback */
        }
    }
    else
    {
        slog ("Unsupported event (%d)\n", event );
        return ( FAIL_BAD_CASE );
    }

    return rc ;
}

/***************************************************************************
 *
 * Name:        service_mtcAgent_command
 *
 * Description: Message handling interface capable of servicing mtcAgent
 *              commands such as 'add host', 'del host', 'mod host', etc.
 *
 * TODO: setup acknowledge mechanism using guestHost
 *
 ***************************************************************************/
int service_mtcAgent_command ( unsigned int cmd , char * buf_ptr )
{
    if ( !buf_ptr )
    {
        slog ("Empty payload");
        return (FAIL);
    }
    string uuid     = "";
    string hostname = "";
    string hosttype = "";
    string ip       = "";

    int rc = jsonUtil_get_key_val ( buf_ptr, "hostname", hostname ) ;
    if ( rc != PASS )
    {
        elog ("failed to get hostname\n");
        return (FAIL_GET_HOSTNAME);
    }
    rc = jsonUtil_get_key_val ( buf_ptr, "uuid", uuid ) ;
    if ( rc != PASS )
    {
        elog ("%s failed to get host 'uuid'\n", hostname.c_str());
        elog ("... buffer:%s\n", buf_ptr );

        return (FAIL_INVALID_UUID);
    }

    if ( cmd == MTC_CMD_ADD_HOST )
    {
        rc = jsonUtil_get_key_val ( buf_ptr, "ip", ip );
        if ( rc == PASS )
        {
            rc = jsonUtil_get_key_val ( buf_ptr, "personality", hosttype );
            if ( rc == PASS )
            {            
                rc = hostInv.add_host ( uuid, ip, hostname, hosttype );
            }
            else
            {
                elog ("%s failed to get host 'personality'\n", hostname.c_str());
            }
        }
        else
        {
            elog ("%s failed to get host 'ip'\n", hostname.c_str());
        }
    }
    else if ( cmd == MTC_CMD_MOD_HOST )
    {
        rc = jsonUtil_get_key_val ( buf_ptr, "ip", ip );
        if ( rc == PASS )
        {
            rc = jsonUtil_get_key_val ( buf_ptr, "personality", hosttype );
            if ( rc == PASS )
            {
                rc = hostInv.mod_host ( uuid, ip, hostname, hosttype );
            }
            else
            {
                elog ("%s failed to get host 'personality'\n", hostname.c_str());
            }
        }
        else
        {
            elog ("%s failed to get host 'ip'\n", hostname.c_str());
        }
    }
    else if ( cmd == MTC_CMD_DEL_HOST )
    {
        rc = hostInv.del_host ( uuid );
    }
    else
    {
        wlog ("Unsupported command (%d)\n", cmd );
        rc = FAIL_BAD_CASE ;
    }
    return (rc);
}



int recv_from_guestServer ( unsigned int cmd, char * buf_ptr )
{
    int rc = PASS ;
    switch ( cmd )
    {
        case MTC_EVENT_MONITOR_READY:
        {
            string hostname = "" ;
            if ( jsonUtil_get_key_val ( buf_ptr, "hostname", hostname ) )
            {
                elog ("failed to extract 'hostname' from 'ready event'\n" );
                rc = FAIL_LOCATE_KEY_VALUE ;
            }
            else
            {
                ilog ("%s guestServer ready event\n", hostname.c_str());

                /* Set all the instance state for this host */
                get_hostInv_ptr()->set_inst_state ( hostname );
            }
            break ;
        }
        case MTC_EVENT_HEARTBEAT_RUNNING:
        case MTC_EVENT_HEARTBEAT_STOPPED:
        case MTC_EVENT_HEARTBEAT_ILLHEALTH:
        {
            string hostname = "" ;
            string uuid     = "" ;
            int rc1 = jsonUtil_get_key_val ( buf_ptr, "hostname", hostname );
            int rc2 = jsonUtil_get_key_val ( buf_ptr, "uuid", uuid );
            if ( rc1 | rc2 )
            {
                elog ("failed to parse 'hostname' or 'uuid' from heartbeat event buffer (%d:%d)\n",
                       rc1, rc2 );
                elog ("... Buffer: %s\n", buf_ptr );
                return ( FAIL_KEY_VALUE_PARSE );
            }
            instInfo * instInfo_ptr = get_hostInv_ptr()->get_inst ( uuid );
            rc1 = guestUtil_get_inst_info ( hostname , instInfo_ptr, buf_ptr );
            if ( rc1 == PASS )
            {
                if ( instInfo_ptr )
                {
                    string state ;

                    if ( instInfo_ptr->heartbeat.reporting == true )
                        state = "enabled" ;
                    else
                        state = "disabled" ;

                    if ( cmd == MTC_EVENT_HEARTBEAT_ILLHEALTH )
                    {
                        ilog ("%s %s ill health notification\n", hostname.c_str(), instInfo_ptr->uuid.c_str());
                        rc = guestVimApi_alarm_event ( hostname, uuid );
                    }
                    else if ( cmd == MTC_EVENT_HEARTBEAT_RUNNING )
                    {
                        if ( instInfo_ptr->heartbeating != true )
                        {
                            instInfo_ptr->heartbeating = true ;
                            ilog ("%s %s is now heartbeating\n", hostname.c_str(), instInfo_ptr->uuid.c_str());
                        }
                        string status = "enabled";
                        rc = guestVimApi_svc_event ( hostname, uuid, state, status, instInfo_ptr->restart_to_str);
                    } 
                    else
                    {
                        if (  instInfo_ptr->heartbeating != false )
                        {
                            instInfo_ptr->heartbeating = false ;
                            wlog ("%s %s is not heartbeating\n", hostname.c_str(), instInfo_ptr->uuid.c_str());
                        }
                        string status = "disabled";
                        rc = guestVimApi_svc_event ( hostname, uuid, state, status, "0");
                    }
                    if ( rc != PASS )
                    {
                        elog ("%s %s failed to send state change 'event' to vim (rc:%d)\n", 
                                     hostname.c_str(), instInfo_ptr->uuid.c_str(), rc );
                    }
                }
                else
                {
                    elog ("%s %s failed instance lookup\n", hostname.c_str(), uuid.c_str());
                }
            }
            else
            {
                elog ("failed to get instance info\n");
            }
            break ;
        }

        case MTC_EVENT_HEARTBEAT_LOSS:
        {
            string hostname = "" ;
            string uuid     = "" ;
            int rc1 = jsonUtil_get_key_val ( buf_ptr, "hostname", hostname ) ;
            int rc2 = jsonUtil_get_key_val ( buf_ptr, "uuid", uuid ) ;
            if ( rc1 | rc2 )
            {
                elog ("failed to parse 'heartbeat loss' key values (%d:%d)\n", rc1, rc2 );
                rc = FAIL_LOCATE_KEY_VALUE ;
            }
            else
            {
                if ( get_hostInv_ptr()->get_reporting_state ( hostname ) == true )
                {
                    instInfo * instInfo_ptr = get_hostInv_ptr()->get_inst ( uuid ) ;
                    if ( instInfo_ptr )
                    {
                        if ( instInfo_ptr->heartbeat.reporting == true )
                        {
                            rc = guestVimApi_inst_failed ( hostname, uuid , MTC_EVENT_HEARTBEAT_LOSS, 0 );
                        }
                        else
                        {
                            ilog ("%s %s reporting disabled\n", hostname.c_str(), uuid.c_str() );
                        }
                    }
                    else
                    {
                        elog ("%s %s failed instance lookup\n", hostname.c_str(), uuid.c_str() );
                        rc = FAIL_HOSTNAME_LOOKUP ;
                    }
                }
                else
                {
                   wlog ("%s heartbeat failure reporting disabled\n", hostname.c_str());
                }
            }
            break ;
        }
        case MTC_CMD_QRY_INST:
        {
            string hostname  = "" ;
                    string uuid   = "" ;
                    string status = "" ;
            
            jlog ("%s Instance Query Response: %s\n", hostname.c_str(), buf_ptr);
            
            int rc1 = jsonUtil_get_key_val ( buf_ptr, "hostname", hostname ) ;
            int rc2 = jsonUtil_get_key_val ( buf_ptr, "uuid"    , uuid     ) ;
            if ( rc1 | rc2 )
            {
                ilog ("failed to parse 'hostname' or 'uuid' (%d:%d)\n", rc1, rc2 );
            }

            instInfo * instInfo_ptr = get_hostInv_ptr()->get_inst ( uuid ) ;
            if ( instInfo_ptr )
            {
                /**
                 *  Verify that this instance is still associated with this host.
                 *  This check was added as a fix and seeing
                 *  a number of stale instance inventory in the guestServer.
                 *
                 *  Without this check a late query response from an instance
                 *  that was just deleted can result in an MOD sent to the
                 *  server causing this instance to be mistakenly 
                 *  re-added to its inventory. 
                 **/
                if ( !hostname.compare(instInfo_ptr->hostname) )
                {
                    /* 
                     * Save the current reporting and heartbeating state
                     * only to compare to see if either has changed 
                     */
                    bool current_heartbeating_status = instInfo_ptr->heartbeating ;
                    bool current_reporting_state     = instInfo_ptr->heartbeat.reporting ;

                    if ( guestUtil_get_inst_info ( hostname, instInfo_ptr, buf_ptr ) == PASS )
                    {
                        if ( instInfo_ptr->heartbeat.reporting != current_reporting_state )
                        {
                            wlog ("%s:%s state mismatch\n", hostname.c_str(), uuid.c_str());
                            wlog ("... state is '%s' but should be '%s' ... fixing\n",
                                     instInfo_ptr->heartbeat.reporting ? "enabled" : "disabled", 
                                               current_reporting_state ? "enabled" : "disabled" );
                        
                            instInfo_ptr->heartbeat.reporting = current_reporting_state ; 
                        
                            rc = send_cmd_to_guestServer ( hostname, 
                                                           MTC_CMD_MOD_INST , 
                                                           uuid, 
                                                           current_reporting_state );
                        } 

                        if ( instInfo_ptr->heartbeating != current_heartbeating_status )
                        {
                            string state ;
                            if ( instInfo_ptr->heartbeat.reporting == true )
                                state = "enabled" ;
                            else
                                state = "disabled" ;
                        
                            if ( instInfo_ptr->heartbeating == true )
                            {
                                string status = "enabled" ;
                                ilog ("%s %s is now heartbeating\n", hostname.c_str(), uuid.c_str());
                                rc = guestVimApi_svc_event ( hostname, uuid, state, status, instInfo_ptr->restart_to_str);
                            }
                            else
                            {
                                string status = "disabled" ;
                                wlog ("%s %s is not heartbeating\n", hostname.c_str(), uuid.c_str());
                                rc = guestVimApi_svc_event ( hostname, uuid, state, status, "0" );
                            }
                            if ( rc != PASS )
                            {
                                /* TODO: make this an elog before delivery */
                                elog ("%s %s failed to send state change 'query' to vim (rc:%d)\n", hostname.c_str(), uuid.c_str(), rc );
                            }
                        }
                    }
                }
                else
                {
                    wlog ("%s %s no longer paired ; dropping query response\n",
                              hostname.c_str(), instInfo_ptr->uuid.c_str() );
                                    
                    /* Delete this just in case */
                    send_cmd_to_guestServer ( hostname, MTC_CMD_DEL_INST , uuid, false );
                }
            }
            else
            {
                elog ("%s unknown uuid ; correcting ...\n", uuid.c_str() );
                
                /* Delete this unknown host as it might somehow be stale */
                rc = send_cmd_to_guestServer ( hostname, MTC_CMD_DEL_INST , uuid, false );

                rc = FAIL_UNKNOWN_HOSTNAME ;
            }
            hostInv.clr_query_flag ( hostname );
            break ;
        }
        case MTC_EVENT_VOTE_NOTIFY:
        {
            string hostname = "" ;
            string instance_uuid = "" ;
            string notification_type = "";
            string event = "";
            string vote = "";
            string reason = "";

            int rc1 = jsonUtil_get_key_val ( buf_ptr, "hostname", hostname ) ;
            int rc2 = jsonUtil_get_key_val ( buf_ptr, "uuid", instance_uuid ) ;
            int rc3 = jsonUtil_get_key_val ( buf_ptr, "notification_type", notification_type ) ;
            int rc4 = jsonUtil_get_key_val ( buf_ptr, "event-type", event ) ;
            int rc5 = jsonUtil_get_key_val ( buf_ptr, "vote", vote ) ;
            if ( rc1 | rc2 | rc3 | rc4 | rc5 )
            {
                elog ("failed to parse 'vote-notify' key values (%d:%d:%d:%d:%d)\n", rc1, rc2, rc3, rc4, rc5);
                rc = FAIL_LOCATE_KEY_VALUE ;
            }
            else
            {
                // 'reason' is optional
                jsonUtil_get_key_val ( buf_ptr, "reason", reason ) ;

                jlog ("%s Instance Vote/Notification Response: %s\n", instance_uuid.c_str(), buf_ptr);

                string guest_response = "";

                if (!vote.compare("accept") || !vote.compare("complete"))
                {
                    if (!notification_type.compare("revocable"))
                    {
                        guest_response = "allow";
                    }
                    else if  (!notification_type.compare("irrevocable"))
                    {
                        guest_response = "proceed";
                    }
                    else
                    {
                        rc = FAIL_BAD_PARM;
                        break;
                    }
                }
                else if (!vote.compare("reject"))
                {
                    guest_response = "reject";
                }
                else
                {
                    rc = FAIL_BAD_PARM;
                }
                guestVimApi_inst_action (hostname, instance_uuid, event, guest_response, reason);
            }
            break ;
        }
        default:
            elog ("Unsupported comand (%d)\n", cmd );
    }
    return (rc);
}


void guestHostClass::run_fsm ( string hostname )
{
    guestHostClass::guest_host * guest_host_ptr ;
    guest_host_ptr = guestHostClass::getHost ( hostname );
    if ( guest_host_ptr != NULL )
    {
        /* This FSM is only run on computes */
        if (( guest_host_ptr->hosttype & COMPUTE_TYPE ) == COMPUTE_TYPE)
        {
            flog ("%s FSM\n", hostname.c_str() );
        }
    }
}

/* Top level call to run FSM */
/* TODO: Deal with delete */
int guest_fsm_run ( guestHostClass * obj_ptr )
{
    instInfo instance ;

    /* Run Maintenance on Inventory */
    for ( obj_ptr->hostlist_iter_ptr  = obj_ptr->hostlist.begin () ;
          obj_ptr->hostlist_iter_ptr != obj_ptr->hostlist.end () ;
          obj_ptr->hostlist_iter_ptr++ )
    {
        string hostname = *obj_ptr->hostlist_iter_ptr ;
        
        daemon_signal_hdlr ();

        obj_ptr->run_fsm ( hostname );

        /* Run the audit on each host */
        if ( obj_ptr->audit_timer.ring == true )
        {
            // ilog ("%s FSM Audit !\n", hostname.c_str() );

            obj_ptr->audit_run = true ;

            if ( obj_ptr->get_got_host_state ( hostname ) == false )
            {
                libEvent & event = hostInv.get_host_event ( hostname );
                string uuid = obj_ptr->get_host_uuid (hostname) ;
                int rc = guestVimApi_getHostState ( hostname, uuid, event );

                if ( rc != PASS )
                {
                    wlog ("%s failed to get host level reporting state (rc=%d)\n", 
                              hostname.c_str(), rc);
                }
                else
                {
                    /* Only set it if true as it is defaulted to false already.
                     * The VIM will send an enable command at a later time */
                    if ( !event.value.compare("enabled"))
                    {
                        ilog ("%s fault reporting enabled\n", hostname.c_str());

                        rc = hostInv.set_reporting_state ( hostname, true );
                        if ( rc != PASS )
                        {
                            wlog ("%s failed to set host level reporting state (rc=%d)\n", 
                                      hostname.c_str(), rc);
                        }
                    }
                    else
                    {
                        rc = hostInv.set_reporting_state ( hostname, false );
                    }

                    dlog ("%s Got host state\n", hostname.c_str() );
                    obj_ptr->set_got_host_state ( hostname );
                }
            }

            /* make sure that the instances for this host are loaded */
            if ( obj_ptr->get_got_instances ( hostname ) == false )
            {
                libEvent & event = hostInv.get_host_event ( hostname );
                string uuid = obj_ptr->get_host_uuid (hostname) ;
                int rc = guestVimApi_getHostInst ( hostname, uuid, event );
                if ( rc != PASS )
                {
                    wlog ("%s failed to get host instances (rc=%d)\n", hostname.c_str(), rc);
                }
                else
                {
                    obj_ptr->set_got_instances ( hostname );
                    dlog ("%s instances loaded\n", hostname.c_str() );
                }
            }
            
            /* only query the guestServer if reporting for that server
             * is 'enabled' and instance list is not empty */
            if (( obj_ptr->num_instances       ( hostname ) != 0 ) &&
                ( obj_ptr->get_reporting_state ( hostname ) == true ))
            {
                if ( obj_ptr->get_query_flag ( hostname ) == true )
                {
                    obj_ptr->inc_query_misses ( hostname);
                    dlog ("%s guestServer Query Misses:%d\n", hostname.c_str(),
                                  obj_ptr->get_query_misses ( hostname ));
                }
                else
                {
                    obj_ptr->clr_query_misses ( hostname );
                }
           
                /* Note: The 3rd and 4th parms are not needed 
                 *       for the MTC_CMD_QRY_INST command */
                send_cmd_to_guestServer ( hostname, MTC_CMD_QRY_INST, "", false );
            }
        }
        if ( obj_ptr->exit_fsm == true )
        {
            obj_ptr->exit_fsm = false ;
            break ;
        }
    }
    if (( obj_ptr->audit_timer.ring == true ) && ( obj_ptr->audit_run == true ))
    {
        // dlog ("Audit Restarted\n");
        obj_ptr->audit_run        = false ;
        obj_ptr->audit_timer.ring = false ;
        mtcTimer_start ( obj_ptr->audit_timer , _timer_handler, 10 );
    }

    return ( PASS );
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

void daemon_service_run ( void )
{
    int rc = PASS ;
    int count = 0 ;
    int flush_thld = 0 ;

    mtcTimer_start ( hostInv.audit_timer , _timer_handler, 2 );

    guestHttpUtil_init ();

    /* socket descriptor list */
    std::list<int> socks ;

    /* Not monitoring address changes RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR */
    if (( _ctrl.sock.ioctl_sock = open_ioctl_socket ( )) <= 0 )
    {
        elog ("Failed to create ioctl socket");
        daemon_exit ();
    }
    
    socks.clear();
    socks.push_front (_ctrl.sock.agent_rx_local_sock->getFD());
    socks.push_front (_ctrl.sock.agent_rx_float_sock->getFD());
    socks.push_front (_ctrl.sock.mtc_cmd_sock->getFD());
    dlog ("Selects: %d %d %d\n", _ctrl.sock.agent_rx_local_sock->getFD(),
                                 _ctrl.sock.agent_rx_float_sock->getFD(),
                                 _ctrl.sock.mtc_cmd_sock->getFD());
    socks.sort();

    ilog ("Sending ready event to maintenance\n");
    do
    {
        /* Wait for maintenance */
        rc = send_event_to_mtcAgent ( hostInv.hostBase.my_hostname, 
                                      MTC_EVENT_MONITOR_READY ) ;
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

    ilog ("------------------------------------------------------------\n");

    for ( ;; )
    {
        /* Service Sockets */
        hostInv.waitd.tv_sec = 0;
        hostInv.waitd.tv_usec = GUEST_SOCKET_TO ;

        /* Initialize the master fd_set */
        FD_ZERO(&hostInv.message_readfds);

        FD_SET(_ctrl.sock.agent_rx_local_sock->getFD(), &hostInv.message_readfds);
        FD_SET(_ctrl.sock.agent_rx_float_sock->getFD(), &hostInv.message_readfds);
        FD_SET(_ctrl.sock.mtc_cmd_sock->getFD(), &hostInv.message_readfds);

        /* Call select() and wait only up to SOCKET_WAIT */
        rc = select( socks.back()+1, &hostInv.message_readfds, NULL, NULL, &hostInv.waitd);
        if (( rc < 0 ) || ( rc == 0 ) || ( rc > (int)socks.size()))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                wlog_throttled ( count, 20, "socket select failed (%d:%m)\n", errno);
            }
            else if ( rc > (int)socks.size())
            {
                wlog_throttled ( count, 100, "Select return exceeds current file descriptors (%ld:%d)\n",
                    socks.size(), rc );
            }
            else
            {
                count = 0 ;
            }
        }
        else
        {
            mtc_message_type msg ;
            memset ((void*)&msg,0,sizeof(mtc_message_type));

            /* Service guestServer messages towards the local IP */
            if (FD_ISSET(_ctrl.sock.agent_rx_local_sock->getFD(), &hostInv.message_readfds) )
            {
                int bytes = _ctrl.sock.agent_rx_local_sock->read((char*)&msg.hdr[0], sizeof(mtc_message_type));

                mlog1 ("Received %d bytes from %s:%d:guestServer (local)\n", bytes,
                        _ctrl.sock.agent_rx_local_sock->get_src_str(),
                                _ctrl.sock.agent_rx_local_sock->get_dst_addr()->getPort());

                recv_from_guestServer ( msg.cmd, msg.buf );
                print_mtc_message ( &msg );
            }

            /* Service guestServer messages towards the floating IP */
            else if (FD_ISSET(_ctrl.sock.agent_rx_float_sock->getFD(), &hostInv.message_readfds) )
            {
                int bytes = _ctrl.sock.agent_rx_float_sock->read((char*)&msg.hdr[0], sizeof(mtc_message_type));

                mlog1 ("Received %d bytes from %s:%d:guestServer (float)\n", bytes,
                        _ctrl.sock.agent_rx_float_sock->get_src_str(),
                                 _ctrl.sock.agent_rx_port);

                recv_from_guestServer ( msg.cmd, msg.buf );
                print_mtc_message ( &msg );
            }

            /* Service mtcAgent commands */
            else if (FD_ISSET(_ctrl.sock.mtc_cmd_sock->getFD(), &hostInv.message_readfds) )
            {
                int bytes = _ctrl.sock.mtc_cmd_sock->read((char*)&msg.hdr[0],sizeof(mtc_message_type));

                mlog1 ("Received %d bytes from %s:%d:mtcAgent\n", bytes,
                        _ctrl.sock.mtc_cmd_sock->get_src_str(),
                                 _ctrl.sock.mtc_cmd_port);
                print_mtc_message ( &msg );
                if ( !strncmp ( get_cmd_req_msg_header(), &msg.hdr[0], MSG_HEADER_SIZE ))
                {
                    service_mtcAgent_command ( msg.cmd , &msg.buf[0] );
                    count = 0 ;
                }
                else
                {
                    wlog_throttled ( count, 100, "Invalid message header\n");
                }
            }
            else
            {
                ilog ("Unknown select\n");
            }
        }

        guestHttpSvr_look ();

        guest_fsm_run ( &hostInv ) ;

        daemon_signal_hdlr ();

        /* Support the log flush config option */
        if ( guest_config.flush )
        {
            if ( ++flush_thld > guest_config.flush_thld )
            {
                flush_thld = 0 ;
                fflush (stdout);
                fflush (stderr);
            }
        }
    }
    daemon_exit ();
}

/* Push daemon state to log file */
void daemon_dump_info ( void )
{
    daemon_dump_membuf_banner ();

    hostInv.print_node_info ();
    hostInv.memDumpAllState ();

    daemon_dump_membuf();
}

const char MY_DATA [100] = { "eieio\n" } ;
const char * daemon_stream_info ( void )
{
    return (&MY_DATA[0]);
}

/** Teat Head Entry */
int daemon_run_testhead ( void )
{
    int rc = PASS;
    return (rc);
}
