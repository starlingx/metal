/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Process Monitor Service Messaging
  */

#include <sys/stat.h>
#include <linux/un.h>

#include "pmon.h"
#include "nodeMacro.h"
#include "pmonAlarm.h"         /* for ... this module's alarm utilities / defs */
#include "hostwMsg.h"
#include "jsonUtil.h"

/* Where to send events */
string mtcAgent_ip = "" ;

static int pulse_log_threshold ;

/**
 * Messaging Socket Control Struct - The allocated struct
 */
static pmon_socket_type pmon_sock;
pmon_socket_type * pmon_getSock_ptr ( void )
{
    return ( &pmon_sock );
}

msgSock_type * get_mtclogd_sockPtr ( void )
{
    return (&pmon_sock.mtclogd);
}

/*********************************************************************/
/* Setup pmon broadcast transmit port/socket                         */
/*********************************************************************/
int event_port_init ( const char * iface , int port )
{
    int rc  = PASS ;

    /* Load the event port number */
    pmon_sock.event_port  = port ;

    mtcAgent_ip = getipbyname ( CONTROLLER );
    ilog ("ControllerIP: %s\n", mtcAgent_ip.c_str());


    pmon_sock.event_sock = new msgClassTx(mtcAgent_ip.c_str(), port, IPPROTO_UDP, iface);
    if (pmon_sock.event_sock->return_status!=PASS)
    {
        elog ("Failed to setup pmond to mtce transmit event port %d\n", port );
        return (pmon_sock.event_sock->return_status) ;
    }

    return rc;
}


/* Setup the Unix Domain Transmit Pulse Socket */
int pulse_port_init ( void )
{
    int rc   = PASS ;
    int port = daemon_get_cfg_ptr()->pmon_pulse_port;
    pmon_sock.pulse_sock = new msgClassTx(LOOPBACK_IP, port, IPPROTO_UDP);

    if (pmon_sock.pulse_sock->return_status!=PASS) return (pmon_sock.pulse_sock->return_status) ;

    snprintf (&pmon_sock.pulse.hdr[0], MSG_HEADER_SIZE, "%s", get_pmond_pulse_header());
    pmon_sock.msg_len = ((sizeof(mtc_message_type))-(BUF_SIZE));
    return (rc);
}

/* Setup the Unix Host Watchdog Socket */
int hostwd_port_init ( void )
{
    memset(&pmon_sock.hostwd_addr, 0, sizeof(pmon_sock.hostwd_addr));
    pmon_sock.hostwd_sock = socket(AF_UNIX, SOCK_DGRAM, 0);

    if (pmon_sock.hostwd_sock <= 0)
    {
        wlog("Could not connect to create hostwd socket - will retry\n");
        pmon_sock.hostwd_sock = 0 ;
        return (FAIL_SOCKET_CREATE);
    }

    /* Set up the socket address */
    memset (&pmon_sock.hostwd_addr, 0, sizeof(pmon_sock.hostwd_addr));
    pmon_sock.hostwd_addr.sun_family = AF_UNIX;

    /* Unix abstract namespace takes a string that starts with a NULL
     * as the identifier.  Thus, we need a pointer to byte[1] of the
     * sockaddr_un.sun_path (a char array)
     */
    strncpy( &(pmon_sock.hostwd_addr.sun_path[1]),
             HOSTW_UNIX_SOCKNAME,
             UNIX_PATH_MAX-1);
    int len = sizeof(pmon_sock.hostwd_addr);
    int connected = connect( pmon_sock.hostwd_sock, (sockaddr*) &pmon_sock.hostwd_addr,
        len);
    if (connected == -1)
    {
        wlog("Could not connect to hostwd port - will retry\n");
        if ( pmon_sock.hostwd_sock )
            close(pmon_sock.hostwd_sock);
        pmon_sock.hostwd_sock = 0;
        return (FAIL_CONNECT);
    }
    ilog ("connected to host watchdog\n");
    return (PASS);
}

