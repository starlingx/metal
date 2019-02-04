/*
 * Copyright (c) 2019 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Starling-X Maintenance Link Monitor Initialization
  */

#include "lmon.h"

/** Daemon Configuration Structure - Allocation and get pointer
 * @see daemon_common.h for daemon_config_type struct format. */
static daemon_config_type lmon_config ;
daemon_config_type * daemon_get_cfg_ptr () { return &lmon_config ; }

/* read config label values */
static int lmon_config_handler ( void * user,
                           const char * section,
                           const char * name,
                           const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("client", "audit_period"))
    {
        config_ptr->audit_period = atoi(value);
        ilog ("Audit Period: %d (secs)", config_ptr->audit_period );
    }
    else if (MATCH("client", "lmon_query_port"))
    {
        config_ptr->lmon_query_port = atoi(value);
        ilog ("Status Query: %d (port)", config_ptr->lmon_query_port );
    }
    else if (MATCH("client", "daemon_log_port"))
    {
        config_ptr->daemon_log_port = atoi(value);
        ilog ("Daemon Log  : %d (port)", config_ptr->daemon_log_port );
    }
    else if (MATCH("client", "uri_path"))
    {
        config_ptr->uri_path = strdup(value);
    }

    return (PASS);
}

/*****************************************************************************
 *
 * Name    : daemon_configure
 *
 * Purpose : Read process config file settings into the daemon configuration
 *
 * Configuration File */

#define CONFIG_FILE   ((const char *)"/etc/mtc/lmond.conf")

/*****************************************************************************/

int daemon_configure ( void )
{
    int rc = PASS ;

    /* read config out of /etc/mtc/lmond.conf */
    if (ini_parse( CONFIG_FILE, lmon_config_handler, &lmon_config) < 0)
    {
        elog("Can't load '%s'\n", CONFIG_FILE );
        rc = FAIL_INI_CONFIG ;
    }
    else
    {
        get_debug_options ( CONFIG_FILE, &lmon_config );
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name    : daemon_init
 *
 * Purpose : Daemon Initialization
 *
 *****************************************************************************/

int daemon_init ( string iface, string nodetype_str )
{
    int rc = PASS ;

    UNUSED(iface);
    UNUSED(nodetype_str);

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

    daemon_wait_for_file ( CONFIG_COMPLETE_FILE, 0);
    daemon_wait_for_file ( PLATFORM_DIR, 0);
    daemon_wait_for_file ( GOENABLED_MAIN_READY, 0);

    /* Configure the daemon */
    if ( (rc = daemon_configure ( )) != PASS )
    {
        elog ("Daemon service configuration failed (rc:%i)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }

    return (rc);
}


void daemon_dump_info ( void )
{


}

void daemon_sigchld_hdlr ( void )
{
    ; /* dlog("Received SIGCHLD ... no action\n"); */
}

const char MY_DATA [100] = { "eieio\n" } ;
const char * daemon_stream_info ( void )
{
    return (&MY_DATA[0]);
}

/** Teat Head Entry */
int daemon_run_testhead ( void )
{
    // ilog ("Empty test head.\n");
    return (PASS);
}
