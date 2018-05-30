/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file Wind River CGTS Platform Guest Heartbeat REST API
  * used to report heartbeat faults or query instance 
  * information from the VIM.
  * 
  */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "vim"

using namespace std;

#include "nodeBase.h"       /* for ... common definitions              */
#include "nodeUtil.h"       /* for ... common utilities                */
#include "jsonUtil.h"       /* for ... jsonUtil_get_key_val            */

#include "guestUtil.h"      /* for ... guestUtil_inst_init             */
#include "guestSvrUtil.h"   /* for ... hb_get_corrective_action_name   */
#include "guestVimApi.h"    /* for ... this module header              */

#define URL_VIM_ADDRESS        "127.0.0.1"
#define URL_VIM_INST_LABEL     "/nfvi-plugins/v1/instances/"
#define URL_VIM_HOST_LABEL     "/nfvi-plugins/v1/hosts/"

#define VIM_EVENT_SIG          "vimEvent"
#define VIM_SIG                "vim"

#define OPER__HOST_STATE_QUERY "host state query"
#define OPER__HOST_INST_QUERY  "host inst query"
#define OPER__HOST_INST_FAIL   "host inst fail"
#define OPER__HOST_INST_STATUS "host inst status"
#define OPER__HOST_INST_CHANGE "inst status change"
#define OPER__HOST_INST_NOTIFY "host inst notify"


/*********************************************************************
 *
 * Name       : guestVimApi_handler
 *
 * Description: The Guest Heartbeat event request handler 
 *
 *********************************************************************/
