/*
 * Copyright (c) 2013-2016, 2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

 /**
  * @file
  * Wind River CGTS Platform Compute Maintenance Daemon
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
 *       mtc_message_init
 *       mtc_socket_init
 *
 *    daemon_service_run
 *       forever ( timer_handler )
 *           mtc_service_command
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>   /* for hostent */
#include <iostream>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <list>
#include <json-c/json.h> /* for ... json_tokener_parse                    */

using namespace std;

#include "daemon_ini.h"     /* for ... Init parset header                 */
#include "daemon_common.h"  /* for ... common daemon definitions          */
#include "daemon_option.h"  /* for ... daemon main options                */

#include "nodeBase.h"       /* for ... Common Definitions                 */
#include "nodeTimers.h"     /* fpr ... Timer Service                      */
#include "nodeUtil.h"       /* for ... Common Utilities                   */
#include "hostUtil.h"       /* for ... hostUtil_is_valid_...              */
#include "jsonUtil.h"       /* for ... jsonUtil_get_key_value_string      */
#include "bmcUtil.h"        /* for ... bmcUtil_accessInfo_type            */
#include "ipmiUtil.h"       /* for ... ipmiUtil_reset_host_now            */
#include "nodeMacro.h"      /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET */
#include "mtcNodeMsg.h"     /* for ... common maintenance messaging       */
#include "mtcNodeComp.h"    /* for ... this module header                 */
#include "regexUtil.h"      /* for ... Regex and String utilities         */
extern "C"
{
#include "amon.h"           /* for ... active monitoring utilities        */

}

static ctrl_type ctrl ;
ctrl_type * get_ctrl_ptr ( void )
{
    return (&ctrl);
}

string get_who_i_am ( void )
{
    return (ctrl.who_i_am) ;
}

bool is_subfunction_worker ( void )
{
    if ( ctrl.subfunction & WORKER_TYPE ) {
        return true ;
    }
    else
        return false ;
}

/* returns my hostname */
string get_hostname ( void )
{
    return ( &ctrl.hostname[0] );
}

/**
 * Daemon Configuration Structure - The allocated struct
 * @see daemon_common.h for daemon_config_type struct format.
 */
static daemon_config_type mtc_config ;
daemon_config_type * daemon_get_cfg_ptr () { return &mtc_config ; }

/**
 * Messaging Socket Control Struct - The allocated struct
 * @see nodeBase.h for mtc_socket_type struct format.
 */
static mtc_socket_type mtc_sock   ;
static mtc_socket_type * sock_ptr ;

static bmcUtil_accessInfo_type peer_controller = {"none","none","none","none","none"};
static bmcUtil_accessInfo_type this_controller = {"none","none","none","none","none"};

int run_goenabled_scripts ( string type );

/* Looks up the timer ID and asserts the corresponding node's ringer */
void timer_handler ( int sig, siginfo_t *si, void *uc)
{
    /* Avoid compiler errors/warnings */
    UNUSED(sig);
    UNUSED(si);
    UNUSED(uc);

    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;

    if ( !(*tid_ptr) )
    {
        return ;
    }
    else if ( *tid_ptr == ctrl.timer.tid )
    {
        mtcTimer_stop_int_safe ( ctrl.timer );
        ctrl.timer.ring = true ;
    }
    else if ( *tid_ptr == ctrl.goenabled.timer.tid )
    {
        mtcTimer_stop_int_safe ( ctrl.goenabled.timer );
        ctrl.goenabled.timer.ring = true ;
    }
    else if ( *tid_ptr == ctrl.hostservices.timer.tid )
    {
        mtcTimer_stop_int_safe ( ctrl.hostservices.timer );
        ctrl.hostservices.timer.ring = true ;
    }
    else if ( *tid_ptr == ctrl.peer_ctrlr_reset.sync_timer.tid )
    {
        ctrl.peer_ctrlr_reset.sync_timer.ring = true ;
        mtcTimer_stop_int_safe ( ctrl.peer_ctrlr_reset.sync_timer );
    }
    else if ( *tid_ptr == ctrl.peer_ctrlr_reset.audit_timer.tid )
    {
        /* use auto restart */
        ctrl.peer_ctrlr_reset.audit_timer.ring = true ;
    }
    else
    {
        mtcTimer_stop_tid_int_safe ( tid_ptr );
    }
}

/********************************************/
/* Network receive socket 'close' functions */
/********************************************/

void _close_pxeboot_rx_socket ( void )
{
    if ( mtc_sock.pxeboot_rx_socket )
    {
        close (mtc_sock.pxeboot_rx_socket);
        mtc_sock.pxeboot_rx_socket = 0 ;
    }
}

void _close_mgmt_rx_socket ( void )
{
    if ( mtc_sock.mtc_client_mgmt_rx_socket )
    {
        delete(mtc_sock.mtc_client_mgmt_rx_socket);
        mtc_sock.mtc_client_mgmt_rx_socket = 0 ;
    }
}

void _close_clstr_rx_socket ( void )
{
    if ( mtc_sock.mtc_client_clstr_rx_socket )
    {
        delete(mtc_sock.mtc_client_clstr_rx_socket);
        mtc_sock.mtc_client_clstr_rx_socket = 0 ;
    }
}

/*********************************************/
/* Network transmit socket 'close' functions */
/*********************************************/

void _close_pxeboot_tx_socket ( void )
{
    if ( mtc_sock.pxeboot_tx_socket )
    {
        close (mtc_sock.pxeboot_tx_socket);
        mtc_sock.pxeboot_tx_socket = 0 ;
    }
}

void _close_mgmt_tx_socket ( void )
{
    if (mtc_sock.mtc_client_mgmt_tx_socket)
    {
        delete (mtc_sock.mtc_client_mgmt_tx_socket);
        mtc_sock.mtc_client_mgmt_tx_socket = 0 ;
    }
}

void _close_clstr_tx_sockets ( void )
{
    if (mtc_sock.mtc_client_clstr_tx_socket_c0)
    {
        delete (mtc_sock.mtc_client_clstr_tx_socket_c0);
        mtc_sock.mtc_client_clstr_tx_socket_c0 = 0 ;
    }
    if (mtc_sock.mtc_client_clstr_tx_socket_c1)
    {
        delete (mtc_sock.mtc_client_clstr_tx_socket_c1);
        mtc_sock.mtc_client_clstr_tx_socket_c1 = 0 ;
    }
}

void _close_amon_sock ( void )
{
    if ( mtc_sock.amon_socket )
    {
        close (mtc_sock.amon_socket);
        mtc_sock.amon_socket = 0 ;
    }
}

void daemon_exit ( void )
{
    daemon_files_fini ();

    _close_mgmt_rx_socket ();
    _close_clstr_rx_socket ();
    _close_mgmt_tx_socket ();
    _close_clstr_tx_sockets();
    _close_amon_sock       ();

    exit (0) ;
}

/* Startup config read */
static int mtc_config_handler ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "mtc_agent_port"))
    {
        config_ptr->mtc_agent_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_MTC_MGMNT_PORT ;
    }
    else if (MATCH("client", "mtc_rx_mgmnt_port"))
    {
        config_ptr->mtc_rx_mgmnt_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_MTC_MGMNT_PORT ;
    }
    else if (MATCH("client", "mtc_rx_clstr_port"))
    {
        config_ptr->mtc_rx_clstr_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_MTC_CLSTR_PORT ;
    }
    else if (MATCH("agent", "mtc_rx_pxeboot_port"))
    {
        // The mtcClient fetches the mtcAgent's pxeboot receive
        // port and uses it for the mtcClient's pxeboot transmitter.
        config_ptr->mtc_tx_pxeboot_port = atoi(value);
        mtc_sock.mtc_tx_pxeboot_port = config_ptr->mtc_tx_pxeboot_port;
    }
    else if (MATCH("client", "mtc_rx_pxeboot_port"))
    {
        config_ptr->mtc_rx_pxeboot_port = atoi(value);
        mtc_sock.mtc_rx_pxeboot_port = mtc_config.mtc_rx_pxeboot_port;
    }
    else if (MATCH("timeouts", "failsafe_shutdown_delay"))
    {
        config_ptr->failsafe_shutdown_delay = atoi(value);
        ilog ("Shutdown TO : %d secs\n", config_ptr->failsafe_shutdown_delay );
    }
    if (( ctrl.nodetype & CONTROLLER_TYPE ) &&
        (MATCH("client", "sync_b4_peer_ctrlr_reset")))
    {
        ctrl.peer_ctrlr_reset.sync = atoi(value);
        ilog("SyncB4 Reset: %s",
              ctrl.peer_ctrlr_reset.sync ? "Yes" : "No" );
    }
    return (PASS);
}

/* Read the mtc.ini file and load control    */
/* settings into the daemon configuration  */
int daemon_configure ( void )
{
    int rc = FAIL ;

    /* Read the ini */
    mtc_config.mask = 0 ;
    if (ini_parse(MTCE_CONF_FILE, mtc_config_handler, &mtc_config) < 0)
    {
        elog("Failed to load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    get_debug_options ( MTCE_CONF_FILE, &mtc_config );

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( mtc_config.mask != CONFIG_CLIENT_MASK )
    {
        elog ("Failed Compute Mtc Configuration (%x)",
             (( -1 ^ mtc_config.mask ) & CONFIG_CLIENT_MASK) );
        rc = FAIL_INI_CONFIG ;
    }

    else
    {
        ilog("Agent Mgmnt : %d (tx)\n", mtc_config.mtc_agent_port );
        ilog("Client Mgmnt: %d (rx)\n", mtc_config.mtc_rx_mgmnt_port );

        if (ini_parse(MTCE_CONF_FILE, client_timeout_handler, &mtc_config) < 0)
        {
            elog ("Can't load '%s'\n", MTCE_CONF_FILE );
            return (FAIL_LOAD_INI);
        }

        rc  = PASS ;
    }
    daemon_load_fit();
    return (rc);
}

/****************************/
/* Initialization Utilities */
/****************************/

void setup_mgmt_rx_socket ( void )
{
    dlog ("setup of Mgmt receive socket");
    ctrl.mgmnt_iface = daemon_mgmnt_iface() ;
    ctrl.mgmnt_iface = daemon_get_iface_master ((char*)ctrl.mgmnt_iface.data());

    if ( ! ctrl.mgmnt_iface.empty() )
    {
        ilog("Mgmnt iface : %s\n", ctrl.mgmnt_iface.c_str() );
        get_iface_macaddr  ( ctrl.mgmnt_iface.data(), ctrl.macaddr );
        get_iface_address  ( ctrl.mgmnt_iface.data(), ctrl.address , true );

        _close_mgmt_rx_socket ();
        mtc_sock.mtc_client_mgmt_rx_socket = new msgClassRx(ctrl.address.c_str(),mtc_sock.mtc_mgmnt_cmd_port, IPPROTO_UDP, ctrl.mgmnt_iface.data(), false );

        /* update health of socket */
        if ( mtc_sock.mtc_client_mgmt_rx_socket )
        {
            /* look for fault insertion request */
            if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_RXSOCK ) )
                mtc_sock.mtc_client_mgmt_rx_socket->return_status = FAIL ;

            if ( mtc_sock.mtc_client_mgmt_rx_socket->return_status == PASS )
            {
                mtc_sock.mtc_client_mgmt_rx_socket->sock_ok (true);
            }
            else
            {
                elog ("failed to init 'management rx' socket (rc:%d)\n",
                mtc_sock.mtc_client_mgmt_rx_socket->return_status );
                mtc_sock.mtc_client_mgmt_rx_socket->sock_ok (false);
            }
        }
    }
}

