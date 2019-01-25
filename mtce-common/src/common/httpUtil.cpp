/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
  * @file
  * Wind River CGTS Platform Common HTTP utility Module
  *
  */

#include <stdlib.h>

using namespace std;

#include "httpUtil.h"
#include "jsonUtil.h"
#include "tokenUtil.h"  /* for ... tokenUtil_handler          */
#include "nodeUtil.h"   /* for ... string_contains            */
#include "timeUtil.h"   /* for ... time_debug_type            */
#include "keyClass.h"   /* for ... add_key, del_key */ 

static keyClass keyValObject ;
static char rest_api_filename[MAX_FILENAME_LEN];
static char rest_api_log_str [MAX_API_LOG_LEN];
static libEvent nullEvent ;

#define HTTP_GET_STR "GET"
#define HTTP_PUT_STR "PUT"
#define HTTP_PATCH_STR "PATCH"
#define HTTP_POST_STR "POST"
#define HTTP_DELETE_STR "DELETE"
#define HTTP_UNKNOWN_STR "UNKNOWN"

/* convert http event type to its string name */
const char * getHttpCmdType_str ( evhttp_cmd_type type )
{
    switch (type)
    {
        case EVHTTP_REQ_GET:    return(HTTP_GET_STR);
        case EVHTTP_REQ_PUT:    return(HTTP_PUT_STR);
        case EVHTTP_REQ_PATCH:  return(HTTP_PATCH_STR);
        case EVHTTP_REQ_POST:   return(HTTP_POST_STR);
        case EVHTTP_REQ_DELETE: return(HTTP_DELETE_STR);
        case EVHTTP_REQ_HEAD:
        case EVHTTP_REQ_OPTIONS:
        case EVHTTP_REQ_TRACE:
        case EVHTTP_REQ_CONNECT:
        default:
            break ;
    }
    return(HTTP_UNKNOWN_STR);
}

/* ***********************************************************************
 *
 * Name       : httpUtil_event_init
 *
 * Description: Initialize the supplied libevent structure to default
 *              start values including with the supplied hostname,
 *              service , ip and port values.
 *
 * Note: No memory allication is performed.
 *
 * ************************************************************************/

int httpUtil_event_init ( libEvent * ptr , 
                            string   hostname,
                            string   service, 
                            string   ip, 
                               int   port )
{
    /* Default Starting States */
    ptr->sequence   = 0              ;
    ptr->request    = SERVICE_NONE   ;
    ptr->state      = HTTP__TRANSMIT ;
    ptr->log_prefix = hostname       ;
    ptr->log_prefix.append(" ")      ;
    ptr->log_prefix.append(service)      ;

    /* Execution Controls */
    ptr->stuck       = 0     ;
    ptr->count       = 0     ;
    ptr->timeout     = 0     ;
    ptr->retries     = 0     ;
    ptr->cur_retries = 0     ;
    ptr->max_retries = 0     ;
    ptr->active      = false ;
    ptr->mutex       = false ;
    ptr->found       = false ;
    ptr->blocking    = false ;
    ptr->noncritical = false ;
    ptr->rx_retry_cnt= 0     ;
    ptr->rx_retry_max= 1000  ;

    ptr->uuid.clear();
    ptr->new_uuid.clear() ;

    /* Service Specific Request Info */
    ptr->ip       = ip       ;
    ptr->port     = port     ;
    ptr->hostname = hostname ;
    ptr->service  = service  ;

    /* Copy the mtce token into the libEvent struct for this command */
    ptr->token.url.clear();
    ptr->token.token.clear();
    ptr->token.issued.clear();
    ptr->token.expiry.clear();
    ptr->token.delay = false ;
    ptr->token.refreshed = false ;

    /* Instance Specific Request Data Data */
    ptr->entity_path.clear() ;
    ptr->entity_path_next.clear() ;
    ptr->address.clear();
    ptr->payload.clear();
    ptr->response.clear();

    ptr->operation.clear();
    ptr->information.clear();
    ptr->result.clear();
    ptr->label.clear();

    /** Default the user agent to mtce ; other users and commands can override */
    ptr->user_agent = "mtce/1.0" ;

    ptr->admin_url.clear(); 
    ptr->internal_url.clear();
    ptr->public_url.clear();

    /* HTTP Specific Info */
    ptr->type = EVHTTP_REQ_GET ; /* request type GET/PUT/PATCH etc */   

    /* Result Info */
    ptr->status      = FAIL;
    ptr->http_status = 0   ;
    ptr->low_wm = ptr->med_wm = ptr->high_wm = false ;
    node_inv_init ( ptr->inv_info ) ;

    ptr->this_time = 0 ;
    ptr->prev_time = 0 ;

    memset (&ptr->req_str[0], 0, MAX_API_LOG_LEN);

    return (PASS);
}


