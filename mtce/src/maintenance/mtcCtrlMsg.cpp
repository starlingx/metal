/*
 * Copyright (c) 2013-2018, 2023-2024 Wind River Systems, Inc.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>     /* for ... unix domain sockets */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>   /* for hostent */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h> /* for close and usleep */
#include <list>
#include <vector>
#include <algorithm>

using namespace std;

#define __AREA__ "msg"

#include "nodeClass.h"
#include "mtcNodeMsg.h"
#include "jsonUtil.h"      /* for ... jsonApi_get_key_value  */
#include "daemon_option.h"
#include "daemon_common.h"
#include "mtcAlarm.h"      /* for ... mtcAlarm...       */
#include "nodeUtil.h"      /* for ... get_event_str ...       */

int service_events ( nodeLinkClass * obj_ptr, mtc_socket_type * sock_ptr );

/* Send specified command to the guestAgent daemon */
int send_guest_command ( string hostname, int command )
{
    int rc = PASS ;
    nodeLinkClass   *  obj_ptr = get_mtcInv_ptr();

    mlog ("%s NodeType %s (0x%x) Check: %c %c \n",
              hostname.c_str(),
              obj_ptr->functions.c_str(),
              obj_ptr->get_nodetype(hostname),
              obj_ptr->is_worker (hostname)            ? 'Y' : 'n',
              obj_ptr->is_worker_subfunction(hostname) ? 'Y' : 'n');

    if ( obj_ptr->is_worker            (hostname) ||
         obj_ptr->is_worker_subfunction(hostname))
    {
        mtc_message_type msg ; /* the message to send */

        int bytes_to_send;
        int bytes        ;

        mtc_socket_type * sock_ptr = get_sockPtr ();

        ilog ("%s sending '%s' to guest service\n", hostname.c_str(), get_event_str(command).c_str());
        string hostinfo = "{\"hostname\":\"" ;
        hostinfo.append (hostname);
        hostinfo.append ("\"");

        if ( (command == MTC_CMD_ADD_HOST)  || (command == MTC_CMD_MOD_HOST) )
        {
            hostinfo.append (",\"uuid\":\"");
            hostinfo.append ( obj_ptr->get_uuid (hostname));
            hostinfo.append ( "\"");
            hostinfo.append (",\"ip\":\"");
            hostinfo.append ( obj_ptr->get_hostaddr (hostname));
            hostinfo.append ( "\"");
            hostinfo.append (",\"personality\":\"worker\"");
        }
        else if ( command == MTC_CMD_DEL_HOST )
        {
            hostinfo.append (",\"uuid\":\"");
            hostinfo.append ( obj_ptr->get_uuid (hostname));
            hostinfo.append ( "\"");
        }

        hostinfo.append ( "}");

        /* Add the header, command and the host info to the message */
        memset (&msg, 0, sizeof(mtc_message_type));
        memcpy(&msg.hdr[0], get_cmd_req_msg_header(), MSG_HEADER_SIZE );
        memcpy(&msg.buf[0], hostinfo.data(), hostinfo.length());
        msg.cmd = command ;

        /* Send to guestAgent daemon port */
        bytes_to_send = ((sizeof(mtc_message_type))-(BUF_SIZE)+(hostinfo.length()));
        bytes = sock_ptr->mtc_to_hbs_sock->write((char*) &msg, bytes_to_send, NULL, daemon_get_cfg_ptr()->mtc_to_guest_cmd_port);
        if ( bytes <= 0 )
        {
            wlog ("Cannot send to heartbeat service\n");
            rc = FAIL_TO_TRANSMIT ;
        }
    }
    return (rc);
}

/* Log throttle counters for this interface */
int rx_error_count = 0 ;

