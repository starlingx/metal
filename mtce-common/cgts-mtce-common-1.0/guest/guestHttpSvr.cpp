/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /** @file Wind River Titanium Cloud Guest Daemon's HTTP Server */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "gst"

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>      /* for ... inet_addr , inet_ntoa    */ 
#include <arpa/inet.h>       /* for ... LOOPBACK_IP              */
#include <evhttp.h>          /* for ... HTTP_ status definitions */
#include <fcntl.h>

using namespace std;

#include "daemon_common.h" /* */

#include "nodeBase.h"      /* Service header */
#include "nodeTimers.h"    /* */
#include "nodeUtil.h"      /* */
#include "jsonUtil.h"      /* for ... jsonUtil_get_key_val    */

#include "guestUtil.h"     /* for ... guestUtil_inst_init     */
#include "guestClass.h"    /* */
#include "guestHttpSvr.h"  /* for ... this module             */
#include "guestVimApi.h"    /* for ... guestVimApi_inst_action  */

extern int send_event_to_mtcAgent ( string hostname, unsigned int event);

/* Used for log messages */
#define GUEST_SERVER "HTTP Guest Server"

/** 
 * HTTP commands level is specified in the URL as either 
 * of the following ; both are at v1 
 **/
#define HOST_LEVEL_URL   "/v1/hosts/"
#define INST_LEVEL_URL   "/v1/instances/"

/* Commands require the "User Agent" to be set to SERVICE_VERSION */
#define USER_AGENT       "User-Agent"
#define SERVICE_VERSION  "vim/1.0"

/* This servers's request structure */
static request_type guest_request ;

int  sequence = 0 ;
char log_str [MAX_API_LOG_LEN];
char filename[MAX_FILENAME_LEN];

/* Module Cleanup */
void guestHttpSvr_fini ( void )
{
    if ( guest_request.fd )
    {
        if ( guest_request.base )
        {
            event_base_free( guest_request.base);
        }
        close ( guest_request.fd );
    }
}

/* Look for events */
void guestHttpSvr_look ( void )
{
    /* Look for INV Events */
    if ( guest_request.base )
        event_base_loop( guest_request.base, EVLOOP_NONBLOCK );
}

/** 
 * Formulates and updates the resp_buffer reference 
 * variable based on the specified error code 
 **/
string _create_error_response ( int error_code )
{
    string resp_buffer = "{" ;
    resp_buffer.append (" \"status\" : \"fail\"");
    switch (error_code)
    {
        case FAIL_KEY_VALUE_PARSE:
        {
            resp_buffer.append (",\"reason\" : \"command parse error\"");
            break ;
        }
        case FAIL_JSON_ZERO_LEN:
        {
            resp_buffer.append (",\"reason\" : \"no buffer\"");
            break ;
        }
        case FAIL_NOT_FOUND:
        {
            resp_buffer.append (",\"reason\" : \"entity not found\"");
            break ;
 
        }
        case FAIL_INVALID_DATA:
        {
            resp_buffer.append (",\"reason\" : \"invalid data\"");
            break ;
        }
        case FAIL_BAD_STATE:
        {
            resp_buffer.append (",\"reason\" : \"bad state\"");
            break ;
        }
        case FAIL_BAD_CASE:
        {
            resp_buffer.append (",\"reason\" : \"unsupported http command\"");
            break ;
        }
        default:
        {
            ;
        }
    }
    resp_buffer.append ("}");

    return (resp_buffer);
}

/********************************************************************
 *
 * Name       : _get_service_level
 *
 * Description: Verify this request contains 
 * 
 *   1. valid service level specification in the URL and 
 *   2. the expected User-Agent value
 *
 ********************************************************************/
service_level_enum _get_service_level ( struct evhttp_request *req )
{
    service_level_enum service_level  = SERVICE_LEVEL_NONE ;

    /* Parse Headers we care about to verify that it also contains the correct User-Agent header */
    struct evkeyvalq * headers_ptr = evhttp_request_get_input_headers (req);
    const char * header_value_ptr  = evhttp_find_header (headers_ptr, USER_AGENT);
    if ( header_value_ptr ) 
    {
        if ( strncmp ( header_value_ptr, SERVICE_VERSION, 20 ) )
        {
            elog ("Request missing required '%s=%s' (%s)\n", 
                   USER_AGENT, SERVICE_VERSION, header_value_ptr );
            return (service_level);
        }
    }

    /* get the URL string */
    const char * url_ptr = evhttp_request_get_uri (req);
    jlog1 ("URI: %s\n", url_ptr );

    /* look for the supported service levels in the url */
    const char * service_level_ptr = strstr ( url_ptr, HOST_LEVEL_URL);
    if ( service_level_ptr )
    {
       service_level = SERVICE_LEVEL_HOST ;
    }
    else
    {
        service_level_ptr = strstr ( url_ptr, INST_LEVEL_URL);
        if ( service_level_ptr )
        {
            service_level = SERVICE_LEVEL_INST ;
        }
    }
    if ( service_level == SERVICE_LEVEL_NONE )
    {
        elog ("Unsupported service level (url:%s)\n", url_ptr );
        return (service_level);
    }
    return (service_level);
}