/* initialize this module */
void httpUtil_init ( void )
{
    httpUtil_event_init ( &nullEvent, "null", "null" , "0.0.0.0", 0);
    nullEvent.request = SERVICE_NONE ;
    
    snprintf (&rest_api_filename[0], MAX_FILENAME_LEN, "/var/log/%s_api.log", 
               program_invocation_short_name );
}

/* ***********************************************************************
 *
 * Name       : httpUtil_free_conn
 *
 * Description: Free an event's connection memory if it exists.
 *
 * ************************************************************************/

void httpUtil_free_conn ( libEvent & event )
{
    if ( event.conn )
    {
        hlog3 ("%s Free Connection (%p)\n", event.log_prefix.c_str(), event.conn );
        evhttp_connection_free ( event.conn );
        event.conn = NULL ;
    }
    else
    {
        hlog1 ("%s Already Freed Connection\n", event.log_prefix.c_str());
    }
}

/* ***********************************************************************
 *
 * Name       : httpUtil_free_base
 *
 * Description: Free an event's base memory if it exists.
 *
 * ************************************************************************/

void httpUtil_free_base ( libEvent & event )
{
    /* Free the base */
    if ( event.base )
    {
        hlog3 ("%s Free Base (%p)\n", event.log_prefix.c_str(), event.base );

        event_base_free(event.base);
        event.base = NULL ;
        if ( event.conn )
        {
            hlog ("%s Free Connection (%p) --------- along with base\n", 
                         event.log_prefix.c_str(), event.conn );

            evhttp_connection_free ( event.conn );
            event.conn = NULL ;
        }
    }
    else
    {
        hlog1 ("%s Already Freed Event Base\n", event.log_prefix.c_str()); 
    }
}

/* ***********************************************************************
 *
 * Name       : httpUtil_connect
 *
 * Description: Allocate memory for a new connection off the supplied
 *              base with respect to an ip and port.
 *
 * ************************************************************************/

int httpUtil_connect ( libEvent & event )
{
    if ( event.base )
    {
        hlog ("%s target:%s:%d\n", event.log_prefix.c_str(), event.ip.c_str(), event.port);

        /* Open an http connection to specified IP and port */
        event.conn = evhttp_connection_base_new ( event.base, NULL,
                                                  event.ip.c_str(), 
                                                  event.port );
        /* bind to the correctly-versioned local address */
        if ( event.conn )
        {
            return(PASS) ;
        }
        else
        {
            elog ("%s create connection failed (evhttp_connection_base_new)\n", event.log_prefix.c_str());
            return (FAIL_CONNECT);
        }
    }
    else
    {
        slog ("%s Null Event base\n", event.log_prefix.c_str());
        return (FAIL_EVENT_BASE);
    }
}

/* ***********************************************************************
 *
 * Name       : httpUtil_request
 *
 * Description: Allocate memory for a new request off the supplied base.
 *
 * ************************************************************************/

int httpUtil_request ( libEvent & event,
                       void(*hdlr)(struct evhttp_request *, void *))
{
    int rc = PASS ;
    
    /* make a new request and bind the event handler to it */
    event.req = evhttp_request_new( hdlr , event.base );
    if ( ! event.req )
    {
        elog ("%s evhttp_request_new returned NULL\n", event.log_prefix.c_str() );
        rc = FAIL ;
    }
    return (rc);
}

/* ***********************************************************************
 *
 * Name       : httpUtil_payload_add
 *
 * Description: Add the payload to the output buffer.
 *
 * @returns 0 for success or -1 in error case
 *
 * ************************************************************************/

int httpUtil_payload_add ( libEvent & event )
{
    int rc = PASS ;
    
    /* Returns the output buffer. */ 
    event.buf = evhttp_request_get_output_buffer ( event.req );
   
    /* Check for no buffer */
    if ( ! event.buf )
    {
        elog ("%s evhttp_request_get_output_buffer returned null (%p)\n", 
                  event.log_prefix.c_str(), event.req );

        rc = FAIL ;
    }
    else
    {
        /* write the body into the buffer */
        rc = evbuffer_add_printf ( event.buf, "%s", event.payload.c_str());
        if ( rc == -1 )
        {
            elog ("%s evbuffer_add_printf returned error (-1)\n",
                      event.log_prefix.c_str());

            rc = FAIL ;
        }
        else if ( rc == 0 )
        {
            elog ("%s no data added to output buffer (len=0)\n", 
                      event.log_prefix.c_str());

            rc = FAIL ;
        }
        else
        {
            rc = PASS ;
        }
    }
    return (rc);
}

