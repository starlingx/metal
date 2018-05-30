/*
 * Copyright (c) 2013, 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
  * @file
  * Wind River CGTS Platform rmon HTTP Utilities.
  * 
  */

#include <time.h>
#include <iostream>
#include <stdio.h>
#include <unistd.h>    /* for .. close and usleep         */
#include <stdlib.h>    /* for .. system                   */
#include <dirent.h>    /* for config dir reading          */
#include <list>        /* for the list of conf file names */
#include <syslog.h>    /* for ... syslog                  */
#include <sys/wait.h>  /* for ... waitpid                 */
#include "rmon.h"
#include "tokenUtil.h" /* for ... tokenUtil_get_ptr     */
using namespace std;

//#include "nodeClass.h"        /* for ... maintenance class nodeLinkClass */
#include "rmonHttp.h"    /* this module header                      */
//#include "rmonJsonUtil.h"    /* Json Utilities                          */
#include "rmonApi.h"       


extern void rmonHdlr_remotelogging_handler ( struct evhttp_request *req, void *arg );
extern void rmonHdlr_ceilometer_handler( struct evhttp_request *req, void *arg );

void rmonHttpUtil_free_base ( libEvent_type & event );


static node_inv_type default_inv ;

/*****************************************************************************
 *
 * Name    : rmonHttpUtil_libEvent_init
 *
 * Purpose : Initialize the libEvent message for the HTTP request 
 *
 *****************************************************************************/
int rmonHttpUtil_libEvent_init ( libEvent_type  *ptr , 
                                       string   service, 
                                       string   ip, 
                                          int   port )
{
    ptr->type  = EVHTTP_REQ_GET ; /* request type GET/PUT/PATCH etc */

    /* Characteristics */
    ptr->ip = ip ;
    ptr->port = port ;
    ptr->hostname = "default" ;
    
    /* Controls */
    ptr->status  = FAIL  ; /* The handler must run to make this PASS */
    ptr->active  = false ;
    ptr->mutex   = false ;
    ptr->stuck   = 0     ;
    ptr->found   = false ;
    ptr->count   = 0     ;
    //ptr->stage   = 0     ;
    ptr->result  = ""    ;
    ptr->timeout = 0     ;

    /* Personality */
    ptr->service = service ;
    ptr->request = RMON_SERVICE_NONE ;

    /* Execution Data */
    ptr->entity_path.clear() ;
    ptr->entity_path_next.clear() ;
    ptr->address.clear();
    ptr->payload.clear();
    ptr->response.clear();
    ptr->user_agent.clear();

    /* Better to access a default struct than a bad pointer */
    ptr->inv_info_ptr = &default_inv ;

    /* Check for memory leaks */
    if ( ptr->base )
    {
        slog ("rmon http base memory leak avoidance (%p) fixme !!\n", ptr->base);
        event_base_free(ptr->base);
    }
    /* Create event base - like opening a socket */
    ptr->base = event_base_new();
    if ( ! ptr->base )
    {
        elog ("Failed to create '%s' libEvent (event_base_new)\n",
               ptr->service.c_str());

        return(FAIL_EVENT_BASE) ;
    }
    return (PASS);
}


void rmonHttpUtil_start_timer ( libEvent_type & event )
{
    clock_gettime (CLOCK_MONOTONIC, &event.start_ts );
}

void rmonHttpUtil_stop_timer ( libEvent_type & event )
{
    clock_gettime (CLOCK_MONOTONIC, &event.stop_ts );
}

/* ***********************************************************************
 *
 * Name       : rmonHttpUtil_free_conn
 *
 * Description: Free an event's connection memory if it exists.
 *
 * ************************************************************************/
void rmonHttpUtil_free_conn ( libEvent_type & event )
{
    if ( event.conn )
    {
        dlog ("rmond Free Connection (%p)\n", event.conn );
        evhttp_connection_free ( event.conn );
        event.conn = NULL ;
    }
    else
    {
        wlog ("rmond Already Freed Connection\n");
    }
}

