#ifndef __INCLUDE_HTTPUTIL_H__
#define __INCLUDE_HTTPUTIL_H__

/*
 * Copyright (c) 2013, 2016, 2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <iostream>         /* for ... string               */
#include <evhttp.h>         /* for ... http libevent client */
#include <time.h>
#include <list>
#include <fcntl.h>          /* for ... F_GETFL              */

using namespace std;

#include "nodeBase.h"
#include "timeUtil.h" /* for ... time_delta_type            */

/* HTTP Error Codes with no specific existing define MACRO */
#define MTC_HTTP_BAD_REQUEST          400
#define MTC_HTTP_UNAUTHORIZED         401
#define MTC_HTTP_FORBIDDEN            403
#define MTC_HTTP_METHOD_NOT_ALLOWED   405
#define MTC_HTTP_CONFLICT             409
#define MTC_HTTP_LENGTH_REQUIRED      411
#define MTC_HTTP_NORESPONSE           444
#define MTC_HTTP_UNPROCESSABLE_ENTITY 422

#define MTC_HTTP_ACCEPTED             202

#define EVENT_METHODS (EVHTTP_REQ_PATCH | \
                       EVHTTP_REQ_POST  | \
                       EVHTTP_REQ_GET   | \
                       EVHTTP_REQ_PUT   | \
                       EVHTTP_REQ_DELETE)

/** Maximum libevent response message size in bytes. */
// #define MAX_EVENT_LEN (163840)
#define MAX_URL_LEN    (200)

#define HTTP_VIM_TIMEOUT    (20)

#define HTTP_MAX_RETRIES    (3)

#define HTTP_SYSINV_CRIT_TIMEOUT   (20)
#define HTTP_SYSINV_NONC_TIMEOUT   (10)

#define HTTP_TOKEN_TIMEOUT         (30)
#define HTTP_KEYSTONE_GET_TIMEOUT  (10)
#define HTTP_SMGR_TIMEOUT          (20)
#define HTTP_VIM_TIMEOUT           (20)
#define HTTP_SECRET_TIMEOUT        (5)

#define SMGR_MAX_RETRIES    (3)

#define CLIENT_HEADER      "User-Agent"
#define CLIENT_SYSINV_1_0  "sysinv/1.0"
#define EVENT_SERVER       "HTTP Event Server"

#define SMGR_EVENT_SIG     "smgrEvent"
#define SYSINV_EVENT_SIG   "sysinvEvent"
#define SECRET_EVENT_SIG   "secretEvent"

#define KEYSTONE_SIG       "token"
#define SENSOR_SIG         "sensor"
#define SYSINV_SIG         "sysinv"
#define SMGR_SIG           "smgr"
#define VIM_SIG            "vim"
#define SECRET_SIG         "secret"

#define SYSINV_OPER__LOAD_HOST     "load host"
#define SYSINV_OPER__UPDATE_TASK   "update task"
#define SYSINV_OPER__FORCE_TASK    "force task"
#define SYSINV_OPER__UPDATE_UPTIME "update uptime"
#define SYSINV_OPER__UPDATE_VALUE  "update value"
#define SYSINV_OPER__UPDATE_STATE  "update state"
#define SYSINV_OPER__UPDATE_STATES "update states"
#define SYSINV_OPER__FORCE_STATES  "force states"
#define SYSINV_OPER__CONFIG_SHOW   "config show"
#define SYSINV_OPER__CONFIG_MODIFY "config modify"

#define VIM_HOST__DISABLED "disabled"
#define VIM_HOST__ENABLED   "enabled"
#define VIM_HOST__OFFLINE   "offline"
#define VIM_HOST__FAILED     "failed"

/** The workQueue_process FSM states */
typedef enum {
   HTTP__TRANSMIT      = 0,
   HTTP__RECEIVE_WAIT  = 1,
   HTTP__RECEIVE       = 2,
   HTTP__FAILURE       = 3,
   HTTP__RETRY_WAIT    = 4,
   HTTP__DONE_FAIL     = 5,
   HTTP__DONE_PASS     = 6,
   HTTP__STAGES        = 7
}  httpStages_enum ;

#define HTTP_RECEIVE_WAIT_MSEC (10)
#define HTTP_RETRY_WAIT_SECS   (10)

typedef struct
{
    string url      ; /**< Keystone server URL string         */
    string issued   ; /**< Timestamp token was issued         */
    string expiry   ; /**< Timestamp when token is expired    */
    string token    ; /**< The huge 3kb token                 */
    bool   refreshed; /**< set true when refreshed            */
    bool   delay    ; /**< trigger renew with small delay
                           error renewal - flood avoidance    */
} keyToken_type ;