/* Build a message for host watchdog, and send it */
int pmon_send_hostwd ( void )
{
    if (pmon_sock.hostwd_sock)
    {
        mtc_message_type msg;
        int bytes;
        int i;

        memset(&msg, 0, sizeof(msg));

        memcpy (&msg.hdr, get_cmd_req_msg_header(), MSG_HEADER_SIZE);
        msg.ver = MTC_CMD_VERSION;
        msg.rev = MTC_CMD_REVISION;
        msg.cmd = MTC_CMD_NONE; /* All good - take no action */

        for (i = 0; i < get_ctrl_ptr()->processes; i++)
        {
            process_config_type * pProcess = get_process_config_ptr (i);

            if (pProcess->quorum && pProcess->quorum_unrecoverable)
            {
                ilog ("%s unrecoverable ; reporting this to host watchdog\n", pProcess->process);
                snprintf ( (char*) &(msg.buf),
                           BUF_SIZE - 1,
                           "PMON detected %s failure",
                           pProcess->process);

                msg.cmd = MTC_EVENT_PMON_CRIT; /* things are bad */
            }
        }

        bytes = sendto( pmon_sock.hostwd_sock,
                        (char*)&msg,
                        sizeof(msg),
                        0, /* flags */
                        (struct sockaddr *) &pmon_sock.hostwd_addr,
                        sizeof(pmon_sock.hostwd_addr) );

        if (bytes == sizeof(msg))
        {
            return (PASS);
        }
        else
        {
            elog("Error sending  message to host watchdog -- error %d (%s)\n",
                errno, strerror(errno));
            if ( pmon_sock.hostwd_sock )
            {
                close(pmon_sock.hostwd_sock);
                pmon_sock.hostwd_sock = 0;
            }
            return (FAIL);

        }
    }
    return (FAIL);
}

/****************************/
/* Initialization Utilities */
/****************************/

/* Init the messaging socket control structure
 * The following messaging interfaces use this structure and
 * are initialized separately
 *
 * pulse_port_init - port that pmon I'm alive messages are transmitted on
 * event_port_init - port that pmon sends events to mtce on
 * amon_port_init  - aggrigated active process monitor receive port
 *
 * */
void pmon_msg_init ( void )
{
    memset(&pmon_sock, 0, sizeof(pmon_sock));
    pulse_log_threshold = 0 ;
}

void pmon_msg_fini ( void )
{
    /* Close the pmond pulse socket */
    if ( pmon_sock.cmd_sock )
        delete (pmon_sock.cmd_sock );
    if ( pmon_sock.event_sock )
        delete (pmon_sock.event_sock);
    if (pmon_sock.pulse_sock)
        delete pmon_sock.pulse_sock;
    if ( pmon_sock.amon_sock )
        close (pmon_sock.amon_sock);
    if ( pmon_sock.hostwd_sock )
        close (pmon_sock.hostwd_sock);
}

/* Initialize the command receive port
 * Its a LO interface only */
int pmon_inbox_init ( void )
{
    pmon_sock.cmd_sock = new msgClassRx(LOOPBACK_IP,daemon_get_cfg_ptr()->pmon_cmd_port,IPPROTO_UDP);
    if ( pmon_sock.cmd_sock )
    {
        pmon_sock.cmd_port = daemon_get_cfg_ptr()->pmon_cmd_port ;
        pmon_sock.cmd_sock->sock_ok(true);
        return (PASS);
    }
    return (FAIL);
}

int pmon_send_pulse ( void )
{
    #define LOG_THROTTLE 1000

    int bytes = pmon_sock.pulse_sock->write((char*)&pmon_sock.pulse, pmon_sock.msg_len);

    if ( bytes <= 0 )
    {
        /* Force reconnect attempt on next go around */
        elog ("Cannot sendto hbsClient (bytes=%d) (%d:%s)\n",
               bytes , errno, strerror(errno));
    }
    else
    {
        ilog_throttled ( pulse_log_threshold, LOG_THROTTLE, "sent health pulse - %d bytes (throttled:%d)\n", bytes, LOG_THROTTLE );

        dlog3 ( "Pulse: %s (%d:%d)\n", &pmon_sock.pulse.hdr[0],
                                        pmon_sock.pulse_sock->get_dst_addr()->getSockLen(), bytes );
    }
    return (PASS) ;
}


