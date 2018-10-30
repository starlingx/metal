/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
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


/* Throttle logging of messages from unknown IP addresses */
std::list<string> unknown_ip_list ;

/* Send specified command to the guestAgent daemon */
int send_guest_command ( string hostname, int command )
{
    int rc = PASS ;
    nodeLinkClass   *  obj_ptr = get_mtcInv_ptr();

    mlog ("%s NodeType %s (0x%x) Check: %c %c \n",
              hostname.c_str(),
              obj_ptr->functions.c_str(),
              obj_ptr->get_nodetype(hostname),
              obj_ptr->is_compute (hostname)            ? 'Y' : 'n',
              obj_ptr->is_compute_subfunction(hostname) ? 'Y' : 'n');

    if ( obj_ptr->is_compute            (hostname) ||
         obj_ptr->is_compute_subfunction(hostname))
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
            hostinfo.append (",\"personality\":\"compute\"");
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
    if ( iface == INFRA_INTERFACE )
    {
        if ( ( obj_ptr ) &&
             ( obj_ptr->infra_network_provisioned == true ) &&
             ( sock_ptr->mtc_agent_infra_rx_socket ))
        {
            bytes = sock_ptr->mtc_agent_infra_rx_socket->read((char*)&msg, sizeof(msg));
        }
        else
        {
            return ( FAIL_NO_INFRA_PROV );
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

    string hostaddr = "" ;
    string hostname = "" ;
    if ( iface == INFRA_INTERFACE )
    {
        hostaddr = sock_ptr->mtc_agent_infra_rx_socket->get_src_str();
        hostname = obj_ptr->get_hostname ( hostaddr ) ;
    }
    else
    {
        hostaddr = sock_ptr->mtc_agent_rx_socket->get_src_str();
        hostname = obj_ptr->get_hostname ( hostaddr ) ;
    }
    if ( hostname.empty() )
    {
        std::list<string>::iterator  iter ;
        iter = std::find (unknown_ip_list.begin(), unknown_ip_list.end(), hostaddr );
        if ( iter == unknown_ip_list.end() )
        {
            mlog3 ( "Received message from unknown IP <%s>\n", hostaddr.c_str());
            unknown_ip_list.push_front(hostaddr);
        }
        return (FAIL_NOT_FOUND);
    }
    else if ( ! hostaddr.empty() )
    {
       unknown_ip_list.remove (hostaddr);
    }

    print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(iface), false );

    if ( msg.hdr[0] == '{' )
    {
        int rc1 ;
        string service ;

        mlog1 ("%s\n", &msg.hdr[0] );

        rc1 = jsonUtil_get_key_val(&msg.hdr[0],"service", service );
        if ( rc1 == PASS )
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
                }
                else
                {
                    obj_ptr->collectd_notify_handler ( hostname,
                                                       resource,
                                                       state );
                }
            }
            /* future service requests */
            else
            {
                wlog ("Unexpected service request: '%s'\n", service.c_str());
            }
        }
        else
        {
            wlog("Unexpected json message: %s\n", &msg.hdr[0] );
        }
    }

    /* Check for response messages */
    else if ( strstr ( &msg.hdr[0], get_cmd_rsp_msg_header() ) )
    {
        obj_ptr->set_cmd_resp ( hostname , msg ) ;
    }

    /*
     * Check for compute messages
     */
    else if ( strstr ( &msg.hdr[0], get_compute_msg_header() ) )
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
                obj_ptr->set_uptime     ( hostname , msg.parm[MTC_PARM_UPTIME_IDX], false );
                obj_ptr->set_health     ( hostname , msg.parm[MTC_PARM_HEALTH_IDX] );
                obj_ptr->set_mtce_flags ( hostname , msg.parm[MTC_PARM_FLAGS_IDX]  );

                obj_ptr->set_mtcAlive   ( hostname, iface );

                mlog1("%s Uptime:%d Health:%d Flags:0x%x mtcAlive:%s\n",
                          hostname.c_str(),
                          msg.parm[MTC_PARM_UPTIME_IDX],
                          msg.parm[MTC_PARM_HEALTH_IDX],
                          msg.parm[MTC_PARM_FLAGS_IDX],
                          obj_ptr->get_mtcAlive_gate ( hostname ) ? "gated" : "open");

                string infra_ip = "";
                /* Get the infra ip address if it is provisioned */
                rc =  jsonUtil_get_key_val ( &msg.buf[0], "infra_ip", infra_ip );
                if ( rc == PASS )
                {
                    obj_ptr->set_infra_hostaddr ( hostname, infra_ip );
                }
                else
                {
                    mlog ("%s null or missing 'infra_ip' value (rc:%d)\n", hostname.c_str(), rc);
                }
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
                mlog ("%s-compute GOENABLED message\n", hostname.c_str());
                if ( !obj_ptr->my_hostname.compare(hostname) )
                {
                    ilog ("%s-compute received GOENABLED from self\n", hostname.c_str());
                }
                rc = send_mtc_cmd ( hostname , msg.cmd, MGMNT_INTERFACE );
                if ( rc != PASS )
                {
                    elog ("%s-compute GOENABLED send reply failed (rc:%d)\n",
                              hostname.c_str(), rc);

                    wlog ("%s-compute ... need successful GOENABLED reply, dropping ...\n",
                              hostname.c_str() );
                }
                else
                {
                    mlog ("%s-compute got GOENABLED (out-of-service tests passed) message\n", hostname.c_str());
                    obj_ptr->set_goEnabled_subf ( hostname );
                }
            }
            else if ( msg.cmd == MTC_MSG_SUBF_GOENABLED_FAILED )
            {
                if ( obj_ptr->get_adminState ( hostname ) == MTC_ADMIN_STATE__UNLOCKED )
                {
                    wlog ("%s-compute failed GOENABLE test: %s\n", hostname.c_str(), &msg.buf[0] );
                    obj_ptr->set_goEnabled_failed_subf ( hostname );
                }
                /* We don't send a reply on a fail */
            }
            else
            {
                wlog ("Unexpected compute message (0x%x) from '%s'\n", msg.cmd, hostname.c_str());
            }
    }

    /*
     * Check for Event Messages
     */
    else if ( strstr ( &msg.hdr[0], get_mtce_event_header() ) )
    {
        rc = PASS ;
        if ( hostname.empty() )
        {
            mlog2 ( "Received mtce event from unknown host\n");
            rc = FAIL_UNKNOWN_HOSTNAME ;
        }
        else if ( !hostname.compare("localhost") )
        {
            mlog2 ("localhost event (%x) ignored", msg.cmd);
        }
        else
        {
            string event = "" ;

            /* TODO: fix this hostname setting */
            if (( msg.cmd == MTC_DEGRADE_CLEAR          ) ||
                ( msg.cmd == MTC_DEGRADE_RAISE          ) ||
                ( msg.cmd == MTC_EVENT_HWMON_CLEAR      ) ||
                ( msg.cmd == MTC_EVENT_HWMON_MINOR      ) ||
                ( msg.cmd == MTC_EVENT_HWMON_MAJOR      ) ||
                ( msg.cmd == MTC_EVENT_HWMON_CRIT       ) ||
                ( msg.cmd == MTC_EVENT_HWMON_RESET      ) ||
                ( msg.cmd == MTC_EVENT_HWMON_POWERDOWN ) ||
                ( msg.cmd == MTC_EVENT_HWMON_POWERCYCLE) ||
                ( msg.cmd == MTC_EVENT_HWMON_CONFIG     ))
            {
                hostname = &msg.hdr[MSG_HEADER_SIZE] ;
            }
            /* the mtce event (process or resource) that causes this raised event is at the
             * head of the message buffer. Load it into an 'event'
             * string to be passed into the individual handlers for
             * convenience. Safer to pass reference to a string than
             * the raw buffer pointer. */
            if ( strnlen ( &msg.buf[0] , MAX_MTCE_EVENT_NAME_LEN ) )
            {
                event = msg.buf ;
            }

            switch ( msg.cmd )
            {
                /* TODO: Port other services to use this common code */
                case MTC_EVENT_MONITOR_READY:
                {
                    std::list<string>::iterator temp ;
                    // bool start_monitoring_flag = false ;

                    if ( !event.compare("pmond") )
                    {
                        /* Notify mtcAgent that we got a pmond ready event */
                        obj_ptr->declare_service_ready ( hostname, MTC_SERVICE_PMOND );
                        return (PASS);
                    }
                    else if ( !event.compare("hbsClient") )
                    {
                        /* Notify mtcAgent that we got a hbsClient ready event */
                        obj_ptr->declare_service_ready ( hostname, MTC_SERVICE_HEARTBEAT );
                        return (PASS);
                    }

                    /* If the active controller got the ready event from a local service
                     * then push the inventory to that service and for each host that is
                     * enabled send the start monitoring command to it if the bm_ip is
                     * provisioned.
                     * Handles the daemon restart case */
                    for ( temp  = obj_ptr->hostname_inventory.begin () ;
                          temp != obj_ptr->hostname_inventory.end () ;
                          temp++ )
                    {
                        hostname = temp->data();

                        /* Set the general start monitoring flag based on service state.
                         * This lag may be over ridden my individual services based on
                         * additional information */
                        if (( obj_ptr->get_adminState  ( hostname ) == MTC_ADMIN_STATE__UNLOCKED ) &&
                            ( obj_ptr->get_operState   ( hostname ) == MTC_OPER_STATE__ENABLED ) &&
                            ((obj_ptr->get_availStatus ( hostname ) == MTC_AVAIL_STATUS__AVAILABLE ) ||
                             (obj_ptr->get_availStatus ( hostname ) == MTC_AVAIL_STATUS__DEGRADED )))
                        {
                            ; // start_monitoring_flag = true ;
                        }
                        else
                        {
                            ; // start_monitoring_flag = false ;
                        }

                        if ( !event.compare("hwmond") )
                        {
                            obj_ptr->declare_service_ready ( hostname, MTC_SERVICE_HWMOND );
                        }
                        else
                        {
                            wlog ("%s Global Ready Event not supported for '%s' service\n",
                                      hostname.c_str(), event.c_str());

                            return (FAIL_BAD_PARM);
                        }
                    }
                    break ;
                }

               /*****************************************************************
                *                   Data Port Events                            *
                *****************************************************************/

               /*****************************************************************
                *                Process Monitor Events                         *
                *****************************************************************/
                case MTC_EVENT_PMON_CLEAR:
                {
                    mlog ("%s pmond: '%s' recovered (clear)\n", hostname.c_str(), event.c_str());
                    obj_ptr->degrade_pmond_clear ( hostname );
                    break ;
                }
                case MTC_EVENT_PMON_CRIT:
                {
                    mlog ("%s pmond: '%s' failed (critical)\n", hostname.c_str(), event.c_str());

                    /**
                     *  event   is the process name that has failed
                     *  parm[0] is the nodetype the process serves
                     **/
                    obj_ptr->critical_process_failed ( hostname, event, msg.parm[0] );
                    break ;
                }
                case MTC_EVENT_PMON_MAJOR:
                {
                    mlog ("%s pmond: '%s' failed (major)\n", hostname.c_str(), event.c_str());
                    obj_ptr->degrade_process_raise ( hostname, event );
                    break ;
                }
                case MTC_EVENT_PMON_MINOR:
                {
                    mlog ("%s pmond: '%s' failed (minor)\n", hostname.c_str(), event.c_str());
                    obj_ptr->alarm_process_failure ( hostname, event );
                    break ;
                }
                case MTC_EVENT_PMON_LOG:
                {
                    mlog ("%s pmond: '%s' failed (log)\n", hostname.c_str(), event.c_str());
                    obj_ptr->log_process_failure ( hostname, event );
                    break ;
                }

               /*****************************************************************
                *                Resource Monitor Events                        *
                *****************************************************************/

                /* TODO: Remove - Suspecting OBS Command */
                case MTC_EVENT_RMON_READY:
                {
                    mlog ("%s RMON Ready\n", hostname.c_str());
                    obj_ptr->declare_service_ready ( hostname, MTC_SERVICE_RMOND );
                    break ;
                }
                case MTC_EVENT_RMON_CLEAR:
                {
                    mlog ("%s rmond: '%s' recovered (clear)\n", hostname.c_str(), event.c_str());
                    obj_ptr->degrade_resource_clear ( hostname , event );
                    break ;
                }
                case MTC_EVENT_RMON_CRIT:
                {
                    mlog ("%s rmond: '%s' failed (critical)\n", hostname.c_str(), event.c_str());
                    obj_ptr->critical_resource_failed ( hostname, event );
                    break ;
                }
                case MTC_EVENT_RMON_MAJOR:
                {
                    mlog ("%s rmond: '%s' failed (major)\n", hostname.c_str(), event.c_str());
                    obj_ptr->degrade_resource_raise ( hostname, event );
                    break ;
                }
                case MTC_EVENT_RMON_MINOR:
                {
                    mlog ("%s rmond: '%s' failed (minor)\n", hostname.c_str(), event.c_str());
                    /* Clear the degrade condition if one is present */
                    obj_ptr->degrade_resource_clear ( hostname , event );
                    obj_ptr->log_resource_failure ( hostname, event );
                    break ;
                }

                case MTC_EVENT_HWMON_CLEAR:
                case MTC_DEGRADE_CLEAR:
                {
                    mlog ("%s hwmon requests to clear its degrade flag\n", hostname.c_str());
                    obj_ptr->node_degrade_control ( hostname, MTC_DEGRADE_CLEAR , "hwmon" );
                    break ;
                }
                case MTC_EVENT_HWMON_MINOR:
                case MTC_EVENT_HWMON_MAJOR:
                case MTC_EVENT_HWMON_CRIT:
                case MTC_DEGRADE_RAISE:
                {
                    mlog ("%s hwmon requested to set its degrade flag\n", hostname.c_str());
                    obj_ptr->node_degrade_control ( hostname, MTC_DEGRADE_RAISE , "hwmon" );
                    break ;
                }
                case MTC_EVENT_HWMON_RESET:
                case MTC_EVENT_HWMON_POWERDOWN:
                case MTC_EVENT_HWMON_POWERCYCLE:
                {
                    mlog ("%s requires maintenance '%s' action due to failing '%s' sensor \n",
                              hostname.c_str(),
                              get_event_str(msg.cmd).c_str(),
                              event.c_str());

                    obj_ptr->invoke_hwmon_action ( hostname, msg.cmd, event );
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
    }
    else
    {
        wlog ( "Received unsupported or badly formed message\n" );
    }

    /* Only do this if the debug level is appropriate */
    if ( daemon_get_cfg_ptr()->debug_msg )
    {
        int count = 0 ;
        std::list<string>::iterator  iter ;
        for ( iter  = unknown_ip_list.begin () ;
              iter != unknown_ip_list.end  () ;
              iter++ )
        {
            count++ ;
            mlog3 ("Unknown IP [%d]:%s\n", count, iter->c_str());
        }
    }
    return (rc);
}

int send_mtc_cmd ( string & hostname, int cmd , int interface )
{
    int rc = FAIL ;
    bool force = false ;
    mtc_message_type mtc_cmd ;
    mtc_socket_type * sock_ptr = get_sockPtr ();
    memset (&mtc_cmd,0,sizeof(mtc_message_type));

    /* Add the command version to he message */
    mtc_cmd.ver = MTC_CMD_VERSION ;
    mtc_cmd.rev = MTC_CMD_REVISION;

    switch ( cmd )
    {
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
        case MTC_CMD_STOP_COMPUTE_SVCS:
        case MTC_CMD_STOP_STORAGE_SVCS:
        case MTC_CMD_START_CONTROL_SVCS:
        case MTC_CMD_START_COMPUTE_SVCS:
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
            ilog ("%s sending '%s' request (%s network)\n", hostname.c_str(), get_mtcNodeCommand_str(cmd), get_iface_name_str(interface));
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
            ilog ("%s sending '%s' request (%s network)\n", hostname.c_str(), get_mtcNodeCommand_str(cmd), get_iface_name_str(interface));
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
            mlog ("%s sending 'Locked' notification (%s network)\n", hostname.c_str(), get_iface_name_str(interface));
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

        /* Temporarily get IP from node inventory till dns is available */
        nodeLinkClass * obj_ptr = get_mtcInv_ptr ();

        /* add the mac address of the target card to the header
         * Note: the minus 1 is to overwqrite the null */
        snprintf ( &mtc_cmd.hdr[MSG_HEADER_SIZE-1], MSG_HEADER_SIZE, "%s", obj_ptr->get_hostIfaceMac(hostname, MGMNT_IFACE).data());

        /* Lets add the controller's floating ip in the buffer so hat he host knowns where to reply */
        snprintf ( &mtc_cmd.buf[0], obj_ptr->my_float_ip.length()+1, "%s", obj_ptr->my_float_ip.data());

        /* only send the minimum amount of data */
        bytes = (sizeof(mtc_message_type)-(BUF_SIZE-(obj_ptr->my_float_ip.length()+1))) ;

        print_mtc_message ( hostname, MTC_CMD_TX, mtc_cmd, get_iface_name_str(interface), force ) ;

        if (interface == MGMNT_INTERFACE)
        {
            string hostaddr = obj_ptr->get_hostaddr(hostname);

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__INVALIDATE_MGMNT_IP, hostname ) )
                hostaddr = "none" ;
#endif

            if ( hostUtil_is_valid_ip_addr ( hostaddr ) != true )
            {
                 wlog("%s has no management IP assigned\n", hostname.c_str());
                return (FAIL_HOSTADDR_LOOKUP);
            }
            /* rc = message size */
            rc = sock_ptr->mtc_agent_tx_socket->write((char *)&mtc_cmd, bytes, hostaddr.c_str(), sock_ptr->mtc_cmd_port);
        }
        else if ((interface == INFRA_INTERFACE) &&
                 ( obj_ptr->infra_network_provisioned == true ) &&
                 ( sock_ptr->mtc_agent_infra_tx_socket != NULL ))
        {
            /* SETUP TX -> COMPUTE SOCKET INFRA INTERFACE */
            string infra_hostaddr = obj_ptr->get_infra_hostaddr(hostname);

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__INVALIDATE_INFRA_IP, hostname ) )
                infra_hostaddr = "none" ;
#endif

            if ( hostUtil_is_valid_ip_addr( infra_hostaddr ) != true )
            {
                return (FAIL_NO_INFRA_PROV);
            }
            rc = sock_ptr->mtc_agent_infra_tx_socket->write((char *)&mtc_cmd, bytes, infra_hostaddr.c_str(), sock_ptr->mtc_cmd_port);
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

int send_hbs_command ( string hostname, int cmd )
{
    int bytes = 0 ;
    int bytes_to_send = 0 ;
    int rc = PASS ;

    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    mtc_message_type event ;
    mtc_socket_type * sock_ptr = get_sockPtr ();

    /* We don't heartbeat self */
    if (( obj_ptr->is_active_controller (hostname) ) &&
        (( cmd == MTC_CMD_ADD_HOST   ) ||
         ( cmd == MTC_CMD_DEL_HOST   ) ||
         ( cmd == MTC_CMD_START_HOST ) ||
         ( cmd == MTC_CMD_STOP_HOST  )))
    {
       dlog ("%s refusing to '%s' self to heartbeat service\n",
                 hostname.c_str(), get_event_str(cmd).c_str());
       return (PASS);
    }

    memset (&event, 0 , sizeof(mtc_message_type));
    snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_hbs_cmd_req_header() );
    snprintf ( &event.hdr[MSG_HEADER_SIZE] , MAX_CHARS_HOSTNAME , "%s", hostname.data());

    /* There is no buffer data in any of these messages */
    bytes_to_send = ((sizeof(mtc_message_type))-(BUF_SIZE)) ;

    switch ( cmd )
    {
        case MTC_CMD_STOP_HOST:
            ilog ("%s sending 'stop' to heartbeat service\n", hostname.c_str());
            break ;
        case MTC_CMD_START_HOST:
            obj_ptr->manage_heartbeat_clear ( hostname , MAX_IFACES );
            ilog ("%s sending 'start' to heartbeat service\n", hostname.c_str());
            break ;
        case MTC_CMD_DEL_HOST:
            ilog ("%s sending 'delete' to heartbeat service\n", hostname.c_str());
            break ;
        case MTC_CMD_ADD_HOST:
            obj_ptr->manage_heartbeat_clear ( hostname, MAX_IFACES );
            ilog ("%s sending 'add' to heartbeat service\n", hostname.c_str());
            break ;
        case MTC_RESTART_HBS:
            ilog ("%s sending 'restart' to heartbeat service\n", hostname.c_str());
            break ;
        case MTC_BACKOFF_HBS:
            ilog ("%s requesting heartbeat period backoff\n", hostname.c_str());
            break ;
        case MTC_RECOVER_HBS:
            ilog ("%s requesting heartbeat period recovery\n", hostname.c_str());
            break ;
        default:
        {
            slog ("%s Unsupported command operation 0x%x\n",  hostname.c_str(), cmd );
            return (FAIL_BAD_PARM);
        }
    }

    event.cmd     = cmd ;
    event.num     = 1   ;
    event.parm[0] = obj_ptr->get_nodetype(hostname);

    /* send to hbsAgent daemon port */
    bytes = sock_ptr->mtc_to_hbs_sock->write((char*) &event, bytes_to_send);
    if ( bytes <= 0 )
    {
        wlog ("Cannot send to heartbeat service\n");
        rc = FAIL_TO_TRANSMIT ;
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

    if ( msg.cmd == MTC_EVENT_LOOPBACK )
    {
        const char * event_hdr_ptr = get_loopback_header() ;

        /* Confirm header */
        if ( strncmp ( &msg.hdr[0], event_hdr_ptr, MSG_HEADER_SIZE ) )
        {
            elog ("Invalid Event header\n");
        }
        else
        {
            ilog ("Service ping\n");

            /* Should send back a response */
        }
    }

    else if (( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_SET ) ||
             ( msg.cmd == MTC_EVENT_HEARTBEAT_MINOR_CLR ))
    {
        const char * event_hdr_ptr = get_heartbeat_event_header() ;

        /* Confirm header */
        if ( strncmp ( &msg.hdr[0], event_hdr_ptr, MSG_HEADER_SIZE ) )
        {
            elog ("Invalid Heartbeat Event header\n");
        }
        else
        {
            string hostname = &msg.buf[0] ;
            print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(MGMNT_INTERFACE), false );

            /* The interface that the heartbeat loss occurred over is
             * specified in parm[0 for this command
             * 0 = MGMNT_IFACE
             * 1 = INFRA_IFACE
             * else default to 0 (MGMNT_IFACE) to be backwards compatible
             *
             * */
            iface_enum iface = MGMNT_IFACE;
            if ( msg.num > 0 )
            {
                if ( msg.parm[0] == INFRA_IFACE )
                {
                    iface = INFRA_IFACE ;
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
        }
        else
        {
            string hostname = &msg.buf[0] ;
            print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(MGMNT_INTERFACE), false );

            /* The interface that the heartbeat loss occurred over is
             * specified in parm[0 for this command
             * 0 = MGMNT_IFACE
             * 1 = INFRA_IFACE
             * else default to 0 (MGMNT_IFACE) to be backwards compatible
             *
             * */
            iface_enum iface = MGMNT_IFACE;
            if ( msg.num > 0 )
            {
                if ( msg.parm[0] == INFRA_IFACE )
                {
                    iface = INFRA_IFACE ;
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
        }
        else
        {
            /* The interface that the heartbeat loss occurred over is
             * specified in parm[0 for this command
             * 0 = MGMNT_IFACE
             * 1 = INFRA_IFACE
             * else default to 0 (MGMNT_IFACE) to be backwards compatible
             *
             * */
            iface_enum iface = MGMNT_IFACE;
            if ( msg.num > 0 )
            {
                if ( msg.parm[0] == INFRA_IFACE )
                {
                    iface = INFRA_IFACE ;
                }
            }
            string hostname = &msg.buf[0] ;
            print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(MGMNT_INTERFACE), false );

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
                dlog ("%s heartbeat loss event dropped (%s)\n",
                          hostname.c_str(),
                          get_iface_name_str(iface));
            }
        }
    }
    else if ( msg.cmd == MTC_EVENT_PMOND_CLEAR )
    {
        string hostname = &msg.hdr[MSG_HEADER_SIZE] ;
        string process = "pmond" ;
        ilog ("%s Degrade Clear Event for process '%s'\n", hostname.c_str(), process.c_str());
        print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(MGMNT_INTERFACE), false );
        obj_ptr->degrade_pmond_clear ( hostname );
    }
    else if ( msg.cmd ==  MTC_EVENT_PMOND_RAISE )
    {
        string hostname = &msg.hdr[MSG_HEADER_SIZE] ;
        string process = "pmond" ;
        ilog ("%s Degrade Assert Event for process '%s'\n", hostname.c_str(), process.c_str());
        print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(MGMNT_INTERFACE), false );
        obj_ptr->degrade_process_raise ( hostname , process );
    }
    else if ( msg.cmd ==  MTC_EVENT_HOST_STALLED )
    {
        string hostname = &msg.hdr[MSG_HEADER_SIZE] ;
        print_mtc_message ( hostname, MTC_CMD_RX, msg, get_iface_name_str(MGMNT_INTERFACE), false );
        elog ("%s Stalled !!!\n", hostname.c_str());
    }

    else if ( msg.cmd == MTC_EVENT_MONITOR_READY )
    {
        string daemon = &msg.hdr[MSG_HEADER_SIZE] ;

        if ( !daemon.compare("guestAgent") )
        {
            std::list<string>::iterator temp ;
            int rc = PASS ;

            /* If the active controller got the ready event from a local service
             * then push the inventory to that service and for each host that is
             * enabled send the start monitoring command to it.
             * Handles the daemon restart case */
            for ( temp  = obj_ptr->hostname_inventory.begin () ;
                  temp != obj_ptr->hostname_inventory.end () ;
                  temp++ )
            {
                string hostname = temp->data();
                rc = send_guest_command ( hostname, MTC_CMD_ADD_HOST );
                if ( rc )
                {
                    elog ("%s host add to '%s' failed\n", hostname.c_str(), daemon.c_str());
                }
                else
                {
                    ilog ("%s added to guestAgent\n", hostname.c_str());
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
        std::list<string>::iterator temp ;

        /* no heartbeating in simplex mode */
        if ( obj_ptr->system_type == SYSTEM_TYPE__CPE_MODE__SIMPLEX )
        {
            return (PASS);
        }

        ilog ("Received 'Heartbeat Service Ready' Event\n");
        obj_ptr->hbs_ready = true ;

        /* Run Maintenance on Inventory */
        for ( temp  = obj_ptr->hostname_inventory.begin () ;
              temp != obj_ptr->hostname_inventory.end () ;
              temp++ )
        {
            string hostname = "" ;
            hostname.append( temp->c_str() ) ;

          /* Add all hosts, even the active controller, to
           * the heartbeat service. This tell the heartbeat
           * service about all the hosts so that it will
           * send heartbeat oob flag events to mtce. */
            if ( send_hbs_command( hostname, MTC_CMD_ADD_HOST ) != PASS )
            {
                elog ("%s Failed to send inventory to heartbeat service\n", hostname.c_str());
            }
            /* Send the start event to the heartbeat service for all enabled hosts except
             * for the active controller which is not actively monitored */
            if ( obj_ptr->is_active_controller ( hostname ) == false )
            {
                if (( obj_ptr->get_adminState  ( hostname ) == MTC_ADMIN_STATE__UNLOCKED ) &&
                    ( obj_ptr->get_operState   ( hostname ) == MTC_OPER_STATE__ENABLED ) &&
                    ((obj_ptr->get_availStatus ( hostname ) == MTC_AVAIL_STATUS__AVAILABLE ) ||
                     (obj_ptr->get_availStatus ( hostname ) == MTC_AVAIL_STATUS__DEGRADED )))
                {
                    send_hbs_command ( hostname, MTC_CMD_START_HOST );
                }
            }
            else
            {
                dlog ("%s Refusing to start heartbeat of self\n", hostname.c_str() );
            }
        }
    }
    else
    {
         wlog ("Unrecognized Event from Heartbeat Service (hbsAgent)\n");
    }
    return PASS ;
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

            ilog ("%s sending '%s' to hwmond service\n", hostname.c_str(), get_event_str(command).c_str());
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
        int bytes = 0;

        mtc_socket_type * sock_ptr = get_sockPtr ();
        nodeLinkClass * obj_ptr = get_mtcInv_ptr ();

        memset   ( &cmd, 0 , sizeof(mtc_message_type));
        snprintf ( &cmd.hdr[0] , MSG_HEADER_SIZE, "%s", get_cmd_req_msg_header());
        snprintf ( &cmd.hdr[MSG_HEADER_SIZE], MAX_CHARS_HOSTNAME, "%s", hostname.data());

        /* Store the command, get the board management info and copy it into the message buffer */
        cmd.cmd = command ;
        hwmon_info = obj_ptr->get_hwmon_info ( hostname );
        memcpy ( &cmd.buf[0], hwmon_info.data(), hwmon_info.length());

        /* rc = message size */
        bytes = sizeof(mtc_message_type);
        rc = sock_ptr->hwmon_cmd_sock->write((char *)&cmd, bytes, obj_ptr->my_float_ip.c_str(), 0);
        if ( 0 > rc )
        {
            elog ("%s Failed sendto command to hwmond (%d:%s)\n", hostname.c_str(), errno, strerror(errno));
            rc = FAIL_SOCKET_SENDTO ;
        }
        else
        {
            print_mtc_message ( hostname, MTC_CMD_TX, cmd, get_iface_name_str(MGMNT_INTERFACE), false );
            rc = PASS ;
        }
    }
    return rc ;
}



