/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Alarm Manager Daemon Initialization
  */

#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>       /* for close and usleep */

using namespace std;

#define __MODULE_PRIVATE__

#include "daemon_ini.h"    /* Ini Parser Header                            */
#include "daemon_common.h" /* Common definitions and types for daemons     */
#include "daemon_option.h" /* Common options  for daemons                  */

#include "alarm.h"         /* module header                                */
#include "msgClass.h"      /* for ... socket message setup                 */

/** Local Identity */
static string my_hostname = "" ;
static string my_local_ip = "" ;
static string my_float_ip = "" ;


/** Maintenance Alarm request socket and port - UDP over lo */
msgClassSock * mtcalarm_req_sock_ptr = NULL ;
int            mtcalarm_req_port = 0    ;


/** Common Daemon Config Struct */
static daemon_config_type _config ;
daemon_config_type * daemon_get_cfg_ptr () { return &_config ; }


/* Cleanup exit handler */
void daemon_exit ( void )
{
    daemon_dump_info  ();
    daemon_files_fini ();

    /* Close sockets */
    if ( mtcalarm_req_sock_ptr )
    {
        delete (mtcalarm_req_sock_ptr );
        mtcalarm_req_sock_ptr = NULL ;
        mtcalarm_req_port = 0 ;
    }
    exit (0);
}


/** Client Config mask */
#define CONFIG_CLIENT_MASK      (CONFIG_CLIENT_PULSE_PORT)

/* Startup config read */
static int _config_handler (       void * user,
                             const char * section,
                             const char * name,
                             const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("client", "mtcalarm_req_port"))
    {
        config_ptr->mtcalarm_req_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_PULSE_PORT ;
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
}

/* Configure the daemon */
int daemon_configure ( void )
{
    int rc = FAIL ;

    if (ini_parse(MTCE_CONF_FILE, _config_handler, &_config) < 0)
    {
        elog("Failed to load '%s'\n", MTCE_CONF_FILE );
        return(FAIL_LOAD_INI);
    }

    get_debug_options ( MTCE_CONF_FILE, &_config );

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( _config.mask != CONFIG_CLIENT_MASK )
    {
        elog ("Client configuration failed (%x)\n",
             (( -1 ^ _config.mask ) & CONFIG_CLIENT_MASK) );
        rc = FAIL_INI_CONFIG ;
    }
    else
    {
        ilog("Alarm Port  : %d\n", _config.mtcalarm_req_port );
        rc = PASS ;
    }

    return (rc);
}

/****************************/
/* Initialization Utilities */
/****************************/

int daemon_socket_init ( void )
{
    int    rc = PASS ;

    /***********************************************************/
    /* Setup the Alarm Request Receiver Socket                 */
    /***********************************************************/

    mtcalarm_req_sock_ptr = new msgClassRx ( LOOPBACK_IP, _config.mtcalarm_req_port, IPPROTO_UDP);
    if (rc)
        return (rc) ;
    if (mtcalarm_req_sock_ptr)
        mtcalarm_req_sock_ptr->sock_ok(true);

    return (rc);
}


/* The main heartbeat service loop */
int daemon_init ( string iface, string nodeType_str )
{
    int rc = PASS ;
    UNUSED(nodeType_str);

    /* Assign interface to config */
    _config.mgmnt_iface = (char*)iface.data() ;

    if ((rc = daemon_files_init ()) != PASS)
    {
       elog ("daemon_files_init failed (rc:%d)\n", rc );
       return ( FAIL_FILES_INIT );
    }

    /* Bind signal handlers */
    if ((rc = daemon_signal_init ()) != PASS)
    {
       elog ("daemon_signal_init failed (rc:%d)\n", rc );
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
    if ((rc = daemon_configure ()) != PASS)
    {
       elog ("daemon_configure failed (rc:%d)\n", rc );
       rc = FAIL_DAEMON_CONFIG ;
    }

    /* Setup messaging sockets */
    else if ((rc = daemon_socket_init ()) != PASS)
    {
       elog ("daemon_socket_init failed (rc:%d)\n", rc );
       rc = FAIL_SOCKET_INIT;
    }

    alarmData_init ();

    return (rc);
}

void daemon_service_run ( void )
{
   int rc = PASS ;

#ifdef WANT_FIT_TESTING
   daemon_init_fit ();
#endif

   alarmMgr_queue_clear();

   if (( mtcalarm_req_sock_ptr ) && ( mtcalarm_req_sock_ptr->getFD() ))
   {
      std::list<int> socks ;

      /* For select dispatch */
      struct timeval waitd  ;
              fd_set readfds;

      int failed_receiver_log_throttle = 0 ;
      int failed_receiver_b2b_count    = 0 ;
      int failed_socket_log_throttle   = 0 ;

      socks.clear();
      socks.push_front (mtcalarm_req_sock_ptr->getFD());
      socks.sort();

      /* Run service forever */
      for ( ;  ; )
      {
         daemon_signal_hdlr ();
         waitd.tv_sec = 0;
         waitd.tv_usec = SOCKET_WAIT_100MS;

         /* Initialize the master fd_set */
         FD_ZERO(&readfds);
         FD_SET( mtcalarm_req_sock_ptr->getFD(), &readfds);
         rc = select( socks.back()+1, &readfds, NULL, NULL, &waitd);
         if (( rc < 0 ) || ( rc == 0 ))
         {
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                wlog_throttled ( failed_socket_log_throttle, 100,
                                 "Socket Select Failed (%d:%m)\n", errno);
            }
         }

         if ( FD_ISSET(mtcalarm_req_sock_ptr->getFD(), &readfds))
         {
            failed_socket_log_throttle = 0 ;
            if ( mtcalarm_req_sock_ptr && ( mtcalarm_req_sock_ptr->sock_ok() == true ))
            {
               char msg [MAX_ALARM_REQ_SIZE] ;
               memset ( &msg , 0, MAX_ALARM_REQ_MSG_SIZE );
               int bytes = mtcalarm_req_sock_ptr->read((char*)&msg, MAX_ALARM_REQ_SIZE-1 );
               if ( bytes > 0 )
               {
                  failed_receiver_b2b_count = 0 ;
                  failed_receiver_log_throttle = 0 ;
                  if ( ( rc = alarmHdlr_request_handler ( msg )) != PASS )
                  {
                     wlog ("failed to handle alarm request (rc:%d)\n", rc );
                  }
               }
               else if ( bytes < 0 )
               {
                  failed_receiver_b2b_count++ ;
                  wlog_throttled ( failed_receiver_log_throttle, 20, "alarm request receive error ; thresholeded ; (%d:%m)\n", errno );
               }
               else
               {
                  failed_receiver_b2b_count++ ;
                  wlog_throttled ( failed_receiver_log_throttle, 20, "alarm request receive ; no data\n" );
               }
            }
            else
            {
               elog ("alarm request socket error ; fatal\n");
               failed_receiver_b2b_count = MAX_FAILED_B2B_RECEIVES_B4_RESTART ;
            }

            if ( failed_receiver_b2b_count >= MAX_FAILED_B2B_RECEIVES_B4_RESTART )
            {
               /* exit and allow process restart by pmond */
               elog ("max (%d) alarm request receive errors reached ; forcing process restart\n", MAX_FAILED_B2B_RECEIVES_B4_RESTART );
               break ;
            }
         }

#ifdef WANT_FIT_TESTING
         daemon_load_fit();
#endif

         alarmMgr_service_queue();
      }
   }
   else
   {
      elog ("alarm request socket error ; not initialized ; exiting\n");
   }
   daemon_exit();
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
