#ifndef __INCLUDE_rmonHTTPUTIL_H__
#define __INCLUDE_rmonHTTPUTIL_H__
/*
 * Copyright (c) 2013, 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform rmon
  * 
  * libevent HTTP support utilities and control structure support header
  */

#include <iostream>         /* for ... string */
#include <evhttp.h>         /* for ... http libevent client */

using namespace std;

/** Maximum libevent response message size in bytes. */
#define MAX_EVENT_LEN (16384)

#define HTTP_VSWITCH_TIMEOUT  (10)
#define HTTP_REMOTELOGGING_TIMEOUT  (10)
#define HTTP_CEILOMETER_TIMEOUT (10)

#define VSWITCH_EVENT_SIG "vswitchEvent"
#define REMOTE_LOGGING_EVENT_SIG "remoteLoggingEvent"
#define CEILOMETER_EVENT_SIG "ceilometerEvent"

/** Request Type Enums for the common rmonHttpUtil_request utility */
typedef enum {
    RMON_SERVICE_NONE,
    VSWITCH_REQUEST,
    REMOTE_LOGGING_REQUEST,
    CEILOMETER_SAMPLE_CREATE
} rmon_libEvent_enum ;

/** Local event control structure for REST API services
 * 
 *  Keystone and Inventory
 *
 */
typedef struct
{
    /** Execution Controls */
    bool   mutex                  ; /**< single operation at a time  */
    bool   active                 ; /**< true if waiting on response */
    int    stuck                  ; /**< Count mutex active stuck state */
    int    status                 ; /**< Execution Status            */
    string result                 ; /**< Command specific result str */
    bool   found                  ; /**< true if query was found     */
    int    timeout                ; /**< Request timeout             */
    int    count                  ; /**< retry recover counter       */
    int    fails                  ; /**< fail counter                */
    int    retries                ; /**< retry counter ; for receive */
    string service                ; /**< Service being executed      */
    string hostname               ; /**< Target hostname             */
    string uuid                   ; /**< The UUID for this request   */
    string ip                     ; /**< Server IP address           */
    rmon_libEvent_enum request    ;
    int    port                   ; /**< Server port number          */
    string user_agent             ; /**< set the User-Agent header   */

    enum   evhttp_cmd_type    type; /**< HTTP Request Type ; PUT/GET */
    struct event_base        *base; /**< libEvent API service base   */
    struct evhttp_connection *conn; /**< HTTP connection ptr         */
    struct evhttp_request    *req ; /**< HTTP request ptr            */
    struct evbuffer          *buf ; /**< HTTP output buffer ptr      */
    struct evbuffer_ptr       evp ; /**< HTTP output buffer ptr      */

    /** Timestamps used to measure the responsiveness of REST API    */
    struct timespec start_ts      ; /**< Request Dispatch Timestamp  */
    struct timespec stop_ts       ; /**< Response Handler Timestamp  */

    string entity_path            ; /**< HTTP entity request string  */
    string entity_path_next       ; /**< next entity request string  */

    /** Result Info */
    node_inv_type * inv_info_ptr  ; /**< Inventory data pointer      */
    string address                ; /**< http url address            */
    string payload                ; /**< the request's payload       */
    size_t response_len           ; /**< the json response length    */
    string response               ; /**< the json response string    */
} libEvent_type;

int rmonHttpUtil_libEvent_init ( libEvent_type * ptr , 
                                       string   service, 
                                       string   ip, 
                                          int   port );

void rmonHttpUtil_start_timer ( libEvent_type & event );
void rmonHttpUtil_stop_timer  ( libEvent_type & event );
void rmonHttpUtil_log_time    ( libEvent_type & event );


/** Maximum number of headers that can be added to an HTTP message. */
#define MAX_HEADERS (10)

#if 0
/** A header entry type. */
typedef struct
{
    string key   ; /**< the header label. */
    string value ; /**< the header value. */
} http_header_entry_type;

/** The header entry table. */
typedef struct
{
    int entries ; /**< Number of entries in the header table.    */
    http_header_entry_type entry[MAX_HEADERS]; /**< entry array. */
} http_headers_type ;
#endif

/** Add payload to the HTTP message body. */
int rmonHttpUtil_payload_add  ( libEvent_type & event );

/** Add all headers in header table to the HTTP connection message. */
int rmonHttpUtil_header_add   ( libEvent_type * ptr, http_headers_type * hdrs_ptr );

/** Create an HTTP request. */
int rmonHttpUtil_request_make ( libEvent_type * ptr, enum evhttp_cmd_type type, string path );

/** Open a connection to an HTTP server. */
int rmonHttpUtil_connect_new  ( libEvent_type & event );

/** Get a new HTTP request pointer. */
int rmonHttpUtil_request_new  ( libEvent_type & event,
                               void(*hdlr)(struct evhttp_request *, void *));

/** Common REST API Request Utility */ 
int rmonHttpUtil_api_request ( rmon_libEvent_enum request,
                              libEvent_type & event, 
                              string command_path );

/** Common REST API Request Utility */ 
int rmonHttpUtil_request ( libEvent_type & event , bool block,
                          void(*hdlr)(struct evhttp_request *, void *));

/** Common REST API Receive Utility for non-blocking requests */ 
int rmonHttpUtil_receive ( libEvent_type & event );

/** HTTP response status checker */
int rmonHttpUtil_status ( libEvent_type & event );

/** TODO: FIXME: Get the payload string length. */
string rmonHttpUtil_payload_len ( libEvent_type * ptr );

/** Get the length of the json response */
int rmonHttpUtil_get_length ( libEvent_type & event );

/** Load the json response into the event struct */
int rmonHttpUtil_get_response ( libEvent_type & event );

/** print event filtered event */
void rmonHttpUtil_log_event ( libEvent_type & event );

#endif /* __INCLUDE_rmonHTTPUTIL_H__ */