void setup_pxeboot_rx_socket ( void )
{
    if ( !ctrl.pxeboot_iface_provisioned ) return ;
    string log_prefix = "setup pxeboot receive socket" ;

    /* The pxeboot interface is always the management interface  */
    ctrl.pxeboot_iface = daemon_mgmnt_iface() ;
    ctrl.pxeboot_iface = daemon_get_iface_master ((char*)ctrl.pxeboot_iface.data());

    /* Use the learned parent if it exists and is not the same */
    if ( ! ctrl.iface_info[PXEBOOT_INTERFACE].parent.empty() )
        if ( ctrl.pxeboot_iface != ctrl.iface_info[PXEBOOT_INTERFACE].parent )
            ctrl.pxeboot_iface = ctrl.iface_info[PXEBOOT_INTERFACE].parent ;

    if ( ctrl.pxeboot_iface.empty() )
    {
        wlog ("cannot %s without a pxeboot iface: %s",
              log_prefix.c_str(),
              ctrl.pxeboot_iface.c_str());
    }
    else if ( mtc_sock.mtc_rx_pxeboot_port <= 0 )
    {
        wlog ("cannot %s without a valid ; port: %d",
               log_prefix.c_str(),
               mtc_sock.mtc_rx_pxeboot_port)
    }
    else if ( ctrl.pxeboot_addr.empty() )
    {
        wlog ("cannot %s socket on %s port %d with no pxeboot address",
               log_prefix.c_str(),
               ctrl.pxeboot_iface.c_str(),
               mtc_sock.mtc_rx_pxeboot_port)
        return ;
    }

    ilog ("%s on %s:%s:%d",
            log_prefix.c_str(),
            ctrl.pxeboot_iface.c_str(),
            ctrl.pxeboot_addr.c_str(),
            mtc_sock.mtc_rx_pxeboot_port);

    _close_pxeboot_rx_socket ();

    struct sockaddr_in pxeboot_addr ;

    // Create the socket
    if ((mtc_sock.pxeboot_rx_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        elog ("failed to create IPV4 pxeboot receive socket");
    }

    // Initialize pxeboot address structure
    memset(&pxeboot_addr, 0, sizeof(pxeboot_addr));

    pxeboot_addr.sin_family = AF_INET;
    pxeboot_addr.sin_port = htons(mtc_sock.mtc_rx_pxeboot_port);
    pxeboot_addr.sin_addr.s_addr = inet_addr(ctrl.pxeboot_addr.data());

    // Bind the pxeboot unit address and messaging port to socket
    if (bind(mtc_sock.pxeboot_rx_socket, (const struct sockaddr*)&pxeboot_addr, sizeof(pxeboot_addr)) == -1)
    {
        elog ("failed to bind %s:%d to socket",
               ctrl.pxeboot_addr.c_str(),
               mtc_sock.mtc_rx_pxeboot_port);
        _close_pxeboot_rx_socket();
    }
}

void setup_clstr_rx_socket ( void )
{
    if ( !ctrl.clstr_iface_provisioned ) return ;
    ilog ("setup of cluster-host receive socket");
    /* Fetch the cluster-host interface name.
     * calls daemon_get_iface_master inside so the
     * aggrigated name is returned if it exists */
    get_clstr_iface (&mtc_config.clstr_iface );
    ctrl.clstr_iface = mtc_config.clstr_iface ;
    if ( !ctrl.clstr_iface.empty())
    {
        /* Only get the cluster-host network address if it is provisioned */
        if ( get_iface_address  ( ctrl.clstr_iface.data(), ctrl.address_clstr, false ) == PASS )
        {
            ilog ("Cluster-host iface : %s\n", ctrl.clstr_iface.c_str());
            ilog ("Cluster-host addr  : %s\n", ctrl.address_clstr.c_str());
        }
    }
    if ( !ctrl.address_clstr.empty() )
    {
        _close_clstr_rx_socket ();

        /* Only set up the socket if an cluster-host interface is provisioned */
        mtc_sock.mtc_client_clstr_rx_socket = new msgClassRx(ctrl.address_clstr.c_str(),mtc_sock.mtc_clstr_cmd_port, IPPROTO_UDP, ctrl.clstr_iface.data(), false );

        /* update health of socket */
        if ( mtc_sock.mtc_client_clstr_rx_socket )
        {
            /* look for fault insertion request */
            if ( daemon_is_file_present ( MTC_CMD_FIT__CLSTR_RXSOCK ) )
                mtc_sock.mtc_client_clstr_rx_socket->return_status = FAIL ;

            if ( mtc_sock.mtc_client_clstr_rx_socket->return_status  == PASS )
            {
                mtc_sock.mtc_client_clstr_rx_socket->sock_ok (true);
            }
            else
            {
                elog ("failed to init 'cluster-host rx' socket (rc:%d)\n",
                mtc_sock.mtc_client_clstr_rx_socket->return_status );
                mtc_sock.mtc_client_clstr_rx_socket->sock_ok (false);
            }
        }
    }
}

void setup_mgmt_tx_socket ( void )
{
    ilog ("setup of Mgmt network transmit socket");
    _close_mgmt_tx_socket ();
    mtc_sock.mtc_client_mgmt_tx_socket = new msgClassTx(CONTROLLER,mtc_sock.mtc_agent_port, IPPROTO_UDP, ctrl.mgmnt_iface.data());

    if ( mtc_sock.mtc_client_mgmt_tx_socket )
    {
        /* look for fault insertion request */
        if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_TXSOCK ) )
            mtc_sock.mtc_client_mgmt_tx_socket->return_status = FAIL ;

        if ( mtc_sock.mtc_client_mgmt_tx_socket->return_status == PASS )
        {
            mtc_sock.mtc_client_mgmt_tx_socket->sock_ok(true);
        }
        else
        {
            elog ("failed to init 'management tx' socket (rc:%d)\n",
            mtc_sock.mtc_client_mgmt_tx_socket->return_status );
            mtc_sock.mtc_client_mgmt_tx_socket->sock_ok(false);
        }
    }
}

