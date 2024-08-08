/*
 * Copyright (c) 2013-2018, 2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance "Compute Messaging"
 * Implementation
 */

/**
 * @detail
 * Detailed description ...
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/un.h>    /* for ... unix domain sockets     */
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <syslog.h>    /* for ... syslog                  */
#include <sys/wait.h>  /* for ... waitpid                 */
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <list>        /* for ... list of conf file names */
#include <unistd.h>    /* for ... sync                    */

using namespace std;

#define __AREA__ "msg"

#include "nodeClass.h"      /* for ... maintenance class nodeLinkClass */
#include "jsonUtil.h"       /* for ... Json utilities                  */
#include <json-c/json.h>    /* for ... json-c json string parsing       */
#include "mtcNodeMsg.h"     /* for ... daemon socket structure         */
#include "mtcNodeComp.h"    /* for ... this module header              */
#include "nodeUtil.h"       /* for ... Utility Service Header          */
#include "daemon_common.h"
#include "regexUtil.h"      /* for ... Regex and String utilities      */

extern "C"
{
#include "amon.h"           /* for ... active monitoring utilities        */
}

/************************************************************************
 *
 * Name        : stop pmon
 *
 * Purpose     : Used before issuing the self reboot so that pmond
 *               does not try and recover any processes,
 *               most importantly this one which would
 *               cancel the sysreq failsafe thread.
 *
 ************************************************************************/
void stop_pmon( void )
{
    /* max pipe command response length */
    #define PIPE_COMMAND_RESPON_LEN (100)

    ilog("Stopping collectd.");
    int rc = system("/usr/local/sbin/pmon-stop collectd");
    sleep (2);
    ilog("Stopping pmon to prevent process recovery during shutdown");
    for ( int retry = 0 ; retry < 5 ; retry++ )
    {
        char pipe_cmd_output [PIPE_COMMAND_RESPON_LEN] ;
        rc = system("/usr/bin/systemctl stop pmon");
        sleep(2);

        /* confirm pmon is no longer active */
        execute_pipe_cmd ( "/usr/bin/systemctl is-active pmon", &pipe_cmd_output[0], PIPE_COMMAND_RESPON_LEN );
        if ( strnlen ( pipe_cmd_output, PIPE_COMMAND_RESPON_LEN ) > 0 )
        {
            string temp = pipe_cmd_output ;
            if ( temp.find ("inactive") != string::npos )
            {
                ilog("pmon is now inactive (%d:%d)", retry, rc);
                break ;
            }
            else
            {
                ilog("pmon is not inactive (%s) ; retrying (%d:%d)",
                      temp.c_str(), retry, rc);
            }
        }
        else
        {
            elog("pmon status query failed ; retrying (%d:%d)", retry, rc);
        }
    }
}

