/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance Daemon
  */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "svr"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>          /* for hostent */
#include <iostream>
#include <string>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>          /* for ... close and usleep         */
#include <evhttp.h>          /* for ... HTTP_ status definitions */
#include <linux/rtnetlink.h> /* for ... RTMGRP_LINK              */

using namespace std;

#include "daemon_common.h" /* */

#include "nodeBase.h"      /* Service header */
#include "nodeTimers.h"    /* */
#include "nodeClass.h"     /* */
#include "nodeUtil.h"      /* */
#include "jsonUtil.h"      /* */
#include "mtcHttpSvr.h"
#include "mtcNodeMsg.h"    /* for ... send_mtc_cmd               */
#include "mtcAlarm.h"      /* for ... mtcAlarm_log               */

#define EVENT_SERVER "HTTP Event Server"

#define CLIENT_SYSINV_URL      "/v1/hosts/"
#define CLIENT_VIM_HOSTS_URL    "/v1/hosts/"
#define CLIENT_SM_URL           "/v1/hosts/"
#define CLIENT_VIM_SYSTEMS_URL  "/v1/systems"
#define CLIENT_HEADER          "User-Agent"

#define CLIENT_SYSINV_1_0      "sysinv/1.0"
#define CLIENT_VIM_1_0            "vim/1.0"
#define CLIENT_SM_1_0		   "sm/1.0"

#define EVENT_METHODS (EVHTTP_REQ_PATCH | \
                       EVHTTP_REQ_POST  | \
                       EVHTTP_REQ_GET   | \
                       EVHTTP_REQ_PUT   | \
                       EVHTTP_REQ_DELETE)

int  sequence = 0 ;
char log_str [MAX_API_LOG_LEN];
char filename[MAX_FILENAME_LEN];


/* Cleanup */
void mtcHttpSvr_fini ( event_type & mtce_event )
{
    if ( mtce_event.fd )
    {
        if ( mtce_event.base )
        {
            event_base_free( mtce_event.base);
        }
        close ( mtce_event.fd );
        mtce_event.fd = 0 ;
    }
}


/************************************************************************************
 *
 * event_base_loopcontinue is not supported until version 2.1.2-alpha 
 * It allows processing of events in main loop instead of in the handler.
 * Theoretically this would be nice to use in conjunction with
 * event_base_loopexit in the selected fd
void mtcHttpSvr_work ( event_type & mtce_event  )
{
    if ( mtce_event.base )
    {
        int rc = event_base_loopcontinue ( mtce_event.base ) ; // EVLOOP_NONBLOCK );
        if ( rc )
        {
            ilog ("HTTP event_base_loopcontinue rc:%d\n", rc );
        }
    }
}
**************************************************************************************/

/* Look for events */
void mtcHttpSvr_look ( event_type & mtce_event  )
{
    /* Look for Events */
    if ( mtce_event.base )
    {
        // rc = event_base_loopexit( mtce_event.base, NULL ) ; // EVLOOP_NONBLOCK );
        event_base_loop( mtce_event.base, EVLOOP_NONBLOCK );
    }
}