string _update_services_response ( string hostname, string uuid, instInfo * instinfo_ptr )
{
    string response = ("{");
    response.append ("\"uuid\":\"");
    response.append (uuid);
    response.append ("\",");
    response.append ("\"hostname\":\"");
    response.append (hostname);
    response.append ("\",");
    response.append ("\"services\": [{ \"service\":\"heartbeat\",");

    response.append ("\"state\":\"");
    if ( instinfo_ptr->heartbeat.reporting == true )
        response.append ("enabled");
    else
        response.append ("disabled");
    
    response.append ("\",\"restart-timeout\":\"");
    if ( instinfo_ptr->heartbeating == true )
    {
        response.append (instinfo_ptr->restart_to_str);
        response.append ("\",\"status\":\"");
        response.append ("enabled\"}]}");
    }
    else
    {
        response.append ("0\",\"status\":\"");
        response.append ("disabled\"}]}");
    }
    return (response);
}

/*****************************************************************************
 *
 * Name:      guestHttpSvr_vim_req
 *
 * Handles three 'operations'
 *
 *  'delete' - based on uuid
 *  'modify' - based on list of key - value pairs
 *  'add'    - based on inventory record
 *
 ******************************************************************************
 * Test Commands:
 *

Add Instance:
curl -i -X POST -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df10449d -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df10449d", "channel" : "cgts-instance000001", "services" : ["heartbeat"]}'



Disable Instance: heartbeat
curl -i -X PUT -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df10449d -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df10449d", "channel" : "cgts-instance000001", "services" : [{"service":"heartbeat" , "state":"disabled"}]}'

Delete Host:
curl -i -X DELETE -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/8aee436e-d564-459e-a0d8-26c44792a9df 

Enable Host: heartbeat
curl -i -X PUT -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/8aee436e-d564-459e-a0d8-26c44792a9df/enable -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df10449d"}'

Enable Host: heartbeat
curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df10449d

*/

/*********************************************************************************
 *
 * Name        : guestHttpSvr_host_req
 *
 * Description : Handles host level VIM requests
 *
 ********************************************************************************/
string guestHttpSvr_host_req ( char          * buffer_ptr, 
                               mtc_cmd_enum    command,
                               evhttp_cmd_type http_cmd, 
                               int           & http_status_code )
{
    string response = "" ;
    string hostname = "" ;

    int rc = jsonUtil_get_key_val ( buffer_ptr, MTC_JSON_INV_NAME, hostname );

    if ( rc )
    {
        wlog ("Failed to parse command key values (%d)\n", rc );
        ilog ("... %s\n", buffer_ptr );

        response = _create_error_response ( FAIL_KEY_VALUE_PARSE );
        http_status_code = HTTP_BADREQUEST ;
    }
    else
    {
        guestHostClass * obj_ptr = get_hostInv_ptr ();

        string instance_info = "" ;
        string instance_uuid = "" ;
        string instance_chan = "" ;
        
        /* WARNING: We only support a single list element for now */
        list<string> services_list ;
        services_list.clear() ;
        
        switch ( http_cmd )
        {
            case EVHTTP_REQ_PUT:
            {
                qlog ("%s VIM CMD: Enable Host\n", hostname.c_str());

                rc = obj_ptr->host_inst ( hostname, command );
                if ( rc )
                {
                    elog ("%s Host Enable Request (vim) - Host Not Found\n", hostname.c_str());
                    response = _create_error_response ( FAIL_NOT_FOUND );
                    http_status_code = HTTP_NOTFOUND ;

                    /* Ask mtce for an inventory update */
                    send_event_to_mtcAgent ( obj_ptr->hostBase.my_hostname, MTC_EVENT_MONITOR_READY ) ;

                }
                else
                {
                    http_status_code = HTTP_OK ;
                    response = " { \"status\" : \"pass\" }" ;
                }
                break ;
            }
            default:
            {
                wlog ("%s Unsupported http command '%s'\n", 
                          hostname.c_str(), getHttpCmdType_str(http_cmd));
                response = _create_error_response ( FAIL_BAD_CASE );
                http_status_code = HTTP_BADREQUEST ;
            }
        }
    }
    return (response);
}

