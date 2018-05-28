/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
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
#define __AREA__ "mgr"

#include "nodeClass.h"      /* for ... maintenance class nodeLinkClass */
#include "mtcSmgrApi.h"     /* for ... this module header              */
#include "jsonUtil.h"       /* for ... jsonUtil_get_key_val            */

/*******************************************/
/* Internal Private utilities and handlers */
/*******************************************/

/* The handles the inventory PATCH request's response message */
void nodeLinkClass::mtcSmgrApi_handler ( struct evhttp_request *req, void *arg )
{
    if ( ! req )
    {
        elog ("%s %s %s Request Timeout (%d)\n",
                  smgrEvent.hostname.c_str(),
                  smgrEvent.service.c_str(),
                  smgrEvent.operation.c_str(),
                  smgrEvent.timeout);

        smgrEvent.status = FAIL_TIMEOUT ;
        goto mtcSmgrApi_handler_out ;
    }

    mtcHttpUtil_status ( smgrEvent );
    if ( smgrEvent.status != PASS )
    {
        wlog ("%s '%s' HTTP %s Request Failed (%d)\n",
                  smgrEvent.hostname.c_str(),
                  smgrEvent.service.c_str(),
                  smgrEvent.operation.c_str(),
                  smgrEvent.status);
    }

    smgrEvent.response.clear();
    if (( mtcHttpUtil_get_response ( smgrEvent )) && ( smgrEvent.response.empty() ))
    {
        wlog ("%s failed to get a response\n", smgrEvent.hostname.c_str() );
    }

mtcSmgrApi_handler_out:

    if ( smgrEvent.blocking == true )
    {
        mtcHttpUtil_free_conn  ( smgrEvent );
        mtcHttpUtil_free_base  ( smgrEvent );

        /* This is needed to get out of the loop in the blocking case
         * Calling this here in non-blocking calls can lead to segfault */
        event_base_loopbreak((struct event_base *)arg);
    }
    smgrEvent.active = false ;
}

/*
 * Name       : mtcSmgrApi_request
 *
 * Description: Submit any of the following requests to
 *              system management against the specified
 *              conroller hostname.
 *
 *   General Operation   - Event Lib Request
 *   -------------------   ---------------------------------
 *   CONTROLLER_QUERY    - SMGR_QUERY_SWACT   (non-blocking)
 *   CONTROLLER_SWACT    - SMGR_START_SWACT   (non-blocking)
 *   CONTROLLER_DISABLED - SMGR_HOST_DISABLED (    blocking)
 *   CONTROLLER_ENABLED  - SMGR_HOST_ENABLED  (    blocking)
 *   CONTROLLER_LOCKED   - SMGR_HOST_LOCKED   (    blocking)
 *   CONTROLLER_UNLOCKED - SMGR_HOST_UNLOCKED (    blocking)
 *
 * Notes      : Retries are ignored for non-blocking operations
 */
