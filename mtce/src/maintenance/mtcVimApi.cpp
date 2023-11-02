/*
 * Copyright (c) 2013, 2016, 2023 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance
  * Access to Service Manager via REST API Interface.
  *
  */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "vim"

using namespace std;

#include "nodeBase.h"       /* for ... common definitions              */
#include "nodeUtil.h"       /* for ... common utilities                */
#include "nodeClass.h"      /* for ... maintenance class nodeLinkClass */
#include "mtcVimApi.h"      /* for ... this module header              */
#include "jsonUtil.h"       /* for ... jsonUtil_get_key_val            */

/*******************************************/
/* Internal Private utilities and handlers */
/*******************************************/

/* The handles the inventory PATCH request's response message */
int mtcVimApi_handler ( libEvent & event )
{
    jlog ("%s Response:%s\n", event.log_prefix.c_str(), event.response.c_str());

    return (PASS);
}

string nodeLinkClass::mtcVimApi_state_get ( string hostname, int & http_status_code )
{
    string payload = "" ;

    /* payload
     *      {
     *           { "availability":"",
     *             "operational":"",
     *             "administrative":"",
     *             "data_ports_oper:"",
     *             "data_ports_avail:"",
     *             "subfunction_oper:"",
     *             "subfunction_avail:"",
     *           },
     *         "uuid":"",
     *         "hostname":"",
     *         "subfunctions":"",
     *         "personality":""
     *      }
     **/

    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( !node_ptr )
    {
        payload.append (" \"status\" : \"fail\"");
        payload.append (",\"reason\" : \"not found\"");
        payload.append (",\"action\" : \"undetermined\"");
        payload.append ("}");

        http_status_code = HTTP_NOTFOUND ;
        return ( payload );
    }
    payload = ("{\"") ;
    payload.append (MTC_JSON_INV_ADMIN);
    payload.append ("\":\"");
    payload.append (adminState_enum_to_str(node_ptr->adminState));

    payload.append ("\",\"");

    payload.append (MTC_JSON_INV_OPER);
    payload.append ("\":\"");
    payload.append (operState_enum_to_str(node_ptr->operState ));

    payload.append ("\",\"");

    payload.append (MTC_JSON_INV_AVAIL);
    payload.append ("\":\"");
    payload.append (availStatus_enum_to_str(node_ptr->availStatus ));
    payload.append ("\",");

    /* Only add these feilds if we have received messages from the vswitch
     * The avail state is defaulted to offline */
    if ( node_ptr->availStatus_dport != MTC_AVAIL_STATUS__OFFDUTY )
    {
        payload.append ("\"data_ports_oper\":\"");
        payload.append (operState_enum_to_str(node_ptr->operState_dport));
        payload.append ("\",");

        payload.append ("\"data_ports_avail\":\"");
        payload.append (availStatus_enum_to_str(node_ptr->availStatus_dport));
        payload.append ("\",");
    }

    payload.append ("\"subfunction_oper\":\"");
    payload.append (operState_enum_to_str(node_ptr->operState_subf));
    payload.append ("\",");

    payload.append ("\"subfunction_avail\":\"");
    payload.append (availStatus_enum_to_str(node_ptr->availStatus_subf));
    payload.append ("\"},");

    payload.append ("\"hostname\":\"");
    payload.append (hostname);
    payload.append ("\",");

    payload.append ("\"uuid\":\"");
    payload.append (node_ptr->uuid);
    payload.append ("\",");

    payload.append ("\"subfunctions\":\"");
    payload.append (node_ptr->functions);
    payload.append ("\",");

    payload.append ("\"personality\":\"");
    payload.append (node_ptr->function_str);
    payload.append ("\"}");

    return (payload);
}

/* curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2112/v1/systems */