// Send mtcAlive messages to the controllers
void setup_pxeboot_tx_socket ( void )
{
    if ( !ctrl.pxeboot_iface_provisioned ) return ;
    ilog ("setup of pxeboot transmit socket");
    _close_pxeboot_tx_socket ();
    if ((mtc_sock.pxeboot_tx_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        elog ("failed to setup pxeboot network transmit socket ; (%d:%m)", errno);
    }
}

void setup_clstr_tx_sockets ( void )
{
    if ( !ctrl.clstr_iface_provisioned ) return ;
    ilog ("setup of %s transmit sockets", CONTROLLER_0_CLUSTER_HOST);

    _close_clstr_tx_sockets ();

    mtc_sock.mtc_client_clstr_tx_socket_c0 =
        new msgClassTx(CONTROLLER_0_CLUSTER_HOST,
                       mtc_sock.mtc_agent_port,
                       IPPROTO_UDP,
                       mtc_config.clstr_iface);

    if ( mtc_sock.mtc_client_clstr_tx_socket_c0 )
    {
        if ( mtc_sock.mtc_client_clstr_tx_socket_c0->return_status == PASS )
        {
            mtc_sock.mtc_client_clstr_tx_socket_c0->sock_ok(true);
        }
        else
        {
            elog ("failed to init '%s' tx socket (rc:%d)\n",
            CONTROLLER_0_CLUSTER_HOST,
            mtc_sock.mtc_client_clstr_tx_socket_c0->return_status );
            mtc_sock.mtc_client_clstr_tx_socket_c0->sock_ok(false);
        }
    }
    if ( ctrl.system_type != SYSTEM_TYPE__AIO__SIMPLEX )
    {
        dlog ("setup of %s TX\n", CONTROLLER_1_CLUSTER_HOST);

        mtc_sock.mtc_client_clstr_tx_socket_c1 =
            new msgClassTx(CONTROLLER_1_CLUSTER_HOST,
                           mtc_sock.mtc_agent_port,
                           IPPROTO_UDP,
                           mtc_config.clstr_iface);

        if ( mtc_sock.mtc_client_clstr_tx_socket_c1 )
        {
            if ( mtc_sock.mtc_client_clstr_tx_socket_c1->return_status == PASS )
            {
                mtc_sock.mtc_client_clstr_tx_socket_c1->sock_ok(true);
            }
            else
            {
                elog ("failed to init '%s' tx socket (rc:%d)\n",
                CONTROLLER_0_CLUSTER_HOST,
                mtc_sock.mtc_client_clstr_tx_socket_c1->return_status );
                mtc_sock.mtc_client_clstr_tx_socket_c1->sock_ok(false);
            }
        }
    }
}


void setup_amon_socket ( void )
{
    ilog ("setup of active monitoring socket");
    char filename [MAX_FILENAME_LEN] ;
    string port_string ;

    snprintf ( filename , MAX_FILENAME_LEN, "%s/%s.conf", PMON_CONF_FILE_DIR, program_invocation_short_name ) ;

    if ( ini_get_config_value ( filename, "process", "port", port_string , false ) != PASS )
    {
        elog ("failed to get active monitor port from %s\n", filename );
        mtc_sock.amon_socket = 0 ;
        return ;
    }

    mtc_sock.amon_socket =
    active_monitor_initialize ( program_invocation_short_name, atoi(port_string.data()));
    if ( mtc_sock.amon_socket )
    {
        int  val = 1;

        /* Make the active monitor socket non-blocking */
        if ( 0 > ioctl(mtc_sock.amon_socket, FIONBIO, (char *)&val) )
        {
            elog ("Failed to set amon socket non-blocking");
            close (mtc_sock.amon_socket);
        }
        else
        {
            ilog ("Active Monitor Socket %d\n", mtc_sock.amon_socket );
            return ;
        }
    }
    mtc_sock.amon_socket = 0 ;
}

/******************************************************************
 *
 * Construct the messaging sockets
 *
 * 1. Unicast UDP Mgmt network RX socket     - mtc_client_mgmt_rx_socket    (msgClass)
 * 2. Unicast UDP Clstr network RX socket    - mtc_client_clstr_rx_socket   (msgClass)
 * 3. Unicast UDP Pxeboot network RX socket  - mtc_clinet_pxeboot_rx_socket (raw)
 *
 * 4. Unicast UDP Mgmt network TX socket    - mtc_client_mgmt_tx_socket     (msgClass)
 * 5. Unicast UDP Clstr network TX socket   - mtc_client_clstr_tx_socket_c? (msgClass)
 * 6. Unicast UDP Pxeboot network TX socket - mtc_clinet_pxeboot_tx_socket  (raw)
 *
 * 7. Unicase UDP lo network active monitor  - amon_socket                  (raw)
 *
 *******************************************************************/
int mtc_socket_init ( void )
{
    /* Setup the Management Interface Recieve Socket */
    /* Read the port config strings into the socket struct */
    mtc_sock.mtc_agent_port        = mtc_config.mtc_agent_port;
    mtc_sock.mtc_mgmnt_cmd_port    = mtc_config.mtc_rx_mgmnt_port;
    mtc_sock.mtc_clstr_cmd_port    = mtc_config.mtc_rx_clstr_port;

    get_hostname ( &ctrl.hostname[0], MAX_HOST_NAME_SIZE );
    ctrl.mtcAgent_ip = getipbyname ( CONTROLLER );
    ilog ("Controller  : %s\n", ctrl.mtcAgent_ip.c_str());

    /************************************************************/
    /* Setup Mgmnt Network messaging sockets to/from mtcAgent   */
    /************************************************************/
    setup_mgmt_rx_socket ();
    setup_mgmt_tx_socket ();

    /************************************************************/
    /* Setup Pxeboot Network messaging sockets to/from mtcAgent */
    /************************************************************/
    if ( ctrl.pxeboot_iface_provisioned )
    {
        setup_pxeboot_rx_socket ();
        setup_pxeboot_tx_socket ();
    }

    /* Manage Cluster-host network setup */
    string mgmnt_iface_name = daemon_mgmnt_iface();
    string clstr_iface_name = daemon_clstr_iface();
    if ( !clstr_iface_name.empty() )
    {
        if ( clstr_iface_name != mgmnt_iface_name )
        {
            ctrl.clstr_iface_provisioned = true ;
            /************************************************************/
            /* Setup the Clstr Interface Receive Socket                 */
            /************************************************************/
            setup_clstr_rx_socket () ;

            /*************************************************************/
            /* Setup the Clstr Interface Transmit Messaging to mtcAgent  */
            /*************************************************************/
            setup_clstr_tx_sockets () ;
        }
    }

    /*************************************************************/
    /* Setup and Open the active monitoring socket               */
    /*************************************************************/
    setup_amon_socket ();

    return (PASS);
}

/****************************************************************************************
 *
 * Build up an 'identity' string to be included in the periodic mtcAlive message.
 *
 * hostname
 * personality
 * mac address
 * mgmnt ip address
 * clstr ip address
 *
 ***************************************************************************************/
string _self_identify ( string nodetype )
{
    string hostname = &ctrl.hostname[0];

    /* Build up the identity string for return to caller */
    ctrl.who_i_am = "{\"hostname\":\"";
    ctrl.who_i_am.append( hostname.data() );
    ctrl.who_i_am.append( "\"");

    ctrl.who_i_am.append(",\"personality\":\"");
    ctrl.who_i_am.append( nodetype.data() );
    ctrl.who_i_am.append( "\"");

    ctrl.who_i_am.append( ",\"pxeboot_ip\":\"");
    ctrl.who_i_am.append( ctrl.pxeboot_addr.data() );
    ctrl.who_i_am.append( "\"");

    ctrl.who_i_am.append( ",\"mgmt_ip\":\"");
    ctrl.who_i_am.append( ctrl.address.data() );
    ctrl.who_i_am.append( "\"");

    ctrl.who_i_am.append( ",\"cluster_host_ip\":\"");
    ctrl.who_i_am.append( ctrl.address_clstr.data() );
    ctrl.who_i_am.append( "\"");

    ctrl.who_i_am.append( ",\"mgmt_mac\":\"");
    ctrl.who_i_am.append( ctrl.macaddr.data() );
    ctrl.who_i_am.append( "\"");

    ilog ("Identity    : %s\n", ctrl.who_i_am.c_str() );
    return ( ctrl.who_i_am );
}

/* Init a specific script 'execution' struct */
void script_exec_init ( script_exec_type * script_exec_ptr )
{
    if ( script_exec_ptr )
    {
        script_exec_ptr->pid    = 0     ;
        script_exec_ptr->done   = false ;
        script_exec_ptr->status = -1    ;
        script_exec_ptr->name.clear()   ;
    }
}

/* Init a specific script 'control' struct */
void script_ctrl_init ( script_ctrl_type * script_ctrl_ptr )
{
    if ( script_ctrl_ptr )
    {
        script_ctrl_ptr->posted       = MTC_CMD_NONE ;
        script_ctrl_ptr->monitor      = MTC_CMD_NONE ;
        script_ctrl_ptr->scripts      = 0 ;
        script_ctrl_ptr->scripts_done = 0 ;
    }
}

/****************************************************************************
 *
 * Name       : _scripts_cleanup
 *
 * Description: For the specified script group ...
 *
 *              Kill off any scripts that are still running and
 *              clear active flag.
 *
 ****************************************************************************/
void _scripts_cleanup ( script_set_enum script_set )
{
    script_ctrl_type * script_ptr ;
    switch ( script_set )
    {
        case GOENABLED_MAIN_SCRIPTS:
        case GOENABLED_SUBF_SCRIPTS:
            script_ptr = &ctrl.goenabled ;
            break ;
        case HOSTSERVICES_SCRIPTS:
            script_ptr = &ctrl.hostservices ;
            break ;
        default:
            slog ("invalid script set (%d)\n", script_set );
            return ;
    }

    mtcTimer_reset ( script_ptr->timer );

    /* loop over looking to see if all the scripts are done */
    for ( int i = 0 ; i < script_ptr->scripts ; i++ )
    {
        if (( script_ptr->script[i].pid ) && ( script_ptr->script[i].done == false ))
        {
            int result = kill ( script_ptr->script[i].pid, 0 );
            if ( result == 0 )
            {
                result = kill ( script_ptr->script[i].pid, SIGKILL );
                if ( result == 0 )
                {
                    wlog ("kill of %s with pid %d succeeded\n", script_ptr->script[i].name.c_str(), script_ptr->script[i].pid );
                }
                else
                {
                    elog ("kill of %s with pid %d failed\n", script_ptr->script[i].name.c_str(), script_ptr->script[i].pid );
                }
            }
        }
        script_exec_init ( &script_ptr->script[i] );
    }

    script_ctrl_init ( script_ptr );
    ctrl.active_script_set = NO_SCRIPTS ;
}

/****************************************************************************
 *
 * Name       : _manage_services_scripts
 *
 * Description: Looks for 3 conditions.
 *
 *              1. done    - all scripts executed - PASS or FAIL_xxxxxx
 *              2. timeout - scripts took too long to complete - FAIL_TIMEOUT
 *              3. empty   - no scripts to run or manage - auto PASS
 *
 *              When done, timeout or empty sends appropriate result
 *              to mtcAgent.
 *
 ***************************************************************************/
void _manage_services_scripts ( void )
{
    bool failed = false ;
    char str [BUF_SIZE] ;

    if ( ! ctrl.hostservices.scripts )
    {
        /* send a PASS result */
        mtce_send_event ( sock_ptr, MTC_CMD_HOST_SVCS_RESULT, NULL );
        return ;
    }

    memset (str,0,BUF_SIZE);

    /* do if all the scripts are done ? */
    if ( ctrl.hostservices.scripts_done == ctrl.hostservices.scripts )
    {
        /* loop over looking to see if all the scripts are done */
        for ( int i = 0 ; i < ctrl.hostservices.scripts ; i++ )
        {
            if ( ctrl.hostservices.script[i].status )
            {
                if ( failed == false )
                {
                    /* only report of the first failure */
                    snprintf(str, BUF_SIZE, "%s failed ; rc:%d",
                                  ctrl.hostservices.script[i].name.data(),
                                  ctrl.hostservices.script[i].status );
                    failed = true ;
                }
            }
        }
        /* handle the aggrigate status */
        if ( failed == true )
        {
            elog ("Host Services: %s\n", str );
            mtce_send_event ( sock_ptr, MTC_CMD_HOST_SVCS_RESULT, str );
        }
        else
        {
            ilog ("Host Services Complete ; all passed\n");
            mtce_send_event ( sock_ptr, MTC_CMD_HOST_SVCS_RESULT, NULL );
        }
    }

    /* do if have we timed out ? */
    else if ( ctrl.hostservices.timer.ring == true )
    {
        bool found = false ;
        if ( ctrl.hostservices.posted || ctrl.hostservices.monitor )
        {
            snprintf(str, BUF_SIZE, "unknown test (timeout)");
            for ( int i = 0 ; i < ctrl.hostservices.scripts ; i++ )
            {
                if ( ctrl.hostservices.script[i].done == false )
                {
                    snprintf(str, BUF_SIZE, "%s (timeout)", ctrl.hostservices.script[i].name.data() );
                    found = true ;
                    wlog ("host services timeout on %s\n", ctrl.hostservices.script[i].name.c_str());
                    mtce_send_event ( sock_ptr, MTC_CMD_HOST_SVCS_RESULT, str );
                    break ;
                }
            }
        }

        if ( found == false )
        {
            slog ("unexpected host services timer ring (cmd:%x)", ctrl.hostservices.posted );
        }
    }
    else
    {
        return ;
    }

    _scripts_cleanup (ctrl.active_script_set) ;
}

/****************************************************************************
 *
 * Name       : _manage_goenabled_tests
 *
 * Description: Looks for 3 conditions.
 *
 *              1. done    - all scripts executed - PASS or FAIL_xxxxxx
 *              2. timeout - scripts took too long to complete - FAIL_TIMEOUT
 *              3. empty   - no scripts to run or manage - auto PASS
 *
 *              When done, timeout or empty sends appropriate result
 *              to mtcAgent.
 *
 ***************************************************************************/

void _manage_goenabled_tests ( void )
{
    bool failed = false ;
    char str [BUF_SIZE] ;
    memset (str,0,BUF_SIZE);

    if ( ! ctrl.goenabled.scripts )
    {
        switch ( ctrl.active_script_set )
        {
            case GOENABLED_SUBF_SCRIPTS:
            {
                time ( &ctrl.goenabled_subf_time );
                daemon_remove_file ( GOENABLED_SUBF_FAIL );

                ilog ("GoEnabled Subfunction Testing Complete ; no tests to run\n");
                daemon_log ( GOENABLED_SUBF_PASS , str );
                send_mtc_msg ( sock_ptr, MTC_MSG_SUBF_GOENABLED, "" );
                break ;
            }
            case GOENABLED_MAIN_SCRIPTS:
            {
                time ( &ctrl.goenabled_main_time );
                daemon_remove_file ( GOENABLED_MAIN_FAIL );

                ilog ("GoEnabled Testing Complete ; no tests to run\n");
                daemon_log ( GOENABLED_MAIN_PASS , str );
                send_mtc_msg ( sock_ptr, MTC_MSG_MAIN_GOENABLED, "");
                break ;
            }
            default:
                slog ("called with invalid active script set (%d)\n",
                       ctrl.active_script_set );
        }
        return ;
    }

    if ( ctrl.goenabled.scripts_done == ctrl.goenabled.scripts )
    {
        /* loop over looking to see if all the scripts are done */
        for ( int i = 0 ; i < ctrl.goenabled.scripts ; i++ )
        {
            if ( ctrl.goenabled.script[i].status )
            {
                if ( failed == false )
                {
                    snprintf(str, BUF_SIZE, "%s (rc:%d)",
                                  ctrl.goenabled.script[i].name.data(),
                                  ctrl.goenabled.script[i].status );
                    failed = true ;
                }
                wlog ("goenabled test %s FAILED with exit status :%d:%x\n",
                         ctrl.goenabled.script[i].name.c_str(),
                         ctrl.goenabled.script[i].status,
                         ctrl.goenabled.script[i].status);
            }
        }
        if ( failed == true )
        {
            switch ( ctrl.active_script_set )
            {
                case GOENABLED_SUBF_SCRIPTS:
                {
                    time ( &ctrl.goenabled_subf_time );
                    daemon_remove_file ( GOENABLED_SUBF_PASS );

                    ilog ("GoEnabled Subfunction Testing Failed ; at least one test failed\n");
                    daemon_log ( GOENABLED_SUBF_FAIL , str );
                    send_mtc_msg ( sock_ptr, MTC_MSG_SUBF_GOENABLED_FAILED, str );
                    break ;
                }
                case GOENABLED_MAIN_SCRIPTS:
                {
                    time ( &ctrl.goenabled_main_time );
                    daemon_remove_file ( GOENABLED_MAIN_PASS );

                    ilog ("GoEnabled Testing Failed ; at least one test failed\n");
                    daemon_log ( GOENABLED_MAIN_FAIL , str );
                    send_mtc_msg ( sock_ptr, MTC_MSG_MAIN_GOENABLED_FAILED, str );
                    break ;
                }
                default:
                    slog ("called with invalid active script set (%d)\n",
                           ctrl.active_script_set );
            }
        }
        else
        {
            switch ( ctrl.active_script_set )
            {
                case GOENABLED_SUBF_SCRIPTS:
                {
                    time ( &ctrl.goenabled_subf_time );

                    ilog ("GoEnabled Subfunction Testing Complete ; all tests passed\n");
                    daemon_log ( GOENABLED_SUBF_PASS , str );
                    send_mtc_msg ( sock_ptr, MTC_MSG_SUBF_GOENABLED, "" );
                    break ;
                }
                case GOENABLED_MAIN_SCRIPTS:
                {
                    time ( &ctrl.goenabled_main_time );

                    ilog ("GoEnabled Testing Complete ; all tests passed\n");
                    daemon_log ( GOENABLED_MAIN_PASS , str );
                    send_mtc_msg ( sock_ptr, MTC_MSG_MAIN_GOENABLED, "");
                    break ;
                }
                default:
                    slog ("called with invalid active script set (%d)\n",
                           ctrl.active_script_set );
            }
            /* fall through to cleanup */
        }
        mtcTimer_reset ( ctrl.goenabled.timer );
    }

    else if ( ctrl.goenabled.timer.ring == true )
    {
        bool found = false ;
        snprintf(str, BUF_SIZE, "unknown test (timeout)");
        for ( int i = 0 ; i < ctrl.goenabled.scripts ; i++ )
        {
            if ( ctrl.goenabled.script[i].done == false )
            {
                snprintf(str, BUF_SIZE, "%s (timeout)", ctrl.goenabled.script[i].name.data() );
                found = true ;
                wlog ("goenabled test timeout - %s\n", ctrl.goenabled.script[i].name.c_str());
                break ;
            }
        }
        if ( found == false )
        {
            slog ("unexpected goenabled timer ring (%x)", ctrl.goenabled.posted );
        }

        switch ( ctrl.active_script_set )
        {
            case GOENABLED_SUBF_SCRIPTS:
            {
                daemon_remove_file ( GOENABLED_SUBF_PASS );
                send_mtc_msg ( sock_ptr, MTC_MSG_SUBF_GOENABLED_FAILED, str );
                daemon_log ( GOENABLED_SUBF_FAIL , str );
                break ;
            }
            case GOENABLED_MAIN_SCRIPTS:
            {
                daemon_remove_file ( GOENABLED_SUBF_PASS );
                send_mtc_msg ( sock_ptr, MTC_MSG_MAIN_GOENABLED_FAILED, str );
                daemon_log ( GOENABLED_MAIN_FAIL , str );
                break ;
            }
            default:
                slog ("called with invalid active script set (%d)\n",
                       ctrl.active_script_set );
        }
    }
    else
    {
        return ;
    }
    _scripts_cleanup (ctrl.active_script_set) ;
}

int issue_reset_and_cleanup ( void )
{
    int rc = FAIL ;
    const char peer_ctrlr [] = "Peer controller reset" ;

    ilog("SM %s request", peer_ctrlr );
    /* check creds */
    if (( hostUtil_is_valid_ip_addr  ( peer_controller.bm_ip ) == false ) ||
        ( hostUtil_is_valid_username ( peer_controller.bm_un ) == false ) ||
        ( hostUtil_is_valid_pw       ( peer_controller.bm_pw ) == false ))
    {
        elog("%s cannot reset peer BMC host at %s due to invalid credentials",
                 ctrl.hostname, peer_controller.bm_ip.c_str());
        return (rc);
    }

    /* create output filename - no need to delete after operation */
    string output_filename = bmcUtil_create_data_fn ( ctrl.hostname,
                             BMC_RESET_CMD_FILE_SUFFIX,
                             BMC_PROTOCOL__IPMITOOL );
    if ( output_filename.empty() )
    {
        elog("%s ; failed to create output filename", peer_ctrlr);
        rc = FAIL_STRING_EMPTY ;
    }
    else if ( ipmiUtil_reset_host_now ( ctrl.hostname,
                                        peer_controller,
                                        output_filename ) == PASS )
    {
        string result = daemon_get_file_str ( output_filename.data() );
        ilog("%s succeeded", peer_ctrlr);

        /* don't fail the operation if the result is unexpected ; but log it */
        if ( result.compare( IPMITOOL_POWER_RESET_RESP ) )
        {
            dlog("... but reset command output was unexpected ; %s",
                      result.c_str());
        }
        rc = PASS ;
    }
    else
    {
        elog("%s failed", peer_ctrlr);
        rc = FAIL_OPERATION ;
    }

    if ( rc == PASS )
    {
        /* give the host a chance to reset before
         * telling SM the reset is done */
        sleep (2) ;

        /* Don't want to remove the file if the reset was not successful */
        dlog("removing %s", RESET_PEER_NOW );
        daemon_remove_file ( RESET_PEER_NOW );
    }
    return (rc);
}

/*****************************************************************************
 * Name       : learn_my_pxeboot_address
 *
 * Purpose    :  Learn my pxeboot ip address.
 *
 * Description:
 *
 * worker and storage nodes' learn their DHCP pxeboot ip from a
 * local /var/lib/dhcp/<interface> file.
 *
 * controllers learn their STATIC pxeboot address based on
 * their mac address from the dnsmasq.hosts file.
 *
 * However, the pxeboot address for a system node installed
 * controller, before it is unlocked, is DHCP'ed from
 * /etc/network/interfaces.d/ifcfg-pxeboot created by the
 * kickstart. So until the controller is unlocked its pxeboot
 * address must be learned like the worker and storage nodes.
 * That being from the local dhcp file.
 *
 * Note: In cases where the pxeboot interface name is the same as the
 *       management interface name then the ifcfg file for the pxeboot
 *       interface is suffixed with ":2" so that ifupdown can handle
 *       each interface independently during networking.service start.
 *       This is true for ethernet type interfaces as well as the
 *       bond interface when there are no vlans. In these cases the
 *       pxeboot and management interface names are the same and need
 *       distinction.
 *
 * Parameters : None
 *
 * Returns    : PASS or failed return from get_iface_info
 *
 *****************************************************************************/
int learn_my_pxeboot_address ( void )
{
    int rc = PASS ;
    if ( ctrl.pxeboot_iface_provisioned == false ) return rc ;

    if ( (rc = get_iface_info ( PXEBOOT_INTERFACE, ctrl.pxeboot_iface, ctrl.iface_info[PXEBOOT_INTERFACE] )) == PASS )
    {
        string ifcfg_file_suffix = ":2" ; // Assume ifcfg file suffix ':2' for first boot after install case
        iface_info_type * iface_info_ptr = &ctrl.iface_info[PXEBOOT_INTERFACE] ;
        iface_info_ptr->iface_name = ctrl.pxeboot_iface ;

        ilog ("...        Type: %s", get_iface_type_str(iface_info_ptr->iface_type));
        ilog ("...      Parent: %s", iface_info_ptr->parent.empty() ? "none" : iface_info_ptr->parent.c_str());
        if ( iface_info_ptr->iface_type == bond )
        {
            ilog ("... Bond Slaves: %s and %s",
                   iface_info_ptr->slave1.empty() ? "none" : iface_info_ptr->slave1.c_str(),
                   iface_info_ptr->slave2.empty() ? "none" : iface_info_ptr->slave2.c_str());
            ilog ("...   Bond Mode: %s",
                   iface_info_ptr->bond_mode.empty() ? "unknown" : iface_info_ptr->bond_mode.c_str());
        }
        ilog ("Pxeboot IF Name: %s", iface_info_ptr->parent.c_str());

        // To handle the first reboot after install where the kickstart adds a ':2'
        // to the boot interface we always try the dhcp search with the ':2' first.
        ctrl.pxeboot_addr = get_pxeboot_dhcp_addr (  iface_info_ptr->parent + ifcfg_file_suffix);
        if ( !ctrl.pxeboot_addr.empty() )
        {
            ilog ("pxeboot dhcp lease address: %s ; initial", ctrl.pxeboot_addr.c_str());
        }
        // If the pxeboot address is not found above then do the full search.
        else
        {
            // If the pxeboot interface is not same as the management interface
            // name then we need to remove the ":2" suffix.
            // The ':2' is something the kickstart and the networking management
            // adds to the interface name to distinguish between mgmt and pxeboot
            // interfaces when they are the same.
            if ( iface_info_ptr->parent != std::string(ctrl.mgmnt_iface))
                ifcfg_file_suffix = "" ;

            ctrl.pxeboot_addr = get_pxeboot_dhcp_addr ( iface_info_ptr->parent + ifcfg_file_suffix);
            if ( !ctrl.pxeboot_addr.empty() )
            {
                ilog ("pxeboot dhcp lease address: %s", ctrl.pxeboot_addr.c_str());
            }
            // Now, override that local address if its found in the controller leases file.
            if ( ctrl.nodetype & CONTROLLER_TYPE )
            {
                string temp_pxeboot_addr= get_pxeboot_static_addr ( iface_info_ptr->parent + ifcfg_file_suffix );
                if ( !temp_pxeboot_addr.empty() )
                {
                    ctrl.pxeboot_addr = temp_pxeboot_addr ;
                    ilog ("pxeboot static address: %s", ctrl.pxeboot_addr.c_str());
                }
            }
        }
        if ( ctrl.pxeboot_addr.empty() )
        {
            elog ("failed to get pxeboot address");
        }
        else
        {
            ilog ("Pxeboot IP: %s", ctrl.pxeboot_addr.c_str());
        }
    }
    else
    {
        elog ("failed to get interface info ; rc:%d", rc);
    }
    return (rc);
}

/* The main service loop */
int daemon_init ( string iface, string nodetype_str )
{
    int rc = PASS ;

    ctrl.nodetype_str = nodetype_str ;

    ctrl.who_i_am = "" ;
    ctrl.macaddr = "" ;
    ctrl.address = "" ;
    ctrl.address_clstr = "" ;
    ctrl.mtcAgent_ip = "";
    ctrl.function    = 0 ;
    ctrl.subfunction = 0 ;
    ctrl.system_type = daemon_system_type ();
    ctrl.clstr_iface_provisioned = false ;
    ctrl.pxeboot_iface_provisioned = false ;
    ctrl.peer_ctrlr_reset.sync = false ;
    ctrl.pxeboot_addr_c0 = "" ;
    ctrl.pxeboot_addr_c1 = "" ;
    ctrl.pxeboot_addr_active_controller = "" ;

    /* convert node type to integer */
    ctrl.nodetype = get_host_function_mask ( nodetype_str ) ;
    ilog ("Node Type   : %s (%d:%x)\n", nodetype_str.c_str(), ctrl.nodetype, ctrl.nodetype);

    /* Initialize socket construct and pointer to it */
    memset ( &mtc_sock,   0, sizeof(mtc_sock));
    sock_ptr = &mtc_sock ;

    /* Assign interface to config */
    ctrl.mgmnt_iface = iface ;

    // Condition gates for pxeboot network provisioning.
    // The pxeboot network is only provisioned while management is not on 'lo'
    if ( iface != LOOPBACK_IF )
    {
        // ... and while this is not the first unconfigured controller.
        if (( daemon_is_file_present ( FIRST_CONTROLLER_FILE ) == true ) &&
            ( daemon_is_file_present ( INIT_CONFIG_COMPLETE ) == false ))
        {
            // This check prevents trying to setup the pxeboot
            // network on the oam interface immediately following
            // initial controller-0 network install.
            // All other cases get a provisioned pxeboot network.
            dlog ("pxeboot network not provisionable yet");
        }
        else
        {
            ilog ("Mgmnt iface : %s", ctrl.mgmnt_iface.c_str());

            // Not on LO, assume pxeboot provisioning starting with it being
            // equal to the management interface, until otherwise updated due
            // to bonding or vlan modes.
            ctrl.pxeboot_iface = ctrl.mgmnt_iface ;
            if ( ctrl.system_type != SYSTEM_TYPE__AIO__SIMPLEX )
            {
                ctrl.pxeboot_iface_provisioned = true ;
            }
            else
            {
                ilog ("Simplex Mode: Pxeboot network not provisioned");
            }
        }
    }
    if ( daemon_files_init () != PASS )
    {
        printf ("Pid, log or other files could not be opened\n");
        rc = FAIL_FILES_INIT ;
    }

    /* Bind signal handlers */
    else if ( daemon_signal_init () != PASS )
    {
       elog ("daemon_signal_init failed\n");
       rc = FAIL_SIGNAL_INIT ;
    }

    /* Configure the worker */
    else if ( (rc = daemon_configure ()) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%i)\n", rc );
        return (FAIL_DAEMON_CONFIG) ;
    }

    else if ( set_host_functions ( nodetype_str, &ctrl.nodetype,
                                                 &ctrl.function,
                                                 &ctrl.subfunction ) != PASS )
    {
        elog ("failed to extract nodetype info\n");
        rc = FAIL_NODETYPE;
    }

    if ( ctrl.system_type != SYSTEM_TYPE__AIO__SIMPLEX )
    {
        if (( rc = learn_my_pxeboot_address () ) != PASS )
        {
            wlog ("failed to learn my pxeboot address ; rc:%d", rc );
        }
    }

    /* Setup the heartbeat service messaging sockets */
    if ( (rc = mtc_socket_init ()) != PASS )
    {
        elog ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT ;
    }

    /* Get my hostname and ip address */
    /* Should not return from this call without an IP address */
    string who_i_am = _self_identify ( ctrl.nodetype_str );

    mtcTimer_init ( ctrl.timer,     &ctrl.hostname[0] , "mtc timer" );
    mtcTimer_init ( ctrl.goenabled.timer, &ctrl.hostname[0], "goenable timer" );
    mtcTimer_init ( ctrl.hostservices.timer, &ctrl.hostname[0], "host services timer" );

    /* initialize peer controller reset feature */
    mtcTimer_init ( ctrl.peer_ctrlr_reset.audit_timer, &ctrl.hostname[0], "peer ctrlr reset audit timer" ),
    mtcTimer_init ( ctrl.peer_ctrlr_reset.sync_timer, &ctrl.hostname[0], "peer ctrlr reset sync timer" ),
    ctrl.peer_ctrlr_reset.sync_timer.ring = false ;
    ctrl.peer_ctrlr_reset.audit_timer.ring = false ;
    ctrl.peer_ctrlr_reset.audit_period = PEER_CTRLR_AUDIT_PERIOD ;

    /* initialize the script group control structures */
    script_ctrl_init ( &ctrl.goenabled    );
    script_ctrl_init ( &ctrl.hostservices );
    for ( int i = 0 ; i < MAX_RUN_SCRIPTS ; i++ )
    {
        script_exec_init ( &ctrl.goenabled.script[i] );
        script_exec_init ( &ctrl.hostservices.script[i] );
    }

    ctrl.active_script_set   = NO_SCRIPTS   ;

    /* default genabled time struct */
    time (&ctrl.goenabled_main_time);
    time (&ctrl.goenabled_subf_time);

    /* Clear and then populate the script sets need to run on daemon startup */
    ctrl.posted_script_set.clear();

    /* Only automatically run the main goenabled tests on process start-up
     * if they have not already been run. This then handles mtcClient
     * restart in the no-reboot patching case */
    if ( daemon_is_file_present ( GOENABLED_MAIN_PASS ) == false )
    {
        ilog ("posting main-function goenable tests");
        ctrl.posted_script_set.push_front(GOENABLED_MAIN_SCRIPTS);
    }

    if (( ctrl.nodetype     & CONTROLLER_TYPE ) &&
        ( ctrl.system_type != SYSTEM_TYPE__NORMAL ) &&
        ( daemon_is_file_present ( GOENABLED_SUBF_PASS ) == false ))
    {
        ilog ("posting  sub-function goenable tests");
        ctrl.posted_script_set.push_back(GOENABLED_SUBF_SCRIPTS);
    }
    return (rc) ;
}

