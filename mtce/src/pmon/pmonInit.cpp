/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Process Monitor Service Header
  */

#include "pmon.h"
#include "alarmUtil.h"     /* for ... alarmUtil_getSev_str and alarmUtil_query_identity  */
#include "pmonAlarm.h"     /* for ... PMON_ALARM_ID__PMOND  */

/* Process Monitor Configuration File */
#define CONFIG_FILE   ((const char *)"/etc/mtc/pmond.conf")

/* Process Monitor Control Structure */
static pmon_ctrl_type pmon_ctrl ;
pmon_ctrl_type * get_ctrl_ptr ( void ) { return (&pmon_ctrl); }


/** Daemon Configuration Structure
 *  - Allocation and get pointer
 * @see daemon_common.h for daemon_config_type struct format. */
static daemon_config_type pmon_config ;
daemon_config_type * daemon_get_cfg_ptr () { return &pmon_config ; }

/* Cleanup exit handler */
void daemon_exit ( void )
{
    pmon_msg_fini ();
    pmon_hdlr_fini ( &pmon_ctrl );
    daemon_files_fini ();
    daemon_dump_info  ();
    exit (0);
}


/* Startup config read */
static int pmon_config_handler ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("config", "audit_period"))
    {
        config_ptr->audit_period = atoi(value);
        config_ptr->mask |= CONFIG_AUDIT_PERIOD ;
    }
    else if (MATCH("config", "pmon_cmd_port"))
    {
        config_ptr->pmon_cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_CMD_PORT ;
    }
    else if (MATCH("config", "pmon_event_port"))
    {
        config_ptr->pmon_event_port = atoi(value);
        config_ptr->mask |= CONFIG_TX_PORT ;
    }
    else if (MATCH("config", "pmon_amon_port"))
    {
        config_ptr->pmon_amon_port = atoi(value);
        config_ptr->mask |= CONFIG_RX_PORT ;
    }
    else if (MATCH("config", "pmon_pulse_port"))
    {
        config_ptr->pmon_pulse_port = atoi(value);
        config_ptr->mask |= CONFIG_PULSE_PORT ;
    }
    else if (MATCH("config", "audit_period"))
    {
        config_ptr->audit_period = atoi(value);
        config_ptr->mask |= CONFIG_AUDIT_PERIOD ;
    }
    else if (MATCH("config", "hostwd_update_period"))
    {
        config_ptr->hostwd_update_period = atoi(value);
        config_ptr->mask |= CONFIG_HOSTWD_PERIOD ;
    }
    else if (MATCH("timeouts", "start_delay"))
    {
        config_ptr->start_delay = atoi(value);
        config_ptr->mask |= CONFIG_START_DELAY ;
    }
    return (PASS);
}


/*****************************************************************************
 *
 * Name    : pmon_load_config
 *
 * Purpose : Read process config file settings into the daemon configuration
 *
 *****************************************************************************/
