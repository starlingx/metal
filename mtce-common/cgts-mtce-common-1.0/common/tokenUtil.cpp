/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud 
  * Common Keystone Token Authentication Utility API
  *
  *     tokenUtil_handler   - handle response
  *     tokenUtil_get_token - refresh the static token
  *     tokenUtil_get_ptr   - get a pointer to the static token
  *
  */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "tok"

#include <time.h>           /* for ... time_t, struct tm               */
#include "daemon_ini.h"     /* for ... MATCH macro                     */

#include "nodeUtil.h"       /* for ... node utilituies                 */
#include "nodeBase.h"       /* for ... node_inv_type                   */
#include "jsonUtil.h"       /* for ... Json utilities                  */
#include "tokenUtil.h"      /* for ... this module header              */

#define GET_SERVICE_LIST_LABEL ((const char *)"OS-KSADM:services")

/* The static token used for authentication by any
 * daemon that includes this module */
static keyToken_type      __token__ ;
keyToken_type * tokenUtil_get_ptr   ( void ) { return &__token__ ; };
keyToken_type   tokenUtil_get_token ( void ) { return __token__ ; };

void tokenUtil_log_refresh ( void )
{
    if ( __token__.refreshed == true )
    {
        __token__.refreshed = false ;
        ilog ( "Token Refresh: [%s] [Expiry: %s %s]\n",
                    md5sum_string ( __token__.token).c_str(),
                                    __token__.expiry.substr(0,10).c_str(),
                                    __token__.expiry.substr(11,8).c_str());
    }
}

/* Handle refreshing the authentication token */
int tokenUtil_token_refresh ( libEvent & event, string hostname )
{
    struct tm tokenExpiry;     // given token expired time (UTC)
    time_t cTime = time(NULL); // current time (UTC)
    double diffTime = 0;

    if ( event.status != PASS )
    {
        event.status = tokenUtil_new_token( event, hostname ); 
    }
    else
    {
        strptime( __token__.expiry.c_str(), "%Y-%m-%dT%H:%M:%S", &tokenExpiry );

        /* Get a new authentication token if the given token is about to expire */
        diffTime = difftime( mktime( &tokenExpiry ), cTime );
        if ( diffTime <= STALE_TOKEN_DURATION )
        {
            ilog ("The given token will expire in %f seconds\n", diffTime);
            event.status = tokenUtil_new_token( event, hostname );
        }
    }
    return (event.status);
}

string _get_ip ( void )
{
    string ip = "" ;
    daemon_config_type * cfg_ptr = daemon_get_cfg_ptr() ;
    if (( cfg_ptr->keystone_auth_host ) && ( strlen (cfg_ptr->keystone_auth_host) >= MIN_CHARS_IN_IP_ADDR ))
    {
       ip = cfg_ptr->keystone_auth_host;
    }
    else
    {
       string my_hostname = "" ;
       string my_local_ip = "" ;
       string my_float_ip = "" ;
       get_ip_addresses ( my_hostname, my_local_ip, my_float_ip ) ;
       if ( !my_float_ip.empty() )
       {
           ip = my_float_ip ;
           ilog ("defaulting keystone auth host to floating IP:%s\n", ip.c_str());
       }
       else
           ip = "localhost" ;

    }
    return (ip);
}

string _get_keystone_prefix_path ( )
{
  string prefix_path = "";
  daemon_config_type * cfg_ptr = daemon_get_cfg_ptr() ;

  if ( cfg_ptr->keystone_prefix_path)
  {
    prefix_path = cfg_ptr->keystone_prefix_path;
  }

  return (prefix_path);
}

/* http://localhost:5000/v2.0/tokens -X POST -H "Content-Type: application/json" 
 *                                            -H "Accept: application/json" 
 *                                            -H "User-Agent: python-keyclient"
 *                                            -H "Connection: close"
 *
 *    { 
 *        "auth": 
 *        {
 *            "tenantName": "admin", 
 *            "passwordCredentials": 
 *            {
 *                "username": "admin", 
 *                "password": "password"
 *            }
 *        }
 *    }
 *
 */

/*******************************************************************
 *
 * Name       : tokenUtil_handler
 *
 * Description: The handles the keystone get request 
 *              responses for the following messages
 *
 *     KEYSTONE_GET_TOKEN,        
 *     KEYSTONE_GET_SERVICE_LIST
 *     KEYSTONE_GET_ENDPOINT_LIST 
 *
 *******************************************************************/
