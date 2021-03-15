/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Host Watchdog Service Handler
  */

#include "hostw.h"
#include <linux/watchdog.h>
#include <fcntl.h>
#include <unistd.h>       /* for execve */

/* In addition to logging to wherever elog messages go,
 * this function does its best to log output to the console
 * (for the purpose of capturing data when the system is
 * about to go down)
 *
 * The path we log to is defined in the config file, exepected to
 * be something like "/dev/console", "/dev/pts/0" or "/dev/ttyS0"
 */
#define emergency_log(...)                                    \
{                                                             \
    daemon_config_type *cfg = daemon_get_cfg_ptr ();          \
    elog(__VA_ARGS__)                                         \
    if (cfg->hostwd_console_path) {                           \
        FILE* console = fopen(cfg->hostwd_console_path, "a"); \
        if (NULL != console) {                                \
            fprintf(console, __VA_ARGS__);                    \
            fclose (console);                                 \
        }                                                     \
    }                                                         \
}

int hostw_service_command ( hostw_socket_type * hostw_socket );

void fork_hostwd_logger ( void );
char   my_hostname [MAX_HOST_NAME_SIZE+1];

/* Push daemon state to log file */
void daemon_dump_info ( void )
{
}

void daemon_sigchld_hdlr ( void )
{
}

static struct mtc_timer pmonTimer ;

void hostwTimer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    UNUSED(sig);
    UNUSED(uc);

    if ( !(*tid_ptr) )
        return ;
    else if (( *tid_ptr == pmonTimer.tid ) )
        pmonTimer.ring = true ;
    else
        mtcTimer_stop_tid_int_safe ( tid_ptr );
}

/***************************************************************************
 *
 * Name      : get_kdump_support
 *
 * Purpose   : Query the state of the kdump service
 *
 * Updates   : kdump_supported default of false is updated to true if
 *             the kdump service query indicates that kdump is active.
 *
 **************************************************************************/

void get_kdump_support ( void )
{
    char pipe_cmd_output [PIPE_COMMAND_RESPON_LEN] ;
    execute_pipe_cmd ( "/usr/bin/systemctl is-active kdump",
                       &pipe_cmd_output[0],
                       PIPE_COMMAND_RESPON_LEN );
    if ( strnlen ( pipe_cmd_output, PIPE_COMMAND_RESPON_LEN ) > 0 )
    {
        if ( ! strncmp (&pipe_cmd_output[0], "active", strlen("active")))
        {
            hostw_ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
            ctrl_ptr->fd_sysrq_enable = open(SYSRQ_CONTROL_INTERFACE,O_WRONLY);
            ctrl_ptr->fd_sysrq_command= open(SYSRQ_COMMAND_INTERFACE,O_WRONLY);
            if ( ctrl_ptr->fd_sysrq_enable && ctrl_ptr->fd_sysrq_command )
            {
                ilog("kdump is active");
                ctrl_ptr->kdump_supported = true ;
                return ;
            }
            ilog("kdump service setup failed ; %d:%d:%s",
                  ctrl_ptr->fd_sysrq_enable,
                  ctrl_ptr->fd_sysrq_command,
                  pipe_cmd_output);
        }
        else
        {
            ilog("kdump is inactive (%s)", pipe_cmd_output);
        }
    }
    else
    {
        elog("kdump status query failed ; assuming kdump is inactive");
    }
}

/***************************************************************************
 *
 * Name      : force_crashdump
 *
 * Purpose   : Force a crash dump via SysRq command 'c'
 *
 * Warning   : Host will reset immediately, without graceful shutdown.
 *
 **************************************************************************/
void force_crashdump ( void )
{
    hostw_ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
    if (( daemon_get_cfg_ptr()->hostwd_kdump_on_stall == 0 ) ||
        ( ctrl_ptr->kdump_supported == false ))
    {
        /* crash dump is disabled or not supported */
        return ;
    }

    /* Go for the crash dump */

    /* Enable all functions of sysrq */
    static char sysrq_enable_cmd = '1' ;

    /* Crash Dump by NULL pointer dereference */
    static char sysrq_crash_dump_cmd = 'c' ;

    int bytes = write(ctrl_ptr->fd_sysrq_enable, &sysrq_enable_cmd, 1 );
    if ( bytes <= 0 )
    {
        elog("SysRq Enable failed (%d:%d:%s)", bytes, errno, strerror(errno) );
    }
    else
    {
        /*************** force crash dump *****************/
        bytes = write(ctrl_ptr->fd_sysrq_command, &sysrq_crash_dump_cmd, 1);
        if ( bytes <= 0 )
        {
            elog("SysRq command failed (%d:%d:%s)",
                  bytes, errno, strerror(errno) );
        }
        else
        {
            ; /* should not get here */
        }
    }
}