/*********************************************************************************
 *
 * Name        : _get_key_val
 *
 * Description : Get valid value from http message and generate error if failed
 *
 ********************************************************************************/
int _get_key_val ( char  *  buffer_ptr,
                   string   key,
                   string & value,
                   int    & http_status_code,
                   string & response )
{
    int rc = jsonUtil_get_key_val ( buffer_ptr, key, value );

    if ( rc )
    {
        wlog ("Failed to extract %s from message\n", key.c_str());
        http_status_code = HTTP_BADREQUEST ;
        response = _create_error_response ( FAIL_KEY_VALUE_PARSE );
    }
    return rc;
}

/*********************************************************************************
 *
 * Name        : _get_list
 *
 * Description : Get valid list from http message and generate error if failed
 *
 ********************************************************************************/
int _get_list ( char        *  buffer_ptr,
                string         key,
                list<string> & list,
                int          & http_status_code,
                string       & response )
{
    int rc = jsonUtil_get_list ( buffer_ptr, key, list );

    if ( rc )
    {
        wlog ("Failed to extract %s from message\n", key.c_str());
        http_status_code = HTTP_BADREQUEST ;
        response = _create_error_response ( FAIL_KEY_VALUE_PARSE );
    }
    return rc;
}

#define EVENT_VOTE                "vote"
#define EVENT_STOP                "stop"
#define EVENT_REBOOT              "reboot"
#define EVENT_PAUSE               "pause"
#define EVENT_UNPAUSE             "unpause"
#define EVENT_SUSPEND             "suspend"
#define EVENT_RESUME              "resume"
#define EVENT_LIVE_MIGRATE_BEGIN  "live_migrate_begin"
#define EVENT_LIVE_MIGRATE_END    "live_migrate_end"
#define EVENT_COLD_MIGRATE_BEGIN  "cold_migrate_begin"
#define EVENT_COLD_MIGRATE_END    "cold_migrate_end"

string _get_action_timeout ( instInfo * instInfo_ptr, string action )
{
    if ( instInfo_ptr->heartbeating == false )
    {
        ilog ("%s returning timeout of zero while not heartbeating for action '%s'\n",
                  log_prefix(instInfo_ptr).c_str(), action.c_str());
        return ("0");
    }
    if ( !action.compare (EVENT_VOTE) )
        return (instInfo_ptr->vote_to_str);

    if ( !action.compare (EVENT_STOP) )
        return (instInfo_ptr->shutdown_to_str);
    if ( !action.compare (EVENT_REBOOT) )
        return (instInfo_ptr->shutdown_to_str);
    if ( !action.compare (EVENT_PAUSE) )
        return (instInfo_ptr->suspend_to_str);
    if ( !action.compare (EVENT_UNPAUSE) )
        return (instInfo_ptr->resume_to_str);
    if ( !action.compare (EVENT_SUSPEND) )
        return (instInfo_ptr->suspend_to_str);
    if ( !action.compare (EVENT_RESUME) )
        return (instInfo_ptr->resume_to_str);

    if ( !action.compare (EVENT_LIVE_MIGRATE_BEGIN) )
        return (instInfo_ptr->suspend_to_str);
    if ( !action.compare (EVENT_LIVE_MIGRATE_END) )
        return (instInfo_ptr->resume_to_str);
    if ( !action.compare (EVENT_COLD_MIGRATE_BEGIN) )
        return (instInfo_ptr->suspend_to_str);
    if ( !action.compare (EVENT_COLD_MIGRATE_END) )
        return (instInfo_ptr->resume_to_str);

    ilog ("%s returning timeout of zero for invalid action '%s'\n",
              log_prefix(instInfo_ptr).c_str(), action.c_str());
    
    return ("0");
}

/*********************************************************************************
 *
 * Name        : guestHttpSvr_inst_req
 *
 * Description : Handles instance level VIM requests
 *
 ********************************************************************************/
