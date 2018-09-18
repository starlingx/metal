/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Resource Monitor Service Initialization
  */

#include "rmon.h"

/* File definitions */
#define CONFIG_FILE   ((const char *)"/etc/mtc/rmond.conf")


static rmon_ctrl_type rmon_ctrl ;
rmon_ctrl_type * get_ctrlPtr ( void ) ;

static daemon_config_type rmon_config ;
daemon_config_type * daemon_get_cfg_ptr () { return &rmon_config ; }

/* Cleanup exit handler */
void daemon_exit ( void )
{
    rmon_msg_fini ();
    rmon_hdlr_fini ( &rmon_ctrl );
    daemon_dump_info  ();
    daemon_files_fini ();
    exit (0);
}

bool is_compute ( void )
{
    if (( rmon_ctrl.function == COMPUTE_TYPE ) && ( rmon_ctrl.subfunction == CGTS_NODE_NULL ))
        return (true);
    else
        return (false);
}

bool is_controller ( void )
{
    if ( rmon_ctrl.function == CONTROLLER_TYPE )
        return (true);
    else
        return (false);
}

bool is_cpe ( void )
{
    if (( rmon_ctrl.function == CONTROLLER_TYPE ) && ( rmon_ctrl.subfunction == COMPUTE_TYPE ))
        return (true);
    else
        return (false);
}

/*****************************************************************************
 *
 * Name    : rmon_config_handler
 *
 * Purpose : Startup config read from file: rmond.conf
 *
 *****************************************************************************/
static int rmon_config_handler ( void * user,
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
    else if (MATCH("config", "pm_period"))
    {
        config_ptr->pm_period = atoi(value);
        config_ptr->mask |= PM_AUDIT_PERIOD ;
    }
    else if (MATCH("config", "ntp_audit_period"))
    {
        config_ptr->ntp_audit_period = atoi(value);
        config_ptr->mask |= NTP_AUDIT_PERIOD ;
    }
    else if (MATCH("config", "ntpq_cmd_timeout"))
    {
        config_ptr->ntpq_cmd_timeout = atoi(value);
        config_ptr->mask |= NTPQ_CMD_TIMEOUT ;
    }
    else if (MATCH("config", "rmon_tx_port"))
    {
        config_ptr->rmon_tx_port = atoi(value);
        config_ptr->mask |= CONFIG_TX_PORT ;
    }
    else if (MATCH("config", "per_node"))
    {
        config_ptr->per_node = atoi(value);
        config_ptr->mask |= CONFIG_NODE ;
    }
    else if (MATCH("timeouts", "start_delay"))
    {
        config_ptr->start_delay = atoi(value);
        config_ptr->mask |= CONFIG_START_DELAY ;
    }
    else if (MATCH("config", "rmon_api_tx_port"))
    {
        config_ptr->rmon_api_tx_port = atoi(value);
        config_ptr->mask |= CONFIG_TX_PORT ;
    }
    else if (MATCH("config", "critical_threshold"))
    {
        config_ptr->rmon_critical_thr = atoi(value);
        config_ptr->mask |= CONFIG_CRITICAL_THR ;
    }
    else if (MATCH("config", "log_step"))
    {
        config_ptr->log_step = atoi(value);
    }
    return (PASS);
}

/*****************************************************************************
 *
 * Name    : rmon_interface_config
 *
 * Purpose : Read interface resource config file settings into the daemon configuration
 *
 *****************************************************************************/
int rmon_interface_config ( void * user,
                    const char * section,
                    const char * name,
                    const char * value)
{
    int rc = FAIL ;
    interface_resource_config_type * ptr = (interface_resource_config_type*)user;

    if (MATCH("resource", "resource"))
    {
        ptr->mask |=  CONF_RESOURCE ;
        ptr->resource = strdup(value);
        dlog ("Resource    : %s\n", ptr->resource);
        rc = PASS ;
    }
    else if (MATCH("resource", "severity"))
    {
        ptr->mask |= CONF_SEVERITY ;
        ptr->severity = strdup(value);
        dlog ("Severity   : %s\n", ptr->severity );
        rc = PASS ;
    }
    else if (MATCH("resource", "debounce"))
    {
        /* A zero value prevents degrade accompanying any alarm */
        ptr->mask |= CONF_DEBOUNCE ;
        ptr->debounce = atoi(value);
        dlog ("Debounce   : %d\n", ptr->debounce );
        rc = PASS ;
    }
    else if (MATCH("resource", "num_tries"))
    {
        ptr->num_tries = atoi(value);
        dlog ("Number of Tries   : %d\n", ptr->num_tries );
        rc = PASS ;
    }
    else if (MATCH("resource", "alarm_on"))
    {
        ptr->alarm_status= atoi(value);
        dlog ("Resource Alarm Status   : %d\n", ptr->alarm_status);
        rc = PASS ;
    }


    return (rc);
}

