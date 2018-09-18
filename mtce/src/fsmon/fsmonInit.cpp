/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform File System Monitor Service Header
  */

#include "fsmon.h"

/* Process Monitor Configuration File */
#define CONFIG_FILE   ((const char *)"/etc/mtc/fsmond.conf")

static unsigned int    my_nodetype = CGTS_NODE_NULL ;

static char hostname_str [MAX_HOST_NAME_SIZE+1];

/** Daemon Configuration Structure
 *  - Allocation and get pointer
 * @see daemon_common.h for daemon_config_type struct format. */
static daemon_config_type fsmon_config ; 
daemon_config_type * daemon_get_cfg_ptr () { return &fsmon_config ; }

/* Cleanup exit handler */
void daemon_exit ( void )
{
    daemon_files_fini ();
    daemon_dump_info  ();
    exit (0);
}


/* Startup config read */
static int fsmon_config_handler ( void * user, 
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
    int rc = PASS ;

    if (ini_parse( CONFIG_FILE, fsmon_config_handler, &fsmon_config) < 0)
    {
        elog("Can't load '%s'\n", CONFIG_FILE );
    }

    get_debug_options ( CONFIG_FILE, &fsmon_config );

    /* Verify loaded config against an expected mask 
     * as an ini file fault detection method */
    if ( fsmon_config.mask != CONFIG_MASK )
    {
        elog ("Error: Agent configuration failed (%x)\n",
             ((-1 ^ fsmon_config.mask) & CONFIG_MASK));
        return (FAIL_INI_CONFIG);
    }

    /* This ensures any link aggregation interface overrides the physical */
    fsmon_config.mgmnt_iface = daemon_get_iface_master ( fsmon_config.mgmnt_iface );

    /* Log the startup settings */
    ilog("Interface   : %s\n", fsmon_config.mgmnt_iface );

    ilog("Audit Period: %d\n", fsmon_config.audit_period );

    return (rc);
}


/****************************/
/* Initialization Utilities */
/****************************/

/* Setup the daemon messaging interfaces/sockets */
int socket_init ( void )
{
    return (PASS);
}

/* The common daemon init */
int daemon_init ( string iface, string nodeType_str )
{
    int rc = PASS ;

    /* convert node type to integer */
    my_nodetype = get_host_function_mask ( nodeType_str ) ;
    ilog ("Node Type   : %s (%d)\n", nodeType_str.c_str(), my_nodetype);

    if ( daemon_files_init ( ) != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return ( FAIL_FILES_INIT ) ;
    }

    /* Assign interface to config */
    fsmon_config.mgmnt_iface = (char*)iface.data() ;

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

    get_hostname ( &hostname_str[0], MAX_HOST_NAME_SIZE );

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
    fsmon_service ( my_nodetype );
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