string guestHttpSvr_inst_req ( char           * buffer_ptr, 
                               mtc_cmd_enum     command,
                               evhttp_cmd_type  http_cmd, 
                               int            & http_status_code )
{
    string response = "" ;
    string hostname = "" ;
    string instance_uuid = "" ;

    _get_key_val ( buffer_ptr, MTC_JSON_INV_NAME, hostname, http_status_code, response);
    if ( _get_key_val ( buffer_ptr, MTC_JSON_INV_NAME, hostname, http_status_code, response ))
        return (response);

    if ( _get_key_val ( buffer_ptr, "uuid", instance_uuid, http_status_code, response ))
        return (response);

    instInfo instance_info ; guestUtil_inst_init ( &instance_info );
    instance_info.uuid = instance_uuid;

    guestHostClass * obj_ptr = get_hostInv_ptr ();

    /* WARNING: We only support a single list element for now */
    list<string> services_list ;
    services_list.clear() ;
        
    switch ( http_cmd )
    {
        case EVHTTP_REQ_POST:
        {
            if ( MTC_CMD_VOTE == command )
            {
                jlog ("vote instance Info: %s", buffer_ptr );

                string action = "";
                if ( _get_key_val (buffer_ptr, "action", action, http_status_code, response ) )
                    return (response);

                qlog ("VIM CMD: Vote instance %s\n",
                      instance_info.uuid.c_str());

                instInfo * instInfo_ptr = obj_ptr->get_inst ( instance_uuid );
                if ( instInfo_ptr )
                {
                    response = ("{\"uuid\":\"");
                    response.append (instance_uuid);
                    response.append ("\",\"hostname\":\"");
                    response.append (hostname);
                    response.append ("\",\"action\":\"");
                    response.append (action);
                    response.append ("\",\"timeout\":\"");
                    response.append (_get_action_timeout ( instInfo_ptr, EVENT_VOTE ));
                    response.append ("\"}");
                    
                    jlog ("%s %s Vote Response: %s\n", hostname.c_str(),
                                                       instance_uuid.c_str(),
                                                       response.c_str());


                    if ( instInfo_ptr->heartbeating )
                        send_cmd_to_guestServer (hostname, MTC_CMD_VOTE_INST, instance_uuid, true, action);
                }
                else
                {
                    elog ("%s %s vote request (from vim) - Instance Not Found\n", hostname.c_str(), instance_uuid.c_str());
                    response = _create_error_response ( FAIL_NOT_FOUND );
                    http_status_code = HTTP_NOTFOUND ;
                }
            }
            else if ( MTC_CMD_NOTIFY == command )
            {
                jlog ("notify instance Info: %s", buffer_ptr );

                string action = "";
                if ( _get_key_val (buffer_ptr, "action", action, http_status_code, response ))
                    return (response);

                qlog ("%s %s VIM CMD: Notify instance\n",
                          hostname.c_str(), instance_info.uuid.c_str());
        
                instInfo * instInfo_ptr = obj_ptr->get_inst ( instance_uuid );
                if ( instInfo_ptr )
                {
                    response = ("{\"uuid\":\"");
                    response.append (instance_uuid);
                    response.append ("\",\"hostname\":\"");
                    response.append (hostname);
                    response.append ("\",\"action\":\"");
                    response.append (action);
                    response.append ("\",\"timeout\":\"");
                    response.append (_get_action_timeout ( instInfo_ptr , action ));
                    response.append ("\"}");
                    
                    jlog ("%s %s Notify Response: %s\n", hostname.c_str(), instInfo_ptr->uuid.c_str(), response.c_str());

                    if ( instInfo_ptr->heartbeating )
                        send_cmd_to_guestServer (hostname, MTC_CMD_NOTIFY_INST, instance_uuid, true, action);
                }
                else
                {
                    elog ("%s %s notify request (vim) - Instance Not Found\n", hostname.c_str(), instance_uuid.c_str());
                    response = _create_error_response ( FAIL_NOT_FOUND );
                    http_status_code = HTTP_NOTFOUND ;
                }
            }
            /* Add instance */
            else
            {
                if ( _get_list (buffer_ptr, "services", services_list, http_status_code, response ))
            	    return (response);

                string service = services_list.front();
                qlog ("%s %s VIM CMD: Add Instance\n",
                          hostname.c_str(),
                          instance_info.uuid.c_str());

                ilog ("%s %s add request (from vim) (%s)\n",
                          hostname.c_str(),
                          instance_info.uuid.c_str(),
                          service.c_str());

                if ( obj_ptr->add_inst ( hostname , instance_info ) != PASS )
                {
                    response = _create_error_response ( FAIL_INVALID_DATA );
                    http_status_code = HTTP_BADREQUEST ;
                }
                else
                {
                    instance_info.heartbeat.provisioned = true ;
                    response = " { \"status\" : \"pass\" }" ;
                }
            }
            break ;
        }
        /* PATCH is used to control service states ; enable or disable */
        case EVHTTP_REQ_PATCH:
        {
            if ( _get_list (buffer_ptr, "services", services_list, http_status_code, response ) )
                return (response);

            jlog ("%s modify instance (%s)", hostname.c_str(), buffer_ptr );

            string service , state ;
            string services = services_list.front() ;
            jsonUtil_get_key_val ( (char*)services.data(), "service", service );
            jsonUtil_get_key_val ( (char*)services.data(), "state"  , state   );
                    
            qlog ("%s %s VIM CMD: Modify Instance\n",
                      hostname.c_str(),
                      instance_info.uuid.c_str());

            if ( service.compare("heartbeat"))
            {
                response = _create_error_response ( FAIL_INVALID_DATA );
                http_status_code = HTTP_BADREQUEST ;
                return (response);
            }
            else if ( !state.compare("enabled"))
                instance_info.heartbeat.reporting = true;
            else if ( !state.compare("disabled"))
                instance_info.heartbeat.reporting = false;
            else
            {
                elog ("%s modify request (vim) - invalid instance state '%s'\n", hostname.c_str(), state.c_str());
                response = _create_error_response ( FAIL_BAD_STATE );
                http_status_code = HTTP_BADREQUEST ;
                return (response);
            }
            int rc = obj_ptr->mod_inst ( hostname, instance_info );
            if ( rc )
            {
                elog ("%s %s modify request (vim) - Instance Not Found\n", 
                          hostname.c_str(), instance_info.uuid.c_str());

                response = _create_error_response ( FAIL_NOT_FOUND );
                http_status_code = HTTP_NOTFOUND ;
                return (response);
            }
            else
            {
                instInfo * instInfo_ptr = obj_ptr->get_inst ( instance_info.uuid );
                if ( instInfo_ptr )
                {
                    response = _update_services_response ( hostname , instInfo_ptr->uuid, instInfo_ptr );
                    http_status_code = HTTP_OK ;
                }
                else
                {
                    response = _create_error_response ( FAIL_NOT_FOUND );
                    http_status_code = HTTP_NOTFOUND ;
                }
            }
            break ;
        }
        default:
        {
            wlog ("%s Unsupported HTTP '%s' request\n", instance_uuid.c_str(), getHttpCmdType_str(http_cmd));
        }
    }
    return (response);
}