int select_log_count = 0 ;
void daemon_service_run ( void )
{
    int rc = PASS ;
    int file_not_present_count = 0 ;

    /* Bool to track whether the start host services scripts run has
     * been attempted at least once since last process startup. */
    bool start_host_services_needs_to_be_run = true ;

    if ( daemon_is_file_present ( NODE_RESET_FILE ) )
    {
        wlog ("mtce reboot required");
        fork_sysreq_reboot ( daemon_get_cfg_ptr()->failsafe_shutdown_delay );
        for ( ; ; )
        {
            wlog ("issuing reboot");
            system("/usr/bin/systemctl reboot");

            // wait up to 30 seconds before the reboot is retried.
            for ( int i = 0 ; i < 10 ; i++ )
                sleep (3) ;
        }
    }

    /* If the mtcClient starts up and finds that its persistent node
     * locked backup file is present then make sure the volatile one
     * is also present. */
    if ( daemon_is_file_present ( NODE_LOCKED_FILE_BACKUP ))
    {
        if ( daemon_is_file_present ( NODE_LOCKED_FILE ) == false )
        {
            ilog ("restoring %s from %s backup", NODE_LOCKED_FILE,
                                                 NODE_LOCKED_FILE_BACKUP);
            daemon_log ( NODE_LOCKED_FILE, ADMIN_LOCKED_STR );
        }
    }
    /* otherwise if the backup file is not there remove volatile file */
    else if ( daemon_is_file_present ( NODE_LOCKED_FILE ))
    {
        daemon_remove_file ( NODE_LOCKED_FILE );
    }

    /* Start mtcAlive message timer */
    /* Send first mtcAlive ASAP */
    mtcTimer_start ( ctrl.timer, timer_handler, 1 );

    /* Monitor for peer controller reset requests when this
     * daemon runs on a controller */
    if ( ctrl.nodetype & CONTROLLER_TYPE )
    {
        mtcTimer_start ( ctrl.peer_ctrlr_reset.audit_timer,
                         timer_handler,
                         ctrl.peer_ctrlr_reset.audit_period );
    }

    /* Send the mtcClient ready event and clear the periodic event counter */
    mtce_send_event ( sock_ptr, MTC_EVENT_MONITOR_READY, NULL );
    ctrl.ready_event_counter = 0 ;

    /* lets go select so that the sock does not go crazy */
    dlog ("%s running main loop with %d msecs socket timeout\n",
                       &ctrl.hostname[0], (SOCKET_WAIT/1000) );

    std::list<int> socks ;

    /* Run heartbeat service forever or until stop condition */
    for ( ; ; )
    {
        /* set the master fd_set */
        FD_ZERO(&mtc_sock.readfds);
        socks.clear();

        if ( mtc_sock.mtc_client_mgmt_rx_socket && mtc_sock.mtc_client_mgmt_rx_socket->return_status==PASS )
        {
            socks.push_front (mtc_sock.mtc_client_mgmt_rx_socket->getFD());
            FD_SET(mtc_sock.mtc_client_mgmt_rx_socket->getFD(), &mtc_sock.readfds);
        }

        if (( ctrl.clstr_iface_provisioned == true ) &&
            ( mtc_sock.mtc_client_clstr_rx_socket ) &&
            ( mtc_sock.mtc_client_clstr_rx_socket->return_status==PASS ))
        {
            socks.push_front (mtc_sock.mtc_client_clstr_rx_socket->getFD());
            FD_SET(mtc_sock.mtc_client_clstr_rx_socket->getFD(), &mtc_sock.readfds);
        }

        if (( ctrl.pxeboot_iface_provisioned ) &&
            ( mtc_sock.pxeboot_rx_socket ))
        {
            socks.push_front (mtc_sock.pxeboot_rx_socket);
            FD_SET(mtc_sock.pxeboot_rx_socket, &mtc_sock.readfds);
        }

        mtc_sock.amon_socket = active_monitor_get_sel_obj ();
        if ( mtc_sock.amon_socket )
        {
            socks.push_front (mtc_sock.amon_socket);
            FD_SET(mtc_sock.amon_socket,          &mtc_sock.readfds);
        }

        /* Initialize the timeval struct to wait for 50 mSec */
        mtc_sock.waitd.tv_sec  = 0;
        mtc_sock.waitd.tv_usec = SOCKET_WAIT;

        /* Call select() and wait only up to SOCKET_WAIT */
        socks.sort();
        rc = select( socks.back()+1,
                    &mtc_sock.readfds, NULL, NULL,
                    &mtc_sock.waitd);

        /* If the select time out expired then  */
        if (( rc < 0 ) || ( rc == 0 ))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                elog ("Select Failed (rc:%d) %s \n", errno, strerror(errno));
            }
        }
        else
        {

            // Is there a Pxeboot network message present ?
            if (( ctrl.pxeboot_iface_provisioned ) &&
                ( mtc_sock.pxeboot_rx_socket )     &&
                ( FD_ISSET(mtc_sock.pxeboot_rx_socket, &mtc_sock.readfds)))
            {
                mlog3 ("pxeboot rx socket fired");
                mtc_service_command ( sock_ptr, PXEBOOT_INTERFACE );
            }

            // Is there a Mgmt network message present ?
            if ((mtc_sock.mtc_client_mgmt_rx_socket &&
                 mtc_sock.mtc_client_mgmt_rx_socket->return_status==PASS) &&
                 FD_ISSET(mtc_sock.mtc_client_mgmt_rx_socket->getFD(), &mtc_sock.readfds))
            {
                mlog3 ("mgmt rx socket fired");
                mtc_service_command ( sock_ptr, MGMNT_INTERFACE );
            }

            // Is there a cluster host network message present ?
            if (( ctrl.clstr_iface_provisioned == true ) &&
                ( !ctrl.address_clstr.empty() ) &&
                ( mtc_sock.mtc_client_clstr_rx_socket ) &&
                ( mtc_sock.mtc_client_clstr_rx_socket->return_status==PASS) &&
                ( FD_ISSET(mtc_sock.mtc_client_clstr_rx_socket->getFD(), &mtc_sock.readfds)))
            {
                mlog3 ("clstr rx socket fired");
                mtc_service_command ( sock_ptr, CLSTR_INTERFACE );
            }

            // Is there a active monitor request pesent
            if ( FD_ISSET(mtc_sock.amon_socket, &mtc_sock.readfds))
            {
                mlog3 ("Active Monitor Select Fired\n");
                active_monitor_dispatch ();
            }
        }

        if (( ctrl.active_script_set == GOENABLED_MAIN_SCRIPTS ) ||
            ( ctrl.active_script_set == GOENABLED_SUBF_SCRIPTS ))
        {
            _manage_goenabled_tests ( );
        }
        else if ( ctrl.active_script_set == HOSTSERVICES_SCRIPTS )
        {
            _manage_services_scripts ( );
        }
        /* now service posted requests */
        else if ( ctrl.active_script_set == NO_SCRIPTS )
        {
            if ( ! ctrl.posted_script_set.empty() )
            {
                /* get the next script set to execute */
                ctrl.active_script_set = ctrl.posted_script_set.front();
                if ( ctrl.active_script_set == GOENABLED_MAIN_SCRIPTS )
                {
                    if (( daemon_is_file_present ( CONFIG_COMPLETE_FILE )) &&
                        ( daemon_is_file_present ( GOENABLED_MAIN_READY )))
                    {
                        ctrl.posted_script_set.pop_front();
                        if (( rc = run_goenabled_scripts ( "self-test" )) != PASS )
                        {
                            if ( rc == RETRY )
                            {
                                ilog ("main goenable testing already in progress\n");
                            }
                            else
                            {
                                elog ("main goenable test start failed (rc:%d)\n", rc );
                            }
                        }
                        else
                        {
                            ilog ("main goenable tests started\n");
                        }
                        file_not_present_count = 0 ;
                    }
                    else
                    {
                        ctrl.active_script_set = NO_SCRIPTS ;
                        ilog_throttled (file_not_present_count, 10000,
                                        "waiting on goenable gates (%s and %s)\n",
                                        CONFIG_COMPLETE_FILE,
                                        GOENABLED_MAIN_READY );
                    }
                }
                else if ( ctrl.active_script_set == GOENABLED_SUBF_SCRIPTS )
                {
                    if (( daemon_is_file_present ( CONFIG_COMPLETE_WORKER )) &&
                        ( daemon_is_file_present ( GOENABLED_SUBF_READY )))
                    {
                        ctrl.posted_script_set.pop_front();
                        if ( run_goenabled_scripts ( "self-test" ) != PASS )
                        {
                            if ( rc == RETRY )
                            {
                                ilog ("subf goenable testing already in progress\n");
                            }
                            else
                            {
                                elog ("subf goenable test start failed (rc:%d)\n", rc );
                            }
                        }
                        else
                        {
                            ilog ("subf goenable tests started\n");
                        }
                        file_not_present_count = 0 ;
                    }
                    else
                    {
                        ctrl.active_script_set = NO_SCRIPTS ;
                        ilog_throttled (file_not_present_count, 10000,
                                        "waiting on subfuction goenable gate %s\n",
                                        GOENABLED_SUBF_READY);
                    }
                }
                else if ( ctrl.active_script_set == HOSTSERVICES_SCRIPTS )
                {
                    ctrl.posted_script_set.pop_front();
                    /* Handle running the host services scripts. */
                    if ( is_host_services_cmd ( ctrl.hostservices.posted ))
                    {
                        if (( rc = run_hostservices_scripts ( ctrl.hostservices.posted )) != PASS )
                        {
                            char str[BUF_SIZE] ;
                            memset (str,0,BUF_SIZE);
                            snprintf ( str, BUF_SIZE, "%s rc:%d", "launch failed", rc );
                            elog ("%s scripts failed (rc:%d)\n", get_mtcNodeCommand_str(ctrl.hostservices.posted), rc );
                            ctrl.hostservices.posted = MTC_CMD_NONE ;

                            /* send error message */
                            mtce_send_event ( sock_ptr, MTC_CMD_HOST_SVCS_RESULT, str );
                        }
                    }
                    else
                    {
                        ctrl.hostservices.monitor = ctrl.hostservices.posted ;
                        ctrl.hostservices.posted = MTC_CMD_NONE ;
                    }
                }
                else
                {
                    slog ("invalid script set (%d)\n", ctrl.active_script_set );
                }
            }
        }
        if ( mtcTimer_expired ( ctrl.timer ) )
        {
            bool socket_reinit = true ;

            /**
             *  Look for failing sockets and try to recover them,
             *  but only one at a time if there are multiple failing.
             *  Priority is the command receiver, then transmitter,
             *  followed by the cluster-host and others.
             **/

            /* Mgmt Rx */
            if (( mtc_sock.mtc_client_mgmt_rx_socket == NULL ) ||
                ( mtc_sock.mtc_client_mgmt_rx_socket->sock_ok() == false ))
            {
                wlog ("calling setup_mgmt_rx_socket (auto-recovery)\n");
                setup_mgmt_rx_socket();
                socket_reinit = true ;
            }

            /* Mgmt Tx */
            else if (( mtc_sock.mtc_client_mgmt_tx_socket == NULL  ) ||
                     ( mtc_sock.mtc_client_mgmt_tx_socket->sock_ok() == false ))
            {
                wlog ("calling setup_mgmt_tx_socket (auto-recovery)");
                setup_mgmt_tx_socket();
                socket_reinit = true ;
            }

            /* Pxeboot Rx */
            else if ((ctrl.pxeboot_iface_provisioned == true) && (mtc_sock.pxeboot_rx_socket <= 0))
            {
                wlog ("calling setup_pxeboot_rx_socket (auto-recovery)");
                setup_pxeboot_rx_socket();
                socket_reinit = true ;
            }

            /* Pxeboot Tx */
            else if ((ctrl.pxeboot_iface_provisioned == true) && (mtc_sock.pxeboot_tx_socket == 0))
            {
                wlog ("calling setup_pxeboot_tx_socket (auto-recovery)");
                setup_pxeboot_tx_socket();
                socket_reinit = true ;
            }

            /* Clstr Rx */
            else if (( ctrl.clstr_iface_provisioned == true ) &&
                     (( mtc_sock.mtc_client_clstr_rx_socket == NULL ) ||
                      ( mtc_sock.mtc_client_clstr_rx_socket->sock_ok() == false )))
            {
                wlog ("calling setup_clstr_rx_socket (auto-recovery)");
                setup_clstr_rx_socket();
                socket_reinit = true ;
            }

            /* Clstr Tx ; AIO SX */
            else if ((ctrl.system_type == SYSTEM_TYPE__AIO__SIMPLEX) &&
                     ( ctrl.clstr_iface_provisioned == true ) &&
                     (( mtc_sock.mtc_client_clstr_tx_socket_c0 == NULL ) ||
                      ( mtc_sock.mtc_client_clstr_tx_socket_c0->sock_ok() == false )))
            {
                wlog ("calling setup_clstr_tx_sockets (auto-recovery)");
                setup_clstr_tx_sockets();
                socket_reinit = true ;
            }

            /* Clstr Tx ; not AIO SX */
            else if ((ctrl.system_type != SYSTEM_TYPE__AIO__SIMPLEX) &&
                     ( ctrl.clstr_iface_provisioned == true ) &&
                     (( mtc_sock.mtc_client_clstr_tx_socket_c0 == NULL ) ||
                      ( mtc_sock.mtc_client_clstr_tx_socket_c1 == NULL ) ||
                      ( mtc_sock.mtc_client_clstr_tx_socket_c0->sock_ok() == false ) ||
                      ( mtc_sock.mtc_client_clstr_tx_socket_c1->sock_ok() == false )))
            {
                wlog ("calling setup_clstr_tx_sockets (auto-recovery)");
                setup_clstr_tx_sockets();
                socket_reinit = true ;
            }

            else if ( mtc_sock.amon_socket <= 0 )
            {
                setup_amon_socket ();
                wlog ("calling setup_amon_socket (auto-recovery)");
                socket_reinit = true ;
            }
            else
            {
                socket_reinit = false ;
            }

            if ( socket_reinit )
            {
                if (( ctrl.pxeboot_iface_provisioned ) &&
                    (( mtc_sock.pxeboot_tx_socket <= 0 ) ||
                     ( mtc_sock.pxeboot_rx_socket <= 0 )))
                {
                    learn_my_pxeboot_address ();
                }
                /* re-get identity if interfaces are re-initialized */
                string who_i_am = _self_identify ( ctrl.nodetype_str );
            }
            alog1 ("sending mtcAlive on all provisioned mtcAlive networks");

#ifdef WANT_FIT_TESTING
            if ( ! daemon_want_fit ( FIT_CODE__FAIL_PXEBOOT_MTCALIVE ) )
#endif
            {
                if ( ctrl.pxeboot_iface_provisioned )
                {
                    send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, PXEBOOT_INTERFACE );
                }
            }
            send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, MGMNT_INTERFACE );
            if (( ctrl.clstr_iface_provisioned == true ) &&
                ( mtc_sock.mtc_client_clstr_rx_socket != NULL ) &&
                ( mtc_sock.mtc_client_clstr_rx_socket->sock_ok() == true ))
            {
                send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, CLSTR_INTERFACE );
            }

            /* Re-Start mtcAlive message timer */
            mtcTimer_start ( ctrl.timer, timer_handler, MTC_ALIVE_TIMER );

            dlog3 ("Clstr is %senabled", ctrl.clstr_iface_provisioned ? "" : "NOT ");

            if ( daemon_is_file_present ( MTC_CMD_FIT__DIR ) )
            {
                if ( ctrl.pxeboot_iface_provisioned )
                {
                    /* fault insertion testing */
                    if ( daemon_is_file_present ( MTC_CMD_FIT__PXEBOOT_RXSOCK ))
                        _close_pxeboot_rx_socket();
                    if ( daemon_is_file_present ( MTC_CMD_FIT__PXEBOOT_TXSOCK ))
                        _close_pxeboot_tx_socket ();
                }
                /* fault insertion testing */
                if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_RXSOCK ))
                {
                    if ( mtc_sock.mtc_client_mgmt_rx_socket )
                    {
                        mtc_sock.mtc_client_mgmt_rx_socket->sock_ok (false);
                        _close_mgmt_rx_socket();
                    }
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_TXSOCK ))
                {
                    if ( mtc_sock.mtc_client_mgmt_tx_socket )
                    {
                        mtc_sock.mtc_client_mgmt_tx_socket->sock_ok (false);
                        _close_mgmt_tx_socket ();
                    }
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__CLSTR_RXSOCK ))
                {
                    if ( mtc_sock.mtc_client_clstr_rx_socket )
                        mtc_sock.mtc_client_clstr_rx_socket->sock_ok (false);
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__CLSTR_TXSOCK ))
                {
                    if ( mtc_sock.mtc_client_clstr_tx_socket_c0 )
                        mtc_sock.mtc_client_clstr_tx_socket_c0->sock_ok (false);
                    if ( mtc_sock.mtc_client_clstr_tx_socket_c1 )
                        mtc_sock.mtc_client_clstr_tx_socket_c1->sock_ok (false);
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__AMON_SOCK ))
                {
                    _close_amon_sock ();
                }
            }

            // Purpose: mtcClient ready event audit
            //
            // Send the ready event every minute just in case the first
            // process startup event was missed by the mtcAgent or
            // the mtcAgent was restarted.
            //.
            // Needed to ensure that pxeboot mtcAlive messaging monitoring
            // gets started over a mtcagent process restart.
            if ( ++ctrl.ready_event_counter >= (MTC_MINS_1/MTC_ALIVE_TIMER) )
            {

                dlog ("sending mtcClient ready event");
                mtce_send_event ( sock_ptr, MTC_EVENT_MONITOR_READY, NULL );
                ctrl.ready_event_counter = 0 ;
            }
        }

        /* service controller specific audits */
        if ( ctrl.nodetype & CONTROLLER_TYPE )
        {
            /* peer controller reset service audit */
            if ( ctrl.peer_ctrlr_reset.audit_timer.ring )
            {
                if ( daemon_is_file_present ( RESET_PEER_NOW ) )
                {
                    if ( ctrl.peer_ctrlr_reset.sync )
                    {
                        if ( ctrl.peer_ctrlr_reset.sync_timer.ring )
                        {
                            issue_reset_and_cleanup ();
                            ctrl.peer_ctrlr_reset.sync_timer.ring = false ;
                        }
                        else if ( ctrl.peer_ctrlr_reset.sync_timer.tid == NULL )
                        {
                            if ( send_mtcClient_cmd ( &mtc_sock,
                                                       MTC_CMD_SYNC,
                                                       peer_controller.hostname,
                                                       peer_controller.host_ip,
                                                       mtc_config.mtc_rx_mgmnt_port) == PASS )
                            {
                                mtcTimer_start ( ctrl.peer_ctrlr_reset.sync_timer, timer_handler, MTC_SECS_10 );
                                ilog("... waiting for peer controller to sync - %d secs", MTC_SECS_10);
                            }
                            else
                            {
                                elog("failed to send 'sync' command to peer controller mtcClient");
                                ctrl.peer_ctrlr_reset.sync_timer.ring = true ;
                            }
                        }
                        else
                        {
                            ; /* wait longer */
                        }
                    }
                    else
                    {
                        issue_reset_and_cleanup ();
                    }
                }
                ctrl.peer_ctrlr_reset.audit_timer.ring = false ;
            }
        }

        // mtcAlive Stress Test. Send the mtcAgent a lot of messages
        #define MTCALIVE_STRESS_FILE ((const char*)"/var/run/mtcAlive_stress")
        if (( daemon_get_cfg_ptr()->testmask & TESTMASK__MSG__MTCALIVE_STRESS ) &&
            ( daemon_is_file_present ( MTCALIVE_STRESS_FILE )))
        {
            int loops = daemon_get_file_int ( MTCALIVE_STRESS_FILE );
            slog ("mtcAlive Stress Test: Sending %d mtcAlive on each network.", loops);
            for ( int loop = 0 ; loop < loops ; loop++ )
            {
                if ( ctrl.pxeboot_iface_provisioned )
                {
                    send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, PXEBOOT_INTERFACE );
                }
                send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, MGMNT_INTERFACE );
                send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, CLSTR_INTERFACE );

                // Service signal handler just in case the loaded loops number is big
                daemon_signal_hdlr ();
            }
        }

        if (( start_host_services_needs_to_be_run == true) &&
            ( ctrl.posted_script_set.size() == 0 ))
        {
            bool run_start_host_services = false ;
            dlog1 ("Start Host Services needs to be run");
            if ( ctrl.system_type == SYSTEM_TYPE__NORMAL )
            {
                /* Any node on a standard system */
                if ( daemon_is_file_present ( GOENABLED_MAIN_PASS ) )
                {
                    ilog ("start host services on standard system accepted");
                    run_start_host_services = true ;
                }
                else if ( daemon_is_file_present ( GOENABLED_MAIN_FAIL ) )
                {
                    /* Don't run start host services if any goenabled failed */
                    wlog ("start host services on standard system rejected ; goenabled failed");
                    start_host_services_needs_to_be_run = false ;
                }
            }
            else if ( ctrl.nodetype & CONTROLLER_TYPE )
            {
                /* AIO controller */
                if ( daemon_is_file_present ( GOENABLED_SUBF_PASS ) )
                {
                    ilog ("start host services on all-in-one controller accepted");
                    run_start_host_services = true ;
                }
                else if (( daemon_is_file_present ( GOENABLED_MAIN_FAIL ) ||
                         ( daemon_is_file_present ( GOENABLED_SUBF_FAIL ))))
                {
                    /* Don't run start host services if any goenabled failed */
                    wlog ("start host services on all-in-one controller rejected ; goenabled failed ");
                    start_host_services_needs_to_be_run = false ;
                }
            }
            else
            {
                /* AIO plus : worker and storage */
                if ( daemon_is_file_present ( GOENABLED_MAIN_PASS ) )
                {
                    ilog ("start host services on all-in-one plus node accepted");
                    run_start_host_services = true ;
                }
                else if ( daemon_is_file_present ( GOENABLED_MAIN_FAIL ) )
                {
                    /* Don't run start host services if any goenabled failed */
                    wlog ("start host services on all-in-one plus node rejected ; goenabled failed");
                    start_host_services_needs_to_be_run = false ;
                }
            }

            if ( run_start_host_services )
            {
                ctrl.posted_script_set.push_back ( HOSTSERVICES_SCRIPTS );

                int cmd = MTC_CMD_NONE ;
                if ( ctrl.nodetype & CONTROLLER_TYPE)
                    cmd = MTC_CMD_START_CONTROL_SVCS ;
                else if ( ctrl.nodetype & WORKER_TYPE )
                    cmd = MTC_CMD_START_WORKER_SVCS ;
                else if ( ctrl.nodetype & STORAGE_TYPE )
                    cmd = MTC_CMD_START_STORAGE_SVCS ;

                ctrl.hostservices.posted  = cmd ;
                ctrl.hostservices.monitor = MTC_CMD_NONE ;
                ilog ("posted start host services ; from process startup ; cmd:%s", get_mtcNodeCommand_str(cmd));

                start_host_services_needs_to_be_run = false ;
            }
        }
        daemon_signal_hdlr ();
    }
    daemon_exit();
}