void guestHostClass::guestVimApi_handler ( struct evhttp_request *req, void *arg )
{
    string hostname = "unknown" ;

    guestHostClass * obj_ptr = get_hostInv_ptr();
    libEvent & event = obj_ptr->getEvent ( (struct event_base *)arg, hostname );
    if ( event.request == SERVICE_NONE )
    {
        slog ("guest instance Lookup Failed (%p)\n", arg);
        return ;
    }

    /* Check for command timeout */
    if ( !req )
    {
        dlog ("hostname=%s service=%s No Request Parm (%s)\n",
                  event.hostname.c_str(),
                  event.service.c_str(),
                  event.uuid.c_str());
    }

    /* Check the HTTP Status Code */
    event.status = guestHttpUtil_status ( event ) ;
    if ( event.status == HTTP_NOTFOUND )
    {
        wlog ("%s Not Found (%d)\n", event.log_prefix.c_str(), 
                                     event.status);
        goto _guest_handler_done ;
    }

    else if ( event.status != PASS )
    {
        /* The VIM Might not be running at he time I issue the query.
         * In hat case I will get back a 400 */
        if (( event.request != VIM_HOST_STATE_QUERY ) && ( event.status != 400 ))
        {
            elog ("%s HTTP Request Failed (%d) (%s)\n",
                      event.log_prefix.c_str(), 
                      event.status,
                      event.uuid.c_str());
        }
        goto _guest_handler_done ;
    }
    
    /* No response content for this command */
    if ( event.request == VIM_HOST_INSTANCE_STATUS )
    {
        jlog ("%s %s instance status change succeeded\n", event.hostname.c_str(), event.uuid.c_str());
        goto _guest_handler_done ;
    }

    /* No response content for this command */
    else if ( event.request == VIM_HOST_INSTANCE_NOTIFY )
    {
        jlog ("%s %s instance notify succeeded\n", event.hostname.c_str(), event.uuid.c_str());
        goto _guest_handler_done ;
    }

    else if ( httpUtil_get_response ( event ) != PASS )
    {
        wlog ("%s no response available\n", hostname.c_str());
        goto _guest_handler_done ;
    }

    if ( event.response.length() )
    {
       jlog ("%s Response: %s\n", event.hostname.c_str(), 
                                  event.response.c_str());

        if ( event.request == VIM_HOST_STATE_QUERY )
        {
            ilog ("%s host state query response\n", event.hostname.c_str());
            int rc = jsonUtil_get_key_val ( (char*)event.response.data(), "state", event.value ) ;
            if ( rc != PASS )
            {
               elog ("failed to state value (rc=%d)\n", rc );
               event.status = FAIL_KEY_VALUE_PARSE ;
               event.value = "disabled" ; /* override to disabled if operation failed */
            }
        }

        else if ( event.request == VIM_HOST_INSTANCE_FAILED )
        {
            ilog ("%s instance failure response\n", event.uuid.c_str());
            // {"services": [ {"state": "enabled", "service": "heartbeat"}], 
            //  "hostname": "compute-1", 
            //  "uuid": "da973c2a-7469-4e06-b7e1-89bf2643f906"}
            string state = "" ;
            string service = "" ;
            string uuid = "" ;
            int rc1 = jsonUtil_get_key_val ( (char*)event.response.data(), "hostname", hostname ) ;
            int rc2 = jsonUtil_get_key_val ( (char*)event.response.data(), "uuid"    , uuid     ) ;
            if (!(rc1 | rc2 ))
            {
                /* Look for the list of services for this instance 
                 * - currently only heartbeat is supported
                 *
                 * services:[ { "state": "enabled", "service": "heartbeat" } ] 
                 */
                string service_list = "" ;
                rc1 = jsonUtil_get_array_idx ((char*)event.response.data(), "services", 0, service_list ) ;
                if ( rc1 == PASS )
                {
                    instInfo instance ; guestUtil_inst_init ( &instance );
                    guestHostClass * obj_ptr = get_hostInv_ptr();
                    string service = "" ;

                    ilog ("Service List:%s\n", service_list.c_str()); // jlog1
                    
                    instance.uuid = uuid ;

                    /* Get the contents of the services list/array 
                     * Note: we only support one element of the array so hat's
                     * why only index 0 is being requested or looked for
                     *
                     * Get the state of the only service - heartbeat */
                    rc1 = jsonUtil_get_key_val ( (char*)service_list.data(), "state", instance.heartbeat.state ) ;
                    rc2 = jsonUtil_get_key_val ( (char*)service_list.data(), "service", service ) ;

                    /* both of these must pass in order to add this instance */
                    if (( rc1 == PASS ) && ( rc2 == PASS ))
                    {
                        if ( !service.compare("heartbeat") )
                        {
                            instance.heartbeat.provisioned = true ;

                            /* Its either enabled or disabled
                             * - default was disabled in guestUtil_inst_init above */
                            if ( !instance.heartbeat.state.compare("enabled") )
                            {
                                instance.heartbeat.reporting = true ;
                                rc1 = obj_ptr->mod_inst ( hostname, instance );
                            }
                            else if ( !instance.heartbeat.state.compare("disabled") )
                            {
                                instance.heartbeat.reporting = false ;
                                rc1 = obj_ptr->mod_inst ( hostname, instance );
                            }
                            else
                            {
                                // raise error if it is neither enabled nor disabled
                                elog ("%s %s invalid heartbeat.state value %s received\n",
                                hostname.c_str(), instance.uuid.c_str(), instance.heartbeat.state.c_str());
                                event.status = FAIL_INVALID_DATA ;
                                rc1 = FAIL;
                            }
                            if ( rc1 == PASS )
                            {
                                /* o.K. so its provisioned !! */
                                dlog ("%s %s instance modified\n", hostname.c_str(), instance.uuid.c_str());
                             }
                            else
                            {
                                event.status = rc1 ;
                            }
                        }
                        else
                        {
                            elog ("%s unsupported  'service' (%s)\n", hostname.c_str(), service.c_str() );
                            event.status = FAIL_INVALID_DATA ;
                        }
                    }
                    else
                    {
                        elog ("%s failed to get 'state' or 'service' (%d:%d)\n", hostname.c_str(), rc1, rc2 );
                        event.status = FAIL_KEY_VALUE_PARSE ;
                    }
                }
                else
                {
                    elog ("%s failed to get 'service list' or 'uuid' (%d:%d)\n", hostname.c_str(), rc1, rc2 );
                    event.status = FAIL_KEY_VALUE_PARSE ;
                }
            }
            else
            {
               ilog ("%s failed to get 'hostname' or 'uuid' (%d:%d)\n", event.hostname.c_str(), rc1, rc2 );
               event.status = FAIL_KEY_VALUE_PARSE ;
            }
        }
        else if ( event.request == VIM_HOST_INSTANCE_QUERY )
        {
            ilog ("%s instance query response\n", event.uuid.c_str());
            /* { "instances": [{"services": {"service":"heartbeat", "state":"enabled"}, 
             *                "hostname": "compute-2", 
             *                "uuid": "3aca8dad-0e38-4a58-83ab-23ee71159e0d"}]} */

            int rc = jsonUtil_get_key_val ( (char*)event.response.data(), "instances", event.value ) ;
            if ( rc != PASS )
            {
               elog ("%s failed to get host instance array (rc=%d) (%s)\n",
                       event.hostname.c_str(), rc, event.uuid.c_str());
               event.status = FAIL_KEY_VALUE_PARSE ;
            }
            else
            {
                /* The following code parses a JSON string that looks like this.
                 * {
                 *    "instances": 
                 *    [
                 *       { "services": { "service":"heartbeat", "state":"enabled" }, 
                 *         "hostname": "compute-2", 
                 *         "uuid"    : "3aca8dad-0e38-4a58-83ab-23ee71159e0d" 
                 *       } 
                 *    ] , ...
                 * }
                 */
                int instances = 0 ;
                jlog ("%s instance array %s\n", event.hostname.c_str(), (char*)event.response.data());
                rc = jsonUtil_array_elements ( (char*)event.response.data(), "instances", instances );
                if ( rc != PASS )
                {
                    elog ("%s failed to get array elements (%d)\n", hostname.c_str(), rc );
                    event.status = FAIL_KEY_VALUE_PARSE ;
                }
                else
                {
                    ilog ("%s has %d instances\n", hostname.c_str(), instances );
                    for ( int i = 0 ; i < instances ; i++ )
                    {
                        string instance_element = "" ;
                        rc = jsonUtil_get_array_idx ( (char*)event.response.data(), "instances", i, instance_element );
                        if ( ( rc == PASS ) && ( instance_element.size() ))
                        {
                            /* Look for the list of services for this instance 
                             * - currently only heartbeat is supported
                             *
                             * services:[ { "state": "enabled", "service": "heartbeat" } ] 
                             **/
                            string service_list = "" ;
                            string uuid = "" ;
                            int rc1 = jsonUtil_get_array_idx ((char*)instance_element.data(), "services", 0, service_list ) ;
                            int rc2 = jsonUtil_get_key_val   ((char*)instance_element.data(), "uuid", uuid ) ;
                            if (( rc1 == PASS ) && ( rc2 == PASS ))
                            {
                                instInfo instance ; guestUtil_inst_init ( &instance );
                                guestHostClass * obj_ptr = get_hostInv_ptr();
                                string service = "" ;

                                ilog ("Service List:%s\n", service_list.c_str());

                                instance.uuid = uuid ;

                                /* Get the contents of the services list/array 
                                 * Note: we only support one element of the array so hat's
                                 * why only index 0 is being requested or looked for
                                 *
                                 * Get the state of the only service - heartbeat */
                                rc1 = jsonUtil_get_key_val ( (char*)service_list.data(), "state", instance.heartbeat.state ) ;
                                rc2 = jsonUtil_get_key_val ( (char*)service_list.data(), "service", service ) ;

                                /* both of these must pass in order to add this instance */
                                if (( rc1 == PASS ) && ( rc2 == PASS ))
                                {
                                    if ( !service.compare("heartbeat") )
                                    {
                                        instance.heartbeat.provisioned = true ;

                                        /* Its either enabled or disabled
                                         * - default was disabled in guestUtil_inst_init above */
                                        if ( !instance.heartbeat.state.compare("enabled") )
                                        {
                                            instance.heartbeat.reporting = true ;
                                            rc = obj_ptr->add_inst ( hostname, instance );
                                        }
                                        else if ( !instance.heartbeat.state.compare("disabled") )
                                        {
                                            instance.heartbeat.reporting = false ;
                                            rc = obj_ptr->add_inst ( hostname, instance );
                                        }
                                        else
                                        {
                                            // raise error if it is neither enabled nor disabled
                                            elog ("%s %s invalid heartbeat.state value %s received\n",
                                                  hostname.c_str(), instance.uuid.c_str(), instance.heartbeat.state.c_str());
                                            event.status = FAIL_INVALID_DATA ;
                                            rc = FAIL;
                                        }
                                        if ( rc == PASS )
                                        {
                                            /* o.K. so its provisioned !! */
                                            ilog ("%s %s instance added\n", hostname.c_str(), instance.uuid.c_str());
                                        }
                                        else
                                        {
                                            event.status = rc ;
                                        }
                                    }
                                    else
                                    {
                                        elog ("%s unsupported  'service' (%s)\n", hostname.c_str(), service.c_str() );
                                        event.status = FAIL_INVALID_DATA ;
                                    }
                                }
                                else
                                {
                                    elog ("%s failed to get 'state' or 'service' (%d:%d)\n", hostname.c_str(), rc1, rc2 );
                                    wlog ("... Service List: %s\n", service_list.data());
                                    event.status = FAIL_KEY_VALUE_PARSE ;
                                }
                            }
                            else
                            {
                                elog ("%s failed to get 'service list' or 'uuid' (%d:%d)\n", hostname.c_str(), rc1, rc2 );
                                event.status = FAIL_KEY_VALUE_PARSE ;
                            }
                        }
                        else if ( rc != PASS )
                        {
                            elog ("%s failed to get array index %d (rc=%d)\n", hostname.c_str(), i, rc );
                            event.status = FAIL_KEY_VALUE_PARSE ;
                        }
                    }
                }
            }
        }
    }
_guest_handler_done:

    // httpUtil_log_event ( event );

    if (( event.request != SERVICE_NONE ) && 
        ( event.status != HTTP_OK ) && 
        ( event.status != PASS ))
    {
        // wlog ("Event Status: %d\n", event.status );

        /* TODO: Enable log_event */
        wlog ("%s Address : %s (%d)\n", 
                            event.log_prefix.c_str(), 
                            event.address.c_str(),
                            event.status);
        elog ("%s Payload : %s\n", event.log_prefix.c_str(), event.payload.c_str());
        if ( event.response.size() )
        {
            elog ("%s Response: %s\n", event.log_prefix.c_str(), event.response.c_str());
        }
        else
        {
            elog ("%s: no response\n", event.log_prefix.c_str());
        }
    }
    event.active = false ;
    httpUtil_free_conn ( event );
    httpUtil_free_base ( event );

    /* This is needed to get out of the loop */
    event_base_loopbreak((struct event_base *)arg);
}

