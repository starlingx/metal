/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Guest Heartbeat Server Daemon on Compute
  */

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>        /* for hostent */
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>       /* for close and usleep */
#include <sched.h>        /* for realtime scheduling api */
#include <json-c/json.h>

using namespace std;

#include "nodeBase.h"
#include "daemon_ini.h"     /* Ini Parser Header                            */
#include "daemon_common.h"  /* Common definitions and types for daemons     */
#include "daemon_option.h"  /* Common options  for daemons                  */
#include "nodeUtil.h"       /* for ... common utilities                     */
#include "jsonUtil.h"       /* for ... jason utilities                      */
#include "nodeTimers.h"     /* for ... maintenance timers                   */
#include "nodeMacro.h"      /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET   */
#include "nodeEvent.h"      /* for ... set_inotify_watch, set_inotify_close */
#include "guestBase.h"
#include "guestInstClass.h" /* for ... guestUtil_inst_init                  */
#include "guestUtil.h"      /* for ... guestUtil_inst_init                  */
#include "guestSvrUtil.h"   /* for ... hb_get_message_type_name             */
#include "guestSvrMsg.h"    /* for ... this module header                   */

extern void hbStatusChange ( instInfo * instInfo_ptr, bool status );
extern void beatStateChange ( instInfo * instInfo_ptr , hb_state_t newState );

/*****************************************************************************
 *
 * Name   : guestSvrMsg_hdr_init 
 *
 * Purpose: Initialize the message header. Example output:
 *          {"version":2,"revision":1,"msg_type":"init","sequence":29,
 *          The rest of the message should be appended to it.
 *
 *****************************************************************************/

string guestSvrMsg_hdr_init (string channel, string  msg_type)
{
    instInfo * instInfo_ptr = get_instInv_ptr()->get_inst (channel);

    string msg = "\n{\"";
    msg.append(GUEST_HEARTBEAT_MSG_VERSION);
    msg.append("\":");
    msg.append(int_to_string(GUEST_HEARTBEAT_MSG_VERSION_CURRENT));
    msg.append(",\"");
    msg.append(GUEST_HEARTBEAT_MSG_REVISION);
    msg.append("\":");
    msg.append(int_to_string(GUEST_HEARTBEAT_MSG_REVISION_CURRENT));
    msg.append(",\"");
    msg.append(GUEST_HEARTBEAT_MSG_MSG_TYPE);
    msg.append("\":\"");
    msg.append(msg_type);
    msg.append("\",\"");
    msg.append(GUEST_HEARTBEAT_MSG_SEQUENCE);
    msg.append("\":");
    msg.append(int_to_string(++(instInfo_ptr->sequence)));
    msg.append(",");

    // store msg_type in instance structure so that it is available to handle timeout
    instInfo_ptr->msg_type = msg_type;
    return msg;
}

/**
 * Manages the fault reporting state 
 * - returns current reporting state
 * */
bool manage_reporting_state ( instInfo * instInfo_ptr, string state)
{
    if (!state.compare("enabled"))
    {
        if ( instInfo_ptr->heartbeat.reporting == false )
        {
            ilog ("%s heartbeat reporting '%s' by guestAgent\n",
                      log_prefix(instInfo_ptr).c_str(),
                      state.c_str());
            
            instInfo_ptr->heartbeat.reporting = true ;
            instInfo_ptr->message_count = 0 ;
        }
    }
    else
    {
        if ( instInfo_ptr->heartbeat.reporting == true )
        {
            ilog ("%s heartbeat reporting '%s' by guestAgent\n",
                      log_prefix(instInfo_ptr).c_str(),
                      state.c_str());

            instInfo_ptr->heartbeat.reporting = false ;
            instInfo_ptr->message_count = 0 ;
            hbStatusChange  ( instInfo_ptr, false) ; /* heartbeating is now false */
            beatStateChange ( instInfo_ptr, hbs_server_waiting_init ) ;
        }
    }

    return instInfo_ptr->heartbeat.reporting ;
}