/***************************************************************************
 *
 * Name       : manage_quorum_failed
 *
 * Purpose    : permit recovery
 *
 * Description: If called while none of the reboot or sysRq reset failure
 *              recovery options are enabled then we are not going for a
 *              reboot or reset so allow recovery.
 *
 **************************************************************************/

void manage_quorum_failed ( void )
{
    hostw_ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
    daemon_config_type* config_ptr = daemon_get_cfg_ptr() ;

    if ((( config_ptr->hostwd_kdump_on_stall == 0 ) ||
         ( ctrl_ptr->kdump_supported == false )) &&
        ( config_ptr->hostwd_reboot_on_err == 0 ))
    {
        /* If we are not going for a reboot or reset then allow recovery
         * by clearing the control boolean that prevents recovery. */
        ilog ("Quorum failed but all reboot/reset recovery options disabled");
        ilog ("... allowing auto recovery");
        ctrl_ptr->quorum_failed = false ;
        ctrl_ptr->pmon_grace_loops = config_ptr->hostwd_failure_threshold ;
    }
}

/**
 * This is the main loop of the program
 *
 * We loop waiting for messages to arrive.  We're allowed to miss some messages
 * (due to jitter in system scheduling, overcommitted resources, etc) and this
 * can be tuned using the config file.  On each loop (regardless of message
 * received) we pet the watchdog.
 *
 * If a large number of messages are missed, or the messages conistently
 * indicate system issues, we take the appropriate action (log what we can
 * and reboot the system).
 */
void hostw_service ( void )
{
    std::list<int> socks ; /* we have a "list" of 1 socket, to allow for
                            * future extension and to mirror code flow of
                            * other utilities */
    hostw_socket_type * hostw_socket = hostw_getSock_ptr ();
    struct timeval timeout;
    int rc;

    hostw_ctrl_type *ctrl = get_ctrl_ptr ();
    daemon_config_type *config = daemon_get_cfg_ptr ();

    get_hostname  (&my_hostname[0], MAX_HOST_NAME_SIZE );

    get_kdump_support(); /* query for kdump support */

    mtcTimer_init ( pmonTimer, my_hostname, "pmon" );
    mtcTimer_start( pmonTimer, hostwTimer_handler, config->hostwd_update_period);

    ctrl->pmon_grace_loops = config->hostwd_failure_threshold;

    socks.clear();
    if ( hostw_socket->status_sock )
    {
        socks.push_front (hostw_socket->status_sock);
        FD_SET(hostw_socket->status_sock, &(hostw_socket->readfds));
    }
    socks.sort();

    ilog("Host Watchdog Service running\n");
    for ( ; ; )
    {
        timeout.tv_sec =1; /* 1 second select ; pet watchdog every second */
        timeout.tv_usec=0;

        kernel_watchdog_pet();

        /* set the master fd_set */
        FD_ZERO(&(hostw_socket->readfds));
        FD_SET(hostw_socket->status_sock, &(hostw_socket->readfds));

        rc = select (socks.back() + 1,
            &(hostw_socket->readfds), NULL, NULL, &timeout);

        /* If the select time out expired then no new message to process */
        if ( rc < 0 )
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if ( errno != EINTR  )
            {
                elog ("Select Failed (rc:%d) %s \n", errno, strerror(errno));
                if ( ctrl->pmon_grace_loops > 0 )
                    ctrl->pmon_grace_loops--;
            }
        }
        else if ( rc == 0 )
        {
            if ( pmonTimer.ring == true )
            {
                if (daemon_is_file_present(NODE_LOCKED_FILE))
                {
                    wlog("Process Quorum Health not receive from PMON ; "
                         "no action while node is locked");
                }
                else
                {
                    if ( ctrl->pmon_grace_loops )
                        ctrl->pmon_grace_loops--;

                    if ( ctrl->pmon_grace_loops > 0 )
                    {
                        wlog ("Process Quorum Health not received from PMON ; "
                              "%d more misses allowed before self-reboot",
                               ctrl->pmon_grace_loops-1);
                    }
                }
                pmonTimer.ring = false ;
            }
        }
        else if ( ctrl->quorum_failed == false )
        {
            if (FD_ISSET(hostw_socket->status_sock, &(hostw_socket->readfds)))
            {
                rc = hostw_service_command ( hostw_socket);
                if ( rc == PASS ) /* got "all is well" message */
                {
                    /* reset the pmon quorum health timer */
                    mtcTimer_reset(pmonTimer);
                    mtcTimer_start(pmonTimer, hostwTimer_handler, config->hostwd_update_period);

                    /* reload pmon grace loops count down */
                    if ( ctrl->pmon_grace_loops != config->hostwd_failure_threshold )
                    {
                        ilog("Process Quorum Health messaging restored");
                        ctrl->pmon_grace_loops = config->hostwd_failure_threshold;
                    }
                }
                else if ( rc != RETRY )
                    ctrl->quorum_failed = true ;
            }
        }
        if ( 0 >= ctrl->pmon_grace_loops )
        {
            if ( ctrl->quorum_failed == false )
            {
                ctrl->quorum_failed = true ;
                if (daemon_is_file_present(NODE_LOCKED_FILE))
                {
                    wlog( "Host watchdog (hostwd) not receiving messages from PMON"
                          " however host is locked - refusing to take reset action"
                          " while locked\n" );
                }
                else
                {
                    /* force a crash dump if that feature is enabled */
                    force_crashdump();

                    emergency_log( "*** Host watchdog (hostwd) not receiving messages "
                                   "from PMON ***\n");

                    hostw_log_and_reboot();
                }
            }
        }
        if ( ctrl->quorum_failed )
            manage_quorum_failed();

        daemon_signal_hdlr ();
    }
}