/*****************************************************************************
 *
 * Name:        guestHttpSvr_vimCmdHdlr
 *
 * Description: Receive an http request extract the request type and buffer from
 *              it and call process request handler.
 *              Send the processed message response back to the connection.
 *
 * Supported requests include: POST, PUT, DELETE
 *
 ******************************************************************************/

int _get_url_info ( struct evhttp_request * req, 
                    const char            * url_ptr,
                    url_info_type         & url_info )
{
    size_t len      = 0  ;

    /* Extract the service level from the request URL ; host or instance */
    url_info.service_level = _get_service_level ( req );
    if ( url_info.service_level == SERVICE_LEVEL_NONE )
    {
        return ( FAIL_INVALID_DATA );
    }
    
    /* Remove the service level from the URL */
    if ( url_info.service_level == SERVICE_LEVEL_HOST )
    {
        url_ptr  = strstr ( url_ptr, HOST_LEVEL_URL );
        len = strlen ( HOST_LEVEL_URL );
    }
    else
    {
        url_ptr  = strstr ( url_ptr, INST_LEVEL_URL);
        len = strlen ( INST_LEVEL_URL );
    }

    if ( url_ptr )
    {
        url_ptr += len ;
        url_info.temp = url_ptr ;
        url_info.uuid = url_info.temp.substr ( 0 , UUID_LEN );
    }
    else
    {
        ilog ("Failed to parse URL (%s)", url_ptr); // DLOG
        return (FAIL_INVALID_UUID) ;
    }
    /**
     * Check to see if there is a command enable/disable/etc after the UUID
     * ... If what is left on the URL is longer than a UUID then 
     *     there must be a command so lets get that 
     **/
    if ( url_info.temp.length() > UUID_LEN )
    {
        url_info.command = url_info.temp.substr(url_info.uuid.length()+1, string::npos );
        dlog ("Command:%s\n", url_info.command.c_str());
    }  
    return (PASS);
}

