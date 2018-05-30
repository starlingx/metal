/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance Daemon
  */


typedef struct
{
    struct sockaddr_in      addr   ;
    struct event_base     * base   ;
    struct evhttp_request * req    ;
    struct evhttp         * httpd  ;
    int                     fd     ;
    int                     port   ;
} request_type ;

void guestHttpSvr_fini  ( void );
int  guestHttpSvr_init  ( int port );
int  guestHttpSvr_setup ( request_type & request );
void guestHttpSvr_look  ( void );