typedef enum
{
    MTC_SECRET__START = 0,
    MTC_SECRET__GET_REF,
    MTC_SECRET__GET_REF_FAIL,
    MTC_SECRET__GET_REF_RECV,
    MTC_SECRET__GET_PWD,
    MTC_SECRET__GET_PWD_FAIL,
    MTC_SECRET__GET_PWD_RECV,
    MTC_SECRET__STAGES,
} mtc_secretStages_enum ;

typedef struct
{
    string                reference;
    string                payload  ;
    mtc_secretStages_enum stage    ;
} barbicanSecret_type;

/** All supported Request Type Enums */
typedef enum {
    SERVICE_NONE,

    SYSINV_ADD,
    SYSINV_GET,
    SYSINV_HOST_QUERY,
    SYSINV_UPDATE,

    SYSINV_CONFIG_SHOW,
    SYSINV_CONFIG_MODIFY,

    SYSINV_SENSOR_LOAD,
    SYSINV_SENSOR_LOAD_GROUPS,
    SYSINV_SENSOR_LOAD_GROUP,
    SYSINV_SENSOR_ADD,
    SYSINV_SENSOR_ADD_GROUP,
    SYSINV_SENSOR_DEL,
    SYSINV_SENSOR_DEL_GROUP,
    SYSINV_SENSOR_MOD,
    SYSINV_SENSOR_MOD_GROUP,
    SYSINV_SENSOR_GROUP_SENSORS,

    VIM_UPDATE,
    VIM_HOST_DISABLED,
    VIM_HOST_ENABLED,
    VIM_HOST_OFFLINE,
    VIM_HOST_FAILED,
    VIM_DPORT_FAILED,
    VIM_DPORT_CLEARED,
    VIM_DPORT_DEGRADED,
    VIM_DPORT_OFFLINE,
    VIM_HOST_QUERY,

    VIM_HOST_STATE_QUERY,
    VIM_HOST_INSTANCE_QUERY,
    VIM_HOST_INSTANCE_FAILED,
    VIM_HOST_INSTANCE_STATUS,
    VIM_HOST_INSTANCE_NOTIFY,

    SMGR_START_SWACT,
    SMGR_QUERY_SWACT,
    SMGR_HOST_UNLOCKED,
    SMGR_HOST_LOCKED,
    SMGR_HOST_ENABLED,
    SMGR_HOST_DISABLED,

    KEYSTONE_TOKEN,
    KEYSTONE_GET_TOKEN,
    KEYSTONE_GET_SERVICE_LIST,
    KEYSTONE_GET_ENDPOINT_LIST,

    BARBICAN_GET_SECRET,
    BARBICAN_READ_SECRET,

    SERVICE_LAST
} libEvent_enum ;


/** Local event control structure for REST API services
 *
 *  Keystone, Barbican and Inventory
 *
 */
struct libEvent
{
    /** Execution Controls */
    httpStages_enum state         ; /**< This http request FSM state */
    int    sequence               ; /**< Event sequence number       */
    bool   mutex                  ; /**< single operation at a time  */
    bool   active                 ; /**< true if waiting on response */
    int    stuck                  ; /**< Count mutex active stuck state */
    bool   blocking               ; /**< true if command is blocking */
    bool   found                  ; /**< true if query was found     */
    int    timeout                ; /**< Request timeout             */
    int    count                  ; /**< retry recover counter       */
    int    fails                  ; /**< fail counter                */
    int    retries                ; /**< number of retries on failure*/
    int    cur_retries            ;
    int    max_retries            ;
    bool   noncritical            ; /**< true: event is non-ctitical */
    int    rx_retry_cnt           ; /**< help avoid infinite rx retry*/
    int    rx_retry_max           ; /**< each cmd can have a max     */
    /* HTTP request Info */
    enum   evhttp_cmd_type    type; /**< HTTP Request Type ; PUT/GET */
    struct event_base        *base; /**< libEvent API service base   */
    struct evhttp_connection *conn; /**< HTTP connection ptr         */
    struct evhttp_request    *req ; /**< HTTP request ptr            */
    struct evbuffer          *buf ; /**< HTTP output buffer ptr      */
    struct evbuffer_ptr       evp ; /**< HTTP output buffer ptr      */

    int                        fd ;
    struct sockaddr_in       addr ;
    struct evhttp          *httpd ;

    string log_prefix             ; /**< log prefix for this event   */

