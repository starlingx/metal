/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
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
//#include <syslog.h>    /* for ... syslog                  */
#include <sys/stat.h>
#include <list>

using namespace std;

#include "daemon_ini.h"     /* for ... Init parset header                 */
#include "daemon_common.h"  /* for ... common daemon definitions          */
#include "daemon_option.h"  /* for ... daemon main options                */

#include "nodeBase.h"       /* for ... Common Definitions                 */
#include "nodeTimers.h"     /* fpr ... Timer Service                      */
#include "nodeUtil.h"       /* for ... Common Utilities                   */
#include "nodeMacro.h"      /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET */
#include "mtcNodeMsg.h"     /* for ... common maintenance messaging       */
#include "mtcNodeComp.h"    /* for ... this module header                 */
#include "regexUtil.h"      /* for ... Regex and String utilities         */
extern "C"
{
#include "amon.h"           /* for ... active monitoring utilities        */
#include "rmon_api.h"       /* for ... resource monitoring utilities      */

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
    else
    {
        mtcTimer_stop_tid_int_safe ( tid_ptr );
    }
}

void _close_mgmnt_rx_socket ( void )
{
    if ( mtc_sock.mtc_client_rx_socket )
    {
        delete(mtc_sock.mtc_client_rx_socket);
        mtc_sock.mtc_client_rx_socket = 0 ;
    }
}

void _close_infra_rx_socket ( void )
{
    if ( mtc_sock.mtc_client_infra_rx_socket )
    {
        delete(mtc_sock.mtc_client_infra_rx_socket);
        mtc_sock.mtc_client_infra_rx_socket = 0 ;
    }
}

void _close_mgmnt_tx_socket ( void )
{
    if (mtc_sock.mtc_client_tx_socket)
    {
        delete (mtc_sock.mtc_client_tx_socket);
        mtc_sock.mtc_client_tx_socket = 0 ;
    }
}

void _close_infra_tx_socket ( void )
{
    if (mtc_sock.mtc_client_infra_tx_socket)
    {
        delete (mtc_sock.mtc_client_infra_tx_socket);
        mtc_sock.mtc_client_infra_tx_socket = 0 ;
    }
}

void _close_rmon_sock ( void )
{
    if ( mtc_sock.rmon_socket )
    {
        close (mtc_sock.rmon_socket);
        mtc_sock.rmon_socket = 0 ;
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

    _close_mgmnt_rx_socket ();
    _close_infra_rx_socket ();
    _close_mgmnt_tx_socket ();
    _close_infra_tx_socket ();
    _close_rmon_sock       ();
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
        config_ptr->mask |= CONFIG_AGENT_PORT ;
    }
    else if (MATCH("client", "mtc_rx_mgmnt_port"))
    {
        config_ptr->mtc_rx_mgmnt_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_MTC_MGMNT_PORT ;
    }
    else if (MATCH("client", "rmon_event_port"))
    {
        config_ptr->rmon_event_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_RMON_PORT ;
    }
    else if (MATCH("timeouts", "failsafe_shutdown_delay"))
    {
        config_ptr->failsafe_shutdown_delay = atoi(value);
        ilog ("Shutdown TO : %d secs\n", config_ptr->failsafe_shutdown_delay );
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
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
        elog ("Failed Compute Mtc Configuration (%x)\n", 
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
    return (rc);
}

/****************************/
/* Initialization Utilities */
/****************************/

void setup_mgmnt_rx_socket ( void )
{
    dlog ("setup of mgmnt RX\n");
    ctrl.mgmnt_iface = daemon_mgmnt_iface() ;
    ctrl.mgmnt_iface = daemon_get_iface_master ((char*)ctrl.mgmnt_iface.data());

    if ( ! ctrl.mgmnt_iface.empty() )
    {
        ilog("Mgmnt iface : %s\n", ctrl.mgmnt_iface.c_str() );
        get_iface_macaddr  ( ctrl.mgmnt_iface.data(), ctrl.macaddr );
        get_iface_address  ( ctrl.mgmnt_iface.data(), ctrl.address , true );
    get_hostname       ( &ctrl.hostname[0], MAX_HOST_NAME_SIZE );

        _close_mgmnt_rx_socket ();
        mtc_sock.mtc_client_rx_socket = new msgClassRx(ctrl.address.c_str(),mtc_sock.mtc_cmd_port, IPPROTO_UDP, ctrl.mgmnt_iface.data(), false );

        /* update health of socket */
        if ( mtc_sock.mtc_client_rx_socket )
        {
            /* look for fault insertion request */
            if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_RXSOCK ) )
                mtc_sock.mtc_client_rx_socket->return_status = FAIL ;

            if ( mtc_sock.mtc_client_rx_socket->return_status == PASS )
            {
                mtc_sock.mtc_client_rx_socket->sock_ok (true);
            }
            else
            {
                elog ("failed to init 'management rx' socket (rc:%d)\n",
                mtc_sock.mtc_client_rx_socket->return_status );
                mtc_sock.mtc_client_rx_socket->sock_ok (false);
            }
        }
    }
}