int pmon_send_event ( unsigned int event_cmd , process_config_type * ptr )
{
    mtc_message_type event ;

    int rc    = PASS ;
    int bytes = 0    ;

    /* Don't report events while we are in reset mode */
    if ( daemon_is_file_present ( NODE_RESET_FILE ) )
       return ( PASS );

    memset (&event, 0 , sizeof(mtc_message_type));

    if (( event_cmd == MTC_EVENT_MONITOR_READY) ||
        ( event_cmd == MTC_EVENT_PMON_LOG)      ||
        ( event_cmd == MTC_EVENT_PMON_MINOR)    ||
        ( event_cmd == MTC_EVENT_PMON_MAJOR)    ||
        ( event_cmd == MTC_EVENT_PMON_CRIT )    ||
        ( event_cmd == MTC_EVENT_PMON_CLEAR ))
    {
        pmon_ctrl_type * ctrl_ptr = get_ctrl_ptr () ;

        snprintf ( &event.hdr[0], MSG_HEADER_SIZE, "%s", get_mtce_event_header());

        /* Set the version/revision for PMON messages. */
        event.ver = MTC_MSG_VERSION_15_12_GA_PMON  ;
        event.rev = MTC_MSG_REVISION_15_12_GA_PMON ;

        if ( ptr->process )
        {
            /* We don't use the buffer for pmon events to remove it from the size */
            bytes = ((sizeof(mtc_message_type))-(BUF_SIZE-MAX_FILENAME_LEN));

            snprintf( &event.buf[0], MAX_PROCESS_NAME_LEN, "%s", ptr->process );

            /* Put the process function in parm zero of the event message */
            event.num = 1 ;
            event.parm[0] = ctrl_ptr->nodetype ; /* default to node type */

            if ( event_cmd == MTC_EVENT_PMON_CLEAR )
            {
                dlog ("pmond degrade clear\n" );
                snprintf( &event.buf[0], MAX_PROCESS_NAME_LEN, "%s", "pmond" );
            }
            else if (( event_cmd == MTC_EVENT_PMON_CRIT ) ||
                     ( event_cmd == MTC_EVENT_PMON_MAJOR ))
            {
                wlog ("%s caused degrade assert\n", ptr->process );
            }
            else if ( event_cmd == MTC_EVENT_PMON_MINOR )
            {
                slog ("degrade does not apply to minor\n" );
                rc = FAIL_BAD_CASE ;
            }

            /* override with subfunction case */
            if (( ctrl_ptr->subfunction != 0 ) &&
                ( ctrl_ptr->subfunction != ctrl_ptr->function ))
            {
                if ( ptr->subfunction != NULL )
                {
                    string temp = ptr->subfunction ;
                    event.parm[0]= get_host_function_mask (temp) ;
                    if ( ( event_cmd == MTC_EVENT_PMON_MINOR) ||
                         ( event_cmd == MTC_EVENT_PMON_MAJOR) ||
                         ( event_cmd == MTC_EVENT_PMON_LOG)   ||
                         ( event_cmd == MTC_EVENT_PMON_CRIT ) )
                    {
                        mlog ("%s process failed\n", ptr->process );
                    }
                    else if (( event_cmd == MTC_EVENT_PMON_CLEAR ) && ( ptr->was_failed == true ))
                    {
                        ilog ("%s process recovered\n", ptr->process );
                        ptr->was_failed = false ;
                    }
                }
            }
        }
    }
    else if ( event_cmd == MTC_EVENT_LOOPBACK )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_loopback_header());

        /* We don't use the buffer for pmon events to remove it from the size */
        bytes = ((sizeof(mtc_message_type))-(BUF_SIZE));
    }
    else
    {
        elog ("Unsupported process monitor event (%d)\n", event_cmd );
        return ( FAIL_BAD_CASE );
    }

    event.cmd = event_cmd ;

    print_mtc_message ( LOCALHOST, MTC_CMD_TX, event, get_iface_name_str(MGMNT_INTERFACE), false );

    /* Send the event */
    if ((rc = pmon_sock.event_sock->write((char*)&event.hdr[0], bytes)) != bytes )
    {
        elog ("Message send failed. (%d)\n", rc);
        elog ("Message: %d bytes to <%s:%d>\n", bytes,
                pmon_sock.event_sock->get_dst_addr()->toString(),
                    pmon_sock.event_sock->get_dst_addr()->getPort());
    }
    else
    {
        string severity = get_event_str ( event.cmd );
        mlog ("Sending '%s' event for process '%s' to %s:%d (bytes:%d)\n",
                  severity.c_str(), event.buf,
                  pmon_sock.event_sock->get_dst_addr()->toString(),
                  pmon_sock.event_sock->get_dst_addr()->getPort(), bytes);
        rc = PASS ;
    }
    return rc ;
}

/**************************************************************************
 *
 * **********        A C T I V E   M O N I T O R I N G           **********
 *
 *************************************************************************/