    /** Service Specific Request Info */
    libEvent_enum       request   ; /**< Specify the request command */
    keyToken_type       token     ; /**< Copy of the active token    */
    string service                ; /**< Service being executed      */
    string hostname               ; /**< Target hostname             */
    string uuid                   ; /**< The UUID for this request   */
    string new_uuid               ; /**< The UUID created & returned */
    string ip                     ; /**< Server IP address           */
    int    port                   ; /**< Server port number          */
    string operation              ; /**< Specify the operation       */
    string information            ;
    string key                    ;
    string value                  ;
    string prefix_path            ;
    string label                  ; /**< typically a response label  */
    string entity_path            ; /**< HTTP entity request string  */
    string entity_path_next       ; /**< next entity request string  */
    string address                ; /**< http url address            */
    string payload                ; /**< the request's payload       */
    string user_agent             ; /**< set the User-Agent header   */

    /** Result Info */
    int    status                 ; /**< Execution Status            */
    int    http_status            ; /**< raw http returned status    */
    int    exec_time_msec         ; /**< execution time in msec      */
    node_inv_type        inv_info ;
    size_t response_len           ; /**< the json response length    */
    string response               ; /**< the json response string    */
    string result                 ; /**< Command specific result str */

    /* Endpoint strings */
    string admin_url              ;
    string internal_url           ;
    string public_url             ;

    time_debug_type send_time ; /**< Request Dispatch Timestamp  */
    time_debug_type done_time ; /**< Response Handler Timestamp  */
    time_delta_type diff_time ; /**< how long the command handling took */

    bool  low_wm ;
    bool  med_wm ;
    bool high_wm ;

    int (*handler) (struct libEvent &) ;

    char req_str[MAX_API_LOG_LEN] ;

    unsigned long long prev_time ; /* latency log candidate start (prev) time */
    unsigned long long this_time ; /* ... end (now or this) time              */

} ;


typedef struct
{
    struct event_base *  base_ptr ;
    struct libEvent   * event_ptr ;
} event_base_pair_type ;

typedef struct
{
    int  elements ;
    list<event_base_pair_type> pair_list ;
} event_base_list_type ;



/** Maximum number of headers that can be added to an HTTP message. */
#define MAX_HEADERS (10)

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

void httpUtil_init ( void );

int httpUtil_event_init ( libEvent * ptr ,
                            string   hostname,
                            string   service,
                            string   ip,
                               int   port );

/** Add payload to the HTTP message body. */
int httpUtil_payload_add  ( libEvent & event );

/** Add all headers in header table to the HTTP connection message. */
int httpUtil_header_add   ( libEvent * ptr, http_headers_type * hdrs_ptr );

/** Create an HTTP request. */
int httpUtil_request_make ( libEvent * ptr, enum evhttp_cmd_type type, string path );

/** Open a connection to an HTTP server. */
int httpUtil_connect ( libEvent & event );

/** Get a new HTTP request pointer. */
int httpUtil_request  ( libEvent & event,
                        void(*hdlr)(struct evhttp_request *, void *));

/** Common REST API Request Utility */
int httpUtil_api_request ( libEvent & event );

/** Common REST API Request Utility */
int httpUtil_request ( libEvent & event , bool block,
                          void(*hdlr)(struct evhttp_request *, void *));

/** Common REST API Receive Utility for non-blocking requests */
int httpUtil_receive ( libEvent & event );

/** HTTP response status checker */
int httpUtil_status ( libEvent & event );

/** Free the libEvent */
void httpUtil_free_base ( libEvent & event );

/** Free the event lib connection */
void httpUtil_free_conn ( libEvent & event );

/** TODO: FIXME: Get the payload string length. */
string httpUtil_payload_len ( libEvent * ptr );

/** Get the length of the json response */
int httpUtil_get_length ( libEvent & event );

/** Load the json response into the event struct */
int httpUtil_get_response ( libEvent & event );

/** print event filtered event */
void httpUtil_log_event ( libEvent * event );

void httpUtil_event_info ( libEvent & event );

const char * getHttpCmdType_str ( evhttp_cmd_type type );

/* HTTP Server setup utilities */
int httpUtil_bind  ( libEvent & event );

int httpUtil_setup ( libEvent & event,
                     int        supported_methods,
                     void(*hdlr)(struct evhttp_request *, void *));
/* Cleanup */
void httpUtil_fini ( libEvent & event );
void httpUtil_look ( libEvent & event );

#endif /* __INCLUDE_HTTPUTIL_H__ */