void setup_infra_rx_socket ( void )
{
    if ( ctrl.infra_iface_provisioned == false )
    {
        return ;
    }

    dlog ("setup of infra RX\n");
    /* Fetch the infrastructure interface name.
     * calls daemon_get_iface_master inside so the
     * aggrigated name is returned if it exists */
    get_infra_iface (&mtc_config.infra_iface );
    if ( strlen(mtc_config.infra_iface) )
    {
        /* Only get the infrastructure network address if it is provisioned */ 
        if ( get_iface_address  ( mtc_config.infra_iface, ctrl.address_infra, false ) == PASS )
        {
            ilog ("Infra iface : %s\n", mtc_config.infra_iface );
            ilog ("Infra addr  : %s\n", ctrl.address_infra.c_str());
        }
    }
    if ( !ctrl.address_infra.empty() )
    {
        _close_infra_rx_socket ();

        /* Only set up the socket if an infra interface is provisioned */
        mtc_sock.mtc_client_infra_rx_socket = new msgClassRx(ctrl.address_infra.c_str(),mtc_sock.mtc_cmd_port, IPPROTO_UDP, ctrl.infra_iface.data(), false );

        /* update health of socket */
        if ( mtc_sock.mtc_client_infra_rx_socket )
        {
            /* look for fault insertion request */
            if ( daemon_is_file_present ( MTC_CMD_FIT__INFRA_RXSOCK ) )
                mtc_sock.mtc_client_infra_rx_socket->return_status = FAIL ;

            if ( mtc_sock.mtc_client_infra_rx_socket->return_status  == PASS )
            {
                mtc_sock.mtc_client_infra_rx_socket->sock_ok (true);
            }
            else
            {
                elog ("failed to init 'infrastructure rx' socket (rc:%d)\n",
                mtc_sock.mtc_client_infra_rx_socket->return_status );
                mtc_sock.mtc_client_infra_rx_socket->sock_ok (false);
            }
        }
    }
}

void setup_mgmnt_tx_socket ( void )
{
    dlog ("setup of mgmnt TX\n");
    _close_mgmnt_tx_socket ();
    mtc_sock.mtc_client_tx_socket = new msgClassTx(CONTROLLER,mtc_sock.mtc_agent_port, IPPROTO_UDP, ctrl.mgmnt_iface.data());

    if ( mtc_sock.mtc_client_tx_socket )
    {
        /* look for fault insertion request */
        if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_TXSOCK ) )
            mtc_sock.mtc_client_tx_socket->return_status = FAIL ;

        if ( mtc_sock.mtc_client_tx_socket->return_status == PASS )
        {
            mtc_sock.mtc_client_tx_socket->sock_ok(true);
        }
        else
        {
            elog ("failed to init 'management tx' socket (rc:%d)\n",
            mtc_sock.mtc_client_tx_socket->return_status );
            mtc_sock.mtc_client_tx_socket->sock_ok(false);
        }
    }
}