void _create_error_response ( int rc , string & resp_buffer , node_inv_type & inv )
{
    resp_buffer = "{" ;
    resp_buffer.append (" \"status\" : \"fail\"");
    if ( rc == FAIL_UNIT_ACTIVE )
    {
        resp_buffer.append (",\"reason\" : \"Controller is Active\"");
        resp_buffer.append (",\"action\" : \"Swact Controller and then Lock\"");
    }
    else if ( rc == FAIL_LOW_STORAGE )
    {
        resp_buffer.append (",\"reason\" : \"Storage redundancy check\"");
        resp_buffer.append (",\"action\" : \"Enable another storage host\"");
    }
    else if ( rc == FAIL_PATCH_INPROGRESS )
    {
        resp_buffer.append (",\"reason\" : \"Operation not permitted while software patching is 'In-Progress'\"");
        resp_buffer.append (",\"action\" : \"Wait for patching to complete and then retry operation\"");
    }
    else if ( rc == FAIL_PATCHED_NOREBOOT )
    {
        resp_buffer.append (",\"reason\" : \"Patches have been applied but not loaded on target host'\"");
        resp_buffer.append (",\"action\" : \"Please 'lock' then 'unlock' host and retry operation\"");
    }
    else if ( rc == FAIL_NEED_STORAGE_MON )
    {
        resp_buffer.append (",\"reason\" : \"Failed Backend Monitor Quorum check\"");
        resp_buffer.append (",\"action\" : \"Enable second controller or additional storage host\"");
    }
    else if ( rc == FAIL_NEED_DUPLEX )
    {
        resp_buffer.append (",\"reason\" : \"Controller redundancy check\"");
        resp_buffer.append (",\"action\" : \"Enable second controller\"");
    }
    else if ( rc == FAIL_DEL_UNLOCKED )
    {
        resp_buffer.append (",\"reason\" : \"Host is Unlocked\"");
        resp_buffer.append (",\"action\" : \"Lock Host and then Delete\"");
    }
    else if ( rc == FAIL_ADMIN_ACTION )
    {
        resp_buffer.append (",\"reason\" : \"Unknown admin action\"");
        resp_buffer.append (",\"action\" : \"Check admin action\"");
    }
    else if ( rc == FAIL_NODETYPE )
    {
        resp_buffer.append (",\"reason\" : \"Swact not supported for this Host\"");
        resp_buffer.append (",\"action\" : \"Re-evaluate selected operation\"");
    }
    else if ( rc == FAIL_SWACT_NOINSVMATE )
    {
        resp_buffer.append (",\"reason\" : \"No unlocked-enabled controller available to switch activity to\"");
        resp_buffer.append (",\"action\" : \"Enable second controller and then retry\"");
    }
    else if ( rc == FAIL_OPER_INPROGRESS )
    {
        resp_buffer.append (",\"reason\" : \"User operation on this host already in-progress\"");
        resp_buffer.append (",\"action\" : \"Wait a moment and then retry\"");
     }
    else if ( rc == FAIL_SWACT_INPROGRESS )
    {
        resp_buffer.append (",\"reason\" : \"Swact operation on this host already in-progress\"");
        resp_buffer.append (",\"action\" : \"Wait for current operation to complete and then retry\"");
    }
    else if ( rc == FAIL_JSON_PARSE )
    {
        resp_buffer.append (",\"reason\" : \"Mtce cannot parse key:values from Inventory request\"");
        resp_buffer.append (",\"action\" : \"Retry operation or contact next level support\"");
    }
    else if ( rc == FAIL_RESET_POWEROFF )
    {
        resp_buffer.append (",\"reason\" : \"Cannot reset a powered off host\"");
        resp_buffer.append (",\"action\" : \"Power-on host and then retry\"");
    }
    else if ( rc == FAIL_NO_IP_SUPPORT )
    {
        resp_buffer.append (",\"reason\" : \"Warning: The board management IP address is not provisioned or learned.\"");
        resp_buffer.append (",\"action\" : \"Make sure the board management controller is powered on, connected to the ");
        resp_buffer.append ("board management network and the provisioned MAC address is correct. Board management actions ");
        resp_buffer.append ("such as 'reset' or 'power-on' or 'power-off' will not work until the ");
        resp_buffer.append ("the host's board management IP is learned.\"");
    }
    else if ( rc == FAIL_DUP_HOSTNAME )
    {
        resp_buffer.append (",\"reason\" : \"Rejecting host-edit with duplicate hostname\"");
        resp_buffer.append (",\"action\" : \"Delete host with hostname '");
        resp_buffer.append (inv.name.data());
        resp_buffer.append ("' first and then retry or use different hostname\"");
    }
    else if ( rc == FAIL_DUP_IPADDR )
    {
        resp_buffer.append (",\"reason\" : \"Rejecting host-edit with duplicate ip address\"");
        resp_buffer.append (",\"action\" : \"Delete host with ip address '");
        resp_buffer.append (inv.ip.data());
        resp_buffer.append ("' first and then retry or contact system administrator\"");
    }
    else if ( rc == FAIL_DUP_MACADDR )
    {
        resp_buffer.append (",\"reason\" : \"Rejecting host-edit with duplicate mac address\"");
        resp_buffer.append (",\"action\" : \"Delete host with mac address '");
        resp_buffer.append (inv.mac.data());
        resp_buffer.append ("' first and then retry or contact system administrator\"");
    }
    else if (( rc == FAIL_POWER_CONTROL ) ||
             ( rc == FAIL_RESET_CONTROL ))
    {
        resp_buffer.append (",\"reason\" : \"The board management controller for this host is not configured\"");
        resp_buffer.append (",\"action\" : \"Edit host to configure board management and then retry\"");
    }

    else if  (( rc == FAIL_RESERVED_NAME ) &&
         ((( !inv.name.compare ("controller-0")) && ( inv.type.compare("controller"))) ||
          (( !inv.name.compare ("controller-1")) && ( inv.type.compare("controller")))))
    {
        resp_buffer.append (",\"reason\" : \"Can only add reserved '");
        resp_buffer.append (inv.name.data());
        resp_buffer.append ("' hostname with personality set to 'controller'\"");
        resp_buffer.append (",\"action\" : \"Retry operation with personality set to 'controller'\"");
    }
    else if (( rc == FAIL_RESERVED_NAME ) &&
            (( !inv.name.compare ("storage-0")) && ( inv.type.compare("storage"))))
    {
        resp_buffer.append (",\"reason\" : \"Can only add reserved '");
        resp_buffer.append (inv.name.data());
        resp_buffer.append ("' hostname with personality set to 'storage'\"");
        resp_buffer.append (",\"action\" : \"Retry operation with personality set to 'storage'\"");
    }
    else if ( rc == FAIL_NOT_ACCESSIBLE )
    {
        resp_buffer.append (",\"reason\" : \"Maintenance has not yet established communication with the board management controller for this host\"");
        resp_buffer.append (",\"action\" : \"Verify board management configuration settings and then retry\"");
    }
    else if ( rc == FAIL_NOT_CONNECTED )
    {
        resp_buffer.append (",\"reason\" : \"Maintenance does not have an established connection to the board management controller for this host\"");
        resp_buffer.append (",\"action\" : \"Verify board management configuration settings and then retry. ");
        resp_buffer.append ("Note: Maintenance is continuously trying to maintain an established connection using the supplied provisioning and credentials\"");
    }
    else if ( rc == FAIL_BM_PROVISION_ERR )
    {
        resp_buffer.append (",\"reason\" : \"Request rejected due to provisioning semantic check. \"");
        resp_buffer.append (",\"action\" : \"Please verify that the board management MAC or IP address being used is ");
        resp_buffer.append ("formatted correctly or not already provisioned against another host\"");
    }
    else
    {
        resp_buffer.append (",\"reason\" : \"Unknown\"");
        resp_buffer.append (",\"action\" : \"Undetermined\"");
        wlog ("%s no supported reason/action string for error code %d\n", inv.name.c_str(), rc);
    }
    resp_buffer.append ("}");
}

