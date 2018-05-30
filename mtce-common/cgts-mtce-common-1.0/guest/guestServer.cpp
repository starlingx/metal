/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Guest Heartbeat Server Daemon on Compute
  */

#include <dirent.h>
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

using namespace std;

#include "nodeBase.h"
#include "daemon_ini.h"    /* Ini Parser Header                            */
#include "daemon_common.h" /* Common definitions and types for daemons     */
#include "daemon_option.h" /* Common options  for daemons                  */
#include "nodeUtil.h"      /* for ... common utilities                     */
#include "jsonUtil.h"      /* for ... jason utilities                      */
#include "nodeTimers.h"    /* for ... maintenance timers                   */
#include "nodeMacro.h"     /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET   */
#include "nodeEvent.h"     /* for ... set_inotify_watch, set_inotify_close */
#include "guestBase.h"
#include "guestUtil.h"     /* for ... guestUtil_inst_init                  */
#include "guestSvrUtil.h"  /* for ... guestUtil_inotify_events             */
#include "guestVirtio.h"   /* for ... virtio_channel_connect               */
#include "guestSvrMsg.h"   /* for ... send_to_guestAgent                   */
#include "guestInstClass.h"

/* Where to send events */
string guestAgent_ip = "" ;

/*****************************************************************************
 *
 * The daemon primary instance racking object.
 *
 * This object is a dynamically managed linked list of tracked insances
 *
 * @see guestInstClass Module control structure in guestInstClass.h
 *
 *****************************************************************************/
guestInstClass       instInv ;
guestInstClass * get_instInv_ptr ( void ) { return(&instInv); }


/* @see guestBase.h Module control structure 
 * TODO: Consider obsoleting by moving into class */
ctrl_type ctrl ;
ctrl_type * get_ctrl_ptr ( void )
{
    return(&ctrl);
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
daemon_config_type * daemon_get_cfg_ptr () { return &guest_config ; }

/* Cleanup exit handler */
void daemon_exit ( void )
{
    daemon_dump_info  ();
    daemon_files_fini ();
   
    /* Close the messaging sockets */
    if ( ctrl.sock.server_rx_sock )
       delete (ctrl.sock.server_rx_sock);
    
    if ( ctrl.sock.server_tx_sock )
       delete (ctrl.sock.server_tx_sock);
    
    if ( ctrl.sock.agent_rx_float_sock )
       delete (ctrl.sock.agent_rx_float_sock);
    
    if ( ctrl.sock.agent_tx_sock )
       delete (ctrl.sock.agent_tx_sock);

    /* Turn off inotify */
    set_inotify_close ( ctrl.inotify_dir_fd, ctrl.inotify_dir_wd );

    instInv.free_instance_resources ();
   
    fflush (stdout);
    fflush (stderr);

    exit (0);
}

/** Client Config mask */
#define CONFIG_MASK (CONFIG_CLIENT_RX_PORT |\
                     CONFIG_AGENT_RX_PORT)

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
    else if (MATCH("client", "rx_port"))
    {
        config_ptr->client_rx_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_RX_PORT ; 
    }
    else if (MATCH("client", "hbs_pulse_period"))
    {
        config_ptr->hbs_pulse_period = atoi(value);
    }
    else if (MATCH("client", "hbs_failure_threshold"))
    {
        config_ptr->hbs_failure_threshold = atoi(value);
    }
#ifdef WANT_REPORT_DELAY
    else if (MATCH("timeouts", "start_delay"))
    {
        config_ptr->start_delay = atoi(value);
    }
#endif
    else
    {
        return (PASS);
    }
    return (FAIL);
}

/* Read the guest.ini file and load agent    */
/* settings into the daemon configuration  */
int daemon_configure ( void )
{
    int rc = FAIL ;

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

    /* Verify loaded config against an expected mask 
     * as an ini file fault detection method */
    if ( guest_config.mask != CONFIG_MASK )
    {
        elog ("Configuration load failed (%x)\n", 
             (( -1 ^ guest_config.mask ) & CONFIG_MASK) );
        rc = FAIL_INI_CONFIG ;
    }
    else
    {
        guest_config.mgmnt_iface = daemon_get_iface_master ( guest_config.mgmnt_iface );
        ilog("Guest Agent : %s:%d\n", guest_config.mgmnt_iface, guest_config.client_rx_port  );

        // get_iface_macaddr  ( guest_config.mgmnt_iface, my_macaddr );
        get_iface_address  ( guest_config.mgmnt_iface, ctrl.address, true );
        get_hostname       ( &ctrl.hostname[0], MAX_HOST_NAME_SIZE );
        
        ilog("Report Thres: %d\n", guest_config.hbs_failure_threshold  );
#ifdef WANT_REPORT_DELAY
        ilog("Report Delay: %d sec\n", guest_config.start_delay  );
#endif
        ilog("Deflt Period: %d msec\n", guest_config.hbs_pulse_period );
        rc = PASS ;
    }

    return (rc);
}