/* The Guest Heartbeat event request handler 
 * wrapper abstracted from guestHostClass */
void guestVimApi_Handler ( struct evhttp_request *req, void *arg )
{
    get_hostInv_ptr()->guestVimApi_handler ( req , arg );
}

/*****************************************************************************
 *
 * Name       : guestVimApi_svc_event
 *
 * Description: Send a VM instance service state/status change notification
 *              to the VIM.
 *
 * Warning    : Only the 'heartbeat' service 'status' change is supported.
 *
 *****************************************************************************/

int guestVimApi_svc_event ( string hostname, 
                            string instance_uuid, 
                            string state, 
                            string status,
                            string timeout)
{
    guestHostClass * obj_ptr = get_hostInv_ptr() ;
                            
    ilog ("%s %s %s heartbeating status change to '%s' (to vim)\n", hostname.c_str(), 
                                                           instance_uuid.c_str(), 
                                                           state.c_str(), 
                                                           status.c_str()); 

    instInfo * instInfo_ptr = obj_ptr->get_inst ( instance_uuid );
    if ( instInfo_ptr )
    {
        httpUtil_event_init ( &instInfo_ptr->vimEvent, 
                               hostname, 
                               VIM_SIG, 
                               URL_VIM_ADDRESS,
                               daemon_get_cfg_ptr()->vim_event_port);
  
        instInfo_ptr->vimEvent.base = NULL ;
        instInfo_ptr->vimEvent.conn = NULL ;

        /* Set the host context */
        instInfo_ptr->vimEvent.uuid        = instance_uuid  ;
        instInfo_ptr->vimEvent.cur_retries = 0              ;
        instInfo_ptr->vimEvent.max_retries = 3              ;
        instInfo_ptr->vimEvent.active      = true           ;
        instInfo_ptr->vimEvent.noncritical = false          ;

        instInfo_ptr->vimEvent.request   = VIM_HOST_INSTANCE_STATUS;
        instInfo_ptr->vimEvent.operation = OPER__HOST_INST_CHANGE  ;
        instInfo_ptr->vimEvent.token.url = URL_VIM_INST_LABEL      ;
        instInfo_ptr->vimEvent.token.url.append(instance_uuid)     ;

        /* The type of HTTP request */
        instInfo_ptr->vimEvent.type = EVHTTP_REQ_PATCH ;
        
        /* Build the payload */
        instInfo_ptr->vimEvent.payload      = ("{\"uuid\":\"");
        instInfo_ptr->vimEvent.payload.append (instance_uuid);
        instInfo_ptr->vimEvent.payload.append ("\",\"hostname\":\"");
        instInfo_ptr->vimEvent.payload.append (hostname);
        instInfo_ptr->vimEvent.payload.append ("\",\"event-type\":\"service\",\"event-data\":{\"services\":");
        instInfo_ptr->vimEvent.payload.append ("[{\"service\":\"heartbeat\",\"state\":\"");
        instInfo_ptr->vimEvent.payload.append (state);
        instInfo_ptr->vimEvent.payload.append ("\",\"status\":\"");
        instInfo_ptr->vimEvent.payload.append (status);
        instInfo_ptr->vimEvent.payload.append ("\",\"restart-timeout\":\"");
        instInfo_ptr->vimEvent.payload.append (timeout);
        instInfo_ptr->vimEvent.payload.append ("\"}]}}");

        jlog ("%s %s Payload: %s\n", hostname.c_str(), instance_uuid.c_str(), instInfo_ptr->vimEvent.payload.c_str());

        return (guestHttpUtil_api_req ( instInfo_ptr->vimEvent ));
    }
    return (FAIL_HOSTNAME_LOOKUP);
}