/**
 * Parse and react to a message from PMON
 */
int hostw_service_command ( hostw_socket_type * hostw_socket)
{
    mtc_message_type msg[2]; /* we use a chunk of memory larger than a single
                              * mtc_message_type to check for oversized messages
                              * (invalid...)
                              */
    int len = sizeof(msg[0]) + 1;

    memset(msg, 0, 2*sizeof(msg[0]));
    socklen_t addrlen = (socklen_t) sizeof(hostw_socket->status_addr);
    len = recvfrom(hostw_socket->status_sock,
                          (char*)&msg,
                          len,
                          0,
                          (struct sockaddr*) &hostw_socket->status_addr,
                          &addrlen);

    if (sizeof(msg[0]) == len)
    {
        /* message is correct size, check pmon reported status */
        switch (msg[0].cmd)
        {
            case MTC_CMD_NONE:
                /* All is well */
                dlog ("pmon is happy");
                return PASS;

            case MTC_EVENT_PMON_CRIT:
                if (daemon_is_file_present(NODE_LOCKED_FILE))
                {
                    wlog("PMON reports unrecoverable system - message '%s'", msg[0].buf );
                    ilog("... no action while node is locked");
                    return PASS;
                }
                else
                {
                    emergency_log( "*** PMON reports unrecoverable system - message '%s' ***\n", msg[0].buf);

                    /* force a crash dump if that feature is enabled */
                    force_crashdump();

                    hostw_log_and_reboot();
                    return FAIL;
                }

            default:
                elog("Unknown status reported\n");
                break;
        }
    }
    else
    {
        /* bad message size */
        elog("Host Watchdog received bad or corrupted message (length = %d)\n", len);
    }
    return RETRY;
}

/**
 * Host watchdog (or PMON) has determined that the system is not healthy and is
 * performing recovery action.
 */
void hostw_log_and_reboot()
{
    daemon_config_type* config = daemon_get_cfg_ptr ();

    emergency_log ("*** Host Watchdog declaring system unhealthy ***\n");

    /* Start the process to log as much data as possible */

    /* NOTE: This function currently does not do anything so its commented
     * out for now. Uncomment when actual value add logging is implemented.
    fork_hostwd_logger (); */

    if (config->hostwd_reboot_on_err) {
        emergency_log ("*** Initiating reboot ***\n");

        /* start the process that will perform an ungraceful reboot, if
         * the graceful reboot fails */
        fork_sysreq_reboot ( FORCE_REBOOT_DELAY );

        /* start the graceful reboot process */
        fork_graceful_reboot ( GRACEFUL_REBOOT_DELAY );
    }
}

/**
 * Initiate the thread which logs as much information about the system
 * as possible.
 */
void fork_hostwd_logger ( void )
{
    int parent = double_fork ();
    if (0 > parent) /* problem forking */
    {
        elog ("failed to fork hostwd logging process\n");
        return ;
    }
    else if (0 == parent) /* if we're the child */
    {
        sigset_t mask , mask_orig ;

        setup_child(false); /* initialize the process group, etc */
        ilog ("*** Host Watchdog Logging Thread ***\n");

        sigemptyset (&mask);
        sigaddset (&mask, SIGTERM );
        sigprocmask (SIG_BLOCK, &mask, &mask_orig );

        /* TODO - log data here */
        exit (0);
    }
}