/* ***********************************************************************
 *
 * Name       : rmonHttpUtil_free_base
 *
 * Description: Free an event's base memory if it exists.
 *
 * ************************************************************************/
void rmonHttpUtil_free_base ( libEvent_type & event )
{
    /* Free the base */
    if ( event.base )
    {
        dlog ("rmond Free Base (%p)\n", event.base );

        event_base_free(event.base);
        event.base = NULL ;
        if ( event.conn )
        {
            dlog ("rmond Free Connection (%p) --------- along with base\n", event.conn );
            evhttp_connection_free ( event.conn );
            event.conn = NULL ;
        }
    }
    else
    {
        wlog ("rmond Already Freed Event Base\n"); 
    }
}

/*****************************************************************************
 *
 * Name    : rmonHttpUtil_connect_new
 *
 * Purpose : generic HTTP Conect utility  
 *
 *****************************************************************************/
int rmonHttpUtil_connect_new ( libEvent_type & event )
{
    if ( event.base )
    {
        /* Open an http connection to specified IP and port */
        event.conn = evhttp_connection_base_new ( event.base, NULL,
                                                  event.ip.c_str(), 
                                                  event.port );
        if ( event.conn )
        {
            dlog("connect successfull \n"); 
            return(PASS) ;
        }
        else
        {
            elog ("Failed to create http connection (evhttp_connection_base_new)\n");
            return (FAIL_CONNECT);
        }
    }
    else
    {
        elog ("Null Event base\n");
        return (FAIL_EVENT_BASE);
    }
}

/* generic HTTP Conect utility */
int rmonHttpUtil_request_new ( libEvent_type & event,
                              void(*hdlr)(struct evhttp_request *, void *))
{
    int rc = PASS ;
    
    /* make a new request and bind the event handler to it */
    event.req = evhttp_request_new( hdlr , event.base );
    if ( ! event.req )
    {
        dlog ("call to 'evhttp_request_new' returned NULL\n");
        rc = FAIL ;
    }
    
    return (rc);
}

/* Fill in the output buffer    */
/* return of 0 or -1 are errors */
int rmonHttpUtil_payload_add ( libEvent_type & event )
{
    int rc = PASS ;
    
    /* Returns the output buffer. */ 
    event.buf = evhttp_request_get_output_buffer ( event.req );
    
    /* Check for no buffer */
    if ( ! event.buf )
    {
        elog ("evhttp_request_get_output_buffer returned null (%p)\n", event.req );
        rc = FAIL ;
    }
    else
    {
        /* write the payload into the buffer */
        rc = evbuffer_add_printf ( event.buf, "%s", event.payload.c_str());
        if ( rc == -1 )
        {
            elog ("evbuffer_add_printf returned error (-1)\n");
            rc = FAIL ;
        }
        else if ( rc == 0 )
        {
            elog ("no data added to output buffer (len=0)\n");
            rc = FAIL ;
        }
        else
        {
            rc = PASS ;
        }
    }
    return (rc);
}

/* get the output buffer length and convert it to a string that is returned */
string rmonHttpUtil_payload_len ( libEvent_type * ptr )
{
    string body_len ;
    char len_str[10] ;
    int len = evbuffer_get_length ( ptr->req->output_buffer ) ;
    if (( len == -1 ) || ( len == 0 ))
    {
        body_len = "" ;
    }
    else
    {
        memset ( &len_str[0], 0 , 10 );
        sprintf ( &len_str[0], "%d", len );
        body_len = len_str ;
        dlog3 ("%s Buffer Len:%s\n", ptr->hostname.c_str(), body_len.c_str() );
    }
    return ( body_len );
}