/*****************************************************************************
 *
 * Name:      mtcHttpSvr_vim_req
 *
 * Handles three 'operations'
 *
 *  'delete' - based on uuid
 *  'modify' - based on list of key - value pairs
 *  'add'    - based on inventory record
 *
 ******************************************************************************/
/* Test Commands:
 *
 * Test 1: Select host, get uuid and make sure it is unlocked-enabled. 
 * Verify: Host should fail, reset and auto re-enable.
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2112/v1/hosts/8b216803-c47c-40b3-bf61-ed84ff83754e -d '{"uuid":"8b216803-c47c-40b3-bf61-ed84ff83754e", "hostname": "compute-1", "severity": "failed"}'

 * Test 2: Lock Host and issue command with correct uuids and hostname. 
 * Verify: The host is rebooted/reset
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2112/v1/hosts/8b216803-c47c-40b3-bf61-ed84ff83754e -d '{"uuid":"8b216803-c47c-40b3-bf61-ed84ff83754e", "hostname": "compute-1", "severity": "failed"}'

 * Test 3: 
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2112/v1/hosts/8b216803-c47c-40b3-bf61-ed84ff83754e -d '{"uuid":"8b216803-c47c-40b3-bf61-ed84ff83754e", "hostname": "compute-1", "severity": "degraded"}'

 * Test 4: 
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2112/v1/hosts/8b216803-c47c-40b3-bf61-ed84ff83754e -d '{"uuid":"8b216803-c47c-40b3-bf61-ed84ff83754e", "hostname": "compute-1", "severity": "cleared"}'

 * Test 5: Unsuppored VIM Command
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2112/v1/hosts/8b216803-c47c-40b3-bf61-ed84ff83754e -d '{"uuid":"8b216803-c47c-40b3-bf61-ed84ff83754e", "hostname": "compute-1", "severity": "degradeded"}'
*/

string mtcHttpSvr_vim_req ( char          * buffer_ptr, 
                            evhttp_cmd_type http_cmd, 
                            int           & http_status_code )
{
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    string response = "" ;
    string severity = "" ;
    string hostname = "" ;

    int rc1 = jsonUtil_get_key_val ( buffer_ptr, MTC_JSON_SEVERITY, severity );
    int rc2 = jsonUtil_get_key_val ( buffer_ptr, MTC_JSON_INV_NAME, hostname );

    jlog ("%s '%s' request\n", hostname.c_str(), getHttpCmdType_str(http_cmd)); 
    if ( rc1 | rc2 )
    {
        wlog ("Failed to parse command key values (%d:%d)\n", rc1, rc2);
        response = "{" ;
        response.append (" \"status\" : \"fail\"");
        response.append (",\"reason\" : \"command parse error\"");
        response.append (",\"action\" : \"retry command or contact next level support\"");
        response.append ("}");
        http_status_code = HTTP_BADREQUEST ;
    }
    else
    {
        if ( ! severity.compare("failed" ))
        {
            if ( obj_ptr->get_adminState ( hostname ) == MTC_ADMIN_STATE__LOCKED )
            {
                /* Test 2 */
                ilog ("%s reboot/reset due to failed event (host is locked)\n", hostname.c_str());
                obj_ptr->set_rebootStage ( hostname , MTC_RESETPROG__START );
                obj_ptr->set_adminAction ( hostname , MTC_ADMIN_ACTION__REBOOT );
            }
            else
            {
                /* Test 1 */
                ilog ("%s is now failed due to failed event (host is unlocked)\n", hostname.c_str());
                obj_ptr->mtcInvApi_update_states ( hostname, 
                                                   get_adminState_str (MTC_ADMIN_STATE__UNLOCKED),
                                                   get_operState_str  (MTC_OPER_STATE__DISABLED ),
                                                   get_availStatus_str(MTC_AVAIL_STATUS__FAILED));
            }
            response = "{ \"status\" : \"pass\" }" ;
            http_status_code = HTTP_OK ;
        }
        else if ( ! severity.compare("degraded"))
        {
            /* Test 3 */
            ilog ("%s severity 'degraded' request from not supported\n", hostname.c_str() );
            response.append ("{ \"status\" : \"fail\"");
            response.append (",\"reason\" : \"Controlled host degrade not supported\"");
            response.append (",\"action\" : \"Upgrade maintenance package containing support and retry\"");
            response.append ("}");
            http_status_code = HTTP_BADMETHOD;
        }
        else if ( ! severity.compare("cleared"))
        {                
            /* Test 4 */
            ilog ("%s severity 'cleared' request not supported\n", hostname.c_str() );
            response.append ("{\"status\" : \"fail\"");
            response.append (",\"reason\" : \"Controlled host degrade clear not supported\"");
            response.append (",\"action\" : \"Upgrade maintenance package containing support and retry\"");
            response.append ("}");
            http_status_code = HTTP_BADMETHOD;
        }
        else
        {
            /* Test 5 */
            ilog ("%s severity '%s' request from not supported\n", hostname.c_str(), severity.c_str());
            response.append ("{\"status\" : \"fail\"");
            response.append (",\"reason\" : \"Unsupported severity request '");
            response.append (severity);
            response.append ("' ,\"action\" : \"Upgrade maintenance package containing support and retry\"");
            response.append ("}");
            http_status_code = HTTP_BADREQUEST;
        }
    }
    return (response);
}


