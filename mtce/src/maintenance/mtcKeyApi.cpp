/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance
  * Authentication Utility API
  *
  *     mtcKeyApi_get_token
  *       _key_POST_request         - Request a authentication token
  *          jsonApi_auth_request
  *          mtcHttpUtil_connect_new
  *          mtcHttpUtil_request_new
  *          mtcHttpUtil_header_add
  *          mtcHttpUtil_request_make
  *          evhttp_connection_set_timeout
  *          event_base_dispatch
  *
  *    _key_POST_handler       - called by libevent like an interrupt handler
  *       evbuffer_remove       - reads the response data out of da resp buffer
  *       jsonApi_auth_load  - extract the data we want from resp json string
  *          tokenid            - load data: the 3604 byte authentication token
  *          adminURL           - load data: the key address
  *          issued             - load data: can use this later so that we 
  *          expiry             - load data: don't have to keep requesting tokens
  *       event_base_loopbreak  - end the interrupt handler 
*/

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "key"

#include "nodeClass.h"      /* for ... maintenance class nodeLinkClass */
#include "nodeUtil.h"
#include "httpUtil.h"       /* for ... libEvent                        */
#include "mtcKeyApi.h"      /* for ... this module header              */
#include "jsonUtil.h"       /* for ... Json utilities                  */

/* Token info is stored in the common public
 * area of the maintenance nodelinkClass structure */

/* http://localhost:5000/v2.0/tokens -X POST -H "Content-Type: application/json" 
 *                                            -H "Accept: application/json" 
 *                                            -H "User-Agent: python-keyclient"
 *                                            -H "Connection: close"
 *
 *    { 
 *        "auth": 
 *        {
 *            "tenantName": "services", 
 *            "passwordCredentials": 
 *            {
 *                "username": "mtce", 
 *                "password": "password"
 *            }
 *        }
 *    }
 *
 */
int throttle = 0 ;

/* The handles the keystone POST request's response message */
int mtcKeyApi_handler ( libEvent & event )
{
    jsonUtil_auth_type info  ;
    string hn = event.hostname ;
    int rc = PASS ;

    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
 
    /* Copy the token info into the static libEvent tokenEvent struct */ 
    obj_ptr->tokenEvent = event ;

    if ( event.status )
    {
        rc = obj_ptr->tokenEvent.status ;
        elog ( "%s Token Request Failed (%d) \n", hn.c_str(), rc );
    }
    else if ( jsonApi_auth_load ( hn, (char*)obj_ptr->tokenEvent.response.data(), info ) )
    {
        rc = obj_ptr->tokenEvent.status = FAIL_JSON_PARSE ;
        elog ( "%s Token Request Failed - Json Parse Error\n", hn.c_str());
    }
    else
    {
        jlog ("%s Token Exp: %s\n", hn.c_str(), info.expiry.c_str() );
        jlog ("%s Admin URL: %s\n" ,hn.c_str(), info.adminURL.c_str() );
        jlog ("%s Token Len: %ld\n",hn.c_str(), info.tokenid.length() );
        obj_ptr->tokenEvent.token.issued = info.issued   ;
        obj_ptr->tokenEvent.token.expiry = info.expiry   ;
        obj_ptr->tokenEvent.token.token  = info.tokenid  ;
        obj_ptr->tokenEvent.token.url    = info.adminURL ;
        obj_ptr->tokenEvent.status = PASS ;
        if ( obj_ptr->token_refresh_rate )
        {
            ilog ( "Token Refresh: [%s] [Expiry: %s %s]\n",
                    md5sum_string ( obj_ptr->tokenEvent.token.token).c_str(),
                                    obj_ptr->tokenEvent.token.expiry.substr(0,10).c_str(),
                                    obj_ptr->tokenEvent.token.expiry.substr(11,8).c_str());
        }
    }

    /* Check for a response string */
    if ( obj_ptr->tokenEvent.token.token.empty() )
    {
        elog ("%s Failed to get token\n",
                  obj_ptr->tokenEvent.hostname.c_str());
        rc = FAIL_TOKEN_GET;
    }

    /* Check for Key URL */
    else if ( obj_ptr->tokenEvent.token.url.empty() )
    {
        elog ("%s Failed to get token URL\n",
                  obj_ptr->tokenEvent.hostname.c_str());
        rc = FAIL_TOKEN_URL;
    }
    else
    {
        dlog ("%s Token Refresh O.K.\n", obj_ptr->tokenEvent.hostname.c_str());
    }
    return (rc);
}

void corrupt_token ( keyToken_type & key )
{
    key.token.replace ( 800, 50, "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE" );
}

/* fetches an authorization token as a blocking request */
int nodeLinkClass::mtcKeyApi_get_token ( string hostname )
{
    mtcHttpUtil_event_init ( &this->tokenEvent,
                              hostname,
                              "mtcKeyApi_get_token",
                              hostUtil_getServiceIp   ( SERVICE_TOKEN ),
                              hostUtil_getServicePort ( SERVICE_TOKEN ));

    this->tokenEvent.prefix_path = hostUtil_getPrefixPath();
    this->tokenEvent.blocking  = true ;
    this->tokenEvent.request   = KEYSTONE_TOKEN ;
    this->tokenEvent.operation = KEYSTONE_SIG ;

    this->tokenEvent.token.token.clear() ;
    this->tokenEvent.token.url.clear();
    this->tokenEvent.token.issued.clear();
    this->tokenEvent.token.expiry.clear();

    ilog ("%s Prefix path: %s\n", hostname.c_str(), this->tokenEvent.prefix_path.c_str() );
    return ( mtcHttpUtil_api_request ( this->tokenEvent ));
}

/* fetches an authorization token and key URL and UUID info */
int nodeLinkClass::mtcKeyApi_refresh_token ( string hostname )
{
    GET_NODE_PTR(hostname);
    mtcHttpUtil_event_init ( &node_ptr->httpReq,
                              hostname,
                              "mtcKeyApi_refresh_token",
                              hostUtil_getServiceIp   ( SERVICE_TOKEN ),
                              hostUtil_getServicePort ( SERVICE_TOKEN ));

    node_ptr->httpReq.prefix_path = hostUtil_getPrefixPath();
    node_ptr->httpReq.hostname    = hostname       ;
    node_ptr->httpReq.uuid        = node_ptr->uuid ;
    node_ptr->httpReq.request     = KEYSTONE_TOKEN ;
    node_ptr->httpReq.operation   = KEYSTONE_SIG   ;
    node_ptr->httpReq.max_retries = 3              ;
    node_ptr->httpReq.cur_retries = 0              ;

    node_ptr->httpReq.token.token.clear() ;
    node_ptr->httpReq.token.url.clear();
    node_ptr->httpReq.token.issued.clear();
    node_ptr->httpReq.token.expiry.clear();

    ilog ("%s Prefix path: %s\n", hostname.c_str(), this->tokenEvent.prefix_path.c_str() );
    return(this->workQueue_enqueue ( node_ptr->httpReq));
}
