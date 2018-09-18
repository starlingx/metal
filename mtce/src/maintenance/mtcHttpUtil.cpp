/*
 * Copyright (c) 2013-2018 Wind River Systems, Inc.
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
  *    Setup Utilities:
  *
  *       mtcHttpUtil_event_init
  *       mtcHttpUtil_free_conn
  *       mtcHttpUtil_free_base
  *       mtcHttpUtil_connect_new
  *       mtcHttpUtil_request_new
  *       mtcHttpUtil_payload_add
  *       mtcHttpUtil_payload_len
  *       mtcHttpUtil_header_add
  *       mtcHttpUtil_status
  * 
  *    Request Utility and Handler:
  *
  *       mtcHttpUtil_api_request
  *       mtcHttpUtil_handler
  *
  *    Result Utilities:
  *
  *       mtcHttpUtil_receive
  *       mtcHttpUtil_get_length
  *       mtcHttpUtil_get_response
  *       mtcHttpUtil_log_event
  *       mtcHttpUtil_event_info
  *
  *    Debug Utilities:
  *
  *       mtcHttpUtil_start_timer
  *       mtcHttpUtil_stop_timer
  *       mtcHttpUtil_log_time
  *       mtcHttpUtil_payload_len
  * 
  */

#include <time.h>

using namespace std;

#include "nodeClass.h"      /* for ... maintenance class nodeLinkClass */
#include "httpUtil.h"       /* this module header                      */
#include "tokenUtil.h"      /* for ... tokenUtil_get_ptr               */
#include "mtcHttpUtil.h"    /* this module header                      */
#include "mtcInvApi.h"      /* Inventory REST API header               */
#include "mtcVimApi.h"      /* VIM REST API header                     */
#include "jsonUtil.h"       /* Json Utilities                          */
#include "nodeUtil.h"       /* Node Utilities                          */

libEvent nullEvent ;

/** Inventory Add, Get, Update, Query HTTP Rest API handler wraqpper headers */
extern void mtcInvApi_add_Handler     ( struct evhttp_request *req, void *arg );
extern void mtcInvApi_qry_Handler     ( struct evhttp_request *req, void *arg );
extern void mtcInvApi_get_Handler     ( struct evhttp_request *req, void *arg );
extern void mtcInvApi_cfg_Handler     ( struct evhttp_request *req, void *arg );

extern void mtcSmgrApi_Handler        ( struct evhttp_request *req, void *arg );
extern void mtcVimApi_Handler         ( struct evhttp_request *req, void *arg );

void mtcHttpUtil_Handler              ( struct evhttp_request *req, void *arg );


/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_event_init
 *
 * Description: Initialize the supplied libevent structure to default
 *              start values including with the supplied hostname,
 *              service , ip and port values.
 *
 * Note: No memory allication is performed.
 *
 * ************************************************************************/

int mtcHttpUtil_event_init ( libEvent * ptr , 
                               string   hostname,
                               string   service, 
                               string   ip, 
                                  int   port)
{
    /* Default Starting States */
    ptr->sequence   = 0                  ;
    ptr->request    = SERVICE_NONE       ;
    ptr->state      = HTTP__TRANSMIT     ;
    ptr->log_prefix = hostname           ;
    ptr->log_prefix.append(" ")          ;
    ptr->log_prefix.append(service)      ;

    /* Execution Controls */
    ptr->stuck       = 0     ;
    ptr->count       = 0     ;
    ptr->timeout     = 0     ;
    ptr->cur_retries = 0     ;
    ptr->max_retries = 0     ;
    ptr->active      = false ;
    ptr->mutex       = false ;
    ptr->found       = false ;
    ptr->blocking    = false ;
    ptr->noncritical = false ;
    ptr->rx_retry_cnt= 0     ;
    ptr->rx_retry_max= 1000  ;

    /* Service Specific Request Info */
    ptr->ip       = ip       ;
    ptr->port     = port     ;
    ptr->hostname = hostname ;
    ptr->service  = service  ;

    /* Copy the mtce token into the libEvent struct for this command */
    ptr->token = get_mtcInv_ptr()->tokenEvent.token ;

    /* Instance Specific Request Data Data */
    ptr->entity_path.clear() ;
    ptr->entity_path_next.clear() ;
    ptr->address.clear();
    ptr->payload.clear();
    ptr->operation.clear();
    ptr->information.clear();

    /* HTTP Specific Info */
    ptr->type = EVHTTP_REQ_GET ; /* request type GET/PUT/PATCH etc */   


    /* Result Info */
    ptr->status      = FAIL;
    ptr->exec_time_msec = 0 ;
    ptr->http_status = 0   ;
    ptr->low_wm = ptr->med_wm = ptr->high_wm = false ;

    ptr->response.clear();
    node_inv_init ( ptr->inv_info ) ;

    memset (&ptr->req_str[0], 0, MAX_API_LOG_LEN);

    return (PASS);
}