int mtc_service_inbox ( nodeLinkClass   *  obj_ptr,
                        mtc_socket_type * sock_ptr,
                        int               iface)
{
    mtc_message_type msg ;
    int bytes = 0    ;
    int rc    = PASS ;
    if ( iface == CLSTR_INTERFACE )
    {
        if ( ( obj_ptr ) &&
             ( obj_ptr->clstr_network_provisioned == true ) &&
             ( sock_ptr->mtc_agent_clstr_rx_socket ))
        {
            bytes = sock_ptr->mtc_agent_clstr_rx_socket->read((char*)&msg, sizeof(msg));
        }
        else
        {
            return ( FAIL_NO_CLSTR_PROV );
        }
    }
    else
    {
        bytes = sock_ptr->mtc_agent_rx_socket->read((char*)&msg, sizeof(msg));
    }
    msg.buf[BUF_SIZE-1] = '\0';

    if ( bytes <= 0 )
    {
        return (RETRY);
    }
    else if ( bytes < 7 )
    {
        wlog_throttled ( rx_error_count, 100, "Message receive error, underrun (only rxed %d bytes)\n", bytes );
        return (FAIL);
    }
    else
    {
        rx_error_count = 0 ;
    }

    zero_unused_msg_buf (msg, bytes);

    /* get the sender's hostname */
    string hostaddr = "" ;
    string hostname = "" ;
    if ( iface == CLSTR_INTERFACE )
    {
        hostaddr = sock_ptr->mtc_agent_clstr_rx_socket->get_src_str();
        hostname = obj_ptr->get_hostname ( hostaddr ) ;
    }
    else
    {
        hostaddr = sock_ptr->mtc_agent_rx_socket->get_src_str();
        hostname = obj_ptr->get_hostname ( hostaddr ) ;
    }

    /* lookup failed if hostname remains empty. */
    if ( hostname.empty() )
    {
        /* try and learn the cluster ip from a mtcAlive message. */
        if (( msg.cmd == MTC_MSG_MTCALIVE ) &&
            (( rc = jsonUtil_get_key_val ( &msg.buf[0], "hostname", hostname )) == PASS ))
        {
            ilog ("%s learned from mtcAlive", hostname.c_str());
        }
        else
        {
            wlog ("unknown hostname message ... dropping" ); /* make dlog */
            print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(iface), true );
            return (FAIL_GET_HOSTNAME);
        }
    }

    print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(iface), false );

    /* handle messages that are not mtc_message_type
     * but rather are simply a json string */
    if ( msg.hdr[0] == '{' )
    {
        string service ;

        mlog1 ("%s\n", &msg.hdr[0] );

        rc = jsonUtil_get_key_val(&msg.hdr[0],"service", service );
        if ( rc == PASS )
        {
            if ( service == "collectd_notifier" )
            {
                int rc1,rc2,rc3 ;
                string hostname,resource,state ;

                rc1 = jsonUtil_get_key_val(&msg.hdr[0],"hostname", hostname );
                rc2 = jsonUtil_get_key_val(&msg.hdr[0],"resource", resource );
                rc3 = jsonUtil_get_key_val(&msg.hdr[0],"degrade", state );
                if ( rc1|rc2|rc3 )
                {
                    elog ("failed to parse '%s' message\n", service.c_str());
                    wlog ("... %s\n", &msg.hdr[0] );
                    rc = FAIL_JSON_PARSE ;
                }
                else
                {
                    obj_ptr->collectd_notify_handler ( hostname,
                                                       resource,
                                                       state );
                    return (PASS) ;
                }
            }
            /* future service requests */
            else
            {
                wlog ("Unexpected service request: '%s'\n", service.c_str());
                rc = FAIL_BAD_PARM ;
            }
        }
        else
        {
            wlog("Unexpected json message: %s\n", &msg.hdr[0] );
            rc = FAIL_BAD_CASE ;
        }
    }

    /* Check for response messages */
    else if ( strstr ( &msg.hdr[0], get_cmd_rsp_msg_header() ) )
    {
        obj_ptr->set_cmd_resp ( hostname , msg, iface ) ;
        if ( msg.num > 0 )
        {
            /* log if not locked message, not start host services result
             * message and there is an error */
            if (( msg.cmd != MTC_MSG_LOCKED ) &&
                ( msg.cmd != MTC_CMD_HOST_SVCS_RESULT ) &&
                ( msg.parm[0] ))
            {
                ilog ("%s '%s' ACK (rc:%d) (%s)",
                          hostname.c_str(),
                          get_mtcNodeCommand_str(msg.cmd),
                          msg.parm[0],
                          get_iface_name_str(iface));
            }
            else
            {
                mlog ("%s '%s' ACK (rc:%d) (%s)",
                          hostname.c_str(),
                          get_mtcNodeCommand_str(msg.cmd),
                          msg.parm[0],
                          get_iface_name_str(iface));
            }
        }
    }

    /*
     * Check for worker messages
     */
    else if ( strstr ( &msg.hdr[0], get_worker_msg_header() ) )
    {
        if ( msg.cmd == MTC_MSG_MTCALIVE )
        {
            string functions = "" ;
            rc =  jsonUtil_get_key_val ( &msg.buf[0], "personality", functions );
            if ( rc )
            {
                wlog ("%s failed to get personality from mtcAlive message\n", hostname.c_str());
                return (FAIL_KEY_VALUE_PARSE);
            }
            rc = obj_ptr->update_host_functions ( hostname, functions );
            dlog3 ("%s functions: %s\n", hostname.c_str(), functions.c_str());
            if ( rc )
            {
                wlog ("%s failed to load functions from mtcAlive message\n", hostname.c_str());
                return (FAIL_NODETYPE);
            }

            if ( obj_ptr->clstr_network_provisioned == true )
            {
                string cluster_host_ip = "";
                /* Get the clstr ip address if it is provisioned */
                rc =  jsonUtil_get_key_val ( &msg.buf[0], "cluster_host_ip", cluster_host_ip );
                if ( rc == PASS )
                {
                    obj_ptr->set_clstr_hostaddr ( hostname, cluster_host_ip );
                }
                else
                {
                    wlog ("%s missing 'cluster_host_ip' value (rc:%d)\n", hostname.c_str(), rc);
                }
            }

            obj_ptr->set_uptime     ( hostname , msg.parm[MTC_PARM_UPTIME_IDX], false );
            obj_ptr->set_health     ( hostname , msg.parm[MTC_PARM_HEALTH_IDX] );
            obj_ptr->set_mtce_flags ( hostname , msg.parm[MTC_PARM_FLAGS_IDX], iface );
            obj_ptr->set_mtcAlive   ( hostname, iface );

            mlog1("%s Uptime:%d Health:%d Flags:0x%x mtcAlive:%s (%s)\n",
                      hostname.c_str(),
                      msg.parm[MTC_PARM_UPTIME_IDX],
                      msg.parm[MTC_PARM_HEALTH_IDX],
                      msg.parm[MTC_PARM_FLAGS_IDX],
                      obj_ptr->get_mtcAlive_gate ( hostname ) ? "gated" : "open",
                      get_iface_name_str(iface));

        }
        else if ( msg.cmd == MTC_MSG_MAIN_GOENABLED )
        {
            if ( !obj_ptr->my_hostname.compare(hostname) )
            {
                ilog ("%s received GOENABLED from self\n", hostname.c_str());
            }
            rc = send_mtc_cmd ( hostname , msg.cmd, MGMNT_INTERFACE );
            if ( rc != PASS )
            {
                elog ("%s GOENABLED send reply failed (rc:%d)\n",
                          hostname.c_str(), rc);

                wlog ("%s ... need successful GOENABLED reply, dropping ...\n",
                          hostname.c_str() );
            }
            else
            {
                mlog ("%s got GOENABLED (out-of-service tests passed) message\n", hostname.c_str());
                obj_ptr->set_goEnabled ( hostname );
            }
        }
        else if ( msg.cmd == MTC_MSG_MAIN_GOENABLED_FAILED )
        {
            if ( obj_ptr->get_adminState ( hostname ) == MTC_ADMIN_STATE__UNLOCKED )
            {
                wlog ("%s failed out-of-service test: %s\n", hostname.c_str(), &msg.buf[0] );
                obj_ptr->set_goEnabled_failed ( hostname );
            }
            /* We don't send a reply on a fail */
        }
        else if ( msg.cmd == MTC_MSG_SUBF_GOENABLED )
        {
            mlog ("%s-worker GOENABLED message\n", hostname.c_str());
            if ( !obj_ptr->my_hostname.compare(hostname) )
            {
                ilog ("%s-worker received GOENABLED from self\n", hostname.c_str());
            }
            rc = send_mtc_cmd ( hostname , msg.cmd, MGMNT_INTERFACE );
            if ( rc != PASS )
            {
                elog ("%s-worker GOENABLED send reply failed (rc:%d)\n",
                          hostname.c_str(), rc);

                wlog ("%s-worker ... need successful GOENABLED reply, dropping ...\n",
                          hostname.c_str() );
            }
            else
            {
                mlog ("%s-worker got GOENABLED (out-of-service tests passed) message\n", hostname.c_str());
                obj_ptr->set_goEnabled_subf ( hostname );
            }
        }
        else if ( msg.cmd == MTC_MSG_SUBF_GOENABLED_FAILED )
        {
            if ( obj_ptr->get_adminState ( hostname ) == MTC_ADMIN_STATE__UNLOCKED )
            {
                wlog ("%s-worker failed GOENABLE test: %s\n", hostname.c_str(), &msg.buf[0] );
                obj_ptr->set_goEnabled_failed_subf ( hostname );
            }
            /* We don't send a reply on a fail */
        }
        else
        {
            wlog ("Unexpected worker message (0x%x) from '%s'\n", msg.cmd, hostname.c_str());
        }
    }

    /*
     * Check for Event Messages
     */
    else if ( strstr ( &msg.hdr[0], get_mtce_event_header() ) )
    {
        string service = "" ;
        string sensor  = "" ;
        string process = "" ;
        hostname = "unknown" ;

        int rc1 = FAIL ;
        if ( ( rc = jsonUtil_get_key_val(&msg.buf[0], MTC_JSON_INV_NAME, hostname )) == PASS )
        {
            if ( ( rc1 = jsonUtil_get_key_val(&msg.buf[0], MTC_JSON_SERVICE, service )) == PASS )
            {
                if (( msg.cmd == MTC_DEGRADE_RAISE )        ||
                    ( msg.cmd == MTC_EVENT_HWMON_CLEAR )    ||
                    ( msg.cmd == MTC_EVENT_HWMON_MINOR )    ||
                    ( msg.cmd == MTC_EVENT_HWMON_MAJOR )    ||
                    ( msg.cmd == MTC_EVENT_HWMON_CRIT )     ||
                    ( msg.cmd == MTC_EVENT_HWMON_RESET )    ||
                    ( msg.cmd == MTC_EVENT_HWMON_POWERDOWN )||
                    ( msg.cmd == MTC_EVENT_HWMON_POWERCYCLE ))
                {
                    jsonUtil_get_key_val(&msg.buf[0], MTC_JSON_SENSOR, sensor );
                }
                else if (( msg.cmd == MTC_EVENT_PMON_CLEAR ) ||
                         ( msg.cmd == MTC_EVENT_PMON_CRIT )  ||
                         ( msg.cmd == MTC_EVENT_PMON_MAJOR ) ||
                         ( msg.cmd == MTC_EVENT_PMON_MINOR ) ||
                         ( msg.cmd == MTC_EVENT_PMON_LOG ))
                {
                    jsonUtil_get_key_val(&msg.buf[0], MTC_JSON_PROCESS, process );
                }
            }
        }
        if (( rc | rc1 ) != PASS )
        {
            elog ("received invalid event [rc:%d:%d]", rc, rc1);
            print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(iface), true );
            return ( FAIL_INVALID_OPERATION );
        }
        switch ( msg.cmd )
        {
            case MTC_EVENT_MONITOR_READY:
            {
                if ( service == MTC_SERVICE_PMOND_NAME )
                {
                    obj_ptr->declare_service_ready ( hostname, MTC_SERVICE_PMOND );
                    return (PASS);
                }
                else if ( service == MTC_SERVICE_HBSCLIENT_NAME )
                {
                    obj_ptr->declare_service_ready ( hostname, MTC_SERVICE_HEARTBEAT );
                    return (PASS);
                }
                else if ( service == MTC_SERVICE_MTCCLIENT_NAME )
                {
                    ilog ("%s %s ready", hostname.c_str(), MTC_SERVICE_MTCCLIENT_NAME);

                    /* if this ready event is from the mtcClient of a
                     * controller that has valid bmc access info then
                     * build the 'peer controller kill' mtcInfo and
                     * send it to that mtcClient */
                    if ( obj_ptr->get_nodetype ( hostname ) & CONTROLLER_TYPE )
                    {
                        string bm_pw = obj_ptr->get_bm_pw ( hostname ) ;
                        if ( !bm_pw.empty() && ( bm_pw != NONE ))
                        {
                            string bm_un = obj_ptr->get_bm_un ( hostname ) ;
                            string bm_ip = obj_ptr->get_bm_ip ( hostname ) ;
                            if (( hostUtil_is_valid_username  ( bm_un )) &&
                                ( hostUtil_is_valid_ip_addr   ( bm_ip )))
                            {
                                send_mtc_cmd ( hostname,
                                               MTC_MSG_INFO,
                                               MGMNT_INTERFACE,
                                               obj_ptr->build_mtcInfo_dict (
                                MTC_INFO_CODE__PEER_CONTROLLER_KILL_INFO));
                            }
                        }
                    }
                    return (PASS);
                }
                if (  service == MTC_SERVICE_HWMOND_NAME )
                {
                    std::list<string>::iterator temp ;

                    /* push inventory to hardware hwmond.
                     * handles the daemon restart case.
                     */
                    for ( temp = obj_ptr->hostname_inventory.begin () ;
                          temp != obj_ptr->hostname_inventory.end () ;
                          temp++ )
                    {
                        hostname = temp->data();
                        obj_ptr->declare_service_ready ( hostname, MTC_SERVICE_HWMOND );
                    }
                }
                else
                {
                    wlog ("%s ready event not supported for '%s' service\n",
                              hostname.c_str(), service.c_str());
                    return (FAIL_BAD_PARM);
                }
                break ;
            }

            /*****************************************************************
             *                Process Monitor Events
             *                ----------------------
             *
             *  service is the process name for this event.
             *  parm[0] is the nodetype the process serves.
             *
             *****************************************************************/
            case MTC_EVENT_PMON_CLEAR:
            {
                mlog ("%s %s: '%s' recovered (clear)\n",
                         hostname.c_str(),
                         MTC_SERVICE_PMOND_NAME,
                         service.c_str());

                obj_ptr->degrade_pmond_clear ( hostname );
                break ;
            }
            case MTC_EVENT_PMON_CRIT:
            {
                mlog ("%s %s: '%s' failed (critical)\n",
                          hostname.c_str(),
                          MTC_SERVICE_PMOND_NAME,
                          process.c_str());

                obj_ptr->critical_process_failed ( hostname,
                                                   process,
                                                   msg.parm[0] );
                break ;
            }
            case MTC_EVENT_PMON_MAJOR:
            {
                mlog ("%s %s: '%s' failed (major)\n",
                          hostname.c_str(),
                          MTC_SERVICE_PMOND_NAME,
                          process.c_str());
                obj_ptr->degrade_process_raise ( hostname, process );
                break ;
            }
            case MTC_EVENT_PMON_MINOR:
            {
                mlog ("%s %s: '%s' failed (minor)\n",
                          hostname.c_str(),
                          MTC_SERVICE_PMOND_NAME,
                          process.c_str());
                obj_ptr->alarm_process_failure ( hostname, process );
                break ;
            }
            case MTC_EVENT_PMON_LOG:
            {
                mlog ("%s %s: '%s' failed (log)\n",
                          hostname.c_str(),
                          MTC_SERVICE_PMOND_NAME,
                          process.c_str());
                obj_ptr->log_process_failure ( hostname, process );
                break ;
            }

            case MTC_EVENT_HWMON_CLEAR:
            case MTC_DEGRADE_CLEAR:
            {
                mlog ("%s %s degrade clear request",
                          hostname.c_str(),
                          service.c_str());
                obj_ptr->node_degrade_control ( hostname,
                                                MTC_DEGRADE_CLEAR,
                                                service, sensor );
                break ;
            }
            case MTC_EVENT_HWMON_MINOR:
            case MTC_EVENT_HWMON_MAJOR:
            case MTC_EVENT_HWMON_CRIT:
            case MTC_DEGRADE_RAISE:
            {
                mlog ("%s %s degrade request %s",
                          hostname.c_str(),
                          service.c_str(),
                          sensor.empty() ? "" : sensor.c_str());
                obj_ptr->node_degrade_control ( hostname,
                                                MTC_DEGRADE_RAISE,
                                                service, sensor );
                break ;
            }
            case MTC_EVENT_HWMON_RESET:
            case MTC_EVENT_HWMON_POWERDOWN:
            case MTC_EVENT_HWMON_POWERCYCLE:
            {
                mlog ("%s '%s' action due to failing '%s' sensor",
                          hostname.c_str(),
                          get_event_str(msg.cmd).c_str(),
                          sensor.c_str());

                obj_ptr->invoke_hwmon_action ( hostname, msg.cmd, sensor );
                break ;
            }
            default:
            {
                wlog ("%s Unknown Event (%x)\n", hostname.c_str(), msg.cmd );
                rc = FAIL ;
                break ;
            }
        }
    }
    else
    {
        wlog ( "Received unsupported or badly formed message\n" );
    }

    return (rc);
}