void setup_infra_tx_socket ( void )
{
    if ( ctrl.infra_iface_provisioned == false )
    {
        return ;
    }

    dlog ("setup of infra TX\n");
    _close_infra_tx_socket ();
    mtc_sock.mtc_client_infra_tx_socket = new msgClassTx(CONTROLLER_NFS,mtc_sock.mtc_agent_port, IPPROTO_UDP, mtc_config.infra_iface);

    if ( mtc_sock.mtc_client_infra_tx_socket )
    {
        /* look for fault insertion request */
        if ( daemon_is_file_present ( MTC_CMD_FIT__INFRA_TXSOCK ) )
            mtc_sock.mtc_client_infra_tx_socket->return_status = FAIL ;

        if ( mtc_sock.mtc_client_infra_tx_socket->return_status == PASS )
        {
            mtc_sock.mtc_client_infra_tx_socket->sock_ok(true);
        }
        else
        {
            elog ("failed to init 'infrastructure tx' socket (rc:%d)\n",
            mtc_sock.mtc_client_infra_tx_socket->return_status );
            mtc_sock.mtc_client_infra_tx_socket->sock_ok(false);
        }
    }
}


void setup_amon_socket ( void )
{
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
        elog ("Failed to set amon socket non-blocking\n");
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
   
void setup_rmon_socket ( void )
{
    mtc_sock.rmon_socket =
    resource_monitor_initialize ( program_invocation_short_name,  mtc_config.rmon_event_port, ALL_USAGE );
    if ( mtc_sock.rmon_socket )
    {
        int  val = 1;

    /* Make the active monitor socket non-blocking */
        if ( 0 > ioctl(mtc_sock.rmon_socket, FIONBIO, (char *)&val) )
    {
        elog ("failed to set rmon event port non-blocking (%d:%s),\n", errno, strerror(errno));
            close ( mtc_sock.rmon_socket );
    }
        else
    {
            ilog ("Resource Monitor Socket %d\n", mtc_sock.rmon_socket );
            return ;
    }
    }
    else
    {
        elog ("failed to register as client with rmond\n");
    }
    mtc_sock.rmon_socket = 0 ;
}

/******************************************************************
 *
 * Construct the messaging sockets
 *
 * 1. Unicast receive socket mgmnt (mtc_client_rx_socket)
 * 2. Unicast receive socket infra (mtc_client_infra_rx_socket)
 * 3. Unicast transmit socket mgmnt (mtc_client_tx_socket)
 * 4. Unicast transmit socket infra (mtc_client_infra_tx_socket)
 *
 * 5. socket for pmond acive monitoring
 * 6. socket to receive rmond events (including AVS)
 *
 *******************************************************************/
int mtc_socket_init ( void )
{
    /* Setup the Management Interface Recieve Socket */
    /* Read the port config strings into the socket struct */
    mtc_sock.mtc_agent_port  = mtc_config.mtc_agent_port;
    mtc_sock.mtc_cmd_port    = mtc_config.mtc_rx_mgmnt_port;

    ctrl.mtcAgent_ip = getipbyname ( CONTROLLER );
    ilog ("Controller  : %s\n", ctrl.mtcAgent_ip.c_str());

    /************************************************************/
    /* Setup the Mgmnt Interface Receive Socket                 */
    /************************************************************/
    setup_mgmnt_rx_socket ();

    /************************************************************/
    /* Setup the Mgmnt Interface Transmit messaging to mtcAgent */
    /************************************************************/
    setup_mgmnt_tx_socket ();

    /* Manage Infrastructure network setup */
    string infra_iface_name = daemon_infra_iface();
    string mgmnt_iface_name = daemon_mgmnt_iface();
    if ( !infra_iface_name.empty() )
    {
        if ( infra_iface_name != mgmnt_iface_name )
        {
            ctrl.infra_iface_provisioned = true ;
            /************************************************************/
            /* Setup the Infra Interface Receive Socket                 */
            /************************************************************/
            setup_infra_rx_socket () ;

            /*************************************************************/
            /* Setup the Infra Interface Transmit Messaging to mtcAgent  */
            /*************************************************************/
            setup_infra_tx_socket () ;
        }
    }

    /*************************************************************/
    /* Setup and Open the active monitoring socket               */
    /*************************************************************/
    setup_amon_socket ();

    /*************************************************************/
    /* Setup and Open the resource monitor event socket          */
    /*************************************************************/
    setup_rmon_socket ();

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
 * infra ip address
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

    ctrl.who_i_am.append( ",\"mgmt_ip\":\"");
    ctrl.who_i_am.append( ctrl.address.data() );
    ctrl.who_i_am.append( "\"");

    ctrl.who_i_am.append( ",\"infra_ip\":\"");
    ctrl.who_i_am.append( ctrl.address_infra.data() );
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


/* The main service loop */
int daemon_init ( string iface, string nodetype_str )
{
    int rc = PASS ;

    ctrl.nodetype_str = nodetype_str ;

    ctrl.who_i_am = "" ;
    ctrl.macaddr = "" ;
    ctrl.address = "" ;
    ctrl.address_infra = "" ;
    ctrl.mtcAgent_ip = "";
    ctrl.function    = 0 ;
    ctrl.subfunction = 0 ;
    ctrl.system_type = daemon_system_type ();
    ctrl.infra_iface_provisioned = false ;

    /* convert node type to integer */
    ctrl.nodetype = get_host_function_mask ( nodetype_str ) ;
    ilog ("Node Type   : %s (%d:%x)\n", nodetype_str.c_str(), ctrl.nodetype, ctrl.nodetype);

    /* Initialize socket construct and pointer to it */
    memset ( &mtc_sock,   0, sizeof(mtc_sock));
    sock_ptr = &mtc_sock ;

    /* Assign interface to config */
    ctrl.mgmnt_iface = iface ;

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
        ctrl.posted_script_set.push_front(GOENABLED_MAIN_SCRIPTS);
    }

    return (rc) ;
}

int select_log_count = 0 ;
void daemon_service_run ( void )
{
    int rmon_code;
    string resource_name;

    int rc = PASS ;
    int file_not_present_count = 0 ;

    /* Start mtcAlive message timer */
    /* Send first mtcAlive ASAP */
    mtcTimer_start ( ctrl.timer, timer_handler, 1 );

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

        if ( mtc_sock.mtc_client_rx_socket && mtc_sock.mtc_client_rx_socket->return_status==PASS )
        {
            socks.push_front (mtc_sock.mtc_client_rx_socket->getFD());
            FD_SET(mtc_sock.mtc_client_rx_socket->getFD(), &mtc_sock.readfds);
        }

        if (( ctrl.infra_iface_provisioned == true ) &&
            ( mtc_sock.mtc_client_infra_rx_socket ) &&
            ( mtc_sock.mtc_client_infra_rx_socket->return_status==PASS ))
        {
            socks.push_front (mtc_sock.mtc_client_infra_rx_socket->getFD());
            FD_SET(mtc_sock.mtc_client_infra_rx_socket->getFD(), &mtc_sock.readfds);
        }

        mtc_sock.amon_socket = active_monitor_get_sel_obj ();
        if ( mtc_sock.amon_socket )
        {
            socks.push_front (mtc_sock.amon_socket);
            FD_SET(mtc_sock.amon_socket,          &mtc_sock.readfds);
        }

        mtc_sock.rmon_socket = resource_monitor_get_sel_obj ();
        if ( mtc_sock.rmon_socket )
        {
            socks.push_front (mtc_sock.rmon_socket);
            FD_SET(mtc_sock.rmon_socket,          &mtc_sock.readfds);
        }

        /* Initialize the timeval struct to wait for 50 mSec */
        mtc_sock.waitd.tv_sec  = 0;
        mtc_sock.waitd.tv_usec = SOCKET_WAIT;

        /* Call select() and wait only up to SOCKET_WAIT */
        socks.sort();

#ifdef WANT_SELECTS
        ilog_throttled ( select_log_count, 200 , "Selects: mgmnt:%d infra:%d amon:%d rmon:%d - Size:%ld  First:%d Last:%d\n",
                mtc_sock.mtc_client_rx_socket,
                mtc_sock.mtc_client_infra_rx_socket,
                mtc_sock.amon_socket,
                mtc_sock.rmon_socket,
                socks.size(), socks.front(), socks.back());
#endif

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
             if ((mtc_sock.mtc_client_rx_socket && mtc_sock.mtc_client_rx_socket->return_status==PASS) && FD_ISSET(mtc_sock.mtc_client_rx_socket->getFD(), &mtc_sock.readfds))
             {
                 mtc_service_command ( sock_ptr, MGMNT_INTERFACE );
             }
             if (( ctrl.infra_iface_provisioned == true ) &&
                 ( !ctrl.address_infra.empty() ) &&
                 ( mtc_sock.mtc_client_infra_rx_socket ) &&
                 ( mtc_sock.mtc_client_infra_rx_socket->return_status==PASS) &&
                 ( FD_ISSET(mtc_sock.mtc_client_infra_rx_socket->getFD(), &mtc_sock.readfds)))
             {
                 mtc_service_command ( sock_ptr, INFRA_INTERFACE );
             }
             if ( FD_ISSET(mtc_sock.amon_socket, &mtc_sock.readfds))
             {
                 dlog3 ("Active Monitor Select Fired\n");
                 active_monitor_dispatch ();
             }
             if ( FD_ISSET(mtc_sock.rmon_socket, &mtc_sock.readfds))
             {
                 dlog3 ("Resource Monitor Select Fired\n");
                 rc = service_rmon_inbox( sock_ptr, rmon_code, resource_name );

                 if (rc == PASS) {

                 switch ( rmon_code ) {
                 case RMON_CLEAR:
                    mtce_send_event ( sock_ptr, MTC_EVENT_RMON_CLEAR, resource_name.c_str() );
                    break;

                 case  RMON_MINOR:
                    mtce_send_event ( sock_ptr, MTC_EVENT_RMON_MINOR, resource_name.c_str() );
                    break;

                 case  RMON_MAJOR:
                    mtce_send_event ( sock_ptr, MTC_EVENT_RMON_MAJOR, resource_name.c_str() );
                    break;

                 case  RMON_CRITICAL:
                    mtce_send_event ( sock_ptr, MTC_EVENT_RMON_CRIT, resource_name.c_str() );
                    break;
                 case MTC_EVENT_AVS_CLEAR:
                 case MTC_EVENT_AVS_MAJOR:
                 case MTC_EVENT_AVS_CRITICAL:
                    mtce_send_event ( sock_ptr, rmon_code, "" );
                    break;
                 default:
                    break;
                 }

               }
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
        if ( ctrl.timer.ring == true )
        {
            bool socket_reinit = true ;

            /**
             *  Look for failing sockets and try to recover them,
             *  but only one at a time if there are multiple failing.
             *  Priority is the command receiver, thehn transmitter,
             *  followed by the infra and others.
             **/

            /* Mgmnt Rx */
            if (( mtc_sock.mtc_client_rx_socket == NULL ) ||
                ( mtc_sock.mtc_client_rx_socket->sock_ok() == false ))
            {
                setup_mgmnt_rx_socket();
                wlog ("calling setup_mgmnt_rx_socket (auto-recovery)\n");
                socket_reinit = true ;
            }

            /* Mgmnt Tx */
            else if (( mtc_sock.mtc_client_tx_socket == NULL  ) ||
                     ( mtc_sock.mtc_client_tx_socket->sock_ok() == false ))
            {
                setup_mgmnt_tx_socket();
                wlog ("calling setup_mgmnt_tx_socket\n");
                socket_reinit = true ;
            }

            /* Infra Rx */
            else if (( ctrl.infra_iface_provisioned == true ) &&
                     (( mtc_sock.mtc_client_infra_rx_socket == NULL ) ||
                      ( mtc_sock.mtc_client_infra_rx_socket->sock_ok() == false )))
            {
                setup_infra_rx_socket();
                wlog ("calling setup_infra_rx_socket (auto-recovery)\n");
                socket_reinit = true ;
            }

            /* Infra Tx */
            else if (( ctrl.infra_iface_provisioned == true ) &&
                     (( mtc_sock.mtc_client_infra_tx_socket == NULL ) ||
                      ( mtc_sock.mtc_client_infra_tx_socket->sock_ok() == false )))
            {
                setup_infra_tx_socket();
                wlog ("calling setup_infra_tx_socket (auto-recovery)\n");
                socket_reinit = true ;
            }

            /* RMON event notifications */
            else if ( mtc_sock.rmon_socket <= 0 )
            {
                setup_rmon_socket ();
                wlog ("calling setup_rmon_socket (auto-recovery)\n");
                socket_reinit = true ;
            }

            else if ( mtc_sock.amon_socket <= 0 )
            {
                setup_amon_socket ();
                wlog ("calling setup_amon_socket (auto-recovery)\n");
                socket_reinit = true ;
            }
            else
            {
                socket_reinit = false ;
            }

            if ( socket_reinit )
            {
                /* re-get identity if interfaces are re-initialized */
                string who_i_am = _self_identify ( ctrl.nodetype_str );
            }

            send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, MGMNT_INTERFACE );
            if (( ctrl.infra_iface_provisioned == true ) &&
                ( mtc_sock.mtc_client_infra_rx_socket != NULL ) &&
                ( mtc_sock.mtc_client_infra_rx_socket->sock_ok() == true ))
            {
                send_mtcAlive_msg ( sock_ptr, ctrl.who_i_am, INFRA_INTERFACE );
            }

            /* Re-Start mtcAlive message timer */
            mtcTimer_start ( ctrl.timer, timer_handler, MTC_ALIVE_TIMER );

            dlog3 ("Infra is %senabled", ctrl.infra_iface_provisioned ? "" : "NOT ");

            if ( daemon_is_file_present ( MTC_CMD_FIT__DIR ) )
            {
                /* fault insertion testing */
                if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_RXSOCK ))
                {
                    if ( mtc_sock.mtc_client_rx_socket )
                    {
                        mtc_sock.mtc_client_rx_socket->sock_ok (false);
                        _close_mgmnt_rx_socket();
                    }
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__MGMNT_TXSOCK ))
                {
                    if ( mtc_sock.mtc_client_tx_socket )
                    {
                        mtc_sock.mtc_client_tx_socket->sock_ok (false);
                        _close_mgmnt_tx_socket ();
                    }
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__INFRA_RXSOCK ))
                {
                    if ( mtc_sock.mtc_client_infra_rx_socket )
                    {
                        mtc_sock.mtc_client_infra_rx_socket->sock_ok (false);
                        _close_infra_rx_socket ();
                    }
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__INFRA_TXSOCK ))
                {
                    if ( mtc_sock.mtc_client_infra_tx_socket )
                    {
                        mtc_sock.mtc_client_infra_tx_socket->sock_ok (false);
                        _close_infra_tx_socket ();
                    }
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__RMON_SOCK ))
                {
                    _close_rmon_sock ();
                }
                if ( daemon_is_file_present ( MTC_CMD_FIT__AMON_SOCK ))
                {
                    _close_amon_sock ();
                }
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
     * worker stop services if we are on a CPE system.
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
            wlog ("child handler running with no active script set (%d)\n", ctrl.active_script_set );
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