/* ***********************************************************************
 *
 * Name       : httpUtil_payload_len
 *
 * Description: Calculate payload length from the output buffer
 *              and return a string representing that length value.
 *
 * ************************************************************************/

string httpUtil_payload_len ( libEvent * ptr )
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
        hlog2 ("%s Payload Length: %s\n", ptr->log_prefix.c_str(), body_len.c_str() );
    }
    return ( body_len );
}

/* ***********************************************************************
 *
 * Name       : httpUtil_header_add
 *
 * Description: Add the supplied list of headers to the http request
 *              headers section.
 *
 * ************************************************************************/

int httpUtil_header_add ( libEvent * ptr, http_headers_type * hdrs_ptr )
{
    int rc = PASS ;

    if ( hdrs_ptr->entries > MAX_HEADERS )
    {
        elog ("%s Too many headers (%d:%d)\n", 
                  ptr->log_prefix.c_str(), MAX_HEADERS, hdrs_ptr->entries );
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
            elog ("%s evhttp_add_header returned failure (%d:%s:%s)\n",
                   ptr->log_prefix.c_str(), rc,
                   hdrs_ptr->entry[i].key.c_str(),
                   hdrs_ptr->entry[i].value.c_str());
            rc = FAIL ;
            break ;
        }  
    }
    return (rc);
}



/* ***********************************************************************
 *
 * Name       : httpUtil_get_length
 *
 * Description: Loads libEvent.response_len with the length of the
 *              input buffer so we can allocate enough memory to
 *              copy it into.
 *
 * Get the length of the json response.
 * Deal with oversized messages.
 *
 * @param event is a reference to the callers libEvent struct
 * where it inds the input buffer pointer
 *
 * @return integer value representing the length of the input buffer
 *
 * ************************************************************************/

int httpUtil_get_length ( libEvent & event )
{
    event.response_len = evbuffer_get_length (event.req->input_buffer);
    if ( event.response_len == 0 )
    {
        hlog ("%s Request - Response has not content\n",
                event.log_prefix.c_str());
        event.status = FAIL_JSON_ZERO_LEN ;
    }
    return ( event.response_len );
}

/* Load the response string into the event struct */
int httpUtil_get_response ( libEvent & event )
{
    if ( httpUtil_get_length ( event ) )
    {
        size_t real_len      ;

        /* Get a stack buffer, zero it, copy to it and terminate it */
        char * stack_buf_ptr = (char*)malloc (event.response_len+1);
        memset ( stack_buf_ptr, 0, event.response_len+1 );
        real_len = evbuffer_remove( event.req->input_buffer, stack_buf_ptr, 
                                event.response_len);

        if ( real_len != event.response_len )
        {
            wlog ("%s Length differs from removed length (%ld:%ld)\n",
                      event.log_prefix.c_str(),
                      event.response_len, 
                      real_len );
        }

        if ( real_len == 0 )
        {
            hlog1 ("%s has no response data\n", event.log_prefix.c_str() );
        }
        /* Terminate the buffer , this is where the +1 above is required. 
         * Without it there is memory corruption reported by Linux */
         *(stack_buf_ptr+event.response_len) = '\0';

        /* Store the response */
        event.response = stack_buf_ptr ;

        free (stack_buf_ptr);
    }
    return ( event.status );
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_status
 *
 * Description: Extracts and returns the HTTP execution status
 *
 * ************************************************************************/

int httpUtil_status ( libEvent & event )
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
            hlog ("%s HTTP_OK (%d)\n", event.hostname.c_str(), event.status );
            event.status = PASS ;
            break;
        }
        /* Authentication error - refresh the token */
        case 401:
        {
            keyToken_type * token_ptr = tokenUtil_get_ptr() ;
            rc = FAIL_AUTHENTICATION ;
            token_ptr->delay = true ; /* force delayed token renewal on authentication error */
            break ;
        }
        case 0:
        {
            wlog ("%s failed to maintain connection to '%s:%d' for '%s'\n",
                      event.hostname.c_str(), event.ip.c_str(), event.port, event.log_prefix.c_str() );
            event.status = FAIL_HTTP_ZERO_STATUS ;
            rc = FAIL_HTTP_ZERO_STATUS ;
            break ;
        }
        default:
        {
            hlog2 ("%s Status: %d\n", event.hostname.c_str(), event.status );
            rc = event.status ;
            break;
        }
    }
    return (rc);
}