/*****************************************************************************
 *
 * Name   : guestAgent_qry_handler
 *
 * Purpose: Loop over all the instances and return their uuid, hostname,
 *          reporting state, heartbneating status and timeout values.
 *
 * { "hostname":"compute-1", "instances": [{"uuid":"<uuid>","heartbeat":"<state>", status":"<status>}, timeouts ...]}
 *
 *****************************************************************************/
int guestInstClass::guestAgent_qry_handler ( void )
{
    int rc = PASS ;
    
    /* check for empty list condition */
    if ( inst_head )
    {
        struct inst * inst_ptr = static_cast<struct inst *>(NULL) ;
        for ( inst_ptr = inst_head ; ; inst_ptr = inst_ptr->next )
        {
            string payload = guestUtil_set_inst_info ( get_ctrl_ptr()->hostname , &inst_ptr->instance );
            jlog ("%s Query Instance Response:%ld:%s\n", 
                      log_prefix(&inst_ptr->instance).c_str(), 
                      payload.size(), 
                      payload.c_str() );

            if (( rc=send_to_guestAgent ( MTC_CMD_QRY_INST, payload.data())) != PASS )
            {
                wlog ("%s failed to send query instance response to guestAgent\n", 
                          log_prefix(&inst_ptr->instance).c_str());
            }

            /* Deal with exit case */
            if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
            {
                break ;
            }
        }
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name   : recv_from_guestAgent
 *
 * Purpose: Handle guestAgent commands
 *
 *            MTC_EVENT_LOOPBACK
 *            MTC_CMD_QRY_INST
 *            MTC_CMD_DEL_INST
 *            MTC_CMD_MOD_INST
 *            MTC_CMD_ADD_INST
 *            MTC_CMD_MOD_HOST
 *
 * ***************************************************************************/
int recv_from_guestAgent ( unsigned int cmd, char * buf_ptr )
{
    int rc = PASS ;

    mlog1 ("Cmd:%x - %s\n", cmd, buf_ptr);

    if ( cmd == MTC_EVENT_LOOPBACK )
    {
        /* TODO: Send message back */
        return (rc) ;
    }
    else if ( cmd == MTC_CMD_QRY_INST )
    {
        if ( ( rc = get_instInv_ptr()->qry_inst ()) != PASS )
        {
            elog ("failed to send hosts instance info\n");
        }
        return (rc) ;
    }
    else if ( cmd == MTC_CMD_VOTE_INST
           || cmd == MTC_CMD_NOTIFY_INST )
    {
        string source;
        string uuid;
        string event;

        rc = FAIL_KEY_VALUE_PARSE ; /* default to parse error */

        if (( rc = jsonUtil_get_key_val ( buf_ptr, "source", source ))        != PASS)
        {
            elog ("failed to extract 'source' (cmd:%x %s)\n", cmd , buf_ptr );
        }
        else if (( rc = jsonUtil_get_key_val ( buf_ptr, "uuid", uuid )) != PASS)
        {
            elog ("failed to extract 'uuid' (cmd:%x %s)\n", cmd , buf_ptr);
        }
        else if (( rc = jsonUtil_get_key_val ( buf_ptr, "event", event )) != PASS)
        {
            elog ("failed to extract 'event' key (cmd:%x %s)\n", cmd , buf_ptr);
        }
        else
        {

            // send message to guest Client
            instInfo * instInfo_ptr = get_instInv_ptr()->get_inst(uuid);
            if ( instInfo_ptr )
            {
                /* If this is a resume then we need to reconnect to the channel */
                if ( !event.compare(GUEST_HEARTBEAT_MSG_EVENT_RESUME) )
                {
                    /* issue a reconnect if we are not connected the hartbeating has not started */
                    if (( instInfo_ptr->connected == false ) || 
                        ( instInfo_ptr->heartbeating == false ))
                    {
                        // instInfo_ptr->connect_wait_in_secs = 10 ;
                        get_instInv_ptr()->reconnect_start ( instInfo_ptr->uuid.data() );
                    }
                }
                
                instInfo_ptr->event_type = event;
                if (MTC_CMD_VOTE_INST == cmd)
                {
                    // for voting
                    instInfo_ptr->notification_type = GUEST_HEARTBEAT_MSG_NOTIFY_REVOCABLE ;
                
                    ilog ("%s sending revocable '%s' vote\n",
                          log_prefix(instInfo_ptr).c_str(),
                          event.c_str());
                }
                else
                {
                    // for notification
                    instInfo_ptr->notification_type = GUEST_HEARTBEAT_MSG_NOTIFY_IRREVOCABLE ;

                    ilog ("%s sending irrevocable '%s' notify\n",
                          log_prefix(instInfo_ptr).c_str(),
                          event.c_str());
                }
                get_instInv_ptr()->send_vote_notify(uuid) ;
                rc = PASS ;
            }
            else
            {
                wlog ("%s is unknown\n", uuid.c_str());
            }
        }
    }
    else
    {
        string source  ;
        string uuid ;
        string service ;
        string state   ;

        rc = FAIL_KEY_VALUE_PARSE ; /* default to parse error */

        if (( rc = jsonUtil_get_key_val ( buf_ptr, "source", source ))        != PASS)
        {
            elog ("failed to extract 'source' (cmd:%x %s)\n", cmd , buf_ptr ); 
        }
        else if (( rc = jsonUtil_get_key_val ( buf_ptr, "uuid", uuid )) != PASS)
        {
            elog ("failed to extract 'uuid' (cmd:%x %s)\n", cmd , buf_ptr); 
        }
        else if (( rc = jsonUtil_get_key_val ( buf_ptr, "service", service )) != PASS)
        {
            elog ("failed to extract 'service' key (cmd:%x %s)\n", cmd , buf_ptr); 
        }
        else if (( rc = jsonUtil_get_key_val ( buf_ptr, "state", state ))     != PASS)
        {
            elog ("failed to extract 'state' (cmd:%x %s)\n", cmd , buf_ptr );
        }
        else
        {
            rc = RETRY ;
            switch ( cmd )
            {
                case MTC_CMD_DEL_INST:
                {
                    ilog ("%s delete\n", uuid.c_str());

                    if ( get_instInv_ptr()->del_inst( uuid ) == PASS )
                    {
                        rc = PASS ;
                    }
                    else
                    {
                        dlog ("%s delete failed ; uuid lookup\n", uuid.c_str());
                        rc = FAIL_NOT_FOUND ;
                    }
                    if (daemon_get_cfg_ptr()->debug_level )
                         get_instInv_ptr()->print_instances ();
                    break ;
                }
                case MTC_CMD_ADD_INST:
                case MTC_CMD_MOD_INST:
                {
                    instInfo * instInfo_ptr = get_instInv_ptr()->get_inst ( uuid );
                    if ( instInfo_ptr )
                    { 
                        manage_reporting_state ( instInfo_ptr, state );
                        rc = PASS ;
                    }
    
                    /* if true then the current channel was not found and we need to add it */
                    if ( rc == RETRY )
                    {
                        instInfo instance ;
                        guestUtil_inst_init (&instance);
                        
                        instance.uuid = uuid ;
                        ilog ("%s add with %s reporting %s\n",
                               uuid.c_str(), 
                               service.c_str(), 
                               state.c_str());

                        get_instInv_ptr()->add_inst ( uuid, instance );

                        manage_reporting_state ( &instance, state );
                    }
                    if (daemon_get_cfg_ptr()->debug_level )
                        get_instInv_ptr()->print_instances();

                    break ;
                }
                case MTC_CMD_MOD_HOST:
                {
                    guestInstClass * obj_ptr = get_instInv_ptr() ;
                    string reporting_state = "" ;
                    rc = jsonUtil_get_key_val ( buf_ptr, "heartbeat", reporting_state ) ;
                    if ( rc != PASS)
                    {
                        elog ("failed to extract heartbeat reporting state (rc=%d)\n", rc );
                        wlog ("... disabling 'heartbeat' fault reporting due to error\n");
                        obj_ptr->reporting = false ;
                        rc = FAIL_JSON_PARSE ;
                    }
                    else if ( !reporting_state.compare("enabled") )
                    {
                        ilog ("Enabling host level 'heartbeat' fault reporting\n");
                        obj_ptr->reporting = true ;
                    }
                    else
                    {
                        ilog ("Disabling host level 'heartbeat' fault reporting\n");
                        obj_ptr->reporting = false ;
                    }
                    break ;
                }
                default:
                {
                    elog ("unsupported command (%x)\n", cmd );
                }
            }
        }
    }
    return (rc);
}

/****************************************************************************
 *
 * Name       : send_to_guestAgent
 *
 * Purpose    : Send a command and buffer to the guestAgent
 *
 * Description: If the guestAgent IP is not known the message is dropped
 *              and a retry is returned. Otherwise the supplied message is
 *              sent to the guestAgent running on the controller.
 *
 * **************************************************************************/
int send_to_guestAgent ( unsigned int cmd, const char * buf_ptr )
{
    int bytes = 0;

    ctrl_type * ctrl_ptr = get_ctrl_ptr () ;

    int rc = PASS ;
    mtc_message_type mtc_cmd ;
    memset (&mtc_cmd,0,sizeof(mtc_message_type));    

    memcpy ( &mtc_cmd.buf[0], buf_ptr, strlen(buf_ptr));
    bytes = sizeof(mtc_message_type) ;

    if ( ctrl_ptr->address_peer.empty())
    {
        mlog2 ("controller address unknown ; dropping message (%x:%s)", cmd , buf_ptr );
        return RETRY ;
    }

    mlog1 ("Sending: %s:%d Cmd:%x:%s\n", ctrl_ptr->address_peer.c_str(), ctrl_ptr->sock.agent_rx_port, cmd, buf_ptr );

    mtc_cmd.cmd = cmd ;

    /* rc = message size */                                
    rc = ctrl_ptr->sock.server_tx_sock->write((char *)&mtc_cmd, bytes,ctrl_ptr->address_peer.c_str());

    if ( 0 > rc )
    {
        elog("failed to send (%d:%m)\n", errno );
        rc = FAIL_SOCKET_SENDTO ;
    }
    else
    {
        mlog1 ("Transmit to %14s port %d\n",
                ctrl_ptr->address_peer.c_str(),
                ctrl_ptr->sock.server_tx_sock->get_dst_addr()->getPort());
        print_mtc_message ( &mtc_cmd );
        rc = PASS ;    
    }

    return (rc);
}

/*********************************************************************************
 *
 * Name   : write_inst   (guestInstClass::public)
 *
 * Purpose: Send a message to the specified VM instance.
 *
 *********************************************************************************/
ssize_t guestInstClass::write_inst ( instInfo * instInfo_ptr, 
                                     const char * message,
                                     size_t   size)
{
    string name = log_prefix(instInfo_ptr);
  
    errno = 0 ;
    size_t len = write ( instInfo_ptr->chan_fd, message, size );
    if ( len != size )
    {
        if ( errno )
        {
            wlog_throttled ( instInfo_ptr->failure_count, 100, 
                             "%s failed to send '%s' (seq:%x) (%d:%m)\n", name.c_str(),
                             instInfo_ptr->msg_type.c_str(),
                             instInfo_ptr->sequence, errno );

            if ( errno == EPIPE )
            {
                instInfo_ptr->connected = false ;
                        
                instInfo_ptr->connect_wait_in_secs = DEFAULT_CONNECT_WAIT ;
                get_instInv_ptr()->reconnect_start ( instInfo_ptr->uuid.data() );
            }

            len = 0 ;
        }
        else
        {
            wlog_throttled ( instInfo_ptr->failure_count, 100, 
                             "%s send '%s' (seq:%x) (len:%ld)\n", name.c_str(),
                             instInfo_ptr->msg_type.c_str(),
                             instInfo_ptr->sequence, len);
        }
    }
    else
    {
        instInfo_ptr->failure_count = 0 ;
        mlog("%s send '%s' (seq:%x)\n", name.c_str(),
             instInfo_ptr->msg_type.c_str(),
             instInfo_ptr->sequence );
    }
    return (len);
}


/*********************************************************************************
 *
 * Name       : readInst   (guestInstClass::private)
 *
 * Purpose    : try to receive a single message from all instances.
 *
 * Description: Each received message is enqueued into the associated
 *              instance's message queue.
 *
 *********************************************************************************/

int fail_count = 0 ;
void guestInstClass::readInst ( void )
{
    int rc ;
    std::list<int> socks ;
   
    waitd.tv_sec  = 0;
    waitd.tv_usec = GUEST_SOCKET_TO;

    struct json_object *jobj_msg = NULL;

    /* Initialize the master fd_set */
    FD_ZERO(&instance_readfds);

    socks.clear();

    for ( struct inst * inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
    {
        if ( inst_ptr->instance.connected )
        {
            socks.push_front( inst_ptr->instance.chan_fd );
            FD_SET(inst_ptr->instance.chan_fd, &instance_readfds);
        }
        if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
            break ;
    }

    /* if there are no connected instance channels then exit */
    if ( socks.empty() )
    {
        return ;
    }

    /* Call select() and wait only up to SOCKET_WAIT */
    socks.sort();
    rc = select( socks.back()+1, &instance_readfds, NULL, NULL, &waitd);

    if (( rc <= 0 ) || ( rc > (int)socks.size()))
    {
        /* Check to see if the select call failed. */
        if ( rc > (int)socks.size())
        {
            wlog_throttled ( fail_count, 100, "select return exceeds current file descriptors (%ld:%d)\n",
                             socks.size(), rc );
        }
        /* ... but filter Interrupt signal         */
        else if (( rc < 0 ) && ( errno != EINTR ))
        {
            wlog_throttled ( fail_count, 100, "socket select failed (%d:%m)\n", errno);
        }
        else
        {
            mlog3 ("nothing received from %ld instances; socket timeout (%d:%m)\n", socks.size(), errno );
        }
    }
    else
    {
        fail_count = 0 ;
        mlog2 ("trying to receive for %ld instances\n", socks.size());

        /* Search through all the instances for watched channels */
        for ( struct inst * inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
        {
            mlog1 ("%s monitoring %d\n", inst_ptr->instance.inst.c_str(),
                                         inst_ptr->instance.chan_fd );

            /* Service guestServer messages towards the local IP */
            if (FD_ISSET(inst_ptr->instance.chan_fd, &instance_readfds) ) 
            {
                bool   message_present ;
                int    count ;
                string last_message_type ;
                char   vm_message[GUEST_HEARTBEAT_MSG_MAX_MSG_SIZE] ;
                string name ;

                if( inst_ptr->instance.inst.empty() )
                    name = inst_ptr->instance.uuid ;
                else
                    name = inst_ptr->instance.inst ;

                count = 0 ;
                last_message_type = GUEST_HEARTBEAT_MSG_INIT_ACK ;
                
                do 
                {
                    message_present = false ;
                    rc = read ( inst_ptr->instance.chan_fd, vm_message, GUEST_HEARTBEAT_MSG_MAX_MSG_SIZE);
                    mlog3 ("%s read channel: bytes:%d, fd:%d\n", name.c_str(), rc,inst_ptr->instance.chan_fd );
                    if ( rc < 0 )
                    {
                        if ( errno == EINTR )
                        {
                             wlog_throttled ( inst_ptr->instance.failure_count, 100, "%s EINTR\n", name.c_str());
                        }
                        else if ( errno == ECONNRESET )
                        {
                            wlog ("%s connection reset ... closing\n", name.c_str());
                    
                            /* Close the connection if we get a 'connection reset by peer' errno */
                            guestUtil_close_channel ( &inst_ptr->instance );
                    
                            /* An element of the list is removed - need to break out */
                        }
                        else if ( errno != EAGAIN )
                        {
                            wlog_throttled ( inst_ptr->instance.failure_count, 100, "%s error (%d:%m)\n", name.c_str(), errno );
                        }
                        else
                        {
                            mlog3 ("%s no more messages\n", name.c_str());
                        }
                        break ;
                    }
                    else if ( rc == 0 )
                    {
                        mlog3 ("%s no message\n" , name.c_str());
                        break ;
                    }
                    else
                    {
                        if ( rc < GUEST_HEARTBEAT_MSG_MIN_MSG_SIZE )
                        {
                            wlog_throttled ( inst_ptr->instance.failure_count, 100, 
                                      "%s message size %d is smaller than minimal %d; dropping\n",
                                      name.c_str(), rc, GUEST_HEARTBEAT_MSG_MIN_MSG_SIZE);
                        }
                        else if ( inst_ptr->message_list.size() > MAX_MESSAGES )
                        {
                            wlog_throttled ( inst_ptr->instance.failure_count, 100, 
                                      "%s message queue overflow (max:%d) ; dropping\n", 
                                      name.c_str(), MAX_MESSAGES );
                        }
                        else
                        {
                            inst_ptr->instance.failure_count = 0 ;
                            jobj_msg = json_tokener_parse(vm_message);
                            int version;
                            string msg_type;
                            string log_err = "failed to parse ";
                            guestInstClass * obj_ptr = get_instInv_ptr();

                            //parse incoming msg
                            if (jobj_msg == NULL)
                            {
                                wlog("failed to parse msg\n");
                                continue;
                            }

                            if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_VERSION, &version) != PASS)
                            {
                                // fail to parse the version
                                log_err.append(GUEST_HEARTBEAT_MSG_VERSION);
                                elog("%s\n", log_err.c_str());
                                obj_ptr->send_client_msg_nack(&inst_ptr->instance, log_err);
                                json_object_put(jobj_msg);
                                continue;
                            }

                            if ( version != GUEST_HEARTBEAT_MSG_VERSION_CURRENT)
                            {
                               char log_err_str[100];
                               sprintf(log_err_str, "Bad version: %d, expect version: %d",
                                     version, GUEST_HEARTBEAT_MSG_VERSION_CURRENT);
                               elog("%s\n", log_err_str);
                               log_err = log_err_str;
                               obj_ptr->send_client_msg_nack(&inst_ptr->instance, log_err);
                               json_object_put(jobj_msg);
                               continue;
                            }

                            message_present = true ;
                            if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_MSG_TYPE, &msg_type) != PASS)
                            {
                                // fail to parse the msg_type
                                log_err.append(GUEST_HEARTBEAT_MSG_MSG_TYPE);
                                elog("%s\n", log_err.c_str());
                                obj_ptr->send_client_msg_nack(&inst_ptr->instance, log_err);
                                json_object_put(jobj_msg);
                                continue;
                            }

                            mlog2 ("%s '%s' message\n", name.c_str(), msg_type.c_str());
                   
                            /* Try and purge out old init messages */
                            if (!msg_type.compare(GUEST_HEARTBEAT_MSG_INIT) &&
                                !msg_type.compare(last_message_type) )
                            {
                                inst_ptr->message_list.pop_back();
                                ilog ("%s deleting stale init message\n", name.c_str());
                            }
                            /* Enqueue the message to its instance message list */
                            inst_ptr->message_list.push_back(jobj_msg);
                            last_message_type = msg_type ;
                        }
                    }
                } while ( ( message_present == true ) && ( ++count<10 ) ) ;
            }
            if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                break ;
        }
    }
}
