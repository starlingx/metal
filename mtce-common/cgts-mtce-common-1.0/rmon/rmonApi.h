#ifndef __INCLUDE_RMONAPI_H__
#define __INCLUDE_RMONAPI_H__
/*
 * Copyright (c) 2013, 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <iostream>
#include <string>

#include "rmonHttp.h"

 /**
  * @file
  * Wind River CGTS Platform 
  * 
  * rmon API Header
 
  */

#define RMON_PUT_VSWITCH_OPER_LABEL  "v1"
#define RMON_PUT_VSWITCH          "/engine/stats"



/* Poll request is a GET operation that looks like this ...
 *
 * http://localhost:9000/v1/engine/stats
 * The following defines are used to help construct that request 
 * 
 */


/** Initializes the module */
int rmonApi_init ( string ip, int port );

/** Frees the module's dynamically allocated resources */
void rmonApi_fini ( void );


/**remote logging service request handlers */

void rmonHdlr_remotelogging_handler ( struct evhttp_request *req, void *arg );
int rmonHdlr_remotelogging_query (resource_config_type * ptr);

/**ceilometer sample create request handlers */
void rmonHdlr_ceilometer_handler ( struct evhttp_request *req, void *arg );
#endif 