void httpUtil_handler ( struct evhttp_request *req, void *arg )
{
    unsigned long temp ;
    int rc = PASS ;

    UNUSED(req);
    libEvent * event_ptr ;

    if ( arg == NULL )
    {
        elog ("null base pointer\n");
        return ;
    }

    /* find the event for this base */
    if ( keyValObject.get_key ((unsigned long)arg, temp ) != PASS )
    {
        wlog ("get_key value 'event' lookup from base (%p) key failed\n", arg );
        return ;
    }

    event_ptr = (libEvent*)temp; 
    if (( event_ptr->request >= SERVICE_LAST ) || ( event_ptr->request == SERVICE_NONE ))
    {
        slog ("HTTP Event Lookup Failed for http base (%p) <------\n", arg);
        return ;
    }

    /* Check the HTTP Status Code */
    event_ptr->status = httpUtil_status ( (*event_ptr) ) ;
    if ( event_ptr->status == HTTP_NOTFOUND )
    {
        elog ("%s returned (Not-Found) (%d)\n", 
                  event_ptr->log_prefix.c_str(), 
                  event_ptr->status);
        if ( event_ptr->type != EVHTTP_REQ_POST )
            event_ptr->status = PASS ;

        goto httpUtil_handler_done ;
    }

    else if (( event_ptr->status != PASS ) && ( ! req ))
    {
        elog ("%s Request Timeout (%d)\n", 
                   event_ptr->log_prefix.c_str(),
                   event_ptr->timeout);

        event_ptr->status = FAIL_TIMEOUT ;
        goto httpUtil_handler_done ;
    }

    else if ( event_ptr->status != PASS )
    {
        goto httpUtil_handler_done ;
    }
   
    /* Delete commands don't have a response unless there is an error.
     * Deal with this as a special case - 
     * Currently only Neutron uses the delete */
    if ( event_ptr->type == EVHTTP_REQ_DELETE )
    {
        if ( httpUtil_get_length ( (*event_ptr) ) != 0 ) 
        {
            /* Preserve the incoming status over the get response */
            rc = event_ptr->status ;
            httpUtil_get_response ( (*event_ptr) ) ;
            event_ptr->status = rc ;
        }
        if (event_ptr->status == FAIL_JSON_ZERO_LEN )
            event_ptr->status = PASS ;
    }
    else if ( httpUtil_get_response ( (*event_ptr) ) != PASS )
    {
        elog ("%s failed to get response\n", event_ptr->log_prefix.c_str());
        goto httpUtil_handler_done ;
    }

    if ( event_ptr->handler )
    {
        // ilog ("%s calling event specific handler\n", event_ptr->log_prefix.c_str() );
        rc = event_ptr->handler ( (*event_ptr) ) ;
    }
    else
    {
        slog ("%s no event handler bound in\n", event_ptr->log_prefix.c_str() );
        rc = event_ptr->status = FAIL_NULL_POINTER ;
    }

httpUtil_handler_done:

    // hlog2 ("%s Base:%p:%p Event:%p\n", event_ptr->log_prefix.c_str(), event_ptr->base, arg, event_ptr );

    keyValObject.del_key ((unsigned long)arg );
    event_ptr->active = false ;
    
    gettime   ( event_ptr->done_time );
    timedelta ( event_ptr->send_time, event_ptr->done_time, event_ptr->diff_time );

    if ( event_ptr->status )
    {
        elog ( "%s Failed (rc:%d)\n",
                   event_ptr->log_prefix.c_str(),
                   event_ptr->status );
    }
    httpUtil_log_event ( event_ptr );
}


/* ***********************************************************************
 *
 * Name       : httpUtil_api_request
 *
 * Description: Makes an HTTP request based on all the info
 *              in the supplied libEvent.
 *
 * This is the primary external interface in this module.
 *
 * Both blocking and non-blocking request type are supported.
 *
 * ************************************************************************/

/***************************************************************************
 *
 * Name       : httpUtil_latency_log
 *
 * Description: Measures command handling time and creates a Latency log
 *              if that time exceeds the specified threshold (msecs).
 *
 * Parms:
 *     event     - the event in context
 *
 *     label_ptr - "start" to init the prev_timer or
 *               - "some label" to identify the point in the code and to
 *                  measure time against the previous call.
 *
 *     msecs     - the latency log threshold
 *
 * Usage:
 *
 *     httpUtil_latency_log ( event, HTTPUTIL_SCHED_MON_START, 0 );
 *
 *     [ timed code ]
 *
 *     httpUtil_latency_log ( event, "label 1" , msecs );
 *
 *     [ timed code ]
 *
 *     httpUtil_latency_log ( event, "label 2", msecs );
 *
 *     ...
 *
 *****************************************************************************/