void guestHttpSvr_vimCmdHdlr (struct evhttp_request *req, void *arg)
{
    int rc ;
    struct evbuffer *resp_buf ;
    url_info_type    url_info ;

    int http_status_code = HTTP_NOTFOUND ;
    guestHostClass * obj_ptr = get_hostInv_ptr () ;
    string response = _create_error_response ( FAIL_JSON_ZERO_LEN );

    guest_request.req = req ;
    jlog1 ("HTTP Request:%p base:%p Req:%p arg:%p\n", &guest_request, 
                                                      guest_request.base, 
                                                      guest_request.req, 
                                                      arg );

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
    jlog1 ("%s request from '%s'\n", getHttpCmdType_str(http_cmd), host_ptr );

    /* Log the request */
    snprintf (&filename[0], MAX_FILENAME_LEN, "/var/log/%s_request.log", program_invocation_short_name );
    // snprintf (&log_str[0], MAX_API_LOG_LEN-1, "\n%s [%5d] http request seq: %d with %d request from %s:%s", 
    snprintf (&log_str[0], MAX_API_LOG_LEN-1, "guest services request '%s' (%d) %s:%s", 
               getHttpCmdType_str(http_cmd), ++sequence, host_ptr, url_ptr );
    // send_log_message ( "controller-0", &filename[0], &log_str[0] );
    jlog ( "%s", log_str );

    /* Fill in the url_info struct from the url string in the request */
    rc = _get_url_info ( req, url_ptr, url_info );
    if ( rc )
    {
        evhttp_send_error ( req, MTC_HTTP_FORBIDDEN, response.data() );
        return ;
    }

    switch ( http_cmd )
    {
        case EVHTTP_REQ_DELETE:
        {
            qlog ("%s VIM CMD: Delete Host or Instance\n", url_info.uuid.c_str()); 
            if ( url_info.service_level == SERVICE_LEVEL_HOST )
            {
                /* Nothing to do at the host level for delete.
                 * Don't try and do a hostname lookup as it may already have been deleted */
                ilog ("%s delete host services request (vim)\n", url_info.uuid.c_str());
            }
            else
            {
                ilog ("%s delete instance request (vim)\n", url_info.uuid.c_str()); 

                rc = obj_ptr->del_inst ( url_info.uuid );
            }
            if ( rc )
            {
                elog ("%s instance not found\n", url_info.uuid.c_str());
                response = _create_error_response ( FAIL_NOT_FOUND );
                http_status_code = HTTP_NOTFOUND ;
            }
            else
            {
                http_status_code = HTTP_NOCONTENT ;
                response = "{ \"status\" : \"pass\" }" ;
            }
            break ;
        }

        /**
         * GET is handled at this level because
         * there is no payload with it. 
         **/
        case EVHTTP_REQ_GET:
        {
            if ( url_info.service_level == SERVICE_LEVEL_INST )
            {
                instInfo * instance_ptr = obj_ptr->get_inst ( url_info.uuid );
                qlog ("%s VIM CMD: Query Instance - Reporting State\n", url_info.uuid.c_str()); 
                if ( !instance_ptr )
                {
                     elog ("%s query instance reporting state (vim) failed - Instance Not Found\n", url_info.uuid.c_str());
                     response = _create_error_response ( FAIL_NOT_FOUND );
                     http_status_code = HTTP_NOTFOUND ;
                }
                else
                {
                    string hostname = obj_ptr->get_inst_host_name(url_info.uuid);
                    response = _update_services_response ( hostname , url_info.uuid, instance_ptr );
                    http_status_code = HTTP_OK ;
                }
            }
            /* GET the host level reporting state */
            else if ( url_info.service_level == SERVICE_LEVEL_HOST )
            {
                string hostname = obj_ptr->get_host_name(url_info.uuid) ;
                qlog ("%s VIM CMD: Query Host - Reporting State\n", hostname.c_str()); 
                if ( hostname.length() )
                {

                    response = ("{");
                    response.append ("\"uuid\":\"");
                    response.append (url_info.uuid);
                    response.append ("\",");
                    response.append ("\"hostname\":\"");
                    response.append (hostname);
                    response.append ("\",");
                    response.append ("\"state\":\"");
                
                    if ( obj_ptr->get_reporting_state(hostname) == true )
                        response.append ("enabled\"}");
                    else
                        response.append ("disabled\"}");
                
                    http_status_code = HTTP_OK ;
                }
                else
                {
                    wlog ("%s query host reporting state (vim) failed - Host Not Found\n", url_info.uuid.c_str());
                    response = _create_error_response ( FAIL_NOT_FOUND );
                    http_status_code = HTTP_NOTFOUND ;

                    /* Ask mtce for an inventory update */
                    send_event_to_mtcAgent ( obj_ptr->hostBase.my_hostname, MTC_EVENT_MONITOR_READY ) ;
                }
            }
            else
            {
                http_status_code = HTTP_NOTFOUND ;
                slog ("invalid service level\n");
            }
            break ;
        }

        case EVHTTP_REQ_PUT:
        case EVHTTP_REQ_POST:
        case EVHTTP_REQ_PATCH:
        {
            /* GET the host level reporting state */
            if (( http_cmd == EVHTTP_REQ_POST ) && 
                ( url_info.service_level == SERVICE_LEVEL_HOST ))
            {
                string hostname = obj_ptr->get_host_name(url_info.uuid) ;
                qlog ("%s VIM CMD: Create Host\n", hostname.c_str());
                if ( hostname.length() )
                {
                    ilog ("%s create host services (vim)\n", hostname.c_str());
                
                    http_status_code = HTTP_OK ;
                    response = " { \"status\" : \"pass\" }" ;
                }
                else
                {
                    wlog ("%s create host (vim) failed - Host Not Found\n", url_info.uuid.c_str());
                    response = _create_error_response ( FAIL_NOT_FOUND );
                    http_status_code = HTTP_NOTFOUND ;

                    /* Ask mtce for an inventory update */
                    send_event_to_mtcAgent ( obj_ptr->hostBase.my_hostname, MTC_EVENT_MONITOR_READY ) ;
                }
                break ;
            }

            /* Otherwise for PUTs and instances ; get the payload */
            struct evbuffer *in_buf = evhttp_request_get_input_buffer ( req );
            if ( in_buf )
            {
                size_t len = evbuffer_get_length(in_buf) ;
                if ( len )
                {
                    ev_ssize_t bytes = 0 ;
                    char * buffer_ptr = (char*)malloc(len+1);
                    jlog1 ("Buffer @ %p contains %ld bytes\n", &in_buf, len );
                
                    memset ( buffer_ptr, 0, len+1 ); 
                    bytes = evbuffer_remove(in_buf, buffer_ptr, len );
                
                    if ( bytes <= 0 )
                    {
                        http_status_code = HTTP_BADREQUEST ;
                        wlog ("http request with no payload\n");
                    }
                    else
                    {
                        http_status_code = HTTP_OK ;
                        mtc_cmd_enum       mtc_cmd;

                        jlog("%s\n", buffer_ptr );

                        if (!url_info.command.compare("enable") )
                            mtc_cmd = MTC_CMD_ENABLE ;
                        else if (!url_info.command.compare("disable") )
                            mtc_cmd = MTC_CMD_DISABLE ;
                        else if (!url_info.command.compare("vote") )
                            mtc_cmd = MTC_CMD_VOTE ;
                        else if (!url_info.command.compare("notify") )
                            mtc_cmd = MTC_CMD_NOTIFY ;
                        else
                            mtc_cmd = MTC_CMD_NOT_SET ;

                        if ( url_info.service_level == SERVICE_LEVEL_INST )
                        {
                            response = guestHttpSvr_inst_req ( buffer_ptr,
                                                               mtc_cmd,
                                                               http_cmd,
                                                               http_status_code );
                        }
                        else if ( url_info.service_level == SERVICE_LEVEL_HOST )
                        {
                            response = guestHttpSvr_host_req ( buffer_ptr,
                                                               mtc_cmd,
                                                               http_cmd,
                                                               http_status_code );
                        }
                        else
                        {
                            slog ("invalid service level\n");
                        }
                    }
                    free ( buffer_ptr );
                }
                else
                {
                     http_status_code = MTC_HTTP_LENGTH_REQUIRED ;
                     wlog ("http request has no length\n");
                }
            }
            else
            {
                http_status_code = HTTP_BADREQUEST ;
                wlog ("Http request has no buffer\n");
            }
            break ;
        }
        default:
        {
            wlog ("Unknown command (%d)\n", http_cmd );
            http_status_code = HTTP_NOTFOUND ;
        }
    }

    if (( http_status_code >= HTTP_OK) && (http_status_code <= HTTP_NOCONTENT ))
    {
        resp_buf = evbuffer_new();
        jlog ("Response: %s\n", response.c_str());
        evbuffer_add_printf (resp_buf, "%s\n", response.data());
        evhttp_send_reply (guest_request.req, http_status_code, "OK", resp_buf ); 
        evbuffer_free ( resp_buf );
    }
    else
    {
        if ( http_status_code == HTTP_NOTFOUND )
        {
            wlog ("%s not found\n", url_ptr );
        }
        else
        {
            elog ("HTTP request error:%d ; cmd:%s url:%s\n", 
                   http_status_code,
                   getHttpCmdType_str(http_cmd), 
                   url_ptr);
            elog ("... response:%s\n", response.c_str());
        }
        evhttp_send_error (guest_request.req, http_status_code, response.data() );
    }
}