int send_mtc_cmd ( string & hostname, int cmd , int interface, string json_dict )
{
    int rc = FAIL ;
    bool force = false ;
    mtc_message_type mtc_cmd ;
    string data = "" ;
    mtc_socket_type * sock_ptr = get_sockPtr ();
    memset (&mtc_cmd,0,sizeof(mtc_message_type));

    /* Add the command version to he message */
    mtc_cmd.ver = MTC_CMD_VERSION ;
    mtc_cmd.rev = MTC_CMD_REVISION;

    switch ( cmd )
    {
        case MTC_MSG_INFO:
        {
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s" , get_cmd_req_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            data = "{\"mtcInfo\":" + json_dict + "}";
            ilog("%s mtc info update", hostname.c_str());
            rc = PASS ;
            break ;
        }
        case MTC_REQ_MTCALIVE:
        {
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s" , get_cmd_req_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            rc = PASS ;
            break ;
        }
        case MTC_REQ_MAIN_GOENABLED:
        case MTC_REQ_SUBF_GOENABLED:
        {
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            rc = PASS ;
            break ;
        }
        case MTC_CMD_STOP_CONTROL_SVCS:
        case MTC_CMD_STOP_WORKER_SVCS:
        case MTC_CMD_STOP_STORAGE_SVCS:
        case MTC_CMD_START_CONTROL_SVCS:
        case MTC_CMD_START_WORKER_SVCS:
        case MTC_CMD_START_STORAGE_SVCS:
        {
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header() );
            mtc_cmd.cmd     = cmd ;
            rc = PASS ;
            break ;
        }
        case MTC_CMD_RESET:
        case MTC_CMD_REBOOT:
        case MTC_CMD_WIPEDISK:
        case MTC_CMD_LAZY_REBOOT:
        {
            ilog ("%s sending '%s' request (%s)",
                      hostname.c_str(),
                      get_mtcNodeCommand_str(cmd),
                      get_iface_name_str(interface));
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            if ( cmd == MTC_CMD_LAZY_REBOOT )
            {
                mtc_cmd.num = 1 ;
                mtc_cmd.parm[0] = MTC_SECS_30 ;
            }
            rc = PASS ;
            break ;
        }

        /* Tell the mtcClient on that host that its subFunction has failed */
        case MTC_MSG_SUBF_GOENABLED_FAILED:
        {
            force = true ;
            ilog ("%s sending '%s' request (%s)",
                      hostname.c_str(),
                      get_mtcNodeCommand_str(cmd),
                      get_iface_name_str(interface));
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            rc = PASS ;
            break ;
        }
        case MTC_MSG_MTCALIVE:
        {
            slog ("request to send mtcAlive message from mtcAgent ; invalid\n");
            return (FAIL_OPERATION);
        }
        case MTC_MSG_MAIN_GOENABLED:
        case MTC_MSG_SUBF_GOENABLED:
        {
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s", get_msg_rep_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            rc = PASS ;
            break ;
        }
        case MTC_MSG_LOCKED:
        {
            mlog ("%s sending 'Locked' notification (%s)",
                      hostname.c_str(),
                      get_iface_name_str(interface));
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            rc = PASS ;
            break ;
        }
        case MTC_MSG_UNLOCKED:
        {
            ilog ("%s sending 'UnLocked' notification (%s)",
                      hostname.c_str(),
                      get_iface_name_str(interface));
            snprintf ( &mtc_cmd.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header() );
            mtc_cmd.cmd = cmd ;
            mtc_cmd.num = 0 ;
            rc = PASS ;
            break ;
        }
        default:
        {
            elog ("Unsupported maintenance command (0x%x)\n", cmd );
            rc = FAIL_BAD_CASE ;
        }
    }
    if ( rc == PASS )
    {
        int bytes = 0;

        nodeLinkClass * obj_ptr = get_mtcInv_ptr ();

        /* add the mac address of the target card to the header
         * Note: the minus 1 is to overwrite the null */
        snprintf ( &mtc_cmd.hdr[MSG_HEADER_SIZE-1], MSG_HEADER_SIZE, "%s", obj_ptr->get_hostIfaceMac(hostname, MGMNT_IFACE).data());

        /* If data is empty then at least add where the message came from */
        if ( data.empty() )
        {
            data = "{\"address\":\"";
            data.append(obj_ptr->my_float_ip) ;
            data.append("\",\"interface\":\"");
            data.append(get_iface_name_str(interface));
            data.append("\"}");
        }
        else
        {
            ; /* data is already pre loaded by the command case above */
        }
        /* copy data into message buffer */
        snprintf ( &mtc_cmd.buf[0], data.length()+1, "%s", data.data());
        bytes = (sizeof(mtc_message_type)-(BUF_SIZE-(data.length()+1)));

        print_mtc_message ( hostname, MTC_CMD_TX, mtc_cmd, get_iface_name_str(interface), force ) ;

        if (interface == MGMNT_INTERFACE)
        {
            string hostaddr = obj_ptr->get_hostaddr(hostname);
            if ( hostUtil_is_valid_ip_addr ( hostaddr ) != true )
            {
                wlog("%s has invalid management addr '%s'\n",
                         hostname.c_str(),
                         hostaddr.c_str());
                return (FAIL_HOSTADDR_LOOKUP);
            }

            mlog ("%s sending %s request to %s (%s)",
                      hostname.c_str(),
                      get_mtcNodeCommand_str(cmd),
                      hostaddr.c_str(),
                      get_iface_name_str(interface));

            rc = sock_ptr->mtc_agent_tx_socket->write((char *)&mtc_cmd, bytes, hostaddr.c_str(), sock_ptr->mtc_mgmnt_cmd_port);
        }
        else if ((interface == CLSTR_INTERFACE) &&
                 ( obj_ptr->clstr_network_provisioned == true ) &&
                 ( sock_ptr->mtc_agent_clstr_tx_socket != NULL ))
        {
            string clstr_hostaddr = obj_ptr->get_clstr_hostaddr(hostname);
            if ( hostUtil_is_valid_ip_addr( clstr_hostaddr ) != true )
                return (FAIL_NO_CLSTR_PROV);

            mlog ("%s sending %s request to %s (%s)",
                      hostname.c_str(),
                      get_mtcNodeCommand_str(cmd),
                      clstr_hostaddr.c_str(),
                      get_iface_name_str(interface));

            rc = sock_ptr->mtc_agent_clstr_tx_socket->write((char *)&mtc_cmd, bytes, clstr_hostaddr.c_str(), sock_ptr->mtc_clstr_cmd_port);
        }

        if ( 0 > rc )
        {
            elog("%s Failed to send command (rc:%i)\n", hostname.c_str(), rc);
            rc = FAIL_SOCKET_SENDTO ;
        }
        else
        {
            rc = PASS ;
        }
    }
    return ( rc );
}

