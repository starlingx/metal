/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Host Watchdog Daemon
  */

#include <fcntl.h>
#include <linux/watchdog.h>
#include "hostw.h"

/* avoid compiler warnings */
#define UNUSED(_x_) ((void) _x_)

/****************************/
/* Initialization Utilities */
/****************************/

/* Local kernel watchdog functions */
static    int kernel_watchdog_init ( void );
static   void kernel_watchdog_close ( void );

/* Host Watchdog Control Structure */
static hostw_ctrl_type hostw_ctrl;

/* Daemon Configuration Structure
 * @see daemon_common.h for daemon_config_type struct format.
 */
static daemon_config_type hostw_config;

/* Get local structs */

hostw_ctrl_type * get_ctrl_ptr ( void )
{
    return (&hostw_ctrl);
}

daemon_config_type * daemon_get_cfg_ptr ( void )
{
    return &hostw_config ;
}

/* Cleanup exit handler */
void daemon_exit ( void )
{
    char pipe_cmd_output [PIPE_COMMAND_RESPON_LEN] ;
    hostw_socket_type   * hostw_socket = hostw_getSock_ptr();

    execute_pipe_cmd ( "systemctl is-system-running", &pipe_cmd_output[0], PIPE_COMMAND_RESPON_LEN );
    if ( strnlen ( pipe_cmd_output, PIPE_COMMAND_RESPON_LEN ) > 0 )
    {
        ilog ("systemctl is-system-running result: <%s>\n", pipe_cmd_output );
        string temp = pipe_cmd_output ;

        if ( temp.find ("stopping") == string::npos )
        {
            /* system is not stopping so turn off the watchdog with process stop */
            kernel_watchdog_close();
        }
        else
        {
            /* leave the watchdog running during shutdown as failsafe reset method */
            ilog ("Leaving watchdog running while system is 'stopping'\n");
        }
    }
    else
    {
        wlog ("call to systemctl is-system-running failed to yield response\n");
    }


    if (hostw_socket->status_sock != 0)
    {
        close (hostw_socket->status_sock);
        hostw_socket->status_sock = 0;
    }

    daemon_files_fini();
    daemon_dump_info();
    exit (0);
}


/* Startup config read */
int hostw_process_config ( void * user,
                     const char * section,
                     const char * name,
                     const char * value )
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("config", "hostwd_failure_threshold"))
    {
        int previous = config_ptr->hostwd_failure_threshold ;
        config_ptr->hostwd_failure_threshold = atoi(value);
        config_ptr->mask |= CONFIG_HOSTWD_FAILURE_THRESHOLD ;
        ilog("Quorum Thld : %d", config_ptr->hostwd_failure_threshold);
        if ( config_ptr->hostwd_failure_threshold != previous )
            hostw_ctrl.pmon_grace_loops = config_ptr->hostwd_failure_threshold;
    }
    else if (MATCH("config", "hostwd_reboot_on_err"))
    {
        config_ptr->hostwd_reboot_on_err = atoi(value);
        config_ptr->mask |= CONFIG_HOSTWD_REBOOT ;
        ilog("Quorum Loss : %s",
              config_ptr->hostwd_reboot_on_err ? "Reboot & Log" : "Log Only");
    }
    else if (MATCH("config", "hostwd_kdump_on_stall"))
    {
        config_ptr->hostwd_kdump_on_stall = atoi(value);
        string state = "enabled" ;
        if ( config_ptr->hostwd_kdump_on_stall == 0 )
            state = "disabled" ;
        ilog("Crash Dump  : %s (%d)", state.c_str(), config_ptr->hostwd_kdump_on_stall );
    }
    else if (MATCH("config", "hostwd_use_kern_wd"))
    {
        config_ptr->hostwd_use_kern_wd = atoi(value);
        config_ptr->mask |= CONFIG_HOSTWD_USE_KERN_WD ;
        ilog("Use Kern Wd : %s", config_ptr->hostwd_use_kern_wd ? "Yes":"No");
    }
    else if (MATCH("config", "hostwd_console_path"))
    {
        config_ptr->hostwd_console_path = strdup(value);
        config_ptr->mask |= CONFIG_HOSTWD_CONSOLE_PATH ;
        ilog("WD Console  : %s", config_ptr->hostwd_console_path);
    }
    else if (MATCH("config", "hostwd_update_period")) /* in pmond.conf file */
    {
        config_ptr->hostwd_update_period = atoi(value);
        config_ptr->mask |= CONFIG_HOSTWD_UPDATE_PERIOD ;
        ilog("Quorum Rate : %d secs", config_ptr->hostwd_update_period);
    }

    /* Timeout config options */

    /* kernwd_update_period is how many seconds the watchdog can run without
     * petting it before timeout and kernel reboot.
     * The timeout for controller nodes is loaded with the stall detection
     * value compared to other nodes with the legacy value.
     */
    else if (hostw_ctrl.nodetype & CONTROLLER_TYPE)
    {
        if (MATCH("timeouts", "kernwd_update_period_stall_detect"))
        {
            hostw_ctrl.kernwd_update_period = atoi(value);
            config_ptr->mask |= CONFIG_KERNWD_UPDATE_PERIOD ;
        }
    }
    else
    {
        if (MATCH("timeouts", "kernwd_update_period"))
        {
            hostw_ctrl.kernwd_update_period = atoi(value);
            config_ptr->mask |= CONFIG_KERNWD_UPDATE_PERIOD ;
        }
    }
    return (PASS);
}

/** Start processing of the config file
 */