#define MAX_ARGS 4
static char start[] = "start" ;
static char stop[] = "stop" ;
int _launch_all_scripts ( script_ctrl_type  & group,
                          std::list<string> & scripts,
                          string              label,
                          string              action,
                          string              option )
{
    int index ;
    char * argv[MAX_ARGS] ;

    if ( action == "start" )
        argv[1] = start ;
    else
        argv[1] = stop ;

    argv[2] = (char*)option.data() ;
    argv[MAX_ARGS-1] = NULL    ;

    /* initialize control struct */
    for ( int i = 0 ; i < MAX_RUN_SCRIPTS ; i++ )
    {
        group.script[i].pid    = 0 ;
        group.script[i].status = 0 ;
        group.script[i].done   = false ;
        group.script[i].name   = "" ;
    }
    group.scripts = scripts.size() ;
    group.scripts_done = 0 ;

    ilog ("Sorted %s File List: %d\n", label.c_str(), group.scripts );

    std::list<string>::iterator string_iter_ptr ;
    for ( string_iter_ptr  = scripts.begin () ;
          string_iter_ptr != scripts.end () ;
          string_iter_ptr++ )
    {
        ilog (" ... %s %s\n", string_iter_ptr->c_str(), action.c_str());
    }

    /* Run Maintenance on Inventory */
    for ( index = 0,
          string_iter_ptr  = scripts.begin () ;
          string_iter_ptr != scripts.end () ;
          string_iter_ptr++ )
    {
        group.script[index].name = *string_iter_ptr ;
        group.script[index].pid = fork();
        if ( group.script[index].pid == 0 )
        {
            bool close_file_descriptors = false ;
            if ( setup_child ( close_file_descriptors ) != PASS )
            {
                 exit(EXIT_FAILURE);
            }

            /* Set child to default signaling */
            signal (SIGCHLD, SIG_DFL);

            umask(022);

            /* Setup exec arguement */
            char script_name[MAX_FILE_SIZE];
            snprintf ( &script_name[0], MAX_FILE_SIZE, "%s", string_iter_ptr->data()) ;
            argv[0] = script_name ;
            for ( int x = 0 ; x < MAX_ARGS ; x++ )
            {
                dlog ("argv[%d] = %s\n", x , argv[x]);
            }

            openlog ( program_invocation_short_name, LOG_PID, LOG_USER );
            syslog ( LOG_INFO, "%s %s\n", string_iter_ptr->c_str(), action.c_str());
            if ( 0 > execv(argv[0], argv ))
            {
                syslog ( LOG_INFO, "%s failed (%d) (%s)\n",
                                       string_iter_ptr->c_str(),
                                       errno,
                                       strerror(errno));
            }
            closelog();
            exit(1);
        }
        gettime ( group.script[index].time_start );
        dlog ("%s %02d: %s (pid:%d) is running\n",
                  label.c_str(),
                  index,
                  string_iter_ptr->c_str(),
                  group.script[index].pid );
        index++ ;
    }
    return (PASS);
}

