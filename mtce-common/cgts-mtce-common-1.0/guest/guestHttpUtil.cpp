/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
  * @file
  * Wind River CGTS Platform Controller Maintenance HTTP Utilities.
  *
  * Public Interfaces:
  *
  */

#include <iostream>
#include <evhttp.h>
#include <list>

using namespace std;

#include "httpUtil.h"       /* for ... common http utilities           */
#include "jsonUtil.h"       /* for ... Json Utilities                  */
#include "nodeUtil.h"       /* for ... Node Utilities                  */

#include "guestClass.h"     /* for ... maintenance class nodeLinkClass */
#include "guestHttpUtil.h"  /* for ... this module header              */
#include "guestVimApi.h"    /* for ... guestVimApi_Handler             */

/* Module init */
void guestHttpUtil_init ( void )
{
    return ;
}

/* Module close */
void guestHttpUtil_fini ( void )
{
    return ;
}

/* *********************************************************************
 *
 * Name       : guestHttpUtil_status
 *
 * Description: Extracts and returns the HTTP execution status
 *
 * *********************************************************************/

int guestHttpUtil_status ( libEvent & event )
{
    int rc = PASS ;

    if ( !event.req )
    {
        elog ("%s Invalid request\n", event.hostname.length() ? event.hostname.c_str() : "unknown" );
        return (FAIL_UNKNOWN_HOSTNAME);
    }
    event.status = event.http_status = evhttp_request_get_response_code (event.req);
    switch (event.status)
    {
        case HTTP_OK:
        case 201:
        case 202:
        case 203:
        case 204:
        {
            dlog ("%s HTTP_OK (%d)\n", event.hostname.c_str(), event.status );
            event.status = PASS ;
            break;
        }
        /* Authentication error - refresh the token */
        case 401:
        {
            rc = FAIL_AUTHENTICATION ;
            break ;
        }
        case 0:
        {
            wlog ("%s Status:0 - failed to connect to '%s:%d'\n",
                      event.hostname.c_str(), event.ip.c_str(), event.port);
            event.status = FAIL_HTTP_ZERO_STATUS ;
            rc = FAIL_HTTP_ZERO_STATUS ;
            break ;
        }
        default:
        {
            dlog3 ("%s Status: %d\n", event.hostname.c_str(), event.status );
            rc = event.status ;
            break;
        }
    }
    return (rc);
}

/* ***********************************************************************
 *
 * Name       : guestHttpUtil_api_req
 *
 * Description: Makes an HTTP request based on all the info
 *              in the supplied libEvent.
 *
 * ************************************************************************/
int guestHttpUtil_api_req ( libEvent & event )

{
    http_headers_type hdrs  ;
    bool has_payload = false;
    int hdr_entry    = 0    ;
    int    rc        = FAIL ;
    void(*handler)(struct evhttp_request *, void *) = NULL ;
 
    /* Bind the unlock handler */
    handler = &guestVimApi_Handler;

    /* set the timeout */
    event.timeout = HTTP_VIM_TIMEOUT ;

    /* Check for memory leaks */
    if ( event.base )
    {
        slog ("%s http base memory leak avoidance (%p) fixme !!\n",
                  event.log_prefix.c_str(), event.base );
        // event_base_free(event.base);
    }

    /* Allocate the base */
    event.base = event_base_new();
    if ( event.base == NULL )
    {
        elog ("%s No Memory for Request\n", event.log_prefix.c_str());
        return ( FAIL_EVENT_BASE );
    }
    
    /* Establish connection */
    else if ( httpUtil_connect ( event ))
    {
        return (FAIL_CONNECT);
    }

    else if ( httpUtil_request ( event, handler ))
    {
        return (FAIL_REQUEST_NEW);
    }

    jlog ("%s Address : %s\n", event.hostname.c_str(), event.token.url.c_str());

    if ((( event.type != EVHTTP_REQ_GET ) && ( event.type != EVHTTP_REQ_DELETE )) ||
         ( event.request == VIM_HOST_STATE_QUERY ))
    {
        has_payload = true ;

        /* Add payload to the output buffer but only for PUT, POST and PATCH requests */
        if ( httpUtil_payload_add ( event ))
        {
            return (FAIL_PAYLOAD_ADD);
        }
        jlog ("%s Payload : %s\n", event.hostname.c_str(), 
                                   event.payload.c_str() );
    }

    /* Convert port to a string */
    char port_str[10] ;
    sprintf ( port_str, "%d", event.port );
    
    /* Build the HTTP Header */
    hdrs.entry[hdr_entry].key   = "Host" ;
    hdrs.entry[hdr_entry].value = event.ip ;
    hdrs.entry[hdr_entry].value.append(":") ;
    hdrs.entry[hdr_entry].value.append(port_str);
    hdr_entry++;

    if ( has_payload == true )
    {
        hdrs.entry[hdr_entry].key   = "Content-Length" ;
        hdrs.entry[hdr_entry].value = httpUtil_payload_len ( &event );
        hdr_entry++;
    }

    hdrs.entry[hdr_entry].key   = "User-Agent" ;
    hdrs.entry[hdr_entry].value = "guest-agent/1.0" ;
    hdr_entry++;
           
    hdrs.entry[hdr_entry].key   = "Content-Type" ;
    hdrs.entry[hdr_entry].value = "application/json" ;
    hdr_entry++;

    hdrs.entry[hdr_entry].key   = "Connection" ;
    hdrs.entry[hdr_entry].value = "close" ;
    hdr_entry++;
    hdrs.entries = hdr_entry ;

    /* Add the headers */
    if ( httpUtil_header_add ( &event, &hdrs ))
    {
        return (FAIL_HEADER_ADD);
    }
    
    event.address = event.token.url ;

    rc = evhttp_make_request ( event.conn, event.req, event.type, event.token.url.data());
    if ( rc == PASS )
    {
        evhttp_connection_set_timeout(event.req->evcon, event.timeout);

        /* Default to retry for both blocking and non-blocking command */
        event.status = RETRY ;
        event.log_prefix = event.hostname ;
        event.log_prefix.append (" ");
        event.log_prefix.append (event.service) ;
        event.log_prefix.append (" ");
        event.log_prefix.append (event.operation) ;
        jlog2 ("%s Requested (blocking) (to:%d)\n", event.log_prefix.c_str(), event.timeout);
        
        /* Send the message with timeout */
        event_base_dispatch(event.base);
        
        httpUtil_free_conn ( event );
        httpUtil_free_base ( event );

        return(event.status) ;
    }
    elog ("%s Call to 'evhttp_make_request' failed (rc:%d)\n", 
              event.hostname.c_str(), rc);

    return (FAIL_MAKE_REQUEST);
}