/*****************************************************************************
 *
 * Name       : guestVimApi_alarm_event
 *
 * Description: Send a VM instance service an alarm event.
 *
 *****************************************************************************/

int guestVimApi_alarm_event ( string hostname, 
                              string instance_uuid) 
{
    guestHostClass * obj_ptr = get_hostInv_ptr() ;
                            
    ilog ("%s %s heartbeating alarm (ill health) event (to vim)\n",
              hostname.c_str(), 
              instance_uuid.c_str()); 

    instInfo * instInfo_ptr = obj_ptr->get_inst ( instance_uuid );
    if ( instInfo_ptr )
    {
        httpUtil_event_init ( &instInfo_ptr->vimEvent, 
                               hostname, 
                               VIM_SIG, 
                               URL_VIM_ADDRESS,
                               daemon_get_cfg_ptr()->vim_event_port);
  
        instInfo_ptr->vimEvent.base = NULL ;
        instInfo_ptr->vimEvent.conn = NULL ;

        /* Set the host context */
        instInfo_ptr->vimEvent.uuid        = instance_uuid  ;
        instInfo_ptr->vimEvent.cur_retries = 0              ;
        instInfo_ptr->vimEvent.max_retries = 3              ;
        instInfo_ptr->vimEvent.active      = true           ;
        instInfo_ptr->vimEvent.noncritical = false          ;

        instInfo_ptr->vimEvent.request   = VIM_HOST_INSTANCE_STATUS;
        instInfo_ptr->vimEvent.operation = OPER__HOST_INST_CHANGE  ;
        instInfo_ptr->vimEvent.token.url = URL_VIM_INST_LABEL      ;
        instInfo_ptr->vimEvent.token.url.append(instance_uuid)     ;

        /* The type of HTTP request */
        instInfo_ptr->vimEvent.type = EVHTTP_REQ_PATCH ;

        /* Build the payload */
        instInfo_ptr->vimEvent.payload      = ("{\"uuid\":\"");
        instInfo_ptr->vimEvent.payload.append (instance_uuid);
        instInfo_ptr->vimEvent.payload.append ("\",\"hostname\":\"");
        instInfo_ptr->vimEvent.payload.append (hostname);
            
        instInfo_ptr->vimEvent.payload.append ("\",\"event-type\":\"alarm\",\"event-data\":{\"services\":");
        instInfo_ptr->vimEvent.payload.append ("[{\"service\":\"heartbeat\",\"state\":\"unhealthy\",\"repair-action\":\"");
        instInfo_ptr->vimEvent.payload.append (instInfo_ptr->corrective_action);
        instInfo_ptr->vimEvent.payload.append ("\"}]}}");
        
        jlog ("%s %s Payload: %s\n", hostname.c_str(), 
                                     instance_uuid.c_str(),
                                     instInfo_ptr->vimEvent.payload.c_str());
        
        return (guestHttpUtil_api_req ( instInfo_ptr->vimEvent ));
    }
    return (FAIL_HOSTNAME_LOOKUP);
}