/*****************************************************************
 *
 * Name        : guestHttpSvr_bind
 *
 * Description : Setup the HTTP server socket
 *
 *****************************************************************/
int guestHttpSvr_bind (  request_type & request )
{
   int rc     ;
   int flags  ;
   int one = 1;
   
   request.fd = socket(AF_INET, SOCK_STREAM, 0);
   if (request.fd < 0)
   {
       elog ("HTTP server socket create failed (%d:%m)\n", errno);
       return FAIL_SOCKET_CREATE ;
   }

   /* make socket reusable */
   rc = setsockopt(request.fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int));
 
   memset(&request.addr, 0, sizeof(struct sockaddr_in));
   request.addr.sin_family = AF_INET;
   request.addr.sin_addr.s_addr = inet_addr(LOOPBACK_IP) ;
   request.addr.sin_port = htons(request.port);
 
   /* bind port */
   rc = bind ( request.fd, (struct sockaddr*)&request.addr, sizeof(struct sockaddr_in));
   if (rc < 0)
   {
       elog ("HTTP bind failure for port %d (%d:%m)\n", request.port, errno );
       return FAIL_SOCKET_BIND ;
   }

   /* Listen for requests */
   rc = listen(request.fd, 10 );
   if (rc < 0)
   {
       elog ("HTTP listen failed (%d:%m)\n", errno );
       return FAIL_SOCKET_LISTEN;
   }

   /* make non-blocking */
   flags = fcntl ( request.fd, F_GETFL, 0) ;
   if ( flags < 0 || fcntl(request.fd, F_SETFL, flags | O_NONBLOCK) < 0)
   {
       elog ("HTTP set to non-blocking failed (%d:%m)\n", errno );       
       return FAIL_SOCKET_OPTION;
   }

   return PASS;
}