int rmonHttpUtil_header_add ( libEvent_type * ptr, http_headers_type * hdrs_ptr )
{
    int rc = PASS ;

    if ( hdrs_ptr->entries > MAX_HEADERS )
    {
        elog ("%s Too many headers (%d:%d)\n", 
                  ptr->hostname.c_str(), MAX_HEADERS, hdrs_ptr->entries );
        return FAIL ;
    }
    for ( int i = 0 ; i < hdrs_ptr->entries ; i++ )
    {
        /* Add the header */
        rc = evhttp_add_header( ptr->req->output_headers, 
                                hdrs_ptr->entry[i].key.c_str() ,  
                                hdrs_ptr->entry[i].value.c_str());
        if ( rc )
        {
            elog ("evhttp_add_header returned failure (%d:%s:%s)\n", rc,
                   hdrs_ptr->entry[i].key.c_str(),
                   hdrs_ptr->entry[i].value.c_str());
            rc = FAIL ;
            break ;
        }  
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name    : rmonHttpUtil_request_make
 *
 * Purpose : Make the HTTP request 
 *
 *****************************************************************************/
int rmonHttpUtil_request_make ( libEvent_type        * ptr, 
                               enum evhttp_cmd_type   type,
                                             string   path )
{
    return (evhttp_make_request( ptr->conn, ptr->req, type, path.data()));
}

/*****************************************************************************
 *
 * Name    : rmonHttpUtil_status
 *
 * Purpose : Get the status of the HTTP request 
 *
 *****************************************************************************/
int rmonHttpUtil_status ( libEvent_type & event )
{
    int rc = PASS ;

    event.status = evhttp_request_get_response_code (event.req);
    switch (event.status)
    {
        case HTTP_OK:
        case 201:
        case 202:
        case 203:
        case 204:
        {
            dlog3 ("%s HTTP_OK (%d)\n", event.hostname.c_str(), event.status );
            event.status = PASS ;
            break;
        }
        case 401:
        {
            /* Authentication error - refresh the token */
            rc = RETRY ;
            break ;
        }
        case 0:
        {
            dlog ("%s Status: 0\n", event.hostname.c_str());
            event.status = FAIL_HTTP_ZERO_STATUS ;
            rc = FAIL_HTTP_ZERO_STATUS ;
            break ;
        }
        default:
        {
            dlog ("%s Status: %d\n", event.hostname.c_str(), event.status );
            rc = event.status ;
            break;
        }
    }
    return (rc);
}



/*****************************************************************************
 *
 * Name    : rmonHttpUtil_api_request
 *
 * Purpose : Issue a HTTP REST API Request
 *
 *****************************************************************************/
#define URL_LEN 200
int rmonHttpUtil_api_request ( rmon_libEvent_enum request,
                              libEvent_type & event, 
                              string command_path )

{
    http_headers_type    hdrs ;
    enum evhttp_cmd_type type = EVHTTP_REQ_PUT ;
    int timeout     = 1    ;
    int hdr_entry   = 0    ;
    string payload  = ""   ; 
    int    rc       = FAIL ;
    void(*handler)(struct evhttp_request *, void *) = NULL ;

    if ( request == REMOTE_LOGGING_REQUEST )
    {
        /* Bind the handler for the request */
        handler = &rmonHdlr_remotelogging_handler ;

        /* The type of HTTP request */
        type = EVHTTP_REQ_GET ;

        /* set the timeout */
        timeout = HTTP_REMOTELOGGING_TIMEOUT ;
    }
   
    else if ( request == CEILOMETER_SAMPLE_CREATE )
    {
        /* Bind the handler for the request */
        handler = &rmonHdlr_ceilometer_handler ;
     
        /* The type of HTTP request */
        type = EVHTTP_REQ_POST ;

        /* set the timeout */
        timeout = HTTP_CEILOMETER_TIMEOUT ;
    }

    else
    {
        slog ("%s Unsupported Request (%d)\n", event.hostname.c_str(), request);
        return (FAIL_BAD_CASE);
    }

    /* Establish connection */
    if ( rmonHttpUtil_connect_new ( event ))
    {
        return (FAIL_CONNECT);   
    }

    /* Create request */
    if ( rmonHttpUtil_request_new ( event, handler ))
    {
        return (FAIL_REQUEST_NEW);
    }

    if ( type == EVHTTP_REQ_POST )
    {
        /* Add payload to the output buffer but only for POST request. */
        if ( rmonHttpUtil_payload_add ( event ) )
        {
            event.status = FAIL_PAYLOAD_ADD;
            return ( event.status );
        }
        if ( daemon_get_cfg_ptr()->debug_json )
        {
            if ((!string_contains(event.payload,"token")) &&
                (!string_contains(event.payload,"assword")))
            {
                jlog ("%s Payload : %s\n", event.hostname.c_str(),
                                           event.payload.c_str() );
            }
            else
            {
                jlog ("%s Payload : ... contains private content ...\n",
                          event.hostname.c_str());
            }
        }
    }

    if ( !command_path.empty() )
    {
        event.address = command_path ;
        dlog (" Address : %s\n", event.address.c_str());
    }

    /* Build the HTTP Header */
    hdrs.entry[hdr_entry].key   = "Host" ;
    hdrs.entry[hdr_entry].value = event.ip ;
    hdr_entry++;
    hdrs.entry[hdr_entry].key   = "X-Auth-Project-Id" ;
    hdrs.entry[hdr_entry].value = "admin";
    hdr_entry++;


    hdrs.entry[hdr_entry].key   = "Content-Type" ;
    hdrs.entry[hdr_entry].value = "application/json" ;
    hdr_entry++;
    hdrs.entry[hdr_entry].key   = "Accept" ;
    hdrs.entry[hdr_entry].value = "application/json" ;
    hdr_entry++;

    if ( request == CEILOMETER_SAMPLE_CREATE )
    {
        hdrs.entry[hdr_entry].key   = "User-Agent" ;
        hdrs.entry[hdr_entry].value = event.user_agent ;
        hdr_entry++;

        hdrs.entry[hdr_entry].key   = "X-Auth-Token" ;
        hdrs.entry[hdr_entry].value = tokenUtil_get_ptr()->token ;
        hdr_entry++;
    }

    hdrs.entry[hdr_entry].key   = "Connection" ;
    hdrs.entry[hdr_entry].value = "close" ;
    hdr_entry++;
    hdrs.entries = hdr_entry ;

    /* Add the headers */
    if ( rmonHttpUtil_header_add ( &event, &hdrs ))
    {
        return (FAIL_HEADER_ADD);
    }

    rc = rmonHttpUtil_request_make ( &event, type, event.address.data() );

    if ( rc == PASS )
    {
        /* Send the message with timeout */
        evhttp_connection_set_timeout(event.req->evcon, timeout);
        event_base_dispatch(event.base);
        rmonHttpUtil_free_conn ( event );
        rmonHttpUtil_free_base ( event );

        return(event.status) ;
    }
    elog ("%s Call to 'evhttp_make_request' failed (rc:%d)\n", 
              event.hostname.c_str(), rc);

    return (FAIL_MAKE_REQUEST);
} 

/*****************************************************************************
 *
 * Name    : rmonHttpUtil_receive
 *
 * Purpose : Get the HTTP request response into a libEvent object 
 *
 *****************************************************************************/
int rmonHttpUtil_receive ( libEvent_type & event )
{
    /* Send the request but don't wait for the response */
    // int rc = event_base_loop(event.base, EVLOOP_NONBLOCK) ;
    int rc = event_base_loop(event.base, EVLOOP_ONCE) ;
    switch ( rc )
    {
        case  PASS: /* 0 */
        {
            /* Set in-progress flag */
            if ( event.active == false )
            {
                /* look at the reported handler status */
                if ( event.status != PASS )
                    rc = event.status ;

                rmonHttpUtil_log_event ( event );
            }
            else
            {
                rc = RETRY ;
            }
            break ;
        }
        case  1:
        {
            dlog ("%s %s No Events Pending (1)\n", 
                      event.hostname.c_str(),
                      event.service.c_str());
            rc = FAIL ;
            break ;
        }
        case -1: 
        {
            event.active = false ;
            elog ("%s %s Failed event_base_loop (-1)\n", 
                      event.hostname.c_str(),
                      event.service.c_str());
            rc = FAIL ;
            break ;
        }
        default:
        {
            event.active = false ;
            slog ("%s %s Failed event_base_loop - Unexpected Return (%d)\n", 
                      event.hostname.c_str(),
                      event.service.c_str(), rc );
            rc = FAIL ;
            break ;
        }
    }
    return (rc);
}

/* Get the length of the json response
 * Deal with oversized messages.
 *
 * Get the length of the buffer so we can 
 * allocate one big enough to copy too.
 */
int rmonHttpUtil_get_length ( libEvent_type & event )
{
    event.response_len = evbuffer_get_length (event.req->input_buffer);
    if ( event.response_len == 0 )
    {
        dlog ("%s %s Request Failed - Zero Length Response\n",
                event.hostname.c_str(),
                event.service.c_str());
        event.status = FAIL_JSON_ZERO_LEN ;
    }
    else
    {
        event.status = PASS ;
    }
    return ( event.status );
}

/* Load the response string into the event struct */
int rmonHttpUtil_get_response ( libEvent_type & event )
{
    if ( rmonHttpUtil_get_length ( event ) == PASS )
    {
        size_t real_len      ;

        /* Get a stack buffer, zero it, copy to it and terminate it */
        char * stack_buf_ptr = (char*)malloc (event.response_len+1);
        memset ( stack_buf_ptr, 0, event.response_len+1 );
        real_len = evbuffer_remove( event.req->input_buffer, stack_buf_ptr, 
                                event.response_len);

        if ( real_len != event.response_len )
        {
            wlog ("%s %s Length differs from removed length (%ld:%ld)\n",
                      event.hostname.c_str(),
                      event.service.c_str(),
                      event.response_len, 
                      real_len );
        }

        /* Terminate the buffer , this is where the +1 above is required. 
         * Without it there is memory corruption reported by Linux */
         *(stack_buf_ptr+event.response_len) = '\0';

        /* Store the response */
        event.response = stack_buf_ptr ;
        dlog ("%s Response: %s\n", event.hostname.c_str(), event.response.c_str());

        free (stack_buf_ptr);
    }
    return ( event.status );
}

/*****************************************************************************
 *
 * Name    : rmonHttpUtil_log_event 
 *
 * Purpose : Log the HTTP event 
 *
 *****************************************************************************/
void rmonHttpUtil_log_event ( libEvent_type & event )
{
    string event_sig = daemon_get_cfg_ptr()->debug_event ;

    dlog3 ("Event Signature (%s)\n", event_sig.c_str());
    if ( !event_sig.compare(event.service) || (event.status))
    {
        if ( !event.address.empty() )
        {
            log_event ("%s %s Address : %s\n", event.hostname.c_str(), event_sig.c_str(), event.address.c_str());
        }
        if (!event.payload.empty())
        {
            if ((!string_contains(event.payload,"token")) && 
                (!string_contains(event.payload,"assword")))
            {
                log_event ("%s %s Payload : %s\n", event.hostname.c_str(), event_sig.c_str(), event.payload.c_str());
            }
            else
            {
                log_event ("%s %s Payload : ... contains private content ...\n", event.hostname.c_str(), event_sig.c_str());
            }
        }
        if ( !event.response.empty() )
        {
            if ((!string_contains(event.payload,"token")) && 
                (!string_contains(event.payload,"assword")))
            {
                log_event ("%s %s Response: %s\n", event.hostname.c_str(), event_sig.c_str(), event.response.c_str());
            }
            else
            {
                log_event ("%s %s Response: ... contains private content ...\n", event.hostname.c_str(), event_sig.c_str());
            }
        }
    }
}