#define HTTPUTIL_SCHED_MON_START ((const char *)"start")
#define MAX_DELAY_B4_LATENCY_LOG  (1700)
void httpUtil_latency_log ( libEvent & event, const char * label_ptr, int line , int msecs )
{
    event.this_time = gettime_monotonic_nsec () ;

    /* If label_ptr is != NULL and != start then take the measurement */
    if ( label_ptr && strncmp ( label_ptr, HTTPUTIL_SCHED_MON_START, strlen(HTTPUTIL_SCHED_MON_START)))
    {
        if ( event.this_time > (event.prev_time + (NSEC_TO_MSEC*(msecs))))
        {
            llog ("%s ... %4llu.%-4llu msec - %s (%d)\n", event.hostname.c_str(),
                 ((event.this_time-event.prev_time) > NSEC_TO_MSEC) ? ((event.this_time-event.prev_time)/NSEC_TO_MSEC) : 0,
                 ((event.this_time-event.prev_time) > NSEC_TO_MSEC) ? ((event.this_time-event.prev_time)%NSEC_TO_MSEC) : 0,
                 label_ptr, line );
        }
    }
    /* reset to be equal for next round */
    event.prev_time = event.this_time ;
}

bool token_recursion = false ;

int httpUtil_api_request ( libEvent & event )