int  amon_port_init ( int port )
{
    int val = 1    ;
    int rc  = FAIL ;
    if ( port )
    {
        pmon_sock.amon_port = port ;
        pmon_sock.amon_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if ( 0 >= pmon_sock.amon_sock )
            return (-errno);

        if ( setsockopt ( pmon_sock.amon_sock , SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1 )
        {
            wlog ( "amon: failed to set active monitor socket as re-useable (%d:%s)\n",
                    errno, strerror(errno));
        }

        /* Set socket to be non-blocking.  */
        rc = ioctl(pmon_sock.amon_sock, FIONBIO, (char *)&val);
        if ( 0 > rc )
        {
            elog ("Failed to set amon socket non-blocking\n");
        }

        /* Setup with localhost ip */
        memset(&pmon_sock.amon_addr, 0, sizeof(struct sockaddr_in));
        pmon_sock.amon_addr.sin_family = AF_INET ;
        // pmon_sock.amon_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        pmon_sock.amon_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        pmon_sock.amon_addr.sin_port = htons(pmon_sock.amon_port) ;

        /* bind socket to the receive addr */
        if ( bind ( pmon_sock.amon_sock, (const struct sockaddr *)&pmon_sock.amon_addr, sizeof(struct sockaddr_in)) == -1 )
        {
            elog ( "failed to bind to rx socket with port %d (%d:%s)\n", port, errno, strerror(errno) );
            close (pmon_sock.amon_sock);
            pmon_sock.amon_sock = 0 ;
            return (-errno);
        }
        else
        {
            rc = PASS ;
        }
    }
    else
    {
        elog ("No port specified\n");
    }

    return (rc) ;
}

int  open_process_socket ( process_config_type * ptr )
{
    int rc = FAIL ;

    /* Prop the port numnber into the message struct */
    if ( ptr->port )
        ptr->msg.tx_port = ptr->port ;

    if ( ptr->msg.tx_port )
    {
        /* if the sock is already open then close it first */
        if ( ptr->msg.tx_sock )
        {
            wlog ("%s open on already open socket %d, closing first\n",
                      ptr->process, ptr->msg.tx_sock );
            close (ptr->msg.tx_sock);
        }
        ptr->msg.tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if ( 0 >= ptr->msg.tx_sock )
            return (-errno);

        /* Setup with localhost ip */
        memset(&ptr->msg.tx_addr, 0, sizeof(struct sockaddr_in));
        ptr->msg.tx_addr.sin_family = AF_INET ;
        ptr->msg.tx_addr.sin_addr.s_addr = inet_addr(LOOPBACK_IP);
        ptr->msg.tx_addr.sin_port = htons(ptr->msg.tx_port) ;

        rc = PASS ;
    }
    else
    {
        elog ("%s has no port specified\n", ptr->process );
    }
    return (rc) ;
}

void close_process_socket ( process_config_type * ptr )
{
    if ( ptr->msg.tx_sock )
        close ( ptr->msg.tx_sock );
}

int  amon_service_inbox ( int processes )
{
    #define MAX_T 100
    int count = 0 ;
    int bytes = 0 ;
    char buf[AMON_MAX_LEN] ;
    socklen_t len = sizeof(struct sockaddr_in) ;
    do
    {
        char str[AMON_MAX_LEN] ;
        unsigned int magic = 0 ;
        int seq = 0 ;
        memset ( buf,0,sizeof(buf));
        memset ( str,0,sizeof(str));
        bytes = recvfrom( pmon_sock.amon_sock, buf, AMON_MAX_LEN, 0, (struct sockaddr *)&pmon_sock.amon_addr, &len);
        if ( bytes > 0 )
        {
            sscanf ( buf, "%99s %8x %d", str, &magic, &seq );
            if ( AMON_MAGIC_NUM == (magic ^ -1) )
            {
                if ( str[0] != '\0' )
                {
                    bool found = false ;
                    for ( int i = 0 ; i < processes ; i++ )
                    {
                        process_config_type * ptr = get_process_config_ptr (i);
                        if ( ptr != NULL )
                        {
                            if ( ! strcmp ( str, ptr->process ) )
                            {
                                found = true ;
                                alog ( "%s %x %d (found)\n", ptr->process, magic, seq );
                                if ( ptr->waiting == true )
                                {
                                    ptr->rx_sequence = seq ;
                                }
                                else
                                {
                                    wlog_throttled ( count , MAX_T, "%s unexpected monitor pulse\n", ptr->process );
                                }
                                break ;
                            }
                        }
                    }
                    if ( !found )
                    {
                        wlog_throttled (count, MAX_T, "Unexpected message (%s)\n", buf );
                    }
                }
                else
                {
                    wlog ("Null string !\n");
                }
            }
            else
            {
                 wlog_throttled ( count, MAX_T, "message with invalid magic number (%x)\n", magic );
            }
        }
        else if (( 0 > bytes ) && ( errno != EINTR ) && ( errno != EAGAIN ))
        {
            wlog_throttled ( count , MAX_T, "receive error (%d:%s)\n", errno, strerror(errno));
        }
    } while ( bytes > 0 ) ;

    /* Put the messages in he structs */
    return (PASS);
}


int amon_send_request ( process_config_type * ptr )
{
    int rc ;

    ptr->rx_sequence = 0 ;
    memset  ( ptr->msg.tx_buf, 0, sizeof(ptr->msg.tx_buf));
    sprintf ( ptr->msg.tx_buf, "%s %x %u", ptr->process, AMON_MAGIC_NUM, ++ptr->tx_sequence ) ;

    rc = sendto (          ptr->msg.tx_sock,
                           ptr->msg.tx_buf ,
                  strlen ( ptr->msg.tx_buf), 0,
      (struct sockaddr *) &ptr->msg.tx_addr,
                 sizeof(struct sockaddr_in));
    if ( 0 >= rc )
    {
        elog ("%s sendto error (%d:%s) (%s) (%s)\n",
                  ptr->process,
                  errno , strerror(errno),
                  ptr->msg.tx_buf,
                  inet_ntoa(ptr->msg.tx_addr.sin_addr));
    }
    else
    {
        mlog3 ("%s\n", &ptr->msg.tx_buf[0] );
        rc = PASS ;
    }
    return (rc);
}


#define MAX_COMMANDS (4)

/******************************************************************
 *
 * Handle pmon command request messages
 *
 * Supports a max MAX_COMMANDS number of queued messages per call
 * Checks for valid command string and content before action
 *
 * message format : json string { key:value , key:value }
 *
 * supported requests:
 *
 *   { "command":"restart", "process":"<process name>" }
 *
 *****************************************************************/
void pmon_service_inbox ( void )
{
    int bytes   = 0 ;
    int retries = 0 ;
    do
    {
        /* Receive command messages */
        char cmd_buf[MAX_COMMAND_LEN] ;

        memset ( &cmd_buf, 0, sizeof(cmd_buf));

        bytes = pmon_sock.cmd_sock->read((char*)&cmd_buf, MAX_COMMAND_LEN );
        if ( bytes > 0 )
        {
            if ( bytes <= MAX_COMMAND_LEN )
            {
                bool string_is_terminated = false ;
                for ( int i = 0 ; i < MAX_COMMAND_LEN ; i++ )
                {
                    if ( cmd_buf[i] == '\0' )
                    {
                        string_is_terminated = true ;
                        break ;
                    }
                }
                if ( string_is_terminated )
                {
                    string command ;
                    string process ;

                    mlog1 ("rx <- %s\n", cmd_buf );
                    int rc1 = jsonUtil_get_key_val ( &cmd_buf[0], "command", command );
                    int rc2 = jsonUtil_get_key_val ( &cmd_buf[0], "process", process );
                    mlog ("cmd:%s process:%s\n", command.c_str(), process.c_str());
                    if ( rc1 || rc2 )
                    {
                       ilog ("failed to parse command request.\n");
                       ilog ("... expecting: command:<command>, process:<process>\n");
                       wlog ("... received : %s\n", cmd_buf );
                    }
                    else
                    {
                        if ( (!command.compare("none")) || (!process.compare("none")))
                        {
                            wlog ("one or more invalid command request key:value pairs\n");
                            wlog ("... command:%s process:%s\n",
                                       command.c_str(),
                                       process.c_str());
                        }
                        /* handle start command
                         * - get the pointer to the specified process
                         * - if its in the stopped state then take it out of that state
                         * - inject it into the respawn phase of the passive monitor FSM
                         */
                        else if ( !command.compare("start"))
                        {
                            process_config_type * ptr = get_process_config_ptr ( process );
                            ilog ("%s process 'start' request\n", process.c_str());
                            if ( ptr != NULL )
                            {
                                if ( strcmp ( ptr->mode, "status" ) == 0 )
                                {
                                   wlog ("%s process-start rejected\n", process.c_str());
                                   wlog ("%s ... status monitoring mode 'start' not supported\n", process.c_str());
                                }
                                else if ( ptr->stopped == true )
                                {
                                    mtcTimer_reset ( ptr->pt_ptr );
                                    ptr->failed  = true  ; // so get_events will ignore it till process respawn is complete
                                    ptr->stopped = false ; // take the process out of the stopped state
                                    ptr->ignore  = false ; // have the fsm stop ignoriing the process ; start respawn
                                    passiveStageChange ( ptr, PMON_STAGE__RESPAWN );
                                }
                                else
                                {
                                    wlog ("%s process is not in the stopped state ; start request ignored\n", ptr->process );
                                }
                            }
                        }
                        /* handle stop command
                         * - get the pointer to the specified process
                         * - unregister the process to avoid a kernel notification
                         * - kill the process
                         * - put it in the ignored state
                         * - put it in the stopped state
                         * - reinitialize its active monitoring states and stats
                         * - start the auto recovery timer
                         */
                        else if ( !command.compare("stop"))
                        {
                            process_config_type * ptr = get_process_config_ptr ( process );
                            ilog ("%s process 'stop' request\n", process.c_str());
                            if ( ptr != NULL )
                            {
                                if ( strcmp ( ptr->mode, "status" ) == 0 )
                                {
                                   wlog ("%s process-stop rejected\n", process.c_str());
                                   wlog ("%s ... status monitoring mode 'stop' not supported\n", process.c_str());
                                }
                                else if  ( ptr->stopped == true )
                                {
                                    wlog ("%s process is already stopped ; stop request ignored\n", ptr->process );
                                }
                                else
                                {
                                    int auto_recovery_timeout = MTC_MINS_30 ;
                                    unregister_process ( ptr );
                                    kill_running_process ( ptr->pid );
                                    ptr->stopped = true ;
                                    ptr->ignore  = true ;
                                    passiveStageChange ( ptr, PMON_STAGE__IGNORE ) ; /* as a backup */
                                    if ( !strcmp ( ptr->mode, "active" ) )
                                    {
                                        activeStageChange ( ptr , ACTIVE_STAGE__IDLE );
                                    }

                                    mtcTimer_reset ( ptr->pt_ptr );
                                    /* Start a recovery timer */
                                    mtcTimer_start ( ptr->pt_ptr, pmon_timer_handler, auto_recovery_timeout );
                                    ilog ("%s process 'stopped' by request ; auto restart in %d seconds\n",
                                              ptr->process,
                                              auto_recovery_timeout);
                                }
                            }
                        }
                        /* handle restart command */
                        else if ( !command.compare("restart"))
                        {
                            /* handle this 'pmond' process restart request */
                            if ( !process.compare("pmond") )
                            {
                                ilog ("%s self-restart ; by request\n", process.c_str());
                                /* process is auto restarted by systemd in centos or inittab in WRL */
                                daemon_exit ();
                            }
                            else
                            {
                                process_config_type * ptr = get_process_config_ptr ( process );
                                dlog ("%s process 'restart' request\n", process.c_str());
                                if ( ptr != NULL )
                                {
                                    if ( strcmp ( ptr->mode, "status" ) == 0 )
                                    {
                                       wlog ("%s process-restart rejected\n", process.c_str());
                                       wlog ("%s ... status monitoring mode restart not supported\n", process.c_str());
                                    }
                                    else if ( ptr->restart == false )
                                    {
                                       ilog ("%s process-restart ; by request\n", process.c_str());
                                       ptr->restart = true ;
                                       if ( ptr->stopped )
                                       {
                                           ptr->stopped = false ;
                                           ptr->ignore  = false ;
                                       }
                                       passiveStageChange ( ptr, PMON_STAGE__MANAGE );
                                    }
                                    else
                                    {
                                       ilog ("%s process-restart ; in progress\n",
                                                 process.c_str());
                                    }
                                }
                                else
                                {
                                    wlog ("%s process-restart ; cannot execute, process not found\n",
                                              process.c_str());
                                }
                            }
                        }
                        else
                        {
                            wlog ("unsupported command:%s for process:%s\n",
                                   command.c_str(), process.c_str());
                        }
                    } /* end else */
                }
                else
                {
                    wlog ("badly formed command request (%d) (not null terminated)\n", bytes );
                }
            }
            else
            {
                ; /* message to big ; do not log to protect against DOS attack */
            }
        }
        else if ( bytes < 0 )
        {
            if ( errno == EAGAIN )
            {
                return ;
            }
            else
            {
                wlog ("commnd socket read error (%d:%d:%m)\n", bytes, errno );
            }
        }
        retries++ ;
    } while ( ( bytes != 0 ) && ( retries < MAX_COMMANDS ) ) ;
}