int pmon_process_config ( void * user,
                    const char * section,
                    const char * name,
                    const char * value)
{
    int rc = FAIL ;
    process_config_type * ptr = (process_config_type*)user;

    if (MATCH("process", "process"))
    {
        ptr->mask |= CONF_PROCESS ;
        ptr->status_mask |= CONF_PROCESS ;
        ptr->process = strdup(value);
        dlog1 ("Process    : %s\n", ptr->process );
        rc = PASS ;
    }
    if (MATCH("process", "service"))
    {
        ptr->mask |= CONF_RECOVERY ;
        ptr->service = strdup(value);
        dlog1 ("Service    : %s\n", ptr->service );
        rc = PASS ;
    }
    else if (MATCH("process", "script"))
    {
        ptr->mask |= CONF_RECOVERY ;
        ptr->status_mask |= CONF_RECOVERY ;
        ptr->script = strdup(value);
        dlog1 ("Script     : %s\n", ptr->script );
    }
    else if (MATCH("process", "style"))
    {
        ptr->mask |= CONF_STYLE ;
        ptr->status_mask |= CONF_STYLE ;
        ptr->style = strdup(value);
        dlog1 ("Style      : %s\n", ptr->style );
        rc = PASS ;
    }
    else if (MATCH("process", "pidfile"))
    {
        ptr->mask |= CONF_PIDFILE ;
        ptr->pidfile = strdup(value);
        dlog1 ("Pid File   : %s\n", ptr->pidfile );
        rc = PASS ;
    }
    else if (MATCH("process", "severity"))
    {
        ptr->mask |= CONF_SEVERITY ;
        ptr->status_mask |= CONF_SEVERITY  ;
        ptr->severity = strdup(value);
        dlog1 ("Severity   : %s\n", ptr->severity );
        rc = PASS ;
    }
    else if (MATCH("process", "restarts"))
    {
        ptr->mask |= CONF_RESTARTS ;
        ptr->status_mask |= CONF_RESTARTS  ;
        ptr->restarts = atoi(value);
        dlog1 ("Restarts   : %d\n", ptr->restarts );
        rc = PASS ;
    }
    else if (MATCH("process", "interval"))
    {
        ptr->mask |= CONF_INTERVAL ;
        ptr->status_mask |= CONF_INTERVAL  ;
        ptr->interval = atoi(value);
        dlog1 ("Interval   : %d\n", ptr->interval );
        rc = PASS ;
    }
    else if (MATCH("process", "debounce"))
    {
        ptr->mask |= CONF_DEBOUNCE ;
        ptr->debounce = atoi(value);
        dlog1 ("Debounce   : %d\n", ptr->debounce );
        rc = PASS ;
    }
    else if (MATCH("process", "startuptime"))
    {
        ptr->startuptime = atoi(value);
        dlog1 ("Debounce   : %d\n", ptr->startuptime );
        rc = PASS ;
    }
    else if (MATCH("process", "subfunction"))
    {
        ptr->subfunction = strdup(value);
        dlog1 ("Subfunction: %s\n", ptr->subfunction );
        rc = PASS ;
    }

    else if (MATCH("process", "mode"))
    {
        // ptr->mask |= CONF_MODE ;
        ptr->mode = strdup(value);
        if (( strcmp(ptr->mode, "active" )) &&
            ( strcmp(ptr->mode, "passive" )) &&
            ( strcmp(ptr->mode, "status" )))
        {
            ptr->ignore = true ;
            dlog1 ("Mode       : ignore\n");
        }
        else
        {
            dlog1 ("Mode       : %s\n", ptr->mode );
        }
        rc = PASS ;
    }
    else if (MATCH("process", "quorum"))
    {
        if (atoi(value) > 0)
        {
            ptr->quorum = true;
        }
        dlog1 ("Quorum     : %d\n", (int) ptr->quorum );
        rc = PASS ;
    }
    else if (MATCH("process", "full_init_reqd"))
    {
        if (atoi(value) > 0)
        {
            ptr->full_init_reqd = true;
        }
        dlog1 ("Full_init_reqd : %d\n", (int) ptr->quorum );
        rc = PASS ;
    }
    if (( ptr->mode != NULL ) && ( !strcmp(ptr->mode, "active" )))
    {
        if (MATCH("process", "port"))
        {
            ptr->amask |= CONF_PORT ;
            ptr->port = atoi(value);
            dlog1 ("Active Port: %d\n", ptr->port );
            rc = PASS ;
        }
        if (MATCH("process", "period"))
        {
            ptr->amask |= CONF_PERIOD ;
            ptr->period = atoi(value);
            dlog1 ("Period     : %d\n", ptr->period );
            rc = PASS ;
        }
        else if (MATCH("process", "timeout"))
        {
            ptr->amask |= CONF_TIMEOUT ;
            ptr->timeout = atoi(value);
            dlog1 ("Timeout    : %d\n", ptr->timeout );
            rc = PASS ;
        }
        else if (MATCH("process", "threshold"))
        {
            ptr->amask |= CONF_THRESHOLD ;
            ptr->threshold = atoi(value);
            dlog1 ("Threshold  : %d\n", ptr->threshold );
            rc = PASS ;
        }
    }

    if (( ptr->mode != NULL ) && ( !strcmp(ptr->mode, "status" )))
    {
        if (MATCH("process", "period"))
        {
            ptr->status_mask |= CONF_PERIOD ;
            ptr->period = atoi(value);
            dlog1 ("Period     : %d\n", ptr->period );
            rc = PASS ;
        }
        else if (MATCH("process", "timeout"))
        {
            ptr->status_mask |= CONF_TIMEOUT ;
            ptr->timeout = atoi(value);
            dlog1 ("Timeout    : %d\n", ptr->timeout );
            rc = PASS ;
        }
        else if (MATCH("process", "status_arg"))
        {
            ptr->status_mask |= CONF_STATUS_ARG ;
            ptr->status_arg = strdup(value);
            dlog1 ("status script argument     : %s\n", ptr->status_arg );
            rc = PASS ;
        }
        else if (MATCH("process", "start_arg"))
        {
            ptr->status_mask |= CONF_START_ARG ;
            ptr->start_arg = strdup(value);
            dlog1 ("start script argument     : %s\n", ptr->start_arg );
            rc = PASS ;
        }
        else if (MATCH("process", "status_failure_text"))
        {
            ptr->status_failure_text_file  = strdup(value);
            dlog1 ("Status error text file     : %s\n", ptr->status_failure_text_file);
            rc = PASS ;
        }
    }

    else
        rc = PASS ;

    return (rc);
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
    int rc = PASS ;

    if (ini_parse( CONFIG_FILE, pmon_config_handler, &pmon_config) < 0)
    {
        elog("Can't load '%s'\n", CONFIG_FILE );
    }

    get_debug_options ( CONFIG_FILE, &pmon_config );

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( pmon_config.mask != CONFIG_MASK )
    {
        elog ("Error: Agent configuration failed (%x)\n",
             ((-1 ^ pmon_config.mask) & CONFIG_MASK));
        return (FAIL_INI_CONFIG);
    }

    /* This ensures any link aggregation interface overrides the physical */
    pmon_config.mgmnt_iface = daemon_get_iface_master ( pmon_config.mgmnt_iface );

    /* Log the startup settings */
    ilog("Interface   : %s\n", pmon_config.mgmnt_iface );
    ilog("Event Port  : %d\n", pmon_config.pmon_event_port );

    get_iface_macaddr  ( pmon_config.mgmnt_iface,  pmon_ctrl.my_macaddr );
    get_iface_address  ( pmon_config.mgmnt_iface,  pmon_ctrl.my_address, true );
    get_hostname       (&pmon_ctrl.my_hostname[0], MAX_HOST_NAME_SIZE );

    /* Manage the daemon pulse period setting - ensure in bound values */
    if ( pmon_config.audit_period < PMON_MIN_AUDIT_PERIOD )
    {
        wlog ("Pulse Period: %d msecs (rounded up)\n",
               PMON_MIN_AUDIT_PERIOD );
        pmon_ctrl.pulse_period = PMON_MIN_AUDIT_PERIOD ;
    }
    else if ( pmon_config.audit_period > PMON_MAX_AUDIT_PERIOD )
    {
        wlog ("Pulse Period: %d msecs (rounded down)\n",
               PMON_MAX_AUDIT_PERIOD );
        pmon_ctrl.pulse_period = PMON_MAX_AUDIT_PERIOD ;
    }
    else
    {
        pmon_ctrl.pulse_period = pmon_config.audit_period ;
        ilog("Pulse Period: %d\n", pmon_ctrl.pulse_period );
    }

    /* Manage the daemon pulse period setting - ensure in bound values */
    if ( pmon_config.start_delay < PMON_MIN_START_DELAY )
    {
        wlog ("Start Delay : %d msecs (rounded up)\n",
               PMON_MIN_AUDIT_PERIOD );
        pmon_config.start_delay = PMON_MIN_START_DELAY ;
    }
    else if ( pmon_config.start_delay > PMON_MAX_START_DELAY )
    {
        wlog ("Start Delay : %d msecs (rounded down)\n",
               PMON_MAX_AUDIT_PERIOD );
        pmon_config.start_delay = PMON_MAX_START_DELAY ;
    }
    else
    {
        ilog("Start Delay : %d\n", pmon_config.start_delay );
    }

    if ( (rc = pmon_hdlr_init (&pmon_ctrl)) != PASS )
    {
        elog ("pmon_hdlt_init failed\n");
        rc = FAIL_HDLR_INIT ;
    }
    ilog ("Function    : %d\n", pmon_ctrl.function    );
    ilog ("SubFunction : %d\n", pmon_ctrl.subfunction );

    pmon_ctrl.reload_config = true ;
    pmon_ctrl.patching_in_progress = false ;

    return (rc);
}