/*****************************************************************************
 *
 * Name       : guestVimApi_inst_failed
 *
 * Description: Send a VM instance a failure notification to the VIM.
 *
 * Supported failures are ...
 *
 *   MTC_EVENT_HEARTBEAT_LOSS
 *
 *****************************************************************************/
int guestVimApi_inst_failed ( string       hostname, 
                              string       instance_uuid,
                              unsigned int event,
                                       int retries )
{
    guestHostClass * obj_ptr = get_hostInv_ptr() ;
                            
    elog ("%s %s *** Heartbeat Loss *** \n", 
              hostname.c_str(), 
              instance_uuid.c_str() ); 

    if ( obj_ptr->get_reporting_state (hostname) == false )
    {
        ilog ("%s cancelling failure notification request\n", hostname.c_str());
        ilog ("%s ... 'host' level fault reporting is disabled\n", hostname.c_str());
        return (PASS);
    }
    instInfo * instInfo_ptr = obj_ptr->get_inst ( instance_uuid );
    if ( instInfo_ptr )
    {
        if (( event == MTC_EVENT_HEARTBEAT_LOSS ) &&
            ( instInfo_ptr->heartbeat.reporting == false ))
        {
            ilog ("%s cancelling failure notification request\n", hostname.c_str());
            ilog ("%s ... 'instance' level fault reporting is disabled\n", hostname.c_str());
            return (PASS);
        }

        httpUtil_event_init ( &instInfo_ptr->vimEvent, 
                               hostname, 
                               VIM_SIG, 
                               URL_VIM_ADDRESS,
                               daemon_get_cfg_ptr()->vim_event_port);
  
        instInfo_ptr->vimEvent.base = NULL ;
        instInfo_ptr->vimEvent.conn = NULL ;

        /* Set the host context */
        instInfo_ptr->vimEvent.uuid        = instance_uuid  ;
        instInfo_ptr->vimEvent.cur_retries = 0              ;
        instInfo_ptr->vimEvent.max_retries = retries        ;
        instInfo_ptr->vimEvent.active      = true           ;
        instInfo_ptr->vimEvent.noncritical = false          ;

        instInfo_ptr->vimEvent.request   = VIM_HOST_INSTANCE_FAILED;
        instInfo_ptr->vimEvent.operation = OPER__HOST_INST_FAIL    ;
        instInfo_ptr->vimEvent.token.url = URL_VIM_INST_LABEL      ;
        instInfo_ptr->vimEvent.token.url.append(instance_uuid)     ;

        /* The type of HTTP request */
        instInfo_ptr->vimEvent.type = EVHTTP_REQ_PATCH ;
        
        /* Build the payload */
        instInfo_ptr->vimEvent.payload      = ("{\"uuid\":\"");
        instInfo_ptr->vimEvent.payload.append (instance_uuid);
        instInfo_ptr->vimEvent.payload.append ("\",\"hostname\":\"");
        instInfo_ptr->vimEvent.payload.append (hostname);
        if ( event == MTC_EVENT_HEARTBEAT_LOSS )
        {
            instInfo_ptr->vimEvent.payload.append ("\",\"event-type\":\"alarm\",\"event-data\":{\"services\":");
            instInfo_ptr->vimEvent.payload.append ("[{\"service\":\"heartbeat\",\"state\":\"failed\",\"repair-action\":\"");
            instInfo_ptr->vimEvent.payload.append (instInfo_ptr->corrective_action);
            instInfo_ptr->vimEvent.payload.append ("\"}]}}");

            wlog ("%s %s Payload: %s\n", hostname.c_str(), 
                                         instance_uuid.c_str(),
                                         instInfo_ptr->vimEvent.payload.c_str());
        }
        else
        {
            elog ("%s Unsupported 'event code' (%d)\n", instance_uuid.c_str(), event );
            return (FAIL_BAD_PARM); 
        }

        return (guestHttpUtil_api_req ( instInfo_ptr->vimEvent ));
    }
    return (FAIL_HOSTNAME_LOOKUP);
}