/****************************/
/* Initialization Utilities */
/****************************/

/* Setup UDP messaging to the guestAgent. */
int _socket_init ( void )
{
    int rc = PASS ;
    
    guestAgent_ip = getipbyname ( CONTROLLER );
    ilog ("ControllerIP: %s\n", guestAgent_ip.c_str());

    /* Read the ports the socket struct */
    ctrl.sock.agent_rx_port  = guest_config.agent_rx_port  ;
    ctrl.sock.server_rx_port = guest_config.client_rx_port ;

    /****************************/
    /* Setup the Receive Socket */
    /****************************/
    ctrl.sock.server_rx_sock = new msgClassRx(ctrl.address.c_str(), guest_config.client_rx_port, IPPROTO_UDP);
    rc = ctrl.sock.server_rx_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup 'guestAgent' receiver on port %d\n", 
               ctrl.sock.server_rx_port );
        return (rc) ;
    }
    ctrl.sock.server_tx_sock = new msgClassTx(guestAgent_ip.c_str(), guest_config.agent_rx_port, IPPROTO_UDP, guest_config.mgmnt_iface);
    rc = ctrl.sock.server_tx_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup 'guestServer' transmiter\n" );
        return (rc) ;
    }
    
    return (rc);
}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodeType_str )
{
    int rc = PASS ;

    ctrl.address.clear() ;
    ctrl.address_peer.clear();
    ctrl.nodetype = CGTS_NODE_NULL ;

    /* Init the Inotify descriptors */
    ctrl.inotify_dir_fd = 0 ;
    ctrl.inotify_dir_wd = 0 ;

    /* clear hostname */
    memset ( &ctrl.hostname[0], 0, MAX_HOST_NAME_SIZE );

    /* Initialize socket construct and pointer to it */   
    memset ( &ctrl.sock, 0, sizeof(ctrl.sock));

    /* Assign interface to config */
    guest_config.mgmnt_iface = (char*)iface.data() ;

    if ( (rc = daemon_files_init ( )) != PASS )
    {
        elog ("Pid, log or other files could not be opened (rc:%d)\n", rc );
        rc = FAIL_FILES_INIT ;
    }

    /* convert node type to integer */
    ctrl.nodetype = get_host_function_mask ( nodeType_str ) ;
    ilog ("Node Type   : %s (%d)\n", nodeType_str.c_str(), ctrl.nodetype );

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
    * Wait for /etc/platform/.initial_config_complete & /var/run/.goenabled */
    daemon_wait_for_file ( CONFIG_COMPLETE_FILE , 0);
    daemon_wait_for_file ( GOENABLED_MAIN_READY , 0);

    /* Configure the client */
    if ( (rc = daemon_configure ()) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%d)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }

    /* Setup the heartbeat service messaging sockets */
    else if ( (rc = _socket_init  ( )) != PASS )
    {
        elog ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT;
    }

    /* Ignore this signal */
    signal(SIGPIPE, SIG_IGN);

    return (rc);
}

/*
   { hostname" : "<hostname>" , 
     "instances" : 
      [
         { "channel" : "<channel>" , "services" : 
             [
                { "service":"heartbeat", "admin":"enabled", "oper":"enabled" , "avail":"available" }
             ],
            "channel: : "<channel>" , "services" :
             [
                { "service":"heartbeat", "admin":"enabled", "oper":"enabled" , "avail":"available"}
             ]
          }
       ]
    }
*/


int select_failure_count = 0 ;

void guestInstClass::manage_comm_loss ( void )
{
    int rc ;

    std::list<int> socks ;
    socks.clear();

    waitd.tv_sec = 0;
    waitd.tv_usec = GUEST_SOCKET_TO;
    
    /* Initialize the master fd_set */
    FD_ZERO(&inotify_readfds);

    /* check for empty list condition */
    if ( inst_head )
    {
        for ( struct inst * inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
        {
            if ( inst_ptr->instance.inotify_file_fd )
            {
                //ilog ("adding inotify_fd %d for %s to select list\n", 
                //       inst_ptr->instance.inotify_file_fd,
                //       inst_ptr->instance.uuid.c_str());

                socks.push_front ( inst_ptr->instance.inotify_file_fd );
                FD_SET ( inst_ptr->instance.inotify_file_fd, &inotify_readfds);
            }
            if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                break ;
        }

        /* if there are no sockets to monitor then just exit */
        if ( socks.empty() )
            return ;

        /* Call select() and wait only up to SOCKET_WAIT */
        socks.sort();
        rc = select( socks.back()+1, &inotify_readfds, NULL, NULL, &waitd);
        if (( rc < 0 ) || ( rc == 0 ) || ( rc > (int)socks.size()))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                wlog_throttled ( select_failure_count, 20, 
                                 "socket select failed (%d:%m)\n", errno);
            }
            else if ( rc > (int)socks.size())
            {
                wlog_throttled ( select_failure_count, 100, 
                                 "Select return exceeds current file descriptors (%ld:%d)\n",
                                 socks.size(), rc );
            }
            else
            {
                select_failure_count = 0 ;
            }
        }
        else
        {
            wlog ( "inotify channel event\n");
            
            for ( struct inst * inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
            {
                if ( inst_ptr->instance.inotify_file_fd )
                {
                    if (FD_ISSET(inst_ptr->instance.inotify_file_fd, &inotify_readfds) ) 
                    {
                        ilog ("Watch Event on instance %s\n", inst_ptr->instance.uuid.c_str());
                        guestUtil_inotify_events (inst_ptr->instance.inotify_file_fd);
                    }
                }
                if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                    break ;
            }
        }
    }
}