/* Receive and process commands from controller maintenance */
int mtc_service_command ( mtc_socket_type * sock_ptr, int interface )
{
    mtc_message_type msg ;
    int rc = FAIL ;
    ssize_t bytes_received = 0 ;
    ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
    bool log_ack = true ;
    const char * iface_name_ptr = get_interface_name_str(interface) ;

    if ( interface == CLSTR_INTERFACE )
    {
        if ( ! ctrl_ptr->clstr_iface_provisioned )
        {
            wlog ("cannot receive from unprovisioned %s interface", iface_name_ptr);
            return (rc);
        }
    }

    /* clean the rx/tx buffer */
    memset ((void*)&msg,0,sizeof(mtc_message_type));
    string hostaddr = "" ;
    if ( interface == PXEBOOT_INTERFACE )
    {
        if ( sock_ptr->pxeboot_rx_socket )
        {
            struct sockaddr_in  client_addr;
            socklen_t addr_len = sizeof(client_addr);

            // Receive data
            bytes_received = recvfrom(sock_ptr->pxeboot_rx_socket,
                                        (char*)&msg.hdr[0],
                                        sizeof(mtc_message_type), 0,
                                        (struct sockaddr*)&client_addr, &addr_len);
            // Terminate the buffer
            msg.hdr[bytes_received] = '\0' ;

            // Log with debug_msg lane 2
            if ( daemon_get_cfg_ptr()->debug_msg&2 )
            {
                // log the message ; both header and buffer
                string _buf = msg.buf[0] ? msg.buf : "empty";
                ilog ("Received %ld bytes (%s) from %s:%d - %s:%s",
                       bytes_received,
                       iface_name_ptr,
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port),
                       &msg.hdr[0], _buf.c_str());
                // dump_memory (&msg.hdr[0], 16, bytes_received);
            }
            hostaddr = inet_ntoa(client_addr.sin_addr);
        }
    }
    else if ( interface == MGMNT_INTERFACE )
    {
        if (( sock_ptr->mtc_client_mgmt_rx_socket ) &&
            ( sock_ptr->mtc_client_mgmt_rx_socket->sock_ok() == true ))
        {
            rc = bytes_received = sock_ptr->mtc_client_mgmt_rx_socket->read((char*)&msg.hdr[0], sizeof(mtc_message_type));
            hostaddr = sock_ptr->mtc_client_mgmt_rx_socket->get_src_str();

            // Log with debug_msg lane 2
            if ( daemon_get_cfg_ptr()->debug_msg&2 )
            {
                // Log the message ; both header and buffer
                string _buf = msg.buf[0] ? msg.buf : "empty";
                ilog ("Received %ld bytes (%s) from %s - %s:%s", bytes_received,
                       iface_name_ptr, hostaddr.c_str(), &msg.hdr[0], _buf.c_str());
            }
        }
        else
        {
            elog ("cannot read from null or failed 'mtc_client_mgmt_rx_socket'\n");
            return (FAIL_TO_RECEIVE);
        }
    }
    else if ( interface == CLSTR_INTERFACE )
    {
        if (( sock_ptr->mtc_client_clstr_rx_socket ) &&
            ( sock_ptr->mtc_client_clstr_rx_socket->sock_ok() == true ))
        {
            rc = bytes_received = sock_ptr->mtc_client_clstr_rx_socket->read((char*)&msg.hdr[0], sizeof(mtc_message_type));
            hostaddr = sock_ptr->mtc_client_clstr_rx_socket->get_src_str();

            // Log with debug_msg lane 2
            if ( daemon_get_cfg_ptr()->debug_msg&2 )
            {
               // Log the message ; both header and buffer
               string _buf = msg.buf[0] ? msg.buf : "empty";
               ilog ("Received %ld bytes (%s) from %s: %s:%s",
                      bytes_received, iface_name_ptr,
                      hostaddr.c_str(), &msg.hdr[0], _buf.c_str());
            }
        }
        else
        {
            elog ("cannot read from null or failed 'mtc_client_clstr_rx_socket'\n");
            return (FAIL_TO_RECEIVE);
        }
    }

    if( rc <= 0 )
    {
        if ( ( errno == EINTR ) || ( errno == EAGAIN ))
        {
            return (RETRY);
        }
        else
        {
            return (FAIL_TO_RECEIVE);
        }
    }
    rc = PASS ;

    bool self = false ;
    if (( hostaddr == ctrl_ptr->address ) ||
        ( hostaddr == ctrl_ptr->address_clstr ))
    {
        self = true ;
    }
    string interface_name = get_interface_name_str (interface) ;
    string command_name = get_mtcNodeCommand_str(msg.cmd) ;

    /* Message version greater than zero have the hosts management
     * mac address appended to the header string */
    if (( !self ) && ( msg.ver >= MTC_CMD_FEATURE_VER__MACADDR_IN_CMD ))
    {
        /* the minus 1 is to back up from the null char that is accounted for in the hearder size */
        if ( strncmp ( &msg.hdr[MSG_HEADER_SIZE-1], ctrl_ptr->macaddr.data(), MSG_HEADER_SIZE ))
        {
            wlog ("%s req command from %s network not for this host (exp:%s det:%s) ; ignoring ...\n",
                      command_name.c_str(),
                      iface_name_ptr,
                      ctrl_ptr->macaddr.c_str(),
                      &msg.hdr[MSG_HEADER_SIZE-1]);
            print_mtc_message ( get_hostname(), MTC_CMD_RX, msg, iface_name_ptr, true );
            return (FAIL_INVALID_DATA);
        }
    }

    if ( ! hostaddr.empty() )
        print_mtc_message ( hostaddr, MTC_CMD_RX, msg, iface_name_ptr, false );

    /* Check for response messages */
    if ( strstr ( &msg.hdr[0], get_cmd_req_msg_header() ) )
    {
        rc = PASS ;
        if ( msg.cmd == MTC_REQ_MTCALIVE )
        {
            alog1 ("mtcAlive request received from %s network", iface_name_ptr);
            if ( interface == PXEBOOT_INTERFACE )
            {
                alog2 ("pxeboot mtcAlive buffer: %s", &msg.buf[0]);
                load_pxebootInfo_msg(msg);

#ifdef WANT_FIT_TESTING
                if ( ! daemon_want_fit ( FIT_CODE__FAIL_PXEBOOT_MTCALIVE ) )
#endif
                {
                    send_mtcAlive_msg ( sock_ptr, ctrl_ptr->who_i_am, interface );
                }
            }
            return (rc);
        }
        else if ( msg.cmd == MTC_MSG_INFO )
        {
            alog2 ("mtc 'info' message received from %s network", iface_name_ptr);
            load_mtcInfo_msg ( msg );
            return ( PASS ); /* no ack for this message */
        }
        else if ( msg.cmd == MTC_CMD_SYNC )
        {
            ilog ("mtc '%s' message received from %s network",
                   get_mtcNodeCommand_str(msg.cmd),
                   iface_name_ptr);

            ilog ("Sync Start");
            sync ();
            ilog ("Sync Done");

            return ( PASS ); /* no ack for this message */
        }
        else if ( msg.cmd == MTC_MSG_LOCKED )
        {
            log_ack = false ;

            /* Only recreate the file if its not already present */
            if ( daemon_is_file_present ( NODE_LOCKED_FILE ) == false )
            {
                ilog ("%s locked (%s)", get_hostname().c_str(), iface_name_ptr);
                daemon_log ( NODE_LOCKED_FILE, ADMIN_LOCKED_STR);
            }

            /* Only create the non-volatile NODE_LOCKED_FILE_BACKUP file if the
             * LOCK_PERSIST flag is present. */
            if ( msg.num && msg.parm[MTC_PARM_LOCK_PERSIST_IDX] )
            {
                /* Preserve the node locked state in a non-volatile backup
                 * file that persists over reboot.
                 * Maintaining the legacy NODE_LOCKED_FILE as other sw looks at it. */
                if ( daemon_is_file_present ( NODE_LOCKED_FILE_BACKUP ) == false )
                    daemon_log ( NODE_LOCKED_FILE_BACKUP, ADMIN_LOCKED_STR );
            }
            /* Otherwise if we get a locked message without the LOCK_PERSIST flag
             * then remove the non-volatile NODE_LOCKED_FILE_BACKUP file if exists */
            else if ( daemon_is_file_present ( NODE_LOCKED_FILE_BACKUP ) == true )
                daemon_remove_file ( NODE_LOCKED_FILE_BACKUP );
        }
        else if ( msg.cmd == MTC_MSG_UNLOCKED )
        {
            ilog ("%s unlocked received from %s network", get_hostname().c_str(), iface_name_ptr);

            /* Only remove the file if it is present */
            if ( daemon_is_file_present ( NODE_LOCKED_FILE ) == true )
            {
                daemon_remove_file ( NODE_LOCKED_FILE );
            }
            if ( daemon_is_file_present ( NODE_LOCKED_FILE_BACKUP ) == true )
            {
                daemon_remove_file ( NODE_LOCKED_FILE_BACKUP );
                ilog ("cleared node locked backup flag (%s)", iface_name_ptr);
            }
        }
        else if ( msg.cmd == MTC_MSG_SUBF_GOENABLED_FAILED )
        {
            /* remove the GOENABLED_SUBF_PASS and create GOENABLED_SUBF_FAIL file */
            daemon_remove_file ( GOENABLED_SUBF_PASS );
            daemon_log ( GOENABLED_SUBF_FAIL, "host subfunction has failed as instructed by maintenance.");
            return (PASS);
        }
        else if ( msg.cmd == MTC_REQ_MAIN_GOENABLED )
        {
            time_t time_now ;
            double goenabled_age ;
            ctrl_type * ctrl_ptr = get_ctrl_ptr () ;

            time (&time_now); // current time in seconds (UTC)
            goenabled_age = difftime ( time_now, ctrl_ptr->goenabled_main_time );

            /* Check to see if we are already running the requested test */
            if ( ctrl_ptr->active_script_set == GOENABLED_MAIN_SCRIPTS )
            {
                ilog ("GoEnabled In-Progress\n");
            }
            /* Report PASS immediately if there was a recent PASS already */
            else if (( daemon_is_file_present ( GOENABLED_MAIN_PASS ) &&
                     ( goenabled_age < MTC_MINS_20 )))
            {
                ilog ("GoEnabled Passed (%f seconds ago)\n", goenabled_age );
                send_mtc_msg ( sock_ptr, MTC_MSG_MAIN_GOENABLED, "" );
            }
            else
            {
                ilog ("GoEnabled request posted (%s)", iface_name_ptr);
                ctrl_ptr->posted_script_set.push_back ( GOENABLED_MAIN_SCRIPTS );
                ctrl_ptr->posted_script_set.unique();
            }
            rc = PASS ;
        }
        else if ( msg.cmd == MTC_REQ_SUBF_GOENABLED )
        {
            time_t time_now ;
            double goenabled_age ;
            ctrl_type * ctrl_ptr = get_ctrl_ptr () ;

            time (&time_now); // current time in seconds (UTC)
            goenabled_age = difftime ( time_now, ctrl_ptr->goenabled_subf_time );
            if ( ctrl_ptr->active_script_set == GOENABLED_SUBF_SCRIPTS )
            {
                ilog ("GoEnabled SubF In-Progress\n");
            }
            /* eport PASS immediately if there was a recent PASS already */
            else if (( daemon_is_file_present ( GOENABLED_SUBF_PASS ) &&
                     ( goenabled_age < MTC_MINS_20 )))
            {
                ilog ("GoEnabled SubF Passed (%f seconds ago)\n", goenabled_age);
                send_mtc_msg ( sock_ptr, MTC_MSG_SUBF_GOENABLED, "" );
            }
            else
            {
                ilog ("GoEnabled Subf request posted (%s)", iface_name_ptr);

                /* Cleanup test result flag files */
                if ( daemon_is_file_present ( GOENABLED_SUBF_PASS) )
                {
                    ilog ("clearing stale %s file\n", GOENABLED_SUBF_PASS );
                    daemon_remove_file (GOENABLED_SUBF_PASS) ;
                }

                if ( daemon_is_file_present ( GOENABLED_SUBF_FAIL) )
                {
                    ilog ("clearing stale %s file\n", GOENABLED_SUBF_FAIL );
                    daemon_remove_file (GOENABLED_SUBF_FAIL) ;
                }
                ctrl_ptr->posted_script_set.push_back ( GOENABLED_SUBF_SCRIPTS );
                ctrl_ptr->posted_script_set.unique();
            }
            rc = PASS ;
        }
        else if ( msg.cmd == MTC_CMD_REBOOT )
        {
            ilog ("%s command received from %s network",
                      command_name.c_str(),
                      iface_name_ptr);
        }
        else if ( msg.cmd == MTC_CMD_LAZY_REBOOT )
        {
            ilog ("%s command received from %s network ; delay:%d seconds",
                      command_name.c_str(),
                      iface_name_ptr,
                      msg.num ? msg.parm[0] : 0 );
        }
        else if ( is_host_services_cmd ( msg.cmd ) == true )
        {
            ctrl_type * ctrl_ptr = get_ctrl_ptr () ;
                ilog ("%s request from %s network", 
                          command_name.c_str(),
                          iface_name_ptr);
            if ( msg.cmd == MTC_CMD_START_CONTROL_SVCS )
            {
                ctrl_ptr->start_controller_hostservices = true ;
                rc = PASS ;
            }
            else if ( msg.cmd == MTC_CMD_START_WORKER_SVCS  )
            {
                ctrl_ptr->start_worker_hostservices = true ;
                rc = PASS ;
            }
            else if ( msg.cmd == MTC_CMD_START_STORAGE_SVCS )
            {
                ctrl_ptr->start_storage_hostservices = true ;
                rc = PASS ;
            }
            else
            {
                ctrl_ptr->posted_script_set.push_back ( HOSTSERVICES_SCRIPTS );
                ctrl_ptr->posted_script_set.unique ();
                ctrl_ptr->hostservices.posted  = msg.cmd ;
                ctrl_ptr->hostservices.monitor = MTC_CMD_NONE ;
                rc = PASS ;
            }

            /* Fault insertion - fail host services command */
            if ( ( daemon_is_file_present ( MTC_CMD_FIT__START_SVCS )))
            {
                rc = FAIL_FIT ;
                wlog ("%s Start Services - fit failure (%s)",
                          command_name.c_str(),
                          iface_name_ptr);
            }

            /* Fault insertion - fail to send host services ACK */
            if ( ( daemon_is_file_present ( MTC_CMD_FIT__NO_HS_ACK )))
            {
                wlog ("%s Start Services - fit no ACK (%s)",
                          command_name.c_str(),
                          iface_name_ptr);
                return (PASS);
            }

            /* inform mtcAgent of enhanced ost services support */
            msg.parm[1] = MTC_ENHANCED_HOST_SERVICES ;
            msg.parm[0] = rc ;
            msg.num     =  2 ;

            if ( rc )
            {
                snprintf (msg.buf, BUF_SIZE, "host service launch failed (rc:%d)", rc );
            }
            else
            {
                snprintf (msg.buf, BUF_SIZE, "host service launched");
            }
        }
        else if ( msg.cmd == MTC_CMD_WIPEDISK )
        {
            ilog ("Reload command received from %s network", iface_name_ptr);
        }
        else if ( msg.cmd == MTC_CMD_RESET )
        {
            ilog ("Reset command received from %s network", iface_name_ptr);
        }
        else if ( msg.cmd == MTC_CMD_LOOPBACK )
        {
            ilog ("Loopback command received from %s network", iface_name_ptr);
        }
        else
        {
            rc = FAIL_BAD_CASE ;
            wlog ( "Unsupported maintenance command (%d) with %ld bytes received from %s network",
                    msg.cmd, bytes_received, iface_name_ptr );
        }

        snprintf ( &msg.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_rsp_msg_header());
    }
    else if ( strstr ( &msg.hdr[0], get_msg_rep_msg_header()) )
    {
        if ( msg.cmd == MTC_MSG_MAIN_GOENABLED )
        {
            ilog ("main function goEnabled results acknowledged from %s network", iface_name_ptr);
            return (PASS);
        }
        else if ( msg.cmd == MTC_MSG_SUBF_GOENABLED )
        {
            ilog ("sub-function goEnabled results acknowledged from %s network", iface_name_ptr);
            return (PASS);
        }
        else
        {
            dlog2 ( "reply message for command %d\n", msg.cmd );
            return (PASS);
        }
    }

    else if ( strstr ( &msg.hdr[0], get_worker_msg_header()) )
    {
        if ( msg.cmd == MTC_MSG_MTCALIVE )
        {
            wlog ("unexpected mtcAlive message from %s from %s network",
                   hostaddr.c_str(), iface_name_ptr);
        }
        else
        {
            wlog ("unsupported worker message from %s from %s network",
                   hostaddr.c_str(), iface_name_ptr);
        }

        wlog ("WARNING: mtcClient is receiving mtcAgent bound mtcAlive messages");
        // dump_memory (&msg, 16, bytes_received);
        return PASS ;
    }
    else
    {
        wlog ("unsupported message from %s from %s network", hostaddr.c_str(), iface_name_ptr);
        // dump_memory (&msg, 16, bytes_received);
        return PASS ;
    }