/*****************************************************************************
 *
 * Name:      mtcHttpSvr_inv_req
 *
 * Handles three 'operations'
 *
 *  'delete' - based on uuid
 *  'modify' - based on list of key - value pairs
 *  'add'    - based on inventory record
 *
 ******************************************************************************/

string mtcHttpSvr_inv_req ( char          * request_ptr, 
                            evhttp_cmd_type event_type, 
                            int           & http_status_code )
{
    int rc = PASS ;

    nodeLinkClass   * obj_ptr  = get_mtcInv_ptr ();
    msgSock_type * mtclogd_ptr = get_mtclogd_sockPtr ();

    event_type = event_type ;

    /* variable scoping */
    string resp_buffer = ""   ;
    string key         = "operation" ;
    string value       = ""    ;
    string hostname    = "n/a" ;

    /* Identify the operation */
    rc = jsonUtil_get_key_val ( request_ptr, key, value ) ;
    if ( rc == PASS )
    {
        node_inv_type  inv ;
        node_inv_init (inv);
        dlog ("%s %s : '%s'\n", obj_ptr->my_hostname.c_str(), key.c_str(), value.c_str()) ;

        rc = jsonUtil_load_host ( request_ptr, inv );
        if ( rc == PASS )
        {
            if ( !inv.name.empty() )
            {
                hostname = inv.name ;
            }

            snprintf (&log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] http event seq: %d Payload:%s: %s", 
                       pt(), getpid(), sequence, hostname.data(), request_ptr);
            send_log_message ( mtclogd_ptr, obj_ptr->my_hostname.data(), &filename[0],  &log_str[0] );

            /* ADD */
            if ( ! strncmp ( value.data() , "add" , strlen("add") ))
            {
                rc = obj_ptr->add_host ( inv );
                if ( rc == PASS )
                {
                    ilog ("%s Add Operation\n", inv.name.c_str());
    
                    /* generate event=add alarm if the add_host returns a PASS */
                    mtcAlarm_log ( inv.name, MTC_LOG_ID__EVENT_ADD );
                }

                /* A RETRY return from add_host indicates that the node is 
                 * already provisioned. At this point changes can only be 
                 * implemented as modification so call mod_host 
                 */
                if ( rc == RETRY )
                {
                    ilog ("%s Modify Operation\n", inv.name.c_str());
                    rc = obj_ptr->mod_host ( inv );
                }

                /* handle the http response code/message */
                if ( rc == PASS )
                {
                    resp_buffer = "{ \"status\" : \"pass\" }" ;
                }
                else
                {
                    elog ("%s Inventory Add failed (%s)\n", 
                              inv.name.length() ? inv.name.c_str() : "none", 
                              inv.uuid.c_str() );
                    _create_error_response ( rc , resp_buffer, inv ) ;
                }
            }

            /* MODIFY ? */
            else if ( ! strncmp ( value.data() , "modify" , strlen("modify") ))
            {
                ilog ("%s Modify Operation\n", inv.name.c_str());

                /* If the return value of get_host is empty then we need to add the host */
                if ( obj_ptr->get_host ( inv.uuid ).empty() )
                {
                    wlog ("%s Missing\n", inv.uuid.c_str() );
                    ilog ("%s Overriding 'modify' with 'add' operation\n", inv.name.c_str() );
                    rc = obj_ptr->add_host ( inv );
                    if ( rc == PASS )
                    {
                        resp_buffer = "{ \"status\" : \"pass\" }" ;
                        http_status_code = HTTP_OK ;
                    }
                    else
                    {
                        elog ("Inventory Add failed for uuid: %s\n", inv.uuid.c_str());
                        resp_buffer = "{" ;
                        resp_buffer.append (" \"status\" : \"fail\"");
                        resp_buffer.append (",\"reason\" : \"Rejected - unknown\"");
                        resp_buffer.append (",\"action\" : \"Switch activity\"");
                        resp_buffer.append ("}");
                    }
                }
                else
                {
                    rc = obj_ptr->mod_host ( inv );
                    if ( rc != PASS )
                    {
                        elog ("Inventory Modify failed for uuid: %s\n", inv.uuid.c_str());
                        _create_error_response ( rc , resp_buffer, inv ) ;
                    }
                    else
                    {
                        resp_buffer = "{ \"status\" : \"pass\" }" ;
                        http_status_code = HTTP_OK ;
                    }
                }
            }
            else
            {
                elog ("Unsupported Inventory Event Operation:%s\n", value.data());
                resp_buffer = "{" ;
                resp_buffer.append (" \"status\" : \"fail\"");
                resp_buffer.append (",\"reason\" : \"Unsupported ");
                resp_buffer.append (value.data());
                resp_buffer.append (" operation\"");
                resp_buffer.append (",\"action\" : \"Use delete, add or modify only\"");
                resp_buffer.append ("}");
                http_status_code = HTTP_BADREQUEST ;
            }
        }
        else
        {
            elog ("JSON key:value parse error: %s\n", request_ptr );
            _create_error_response ( FAIL_JSON_PARSE , resp_buffer, inv ) ;
            http_status_code = HTTP_BADREQUEST ;
        }
    }
    else
    {
        elog ("Unable to get key value\n");
        resp_buffer = "{" ;
        resp_buffer.append (" \"status\" : \"fail\"");
        resp_buffer.append (",\"reason\" : \"String deserialization\"");
        resp_buffer.append (",\"action\" : \"Fix event dictionary\"");
        resp_buffer.append ("}");
        http_status_code = HTTP_BADREQUEST ;
    }
    return resp_buffer ;
}


