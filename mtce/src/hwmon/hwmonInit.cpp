/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Hardware Monitor Service Header
  */


#include "hwmon.h"         /* for ... service module header          */
#include "httpUtil.h"      /* for ... httpUtil_init                  */
#include "tokenUtil.h"     /* for ... tokenUtil_new_token            */
#include "threadUtil.h"    /* for ... common pthread support         */
#include "hwmonClass.h"    /* for ... get_hwmonHostClass_ptr         */
#include "hwmonHttp.h"     /* for ... hwmonHttp_server_fini          */
#include "tokenUtil.h"     /* for ... keystone_config_handler        */

/* Process Monitor Control Structure */
static hwmon_ctrl_type hwmon_ctrl ;
hwmon_ctrl_type * get_ctrl_ptr ( void ) { return(&hwmon_ctrl) ; }

/** Daemon Configuration Structure
 *  - Allocation and get pointer
 * @see daemon_common.h for daemon_config_type struct format. */
static daemon_config_type hwmon_config ;
daemon_config_type * daemon_get_cfg_ptr () { return &hwmon_config ; }

/* Cleanup exit handler */
void daemon_exit ( void )
{
    hwmonHttp_server_fini ();

    threadUtil_fini () ;

    hwmon_msg_fini ();
    hwmon_hdlr_fini ( &hwmon_ctrl );

    daemon_files_fini ();
    daemon_dump_info  ();

    exit (0);
}


/* Startup config read */
static int hwmon_config_handler ( void * user, 
                            const char * section,
                            const char * name,
                            const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("config", "audit_period"))
    {
        config_ptr->audit_period = atoi(value);
        config_ptr->mask |= CONFIG_AUDIT_PERIOD ;
        ilog("Audit Period      : %d secs\n", config_ptr->audit_period );
        hwmon_ctrl.audit_period = hwmon_config.audit_period ;
    }
    else if (MATCH("config", "event_port"))
    {
        config_ptr->event_port = atoi(value);
        config_ptr->mask |= CONFIG_EVENT_PORT ;
        ilog("Mtce Event Port   : %d (rx)\n", config_ptr->event_port );
    }
    else if (MATCH("config", "inv_event_port"))
    {
        config_ptr->inv_event_port = atoi(value);
        config_ptr->mask |= CONFIG_INV_EVENT_PORT ;
        ilog("SysInv Event Port : %d (rx)\n", config_ptr->inv_event_port );
    }
    else if (MATCH("config", "cmd_port"))
    {
        config_ptr->cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_CMD_PORT ;
        ilog("Mtce Command Port : %d (rx)\n", config_ptr->cmd_port );
    }
    return (PASS);
}

/* mtc.ini config file read - for the keystone url */
static int mtc_config_handler ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "keystone_port"))
    {
        config_ptr->keystone_port = atoi(value);
        config_ptr->mask |= CONFIG_KEYSTONE_PORT ;
    }
    else if (MATCH("agent", "token_refresh_rate"))
    {
        config_ptr->token_refresh_rate = atoi(value);
        config_ptr->mask |= CONFIG_TOKEN_REFRESH ;
    }
    else if (MATCH("client", "daemon_log_port"))
    {
        config_ptr->daemon_log_port = atoi(value);
        ilog("mtclogd port: %d (tx)\n", config_ptr->daemon_log_port );
    }

   return (PASS);
}

/*****************************************************************************
 *
 * Name    : daemon_configure
 *
 * Purpose : Read process config file settings into the daemon configuration
 *
 *****************************************************************************/