/***********************************************************************
 *
 * Name       : run_hostservices_scripts
 *
 * Purpose    : Call the files in /etc/services.d with start or stop
 *
 * Description: Controller maintenance requests a host's command handler
 *              to 'Start' or 'Stop' Host Services. This results in a call
 *              to this handler. All the files in /etc/services.d are
 *              read. Each is called alphabetically with the requested
 *              command option of start or stop.
 *
 *              The execution time and exit status of each script is logged.
 *              The exit status of each script is checked. If any return a
 *              non-zero value then that is an indication of that operation
 *              failed and the overall command is failed. In the failure case,
 *              which includes an overall execution timeout case, this utility
 *              returns a message to maintenance indicating the name of the
 *              script that failed and its return code.
 *
 * Params     :
 *
 *   cmd    - 'uint' representing start or stop services commands
 *
 *             MTC_CMD_STOP_CONTROL_SVCS
 *             MTC_CMD_STOP_WORKER_SVCS
 *             MTC_CMD_STOP_STORAGE_SVCS
 *             MTC_CMD_START_CONTROL_SVCS
 *             MTC_CMD_START_WORKER_SVCS
 *             MTC_CMD_START_STORAGE_SVCS
 *
 * Returns   : Operation PASS or non-zero return code with the failing
 *             script name in the message buffer.
 *
 ****************************************************************************/