/*****************************************************************************
 *
 * Name:      mtcHttpSvr_sm_req
 *
 * Handles only 1 'operation'
 *
 *  'event'    - based on hostname, to set host state
 *
 ******************************************************************************/

string mtcHttpSvr_sm_req ( char          * request_ptr,
                            evhttp_cmd_type event_type,
                            int           & http_status_code )
{
    int rc = PASS ;
    http_status_code = HTTP_BADREQUEST ;

    nodeLinkClass   * obj_ptr  = get_mtcInv_ptr ();
    msgSock_type * mtclogd_ptr = get_mtclogd_sockPtr ();

    event_type = event_type ;

    /* variable scoping */
    string resp_buffer = ""   ;
    string key         = "action" ;
    string value       = ""    ;
    string hostname    = "n/a" ;

    /* Identify the operation */
    rc = jsonUtil_get_key_val ( request_ptr, key, value ) ;
    if ( rc == PASS )
    {
        node_inv_type  inv ;
        node_inv_init (inv);

        ilog ("%s %s : '%s'\n", obj_ptr->my_hostname.c_str(), key.c_str(), value.c_str()) ;

        rc = jsonUtil_load_host_state ( request_ptr, inv );
        if ( rc == PASS )
        {
            if ( !inv.name.empty() )
            {
                hostname = inv.name ;
            }

            snprintf (&log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] http event seq: %d Payload:%s: %s",
                       pt(), getpid(), sequence, hostname.data(), request_ptr);
            send_log_message ( mtclogd_ptr, obj_ptr->my_hostname.data(), &filename[0],  &log_str[0] );

            /* state change event */
            if ( !value.compare("event") )
            {
                ilog ("%s state change\n", inv.name.c_str());

                if ( obj_ptr->get_host (inv.name).empty() )
                {
                    string reason_text = "hostname not provided";
                    if( !inv.name.empty() )
                    {
                        reason_text = "host " + inv.name + " not found";
                    }
                    wlog ("%s\n", reason_text.c_str());

                    resp_buffer = "{";
                    resp_buffer.append (" \"status\" : \"fail\"");
                    resp_buffer.append (",\"reason\" : \"" + reason_text + "\"");
                    resp_buffer.append (",\"action\" : \"event\"");
                    resp_buffer.append ("}");
                    http_status_code = HTTP_OK ;
                }
                else
                {
                    bool executed = false;
                    if( (inv.avail.compare("failed") == 0) && (inv.oper.compare("disabled")==0) )
                    {
                        rc = obj_ptr->set_host_failed ( inv );
                        executed = true;
                    }

                    if (!executed)
                    {
                        resp_buffer = "{" ;
                        resp_buffer.append (" \"status\" : \"fail\"");
                        resp_buffer.append (",\"reason\" : \"Rejected - operation not supported\"");
                        resp_buffer.append (",\"action\" : \"event\"");
                        resp_buffer.append ("}");
                    }else
                    {
                        if ( rc != PASS )
                        {
                            char errcode[12];
                            snprintf(errcode, sizeof(errcode), "%d", rc);
                            resp_buffer = "{" ;
                            resp_buffer.append (" \"status\" : \"fail\"");
                            resp_buffer.append (",\"reason\" : \"Rejected - ");
                            resp_buffer.append ( errcode );
                            resp_buffer.append ("\"");
                            resp_buffer.append (",\"action\" : \"event\"");
                            resp_buffer.append ("}");
                        }
                        else
                        {
                            resp_buffer = "{ \"status\" : \"pass\" }" ;
                            http_status_code = HTTP_OK ;
                        }
                    }
                }
            }
            else
            {
                elog ("Unsupported Inventory Event Operation:%s\n", value.data());
                resp_buffer = "{" ;
                resp_buffer.append (" \"status\" : \"fail\"");
                resp_buffer.append (",\"reason\" : \"Unsupported ");
                resp_buffer.append (value.data());
                resp_buffer.append (" operation\"");
                resp_buffer.append (",\"action\" : \"Use event only\"");
                resp_buffer.append ("}");
                http_status_code = HTTP_BADREQUEST ;
            }
        }
        else
        {
            elog ("JSON key:value parse error: %s\n", request_ptr );
            _create_error_response ( FAIL_JSON_PARSE , resp_buffer, inv ) ;
            http_status_code = HTTP_BADREQUEST ;
        }
    }
    else
    {
        elog ("Unable to get key value\n");
        resp_buffer = "{" ;
        resp_buffer.append (" \"status\" : \"fail\"");
        resp_buffer.append (",\"reason\" : \"String deserialization\"");
        resp_buffer.append (",\"action\" : \"Fix event dictionary\"");
        resp_buffer.append ("}");
        http_status_code = HTTP_BADREQUEST ;
    }
    return resp_buffer ;
}
/********************************************************************
 *
 * Verify this request contains valid client info.
 *
 * 1. the URL must have 
 *        CLIENT_SYSINV_URL or 
 *        CLIENT_VIM_HOSTS_URL or
 *        CLIENT_VIM_SYSTEMS_URL
 *
 * 2. the user-Agent header needs to exist and be set to either
 *        CLIENT_SYSINV_1_0 or
 *        CLIENT_VIM_1_0
 *
 ********************************************************************/
