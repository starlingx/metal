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

static void fork_hostwd_logger ( void );
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
    for ( int quorum_failed = 0 ; ; )
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
        else if ( quorum_failed == 0 )
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
                    quorum_failed++ ;
            }
        }
        if ( 0 >= ctrl->pmon_grace_loops )
        {
            if ( quorum_failed++ == 0 )
            {
                if (daemon_is_file_present(NODE_LOCKED_FILE))
                {
                    wlog( "Host watchdog (hostwd) not receiving messages from PMON"
                          " however host is locked - refusing to take reset action"
                          " while locked\n" );
                }
                else
                {
                    emergency_log( "*** Host watchdog (hostwd) not receiving messages "
                               "from PMON ***\n");
                    hostw_log_and_reboot();
                }
            }
        }
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

    /* start the process to log as much data as possible */
    fork_hostwd_logger ();

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
static void fork_hostwd_logger ( void )
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