static char rest_api_filename[MAX_FILENAME_LEN];
static char rest_api_log_str [MAX_API_LOG_LEN];

void mtcHttpUtil_init ( void )
{
    mtcHttpUtil_event_init ( &nullEvent, "null", "null" , "0.0.0.0", 0);
    nullEvent.request = SERVICE_NONE ;

    snprintf (&rest_api_filename[0], MAX_FILENAME_LEN, "/var/log/%s_api.log", 
               program_invocation_short_name );
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_free_conn
 *
 * Description: Free an event's connection memory if it exists.
 *
 * ************************************************************************/

void mtcHttpUtil_free_conn ( libEvent & event )
{
    if ( event.conn )
    {
        hlog2 ("%s Free Connection (%p)\n", event.log_prefix.c_str(), event.conn );
        evhttp_connection_free ( event.conn );
        event.conn = NULL ;
    }
    else
    {
        hlog2 ("%s Already Freed Connection\n", event.log_prefix.c_str());
    }
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_free_base
 *
 * Description: Free an event's base memory if it exists.
 *
 * ************************************************************************/

void mtcHttpUtil_free_base ( libEvent & event )
{
    /* Free the base */
    if ( event.base )
    {
        hlog2 ("%s Free Base (%p)\n", event.log_prefix.c_str(), event.base );

        event_base_free(event.base);
        event.base = NULL ;
        if ( event.conn )
        {
            hlog2 ("%s Free Connection (%p)\n", 
                         event.log_prefix.c_str(), event.conn );

            evhttp_connection_free ( event.conn );
            event.conn = NULL ;
        }
    }
    else
    {
        hlog2 ("%s Already Freed Event Base\n", event.log_prefix.c_str()); 
    }
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_connect_new
 *
 * Description: Allocate memory for a new connection off the supplied
 *              base with respect to an ip and port.
 *
 * ************************************************************************/

int mtcHttpUtil_connect_new ( libEvent & event )
{
    if ( event.base )
    {
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

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_request_new
 *
 * Description: Allocate memory for a new request off the supplied base.
 *
 * ************************************************************************/

int mtcHttpUtil_request_old ( libEvent & event,
                              void(*hdlr)(struct evhttp_request *, void *))
{
    int rc = PASS ;

    /* make a new request and bind the event handler to it */
    event.req = evhttp_request_new( hdlr , event.base );
    if ( ! event.req )
    {
        elog ("call to 'evhttp_request_new' returned NULL\n");
        rc = FAIL ;
    }
    return (rc);
}

int mtcHttpUtil_request_new ( libEvent & event,
                              void(*hdlr)(struct evhttp_request *, void *))
{
    int rc = PASS ;

    /* make a new request and bind the event handler to it */
    event.req = evhttp_request_new( hdlr , &event );
    if ( ! event.req )
    {
        elog ("call to 'evhttp_request_new' returned NULL\n");
        rc = FAIL ;
    }
    return (rc);
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_payload_add
 *
 * Description: Add the payload to the output buffer.
 *
 * @returns 0 for success or -1 in error case
 *
 * ************************************************************************/

int mtcHttpUtil_payload_add ( libEvent & event )
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
        /* write the body into the buffer */
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

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_payload_len
 *
 * Description: Calculate payload length from the output buffer
 *              and return a string representing that length value.
 *
 * ************************************************************************/

string mtcHttpUtil_payload_len ( libEvent * ptr )
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
        hlog3 ("%s Buffer Len:%s\n", ptr->hostname.c_str(), body_len.c_str() );
    }
    return ( body_len );
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_header_add
 *
 * Description: Add the supplied list of headers to the http request
 *              headers section.
 *
 * ************************************************************************/

int mtcHttpUtil_header_add ( libEvent * ptr, http_headers_type * hdrs_ptr )
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

//int mtcHttpUtil_request_make ( libEvent        * ptr,
//                               enum evhttp_cmd_type   type,
//                                             string   path )
//{
//    return (evhttp_make_request( ptr->conn, ptr->req, type, path.data()));
//}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_status
 *
 * Description: Extracts and returns the HTTP execution status
 *
 * ************************************************************************/

int mtcHttpUtil_status ( libEvent & event )
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
            token_ptr->delay = true ;
            rc = FAIL_AUTHENTICATION ;
            break ;
        }
        case 0:
        {
            elog ("%s connection loss (%s:%d)\n",
                      event.log_prefix.c_str(), event.ip.c_str(), event.port );
            event.status = FAIL_HTTP_ZERO_STATUS ;
            rc = FAIL_HTTP_ZERO_STATUS ;
            break ;
        }
        default:
        {
            wlog ("%s Status: %d\n", event.hostname.c_str(), event.status );
            rc = event.status ;
            break;
        }
    }
    return (rc);
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_api_request
 *
 * Description: Makes an HTTP request based on all the info
 *              in the supplied libEvent.
 *
 * This is the primary external interface in this module.
 *
 * Both blocking and non-blocking request type are supported.
 *
 * ************************************************************************/


int mtcHttpUtil_api_request ( libEvent & event )

{
    http_headers_type hdrs ;
    int hdr_entry   = 0    ;
    int    rc       = FAIL ;
    void(*handler)(struct evhttp_request *, void *) = NULL ;

    /* Default to PUT */
    event.type = EVHTTP_REQ_PUT ;

    if (( event.request == SERVICE_NONE ) ||
        ( event.request >= SERVICE_LAST ))
    {
        slog ("Invalid request %d\n", event.request);
        event.status = FAIL_BAD_PARM ;
        return (event.status);
    }
    /* Check for memory leaks */
    if ( event.base )
    {
        slog ("%s http base memory leak avoidance (%p)\n",
                  event.log_prefix.c_str(), event.base );
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
        hlog2 ("%s base:%p object:%p\n", event.log_prefix.c_str(), event.base, &event );
    }

    if ( event.request == SYSINV_GET )
    {
        event.payload = "" ;

        /* Bind the update handler */
        handler = &mtcInvApi_get_Handler ;

        /* The type of HTTP request */
        event.type = EVHTTP_REQ_GET ;

        /* set the timeout */
        event.timeout = get_mtcInv_ptr()->sysinv_timeout ;
    }

    else if ( event.request == SYSINV_HOST_QUERY )
    {
        event.token.url = MTC_INV_LABEL ;
        event.token.url.append( event.hostname.data() );

        event.payload = "" ;

        hlog ("%s sysinv query %s\n", event.hostname.c_str(), event.token.url.c_str());

        /* Bind the update handler */
        handler = &mtcInvApi_qry_Handler ;

        /* The type of HTTP request */
        event.type = EVHTTP_REQ_GET ;

        /* set the timeout */
        event.timeout = get_mtcInv_ptr()->sysinv_timeout ;
    }

    else if ( event.request == SYSINV_UPDATE )
    {
        event.token.url = MTC_INV_LABEL ;
        event.token.url.append( event.uuid.data() );

        /* Bind the generic handler */
        handler = &mtcHttpUtil_Handler ;

        /* The type of HTTP request */
        event.type = EVHTTP_REQ_PATCH ;
    }

    else if ( event.request == SYSINV_ADD )
    {
        event.token.url = MTC_INV_LABEL ;

        event.payload = "{" ;

        event.payload.append ("\"mgmt_ip\":\"") ;
        event.payload.append ( event.inv_info.ip );
        event.payload.append ("\"");

        event.payload.append (",\"mgmt_mac\":\"");
        event.payload.append ( event.inv_info.mac );
        event.payload.append ("\"");

        event.payload.append (",\"hostname\":\"");
        event.payload.append ( event.inv_info.name );
        event.payload.append ("\"");

        event.payload.append (",\"task\":\"\"");
        event.payload.append (",\"action\":\"none\"");

        event.payload.append (",\"personality\":\"");
        event.payload.append ( event.inv_info.type );
        event.payload.append ("\"");

        event.payload.append (",\"administrative\":\"");
        event.payload.append ( event.inv_info.admin );
        event.payload.append ("\"");

        event.payload.append (",\"operational\":\"");
        event.payload.append ( event.inv_info.oper );
        event.payload.append ("\"");

        event.payload.append (",\"availability\":\"");
        event.payload.append ( event.inv_info.avail );
        event.payload.append ("\"");

        event.payload.append (",\"bm_ip\":\"\"");

        if ( !event.inv_info.name.compare("controller-0") )
        {
            event.payload.append (",\"invprovision\":\"provisioned\"");
            event.payload.append ( "}");
        }

        /* Bind the unlock handler */
        handler = &mtcInvApi_add_Handler ;

        /* The type of HTTP request */
        event.type = EVHTTP_REQ_POST ;

        /* set the timeout */
        event.timeout = get_mtcInv_ptr()->sysinv_timeout ;
    }
    else if ( ( event.request == SYSINV_CONFIG_SHOW   ) ||
              ( event.request == SYSINV_CONFIG_MODIFY ))
    {
        /* Bind the unlock handler */
        handler = &mtcHttpUtil_Handler ;

        /* The type of HTTP request */
        if ( event.request == SYSINV_CONFIG_SHOW )
        {
            event.type = EVHTTP_REQ_GET ;
            event.token.url = MTC_INV_IUSER_LABEL ;
        }
        else if ( event.request == SYSINV_CONFIG_MODIFY )
        {
            event.type = EVHTTP_REQ_PATCH ;
            event.token.url = MTC_INV_IUSER_LABEL ;
            event.token.url.append ( event.uuid );
        }
        else
        {
            elog ("Unsupported request (%d)\n", event.request );

            event.status = FAIL_BAD_CASE ;
            goto mtcHttpUtil_api_request_done ;
        }

        /* set the timeout */
        event.timeout = get_mtcInv_ptr()->sysinv_timeout ;
    }
    else if (( event.request == VIM_HOST_DISABLED ) ||
             ( event.request == VIM_HOST_ENABLED  ) ||
             ( event.request == VIM_HOST_OFFLINE  ) ||
             ( event.request == VIM_HOST_FAILED   ) ||
             ( event.request == VIM_DPORT_OFFLINE ) ||
             ( event.request == VIM_DPORT_FAILED  ) ||
             ( event.request == VIM_DPORT_CLEARED ) ||
             ( event.request == VIM_DPORT_DEGRADED ))
    {
        event.token.url = MTC_VIM_LABEL;
        event.token.url.append(event.uuid);

        /* Bind the unlock handler */
        handler = &mtcHttpUtil_Handler ;

        /* The type of HTTP request */
        event.type = EVHTTP_REQ_PATCH ;

        /* set the timeout */
        event.timeout = HTTP_VIM_TIMEOUT ;
    }

    else if (( event.request == SMGR_QUERY_SWACT   ) ||
             ( event.request == SMGR_START_SWACT   ) ||
             ( event.request == SMGR_HOST_LOCKED   ) ||
             ( event.request == SMGR_HOST_UNLOCKED ) ||
             ( event.request == SMGR_HOST_DISABLED ) ||
             ( event.request == SMGR_HOST_ENABLED  ))
    {
        event.timeout = HTTP_SMGR_TIMEOUT ;
        handler = &mtcSmgrApi_Handler ;
        if ( event.request == SMGR_QUERY_SWACT )
        {
            event.type = EVHTTP_REQ_GET ;
        }
        else
        {
            event.type = EVHTTP_REQ_PATCH  ;
        }
    }
    else
    {
        slog ("%s Unsupported Request (%d)\n", event.hostname.c_str(), event.request);
        event.status = FAIL_BAD_CASE ;
        goto mtcHttpUtil_api_request_done ;
    }

    /* Establish connection */
    if ( mtcHttpUtil_connect_new ( event ))
    {
        event.status = FAIL_CONNECT ;
        event.conn = NULL ;
        goto mtcHttpUtil_api_request_done ;
    }

    /* Create request */
    if ( handler == &mtcHttpUtil_Handler )
    {
        if ( mtcHttpUtil_request_new ( event, handler ))
        {
            event.status = FAIL_REQUEST_NEW ;
            goto mtcHttpUtil_api_request_done ;
        }
    }
    else
    {
        if ( mtcHttpUtil_request_old ( event, handler ))
        {
            event.status = FAIL_REQUEST_NEW ;
            goto mtcHttpUtil_api_request_done ;
        }
    }

    if ( event.request != KEYSTONE_TOKEN )
    {
        event.address = event.token.url ;
        jlog ("%s Address : %s\n", event.hostname.c_str(), event.token.url.c_str());
    }

    if (( event.type != EVHTTP_REQ_GET ) &&
        ( event.type != EVHTTP_REQ_DELETE ))
    {
        /* Add payload to the output buffer but only for PUT, POST and PATCH requests */
        if ( mtcHttpUtil_payload_add ( event ))
        {
            event.status = FAIL_PAYLOAD_ADD ;
            goto mtcHttpUtil_api_request_done ;
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
        hdrs.entry[hdr_entry].value = mtcHttpUtil_payload_len ( &event );
        hdr_entry++;
    }

    hdrs.entry[hdr_entry].key   = "User-Agent" ;
    hdrs.entry[hdr_entry].value = "mtce/1.0" ;
    hdr_entry++;

    hdrs.entry[hdr_entry].key   = "Content-Type" ;
    hdrs.entry[hdr_entry].value = "application/json" ;
    hdr_entry++;

    hdrs.entry[hdr_entry].key   = "Accept" ;
    hdrs.entry[hdr_entry].value = "application/json" ;
    hdr_entry++;

    if (( event.request != KEYSTONE_TOKEN     ) &&
        ( event.request != VIM_HOST_DISABLED  ) &&
        ( event.request != VIM_HOST_ENABLED   ) &&
        ( event.request != VIM_HOST_OFFLINE   ) &&
        ( event.request != VIM_HOST_FAILED    ) &&
        ( event.request != VIM_DPORT_OFFLINE  ) &&
        ( event.request != VIM_DPORT_FAILED   ) &&
        ( event.request != VIM_DPORT_CLEARED  ) &&
        ( event.request != VIM_DPORT_DEGRADED ))
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
    if ( mtcHttpUtil_header_add ( &event, &hdrs ))
    {
        event.status = FAIL_HEADER_ADD ;
        goto mtcHttpUtil_api_request_done ;
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

    if ( event.request == KEYSTONE_TOKEN )
    {
        string path = MTC_POST_KEY_LABEL ;
        event.address = path ;
        event.prefix_path += path;
        jlog ("%s Keystone Address : %s\n", event.hostname.c_str(), event.prefix_path.c_str());
        event.status = evhttp_make_request ( event.conn, event.req, event.type, event.prefix_path.data());
    }
    else
    {
        event.status = evhttp_make_request ( event.conn, event.req, event.type, event.token.url.data());
    }
    if ( event.status == PASS )
    {
        evhttp_connection_set_timeout(event.req->evcon, event.timeout);

        /* Default to retry for both blocking and non-blocking command */
        event.status = RETRY ;
        if ( event.blocking == true )
        {
            event.log_prefix = event.hostname ;
            event.log_prefix.append (" ");
            event.log_prefix.append (event.service) ;
            event.log_prefix.append (" ");
            event.log_prefix.append (event.operation) ;
            hlog ("%s Requested (blocking) (to:%d)\n", event.log_prefix.c_str(), event.timeout);

            /* Send the message with timeout */
            event_base_dispatch(event.base);

            goto mtcHttpUtil_api_request_done ;
        }
        else if (( event.request == SYSINV_UPDATE ) ||
                 ( event.request == SYSINV_CONFIG_SHOW ) ||
                 ( event.request == SYSINV_CONFIG_MODIFY ) ||
                 ( event.request == VIM_HOST_DISABLED) ||
                 ( event.request == VIM_HOST_ENABLED ) ||
                 ( event.request == VIM_HOST_OFFLINE ) ||
                 ( event.request == VIM_HOST_FAILED  ) ||
                 ( event.request == VIM_DPORT_OFFLINE ) ||
                 ( event.request == VIM_DPORT_FAILED  ) ||
                 ( event.request == VIM_DPORT_CLEARED ) ||
                 ( event.request == VIM_DPORT_DEGRADED) ||
                 ( event.request == SMGR_QUERY_SWACT) ||
                 ( event.request == SMGR_START_SWACT) ||
                 ( event.request == KEYSTONE_TOKEN ))
        {
            if ( event.operation.compare(SYSINV_OPER__UPDATE_UPTIME) )
            {
                hlog ("%s Dispatched (to:%d)\n", event.log_prefix.c_str(), event.timeout);
            }

            /*
             * non-blocking event_base_loop can return ...
             *
             *  0 - command complete ; data available
             *  1 - command dispatched but not complete ; no data available
             * -1 - error in dispatch ; check errno
             *
             */
            event.active = true ;
            rc = event_base_loop(event.base, EVLOOP_NONBLOCK);

#ifdef WANT_FIT_TESTING
            string value = "" ;
            if ( daemon_want_fit ( FIT_CODE__FAIL_SWACT, event.hostname, "query", value ))
            {
                if ( value == "-1" )
                    rc = -1 ;
                else
                    rc = atoi(value.data());
            }
#endif
            if (( rc == 0 ) || // Dispatched and done with Data ready
                ( rc == 1 ))   // Dispatched but no response yet
            {
                if (( event.request == SMGR_QUERY_SWACT ) ||
                    ( event.request == SMGR_START_SWACT ))
                {
                    ilog ("%s dispatched%s\n",
                              event.log_prefix.c_str(),
                              rc ? "" : " ; data ready" );
                }
                rc = PASS ;
            }
            else
            {
                elog ("%s command dispatch failed (%d)\n",
                          event.log_prefix.c_str(), errno );
                event.active = false ;
                rc = FAIL_REQUEST ;
            }
            return (rc);
        }
        else
        {
            /* Catch all but should not be */
            event.log_prefix = event.hostname ;
            event.log_prefix.append (" ");
            event.log_prefix.append (event.service) ;
            event.log_prefix.append (" ");
            event.log_prefix.append (event.operation) ;
            slog ("%s Requested (blocking) (to:%d) ----------------------------------------\n", event.log_prefix.c_str(), event.timeout );

            event_base_dispatch(event.base);

            goto mtcHttpUtil_api_request_done ;
        }
    }
    else
    {
        elog ("%s Call to 'evhttp_make_request' failed (rc:%d)\n",
                  event.hostname.c_str(), rc);
    }

    return (FAIL_MAKE_REQUEST);

mtcHttpUtil_api_request_done:


    if ( event.blocking == true )
    {
        mtcHttpUtil_free_conn ( event );
        mtcHttpUtil_free_base ( event );

        /**
         *  If tere is an authentication error then request a new token and
         *  return the error to the caller so that the request can be retried
         **/
        if (( event.status == FAIL_AUTHENTICATION ) ||
            ( event.status == MTC_HTTP_UNAUTHORIZED ))
        {
            /* Find the host this handler instance is being run against */
            nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
            tokenUtil_new_token ( obj_ptr->tokenEvent, obj_ptr->my_hostname );
            mtcHttpUtil_free_conn ( obj_ptr->tokenEvent );
            mtcHttpUtil_free_base ( obj_ptr->tokenEvent );
            event.status = FAIL_AUTHENTICATION ;
        }
    }

    return (event.status);
}


/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_receive
 *
 * Description: Issues a non-blocking call to event_base_loop to receive
 *              from the connection for the specified libevent
 *
 * @param event is a reference to the callers libEvent struct
 * to receive against
 *
 * @return RETRY if there is no data to receive on the open connection
 * Otherwise the status of the command that was received.
 *
 * ************************************************************************/

int mtcHttpUtil_receive ( libEvent & event )
{
    int rc = event_base_loop(event.base, EVLOOP_NONBLOCK) ;
    switch ( rc )
    {
        case FAIL: /* 1 - returns 1 if there was nothing to receive , MAY HAVE ALREADY BEEN RECEIVED */
        case PASS: /* 0 - returns 0 if there was a successful receive of something */
        {
            // hlog1 ("%s receive O.K. (active:%d)\n", event.log_prefix.c_str(), event.active );

            /* Check in-progress flag */
            if ( event.active == false )
            {
                if ( event.status == RETRY )
                {
                    event.status = FAIL_RETRY ;
                }
                else
                {
                    /* return the reported handler status */
                    rc = event.status ;
                }
                /* the log_event is called in the mtcHttpUtil_handler */
                if (( event.request == SYSINV_UPDATE ) ||
                    ( event.request == SYSINV_CONFIG_SHOW ) ||
                    ( event.request == SYSINV_CONFIG_MODIFY ) ||
                    ( event.request == KEYSTONE_TOKEN ))
                {
                   ;
                }
            }
            else
            {
                rc = RETRY ;
            }
            break ;
        }

        /* event_base_loop returns -1 for some unhandled error in the backend */
        case -1: 
        {
            event.active = false ;
            elog ("%s Failed event_base_loop (-1)\n", event.log_prefix.c_str()); 
            rc = FAIL ;
            break ;
        }
        default:
        {
            event.active = false ;
            slog ("%s Failed event_base_loop - Unexpected Return (%d)\n", 
                      event.log_prefix.c_str(), rc );
            rc = FAIL ;
            break ;
        }
    }
    if ( rc != RETRY )
    {
        mtcHttpUtil_free_conn  ( event );
        mtcHttpUtil_free_base  ( event );
    }
    return (rc);
}

/* ***********************************************************************
 *
 * Name       : mtcHttpUtil_get_length
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

int mtcHttpUtil_get_length ( libEvent & event )
{
    event.response_len = evbuffer_get_length (event.req->input_buffer);
    if ( event.response_len == 0 )
    {
        hlog ("%s Request Failed - Zero Length Response\n",
                event.log_prefix.c_str());
        event.status = FAIL_JSON_ZERO_LEN ;
    }
//    else if ( event.response_len > MAX_EVENT_LEN )
//    {
//        elog ("%s Request Failed - Length Too Long (%d:%ld)\n",
//                  event.log_prefix.c_str(), MAX_EVENT_LEN, event.response_len );
//        
//        event.status = FAIL_JSON_TOO_LONG ;
//    }
    return ( event.response_len );
}

/* Load the response string into the event struct */
int mtcHttpUtil_get_response ( libEvent & event )
{
    if ( mtcHttpUtil_get_length ( event ) )
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

        /* Terminate the buffer , this is where the +1 above is required. 
         * Without it there is memory corruption reported by Linux */
         *(stack_buf_ptr+event.response_len) = '\0';

        /* Store the response */
        event.response = stack_buf_ptr ;

        free (stack_buf_ptr);
    }
    return ( event.status );
}

void mtcHttpUtil_log_event ( libEvent & event )
{
    msgSock_type * mtclogd_ptr = get_mtclogd_sockPtr ();

    string info = "" ;
    string event_sig = daemon_get_cfg_ptr()->debug_event ;
    
    send_log_message ( get_mtclogd_sockPtr(), event.hostname.data(), &rest_api_filename[0], &event.req_str[0] );

    if (( event.payload.length()) && 
        ((!string_contains(event.payload,"token")) && 
         (!string_contains(event.payload,"assword"))))
    {
        snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
                   "%s [%5d] %s -> Payload : %s", pt(), getpid(), event.log_prefix.c_str(), event.payload.c_str());
        send_log_message ( mtclogd_ptr, event.hostname.data(), &rest_api_filename[0], &rest_api_log_str[0] );
    }

    /* Don't log update uptime and update task responses nor
     * responses that have token or password in them */
    if ( (event.response.length()) && 
         (event.operation.compare(SYSINV_OPER__UPDATE_UPTIME)) &&
         (event.operation.compare(SYSINV_OPER__UPDATE_TASK)) &&
         (event.operation.compare(SYSINV_OPER__FORCE_TASK)) &&
         ((!string_contains(event.response,"token")) && 
          (!string_contains(event.response,"assword"))))
    {
        snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
                   "%s [%5d] %s -> Response: %s", pt(), getpid(), event.log_prefix.c_str(), event.response.c_str());
        send_log_message ( mtclogd_ptr, event.hostname.data(), rest_api_filename, &rest_api_log_str[0] );
    }

    snprintf (&rest_api_log_str[0], MAX_API_LOG_LEN-1, 
          "%s [%5d] %s %s '%s' seq:%d -> Status  : %d {execution time %ld.%06ld secs}\n",
          pt(), getpid(),
          event.hostname.c_str(),
          event.service.c_str(), 
          event.operation.c_str(),
          event.sequence,
          event.http_status,
          event.diff_time.secs, 
          event.diff_time.msecs );

    if ( ( event.diff_time.secs > 2 ) || ( event.http_status != HTTP_OK ) )
    {
        int len = strlen (rest_api_log_str) ;
        snprintf (&rest_api_log_str[len-1], 20, "  <---------");
    }
    send_log_message ( mtclogd_ptr, event.hostname.data(), &rest_api_filename[0], &rest_api_log_str[0] );
}

void mtcHttpUtil_event_info ( libEvent & event )
{
    ilog ("--- %s request to %s.%d Status:%d \n", 
            event.log_prefix.c_str(), 
            event.ip.c_str(), 
            event.port,
            event.status);
    ilog ("--- Address : %s\n", event.address.c_str());
    ilog ("--- Payload : %s\n", event.payload.c_str());
    ilog ("--- Response: %s\n", event.response.c_str());
    ilog ("--- TokenUrl: %s\n", event.token.url.c_str());
}

libEvent & nodeLinkClass::getEvent ( struct event_base * base_ptr)
{
    struct node * ptr = static_cast<struct node *>(NULL) ;

    /* check for empty list condition */
    if ( head == NULL )
        return (nullEvent) ;

    if ( base_ptr == NULL )
        return (nullEvent) ;

    if ( base_ptr == (struct event_base *)&tokenEvent )
    {
        hlog1 ("%s Found libEvent Pointer (%p) tokenEvent (%p) Active : %s\n", 
                  tokenEvent.log_prefix.c_str(), 
                  base_ptr, &tokenEvent,
                  tokenEvent.active ? "Yes" : "No" );
        return (tokenEvent);
    }

    if ( base_ptr == (struct event_base *)&smgrEvent )
    {
        hlog1 ("%s Found libEvent Pointer (%p) smgrEvent (%p) Active : %s\n", 
                  smgrEvent.log_prefix.c_str(), 
                  base_ptr, &smgrEvent,
                  smgrEvent.active ? "Yes" : "No" );
        return (smgrEvent);
    }

    if ( base_ptr == (struct event_base *)&sysinvEvent )
    {
        hlog1 ("%s Found libEvent Pointer (%p) sysinvEvent (%p) Active : %s\n", 
                  sysinvEvent.log_prefix.c_str(), 
                  base_ptr, &sysinvEvent,
                  sysinvEvent.active ? "Yes" : "No" );
        return (sysinvEvent);
    }

    /* Now search the node list */
    for ( ptr = head ; ptr != NULL ; ptr = ptr->next )
    {
        if ( base_ptr == (struct event_base *)&ptr->thisReq )
        {
           if ( ptr->thisReq.active == true )
           {
               if ( workQueue_present ( ptr->thisReq ) == true )
               {
                   hlog2 ("%s found and is active\n", ptr->thisReq.log_prefix.c_str());
                   return (ptr->thisReq) ;
               }
               else
               {
                   slog ("%s is active but not in work queue\n", ptr->thisReq.log_prefix.c_str());
                   ptr->thisReq.active = false ;
               }
           }
           else
           {
               if ( workQueue_present ( ptr->thisReq ) == true )
               {
                   slog ("%s is not active ; removing from workQueue\n", ptr->thisReq.log_prefix.c_str() );
                   workQueue_del_cmd ( ptr, ptr->thisReq.sequence );
               }
               else
               {
                   wlog ("%s is not active and not in workQueue\n", ptr->thisReq.log_prefix.c_str() );
               }
           }
           return (nullEvent) ;
        }

        if ( ptr->next == NULL )
            break ;
    }

    wlog ("libEvent for base pointer (%p) not found\n", base_ptr );
    return (nullEvent) ;
}

/* HTTP Request Handler Dispatcher */
void nodeLinkClass::mtcHttpUtil_handler ( struct evhttp_request *req, void *arg )
{
    int rc = PASS ;

    req = req ;

    /* Find the host this handler instance is being run against */
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;

    /* Make sure we get a valid event to work on */
    libEvent & event = obj_ptr->getEvent ( (struct event_base *)arg ) ;
    if (( event.request >= SERVICE_LAST ) || ( event.request == SERVICE_NONE ))
    {
        slog ("HTTP Event Lookup Failed for http base (%p) <------\n", arg);
        return ;
    }


    /* Check the HTTP Status Code */
    event.status = mtcHttpUtil_status ( event ) ;
    if ( event.status == HTTP_NOTFOUND )
    {
        elog ("%s returned (Not-Found) (%d)\n", 
                  event.log_prefix.c_str(), 
                  event.status);
        event.status = PASS ;
    }

    // hlog ("%s Status:%d Req:%p\n", event.log_prefix.c_str(), event.status);

    else if (( event.status != PASS ) && ( ! req ))
    {
        elog ("%s Request Timeout (%d)\n", 
                   event.log_prefix.c_str(),
                   event.timeout);

        event.status = FAIL_TIMEOUT ;
        goto _handler_done ;
    }

    else if ( event.status != PASS )
    {
        goto _handler_done ;
    }
   
    /* Delete commands don't have a response unless there is an error.
     * Deal with this as a special case - 
     * Currently only Neutron uses the delete */
    if ( event.type == EVHTTP_REQ_DELETE )
    {
        if ( mtcHttpUtil_get_length ( event ) != 0 ) 
        {
            /* Preserve the incoming status over the get response */
            rc = event.status ;
            mtcHttpUtil_get_response ( event ) ;
            event.status = rc ;
        }
        if (event.status == FAIL_JSON_ZERO_LEN )
            event.status = PASS ;
    }
    else if ( mtcHttpUtil_get_response ( event ) != PASS )
    {
        elog ("%s failed to get response\n", event.log_prefix.c_str());
        goto _handler_done ;
    }

    if ( event.request == KEYSTONE_TOKEN )
    {
        /* TODO: Deal with Failure */
        ilog ("CALLING TOKENUTIL_HANDLER !!!!\n");
        rc = tokenUtil_handler ( event );
        if ( rc )
        {
            wlog ("%s tokenUtil_handler reported failure (%d)\n", event.hostname.c_str(), rc );
        }
    }
    else if (( event.request == SYSINV_UPDATE )||
             ( event.request == SYSINV_CONFIG_SHOW ) ||
             ( event.request == SYSINV_CONFIG_MODIFY ))
    {
        /* TODO: Deal with Failure */
        rc = mtcInvApi_handler ( event );
        if ( rc )
        {
            wlog ("%s mtcInvApi_handler reported failure (%d)\n", event.hostname.c_str(), rc );
        }
    }
    else if (( event.request == VIM_HOST_DISABLED )||
             ( event.request == VIM_HOST_ENABLED ) ||
             ( event.request == VIM_HOST_OFFLINE ) ||
             ( event.request == VIM_HOST_FAILED  ) ||
             ( event.request == VIM_DPORT_OFFLINE) ||
             ( event.request == VIM_DPORT_FAILED ) ||
             ( event.request == VIM_DPORT_CLEARED) ||
             ( event.request == VIM_DPORT_DEGRADED ))
    {
        rc = mtcVimApi_handler ( event );
        if ( rc )
        {
            wlog ("%s mtcVimApi_handler reported failure (%d)\n", event.hostname.c_str(), rc );
        }
    }
    else
    {
        wlog ( "%s has unknown request id (%d)\n",
                   event.log_prefix.c_str(), 
                   event.request );
    }

_handler_done:

   event.active = false ;

   gettime   ( event.done_time );
   timedelta ( event.send_time, event.done_time, event.diff_time );

// Redundant log - already logged in the work queue FSM
//   if ( event.status )
//   {
//       elog ( "%s Failed (rc:%d)\n",
//                  event.log_prefix.c_str(),
//                  event.status );
//   }
   mtcHttpUtil_log_event ( event );

   if ( event.blocking == false )
   {
       // mtcHttpUtil_free_conn ( event );
       // mtcHttpUtil_free_base ( event );

       /**
        *  If tere is an authentication error then request a new token and
        *  return the error to the caller so that the request can be retried
        **/
       if (( event.status == FAIL_AUTHENTICATION ) ||
           ( event.status == MTC_HTTP_UNAUTHORIZED ))
       {
           /* Find the host this handler instance is being run against */
           nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
           tokenUtil_new_token ( obj_ptr->tokenEvent, obj_ptr->my_hostname );
           mtcHttpUtil_free_conn ( obj_ptr->tokenEvent );
           mtcHttpUtil_free_base ( obj_ptr->tokenEvent );
           event.status = FAIL_AUTHENTICATION ;
       }
   }
}


/* HTTP Handler Dispatcher - wrapper abstracted from nodeLinkClass */
void mtcHttpUtil_Handler ( struct evhttp_request *req, void *arg )
{
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    obj_ptr->mtcHttpUtil_handler ( req , arg );
}