mtc_client_enum _get_client_id ( struct evhttp_request *req )
{
    mtc_client_enum client     = CLIENT_NONE ;

    /* Parse Headers we care about to verify that it also contains the
     * correct User-Agent header and supported version */
    struct evkeyvalq * headers_ptr = evhttp_request_get_input_headers (req);
    const char * header_value_ptr  = evhttp_find_header (headers_ptr, CLIENT_HEADER);
    if ( header_value_ptr ) 
    {
        const char * url_ptr = evhttp_request_get_uri (req);
    
        hlog2 ("URI: %s\n", url_ptr );

        if ( ! strncmp ( header_value_ptr, CLIENT_SYSINV_1_0, 20 ) )
        {
            hlog3 ("%s\n", header_value_ptr );
            
            if ( strstr ( url_ptr, CLIENT_SYSINV_URL) )
            {
               client = CLIENT_SYSINV ;
            }
        }
        else if ( ! strncmp ( header_value_ptr, CLIENT_VIM_1_0, 20 ) )
        {
            hlog3 ("%s\n", header_value_ptr );
              
            if ( strstr ( url_ptr, CLIENT_VIM_HOSTS_URL))
            {
                client = CLIENT_VIM_HOSTS ;
            }
            else if ( strstr ( url_ptr, CLIENT_VIM_SYSTEMS_URL) )
            {
                client = CLIENT_VIM_SYSTEMS ;
            }
        }
        else if ( ! strncmp ( header_value_ptr, CLIENT_SM_1_0, 20 ) )
        {
            hlog3 ("%s\n", header_value_ptr);
            if ( strstr ( url_ptr, CLIENT_SM_URL ) )
            {
                client = CLIENT_SM;
            }
        }
    }
    else
    {
        wlog ("Unknown or mismatched client (%d)\n", client) ;
    }
    return (client);
}

/*****************************************************************************
 *
 * Name:        mtcHttpSvr_handler
 *
 * Description: Receive an http event extract the event type and buffer from
 *              it and call process request handler.
 *              Send the processed message response back to the connection.
 *
 * Supported events include: POST, PUT, DELETE
 *
 ******************************************************************************/