/*****************************************************************************
 *
 * Name       : guestVimApi_inst_action
 *
 * Description: Send a notify message to the VIM in response to voting or notification
 *
 *****************************************************************************/
int guestVimApi_inst_action ( string       hostname,
		                             string       instance_uuid,
                                     string       action,
                                     string       guest_response,
                                     string       reason,
									 int          retries)
{
    guestHostClass * obj_ptr = get_hostInv_ptr() ;

    ilog ("%s %s '%s' action (to vim)\n", hostname.c_str(), instance_uuid.c_str() , action.c_str() );

    instInfo * instInfo_ptr = obj_ptr->get_inst ( instance_uuid );
    if ( !instInfo_ptr )
    	return FAIL_HOSTNAME_LOOKUP;

    httpUtil_event_init ( &instInfo_ptr->vimEvent,
                           hostname,
                           VIM_SIG,
                           URL_VIM_ADDRESS,
                           daemon_get_cfg_ptr()->vim_event_port);

    instInfo_ptr->vimEvent.base = NULL ;
    instInfo_ptr->vimEvent.conn = NULL ;

    /* Set the host context */
    instInfo_ptr->vimEvent.uuid        = instance_uuid  ;
    instInfo_ptr->vimEvent.cur_retries = 0              ;
    instInfo_ptr->vimEvent.max_retries = retries        ;
    instInfo_ptr->vimEvent.active      = true           ;
    instInfo_ptr->vimEvent.noncritical = false          ;

    instInfo_ptr->vimEvent.request   = VIM_HOST_INSTANCE_NOTIFY;
    instInfo_ptr->vimEvent.operation = OPER__HOST_INST_NOTIFY  ;
    instInfo_ptr->vimEvent.token.url = URL_VIM_INST_LABEL      ;
    instInfo_ptr->vimEvent.token.url.append(instance_uuid)     ;

    /* The type of HTTP request */
    instInfo_ptr->vimEvent.type = EVHTTP_REQ_PATCH ;

    /* Build the payload */
    instInfo_ptr->vimEvent.payload = ("{\"uuid\":\"");
    instInfo_ptr->vimEvent.payload.append (instance_uuid);
    instInfo_ptr->vimEvent.payload.append ("\",\"event-type\": \"action\",\"event-data\": {\"action\": \"");
    instInfo_ptr->vimEvent.payload.append (action);
    instInfo_ptr->vimEvent.payload.append ("\", \"guest-response\": \"");
    instInfo_ptr->vimEvent.payload.append (guest_response);
    instInfo_ptr->vimEvent.payload.append ("\", \"reason\": \"");
    instInfo_ptr->vimEvent.payload.append (jsonUtil_escapeSpecialChar(reason));
    instInfo_ptr->vimEvent.payload.append ("\"}}");

    jlog ("%s %s Payload: %s\n", hostname.c_str(), instance_uuid.c_str(), instInfo_ptr->vimEvent.payload.c_str());

    return (guestHttpUtil_api_req ( instInfo_ptr->vimEvent ));
}