int send_hbs_command ( string hostname, int cmd, string controller )
{
    int rc = PASS ;

    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    mtc_message_type event ;
    mtc_socket_type * sock_ptr = get_sockPtr ();

    memset (&event, 0 , sizeof(mtc_message_type));
    snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_hbs_cmd_req_header() );
    snprintf ( &event.hdr[MSG_HEADER_SIZE] , MAX_CHARS_HOSTNAME_32 , "%s", hostname.data());

    event.cmd     = cmd ;
    event.num     = 1   ;
    event.parm[0] = obj_ptr->get_nodetype(hostname);

    /* send to hbsAgent daemon port */
    std::list<string> controllers ;
    controllers.clear();
    if ( controller == CONTROLLER )
    {
        if ( obj_ptr->hostname_provisioned(CONTROLLER_0) )
            controllers.push_back(CONTROLLER_0);
        if ( obj_ptr->hostname_provisioned(CONTROLLER_1) )
            controllers.push_back(CONTROLLER_1);
    }
    else
    {
        controllers.push_back(controller);
    }
    string ip = "" ;
    std::list<string>::iterator unit ;
    for ( unit  = controllers.begin () ;
          unit != controllers.end () ;
          unit++ )
    {
        switch ( cmd )
        {
            case MTC_CMD_ADD_HOST:
            case MTC_CMD_MOD_HOST:
            case MTC_CMD_START_HOST:
                obj_ptr->manage_heartbeat_clear ( hostname, MAX_IFACES );
                break ;
            case MTC_CMD_ACTIVE_CTRL:
            case MTC_CMD_STOP_HOST:
            case MTC_CMD_DEL_HOST:
            case MTC_RESTART_HBS:
            case MTC_BACKOFF_HBS:
            case MTC_RECOVER_HBS:
                break ;
            default:
            {
                slog ("%s Unsupported command operation 0x%x\n",  hostname.c_str(), cmd );
                rc = FAIL_BAD_PARM ;
                continue ;
            }
        }

        /* the command */
        event.cmd     = cmd ;

        /* add the node type */
        event.num     = 1   ;
        event.parm[0] = obj_ptr->get_nodetype(hostname);

        /* support for 64 byte hostnames */
        event.ver = MTC_CMD_FEATURE_VER__KEYVALUE_IN_BUF ;

        /* the json string with hostname starts at the beginning of the buffer */
        event.res = 0 ;

        /* build the message info */
        string hbs_info = "{\"";
        hbs_info.append(MTC_JSON_INV_NAME);
        hbs_info.append("\":\"") ;
        hbs_info.append(hostname);

        hbs_info.append("\",\"");
        hbs_info.append(MTC_JSON_INV_HOSTIP);
        hbs_info.append("\":\"");
        hbs_info.append(obj_ptr->get_hostaddr(hostname));

        if  ( obj_ptr->clstr_network_provisioned )
        {
            hbs_info.append("\",\"");
            hbs_info.append(MTC_JSON_INV_CLSTRIP);
            hbs_info.append("\":\"");
            hbs_info.append(obj_ptr->get_clstr_hostaddr(hostname));
        }
        hbs_info.append("\"}");

        /* copy the json info string into the buffer.
         *
         * add one to the length to accomodate for the null terminator
         * snprintf automatically adds */
        snprintf ( &event.buf[event.res], hbs_info.length()+1,
                   "%s", hbs_info.data());

        /* send to hbsAgent for the specific controller */
        string ip = get_mtcInv_ptr()->get_hostaddr(*unit) ;
        if ( ! ip.empty() )
        {
            rc = sock_ptr->mtc_to_hbs_sock->write((char*) &event,
                                                   sizeof(mtc_message_type),
                                                   ip.data());
            if ( rc <= 0 )
            {
                wlog ("%s send command (0x%x) failed (%s)",
                          unit->c_str(), cmd, ip.c_str() );
                rc = FAIL_TO_TRANSMIT ;
            }
            else
            {
                if ( cmd == MTC_CMD_ACTIVE_CTRL )
                {
                    mlog3 ("%s %s sent to %s %s",
                               hostname.c_str(),
                               get_mtcNodeCommand_str(cmd),
                               unit->c_str(),
                               MTC_SERVICE_HBSAGENT_NAME);
                }
                else
                {
                    ilog ("%s %s sent to %s %s",
                              hostname.c_str(),
                              get_mtcNodeCommand_str(cmd),
                              unit->c_str(),
                              MTC_SERVICE_HBSAGENT_NAME);
                }
                rc  = PASS ;
            }
        }
        else
        {
            rc = FAIL_STRING_EMPTY ;
        }
        print_mtc_message ( hostname, MTC_CMD_RX, event, get_iface_name_str(MGMNT_INTERFACE), rc );
    }
    return rc ;
}