{
    http_headers_type hdrs ;
    int hdr_entry   = 0    ;
    string path     = ""   ;
    bool free_key   = true ;
    event.status    = PASS ;

    event.log_prefix = event.hostname ;
    event.log_prefix.append (" ");
    event.log_prefix.append (event.service) ;
    event.log_prefix.append (" '");
    event.log_prefix.append (event.operation) ;
    event.log_prefix.append ("'");

    hlog ("%s '%s' request\n", event.log_prefix.c_str(), getHttpCmdType_str(event.type));

    if (( event.request == SERVICE_NONE ) || 
        ( event.request >= SERVICE_LAST )) 
    {
        slog ("%s Invalid request %d\n", event.log_prefix.c_str(), event.request);
        event.status = FAIL_BAD_PARM ;
        return (event.status);
    }
    /* Check for memory leaks */
    if ( event.base )
    {
        slog ("%s http base memory leak avoidance (%p)\n",
                  event.log_prefix.c_str(), event.base );

        // Be sure to free the key
        keyValObject.del_key ((unsigned long)event.base );
       // event_base_free(event.base);
    }

    /* Allocate the base */
    event.base = event_base_new();
    if ( event.base == NULL )
    {
        elog ("%s No Memory for Request\n", event.log_prefix.c_str());
        event.status = FAIL_EVENT_BASE ;
        return (event.status) ;
    }
    else
    {
        if ( keyValObject.add_key ((unsigned long)event.base, (unsigned long)&event) != PASS )
        {
            slog ("%s failed to store base:event as key (%p) value pair\n", 
                      event.log_prefix.c_str(), event.base );

            /* lets try and recover from this */
            keyValObject.del_key ((unsigned long)event.base);
            if ( keyValObject.add_key ((unsigned long)event.base, (unsigned long)&event) != PASS )
            {
                slog ("%s still cannot store base:event after key_del\n", event.log_prefix.c_str());

                event.status = FAIL_LOCATE_KEY_VALUE ;
                goto httpUtil_api_request_done ;
            }
        }
    }

    if ( event.request == KEYSTONE_GET_TOKEN )
    {
        event.payload  = ""   ;

        /* create the json string that can request an authority 
         * token and write that string to 'payload' */
        event.status = jsonApi_auth_request ( event.hostname, event.payload );
        if ( event.status != PASS )
        {
            elog ("%s unable to perform get token request (rc:%d)\n", event.hostname.c_str(), event.status );
            goto httpUtil_api_request_done ;
        }
    }
    else if (( event.request == KEYSTONE_GET_ENDPOINT_LIST ) ||
             ( event.request == KEYSTONE_GET_SERVICE_LIST ))
    {
        ;
    }

    else if (( event.request == SYSINV_SENSOR_ADD         ) ||
             ( event.request == SYSINV_SENSOR_DEL         ) ||
             ( event.request == SYSINV_SENSOR_LOAD        ) ||
             ( event.request == SYSINV_SENSOR_MOD         ) ||
             ( event.request == SYSINV_SENSOR_MOD_GROUP   ) ||
             ( event.request == SYSINV_SENSOR_ADD_GROUP   ) ||
             ( event.request == SYSINV_SENSOR_DEL_GROUP   ) ||
             ( event.request == SYSINV_SENSOR_LOAD_GROUPS ) ||
             ( event.request == SYSINV_SENSOR_LOAD_GROUP  ) ||
             ( event.request == SYSINV_SENSOR_GROUP_SENSORS ))
    {
        ;
    }
    else
    {
        slog ("%s Unsupported Request (%d)\n", event.hostname.c_str(), event.request);
        event.status = FAIL_BAD_CASE ;
        goto httpUtil_api_request_done ;
    }

    /* Establish connection */
    if ( httpUtil_connect ( event ))
    {
        event.status = FAIL_CONNECT ;
        goto httpUtil_api_request_done ;        
    }

    if ( httpUtil_request ( event, &httpUtil_handler ))
    {
        event.status = FAIL_REQUEST_NEW ;
        goto httpUtil_api_request_done ;
    }

    if ( event.request != KEYSTONE_GET_TOKEN )
    {
        jlog ("%s Address : %s\n", event.hostname.c_str(), event.address.c_str());
    }

    if (( event.type != EVHTTP_REQ_GET ) && 
        ( event.type != EVHTTP_REQ_DELETE ))
    {
        /* Add payload to the output buffer but only for PUT, POST and PATCH requests */
        if ( httpUtil_payload_add ( event ))
        {
            event.status = FAIL_PAYLOAD_ADD ;
            goto httpUtil_api_request_done ;
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

    /* Build the HTTP Header */
    hdrs.entry[hdr_entry].key   = "Host" ;
    hdrs.entry[hdr_entry].value = event.ip ;
    hdr_entry++;

    hdrs.entry[hdr_entry].key   = "X-Auth-Project-Id" ;
    hdrs.entry[hdr_entry].value = "admin";
    hdr_entry++;

    if (( event.type != EVHTTP_REQ_GET ) && 
        ( event.type != EVHTTP_REQ_DELETE ))
    {
        hdrs.entry[hdr_entry].key   = "Content-Length" ;
        hdrs.entry[hdr_entry].value = httpUtil_payload_len ( &event );
        hdr_entry++;
    }

    hdrs.entry[hdr_entry].key   = "User-Agent" ;
    hdrs.entry[hdr_entry].value = event.user_agent ;
    hdr_entry++;
           
    hdrs.entry[hdr_entry].key   = "Content-Type" ;
    hdrs.entry[hdr_entry].value = "application/json" ;
    hdr_entry++;

    hdrs.entry[hdr_entry].key   = "Accept" ;
    hdrs.entry[hdr_entry].value = "application/json" ;
    hdr_entry++;

    if ( event.request != KEYSTONE_GET_TOKEN )
    {
        hdrs.entry[hdr_entry].key   = "X-Auth-Token" ;
        hdrs.entry[hdr_entry].value = tokenUtil_get_ptr()->token ;
        hdr_entry++;
    }

    hdrs.entry[hdr_entry].key   = "Connection" ;
    hdrs.entry[hdr_entry].value = "close" ;
    hdr_entry++;
    hdrs.entries = hdr_entry ;

    /* Add the headers */
    if ( httpUtil_header_add ( &event, &hdrs ))
    {
        event.status = FAIL_HEADER_ADD ;
        goto httpUtil_api_request_done ;
    }

    /* get some timestamps and log the request */
    snprintf (&event.req_str[0], MAX_API_LOG_LEN-1,
              "\n%s [%5d] %s %s '%s' seq:%d -> Address : %s:%d %s %s ... %s",
              pt(), getpid(),
              event.hostname.c_str(),
              event.service.c_str(),
              event.operation.c_str(),
              event.sequence, event.ip.c_str(), event.port,
              getHttpCmdType_str( event.type ),
              event.address.c_str(),
              event.information.c_str());

    gettime   ( event.send_time );
    gettime   ( event.done_time ); /* create a valid done value */

    if ( event.request == KEYSTONE_GET_TOKEN )
    {
        path = MTC_POST_KEY_LABEL ;
        event.address = path ;
        event.prefix_path += path;
        hlog ("%s Keystone Internal Address : %s\n", event.hostname.c_str(), event.prefix_path.c_str());
        event.status = evhttp_make_request ( event.conn, event.req, event.type, event.prefix_path.data());
    }
    else
    {
        event.status = evhttp_make_request ( event.conn, event.req, event.type, event.address.data());
    }
    daemon_signal_hdlr ();
    if ( event.status == PASS )
    {
        string label = event.log_prefix ;
        label.append (" - ");
        label.append (event.operation);

        /* O.K we are commited to making the request */
        free_key = false ;

        evhttp_connection_set_timeout(event.conn, event.timeout);

        httpUtil_latency_log ( event, HTTPUTIL_SCHED_MON_START,__LINE__,  0 );

        /* Default to retry for both blocking and non-blocking command */
        event.status = RETRY ;
        if ( event.blocking == true )
        {
            hlog ("%s Requested (blocking) (timeout:%d secs)\n", event.log_prefix.c_str(), event.timeout);

            /* Send the message with timeout */
            event_base_dispatch(event.base);
            httpUtil_latency_log ( event, label.c_str(), __LINE__, MAX_DELAY_B4_LATENCY_LOG );
            goto httpUtil_api_request_done ;
        }
        else if ( event.request == KEYSTONE_GET_TOKEN )
        {
            hlog ("%s Requested (non-blocking) (timeout:%d secs)\n", event.log_prefix.c_str(), event.timeout);
            event.active = true ;
            event.status = event_base_loop(event.base, EVLOOP_NONBLOCK);
            httpUtil_latency_log ( event, label.c_str(), __LINE__, MAX_DELAY_B4_LATENCY_LOG ); /* Should be immediate ; non blocking */
            return (event.status);
            // goto httpUtil_api_request_done ;
        }
        else
        {
            hlog ("%s Requested (blocking) (timeout:%d secs)\n", event.log_prefix.c_str(), event.timeout );
            event_base_dispatch(event.base);
            httpUtil_latency_log ( event, label.c_str(), __LINE__, MAX_DELAY_B4_LATENCY_LOG ) ;
            goto httpUtil_api_request_done ;
        }
    }
    else
    {
        elog ("%s Call to 'evhttp_make_request' failed (rc:%d)\n",
                  event.hostname.c_str(), event.status);
    }

httpUtil_api_request_done:

    httpUtil_free_conn ( event );
    httpUtil_free_base ( event );

    /* If the request fails then delete the key here */
    if ( free_key )
    {
        keyValObject.del_key ((unsigned long)event.base) ;
    }

    return (event.status);
}


void httpUtil_event_info ( libEvent & event )
{
    ilog ("%s request to %s.%d Status:%d \n", 
            event.log_prefix.c_str(), 
            event.ip.c_str(), 
            event.port,
            event.status);
    if ( event.request == KEYSTONE_GET_TOKEN )
    {
        ilog ("--- Address : %s\n", event.prefix_path.c_str());
    }
    else
    {
        ilog ("--- Address : %s\n", event.address.c_str());
    }
    ilog ("--- Payload : %s\n", event.payload.c_str());
    ilog ("--- Response: %s\n", event.response.c_str());
    ilog ("--- TokenUrl: %s\n", event.token.url.c_str());
}

void httpUtil_log_event ( libEvent * event_ptr )
{
    string event_sig = daemon_get_cfg_ptr()->debug_event ;
    msgSock_type * mtclogd_ptr = get_mtclogd_sockPtr ();
    
    send_log_message ( get_mtclogd_sockPtr(), event_ptr->hostname.data(), &rest_api_filename[0], &event_ptr->req_str[0] );

    if ( event_ptr->request == KEYSTONE_GET_TOKEN )
    {
        jlog1 ("%s seq:%d -> %s:%d %s %s http status: %d ... %s\n",
              event_ptr->log_prefix.c_str(),
              event_ptr->sequence,
              event_ptr->ip.c_str(),
              event_ptr->port,
              getHttpCmdType_str( event_ptr->type ),
              event_ptr->prefix_path.c_str(),
              event_ptr->http_status,
              event_ptr->information.c_str());
    }
    else
    {
        jlog1 ("%s seq:%d -> %s:%d %s %s http status: %d ... %s\n",
              event_ptr->log_prefix.c_str(),
              event_ptr->sequence,
              event_ptr->ip.c_str(),
              event_ptr->port,
              getHttpCmdType_str( event_ptr->type ),
              event_ptr->address.c_str(),
              event_ptr->http_status,
              event_ptr->information.c_str());
    }

    if (!event_ptr->payload.empty())
    {
        if ((!string_contains(event_ptr->payload,"token")) && 
            (!string_contains(event_ptr->payload,"assword")))
        {
            snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
                       "%s [%5d] %s seq:%d -> Payload : %s",
                       pt(), getpid(), event_ptr->log_prefix.c_str(), event_ptr->sequence, event_ptr->payload.c_str() );
        }
        else
        {
            snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
                       "%s [%5d] %s seq:%d -> Payload : ... contains private content ...",
                       pt(), getpid(), event_ptr->log_prefix.c_str(), event_ptr->sequence );
        }
        send_log_message ( mtclogd_ptr, event_ptr->hostname.data(), &rest_api_filename[0], &rest_api_log_str[0] );
    }

    if ( !event_ptr->response.empty() )
    {
        if ((!string_contains(event_ptr->response,"token")) && 
            (!string_contains(event_ptr->response,"assword")))
        {
            snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
                       "%s [%5d] %s seq:%d -> Response: %s",
                       pt(), getpid(), event_ptr->log_prefix.c_str(), event_ptr->sequence, event_ptr->response.c_str() );
        }
        else
        {
            snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
                       "%s [%5d] %s seq:%d -> Response: ... contains private content ...",
                       pt(), getpid(), event_ptr->log_prefix.c_str(), event_ptr->sequence );
        }
        send_log_message ( mtclogd_ptr, event_ptr->hostname.data(), rest_api_filename, &rest_api_log_str[0] );
    }
    
    snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
          "%s [%5d] %s %s '%s' seq:%d -> Status  : %d {execution time %ld.%06ld secs}\n",
          pt(), getpid(),
          event_ptr->hostname.c_str(),
          event_ptr->service.c_str(), 
          event_ptr->operation.c_str(),
          event_ptr->sequence,
          event_ptr->http_status,
          event_ptr->diff_time.secs, 
          event_ptr->diff_time.msecs );
    
    if (( event_ptr->diff_time.secs > 2 ) || (event_ptr->http_status != HTTP_OK ) )
    {
        int len = strlen (rest_api_log_str) ;
        snprintf (&rest_api_log_str[len-1], 20, "  <---------");
    }

    send_log_message ( mtclogd_ptr, event_ptr->hostname.data(), &rest_api_filename[0], &rest_api_log_str[0] );
}