/***********************************************************
 *
 * If we get here, the response should be sent
 * regardless of the execution status.
 *
 *  if ( rc == PASS )
 **********************************************************/
    {
        rc = PASS ;

        int bytes = sizeof(mtc_message_type)-BUF_SIZE;

        if ( interface == PXEBOOT_INTERFACE )
        {
            int flags = 0 ; // no tx flags
            if ( sock_ptr->pxeboot_tx_socket <= 0 )
            {
                elog("pxeboot_tx_socket not ok (%d)", sock_ptr->pxeboot_tx_socket);
                return (FAIL_SOCKET_SENDTO);
            }

            if ( log_ack )
            {
                ilog ("sending %s ack to %s over %s network",
                       command_name.c_str(),
                       hostaddr.c_str(),
                       iface_name_ptr);
            }

            struct sockaddr_in hostAddr;
            memset(&hostAddr, 0, sizeof(hostAddr));
            print_mtc_message ( hostaddr.data(), MTC_CMD_TX, msg, iface_name_ptr, false );
            hostAddr.sin_addr.s_addr = inet_addr(hostaddr.data());
            hostAddr.sin_family = AF_INET;
            hostAddr.sin_port   = htons(sock_ptr->mtc_tx_pxeboot_port);

            ssize_t bytes_sent = sendto(sock_ptr->pxeboot_tx_socket, &msg.hdr[0], bytes, flags,
                                        (const struct sockaddr*)&hostAddr, sizeof(hostAddr));
            if (bytes_sent <= 0)
            {
                elog ("failed to send %s ack to %s:%d on %s network (rc:%ld) (%d:%m)",
                        command_name.c_str(),
                        hostaddr.c_str(),
                        hostAddr.sin_port,
                        iface_name_ptr,
                        bytes_sent, errno);
            }
        }

        /* send the message back either over the mgmnt or clstr interface */
        else if ( interface == MGMNT_INTERFACE )
        {
            if (( sock_ptr->mtc_client_mgmt_tx_socket ) &&
                ( sock_ptr->mtc_client_mgmt_tx_socket->sock_ok() == true ))
            {
                rc = sock_ptr->mtc_client_mgmt_tx_socket->write((char*)&msg.hdr[0], bytes);
                if ( rc <= 0 )
                {
                    elog ("%s reply send (mtc_client_mgmt_tx_socket) failed (%s) (rc:%d)",
                              command_name.c_str(),
                              iface_name_ptr, rc);
                }
                else if ( log_ack )
                {
                    ilog ("%s reply send (%s)",
                              command_name.c_str(),
                              iface_name_ptr);
                }
            }
            else
            {
                elog ("cannot send to null or failed socket (%s)", iface_name_ptr);
            }
        }
        else if ( interface == CLSTR_INTERFACE )
        {
            if (( sock_ptr->mtc_client_clstr_tx_socket_c0 ) &&
                ( sock_ptr->mtc_client_clstr_tx_socket_c0->sock_ok() == true ))
            {
                rc = sock_ptr->mtc_client_clstr_tx_socket_c0->write((char*)&msg.hdr[0], bytes);
                if ( rc <= 0 )
                {
                    elog ("%s reply send (mtc_client_clstr_tx_socket_c0) failed (%s) (rc:%d)",
                              command_name.c_str(),
                              iface_name_ptr, rc);
                }
                else if ( log_ack )
                {
                    ilog ("%s reply send (%s)",
                              command_name.c_str(),
                              iface_name_ptr);
                }
            }
            if (( sock_ptr->mtc_client_clstr_tx_socket_c1 ) &&
                ( sock_ptr->mtc_client_clstr_tx_socket_c1->sock_ok() == true ))
            {
                rc = sock_ptr->mtc_client_clstr_tx_socket_c1->write((char*)&msg.hdr[0], bytes);
                if ( rc <= 0 )
                {
                    elog ("%s reply send (mtc_client_clstr_tx_socket_c1) failed (%s) (rc:%d)",
                              command_name.c_str(),
                              iface_name_ptr, rc);
                }
                else if ( log_ack )
                {
                    ilog ("%s reply send (%s)",
                              command_name.c_str(),
                              iface_name_ptr);
                }
            }
        }

        print_mtc_message ( get_hostname(), MTC_CMD_TX, msg, iface_name_ptr, (rc != bytes) );

        /* get the shutdown delay config alue */
        int delay = daemon_get_cfg_ptr()->failsafe_shutdown_delay ;
        if ( delay < 1 )
            delay = 2 ;

        daemon_dump_info  ();

        if ( msg.cmd == MTC_CMD_REBOOT )
        {
            if ( daemon_is_file_present ( MTC_CMD_FIT__NO_REBOOT ) )
            {
                ilog ("Reboot - fit bypass (%s)", iface_name_ptr);
                return (PASS);
            }
            stop_pmon();
            ilog ("Reboot (%s)", iface_name_ptr);
            daemon_log ( NODE_RESET_FILE, "reboot command" );
            fork_sysreq_reboot ( delay );
            rc = system("/usr/bin/systemctl reboot");
        }
        if ( msg.cmd == MTC_CMD_LAZY_REBOOT )
        {
            daemon_log ( NODE_RESET_FILE, "lazy reboot command" );

            /* stop pmon before issuing the lazy reboot so that it does not
             * try and recover any processes, most importantly this one */
            stop_pmon();

            if ( msg.num >= 1 )
            {
                do
                {
                    ilog ("Lazy Reboot (%s) ; rebooting in %d seconds", iface_name_ptr, msg.num ? msg.parm[0] : 1 );
                    sleep (1);
                    if ( msg.parm[0] % 5 )
                    {
                        /* service the active monitoring every 5 seconds */
                        active_monitor_dispatch ();
                    }
                } while ( msg.parm[0]-- > 0 ) ;
            }
            else
            {
                ilog ("Lazy Reboot (%s) ; now", iface_name_ptr);
            }

            fork_sysreq_reboot ( delay );
            rc = system("/usr/bin/systemctl reboot");
        }
        else if ( msg.cmd == MTC_CMD_RESET )
        {
            if ( daemon_is_file_present ( MTC_CMD_FIT__NO_RESET ) )
            {
                ilog ("Reset - fit bypass (%s)", iface_name_ptr);
                return (PASS);
            }
            stop_pmon();
            ilog ("Reset 'reboot -f' (%s)", iface_name_ptr);
            daemon_log ( NODE_RESET_FILE, "reset command" );
            fork_sysreq_reboot ( delay/2 );
            rc = system("/usr/bin/systemctl reboot --force");
        }
        else if ( msg.cmd == MTC_CMD_WIPEDISK )
        {
            int parent = 0 ;

            if ( daemon_is_file_present ( MTC_CMD_FIT__NO_WIPEDISK ) )
            {
                ilog ("Wipedisk - fit bypass (%s)", iface_name_ptr);
                return (PASS);
            }
            /* We fork a reboot as a fail safe.
             * If something goes wrong we should reboot anyway
             */
            stop_pmon();
            fork_sysreq_reboot ( delay/2 );

            /* We fork the wipedisk command as it may take upwards of 30s
             * If we hold this thread for that long pmon will kill mtcClient
             * which will prevent the reboot command from being issued
             */
            if ( 0 > ( parent = double_fork()))
            {
                elog ("failed to fork wipedisk command\n");
            }
            else if( 0 == parent ) /* we're the child */
            {
                ilog ("Disk wipe in progress (%s)", iface_name_ptr);
                daemon_log ( NODE_RESET_FILE, "wipedisk command" );
                rc = system("/usr/local/bin/wipedisk --force");
                ilog ("Disk wipe complete - Forcing Reboot ...\n");
                rc = system("/usr/bin/systemctl reboot --force");
                exit (0);
            }

        }
        rc = PASS ;
    }
    return (rc);
}