/* Setup the http server */
int guestHttpSvr_setup ( request_type & request )
{
   int rc = PASS ;
   if ( ( rc = guestHttpSvr_bind ( request )) != PASS )
   {
       return rc ;
   }
   else if (request.fd < 0)
   {
       wlog ("failed to get http server socket file descriptor\n");
       return RETRY ;
   }

   request.base = event_base_new();
   if (request.base == NULL)
   {
       elog ("failed to get http server request base\n");
       return -1;
   }
   request.httpd = evhttp_new(request.base);
   if (request.httpd == NULL)
   {
       elog ("failed to get httpd server handle\n");
       return -1;
   }

   evhttp_set_allowed_methods (request.httpd, EVENT_METHODS );

   rc = evhttp_accept_socket(request.httpd, request.fd);
   if ( rc == -1)
   {
       elog ("failed to accept on http server socket\n");
       return -1;
   }
   evhttp_set_gencb(request.httpd, guestHttpSvr_vimCmdHdlr, NULL);
   
   return PASS ;
}

/* initialize the mtce http server */
int guestHttpSvr_init ( int port )
{
    int rc = PASS ;
    memset ( &guest_request, 0, sizeof(request_type));
    guest_request.port = port ;

    for ( ; ; )
    {
        rc = guestHttpSvr_setup ( guest_request );
        if ( rc == RETRY )
        {
            wlog ("%s bind failed (%d)\n", GUEST_SERVER, guest_request.fd );
        }
        else if ( rc != PASS )
        {
            elog ("%s start failed (rc:%d)\n", GUEST_SERVER, rc );
        }
        else if ( guest_request.fd > 0 )
        {
            ilog ("Listening for 'http command' messages on %s:%d\n", 
                   inet_ntoa(guest_request.addr.sin_addr), guest_request.port );
            rc = PASS ;
            break ;
        }
        if ( rc ) mtcWait_secs (5);
    }
    return ( rc ) ;
}