int tokenUtil_handler ( libEvent & event )
{
    jsonUtil_auth_type info  ;
    
    string hn = event.hostname ;
    int    rc = event.status   ;

    keyToken_type * token_ptr = tokenUtil_get_ptr ( ) ;

    if ( event.status )
    {
        elog ( "%s Token Request Failed - Error Code (%d) \n", hn.c_str(), event.status );
    }
    if ( event.request == KEYSTONE_GET_TOKEN )
    {
        /* get the token from response header*/
        struct evkeyvalq *header_ptr = evhttp_request_get_input_headers(event.req);
        const char * header_token_ptr  = evhttp_find_header (header_ptr, MTC_JSON_AUTH_ID);
        if ( !header_token_ptr )
        {
            rc = FAIL_JSON_PARSE ;
            elog ( "%s Token Request Failed - no token in header\n", hn.c_str());
        }
        std::string token_str(header_token_ptr);

        if ( jsonApi_auth_load ( hn, (char*)event.response.data(), info ) )
        {
            rc = FAIL_JSON_PARSE ;
            elog ( "%s Token Request Failed - Json Parse Error\n", hn.c_str());
        }
        else
        {
            jlog ("%s Token Exp: %s\n", hn.c_str(), info.expiry.c_str() );
            jlog ("%s Admin URL: %s\n" ,hn.c_str(), info.adminURL.c_str() );
            jlog ("%s Token Len: %ld\n",hn.c_str(), token_str.length() );
            token_ptr->issued = info.issued   ;
            token_ptr->expiry = info.expiry   ;
            token_ptr->token  = token_str ;
            token_ptr->url    = info.adminURL ;
            token_ptr->refreshed = true ;
        }
    }
    else if ( event.request == KEYSTONE_GET_ENDPOINT_LIST )
    {
        /* Response: {"endpoints": 
         * [{
         *    "service_id": "067...b60", 
         *    "region": "RegionOne",
         *    "enabled": true, 
         *    "id": "410ab64a37114a418d188f450300aa48", 
         *    "interface": "internal",
         *    ""links": {
         *         "self": "http://192.168.204.2:5000/v3/endpoints/410ab64a37114a418d188f450300aa48"
         *     }
         *    "url": "http://192.168.204.2:8777",
         *    { ... }]}
         *
         * Output:
         *
         *  event.admin_url    = "http://192.168.204.2:8777" ;
         *  event.internal_url = "http://192.168.204.2:8777" ;
         *  event.public_url   = "http://10.10.10.2:8777"    ;
         *
         */
        list<string> endpoint_list ; endpoint_list.clear() ;
        rc = jsonUtil_get_list ( (char*)event.response.data(), (char*)event.label.data(), endpoint_list );
        if ( rc == PASS )
        {
            std::list<string>::iterator iter_ptr ;
            string interface_type;
            int rc1 = FAIL, rc2 = FAIL, rc3 = FAIL;
            for ( iter_ptr  = endpoint_list.begin();
                  iter_ptr != endpoint_list.end();
                  iter_ptr++ )
            {
                if ( jsonUtil_get_key_val ( (char*)iter_ptr->data(), "service_id", event.value ) == PASS )
                {
                    if ( !event.value.compare(event.information) )
                    {
                        rc = jsonUtil_get_key_val ( (char*)iter_ptr->data(), MTC_JSON_AUTH_INTERFACE, interface_type);
                        if ( rc)
                        {
                            wlog ("%s '%s' failed to get interface type from endpoint list (%d)\n",
                                   event.hostname.c_str(),
                                   event.information.c_str(), rc);
                         }
                        else if ( interface_type == "admin" )
                        {
                            rc1 = jsonUtil_get_key_val ( (char*)iter_ptr->data(), MTC_JSON_AUTH_URL, event.admin_url );
                        }
                        else if ( interface_type == "internal" )
                        {
                            rc2 = jsonUtil_get_key_val ( (char*)iter_ptr->data(), MTC_JSON_AUTH_URL, event.internal_url );
                        }
                        else if ( interface_type == "public" )
                        {
                            rc3 = jsonUtil_get_key_val ( (char*)iter_ptr->data(), MTC_JSON_AUTH_URL, event.public_url );
                        }
                    }
                    else
                    {
                       wlog ("%s '%s' service endpoint not found\n", event.hostname.c_str(), event.information.c_str());
                    }
                }
                else
                {
                   elog ("%s Parse service endpoint list failed (rc:%d)\n", event.hostname.c_str(), rc);
                   elog ("%s Response: %s\n", event.hostname.c_str(), event.response.c_str() );
                   event.status = rc ;
                }
            }

            if ( rc1 | rc2 | rc3 )
            {
                wlog ("%s '%s' one or mode endpoint parse failure (%d:%d:%d)\n",
                event.hostname.c_str(),
                event.information.c_str(), rc1, rc2, rc3 );
                event.status = FAIL_KEY_VALUE_PARSE ;
            }
            else
            {
                ilog ("%s keystone '%s' service endpoint    admin url: %s\n",
                       event.hostname.c_str(),
                       event.information.c_str(),
                       event.admin_url.c_str());
                ilog ("%s keystone '%s' service endpoint   public url: %s\n",
                       event.hostname.c_str(),
                       event.information.c_str(),
                       event.public_url.c_str());
                ilog ("%s keystone '%s' service endpoint internal url: %s\n",
                       event.hostname.c_str(),
                       event.information.c_str(),
                       event.internal_url.c_str());
                       event.status = PASS ;
            }
        }
        else
        {
        wlog ("%s '%s' service not found using '%s' label\n",
               event.hostname.c_str(),
               event.information.c_str(),
               event.label.c_str());
        }
        return (event.status);
    }
    else if ( event.request == KEYSTONE_GET_SERVICE_LIST )
    {
        /* Response: {"services":
           [
           {"id": "49fc93c32d734c78a9d9f975c22f1703", "type": "network", "name": "neutron", "description": "Neutron Networking Service"}, 
           {"id": "0900a982ff114e7ba62c317443b43362", "type": "metering", "name": "ceilometer", "description": "Openstack Metering Service"}, 
           {"id": "97940d057bec47cc989cc190b4293aad", "type": "ec2", "name": "nova_ec2", "description": "EC2 Service"}, 
           {"id": "7ce51d481d024b1f8b80bb1127b80752", "type": "volumev2", "name": "cinderv2", "description": "Cinder Service v2"}, 
           {"id": "3ed8ae6ccf85445ebdf2e93bbce9f5fb", "type": "computev3", "name": "novav3", "description": "Openstack Compute Service v3"},
           {"id": "564bf663693c49cf9fee24e2fdbdba3a", "type": "identity", "name": "keystone", "description": "OpenStack Identity Service"}, 
           {"id": "7e0cadd9db444342b7fddb0005c4ce5f", "type": "platform", "name": "sysinv", "description": "SysInv Service"},
           {"id": "be7afccda91c4ba19ac2e53f613c6b63", "type": "volume", "name": "cinder", "description": "Cinder Service"}, 
           {"id": "edf60a37f4f84b9baba215d8346b814f", "type": "image", "name": "glance", "description": "Openstack Image Service"}, 
           {"id": "0673921c7b094178989455a5b157fb60", "type": "patching", "name": "patching", "description": "Patching Service"}, 
           {"id": "d7621026166f43c0a1c74e0e9784cce6", "type": "compute", "name": "nova", "description": "Openstack Compute Service"},
           {"id": "aef585311e3144e0b1267ea25dc40b70", "type": "orchestration", "name": "heat", "description": "Openstack Orchestration Service"}, 
           {"id": "0a67bc174fa0469e9b837daf23d83aaf", "type": "cloudformation", "name": "heat-cfn", "description": "Openstack Cloudformation Service"}
           ]} */
        
        bool   found = false ;
        list<string> service_list ; service_list.clear() ;
        rc = jsonUtil_get_list ( (char*)event.response.data(), (char*)event.label.data(), service_list );
        if ( rc == PASS )
        {
            std::list<string>::iterator iter_ptr ;
            
            for ( iter_ptr = service_list.begin() ;
                  iter_ptr != service_list.end() ;
                  iter_ptr++ )
            {
                if ( jsonUtil_get_key_val ( (char*)iter_ptr->data(), "name", event.value ) == PASS )
                {
                    if ( !event.value.compare(event.information) )
                    {
                        if ( jsonUtil_get_key_val ( (char*)iter_ptr->data(), "id", event.result ) == PASS )
                        {
                            found = true ;
                            ilog ("%s '%s' service uuid is '%s'\n", 
                                       event.hostname.c_str(), 
                                       event.information.c_str(),
                                       event.result.c_str());
                            break ;
                        }
                        else
                        {
                            wlog ("%s '%s' service uuid not found\n", 
                                       event.hostname.c_str(), 
                                       event.information.c_str());
                            event.status = FAIL_KEY_VALUE_PARSE ;
                        }
                    }
                }
                else
                {
                    wlog ("%s '%s' service not found\n", 
                              event.hostname.c_str(), 
                              event.information.c_str());
                }
            }
        }
        else
        {
            elog ("%s Parse service list failed (rc:%d)\n", event.hostname.c_str(), rc);
            wlog ("%s Response: %s\n", event.hostname.c_str(), event.response.c_str() );
            event.status = rc ;
        }

        if ( found == true )
        {
            event.status = PASS ;
        }
        else
        {
            wlog ("%s '%s' service not found using '%s' label\n", 
                      event.hostname.c_str(), 
                      event.information.c_str(), 
                      event.label.c_str());
            event.status = FAIL_NOT_FOUND ;
        }
        return (event.status);
    }
    else
    {
        wlog ("%s Keystone Request Failed - Unsupported Request (%d)\n", hn.c_str(), event.request );
    }

    /* Check for a response string */
    if ( token_ptr->token.empty() )
    {
        elog ("%s Failed to get token\n", hn.c_str());
        rc = FAIL_TOKEN_GET;
    }

    /* Check for Key URL */
    else if ( token_ptr->url.empty() )
    {
        elog ("%s Failed to get token URL\n", hn.c_str());
        rc = FAIL_TOKEN_URL;
    }
    else
    {
        dlog ("%s Token Refresh O.K.\n", event.hostname.c_str());
    }
    return (rc);
}