int nodeLinkClass::mtcSmgrApi_request ( struct nodeLinkClass::node * node_ptr, mtc_cmd_enum operation, int retries )
{
    int count = 0 ;
    int rc = PASS ;
    string operation_string = "unknown" ;

    if ( system_type == SYSTEM_TYPE__CPE_MODE__SIMPLEX )
    {
        dlog ("%s simpex mode ; SM '%d' request not sent\n", node_ptr->hostname.c_str(), operation );
        return ( PASS );
    }

    if ( smgrEvent.active == true )
    {
        wlog ("%s Service Manager %s Request - In-Progress (retry)\n",
                  node_ptr->hostname.c_str(),
                  smgrEvent.operation.c_str());

        smgrEvent.status = FAIL_MUTEX_ERROR ;
        return (RETRY);
    }

    rc = mtcHttpUtil_event_init ( &smgrEvent,
                                   my_hostname,
                                   "mtcSmgrApi_request",
                                   hostUtil_getServiceIp  (SERVICE_SMGR),
                                   hostUtil_getServicePort(SERVICE_SMGR));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }
    /* Set the common context of this new operation */
    smgrEvent.status   = RETRY ;
    smgrEvent.hostname = node_ptr->hostname ;
    smgrEvent.uuid     = get_uuid ( node_ptr->hostname );

    /* Clear payload and response */
    smgrEvent.address = MTC_SMGR_LABEL ;
    smgrEvent.address.append(node_ptr->hostname);
    smgrEvent.token.url = smgrEvent.address ;
    smgrEvent.blocking = true ;

    smgrEvent.payload = "{\"origin\":\"mtce\"," ;

    if ( operation == CONTROLLER_QUERY )
    {
        smgrEvent.operation = "Query"   ;
        smgrEvent.request = SMGR_QUERY_SWACT ;
        string availStatus = availStatus_enum_to_str (get_availStatus (node_ptr->hostname));
        smgrEvent.blocking = false ;
        smgrEvent.payload     = "" ;
        ilog ("%s sending 'query services' request to HA Service Manager\n",
                  smgrEvent.hostname.c_str());

        return ( mtcHttpUtil_api_request ( smgrEvent )) ;
    }
    else if ( operation == CONTROLLER_SWACT )
    {
        smgrEvent.operation = "Swact"   ;
        smgrEvent.request = SMGR_START_SWACT ;
        string availStatus = availStatus_enum_to_str (get_availStatus (node_ptr->hostname));
        smgrEvent.blocking = false ;
        smgrEvent.payload.append ("\"action\":\"swact\",");
        smgrEvent.payload.append ("\"admin\":\"unlocked\",");
        smgrEvent.payload.append ("\"oper\":\"enabled\",");
        smgrEvent.payload.append ("\"avail\":\"");
        smgrEvent.payload.append (availStatus);
        smgrEvent.payload.append ("\"}");
        ilog ("%s sending 'swact' request to HA Service Manager\n",
                  smgrEvent.hostname.c_str());

        return ( mtcHttpUtil_api_request ( smgrEvent )) ;
    }
    else if ( operation == CONTROLLER_DISABLED )
    {
        operation_string = "disabled" ;
        smgrEvent.operation = "Disable"  ;
        smgrEvent.request = SMGR_HOST_DISABLED ;

        string availStatus = availStatus_enum_to_str (get_availStatus (node_ptr->hostname));
        string adminState  = adminState_enum_to_str  (get_adminState  (node_ptr->hostname));
        string adminAction = adminAction_enum_to_str (get_adminAction (node_ptr->hostname));

        smgrEvent.payload.append ("\"action\":\"");
        if (adminAction.compare("lock"))
            adminAction = "event" ;
        smgrEvent.payload.append (adminAction);
        smgrEvent.payload.append ("\",");

        smgrEvent.payload.append ("\"admin\":\"");
        smgrEvent.payload.append (adminState);
        smgrEvent.payload.append ("\",");

        smgrEvent.payload.append ("\"oper\":\"disabled\",");

        smgrEvent.payload.append ("\"avail\":\"");
        smgrEvent.payload.append (availStatus);
        smgrEvent.payload.append ("\"}");

        ilog ("%s sending '%s-disabled' request to HA Service Manager\n",
                  smgrEvent.hostname.c_str(), adminState.c_str() );
    }
    else if ( operation == CONTROLLER_ENABLED )
    {
        smgrEvent.request = SMGR_HOST_ENABLED ;
        smgrEvent.operation = "Enable" ;
        operation_string = "enabled" ;

        string availStatus = availStatus_enum_to_str (get_availStatus (node_ptr->hostname));
        string adminState = adminState_enum_to_str (get_adminState (node_ptr->hostname));
        string adminAction = adminAction_enum_to_str (get_adminAction (node_ptr->hostname));

        smgrEvent.payload.append ("\"action\":\"");
        if (adminAction.compare("unlock"))
            adminAction = "event" ;
        smgrEvent.payload.append (adminAction);
        smgrEvent.payload.append ("\",");

        smgrEvent.payload.append ("\"admin\":\"");
        smgrEvent.payload.append (adminState);
        smgrEvent.payload.append ("\",");

        smgrEvent.payload.append ("\"oper\":\"enabled\",");

        smgrEvent.payload.append ("\"avail\":\"");
        smgrEvent.payload.append (availStatus);
        smgrEvent.payload.append ("\"}");

        ilog ("%s sending 'unlocked-enabled' request to HA Service Manager\n", 
                  smgrEvent.hostname.c_str());
    }
    else if ( operation == CONTROLLER_LOCKED )
    {
        operation_string = "locked" ;
        smgrEvent.request = SMGR_HOST_LOCKED ;
        smgrEvent.operation = "Lock" ;
        smgrEvent.payload.append ("\"action\":\"lock\",");
        smgrEvent.payload.append ("\"admin\":\"locked\",");
        smgrEvent.payload.append ("\"oper\":\"disabled\",");
        smgrEvent.payload.append ("\"avail\":\"online\"}");

        ilog ("%s sending 'locked-disabled' request to HA Service Manager\n",
                  smgrEvent.hostname.c_str());

    }
    else if ( operation == CONTROLLER_UNLOCKED )
    {
        operation_string = "unlocked" ;
        smgrEvent.request = SMGR_HOST_UNLOCKED ;
        smgrEvent.operation = "Unlock" ;
        smgrEvent.payload.append ("\"action\":\"unlock\",");
        smgrEvent.payload.append ("\"admin\":\"unlocked\",");
        smgrEvent.payload.append ("\"oper\":\"enabled\",");
        smgrEvent.payload.append ("\"avail\":\"available\"}");

        ilog ("%s sending 'unlocked-enabled' request to Service Manager\n",
                  smgrEvent.hostname.c_str());
    }
    else
    {
        return (FAIL_BAD_CASE);
    }
    do
    {
        rc = mtcHttpUtil_api_request ( smgrEvent ) ;

        if ((( operation == CONTROLLER_DISABLED ) ||
             ( operation == CONTROLLER_LOCKED )) &&
             ( rc == HTTP_NOTFOUND ))
        {
            dlog ("%s Service Management (%d)\n", node_ptr->hostname.c_str(), rc );
            rc = PASS ;
        }
        if ( rc != PASS )
        {
            count++ ;
            wlog ("%s failed sending '%s' state to SM (rc:%d) ... retrying (cnt:%d)\n",
                      node_ptr->hostname.c_str(), operation_string.c_str(),
                      rc, count );
        }
    } while ( ( rc != PASS ) && ( count < retries ) ) ;

    if ( rc )
    {
        elog ("%s failed sending '%s' state to Service Management failed (%d) ; giving up (cnt:%d)\n",
                  node_ptr->hostname.c_str(),
                  operation_string.c_str(),
                  rc , count );
    }
    else
    {
        ilog ("%s is '%s' to Service Management\n", node_ptr->hostname.c_str(), operation_string.c_str());
    }
    return ( rc );
}

