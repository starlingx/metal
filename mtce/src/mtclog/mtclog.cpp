/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform maintenance Log daemon
  */


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <list>

using namespace std;

#include "daemon_ini.h"
#include "daemon_common.h"
#include "daemon_option.h"

#include "nodeBase.h"       /* for ... service module header      */
#include "nodeTimers.h"     /* Timer Service      */
#include "nodeUtil.h"       /* Common Utilities   */
#include "nodeMacro.h"      /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET */
// #include "mtcNodeMsg.h"     /* Common Messaging   */

string my_hostname = "" ;
string my_local_ip = "" ;
string my_float_ip = "" ;

static daemon_config_type _config ;
daemon_config_type * daemon_get_cfg_ptr () { return &_config ; }

static msgSock_type log_sock ;

/* Cleanup exit handler */
void daemon_exit ( void )
{
    fflush (stdout);
    fflush (stderr);

    if ( log_sock.sock > 0 )
    {
        close(log_sock.sock);
        log_sock.sock = 0 ;
    }

    exit (0);
}

/*******************************************************************
 *                   Module Utilities                              *
 ******************************************************************/
/* SIGCHLD handler support - for waitpid */
void daemon_sigchld_hdlr ( void )
{
    dlog("Received SIGCHLD ...\n");
}

/*****************************************************************************
 *
 * Name    : _config_handler
 *
 * Purpose : Read specified config options
 *
 *****************************************************************************/
static int _config_handler ( void * user, 
                             const char * section,
                             const char * name,
                             const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("client", "daemon_log_port"))
    {
        config_ptr->daemon_log_port = atoi(value);
        // ilog ("Log port = %d\n", atoi(value));
    }
    return (PASS);
}

/*****************************************************************************
 *
 * Name    : daemon_configure
 *
 * Purpose : Run the config handler against a specified config file.
 *
 *****************************************************************************/
int daemon_configure ( void )
{
    int rc = PASS ;

    if (ini_parse( MTCE_CONF_FILE, _config_handler, &_config) < 0)
    {
        elog("Can't load '%s'\n", MTCE_CONF_FILE );
    }

    get_debug_options ( MTCE_CONF_FILE, &_config );

    /* This ensures any link aggregation interface overrides the physical */
    _config.mgmnt_iface = daemon_get_iface_master  ( _config.mgmnt_iface );
    ilog("Mgmnt Iface : %s\n", _config.mgmnt_iface );

    bool waiting_msg = false ;
    for ( ;; ) 
    {
        get_ip_addresses ( my_hostname, my_local_ip , my_float_ip );
        if ( my_float_ip.empty() || my_local_ip.empty() )
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
    }

    ilog("Logger Port : %d\n", _config.daemon_log_port  );
    return (rc);
}


/****************************/
/* Initialization Utilities */
/****************************/

/* Setup the daemon messaging interfaces/sockets */
int socket_init ( void )
{
    int rc = FAIL ;
    int port = _config.daemon_log_port ;
    CREATE_NONBLOCK_INET_UDP_RX_SOCKET ( LOOPBACK_IP, 
                                         port, 
                                         log_sock.sock, 
                                         log_sock.addr, 
                                         log_sock.port, 
                                         log_sock.len, 
                                         "daemon log receiver", 
                                         rc );
    if ( rc )
    {
        printf ("Failed to setup the daemon log reciver port %d\n", _config.daemon_log_port );
    }
    return (rc);
}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodetype )
{
    int rc = PASS ;

    /* Not used by this daemon */
    UNUSED(nodetype);
    /* init the control struct */
    my_hostname  = "" ;
    my_local_ip  = "" ;
    my_float_ip  = "" ;

    /* Assign interface to config */
    _config.mgmnt_iface = (char*)iface.data() ;

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return ( FAIL_FILES_INIT ) ;
    }

    /* Bind signal handlers */
    if ( daemon_signal_init () != PASS )
    {
        elog ("daemon_signal_init failed\n");
        return ( FAIL_SIGNAL_INIT ) ;
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

    /* Configure the daemon */ 
    if ( (rc = daemon_configure ( )) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%i)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }

    /* Setup the messaging sockets */
    else if ( (rc = socket_init ( )) != PASS )
    {
        elog ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT ;
    }

    return (rc);
}

/* ****************************************************
 * Start the service
 * ****************************************************/
void daemon_service_run ( void )
{
    int rc    = PASS ;
    int count = 0    ;

    /* For select dispatch */ 
    struct timeval waitd  ;
    fd_set readfds;

    /* Wait for config complete indicated by presence
     * of /etc/platform/.initial_config_complete */
    struct stat p ;
    memset ( &p, 0 , sizeof(struct stat));
    do
    {
        stat (CONFIG_COMPLETE_FILE, &p);
        mtcWait_secs (2);
        wlog_throttled ( count, 60, "Waiting for %s\n", CONFIG_COMPLETE_FILE);

        /* The CONFIG_COMPLETE file may be empty so don't look at size,
         * look at the node and dev ids as non-zero instead */
    } while ((p.st_ino == 0 ) || (p.st_dev == 0)) ;

    /* Set umask for the log files that will be created */
    umask(027);

    /* Run daemon main loop */ 
    for ( ; ; )
    {
        if ( log_sock.sock <= 0 )
        {
           daemon_exit ();
        }
        
        /* Initialize the timeval struct to wait for 1 mSec */
        waitd.tv_sec  = 0;
        waitd.tv_usec = (SOCKET_WAIT*5);
        FD_ZERO(&readfds);
        FD_SET(log_sock.sock, &readfds);

        /* Call select() and wait only up to SOCKET_WAIT */
        rc = select( log_sock.sock+1, &readfds, NULL, NULL, &waitd);
        /* If the select time out expired then  */
        if (( rc < 0 ) || ( rc == 0 ))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                elog ("Select Failed (rc:%d) %s\n", errno, strerror(errno));
            }
        }
        else
        {
            if (FD_ISSET(log_sock.sock, &readfds))
            {
                int bytes ;
                log_message_type log ;
                unsigned int len = sizeof(log_sock.addr);


                /* Look for maintenance command messages */
                memset (&log, 0, sizeof(log_message_type));
                bytes = recvfrom ( log_sock.sock,
                               (char*)&log, sizeof(log_message_type), 0,
                               (struct sockaddr *) &log_sock.addr, &len );
                if ( bytes > 0 )
                {
                    if ( strnlen ( &log.hostname[0], MAX_HOST_NAME_SIZE ))
                    {
                        if ( strnlen ( &log.filename[0], MAX_FILENAME_LEN ))
                        {
                            if ( strnlen ( &log.logbuffer[0], MAX_LOG_MSG ))
                            {
                                char temp_buf [20] ;
                                strncpy ( temp_buf, &log.logbuffer[0], 19 );
                                temp_buf[19] = '\0' ;
                                dlog ("%s %s [%s]\n", &log.hostname[0], &log.filename[0], &temp_buf[0] );
                                daemon_log ( &log.filename[0], &log.logbuffer[0] );
                            }
                        }
                    }
                }
            }
        }
        daemon_signal_hdlr ();
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

/** Teat Head Entry */
int daemon_run_testhead ( void )
{
    ilog ("Empty test head.\n");
    return (PASS);
}
