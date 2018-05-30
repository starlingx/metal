#ifndef __INCLUDE_MTCSMHAAPI_H__
#define __INCLUDE_MTCSMHAAPI_H__
/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <iostream>
#include <string>

// #include "mtcHttpUtil.h"
#include "httpUtil.h"

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance
  * 
  * Service Manager HA API Header
  *
  * This module offers init and cleanup utils along with
  * a single request based utility
  *
  * int mtcSmhaApi_request ( hostname, operation, retries )
  *
  * Operations:
  *
  *  HA Service Manager  - Command Descriptions
  *  ------------------    ------------------------------
  *  CONTROLLER_LOCKED   - specified controller is locked
  *  CONTROLLER_UNLOCKED - specified controller is unlocked
  *  CONTROLLER_DISABLED - specified controller is unlocked-disabled
  *  CONTROLLER_ENABLED  - specified controller is unlocked-enabled
  *  CONTROLLER_SWACT    - swact services away from specified controller
  *  CONTROLLER_QUERY    - query active services on specified controller
  *
  */

#define MTC_SMGR_LABEL   "/v1/servicenode/"
#define MTC_SMGR_ADDR    "localhost"

/** Initializes the module */
int mtcSmgrApi_init ( string ip, int port );

/** Frees the module's dynamically allocated resources */
void mtcSmgrApi_fini ( void );

int mtcSmgrApi_service_state ( libEvent & event, bool & active_services );

#endif /* __INCLUDE_MTCSMHAAPI_H__ */