/*****************************************************************************
 *
 * Name    : rmon_thinmeta_config
 *
 * Purpose : Read resource config file settings into the daemon configuration
 *
 *****************************************************************************/
int rmon_thinmeta_config ( void * user,
                         const char * section,
                         const char * name,
                         const char * value)
{
    int rc = FAIL ;
    thinmeta_resource_config_type * ptr = (thinmeta_resource_config_type*)user;

    if(strcmp(section, "thinpool_metadata") == 0)
    {
        // This configuration item has the thinpool metadata section
        ptr->section_exists = true;
    }

    if (MATCH(THINMETA_CONFIG_SECTION, "vg_name"))
    {
        ptr->vg_name = strdup(value);
        dlog ("Thinpool VG Name                 : %s\n", ptr->vg_name);
        rc = PASS ;
    }
    else if (MATCH(THINMETA_CONFIG_SECTION, "thinpool_name"))
    {
        ptr->thinpool_name = strdup(value);
        dlog ("Thinpool Thinpool Name           : %s\n", ptr->thinpool_name);
        rc = PASS ;
    }
    else if (MATCH(THINMETA_CONFIG_SECTION, "critical_threshold"))
    {
       ptr->critical_threshold = atoi(value);
       dlog ("Thinpool Critical Alarm Threshold : %d%%\n", ptr->critical_threshold);
       rc = PASS ;
    }
    else if (MATCH(THINMETA_CONFIG_SECTION, "alarm_on"))
    {
       ptr->alarm_on = atoi(value);
       dlog ("Thinpool Metadata alarm_on        : %s\n", ptr->alarm_on? "On": "Off");
       rc = PASS ;
    }
    else if (MATCH(THINMETA_CONFIG_SECTION, "autoextend_on"))
    {
       ptr->autoextend_on = atoi(value);
       dlog ("Thinpool Metadata autoextend      : %s\n", ptr->autoextend_on? "On": "Off");
       rc = PASS ;
    }
    else if (MATCH(THINMETA_CONFIG_SECTION, "autoexent_by"))
    {
       ptr->autoextend_by = atoi(value);
       dlog ("Metadata Autoextend by            : %d\n", ptr->autoextend_by);
       rc = PASS ;
    }
    else if (MATCH(THINMETA_CONFIG_SECTION, "autoextend_percent"))
    {
       ptr->autoextend_percent = atoi(value);
       dlog ("Thinpool Metadata Autoextend by   : %s\n",
             ptr->autoextend_percent? "percents": "absolute value (MiB)");
       rc = PASS ;
    }
    else if (MATCH(THINMETA_CONFIG_SECTION, "audit_period"))
    {
       ptr->audit_period = atoi(value);
       dlog ("Metadata Audit Period             : %ds\n", ptr->audit_period);
       rc = PASS ;
    }

    return (rc);
}

/*****************************************************************************
 *
 * Name    : rmon_resource_config
 *
 * Purpose : Read resource config file settings into the daemon configuration
 *
 *****************************************************************************/