/****************************/
/* Initialization Utilities */
/****************************/

/* Setup the daemon messaging interfaces/sockets */
int socket_init ( void )
{
    pmon_msg_init ( );

    /* Setup the pmon event port.
     * This is the port that pmon sends events
     * to maintenance on */
    int rc = event_port_init ( pmon_config.mgmnt_iface ,
                               pmon_config.pmon_event_port );

    /* Setup the pmon autonomout pulse port.
     * This is the port that pmon sends i'm alive messages
     * to the hbsClient - the watcher of the watcher */
    if ( rc == PASS )
    {
        rc = pulse_port_init ( );
    }

    /* Init the avtive monitor receive port.
     * This is the port that all active monitored
     * processes send their responses on */
    if ( rc == PASS )
    {
        rc = amon_port_init ( pmon_config.pmon_amon_port );
    }

    /* Setup the pmon hostwd connection.
     * This lets pmon commuicate essential process info to the
     * host watchdog process */
    if ( rc == PASS )
    {
        hostwd_port_init ( );
    }

    pmon_inbox_init ( );

    return (rc);
}

/* The main heartbeat service loop */
int daemon_init ( string iface, string nodetype_str )
{
    int rc = PASS ;

    /* init the control struct */
    memset ( &pmon_ctrl.my_hostname[0], 0, sizeof(pmon_ctrl.my_hostname));
    pmon_ctrl.my_macaddr   = "" ;
    pmon_ctrl.my_address   = "" ;
    pmon_ctrl.pulse_period = PMON_MAX_AUDIT_PERIOD ;
    pmon_ctrl.processes    = 0  ;
    pmon_ctrl.system_type  = daemon_system_type ();

    /* sets in pmonHdlr.cpp */
    pmon_set_ctrl_ptr ( &pmon_ctrl );

    pmonAlarm_init ();

    /* Assign interface to config */
    pmon_config.mgmnt_iface = (char*)iface.data() ;

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return ( FAIL_FILES_INIT ) ;
    }

    /* Bind signal handlers */
    if ( daemon_signal_init () != PASS )
    {
        elog ("daemon_signal_init failed\n");
        return ( FAIL_SIGNAL_INIT );
    }

    if ( set_host_functions ( nodetype_str, &pmon_ctrl.nodetype, &pmon_ctrl.function, &pmon_ctrl.subfunction ) != PASS )
    {
        elog ("failed to extract nodetype\n");
        return ( FAIL_NODETYPE );
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
    else
    {
        /* Init the pmon service timers */
        pmon_timer_init ();
    }

    pmon_ctrl.recovery_method = PMOND_RECOVERY_METHOD__SYSTEMD ;
    pmon_ctrl.system_state = get_system_state();
    ilog ("Recovery Method: %s\n", pmon_ctrl.recovery_method ? "systemd via systemctl" : "sysvinit via script" );
    return (rc);
}

/* Start the service
 *
 *   1. Wait for host config (install) complete
 *   2. Wait for goenable
 *   3. Do startup delay
 *   4. run the pmon service inside pmonHdlr.cpp
 *
 */
void daemon_service_run ( void )
{
    int rc     = PASS  ;

    process_config_type dummy_process ;
    memset ( (char*)&dummy_process, 0, (sizeof(process_config_type)));
    dummy_process.process = strdup("pmond");

    ilog ("Transmitting: 'monitor ready event'\n" );
    do
    {
        rc = pmon_send_event ( MTC_EVENT_MONITOR_READY, &dummy_process ) ;
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

    /* Wait a few seconds after go enabled to
     * allow the rest of init to finish before
     * starting to process monitor */
    ilog ("Delaying %d seconds to allow other processes to start\n", pmon_config.start_delay);
    for ( int i = 0 ; i < pmon_config.start_delay ; i++ )
    {
        mtcWait_secs ( 1 );
        pmon_send_pulse ( );
    }

    pmon_service ( &pmon_ctrl );
    daemon_exit ();
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