int run_hostservices_scripts ( unsigned int cmd )
{
    string dir    = SERVICES_DIR ;
    string action = "" ;
    string func   = "" ;

    switch ( cmd )
    {
        case MTC_CMD_STOP_CONTROL_SVCS:
            dir.append("/controller");
            action = "stop" ;
            func = "controller";
            break ;
        case MTC_CMD_STOP_WORKER_SVCS:
            dir.append("/worker");
            action = "stop" ;
            func = "worker";
            break ;
        case MTC_CMD_STOP_STORAGE_SVCS:
            dir.append("/storage");
            action = "stop" ;
            func = "storage";
            break ;
        case MTC_CMD_START_CONTROL_SVCS:
            dir.append("/controller");
            action = "start" ;
            func = "controller";
            break ;
        case MTC_CMD_START_WORKER_SVCS:
            dir.append("/worker");
            action = "start" ;
            func = "worker";
            break ;
        case MTC_CMD_START_STORAGE_SVCS:
            dir.append("/storage");
            action = "start" ;
            func = "storage";
            break ;
        default:
            ctrl.active_script_set = NO_SCRIPTS ;
            return (FAIL_BAD_CASE);
    }

    /* list of service files */
    std::list<string> scripts ;
    if ( load_filenames_in_dir ( dir.data(), scripts ) != PASS )
    {
        elog ("failed to load host services scripts dir:%s\n", dir.c_str());
        ctrl.active_script_set = NO_SCRIPTS ;
        return (FAIL_READ_FILES) ;
    }


    /* For the stop command we need the mtcClient to run both controller and
     * worker stop services if we are on a AIO system.
     * This saves the mtcAgent from having to issue and manage 2 commands,
     * one for controller and 1 for worker */
    if ( ctrl.system_type != SYSTEM_TYPE__NORMAL )
    {
        string dir = "" ;
        if ( action == "stop" )
        {
            std::list<string> more_scripts ;
            if ( cmd == MTC_CMD_STOP_WORKER_SVCS )
            {
                /* only add the controller if we get a worker stop
                 * and this host has a controller nodetype function */
                if (ctrl.nodetype & CONTROLLER_TYPE)
                {
                    dir = SERVICES_DIR ;
                    dir.append("/controller");
                }
            }
            else if ( cmd == MTC_CMD_STOP_CONTROL_SVCS )
            {
                /* add the worker stop if we get a controller stop
                 * and this host has a worker nodetype function */
                if (ctrl.nodetype & WORKER_TYPE)
                {
                    dir = SERVICES_DIR ;
                    dir.append("/worker");
                }
            }

            if ( ! dir.empty() )
            {
                if ( load_filenames_in_dir ( dir.data(), more_scripts ) != PASS )
                {
                    ctrl.active_script_set = NO_SCRIPTS ;
                    return (FAIL_READ_FILES) ;
                }

                if ( ! more_scripts.empty() )
                {
                    scripts.merge(more_scripts);
                }
            }
        }
    }

    if ( scripts.empty() )
    {
        ilog ("no service scripts\n");
        ctrl.hostservices.scripts = 0 ;
        _manage_services_scripts ();
        ctrl.active_script_set = NO_SCRIPTS ;
        return (PASS);
    }

    scripts.sort();
    mtcTimer_reset ( ctrl.hostservices.timer );
    mtcTimer_start ( ctrl.hostservices.timer, timer_handler, mtc_config.host_services_timeout );

    /* launch the scripts */
    return (_launch_all_scripts ( ctrl.hostservices, scripts, "Host Services", action, "both" ));
}


/***********************************************************************
 *
 * Name       : run_goenabled_start
 *
 * Purpose    : Call the files in /etc/goenable.d with start command
 *
 * Description: This procedure forks off a mtcClient child process
 *              which runs a sorted list of files in the /etc/goenable.d
 *              diectory.
 *
 * This child waits for the completion of each goenabled script before
 * running the next one.
 *
 * Success Path Behavior:
 *
 * If all the scripts complete with an exit status of zero then this
 * child process will send a GOENABLED message to the mtcAgent
 * informing it that the intest phase completed successfully.
 * This success message is logged in the mtcAgent and if this
 * host was undergoing an enable sequence or graceful recovery then
 * those FSMs would see the pass and proceed to its next state/phase.
 *
 * Failure Path Behavior:
 *
 * If one of the goenabled scripts exits with a return code other than
 * zero then the child creates a GOENABLED_FAILED message containing
 * the name of the script that failed and the error code that was
 * returned and sends that message to the mtcAgent which will cause
 * a failure of an enable or graceful recovery of that host.
 *
 * Returns    : operation PASS or FAIL.
 *
 */

int run_goenabled_scripts ( string requestor )
{
    int rc = RETRY ;

    /* list of service files */
    std::list<string> scripts ;
    std::list<string>::iterator string_iter_ptr ;

    /* handle mutual exclusion */
    if ( ctrl.goenabled.posted )
    {
        return (rc);
    }

    if ((rc = load_filenames_in_dir ( GOENABLED_DIR, scripts )) != PASS )
    {
        return (FAIL_READ_FILES);
    }
    else if ( scripts.empty() )
    {
        ctrl.goenabled.posted = MTC_CMD_NONE ;
        _manage_goenabled_tests ();
        return (PASS);
    }

    if ( ctrl.active_script_set == GOENABLED_SUBF_SCRIPTS )
    {
        ilog ("GoEnabled Scripts : Sub-Function Context\n");
        daemon_remove_file ( GOENABLED_SUBF_FAIL );
    }
    else
    {
        ilog ("GoEnabled Scripts : Main-Function Context\n");
        daemon_remove_file ( GOENABLED_MAIN_FAIL );
    }

    scripts.sort();

    /* manage the goenabled timeout timer */
    if ( ctrl.goenabled.timer.tid )
    {
        slog ("goenabled timer unexpectedly active\n");
        mtcTimer_stop ( ctrl.goenabled.timer );
    }

    dlog ("Goenabled Timeout : %d secs\n", mtc_config.goenabled_timeout );
    mtcTimer_start ( ctrl.goenabled.timer, timer_handler, mtc_config.goenabled_timeout );

    /* launch the scripts */
    return (_launch_all_scripts ( ctrl.goenabled, scripts, "Test", "start", requestor ));
}