/** Send an event to the mtcAgent **/
int mtce_send_event ( mtc_socket_type * sock_ptr, unsigned int cmd , const char * mtce_name_ptr )
{
    mtc_message_type event ;
    ctrl_type *ctrl_ptr = get_ctrl_ptr();

    int rc    = PASS ;
    int bytes = 0    ;

    memset (&event, 0 , sizeof(mtc_message_type));

    if ( cmd == MTC_EVENT_LOOPBACK )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_loopback_header() );

        /* We don't use the buffer for mtce events to remove it from the size */
        bytes = ((sizeof(mtc_message_type))-(BUF_SIZE));
    }
    else if ( cmd == MTC_EVENT_MONITOR_READY )
    {
        string event_info = "{\"" ;
        event_info.append(MTC_JSON_INV_NAME);
        event_info.append("\":\"");
        event_info.append(get_hostname());
        event_info.append("\",\"");
        event_info.append(MTC_JSON_SERVICE);
        event_info.append("\":\"");
        event_info.append(MTC_SERVICE_MTCCLIENT_NAME );

        event_info.append("\",\"active_controller_pxeboot_address\":\"");
        event_info.append(ctrl_ptr->pxeboot_addr_active_controller);

        event_info.append("\",\"");
        event_info.append(MTC_JSON_FEATURES);
        event_info.append("\":[\"");
        event_info.append(MTC_PXEBOOT_MTCALIVE);
        event_info.append("\"]}");

        size_t len =  event_info.length()+1 ;
        snprintf ( &event.hdr[0], MSG_HEADER_SIZE, "%s", get_mtce_event_header());
        snprintf ( &event.buf[0], len, "%s", event_info.data());
        bytes = ((sizeof(mtc_message_type))-(BUF_SIZE-len));
        dlog ("%s %s ready", get_hostname().c_str(), MTC_SERVICE_MTCCLIENT_NAME);
    }
    else if (( cmd == MTC_EVENT_AVS_CLEAR    ) ||
             ( cmd == MTC_EVENT_AVS_MAJOR    ) ||
             ( cmd == MTC_EVENT_AVS_CRITICAL ))
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_mtce_event_header() );

        /* We don't use the buffer for mtce events so remove it from the size */
        bytes = ((sizeof(mtc_message_type))-(BUF_SIZE));
    }
    else if ( is_host_services_cmd ( cmd ) == true )
    {
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_cmd_rsp_msg_header() );

        if ( mtce_name_ptr )
        {
            /* add the error message to the message buffer */
            size_t len = strnlen ( mtce_name_ptr, MAX_MTCE_EVENT_NAME_LEN );

            /* We don't use the buffer for mtce events to remove it from the size */
            bytes = ((sizeof(mtc_message_type))-(BUF_SIZE-len));

            snprintf ( &event.buf[0], MAX_MTCE_EVENT_NAME_LEN , "%s", mtce_name_ptr );
            rc = FAIL_OPERATION ;
        }
        else
        {
            /* We don't use the buffer in the pass case */
            bytes = ((sizeof(mtc_message_type))-(BUF_SIZE));
            rc = PASS ;
        }
        event.cmd     = cmd ;
        event.parm[0] = rc  ;
        event.num     = 1   ;
    }
    else
    {
        elog ("Unsupported mtce event (%d)\n", cmd );
        return ( FAIL_BAD_CASE );
    }

    event.cmd = cmd ;

    if (( sock_ptr->mtc_client_mgmt_tx_socket ) &&
        ( sock_ptr->mtc_client_mgmt_tx_socket->sock_ok() == true ))
    {
        if ( bytes == 0 )
        {
           slog ("message send failed ; message size=0 for cmd:0x%x is 0\n", event.cmd );
           rc = FAIL_NO_DATA ;
        }
        else if ((rc = sock_ptr->mtc_client_mgmt_tx_socket->write((char*)&event.hdr[0], bytes))!= bytes )
        {
            elog ("message send failed. (%d) (%d:%s) \n", rc, errno, strerror(errno));
            elog ("message: %d bytes to <%s:%d>\n", bytes,
                    sock_ptr->mtc_client_mgmt_tx_socket->get_dst_str(),
                    sock_ptr->mtc_client_mgmt_tx_socket->get_dst_addr()->getPort());
            rc = FAIL_TO_TRANSMIT ;
        }
        else
        {
            mlog2 ("Transmit: %x bytes to %s:%d\n", bytes,
                    sock_ptr->mtc_client_mgmt_tx_socket->get_dst_str(),
                    sock_ptr->mtc_client_mgmt_tx_socket->get_dst_addr()->getPort());
            print_mtc_message ( get_hostname(), MTC_CMD_TX, event, get_interface_name_str(MGMNT_INTERFACE), false );
            rc = PASS ;
        }
    }
    else
    {
       elog ("cannot send to null or failed management network socket (%s)",
              get_interface_name_str (MGMNT_INTERFACE) );
       rc = FAIL_SOCKET_SENDTO ;
    }

    // Only the events sent on the pxeboot network are:
    // - ready event
    if (( cmd == MTC_EVENT_MONITOR_READY ) &&
        ( sock_ptr->pxeboot_tx_socket > 0 ) &&
        ( !ctrl_ptr->pxeboot_addr_active_controller.empty()))
    {
        int flags = 0 ; // no tx flags
        struct sockaddr_in hostAddr;
        memset(&hostAddr, 0, sizeof(hostAddr));
        print_mtc_message ( ctrl_ptr->pxeboot_addr_active_controller.data(), MTC_CMD_TX, event, get_interface_name_str(PXEBOOT_INTERFACE), false);
        hostAddr.sin_addr.s_addr = inet_addr(ctrl_ptr->pxeboot_addr_active_controller.data());
        hostAddr.sin_family = AF_INET;
        hostAddr.sin_port   = htons(sock_ptr->mtc_tx_pxeboot_port);

        ssize_t bytes_sent = sendto(sock_ptr->pxeboot_tx_socket, &event.hdr[0], bytes, flags,
                                    (const struct sockaddr*)&hostAddr, sizeof(hostAddr));
        if (bytes_sent <= 0)
        {
            elog ("failed to send %s to %s:%d on %s network (rc:%ld) (%d:%m)",
                    get_mtcNodeCommand_str(event.cmd),
                    ctrl_ptr->pxeboot_addr_active_controller.c_str(),
                    hostAddr.sin_port,
                    get_interface_name_str(PXEBOOT_INTERFACE),
                    bytes_sent, errno);
        }
    }
    return rc ;
}