int rmon_resource_config ( void * user,
                    const char * section,
                    const char * name,
                    const char * value)
{
    int rc = FAIL ;
    resource_config_type * ptr = (resource_config_type*)user;

    if (MATCH("resource", "resource"))
    {
        ptr->mask |=  CONF_RESOURCE ;
        ptr->resource = strdup(value);
        dlog ("Resource    : %s\n", ptr->resource);
        rc = PASS ;
    }
    else if (MATCH("resource", "severity"))
    {
        ptr->mask |= CONF_SEVERITY ;
        ptr->severity = strdup(value);
        dlog ("Severity   : %s\n", ptr->severity );
        rc = PASS ;
    }
    else if (MATCH("resource", "debounce"))
    {
        ptr->mask |= CONF_DEBOUNCE ;
        ptr->debounce = atoi(value);
        dlog ("Debounce   : %d\n", ptr->debounce );
        rc = PASS ;
    }
    else if (MATCH("resource", "minor_threshold"))
    {
        ptr->minor_threshold = atoi(value);
        dlog ("Minor Threshold   : %d\n", ptr->minor_threshold );
        rc = PASS ;
    }
    else if (MATCH("resource", "major_threshold"))
    {
        ptr->major_threshold = atoi(value);
        dlog ("Major Threshold   : %d\n", ptr->major_threshold );
        rc = PASS ;
    }
    else if (MATCH("resource", "critical_threshold"))
    {
        ptr->critical_threshold = atoi(value);
        dlog ("Critical Threshold   : %d\n", ptr->critical_threshold );
        rc = PASS ;
    }
    else if (MATCH("resource", "minor_threshold_abs_node0"))
    {
        ptr->minor_threshold_abs_node0 = atoi(value);
        dlog ("Minor Threshold Absolute Node 0   : %d\n", ptr->minor_threshold_abs_node0 );
        rc = PASS ;
    }
    else if (MATCH("resource", "major_threshold_abs_node0"))
    {
        ptr->major_threshold_abs_node0 = atoi(value);
        dlog ("Major Threshold Absolute Node 0   : %d\n", ptr->major_threshold_abs_node0 );
        rc = PASS ;
    }
    else if (MATCH("resource", "critical_threshold_abs_node0"))
    {
        ptr->critical_threshold_abs_node0 = atoi(value);
        dlog ("Critical Threshold  Absolute Node 0  : %d\n", ptr->critical_threshold_abs_node0 );
        rc = PASS ;
    }
    else if (MATCH("resource", "minor_threshold_abs_node1"))
    {
        ptr->minor_threshold_abs_node1 = atoi(value);
        dlog ("Minor Threshold Absolute Node 1   : %d\n", ptr->minor_threshold_abs_node1 );
        rc = PASS ;
    }
    else if (MATCH("resource", "major_threshold_abs_node1"))
    {
        ptr->major_threshold_abs_node1 = atoi(value);
        dlog ("Major Threshold Absolute Node 1   : %d\n", ptr->major_threshold_abs_node1 );
        rc = PASS ;
    }
    else if (MATCH("resource", "critical_threshold_abs_node1"))
    {
        ptr->critical_threshold_abs_node1 = atoi(value);
        dlog ("Critical Threshold  Absolute Node 1  : %d\n", ptr->critical_threshold_abs_node1 );
        rc = PASS ;
    }
    else if (MATCH("resource", "num_tries"))
    {
        ptr->num_tries = atoi(value);
        dlog ("Number of Tries   : %d\n", ptr->num_tries );
        rc = PASS ;
    }
    else if (MATCH("resource", "alarm_on"))
    {
        ptr->alarm_status= atoi(value);
        dlog ("Resource Alarm Status   : %d\n", ptr->alarm_status);
        rc = PASS ;
    }
    else if (MATCH("resource", "percent"))
    {
        ptr->percent= atoi(value);
        dlog ("Resource Percent   : %d\n", ptr->percent);
        rc = PASS ;
    }

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

    if (ini_parse( CONFIG_FILE, rmon_config_handler, &rmon_config) < 0)
    {
        elog("Can't load '%s'\n", CONFIG_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(MTCE_INI_FILE, keystone_config_handler, &rmon_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_INI_FILE );
        return (FAIL_LOAD_INI);
    }

    get_debug_options ( CONFIG_FILE, &rmon_config );

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( rmon_config.mask != CONF_MASK )
    {
        elog ("Error: Agent configuration failed (%x)\n",
             ((-1 ^ rmon_config.mask) & CONF_MASK));
        return (FAIL_INI_CONFIG);
    }

    /* Manage the daemon pulse period setting - ensure in bound values */
    if ( rmon_config.audit_period < RMON_MIN_AUDIT_PERIOD )
    {
        rmon_ctrl.audit_period = RMON_MIN_AUDIT_PERIOD ;
    }
    else if ( rmon_config.audit_period > RMON_MAX_AUDIT_PERIOD )
    {
        rmon_ctrl.audit_period = RMON_MAX_AUDIT_PERIOD ;
    }
    else
    {
        rmon_ctrl.audit_period = rmon_config.audit_period ;
    }
    ilog("Event Audit Period: %d secs\n", rmon_ctrl.audit_period );
    rmon_ctrl.rmon_critical_thr = rmon_config.rmon_critical_thr;

    /* Manage the ceilometer pm period setting - ensure in bound values */
    if ( rmon_config.pm_period < RMON_MIN_PM_PERIOD )
    {
        rmon_ctrl.pm_period = RMON_MIN_PM_PERIOD ;
    }
    else if ( rmon_config.pm_period > RMON_MAX_PM_PERIOD )
    {
        rmon_ctrl.pm_period = RMON_MAX_PM_PERIOD ;
    }
    else
    {
        rmon_ctrl.pm_period = rmon_config.pm_period ;
    }
    ilog("PM Audit Period: %d\n", rmon_ctrl.pm_period );

    /* Manage the NTP query pulse period setting - ensure in bound values */
    if ( rmon_config.ntp_audit_period < RMON_MIN_NTP_AUDIT_PERIOD )
    {
        rmon_ctrl.ntp_audit_period = RMON_MIN_NTP_AUDIT_PERIOD ;
    }
    else if ( rmon_config.ntp_audit_period > RMON_MAX_NTP_AUDIT_PERIOD )
    {
        rmon_ctrl.ntp_audit_period = RMON_MAX_NTP_AUDIT_PERIOD ;
    }
    else
    {
        rmon_ctrl.ntp_audit_period = rmon_config.ntp_audit_period ;
    }
    ilog("NTP Audit Period: %d secs\n", rmon_ctrl.ntp_audit_period );


    // NTPQ Command timeout
    if ( rmon_config.ntpq_cmd_timeout >= rmon_ctrl.ntp_audit_period )
    {
        rmon_ctrl.ntpq_cmd_timeout = NTPQ_CMD_TIMEOUT ;
        wlog("NTPQ command timeout (%d secs) should be less than ntp_audit_period (%d secs) ; forcing default\n",
             rmon_ctrl.ntpq_cmd_timeout, rmon_ctrl.ntp_audit_period );
    }
    else
    {
        rmon_ctrl.ntpq_cmd_timeout = rmon_config.ntpq_cmd_timeout ;
    }
    ilog("NTPQ command timeout: %d secs\n", rmon_ctrl.ntpq_cmd_timeout );

    rmon_ctrl.per_node = rmon_config.per_node;

    return (rc);
}

/****************************/
/* Initialization Utilities */
/****************************/

/* Construct the messaging sockets  *
 * 1. receive socket (mtc_client_rx_socket)    *
 * 2. transmit socket (mtc_client_tx_socket)   */
int socket_init ( void )
{
    int rc;

    rmon_msg_init ( );
    /* Init the resource monitor api tx port.
    * This is the port that the rmon client api uses to
    * inform rmon of any registering or deregistering client
    * processes */
    rc = rmon_port_init ( rmon_config.rmon_api_tx_port );

    return (rc);
}

/*****************************************************************************
 *
 * Name    : daemon_init
 *
 * Purpose : initialize the daemon and sockets
 *
 *****************************************************************************/
int daemon_init ( string iface, string nodetype_str )
{
    int rc = PASS ;
    char   temp_hostname [MAX_HOST_NAME_SIZE+1];

    /* init the control struct */
    memset ( &rmon_ctrl.my_hostname[0], 0, MAX_HOST_NAME_SIZE+1);
    rmon_ctrl.my_macaddr   = "" ;
    rmon_ctrl.my_address   = "" ;
    rmon_ctrl.resources    = 0  ;
    rmon_ctrl.clients = 0 ;

    /* Assign interface to config */
    rmon_config.mgmnt_iface = (char*)iface.data() ;

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return ( FAIL_FILES_INIT ) ;
    }

    if ( set_host_functions ( nodetype_str, &rmon_ctrl.nodetype, &rmon_ctrl.function, &rmon_ctrl.subfunction ) != PASS )
    {
        elog ("failed to extract nodetype\n");
        return ( FAIL_NODETYPE );
    }

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
    * Wait for /etc/platform/.initial_config_complete & /var/run/goenabled */
    daemon_wait_for_file ( CONFIG_COMPLETE_FILE , 0);
    daemon_wait_for_file ( GOENABLED_MAIN_PASS , 0);

    /* Configure the daemon */
    if ( (rc = daemon_configure ( )) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%i)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }

    /* This ensures any link aggregation interface overrides the physical */
    rmon_config.mgmnt_iface = daemon_get_iface_master ( rmon_config.mgmnt_iface );

    /* Log the startup settings */
    ilog("Interface   : %s\n", rmon_config.mgmnt_iface );
    ilog("TX Interface: %d\n", rmon_config.rmon_tx_port );

    get_iface_macaddr  ( rmon_config.mgmnt_iface,  rmon_ctrl.my_macaddr );
    get_iface_address  ( rmon_config.mgmnt_iface,  rmon_ctrl.my_address , true );
    get_iface_hostname ( rmon_config.mgmnt_iface,  &temp_hostname[0] );

    strcat(rmon_ctrl.my_hostname, "host=" );
    strcat(rmon_ctrl.my_hostname, temp_hostname);

    if ( (rc = rmon_hdlr_init (&rmon_ctrl)) != PASS )
    {
        ilog ("rmon_hdlt_init failed\n");
        rc = FAIL_HDLR_INIT ;
    }

    /* Setup the messaging sockets */
    else if ( (rc = socket_init ( )) != PASS )
    {
        elog ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT ;
    }

    return (rc);
}

/*****************************************************************************
 *
 * Name    : daemon_service_run
 *
 * Purpose : The main rmon service launch
 *
 * Waits for initial config complete and then go enabled pass flag files
 * before starting resource monitoring.
 *
 *****************************************************************************/
void daemon_service_run ( void )
{
    rmon_service ( &rmon_ctrl );
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

/*****************************************************************************
 *
 * Name    : daemon_run_testhead
 *
 * Purpose : Run the rmon test suite by sending alarms to maintainance
 *           (To be used in Sprint 11 for testing)
 *
 *****************************************************************************/
int daemon_run_testhead ( void )
{
    /* Clear All */
    return (FAIL);
}