/*****************************************************************
 *
 * Name        : httpUtil_bind
 *
 * Description : Setup the HTTP server socket
 *
 *****************************************************************/
int httpUtil_bind ( libEvent & event )
{
   int one = 1;

   event.fd = socket(AF_INET, SOCK_STREAM, 0);
   if (event.fd < 0)
   {
       elog ("failed to create http server socket (%d:%m)\n", errno );
       return FAIL_SOCKET_CREATE ;
   }

   /* make socket reusable */
   if ( 0 > setsockopt(event.fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int)))
   {
       elog ("failed to set http server socket to reusable (%d:%m)\n", errno );
       return FAIL_SOCKET_OPTION ;
   }

   memset(&event.addr, 0, sizeof(struct sockaddr_in));
   event.addr.sin_family = AF_INET;

   /* ERIK: INADDR_ANY; TODO: Refine this if we can */
   event.addr.sin_addr.s_addr = inet_addr(LOOPBACK_IP);
   // event.addr.sin_addr.s_addr = INADDR_ANY;
   event.addr.sin_port = htons(event.port);

   /* bind port */
   if ( 0 > bind ( event.fd, (struct sockaddr*)&event.addr, sizeof(struct sockaddr_in)))
   {
       elog ("failed to bind to http server port %d (%d:%m)\n", event.port, errno );
       return FAIL_SOCKET_BIND ;
   }

   /* Listen for events */
   if ( 0 > listen(event.fd, 10 ))
   {
       elog ("failed to listen to http server socket (%d:%m)\n", errno );
       return FAIL_SOCKET_LISTEN;
   }

   /* make non-blocking */
   int flags = fcntl ( event.fd, F_GETFL, 0) ;
   if ( flags < 0 || fcntl(event.fd, F_SETFL, flags | O_NONBLOCK) < 0)
   {
       elog ("failed to set http server socket to non-blocking (%d:%m)\n", errno );
       return FAIL_SOCKET_OPTION;
   }

   return PASS;
}

