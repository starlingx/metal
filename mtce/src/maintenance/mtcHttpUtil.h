#ifndef __INCLUDE_MTCHTTPUTIL_H__
#define __INCLUDE_MTCHTTPUTIL_H__
/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance ...
  * 
  * libevent HTTP support utilities and control structure support header
  */

#include <iostream>         /* for ... string */
#include <evhttp.h>         /* for ... http libevent client */

using namespace std;

#include "httpUtil.h"


void mtcHttpUtil_init ( void );

int mtcHttpUtil_event_init ( libEvent * ptr , 
                               string   hostname,
                               string   service, 
                               string   ip, 
                                  int   port );

/** Maximum number of headers that can be added to an HTTP message. */
#define MAX_HEADERS (10)

/** Add payload to the HTTP message body. */
int mtcHttpUtil_payload_add  ( libEvent & event );

/** Add all headers in header table to the HTTP connection message. */
int mtcHttpUtil_header_add   ( libEvent * ptr, http_headers_type * hdrs_ptr );

/** Create an HTTP request. */
int mtcHttpUtil_request_make ( libEvent * ptr, enum evhttp_cmd_type type, string path );

/** Open a connection to an HTTP server. */
int mtcHttpUtil_connect_new  ( libEvent & event );

/** Get a new HTTP request pointer. */
int mtcHttpUtil_request_new  ( libEvent & event,
                               void(*hdlr)(struct evhttp_request *, void *));

/** Common REST API Request Utility */ 
int mtcHttpUtil_api_request ( libEvent & event );

/** Common REST API Request Utility */ 
int mtcHttpUtil_request ( libEvent & event , bool block,
                          void(*hdlr)(struct evhttp_request *, void *));

/** Common REST API Receive Utility for non-blocking requests */ 
int mtcHttpUtil_receive ( libEvent & event );

/** HTTP response status checker */
int mtcHttpUtil_status ( libEvent & event );

/** Free the libEvent */
void mtcHttpUtil_free_base ( libEvent & event );

/** Free the event lib connection */
void mtcHttpUtil_free_conn ( libEvent & event );

/** TODO: FIXME: Get the payload string length. */
string mtcHttpUtil_payload_len ( libEvent * ptr );

/** Get the length of the json response */
int mtcHttpUtil_get_length ( libEvent & event );

/** Load the json response into the event struct */
int mtcHttpUtil_get_response ( libEvent & event );

/** print event filtered event */
void mtcHttpUtil_log_event ( libEvent & event );

void mtcHttpUtil_event_info ( libEvent & event );

const char * getHttpCmdType_str ( evhttp_cmd_type type );

#endif /* __INCLUDE_MTCHTTPUTIL_H__ */
