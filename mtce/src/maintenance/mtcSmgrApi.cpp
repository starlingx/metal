/*
 * Copyright (c) 2013, 2015, 2025 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

 /**
  * @file
  * Wind River Cloud Platform Controller Maintenance.
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

/* Handles Service manager requests */
int mtcSmgrApi_handler ( libEvent & event )
{
    if ( event.status )
    {
        wlog ("%s HTTP '%s' request failed (rc:%d http:%d ) response:%s",
                  event.log_prefix.c_str(),
                  event.operation.c_str(),
                  event.status,
                  event.http_status,
                  event.response.c_str());
    }
    /* log when a command completed ok with work queue level retries */
    if ( event.cur_retries )
    {
        ilog ("%s HTTP '%s' request completed ok ; with %d retries",
                  event.log_prefix.c_str(),
                  event.operation.c_str(),
                  event.cur_retries);
    }
    else
    {
        /* Nothing extra to do here yet.
         * Just report the successful completion */
        ilog ("%s HTTP '%s' request completed ok",
                  event.log_prefix.c_str(),
                  event.operation.c_str());
        hlog ("%s ... response:%s", event.log_prefix.c_str(), event.response.c_str());
    }
    event.active = false ;
    event.done = true ;
    return (event.status);
}

static void mtcSmgrApi_callback ( libEvent & event )
{
    hlog ("%s mtcSmgrApi_callback invoked", event.log_prefix.c_str() );

    /* Pass the workQueue event result data to the FSM */
    nodeLinkClass * object_ptr        = get_mtcInv_ptr() ;
    object_ptr->smgrEvent.done        = true ;
    object_ptr->smgrEvent.active      = false ;
    object_ptr->smgrEvent.status      = event.status   ;
    object_ptr->smgrEvent.http_status = event.http_status ;
    object_ptr->smgrEvent.result      = event.result   ;
    object_ptr->smgrEvent.response    = event.response ;
    object_ptr->smgrEvent.sequence    = event.sequence ;
    object_ptr->smgrEvent.cur_retries = event.cur_retries ;
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
 *   CONTROLLER_DISABLED - SMGR_HOST_DISABLED (non-blocking)
 *   CONTROLLER_ENABLED  - SMGR_HOST_ENABLED  (non-blocking)
 *   CONTROLLER_LOCKED   - SMGR_HOST_LOCKED   (non-blocking)
 *   CONTROLLER_UNLOCKED - SMGR_HOST_UNLOCKED (non-blocking)
 *
 */
int nodeLinkClass::mtcSmgrApi_request ( struct nodeLinkClass::node * node_ptr, mtc_cmd_enum operation, int retries )
{
    mtcHttpUtil_event_init ( &smgrEvent, my_hostname, "mtcSmgrApi_request",
                             hostUtil_getServiceIp  (SERVICE_SMGR),
                             hostUtil_getServicePort(SERVICE_SMGR));

    /* Set the common context of this new operation */
    smgrEvent.status   = RETRY ;
    smgrEvent.hostname = node_ptr->hostname ;
    smgrEvent.uuid     = get_uuid ( node_ptr->hostname );
    smgrEvent.callback = &mtcSmgrApi_callback ;

    smgrEvent.max_retries = retries ;
    smgrEvent.blocking    = false   ;

    /* Clear payload and response */
    smgrEvent.address = MTC_SMGR_LABEL ;
    smgrEvent.address.append(node_ptr->hostname);
    smgrEvent.token.url = smgrEvent.address ;

    smgrEvent.payload = "{\"origin\":\"mtce\"," ;

    if ( operation == CONTROLLER_QUERY )
    {
        smgrEvent.operation = "Query"   ;
        smgrEvent.request = SMGR_QUERY_SWACT ;
        string availStatus = availStatus_enum_to_str (get_availStatus (node_ptr->hostname));
        smgrEvent.payload     = "" ;
    }
    else if ( operation == CONTROLLER_SWACT )
    {
        smgrEvent.operation = "Swact"   ;
        smgrEvent.request = SMGR_START_SWACT ;
        string availStatus = availStatus_enum_to_str (get_availStatus (node_ptr->hostname));
        smgrEvent.payload.append ("\"action\":\"swact\",");
        smgrEvent.payload.append ("\"admin\":\"unlocked\",");
        smgrEvent.payload.append ("\"oper\":\"enabled\",");
        smgrEvent.payload.append ("\"avail\":\"");
        smgrEvent.payload.append (availStatus);
        smgrEvent.payload.append ("\"}");
    }
    else if ( operation == CONTROLLER_DISABLED )
    {
        smgrEvent.operation = "Disable"  ;

        /* Cannot fail the Disable.
         * Failure in this request should not fail the eventual Enable.
         * The workQueue will log but discard noncritical failed requests. */
        smgrEvent.noncritical = true     ;
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
    }
    else if ( operation == CONTROLLER_ENABLED )
    {
        smgrEvent.request = SMGR_HOST_ENABLED ;
        smgrEvent.operation = "Enable" ;

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
    }
    else if ( operation == CONTROLLER_LOCKED )
    {
        smgrEvent.request = SMGR_HOST_LOCKED ;
        /* Cannot fail the Lock .
         * Failure in this request should not fail the lock operarion.
         * The workQueue will log but discard noncritical failed requests. */
        smgrEvent.noncritical = true     ;
        smgrEvent.operation   = "Locked" ;
        smgrEvent.payload.append ("\"action\":\"lock\",");
        smgrEvent.payload.append ("\"admin\":\"locked\",");
        smgrEvent.payload.append ("\"oper\":\"disabled\",");
        smgrEvent.payload.append ("\"avail\":\"online\"}");
    }
    else if ( operation == CONTROLLER_UNLOCKED )
    {
        smgrEvent.request = SMGR_HOST_UNLOCKED ;

        /* Should not fail the Unlock state change to SM.
         * A failure of the final Enable will fail the host enable */
        smgrEvent.noncritical = true       ;
        smgrEvent.operation   = "Unlocked" ;
        smgrEvent.payload.append ("\"action\":\"unlock\",");
        smgrEvent.payload.append ("\"admin\":\"unlocked\",");
        smgrEvent.payload.append ("\"oper\":\"enabled\",");
        smgrEvent.payload.append ("\"avail\":\"available\"}");
    }
    else
    {
        return (FAIL_BAD_CASE);
    }
    ilog ("%s enqueueing '%s' request to Service Manager",
              smgrEvent.hostname.c_str(),
              smgrEvent.operation.c_str());

    return(workQueue_enqueue(smgrEvent));
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

        ilog ("%s Swactable Services Query Response: %s", event.hostname.c_str(), event.response.c_str());

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
            elog ("%s Query Services Failed - %s service state (%s:%s) - response:%s",
                      event.hostname.c_str(),
                      origin.c_str(),
                      hn.c_str(),
                      yesno.c_str(),
                      event.response.c_str());
        }
    }
    return (rc);
}