/* Setup the http server */
int httpUtil_setup ( libEvent & event,
                     int          supported_methods,
                     void(*hdlr)(struct evhttp_request *, void *) )
{
   int rc = PASS ;
   if ( ( rc = httpUtil_bind ( event )) != PASS )
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

   /* api is a void return */
   evhttp_set_allowed_methods (event.httpd, supported_methods );

   rc = evhttp_accept_socket(event.httpd, event.fd);
   if ( rc == -1)
   {
       elog ("failed to accept on http server socket\n");
       return -1;
   }

   /* api is a void return */
   evhttp_set_gencb(event.httpd, hdlr, NULL);

   ilog ("Listening On: 'http server' socket %s:%d\n",
          inet_ntoa(event.addr.sin_addr), event.port );
   return PASS ;
}

void httpUtil_fini ( libEvent & event )
{
    if ( event.fd )
    {
        if ( event.base )
        {
            event_base_free( event.base);
        }
        close ( event.fd );
        event.fd = 0 ;
    }
}

void httpUtil_look ( libEvent & event )
{
    /* Look for Events */
    if ( event.base )
    {
        // rc = event_base_loopexit( mtce_event.base, NULL ) ; // EVLOOP_NONBLOCK );
        event_base_loop(event.base, EVLOOP_NONBLOCK );
    }
}