int mtcSmgrApi_service_state ( libEvent & event , bool & swactable_services )
{
    int rc = FAIL ;

    swactable_services = false ;

    if ( event.response.empty() )
    {
        elog ("%s Query Service State Failed - Empty Response\n",
                  event.hostname.c_str());
        return (rc);
    }
    else
    {
        /* O.K. Lets look at the response - load it into the strings */
        string origin = "" ;
        string hn     = "" ;
        string yesno  = "" ;
        string origin_key          = "origin" ;
        string hostname_key        = "hostname" ;
        string swactable_services_key = "swactable_services" ;
        /*
         *   {
         *       "origin"            : "sm                         "
         *       "admin"             : "unlocked     | locked      " don't care
         *       "oper"              : "enabled      | disabled    " don't care
         *       "avail"             : "any availability state     " don't care
         *       "hostname"          : "controller-0 | controller-1"
         *       "active_services"   : "yes          | no          "
         *       "swactable_services": "yes          | no          "
         *   }
         */

        int rc1 = jsonUtil_get_key_val((char*)event.response.c_str(),
                                  hostname_key, hn );
        int rc2 = jsonUtil_get_key_val((char*)event.response.c_str(),
                                  origin_key, origin );
        int rc3 = jsonUtil_get_key_val((char*)event.response.c_str(),
                                  swactable_services_key, yesno );
        if (( rc1 != PASS ) ||
            ( rc2 != PASS ) ||
            ( rc3 != PASS ) )
        {
            elog ("%s Query Services Failed - Read Key:Values (hn:%d or:%d yn:%d)\n",
                      event.hostname.c_str(),
                      rc1, rc2, rc3 );
        }
        else if ( event.hostname.compare (hn) )
        {
            elog ("%s Query Services Failed - Wrong Controller (%s)\n",
                      event.hostname.c_str(),
                      hn.c_str());
        }
        else if ( !yesno.compare("yes") )
        {
            swactable_services = true ;
            rc = PASS ;
        }
        else if ( !yesno.compare("no") )
        {
            swactable_services = false ;
            rc = PASS ;
        }
        else
        {
            elog ("%s Query Services Failed - %s service state (%s:%s)\n",
                      event.hostname.c_str(),
                      yesno.c_str(),
                      hn.c_str(),
                      yesno.c_str());
        }
    }
    return (rc);
}

/* The Neutron request handler wrapper abstracted from nodeLinkClass */
void mtcSmgrApi_Handler ( struct evhttp_request *req, void *arg )
{
    get_mtcInv_ptr()->mtcSmgrApi_handler ( req , arg );
}