int daemon_configure ( void )
{
    bool waiting_msg = false ;

    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr();

    /* Read the ini and config files but start off with a cleared flag mask */
    hwmon_config.mask = 0 ;

    get_debug_options ( HWMON_CONF_FILE, &hwmon_config );

    if (ini_parse(MTCE_CONF_FILE, mtc_config_handler, &hwmon_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(MTCE_INI_FILE, keystone_config_handler, &hwmon_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_INI_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse( HWMON_CONF_FILE, hwmon_config_handler, &hwmon_config) < 0)
    {
        elog("Can't load '%s'\n", HWMON_CONF_FILE );
        return ( FAIL_LOAD_INI );
    }

    if (ini_parse(SYSINV_CFG_FILE, sysinv_config_handler, &hwmon_config) < 0)
    {
        elog ("Can't load '%s'\n", SYSINV_CFG_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(SECRET_CFG_FILE, barbican_config_handler, &hwmon_config) < 0)
    {
        elog ("Can't load '%s'\n", SECRET_CFG_FILE );
        return (FAIL_LOAD_INI);
    }

    /* tell the host service that there has been a config reload */
    obj_ptr->config_reload = true ;

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( hwmon_config.mask != CONFIG_MASK )
    {
        elog ("Daemon ini configuration failed (0x%x)\n",
             ((-1 ^ hwmon_config.mask) & CONFIG_MASK));
        return(FAIL_INI_CONFIG) ;
    }

    /* No bigger than every 8 hours - that's all that has been tested */
    if ( hwmon_config.token_refresh_rate > MTC_HRS_8 )
    {
        wlog ("Token refresh rate rounded down to 8 hour maximum\n");
        hwmon_config.token_refresh_rate = MTC_HRS_8 ;
    }

    /* This ensures any link aggregation interface overrides the physical */
    hwmon_config.mgmnt_iface =
    daemon_get_iface_master  ( hwmon_config.mgmnt_iface );
    ilog("Mgmnt Iface : %s\n", hwmon_config.mgmnt_iface );

    get_iface_macaddr  ( hwmon_config.mgmnt_iface,  hwmon_ctrl.my_macaddr   );
    // get_iface_address  ( hwmon_config.mgmnt_iface,  hwmon_ctrl.my_address   );

    do
    {
        get_ip_addresses ( hwmon_ctrl.my_hostname, hwmon_ctrl.my_local_ip , hwmon_ctrl.my_float_ip );
        if ( hwmon_ctrl.my_float_ip.empty() )
        {
            if ( waiting_msg == false )
            {
                ilog ("Waiting on ip address config ...\n");
                waiting_msg = true ;
            }
            daemon_signal_hdlr ();

            mtcWait_secs (2);
        }
    } while ( hwmon_ctrl.my_float_ip.empty() );


    /* remove any existing fit */
    daemon_init_fit ();

    return (PASS);
}


/****************************/
/* Initialization Utilities */
/****************************/

/* Setup the daemon messaging interfaces/sockets */
int socket_init ( void )
{
    int rc ;
    hwmon_msg_init ( );

    /* Setup the hwmon event port. This is the port
     * that hwmon sends events to maintenance on */
    rc = event_tx_port_init ( hwmon_config.event_port,
                              hwmon_config.mgmnt_iface);

    if ( rc == PASS )
    {
        /* ... and now the command receive port */
        rc = cmd_rx_port_init ( hwmon_config.cmd_port );
        if ( rc == PASS )
        {
            /* setup http server to receive sensor model change requests
             * from system inventory */
            rc = hwmonHttp_server_init ( hwmon_config.inv_event_port );

            /* Don't fail the daemon if the logger port is not working */
            mtclogd_tx_port_init ();
        }
    }
    return (rc);
}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodetype )
{
    int rc = PASS ;

    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr();

    /* Not used by this daemon */
    UNUSED(nodetype) ;

    hwmon_hdlr_init ( &hwmon_ctrl );
    hwmon_stages_init ();
    httpUtil_init ();
    bmcUtil_init();

    /* init the control struct */
    hwmon_ctrl.my_hostname  = "" ;
    hwmon_ctrl.my_macaddr   = "" ;
    hwmon_ctrl.my_local_ip  = "" ;
    hwmon_ctrl.my_float_ip  = "" ;

    /* Assign interface to config */
    hwmon_config.mgmnt_iface = (char*)iface.data() ;

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return ( FAIL_FILES_INIT ) ;
    }

    obj_ptr->system_type = daemon_system_type ();

    threadUtil_init ( hwmonTimer_handler ) ;

    /* Bind signal handlers */
    if ( daemon_signal_init () != PASS )
    {
        elog ("daemon_signal_init failed\n");
        rc = FAIL_SIGNAL_INIT ;
    }

    /* Configure the daemon */
    else if ( (rc = daemon_configure ( )) != PASS )
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

    threadUtil_init ( hwmonTimer_handler ) ;

    /* override the config reload for the startup case */
    obj_ptr->config_reload = false ;

    /* Init the hwmon service timers */
    hwmon_timer_init ();


#ifdef WANT_FIT_TESTING
    daemon_make_dir(FIT__INFO_FILEPATH);
#endif

    return (rc);
}


/* ****************************************************
 * Start the service
 * ****************************************************
 *
 * 1. Wait for config_complete
 * 2. Wait for GOEnable Ready
 * 3. Send Service Ready
 *
 * Note: Service must be started by Mtce command ;
 *       Ready does not imply auto start like PMON
 *
 * ****************************************************/
void daemon_service_run ( void )
{
    int rc    = PASS ;
    int count = 0    ;

    /* Wait for config complete indicated by presence
     * of /etc/platform/.initial_config_complete */
    struct stat p ;
    memset ( &p, 0 , sizeof(struct stat));
    do
    {
        stat (CONFIG_COMPLETE_FILE, &p);
        mtcWait_secs (2);
        ilog_throttled ( count, 60, "Waiting for %s\n", CONFIG_COMPLETE_FILE);

        /* The CONFIG_COMPLETE file may be empty so don't look at size,
         * look at the node and dev ids as non-zero instead */
    } while ((p.st_ino == 0 ) || (p.st_dev == 0)) ;

    count = 0 ;

    /* Waiting for goenabled signal indicated by the presence of
     * the GOENABLED_MAIN_PASS  and then send HWMOND READY message */
    memset ( &p, 0 , sizeof(struct stat));
    for ( ; ; )
    {
        stat ( GOENABLED_MAIN_READY, &p ) ;
        if ( p.st_size )
        {
            ilog ("Transmitting: Monitor READY Event\n" );
            do
            {
                rc = hwmon_send_event ( hwmon_ctrl.my_hostname, MTC_EVENT_MONITOR_READY, "" );
                if ( rc == RETRY )
                {
                    mtcWait_secs ( 2 );
                }
                if ( rc == FAIL )
                {
                    elog ("Failed to Send READY event (rc=%d)\n", rc );
                    elog ("Trying to provide service anyway\n");
                }
            } while ( rc == RETRY ) ;
            break ;
        }
        else
        {
            wlog_throttled ( count, 60, "Waiting for 'goenabled' signal ...\n");
            mtcWait_secs (2);
        }
    }

    /* Get the initial token.
     * This call does not return until a token is received */
    tokenUtil_get_first ( hwmon_ctrl.httpEvent, hwmon_ctrl.my_hostname );

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__CORRUPT_TOKEN ))
        tokenUtil_fail_token ();
#endif

    /* enable the base level signal handler latency monitor */
    daemon_latency_monitor (true);

    /* get activity state */
    hwmon_ctrl.active = daemon_get_run_option ("active") ;
    hwmon_service ( &hwmon_ctrl );
    daemon_exit ();
}


/* Push daemon state to log file */
void daemon_dump_info ( void )
{
    daemon_dump_membuf_banner ();

    get_hwmonHostClass_ptr()->memDumpAllState ();

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
    ilog ("Empty test head.\n");
    return (PASS);
}