/* Handle client 'events' */
int service_events ( nodeLinkClass * obj_ptr, mtc_socket_type * sock_ptr )
{
    mtc_message_type msg ;
    int bytes = 0    ;

    /* Receive event messages */
    memset (&msg, 0, sizeof(mtc_message_type));
    bytes = sock_ptr->mtc_event_rx_sock->read((char*)&msg, sizeof(mtc_message_type));
    if ( bytes <= 0 )
    {
        return (RETRY) ;
    }

    string hostaddr = sock_ptr->mtc_event_rx_sock->get_src_str();
    string hostname = obj_ptr->get_hostname ( hostaddr ) ;
    if ( hostname.empty() )
    {
        wlog ("%s ignoring service event from unknown host (%s)",
                obj_ptr->my_hostname.c_str(), hostaddr.c_str());
        return (FAIL_UNKNOWN_HOSTNAME);
    }
    if (( hostname != obj_ptr->my_hostname ) &&
        (( msg.cmd == MTC_EVENT_HEARTBEAT_LOSS )       ||
         ( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_SET )  ||
         ( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_CLR )  ||
         ( msg.cmd == MTC_EVENT_HEARTBEAT_DEGRADE_SET )||
         ( msg.cmd == MTC_EVENT_HEARTBEAT_DEGRADE_CLR )))
    {
        mlog3 ("%s '%s' heartbeat event for '%s' from inactive controller ... ignoring",
                   hostname.c_str(),
                   get_mtcNodeCommand_str(msg.cmd),
                   msg.buf[0] ? &msg.buf[0] : "unknown host");
        return (PASS);
    }

    else if (( msg.cmd == MTC_EVENT_HEARTBEAT_LOSS )        ||
             ( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_SET )   ||
             ( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_CLR )   ||
             ( msg.cmd == MTC_EVENT_HEARTBEAT_DEGRADE_SET ) ||
             ( msg.cmd == MTC_EVENT_HEARTBEAT_DEGRADE_CLR ) ||
             ( msg.cmd == MTC_EVENT_PMOND_CLEAR )           ||
             ( msg.cmd == MTC_EVENT_PMOND_RAISE )           ||
             ( msg.cmd == MTC_EVENT_HOST_STALLED ))
    {
        if (( msg.ver >= MTC_CMD_FEATURE_VER__KEYVALUE_IN_BUF ) &&
            ( msg.buf[msg.res] == '{' ))
        {
            jsonUtil_get_key_val(&msg.buf[msg.res], MTC_JSON_INV_NAME, hostname) ;
        }
        else if ( msg.buf[0] != '\0' )
        {
           hostname = &msg.buf[0] ;
        }
        else
        {
            slog ("failed to get hostname from '%s' message",
                   get_mtcNodeCommand_str(msg.cmd));
            print_mtc_message ( "unknown", MTC_CMD_TX, msg, get_iface_name_str(MGMNT_INTERFACE), true );
            return (FAIL_UNKNOWN_HOSTNAME);
        }
    }

    /* print the ready event log */
    if (( msg.cmd != MTC_EVENT_HEARTBEAT_READY ) && ( !hostname.empty () ))
    {
        string log_suffix = "" ;
        if (msg.num)
        {
            log_suffix = "(" ;
            log_suffix.append(get_iface_name_str((int)msg.parm[0]));
            log_suffix.append(")") ;
        }
        if ( msg.cmd != MTC_EVENT_MONITOR_READY )
        {
            ilog ("%s %s %s",
                      hostname.c_str(),
                      get_mtcNodeCommand_str(msg.cmd),
                      log_suffix.c_str() );
        }
    }

    /* handle the events */
    /* ----------------- */
    int rc = PASS ;
    if (( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_SET ) ||
        ( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_CLR ))
    {
        const char * event_hdr_ptr = get_heartbeat_event_header() ;

        /* Confirm header */
        if ( strncmp ( &msg.hdr[0], event_hdr_ptr, MSG_HEADER_SIZE ) )
        {
            elog ("Invalid Heartbeat Event header\n");
            rc = FAIL_BAD_PARM ;
        }
        else
        {
            /* The interface that the heartbeat minor occurred over is
             * specified in parm[0] for this command
             * 0 = MGMNT_IFACE
             * 1 = CLSTR_IFACE
             * else default to 0 (MGMNT_IFACE) to be backwards compatible
             *
             * */
            iface_enum iface = MGMNT_IFACE;
            if ( msg.num > 0 )
            {
                if ( msg.parm[0] == CLSTR_IFACE )
                {
                    iface = CLSTR_IFACE ;
                }
            }
            if ( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_SET )
            {
                /* Assert the minor condition with the 'false' (i.e. not clear)*/
                obj_ptr->manage_heartbeat_minor ( hostname, iface, false );
            }
            else
            {
                /* Clear the minor condition with the 'clear=true' */
                obj_ptr->manage_heartbeat_minor ( hostname, iface, true );
            }
        }
    }
    else if (( msg.cmd == MTC_EVENT_HEARTBEAT_DEGRADE_SET ) ||
             ( msg.cmd == MTC_EVENT_HEARTBEAT_DEGRADE_CLR ))
    {
        const char * event_hdr_ptr = get_heartbeat_event_header() ;

        /* Confirm header */
        if ( strncmp ( &msg.hdr[0], event_hdr_ptr, MSG_HEADER_SIZE ) )
        {
            elog ("Invalid Heartbeat Event header\n");
            rc = FAIL_BAD_PARM ;
        }
        else
        {
            /* The interface that the heartbeatdegrade  occurred over is
             * specified in parm[0] for this command
             * 0 = MGMNT_IFACE
             * 1 = CLSTR_IFACE
             * else default to 0 (MGMNT_IFACE) to be backwards compatible
             *
             * */
            iface_enum iface = MGMNT_IFACE;
            if ( msg.num > 0 )
            {
                if ( msg.parm[0] == CLSTR_IFACE )
                {
                    iface = CLSTR_IFACE ;
                }
            }

            if ( msg.cmd == MTC_EVENT_HEARTBEAT_DEGRADE_SET )
            {
                if (( obj_ptr->hbs_failure_action == HBS_FAILURE_ACTION__FAIL ) ||
                    ( obj_ptr->hbs_failure_action == HBS_FAILURE_ACTION__DEGRADE ))
                {
                    /* Assert the degrade condition with the 'false' (i.e. not clear)*/
                    obj_ptr->manage_heartbeat_degrade ( hostname, iface, false );
                }
                /* Otherwise the action must be alarm only or none ; both of which
                 * are already handled by the hbsAgent, so do nothing */
                else
                {
                    ilog ("%s heartbeat degrade event dropped ; action is not fail or degrade (%s)\n",
                              hostname.c_str(),
                              get_iface_name_str(iface));
                }
            }
            else
            {
                /* Clear the degrade condition with the 'true' */
                obj_ptr->manage_heartbeat_degrade ( hostname, iface, true );
            }
        }
    }
    else if ( msg.cmd == MTC_EVENT_HEARTBEAT_LOSS )
    {
        const char * loss_hdr_ptr = get_heartbeat_loss_header() ;

        /* Confirm header */
        if ( strncmp ( &msg.hdr[0], loss_hdr_ptr, MSG_HEADER_SIZE ) )
        {
            elog ("Invalid Heartbeat Loss event header\n");
            rc = FAIL_BAD_PARM ;
        }
        else
        {
            /* The interface that the heartbeat loss occurred over is
             * specified in parm[0 for this command
             * 0 = MGMNT_IFACE
             * 1 = CLSTR_IFACE
             * else default to 0 (MGMNT_IFACE) to be backwards compatible
             *
             * */
            iface_enum iface = MGMNT_IFACE;
            if ( msg.num > 0 )
            {
                if ( msg.parm[0] == CLSTR_IFACE )
                {
                    iface = CLSTR_IFACE ;
                }
            }

            /* If heartbeat failure action is fail then call the fail handler */
            if ( obj_ptr->hbs_failure_action == HBS_FAILURE_ACTION__FAIL )
                obj_ptr->manage_heartbeat_failure ( hostname, iface, false );

            /* If heartbeat failure action is degrade then call the degrade handler */
            else if ( obj_ptr->hbs_failure_action == HBS_FAILURE_ACTION__DEGRADE )
                obj_ptr->manage_heartbeat_degrade ( hostname, iface, false );

            /* Otherwise the action must be alarm only or none ; both of which
             * are already handled by the hbsAgent, so do nothing */
            else
            {
                ilog ("%s heartbeat loss event dropped ; action is not fail or degrade (%s)\n",
                          hostname.c_str(),
                          get_iface_name_str(iface));
            }
        }
    }
    else if ( msg.cmd == MTC_EVENT_PMOND_CLEAR )
    {
        string process = MTC_SERVICE_PMOND_NAME ;
        ilog ("%s Degrade Clear Event for process '%s'\n", hostname.c_str(), process.c_str());
        obj_ptr->degrade_pmond_clear ( hostname );
    }
    else if ( msg.cmd ==  MTC_EVENT_PMOND_RAISE )
    {
        string process = MTC_SERVICE_PMOND_NAME ;
        ilog ("%s Degrade Assert Event for process '%s'\n", hostname.c_str(), process.c_str());
        obj_ptr->degrade_process_raise ( hostname , process );
    }
    else if ( msg.cmd ==  MTC_EVENT_HOST_STALLED )
    {
        elog ("%s Stalled !!!\n", hostname.c_str());
    }

    else if ( msg.cmd == MTC_EVENT_MONITOR_READY )
    {
        string daemon = &msg.hdr[MSG_HEADER_SIZE] ;

        if ( !daemon.compare(MTC_SERVICE_GUESTAGENT_NAME) )
        {
            std::list<string>::iterator temp ;
            rc = PASS ;

            ilog ("%s %s ready event", hostname.c_str(), MTC_SERVICE_GUESTAGENT_NAME );

            /* If the active controller got the ready event from a local service
             * then push the inventory to that service and for each host that is
             * enabled send the start monitoring command to it.
             * Handles the daemon restart case */
            for ( temp  = obj_ptr->hostname_inventory.begin () ;
                  temp != obj_ptr->hostname_inventory.end () ;
                  temp++ )
            {
                hostname = temp->data();
                rc = send_guest_command ( hostname, MTC_CMD_ADD_HOST );
                if ( rc )
                {
                    elog ("%s host add to '%s' failed",
                              hostname.c_str(),
                              daemon.c_str());
                }
                else
                {
                    ilog ("%s added to %s",
                            hostname.c_str(),
                            daemon.c_str());
                }
            }
            /* Done sending the host info */
        }
        else
        {
            wlog ("Unsupported ready event for daemon: '%s'\n", daemon.c_str());
        }
    }

    else if ( msg.cmd == MTC_EVENT_HEARTBEAT_READY )
    {
        /* no heartbeating in simplex mode */
        if ( obj_ptr->system_type == SYSTEM_TYPE__AIO__SIMPLEX )
        {
            return (PASS);
        }

        /* Support for json formatted message in buffer */
        if (( msg.ver >= MTC_CMD_FEATURE_VER__KEYVALUE_IN_BUF ) &&
            ( msg.buf[msg.res] == '{' ))
        {
            jsonUtil_get_key_val(&msg.buf[msg.res], MTC_JSON_INV_NAME, hostname) ;
        }
        ilog ("%s %s ready event",
                  hostname.c_str(),
                  MTC_SERVICE_HBSAGENT_NAME);

        obj_ptr->hbs_ready = true ;
        /* Send inventory to the controller's hbsAgent that sent
         * the ready request. Save controller hostname. */
        string controller = hostname ;
        ilog ("%s %s inventory push ... start",
                  controller.c_str(),
                  MTC_SERVICE_HBSAGENT_NAME);

        std::list<string>::iterator temp ;
        for ( temp  = obj_ptr->hostname_inventory.begin () ;
              temp != obj_ptr->hostname_inventory.end () ;
              temp++ )
        {
            hostname = temp->data();

           /* Add all hosts, even the active controller, to
            * the heartbeat service. This tell the heartbeat
            * service about all the hosts so that it will
            * send heartbeat oob flag events to mtce. */
            if ( send_hbs_command( hostname, MTC_CMD_ADD_HOST, controller ) != PASS )
            {
                elog ("%s Failed to send inventory to heartbeat service\n", hostname.c_str());
            }
            /* Consider sending the 'start' request to the heartbeat service
             * for all enabled hosts. */
            if (( obj_ptr->get_adminState  ( hostname ) == MTC_ADMIN_STATE__UNLOCKED ) &&
                ( obj_ptr->get_operState   ( hostname ) == MTC_OPER_STATE__ENABLED ) &&
                ((obj_ptr->get_availStatus ( hostname ) == MTC_AVAIL_STATUS__AVAILABLE ) ||
                 (obj_ptr->get_availStatus ( hostname ) == MTC_AVAIL_STATUS__DEGRADED )))
            {
                /* However, bypass sending heartbeat 'start' for nodes that
                 * are not ready to heartbeat; enabling, configuring, testing.
                 * Such cases are if a host is:
                 *
                 * 1. running the add_handler or
                 * 2. running the enable_handler or
                 * 3. running the enable_subf_handler or
                 * 4. not configured or
                 * 5. not tested (goenabled not complete)
                 *
                 */
                mtc_nodeAdminAction_enum current_action =
                    obj_ptr->get_adminAction (hostname);
                if (( current_action != MTC_ADMIN_ACTION__ADD ) &&
                    ( current_action != MTC_ADMIN_ACTION__ENABLE ) &&
                    ( current_action != MTC_ADMIN_ACTION__ENABLE_SUBF ))
                {
                    int mtce_flags = obj_ptr->get_mtce_flags(hostname);
                    if (( mtce_flags & MTC_FLAG__I_AM_CONFIGURED ) &&
                        ( mtce_flags & MTC_FLAG__I_AM_HEALTHY  ) &&
                        ( mtce_flags & MTC_FLAG__MAIN_GOENABLED ))
                    {
                        if (( obj_ptr->system_type != SYSTEM_TYPE__NORMAL ) &&
                            ( obj_ptr->get_nodetype ( hostname ) & CONTROLLER_TYPE ))
                        {
                            /* If its an AIO then its worker subfunction
                             * needs to have been be configured and tested. */
                            if (( mtce_flags & MTC_FLAG__SUBF_CONFIGURED ) &&
                                ( mtce_flags & MTC_FLAG__SUBF_GOENABLED ))
                            {
                                ilog("%s heartbeat start (AIO controller)",
                                         hostname.c_str());
                                send_hbs_command ( hostname, MTC_CMD_START_HOST, controller );
                            }
                            else
                            {
                                wlog ("%s not heartbeat ready (subf) (oob:%x)",
                                          hostname.c_str(),
                                          mtce_flags);
                            }
                        }
                        else
                        {
                            ilog("%s heartbeat start (from ready event)",
                                     hostname.c_str());
                            send_hbs_command ( hostname, MTC_CMD_START_HOST, controller );
                        }
                    }
                    else
                    {
                        wlog ("%s not heartbeat ready (main) (oob:%x)",
                                  hostname.c_str(),
                                  mtce_flags);
                    }
                }
            }
        }
        ilog ("%s %s inventory push ... done",
                  controller.c_str(),
                  MTC_SERVICE_HBSAGENT_NAME);

        /* Ensure that the hbsAgent heartbeat period is correct */
        if ( obj_ptr->mnfa_backoff == true )
            send_hbs_command ( obj_ptr->my_hostname, MTC_BACKOFF_HBS, CONTROLLER );
        else
            send_hbs_command ( obj_ptr->my_hostname, MTC_RECOVER_HBS, CONTROLLER );
    }
    else
    {
        wlog ("Unrecognized Event from Heartbeat Service (hbsAgent)\n");
        rc = FAIL_BAD_PARM ;
    }
    /* print the message if there was an error */
    print_mtc_message ( hostname, MTC_CMD_TX, msg, get_iface_name_str(MGMNT_INTERFACE), rc );
    return rc ;
}