void mtcHttpSvr_handler (struct evhttp_request *req, void *arg)
{
    struct evbuffer *resp_buf ;
    mtc_client_enum client = CLIENT_NONE ; 
    int http_status_code = HTTP_NOTFOUND ;
    string service  = "" ;
    string uuid     = "" ;
    string response = "" ;
    string hostname = "n/a" ;

    UNUSED(arg);    

    response = "{" ;
    response.append (" \"status\" : \"fail\"");
    response.append (",\"reason\" : \"not found\"");
    response.append (",\"action\" : \"retry with valid host\"");
    response.append ("}");

    nodeLinkClass   * obj_ptr  = get_mtcInv_ptr ();
    msgSock_type * mtclogd_ptr = get_mtclogd_sockPtr ();
    event_type   *   event_ptr = get_eventPtr ();
    event_ptr->req = req ;

    /* Get sender must be localhost */
    const char * host_ptr = evhttp_request_get_host (req);
    if ( strncmp ( host_ptr , "localhost" , 10 ))
    {
        wlog ("Message received from unknown host (%s)\n", host_ptr );

        /* TODO: Fail the request if from unknown host */
    }

    const char * url_ptr = evhttp_request_get_uri (req);
 
    /* Extract the operation */
    evhttp_cmd_type http_cmd = evhttp_request_get_command (req);
    jlog ("%s request from '%s'\n", getHttpCmdType_str(http_cmd), host_ptr );

    /* Acquire the client that sent this event from the url URI */
    client = _get_client_id ( req );
    if ( client == CLIENT_NONE )
    {
        response = ("{\"status\" : \"fail\"");
        response.append (",\"reason\" : \"unknown client in User-Agent header\"");
        response.append (",\"action\" : \"use ");
        response.append (CLIENT_VIM_1_0);
        response.append (" or ");
        response.append (CLIENT_SYSINV_1_0);
        response.append (" in User-Agent header\"}");
        http_status_code = HTTP_BADREQUEST ;
        elog ("%s\n", response.c_str());
        evhttp_send_error (event_ptr->req, MTC_HTTP_FORBIDDEN, response.data() );
        return ;
    }

    if (( client == CLIENT_VIM_HOSTS ) || 
        ( client == CLIENT_VIM_SYSTEMS ))
    {
        service = "vim" ;
    }
    else if ( client == CLIENT_SYSINV )
    {
        service = "sysinv" ;
    }
    else if ( client == CLIENT_SM )
    {
        service = "sm";
    }
    else
        service = "unknown" ;

    snprintf (&log_str[0], MAX_API_LOG_LEN-1, "\n%s [%5d] http event seq: %d with %s %s request from %s:%s", 
               pt(), getpid(), ++sequence, service.c_str(), getHttpCmdType_str(http_cmd), host_ptr, url_ptr );
    send_log_message ( mtclogd_ptr, obj_ptr->my_hostname.data(), &filename[0], &log_str[0] );

    switch ( http_cmd )
    {
        case EVHTTP_REQ_GET:
        case EVHTTP_REQ_DELETE:
        {
            size_t len = strlen(CLIENT_SYSINV_URL) ;
            uuid = (url_ptr+len) ;
            hostname = obj_ptr->get_host(uuid) ;
            if (( http_cmd == EVHTTP_REQ_GET ) && ( client == CLIENT_VIM_SYSTEMS ))
            {
                http_status_code = obj_ptr->mtcVimApi_system_info ( response );
                break ;
            }
            else
            {
                http_status_code = HTTP_OK ;
                if ( uuid.length() != UUID_LEN )
                {
                    wlog ("http '%s' request rejected, invalid uuid size (%ld:%s)\n", 
                           getHttpCmdType_str(http_cmd),
                           uuid.length(), uuid.c_str());
                    response = "{" ;
                    response.append (" \"status\" : \"fail\"");
                    response.append (",\"reason\" : \"Uuid size error\"");
                    response.append (",\"action\" : \"Undetermined\"");
                    response.append ("}");
                    http_status_code = HTTP_BADREQUEST ;
                }                
                if (( http_cmd == EVHTTP_REQ_DELETE ) &&
                    (( hostname.length() == 0 ) || ( !hostname.compare("none"))))
                {
                    wlog ("deleting unknown resource: %s\n", uuid.length() ? uuid.c_str() : "(null)" );
                    response = "{ \"status\" : \"pass\" }" ;
                    http_status_code = HTTP_OK ;
                }
                else if (( http_cmd == EVHTTP_REQ_GET ) )
                {
                    if ( client == CLIENT_VIM_HOSTS )
                    {
                        response = "{\"state\": " ;
                        response.append(obj_ptr->mtcVimApi_state_get ( hostname, http_status_code ));

                    }
                    else
                    {
                        elog ("http GET request not from VIM (client:%d)\n", client );
                        response = "{" ;
                        response.append (" \"status\" : \"fail\"");
                        response.append (",\"reason\" : \"command not supported for specified User-Agent header\"");
                        response.append (",\"action\" : \"use ");
                        response.append (CLIENT_VIM_1_0);
                        response.append (" as User-Agent\"");
                        response.append ("}");
                        http_status_code = HTTP_BADREQUEST ;
                    }
                }
                else
                {
                    ilog ("%s Delete Request Posted (%s)\n", hostname.c_str(), uuid.c_str());
                    obj_ptr->set_adminAction ( hostname, MTC_ADMIN_ACTION__DELETE );
                    response = "{ \"status\" : \"pass\" }" ;
                    http_status_code = HTTP_OK ;
                }
            }
            break ;
        }
        case EVHTTP_REQ_PATCH:
        case EVHTTP_REQ_POST:
        {
            response = "{" ;
            response.append (" \"status\" : \"fail\"");
            response.append (",\"reason\" : \"no buffer\"");
            response.append (",\"action\" : \"retry with data\"");
            response.append ("}");

            /* get the payload */
            struct evbuffer *in_buf = evhttp_request_get_input_buffer ( req );
            if ( in_buf )
            {
                size_t len = evbuffer_get_length(in_buf) ;
                if ( len )
                {
                    ev_ssize_t bytes = 0 ;
                    char * buffer_ptr = (char*)malloc(len+1);
                    memset ( buffer_ptr, 0, len+1 ); 
                    bytes = evbuffer_remove(in_buf, buffer_ptr, len );
                
                    if ( bytes <= 0 )
                    {
                        http_status_code = HTTP_BADREQUEST ;
                        wlog ("http event request with no payload\n");
                    }
                    else
                    {
                        http_status_code = HTTP_OK ;
                        if ( client == CLIENT_VIM_HOSTS )
                        {
                            response = mtcHttpSvr_vim_req ( buffer_ptr, http_cmd, http_status_code );
                        }
                        else if ( client == CLIENT_SYSINV )
                        {
                            response = mtcHttpSvr_inv_req ( buffer_ptr, http_cmd, http_status_code );
                        }
                        else if ( client == CLIENT_SM )
                        {
                            response = mtcHttpSvr_sm_req ( buffer_ptr, http_cmd, http_status_code );
                        }
                        else
                        {
                            http_status_code = HTTP_BADREQUEST ;
                        }
                    }
                    free ( buffer_ptr );
                }
                else
                {
                     http_status_code = MTC_HTTP_LENGTH_REQUIRED ;
                     wlog ("Http event request has no payload\n");
                }
            }
            else
            {
                http_status_code = HTTP_BADREQUEST ;
                wlog ("Http event request has no buffer\n");
            }
            break ;
        }
        default:
        {
            wlog ("Unknown command (%d)\n", http_cmd );
            http_status_code = HTTP_NOTFOUND ;
        }
    }

    snprintf (&log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] http event seq: %d Response:%s: %s", pt(), getpid(), sequence, hostname.c_str(), response.c_str() );
    send_log_message ( mtclogd_ptr, obj_ptr->my_hostname.data(), &filename[0], &log_str[0] );

    if (( http_status_code == HTTP_OK ) || ( http_status_code == MTC_HTTP_ACCEPTED ))
    {
        resp_buf = evbuffer_new();
        jlog ("Event Response: %s\n", response.c_str());
        evbuffer_add_printf (resp_buf, "%s\n", response.data());
        evhttp_send_reply (event_ptr->req, http_status_code, "OK", resp_buf ); 
        evbuffer_free ( resp_buf );
    }
    else
    {
        elog ("HTTP Event error:%d ; cmd:%s url:%s response:%s\n", 
               http_status_code,
               getHttpCmdType_str(http_cmd), 
               url_ptr,
               response.c_str());
        evhttp_send_error (event_ptr->req, http_status_code, response.data() );
    }
}