void tokenUtil_fail_token ( void )
{
    __token__.token.replace ( 8, 8, "EEEEEEEE" );
    slog ("Corrupting Token: %s\n",__token__.token.c_str());
}

/* fetches an authorization token as a blocking request */
int tokenUtil_new_token ( libEvent & event, string hostname )
{
    ilog ("%s Requesting Authentication Token\n", hostname.c_str());

    httpUtil_event_init ( &event,
                           hostname, 
                           "tokenUtil_new_token", 
                           _get_ip(),
                           daemon_get_cfg_ptr()->keystone_port);

    event.hostname = _hn ();
    
    dlog ("%s fetching new token\n", event.hostname.c_str());

    event.prefix_path = _get_keystone_prefix_path();
    event.blocking    = true ;
    event.request     = KEYSTONE_GET_TOKEN ;
    event.operation   = "get new" ;
    event.type        = EVHTTP_REQ_POST ;
    event.timeout     = HTTP_TOKEN_TIMEOUT ;
    event.handler     = &tokenUtil_handler ;
    
    return ( httpUtil_api_request ( event ));
}

/* returns the uuid for the specified keystone service */
string tokenUtil_get_svc_uuid ( libEvent & event, string service_name )
{
    httpUtil_event_init ( &event,
                           service_name, 
                           "tokenUtil_get_svc_uuid", 
                           _get_ip(),
                           5000 ) ; // get_keystone_admin_port() ;
    
    event.hostname = _hn() ;

    /* The type of HTTP request */
    event.type = EVHTTP_REQ_GET ;

    /* set the timeout */
    event.timeout = HTTP_KEYSTONE_GET_TIMEOUT ;

    event.prefix_path = _get_keystone_prefix_path() ;
    event.blocking    = true ;
    event.request     = KEYSTONE_GET_SERVICE_LIST ;
    event.operation   = "get service list" ;
    event.handler     = &tokenUtil_handler ;
    event.information = service_name ;
    event.label       = "services" ;
    event.token.url = "/v3/services" ;
    event.address   = event.token.url;

    if ( httpUtil_api_request ( event ) != PASS )
    {
        elog ("%s service name fetch failed\n", service_name.c_str() );
    }
    return ( event.result );
}

