/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
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
} event_type ;

event_type * get_eventPtr ( void );

void mtcHttpSvr_fini  ( event_type & event );
int  mtcHttpSvr_init  ( event_type & event );
int  mtcHttpSvr_setup ( event_type & event );
void mtcHttpSvr_look  ( event_type & event );
void mtcHttpSvr_work  ( event_type & event );