/* Reap the go enabled tests */
void daemon_sigchld_hdlr ( void )
{
    pid_t tpid = 0 ;
    int status = 0 ;
    bool found = 0 ;
    static script_ctrl_type * scripts_ptr ;

    dlog("Received SIGCHLD ...\n");

    /* select the correct script set based on which is active */
    switch ( ctrl.active_script_set )
    {
        case GOENABLED_MAIN_SCRIPTS:
        case GOENABLED_SUBF_SCRIPTS:
        {
            scripts_ptr = &ctrl.goenabled ;
            break ;
        }
        case HOSTSERVICES_SCRIPTS:
        {
            scripts_ptr = &ctrl.hostservices ;
            break ;
        }
        default:
        {
            return ;
        }
    }

    while ( 0 < ( tpid = waitpid ( -1, &status, WNOHANG | WUNTRACED )))
    {
        /* loop over all the scripts and get the child execution status */
        for ( int i = 0 ; i < scripts_ptr->scripts ; i++ )
        {
            if ( tpid == scripts_ptr->script[i].pid )
            {
                found = true ;
                scripts_ptr->script[i].status = status ;
                if ( scripts_ptr->script[i].done == false )
                {
                    dlog("%5d %s exited (%d)\n", scripts_ptr->script[i].pid, scripts_ptr->script[i].name.c_str(), i );
                    scripts_ptr->script[i].done = true ;
                    scripts_ptr->scripts_done++ ;
                }
                else
                {
                    slog ("%5d %s exited already (%d)\n", scripts_ptr->script[i].pid, scripts_ptr->script[i].name.c_str(), i );
                }

                /* script ended */
                if (WIFEXITED(scripts_ptr->script[i].status))
                {
                    gettime   ( scripts_ptr->script[i].time_stop );
                    timedelta ( scripts_ptr->script[i].time_start,
                                scripts_ptr->script[i].time_stop,
                                scripts_ptr->script[i].time_delta );

                    dlog ("%s exited properly \n", scripts_ptr->script[i].name.c_str());

                    /* only print log if there is an error */
                    scripts_ptr->script[i].status = WEXITSTATUS(scripts_ptr->script[i].status) ;
                    if ( status )
                    {
                        elog ("FAILED: %s (%ld.%03ld secs) (rc:%d)\n",
                                  scripts_ptr->script[i].name.c_str(),
                                  scripts_ptr->script[i].time_delta.secs,
                                  scripts_ptr->script[i].time_delta.msecs/1000,
                                  scripts_ptr->script[i].status);
                    }
                    else
                    {
                        ilog ("PASSED: %s (%ld.%03ld secs)\n",
                                  scripts_ptr->script[i].name.c_str(),
                                  scripts_ptr->script[i].time_delta.secs,
                                  scripts_ptr->script[i].time_delta.msecs/1000);
                    }
                }
                else if (WIFSIGNALED(scripts_ptr->script[i].status))
                {
                    wlog ("%s test uncaught signal\n", scripts_ptr->script[i].name.c_str());
                }
                else if (WIFSTOPPED(scripts_ptr->script[i].status))
                {
                    wlog ("%s test stopped.\n", scripts_ptr->script[i].name.c_str());
                }
            }
        }
    }
    if ( ( tpid > 0 ) && ( found == false ) )
    {
        ilog ("PID:%d reaped with no corresponding process\n", tpid );
    }
}

/***************************************************************************
 *
 * Name       : load_mtcInfo_msg
 *
 * Description: Extract the mtc info from the MTC_MSG_INFO message.
 *
 * Assumptions: So far only the peer controller reset feature uses this.
 *
 * Returns    : Nothing
 *
 ***************************************************************************/

void load_mtcInfo_msg ( mtc_message_type & msg )
{
    if ( ctrl.nodetype & CONTROLLER_TYPE )
    {
        mlog1("%s", &msg.buf[0]);
        struct json_object *_obj = json_tokener_parse( &msg.buf[0] );
        if ( _obj )
        {
            if ( strcmp(&ctrl.hostname[0], CONTROLLER_0 ))
                peer_controller.hostname = CONTROLLER_0 ;
            else
                peer_controller.hostname = CONTROLLER_1 ;

            struct json_object *info_obj = (struct json_object *)(NULL);
            json_bool json_rc = json_object_object_get_ex( _obj,
                                                          "mtcInfo",
                                                          &info_obj );
            if ( ( json_rc == true ) && ( info_obj ))
            {
                struct json_object *ctrl_obj = (struct json_object *)(NULL);
                json_bool json_rc =
                json_object_object_get_ex( info_obj,
                                           peer_controller.hostname.data(),
                                          &ctrl_obj );

                if (( json_rc == true ) && ( ctrl_obj ))
                {
                    peer_controller.host_ip = jsonUtil_get_key_value_string(ctrl_obj, MTC_JSON_INV_HOSTIP) ;
                    peer_controller.bm_ip = jsonUtil_get_key_value_string(ctrl_obj, MTC_JSON_INV_BMIP) ;
                    peer_controller.bm_un = jsonUtil_get_key_value_string(ctrl_obj, "bm_un");
                    peer_controller.bm_pw = jsonUtil_get_key_value_string(ctrl_obj, "bm_pw");

                    /* Log the mc info but not the bmc password.
                     * Only indicate that it looks 'ok' or 'is 'none'.
                     * However, don't log if the bmc ip is none */
                    if ( peer_controller.bm_ip.compare("none") )
                    {
                        ilog ("%s is my peer [host:%s bmc:%s:%s:%s]",
                               peer_controller.hostname.c_str(),
                               peer_controller.host_ip.c_str(),
                               peer_controller.bm_ip.c_str(),
                               peer_controller.bm_un.c_str(),
                               hostUtil_is_valid_pw(peer_controller.bm_pw) ? "ok":"none");
                    }
                }
                else if ( ctrl.system_type != SYSTEM_TYPE__AIO__SIMPLEX )
                {
                    wlog("peer mtcInfo missing (rc:%d) ; %s",
                          json_rc, &msg.buf[0]);
                }
            }
            else
            {
                wlog("mtcInfo label parse error (rc:%d) ; %s",
                      json_rc, &msg.buf[0]);
            }
            json_object_put(_obj);
        }
        else
        {
            wlog("message buffer tokenize error ; %s", &msg.buf[0]);
        }
    }
    else
    {
        slog("%s got mtcInfo ; unexpected for this nodetype", ctrl.hostname);
    }
}

/***************************************************************************
 *
 * Name       : load_pxebootInfo_msg
 *
 * Description: Extract the pxeboot info from the MTC_REQ_MTCALIVE message.
 *
 * Assumptions: Contains a json string with the controller pxeboot
 *              network unit IP addresses in the following form.
 *              Address can be empty of an unprovisioned controller.
 *
 *              { "pxebootInfo":{
 *                   "controller"   : "169.254.202.2"
 *                   "controller-0" : "169.254.202.2",
 *                   "controller-1" : "169.254.202.3"
 *                 }
 *              }
 *
 * Returns    : Nothing
 *
 ***************************************************************************/
void load_pxebootInfo_msg ( mtc_message_type & msg )
{
    struct json_object *json_obj = json_tokener_parse( &msg.buf[0] );
    if ( json_obj )
    {
        const char dict_label [] = "pxebootInfo" ;
        struct json_object *info_obj = (struct json_object *)(NULL);
        json_bool json_rc = json_object_object_get_ex( json_obj,
                                            &dict_label[0],
                                            &info_obj );

        if ( ( json_rc == true ) && ( info_obj ) )
        {
            jlog ("%s: %s ", &dict_label[0], json_object_get_string(info_obj));
            struct json_object *ctrl_obj = (struct json_object *)(NULL);
            json_rc = json_object_object_get_ex( info_obj, CONTROLLER, &ctrl_obj );
            if (( json_rc == true ) && ( ctrl_obj ))
            {
                string active_controller = json_object_get_string(ctrl_obj);
                if ( ctrl.pxeboot_addr_active_controller != active_controller )
                {
                    string prefix = "controller pxeboot address" ;
                    if ( ctrl.pxeboot_addr_active_controller.empty() )
                    {
                        ilog ("%s: %s",
                               prefix.c_str(),
                               active_controller.c_str());
                    }
                    else
                    {
                        ilog ("%s: %s ; was %s",
                               prefix.c_str(),
                               active_controller.c_str(),
                               ctrl.pxeboot_addr_active_controller.c_str());
                    }
                    ctrl.pxeboot_addr_active_controller = active_controller ;
                }
            }
            // now get the individual controller addresses
            string pxeboot_addr_cx[CONTROLLERS] = {CONTROLLER_0, CONTROLLER_1};
            for (int c = 0 ; c < CONTROLLERS ; c++)
            {
                // used to store the in-loop controller current pxeboot address
                string cur_pxeboot_addr ;
                // only updated if the address changes
                string new_pxeboot_addr ;
                // current loop controller hostname
                string controller = pxeboot_addr_cx[c] ;

                // get the current pxeboot address for the in loop controller
                cur_pxeboot_addr = (controller == CONTROLLER_0) ? ctrl.pxeboot_addr_c0 : ctrl.pxeboot_addr_c1;

                json_rc = json_object_object_get_ex( info_obj, controller.data(), &ctrl_obj );
                if (( json_rc == true ) && (ctrl_obj))
                {
                    jlog ("controller-x obj data: %s", json_object_get_string(ctrl_obj));

                    // get the in-loop controller pxeboot address from the msg
                    string now_pxeboot_addr = json_object_get_string(ctrl_obj);
                    if ( now_pxeboot_addr != cur_pxeboot_addr )
                    {
                        if ( now_pxeboot_addr.empty() )
                        {
                            new_pxeboot_addr = now_pxeboot_addr ;
                            wlog ("%s pxeboot address now null ; was %s", controller.c_str(),
                                      cur_pxeboot_addr.empty() ? "null" : cur_pxeboot_addr.c_str());
                        }
                        else if ( cur_pxeboot_addr.empty() )
                        {
                            new_pxeboot_addr = now_pxeboot_addr ;
                            ilog ("%s pxeboot ip: %s", controller.c_str(), new_pxeboot_addr.c_str());
                        }
                        else
                        {
                            new_pxeboot_addr = now_pxeboot_addr ;
                            ilog ("%s pxeboot ip: %s ; change from %s", controller.c_str(),
                                      new_pxeboot_addr.c_str(), cur_pxeboot_addr.c_str());
                        }
                    }
                    else if ( !cur_pxeboot_addr.empty() )
                    {
                        alog1 ("%s pxeboot ip %s ; unchanged", controller.c_str(), cur_pxeboot_addr.c_str());
                    }

                    // now manage the change
                    if ( !new_pxeboot_addr.empty() )
                    {
                        if ( controller == CONTROLLER_0 )
                            ctrl.pxeboot_addr_c0 = new_pxeboot_addr ;
                        else
                            ctrl.pxeboot_addr_c1 = new_pxeboot_addr ;
                    }
                }
                else
                {
                    wlog ("Failed to parse %s pxeboot ip from '%s' : %s",
                           controller.c_str(), &dict_label[0], &msg.buf[0]);
                }
            } // for loop
        }
        else
        {
            elog("Failed to parse '%s' from mtcAlive request message: %s",
                  &dict_label[0], &msg.buf[0]);
        }
        json_object_put(json_obj);
    }
    else
    {
        elog("Failed to tokenize mtcAlive request message data: %s",
              &msg.buf[0]);
    }
}

/* Push daemon state to log file */
void daemon_dump_info ( void )
{
    ;
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

extern int mtcCompMsg_testhead ( void );

/** Teat Head Entry */
int daemon_run_testhead ( void )
{
    int rc    = PASS;
    int stage = 1;
    printf ("\n");
    rc = mtcCompMsg_testhead ();
    printf  ("\n\n+---------------------------------------------------------+\n");

    /***********************************************
    * STAGE 1: some test
    ************************************************/
    printf ( "| Test  %d : Maintenance Service Test ............. ", stage );
    if ( rc != PASS )
    {
       FAILED_STR ;
       rc = FAIL ;
    }
    else
       PASSED ;

    printf  ("+---------------------------------------------------------+\n");
    return PASS ;
}