/*****************************************************************
 *
 * Name        : mtcHttpSvr_bind
 *
 * Description : Setup the HTTP server socket
 *
 *****************************************************************/
int mtcHttpSvr_bind ( event_type & event )
{
   int rc     ;
   int flags  ;
   int one = 1;
   
   event.fd = socket(AF_INET, SOCK_STREAM, 0);
   if (event.fd < 0)
   {
       elog ("HTTP server socket create failed (%d:%m)\n", errno );
       return FAIL_SOCKET_CREATE ;
   }

   /* make socket reusable */
   rc = setsockopt(event.fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int));
 
   memset(&event.addr, 0, sizeof(struct sockaddr_in));
   event.addr.sin_family = AF_INET;
   event.addr.sin_addr.s_addr = inet_addr(LOOPBACK_IP) ; /* INADDR_ANY; TODO: Refine this if we can */
   // event.addr.sin_addr.s_addr = INADDR_ANY;
   event.addr.sin_port = htons(event.port);
 
   /* bind port */
   rc = bind ( event.fd, (struct sockaddr*)&event.addr, sizeof(struct sockaddr_in));
   if (rc < 0)
   {
       elog ("HTTP server port %d bind failed (%d:%m)\n", event.port, errno );
       return FAIL_SOCKET_BIND ;
   }

   /* Listen for events */
   rc = listen(event.fd, 10 );
   if (rc < 0)
   {
       elog ("HTTP server listen failed (%d:%m)\n", errno );
       return FAIL_SOCKET_LISTEN;
   }

   /* make non-blocking */
   flags = fcntl ( event.fd, F_GETFL, 0) ;
   if ( flags < 0 || fcntl(event.fd, F_SETFL, flags | O_NONBLOCK) < 0)
   {
       elog ("failed to set HTTP server socket to non-blocking (%d:%m)\n", errno );       
       return FAIL_SOCKET_OPTION;
   }

   return PASS;
}

/* Setup the http server */
int mtcHttpSvr_setup ( event_type & event )
{
   int rc = PASS ;
   if ( ( rc = mtcHttpSvr_bind ( event )) != PASS )
   {
       return rc ;
   }
   else if (event.fd < 0)
   {
       wlog ("failed to get http server socket file descriptor\n");
       return RETRY ;
   }

   event.base = event_base_new();
   if (event.base == NULL)
   {
       elog ("failed to get http server event base\n");
       return -1;
   }
   event.httpd = evhttp_new(event.base);
   if (event.httpd == NULL)
   {
       elog ("failed to get httpd server handle\n");
       return -1;
   }

   evhttp_set_allowed_methods (event.httpd, EVENT_METHODS );

   rc = evhttp_accept_socket(event.httpd, event.fd);
   if ( rc == -1)
   {
       elog ("failed to accept on http server socket\n");
       return -1;
   }
   evhttp_set_gencb(event.httpd, mtcHttpSvr_handler, NULL);
   
   return PASS ;
}

/* initialize the mtce http server */
int mtcHttpSvr_init ( event_type & mtce_event )
{
    int rc = PASS ;
    snprintf (&filename[0], MAX_FILENAME_LEN, "/var/log/%s_event.log", program_invocation_short_name );
    for ( ; ; )
    {
        rc = mtcHttpSvr_setup ( mtce_event );
        if ( rc == RETRY )
        {
            wlog ("%s bind failed (%d)\n", EVENT_SERVER, mtce_event.fd );
        }
        else if ( rc != PASS )
        {
            elog ("%s start failed (rc:%d)\n", EVENT_SERVER, rc );
        }
        else if ( mtce_event.fd > 0 )
        {
            ilog ("Listening On: 'http event server ' socket %s:%d\n", 
                   inet_ntoa(mtce_event.addr.sin_addr), mtce_event.port );
            rc = PASS ;
            break ;
        }
        if ( rc ) mtcWait_secs (5);
    }
    return ( rc ) ;
}