/* returns the endpoint string for the specified service uuid */
int tokenUtil_get_endpoints ( libEvent & event, string service_uuid )
{
    httpUtil_event_init ( &event,
                           service_uuid, 
                           "tokenUtil_get_endpoints", 
                           _get_ip(),
                           5000 ); // get_keystone_admin_port();

    event.hostname = _hn() ;

    /* The type of HTTP request */
    event.type = EVHTTP_REQ_GET ;

    /* set the timeout */
    event.timeout = HTTP_KEYSTONE_GET_TIMEOUT ;

    event.prefix_path = _get_keystone_prefix_path() ;
    event.blocking    = true ;
    event.request     = KEYSTONE_GET_ENDPOINT_LIST ;
    event.operation   = "get endpoint list" ;
    event.handler     = &tokenUtil_handler ;
    event.information = service_uuid ;
    event.label       = "endpoints" ;
    event.token.url = "/v3/endpoints" ;
    /* get the endpoints by service uuid*/
    event.token.url.append("?service_id=");
    event.token.url.append(service_uuid.data());
    event.address   = event.token.url;

    if ( httpUtil_api_request ( event ) != PASS )
    {
        elog ("%s service uuid fetch failed\n", service_uuid.c_str() );
    }
    return ( event.status );
}

int keystone_config_handler ( void * user, 
                        const char * section,
                        const char * name,
                        const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "keystone_auth_host"))
    {
        /* Read this into a config_ptr parameter */
        config_ptr->keystone_auth_host = strdup(value);
        ilog("Keystone IP : %s\n", config_ptr->keystone_auth_host );
    }
    else if (MATCH("agent", "keystone_auth_port"))
    {
        config_ptr->keystone_port = atoi(value);
        dlog("Keystone Port : %d\n", config_ptr->keystone_port );
    }
    else if (MATCH("agent", "keystone_auth_uri"))
    {
        /* Read this into a config_ptr parameter.
         * Note, if keystone_auth_uri is defined, it will take
         * precedence over keystone_auth_host, and auth_port.
         */
        if ( value != NULL && (strlen(value) > 0))
        {
            if ( strcmp(value,"undef") != 0)
            {
                config_ptr->keystone_auth_uri = strdup(value);
                ilog("Mtce Keystone auth uri : %s\n",
                     config_ptr->keystone_auth_uri);
            }
        }
    }
    else if (MATCH("agent", "keyring_directory"))
    {
	    config_ptr->keyring_directory = strdup(value);
        ilog("Keyring Directory : %s\n", config_ptr->keyring_directory );
    }
    else if (MATCH("agent", "keystone_auth_username"))
    {
	    config_ptr->keystone_auth_username = strdup(value);
        ilog("Mtce Keystone username : %s\n", 
             config_ptr->keystone_auth_username );
    }
    else if (MATCH("agent", "keystone_auth_pw"))
    {
	    config_ptr->keystone_auth_pw = strdup(value);
        dlog("Mtce Keystone pw : %s\n", 
             config_ptr->keystone_auth_pw );
    }
    else if (MATCH("agent", "keystone_auth_project"))
    {
	    config_ptr->keystone_auth_project = strdup(value);
        ilog("Mtce Keystone project : %s\n", 
             config_ptr->keystone_auth_project );
    }
    else if (MATCH("agent", "keystone_user_domain"))
    {
	    config_ptr->keystone_user_domain = strdup(value);
        ilog("Mtce Keystone user domain : %s\n", 
             config_ptr->keystone_user_domain );
    }
    else if (MATCH("agent", "keystone_project_domain"))
    {
	    config_ptr->keystone_project_domain = strdup(value);
        ilog("Mtce Keystone project domain : %s\n",
             config_ptr->keystone_project_domain );
    }
    else if (MATCH("agent", "keystone_region_name")) // region_name=RegionOne
    {
        config_ptr->keystone_region_name = strdup(value);
        ilog("Region Name : %s\n", config_ptr->keystone_region_name );
    }
    else if (MATCH("agent", "ceilometer_port"))
    {
        config_ptr->ceilometer_port = atoi(value);
        dlog("Ceilometer Port : %d\n", config_ptr->ceilometer_port );
    }
    return (PASS);
}