#define MAX_LEN 300
void daemon_service_run ( void )
{
    int rc     = 0 ;
    int count  = 0 ;
    int flush_thld = 0 ;
    
    string payload = "" ; /* for the ready event */

    std::list<int> socks ;

    guestUtil_load_channels ();

    /* Setup inotify to watch for new instance serial IO channel creations */
    if ( set_inotify_watch ( QEMU_CHANNEL_DIR, 
                             ctrl.inotify_dir_fd, 
                             ctrl.inotify_dir_wd ) )
    {
        elog ("failed to setup inotify on %s\n", QEMU_CHANNEL_DIR );
    }

    socks.clear();
    socks.push_front (ctrl.sock.server_rx_sock->getFD());
    if ( ctrl.inotify_dir_fd )
        socks.push_front (ctrl.inotify_dir_fd);
    else
    {
        elog ("unable to inotify monitor %s\n", QEMU_CHANNEL_DIR );

        // TODO: consider exiting daemon
    }
    socks.sort();
        
    mtcTimer_init  ( ctrl.timer, ctrl.hostname );
    mtcTimer_init  ( instInv.search_timer, ctrl.hostname );
    
    mtcTimer_start ( ctrl.timer , guestTimer_handler, 2 );
    mtcTimer_start ( instInv.search_timer, guestTimer_handler, SEARCH_AUDIT_TIME );

    ilog ("Selects: guestAgent:%d qemuDir:%d\n", ctrl.sock.server_rx_sock->getFD(), ctrl.inotify_dir_fd );
    ilog ("-------------------------------------------------------\n");

    /* Tell the guestAgent that we started or restarted
     * so that it can send instance state data */
    payload = "{\"hostname\":\"" ;
    payload.append(ctrl.hostname);
    payload.append("\"}");

    /* Run heartbeat service forever or until stop condition */ 
    for ( ; ; )
    {
        instInv.waitd.tv_sec  = 0;
        instInv.waitd.tv_usec = GUEST_SOCKET_TO;

        /* Initialize the master fd_set */
        FD_ZERO(&instInv.message_readfds);

        FD_SET ( ctrl.sock.server_rx_sock->getFD(), &instInv.message_readfds);
        if ( ctrl.inotify_dir_fd )
        {
            FD_SET ( ctrl.inotify_dir_fd, &instInv.message_readfds);
        }

        rc = select( socks.back()+1, &instInv.message_readfds, NULL, NULL, &instInv.waitd);
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
        else if (FD_ISSET(ctrl.sock.server_rx_sock->getFD(), &instInv.message_readfds))
        {
            /* clean the rx/tx buffer */ 
            mtc_message_type msg ;
            memset ((void*)&msg,0,sizeof(mtc_message_type));

            int bytes = ctrl.sock.server_rx_sock->read((char*)&msg.hdr[0], sizeof(mtc_message_type));
            ctrl.address_peer = ctrl.sock.server_rx_sock->get_src_str() ;
            mlog1 ("Received %d bytes from %s:%d:guestAgent\n", bytes, 
                    ctrl.sock.server_rx_sock->get_src_str(),
                    ctrl.sock.server_rx_sock->get_dst_addr()->getPort() );
            print_mtc_message (&msg);

            if ( bytes > 0 )
            {
                recv_from_guestAgent ( msg.cmd, &msg.buf[0] );
            }
        }

        else if (FD_ISSET(ctrl.inotify_dir_fd, &instInv.message_readfds)) 
        {
            dlog ("%s dir change\n", QEMU_CHANNEL_DIR );

            guestUtil_inotify_events (ctrl.inotify_dir_fd);
        }

        fflush (stdout);
        fflush (stderr);

        instInv.guest_fsm_run ( );

        if ( ctrl.timer.ring == true )
        {
            /* restart the timer and try again if this call returns a RETRY */
            if ( send_to_guestAgent ( MTC_EVENT_MONITOR_READY, payload.data()) == RETRY )
            { 
                mtcTimer_start ( ctrl.timer, guestTimer_handler, 5 );
            }
            ctrl.timer.ring = false ;
        }

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
/* Write the daemon /var/log/<daemon>.dump */
void daemon_dump_info ( void )
{
    daemon_dump_membuf_banner ();

    instInv.print_node_info ();
    instInv.memDumpAllState ();

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