int send_hwmon_command ( string hostname, int command )
{
    int rc = PASS ;

    switch ( command )
    {
        case MTC_CMD_QRY_HOST:
        {
            ilog ("%s sending 'sensor read' request\n", hostname.c_str());
            break ;
        }
        case MTC_CMD_START_HOST:
        case MTC_CMD_STOP_HOST:
        case MTC_CMD_ADD_HOST:
        case MTC_CMD_MOD_HOST:
        case MTC_CMD_DEL_HOST:
        {
            if ( command == MTC_CMD_START_HOST )
            {
                get_mtcInv_ptr()->set_hwmond_monitor_state ( hostname, true );
            }
            else if ( command == MTC_CMD_STOP_HOST )
            {
                get_mtcInv_ptr()->set_hwmond_monitor_state ( hostname, false );
            }

            ilog ("%s %s sent to %s",
                      hostname.c_str(),
                      get_mtcNodeCommand_str(command),
                      MTC_SERVICE_HWMOND_NAME);
            break ;
        }
        default:
        {
            slog ("%s Unsupported command operation 0x%x\n",  hostname.c_str(), command );
            rc = FAIL_BAD_PARM ;
        }
    }

    if ( rc == PASS )
    {
        mtc_message_type cmd ;

        string hwmon_info = "" ;

        mtc_socket_type * sock_ptr = get_sockPtr ();
        nodeLinkClass * obj_ptr = get_mtcInv_ptr ();

        memset   ( &cmd, 0 , sizeof(mtc_message_type));
        snprintf ( &cmd.hdr[0] , MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header());
        snprintf ( &cmd.hdr[MSG_HEADER_SIZE], MAX_CHARS_HOSTNAME_32, "%s", hostname.data());

        /* Support for 64 byte hostnames */
        cmd.ver = MTC_CMD_FEATURE_VER__KEYVALUE_IN_BUF ;

        /* Hostname starts at the beginning of the buffer */
        cmd.res = 0 ;

        /* Store the command */
        cmd.cmd = command ;

        /* Copy the board management info string into the buffer and add one
         * to the length to accomodate for the null terminator snprintf
         * automatically adds */

        hwmon_info = obj_ptr->get_hwmon_info ( hostname );
        snprintf ( &cmd.buf[cmd.res] , hwmon_info.length()+1, "%s", hwmon_info.data());

        rc = sock_ptr->hwmon_cmd_sock->write((char *)&cmd, sizeof(mtc_message_type), obj_ptr->my_float_ip.c_str(), 0);
        if ( 0 > rc )
        {
            elog ("%s Failed sendto command to hwmond (%d:%s)\n", hostname.c_str(), errno, strerror(errno));
            rc = FAIL_SOCKET_SENDTO ;
        }
        else
        {
            rc = PASS ;
        }
        print_mtc_message ( hostname, MTC_CMD_TX, cmd, get_iface_name_str(MGMNT_INTERFACE), rc );
    }
    return rc ;
}