int nodeLinkClass::mtcVimApi_system_info ( string & response )
{
    int http_status_code = HTTP_OK ;
    if ( hosts )
    response = "{\"hosts\":[" ;
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        dlog ("%s %6d %s\n", ptr->uuid.c_str(), ptr->uptime, ptr->hostname.c_str() );

        if ( ptr->add_completed == true )
        {
            response.append("{\"uuid\":\"");
            response.append(ptr->uuid);
            response.append("\",\"hostname\":\"");
            response.append(ptr->hostname);
            response.append("\",\"uptime\":");
            response.append(itos(ptr->uptime));
            response.append("}");
            if (( ptr->next != NULL ) && ( ptr != tail ))
            {
                response.append(",");
            }
        }
        else
        {
            http_status_code = MTC_HTTP_ACCEPTED ;
        }
        if (( ptr->next == NULL ) || ( ptr == tail ))
        {
            break ;
        }
    }
    response.append("]}");
    jlog ("Response: %s", response.c_str() );
    return (http_status_code);
}

/**
  * Name       : mtcVimApi_state_change
  *
  * Description: Submit any of the following requests to
  *              the VIM against the specified hostname.
  *
  * Operations:
  *
  *  VIM Commands  - Descriptions
  *  -------------   ------------------------------
  *  HOST_DISABLE  - Inform VIM that this host is now Mtce-Disabled
  *  HOST_ENABLE   - Inform VIM that this host is now Mtce-Enabled
  *  HOST_FAIL     - Inform VIM that this host has failed and
  *                  undergoing auto recovery
  *
  */
int nodeLinkClass::mtcVimApi_state_change ( struct nodeLinkClass::node * node_ptr,
                                            libEvent_enum request,
                                            int retries )
{
    int http_status_code = HTTP_OK ;
    string type ="host" ;
    mtcHttpUtil_event_init ( &node_ptr->httpReq,
                              node_ptr->hostname,
                              "mtcVimApi_state_change",
                              hostUtil_getServiceIp   ( SERVICE_VIM ),
                              hostUtil_getServicePort ( SERVICE_VIM ));

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.max_retries = retries       ;
    node_ptr->httpReq.active      = true          ;
    node_ptr->httpReq.noncritical = false         ;
    switch ( request )
    {
        case VIM_HOST_DISABLED:
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = VIM_HOST__DISABLED ;
            break ;
        case VIM_HOST_ENABLED:
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = VIM_HOST__ENABLED ;
            break ;
        case VIM_HOST_OFFLINE:
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = VIM_HOST__OFFLINE;
            break ;
        case VIM_HOST_FAILED:
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = VIM_HOST__FAILED ;
            break ;
        case VIM_DPORT_OFFLINE:
            type = "data port";
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = "offline" ;
            break ;
        case VIM_DPORT_CLEARED:
            type = "data port";
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = "clear" ;
            break ;
        case VIM_DPORT_DEGRADED:
            type = "data port";
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = "major" ;
            break ;
        case VIM_DPORT_FAILED:
            type = "data port";
            node_ptr->httpReq.request   = request   ;
            node_ptr->httpReq.operation = "critical" ;
            break ;
        default:
            return (FAIL_BAD_PARM) ;
    }

    node_ptr->httpReq.payload = "{\"state-change\": " ;
    node_ptr->httpReq.payload.append (mtcVimApi_state_get ( node_ptr->hostname , http_status_code ));

    if (( request == VIM_HOST_FAILED ) || ( request == VIM_DPORT_FAILED ))
    {
        wlog ("%s %s\n", node_ptr->hostname.c_str(), node_ptr->httpReq.payload.c_str());
    }
    else
    {
        ilog ("%s sending '%s' state change to vim (%s)",
                  node_ptr->hostname.c_str(),
                  type.c_str(),
                  node_ptr->httpReq.operation.c_str());
        dlog ("%s %s\n", node_ptr->hostname.c_str(), node_ptr->httpReq.payload.c_str());
    }

    return(workQueue_enqueue( node_ptr->httpReq));
}