/*****************************************************************************
 *
 * Name       : guestVimApi_getHostState
 *
 * Description: Ask the VIM for the top level fault reporting 
 *              state for this host 
 *
 *****************************************************************************/

int guestVimApi_getHostState ( string hostname, string uuid, libEvent & event )
{
    httpUtil_event_init ( &event, 
                           hostname, 
                           VIM_SIG, 
                           URL_VIM_ADDRESS,
                           daemon_get_cfg_ptr()->vim_event_port);
  
    event.base        = NULL     ;
    event.conn        = NULL     ;
    event.uuid        = uuid     ;
    event.active      = true     ;
    event.noncritical = false    ;

    event.type        = EVHTTP_REQ_GET ;
    event.request     =   VIM_HOST_STATE_QUERY;
    event.operation   = OPER__HOST_STATE_QUERY ;
    event.token.url   = URL_VIM_HOST_LABEL ;
    event.token.url.append(event.uuid);
    
    /* Build the payload */
    event.payload = "{\"hostname\": \"";
    event.payload.append (hostname) ;
    event.payload.append ("\",\"uuid\":\"");
    event.payload.append (uuid);
    event.payload.append ("\"}");

    jlog ("%s %s Payload: %s\n", hostname.c_str(), uuid.c_str(), event.payload.c_str());

    return ( guestHttpUtil_api_req ( event ) );
}


/*****************************************************************************
 *
 * Name       : guestVimApi_getHostInst
 *
 * Description: Ask the VIM for all the VM instance info for the 
 *              specified host.
 *
 *****************************************************************************/
int guestVimApi_getHostInst ( string hostname, string uuid, libEvent & event )
{
    httpUtil_event_init ( &event, 
                           hostname, 
                           VIM_SIG, 
                           URL_VIM_ADDRESS,
                           daemon_get_cfg_ptr()->vim_event_port);
  
    event.base        = NULL     ;
    event.conn        = NULL     ;
    event.uuid        = uuid     ;
    event.active      = true     ;
    event.noncritical = false    ;

    event.type        = EVHTTP_REQ_GET ;
    event.request     =   VIM_HOST_INSTANCE_QUERY;
    event.operation   = OPER__HOST_INST_QUERY ;
    event.token.url   = URL_VIM_INST_LABEL ;
    event.token.url.append("?host_uuid=");
    event.token.url.append(event.uuid);

    jlog ("%s %s Payload: %s\n", hostname.c_str(), event.uuid.c_str(), event.token.url.c_str());

    return ( guestHttpUtil_api_req ( event ) );
}