int daemon_configure ( void )
{
    daemon_config_type * config = daemon_get_cfg_ptr();

    if (ini_parse(HOSTWD_CONFIG_FILE, hostw_process_config, config) < 0)
    {
        elog("Can't load '%s'\n", HOSTWD_CONFIG_FILE);
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(PMOND_CONFIG_FILE, hostw_process_config, config) < 0)
    {
        elog("Can't load '%s'\n", PMOND_CONFIG_FILE);
        return (FAIL_LOAD_INI);
    }

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( config->mask != CONFIG_MASK )
    {
        elog ("Error: host watchdog configuration failed (%x)\n",
             ((-1 ^ config->mask) & CONFIG_MASK));
        return (FAIL_INI_CONFIG);
    }

    /* hostwd_update_period is how long we *expect* to wait between updates.
     * given the unpredicability of scheduling, etc, we'll not consider a
     * message missed until twice the expected time has elapsed
     */
    config->hostwd_update_period *= 2;

    return (PASS);
}

/* Setup the daemon messaging interfaces/sockets */
int socket_init ( void )
{
    int rc = PASS;
    ilog("setting up host socket\n");
    rc = hostw_socket_init();

    return (rc);
}

/* The main program initializer
 * iface and nodetype_str are passed by the daemon framework, but are
 * not needed in this program
 */
int daemon_init ( string iface, string nodetype_str )
{
    int rc = PASS ;
    UNUSED(iface);

    /* init the control struct */
    memset(&hostw_ctrl, 0, sizeof(hostw_ctrl_type));

    if (daemon_files_init() != PASS)
    {
        elog ("Pid, log or other files could not be opened\n");
        return ( FAIL_FILES_INIT ) ;
    }

    /* Bind signal handlers */
    if (daemon_signal_init() != PASS)
    {
        elog ("daemon_signal_init failed\n");
        return ( FAIL_SIGNAL_INIT );
    }

    /* convert node type to bit field integer */
    hostw_ctrl.nodetype = get_host_function_mask ( nodetype_str ) ;
    ilog ("Node Type   : %s (0x%x)\n",
           nodetype_str.c_str(),
           hostw_ctrl.nodetype);

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
    if ((rc = daemon_configure()) != PASS)
    {
        elog ("Daemon service configuration failed (rc:%i)\n", rc );
        rc = FAIL_DAEMON_CONFIG ;
    }

    /* Setup the messaging sockets */
    else if ((rc = socket_init()) != PASS)
    {
        elog ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT ;
    }
    return (rc);
}

/* Start the service
 *
 *   1. Wait for host config (install) complete
 *   2. Wait for goenable
 *   3. Do startup delay
 *   4. run the host watchdog service inside hostwHdlr.cpp
 *
 */
void daemon_service_run ( void )
{
    ilog ("System is up and running, hostwd ready for action\n" );

    /* last step before starting main loop - start kernel watchdog */
    kernel_watchdog_init();

    hostw_service();
    daemon_exit();
}

/* Startup the kernel watchdog
 *
 * We have to regularly pet the watchdog after calling this, so don't call
 * this function until we're ready to start our main program loop where
 * the watchdog is pet.
 *
 * Potential improvement - use an mtcTimer rather than main loop to pet
 * watchdog to avoid this requirement
 */
static int kernel_watchdog_init ( void )
{
    daemon_config_type * config_ptr = daemon_get_cfg_ptr();

    /* open the watchdog */

    if ( (config_ptr->hostwd_use_kern_wd == 0) ||
         (hostw_ctrl.kernwd_update_period < HOSTW_MIN_KERN_UPDATE_PERIOD))
    {
        /* config file says don't use watchdog, or used too small a period */
        wlog("Watchdog NOT enabled ; %s",
              config_ptr->hostwd_use_kern_wd ?
              "kernwd_update_period too small":
              "hostwd_use_kern_wd=0");
        return PASS;
    }

    hostw_ctrl.watchdog = open("/dev/watchdog", O_WRONLY);
    if (0 >= hostw_ctrl.watchdog)
    {
        elog("Could not open kernel watchdog\n");
        return FAIL;
    }

    /* set watchdog timeout (in seconds) */
    if (ioctl(hostw_ctrl.watchdog, WDIOC_SETTIMEOUT, &hostw_ctrl.kernwd_update_period))
    {
        elog ("Failed to enable watchdog")
        kernel_watchdog_close();
        return FAIL;
    }
    ilog ("Kernel watchdog enabled with %d second timeout",
           hostw_ctrl.kernwd_update_period);
    return PASS;
}

/* Gracefully take the watchdog to live on the farm */
static void kernel_watchdog_close ( void )
{
    hostw_ctrl_type* ctrl_ptr = get_ctrl_ptr();

    if (ctrl_ptr->watchdog)
    {
        /* "Magic close" - special character required by some watchdogs */
        size_t written;
        written = write(ctrl_ptr->watchdog, "V", 1);
        if (written <= 0)
        {
            wlog("Can't send magic close to kernel watchdog - behavior will"
                 " be implementation dependant");
        }
        close(ctrl_ptr->watchdog);
        ctrl_ptr->watchdog = 0;
    }
}

/* Pet the watchdog to keep it from barking (resetting the system) */
void kernel_watchdog_pet ( void )
{
    hostw_ctrl_type* ctrl_ptr = get_ctrl_ptr();
    if (ctrl_ptr->watchdog != 0){
        ioctl(ctrl_ptr->watchdog, WDIOC_KEEPALIVE, 0);
    }
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