/****************************************************************************
 *
 * Name       : create_mtcAlive_msg
 *
 * Description: Creates a common mtcAlive message that consists of the
 *               - out-of-band health/status flags
 *               - host uptime
 *               - json string of some of the host's info
 *                 {
 *                   "hostname":"controller-0",
 *                   "personality":"controller,worker",
 *                   "pxeboot_ip":"169.254.202.2",
 *                   "mgmt_ip":"192.168.204.2",
 *                   "cluster_host_ip":"192.168.206.2",
 *                   "mgmt_mac":"08:00:27:9f:ef:57",
 *                   "interface":"Mgmnt",
 *                   "sequence":145
 *                 }
 *
 ****************************************************************************/
int create_mtcAlive_msg ( ctrl_type * ctrl_ptr, mtc_message_type & msg, int cmd, string identity, int interface )
{
    static int _sm_unhealthy_debounce_counter [MAX_IFACES] = {0,0} ;

    struct timespec ts ;
    clock_gettime (CLOCK_MONOTONIC, &ts );

    /* Get health state of the host - presently limited to the following
     *
     * during boot           = NODE_HEALTH_UNKNOWN
     * /var/run/.config_pass = NODE_HEALTHY
     * /var/run/.config_fail = NODE_UNHEALTHY
     *
     * */

    /* Init the message buffer */
    MEMSET_ZERO (msg);
    snprintf ( &msg.hdr[0], MSG_HEADER_SIZE, "%s", get_worker_msg_header());
    msg.cmd = cmd ;
    msg.num = MTC_PARM_MAX_IDX ;

    /* Insert the host uptime */
    msg.parm[MTC_PARM_UPTIME_IDX] = ts.tv_sec ;

    /* Insert the host health - TO BE OBSOLTETED */
    msg.parm[MTC_PARM_HEALTH_IDX] = get_node_health( get_hostname() ) ;

    /* Insert the mtce flags */
    msg.parm[MTC_PARM_FLAGS_IDX] = 0 ;

    //Check if LUKS FS manager service is active
    int exitstatus = system("cryptsetup status luks_encrypted_vault");
    if ( 0 != exitstatus )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__LUKS_VOL_FAILED ;
    if ( daemon_is_file_present ( CONFIG_COMPLETE_FILE ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__I_AM_CONFIGURED ;
    if ( daemon_is_file_present ( CONFIG_FAIL_FILE ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__I_AM_NOT_HEALTHY ;
    if ( daemon_is_file_present ( CONFIG_PASS_FILE ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__I_AM_HEALTHY ;
    if ( daemon_is_file_present ( NODE_LOCKED_FILE ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__I_AM_LOCKED ;
    if ( daemon_is_file_present ( GOENABLED_MAIN_PASS ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__MAIN_GOENABLED ;
    if ( daemon_is_file_present ( PATCHING_IN_PROG_FILE ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__PATCHING ;
    if ( daemon_is_file_present ( NODE_IS_PATCHED_FILE ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__PATCHED ;

    /* manage the worker subfunction flag */
    if ( is_subfunction_worker () == true )
    {
        if ( daemon_is_file_present ( CONFIG_COMPLETE_WORKER ) )
        {
            msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__SUBF_CONFIGURED ;

            /* Only set the go enabled subfunction flag if the pass file only exists */
            if (( daemon_is_file_present ( GOENABLED_SUBF_PASS ) == true ) &&
                ( daemon_is_file_present ( GOENABLED_SUBF_FAIL ) == false ))
            {
                msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__SUBF_GOENABLED ;
            }
        }
    }

    if ( daemon_is_file_present ( SMGMT_DEGRADED_FILE ) )
        msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__SM_DEGRADED ;

    if ( daemon_is_file_present ( SMGMT_UNHEALTHY_FILE ) )
    {
        /* debounce 6 mtcAlive messages = ~25-30 second debounce */
        #define MAX_SM_UNHEALTHY_DEBOUNCE (6)
        if ( ++_sm_unhealthy_debounce_counter[interface] > MAX_SM_UNHEALTHY_DEBOUNCE )
        {
            wlog("SM Unhealthy flag set (%s)",
                  get_interface_name_str(interface));
            msg.parm[MTC_PARM_FLAGS_IDX] |= MTC_FLAG__SM_UNHEALTHY ;
        }
        else
        {
            wlog("SM Unhealthy debounce %d of %d (%s)",
                  _sm_unhealthy_debounce_counter[interface],
                  MAX_SM_UNHEALTHY_DEBOUNCE,
                  get_interface_name_str(interface));
        }
    }
    else
    {
        _sm_unhealthy_debounce_counter[interface] = 0 ;
    }

    /* add the interface and sequence number to the mtcAlive message */
    identity.append ( ",\"interface\":\"");
    identity.append (get_interface_name_str(interface));
    identity.append("\",\"sequence\":");

    if ( interface == PXEBOOT_INTERFACE )
    {
        ctrl_ptr->mtcAlive_pxeboot_sequence++ ;
        identity.append(itos(ctrl_ptr->mtcAlive_pxeboot_sequence));
        msg.parm[MTC_PARM_SEQ_IDX] = ctrl_ptr->mtcAlive_pxeboot_sequence ;
    }
    else if ( interface == MGMNT_INTERFACE )
    {
        ctrl_ptr->mtcAlive_mgmnt_sequence++ ;
        identity.append(itos(ctrl_ptr->mtcAlive_mgmnt_sequence));
        msg.parm[MTC_PARM_SEQ_IDX] = ctrl_ptr->mtcAlive_mgmnt_sequence ;
    }
    else if ( interface == CLSTR_INTERFACE )
    {
        ctrl_ptr->mtcAlive_clstr_sequence++ ;
        identity.append(itos(ctrl_ptr->mtcAlive_clstr_sequence));
        msg.parm[MTC_PARM_SEQ_IDX] = ctrl_ptr->mtcAlive_clstr_sequence ;
    }
    else
        identity.append(itos(0));

    identity.append("}");

    memcpy ( &msg.buf[0], identity.c_str(), identity.size() );

    /* Send only the data we care about */
    return (((sizeof(mtc_message_type))-(BUF_SIZE)+(identity.size())+1));
}



/* Send GOENABLED messages to the controller */

int send_mtc_msg_failed = 0 ;
int send_mtc_msg ( mtc_socket_type * sock_ptr, int cmd , string identity )
{
    int rc = FAIL ;

    if (( cmd == MTC_MSG_MAIN_GOENABLED ) ||
        ( cmd == MTC_MSG_SUBF_GOENABLED ) ||
        ( cmd == MTC_MSG_MAIN_GOENABLED_FAILED ) ||
        ( cmd == MTC_MSG_SUBF_GOENABLED_FAILED ))
    {
        int interface = MGMNT_INTERFACE ;
        mtc_message_type msg ;
        int bytes = create_mtcAlive_msg ( get_ctrl_ptr(), msg, cmd, identity, interface );
        if (( sock_ptr->mtc_client_mgmt_tx_socket ) &&
            ( sock_ptr->mtc_client_mgmt_tx_socket->sock_ok() == true ))
        {
            /* Send back to requester - TODO: consider sending back to both as multicast */
            if ((rc = sock_ptr->mtc_client_mgmt_tx_socket->write((char*)&msg.hdr[0], bytes)) != bytes )
            {
                if ( rc == -1 )
                {
                    wlog_throttled (send_mtc_msg_failed, 100 ,
                              "failed to send <%s:%d> (%d:%m)",
                              sock_ptr->mtc_client_mgmt_tx_socket->get_dst_str(),
                              sock_ptr->mtc_client_mgmt_tx_socket->get_dst_addr()->getPort(), errno );
                }
                else
                {
                    wlog_throttled ( send_mtc_msg_failed, 100 ,
                              "sent only %d of %d bytes to <%s:%d>\n",
                              rc, bytes,
                              sock_ptr->mtc_client_mgmt_tx_socket->get_dst_str(),
                              sock_ptr->mtc_client_mgmt_tx_socket->get_dst_addr()->getPort());
                }
            }
            else
            {
                send_mtc_msg_failed = 0 ;
                print_mtc_message ( get_hostname(), MTC_CMD_TX, msg, get_interface_name_str(interface), false );
                rc = PASS ;
            }
        }
        else
        {
           elog ("cannot send to null or failed socket (%s)",
                  get_interface_name_str (MGMNT_INTERFACE) );
        }
    }
    else
    {
        elog ( "Unsupported Mtc command (%d)\n", cmd );
    }

    return (PASS) ;
}

int send_mtcAlive_msg_failed = 0 ;
int send_mtcAlive_msg ( mtc_socket_type * sock_ptr, string identity, int interface )
{
    int flags = 0 ; // no tx flags

    /* get a pointer to the process control structure */
    ctrl_type * ctrl_ptr = get_ctrl_ptr() ;

    if (( interface == PXEBOOT_INTERFACE ) &&
        ( ctrl_ptr->pxeboot_iface_provisioned == false ))
        return (PASS) ;

    if (( interface == CLSTR_INTERFACE ) &&
        ( ctrl_ptr->clstr_iface_provisioned != true ))
    {
        dlog2 ("cannot send to unprovisioned %s interface",
               get_interface_name_str(interface) );
        return (FAIL);
    }

    mtc_message_type msg ;
    int bytes = create_mtcAlive_msg ( ctrl_ptr, msg, MTC_MSG_MTCALIVE, identity, interface );

    if ( interface == PXEBOOT_INTERFACE )
    {
        /* Send to controller-0 pxeboot address */
        if ( sock_ptr->pxeboot_tx_socket <= 0 )
        {
            elog("pxeboot_tx_socket not ok (%d)", sock_ptr->pxeboot_tx_socket);
            return (FAIL_SOCKET_SENDTO);
        }

        // TODO: Consider adding controllers info to ctrl struct
        string controllers[CONTROLLERS] = {CONTROLLER_0, CONTROLLER_1};
        alog1 ("sending mtcAlive to both controllers");
        for (int c = 0 ; c < CONTROLLERS ; c++)
        {
            string pxeboot_addr_cx ;
            struct sockaddr_in hostAddr;
            memset(&hostAddr, 0, sizeof(hostAddr));

            if (controllers[c] == CONTROLLER_1)
            {
                if ( ctrl_ptr->system_type != SYSTEM_TYPE__AIO__SIMPLEX )
                    pxeboot_addr_cx = ctrl_ptr->pxeboot_addr_c1;
                else
                    continue; // skip controller-1 for SX systems
            }
            else
                pxeboot_addr_cx = ctrl_ptr->pxeboot_addr_c0;

            if ( pxeboot_addr_cx.empty() )
            {
                if ( ctrl_ptr->pxeboot_address_learned[c] == true )
                {
                    ctrl_ptr->pxeboot_address_learned[c] = false ;
                    wlog ( "%s pxeboot address not learned ; unable to send pxeboot mtcAlive",
                               controllers[c].c_str() );
                }
                continue ;
            }

            if ( ctrl_ptr->pxeboot_address_learned[c] == false )
            {
                // Only log this if the not learned log was produced.
                // Which is most likely case on process startup.
                ilog ("sending pxeboot network mtcAlive msg on port %d to %s at %s",
                       sock_ptr->mtc_tx_pxeboot_port,
                       controllers[c].c_str(),
                       pxeboot_addr_cx.c_str());
                ctrl_ptr->pxeboot_address_learned[c] = true ;
            }

            print_mtc_message ( controllers[c], MTC_CMD_TX, msg, get_interface_name_str(PXEBOOT_INTERFACE), false );
            hostAddr.sin_addr.s_addr = inet_addr(pxeboot_addr_cx.data());
            hostAddr.sin_family = AF_INET;
            hostAddr.sin_port = htons(sock_ptr->mtc_tx_pxeboot_port); // 2102
            alog1 ("sending pxeboot network mtcAlive msg to %s", controllers[c].c_str() );
            ssize_t bytes_sent = sendto(sock_ptr->pxeboot_tx_socket, &msg.hdr[0], bytes, flags,
                                (const struct sockaddr*)&hostAddr, sizeof(hostAddr));
            if (bytes_sent <= 0)
            {
                elog ("failed to send mtcAlive to %s using %s:%d (pxeboot) (rc:%ld) (%d:%m)",
                       controllers[c].c_str(), pxeboot_addr_cx.c_str(), hostAddr.sin_port, bytes_sent, errno);
            }
        } // for loop
    }

    else if ( interface == MGMNT_INTERFACE )
    {
        /* Send to controller floating address */
        if (( sock_ptr->mtc_client_mgmt_tx_socket ) &&
            ( sock_ptr->mtc_client_mgmt_tx_socket->sock_ok() == true ))
        {
            alog1 ("sending mgmt network mtcAlive msg to %s", CONTROLLER);
            print_mtc_message ( CONTROLLER, MTC_CMD_TX, msg, get_interface_name_str(MGMNT_INTERFACE), false );
            sock_ptr->mtc_client_mgmt_tx_socket->write((char*)&msg.hdr[0], bytes) ;
        }
        else
        {
            elog("mtc_client_mgmt_tx_socket not ok");
        }
    }
    else if ( interface == CLSTR_INTERFACE )
    {
        /* Send to controller-0 cluster address */
        if (( sock_ptr->mtc_client_clstr_tx_socket_c0 ) &&
            ( sock_ptr->mtc_client_clstr_tx_socket_c0->sock_ok() == true ))
        {
            alog1 ("sending clstr network mtcAlive msg to %s", CONTROLLER_0);
            print_mtc_message ( CONTROLLER_0, MTC_CMD_TX, msg, get_interface_name_str(CLSTR_INTERFACE), false );
            sock_ptr->mtc_client_clstr_tx_socket_c0->write((char*)&msg.hdr[0], bytes ) ;
        }
        else
        {
            elog("mtc_client_clstr_tx_socket_c0 not ok");
        }

        /* Send to controller-1 cluster address */
        if ( get_ctrl_ptr()->system_type != SYSTEM_TYPE__AIO__SIMPLEX )
        {
            if (( sock_ptr->mtc_client_clstr_tx_socket_c1 ) &&
                ( sock_ptr->mtc_client_clstr_tx_socket_c1->sock_ok() == true ))
            {
                alog1 ("sending clstr mtcAlive msg to %s", CONTROLLER_1);
                print_mtc_message ( CONTROLLER_1, MTC_CMD_TX, msg, get_interface_name_str(CLSTR_INTERFACE), false );
                sock_ptr->mtc_client_clstr_tx_socket_c1->write((char*)&msg.hdr[0], bytes ) ;
            }
            else
            {
                elog("mtc_client_clstr_tx_socket_c1 not ok");
            }
        }
    }
    else
    {
        wlog_throttled ( send_mtcAlive_msg_failed, 100,
                         "Unsupported interface (%d)\n", interface );
        return (FAIL_BAD_PARM);
    }

    return (PASS) ;
}

int send_mtcClient_cmd ( mtc_socket_type * sock_ptr, int cmd, string hostname, string address, int port)
{
    mtc_message_type msg ;
    int bytes = 0 ;
    MEMSET_ZERO (msg);
    snprintf ( &msg.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header());
    msg.cmd = cmd ;

    switch ( cmd )
    {
        case MTC_CMD_SYNC:
        {
            ilog ("Sending '%s' command to %s:%s:%d",
                   get_mtcNodeCommand_str(cmd),
                   hostname.c_str(),
                   address.c_str(), port);

            msg.num = 0   ;

            /* buffer  not used in this message */
            bytes = ((sizeof(mtc_message_type))-(BUF_SIZE));

            break ;
        }
        default:
        {
            slog("Unsupported command ; %s:%d", get_mtcNodeCommand_str(cmd), cmd );
            return (FAIL_BAD_CASE);
        }
    }
    int rc = FAIL ;

    /* Send to controller floating address */
    if (( sock_ptr->mtc_client_mgmt_tx_socket ) &&
        ( sock_ptr->mtc_client_mgmt_tx_socket->sock_ok() == true ))
    {
        print_mtc_message ( hostname, MTC_CMD_TX, msg, get_interface_name_str(MGMNT_INTERFACE), false );
        rc = sock_ptr->mtc_client_mgmt_tx_socket->write((char*)&msg.hdr[0], bytes, address.data(), port ) ;
        if ( 0 >= rc )
        {
            elog("failed to send command to mtcClient (%d) (%d:%s)", rc, errno, strerror(errno));
            rc = FAIL_SOCKET_SENDTO ;
        }
        else
            rc = PASS ;
    }
    else
    {
        elog("mtc_client_mgmt_tx_socket not ok");
        rc = FAIL_BAD_STATE ;
    }
    return (rc) ;
}

int mtcCompMsg_testhead ( void )
{
    return (PASS);
}
