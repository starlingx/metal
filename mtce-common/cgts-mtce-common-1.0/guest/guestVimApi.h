#ifndef __INCLUDE_GUESTVIMAPI_H__
#define __INCLUDE_GUESTVIMAPI_H__
/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <iostream>
#include <string>

#include "guestHttpUtil.h"

 /**
  * @file
  * Wind River CGTS Platform Guest Services Request Transmitter.
  *
  * This module is used by the guestAgent only and allows the guestAgent to 
  *
  *  1. Transmit notification of an instance failure to the VIM
  *
  *         guestVimApi_inst_failed
  *   
  *  2. Get the instrance info for a specified host from the VIM
  * 
  *         guestVimApi_getHostState
  *
  *  3. Get the host level fault reporting state.
  *
  *         guestVimApi_getHostInst
  *
  **************************************************************************/

int  guestVimApi_init        ( string ip, int port );
void guestVimApi_fini        ( void );

int  guestVimApi_inst_failed  ( string hostname, string instance, unsigned int event, int retries );
int  guestVimApi_inst_action  ( string hostname, string instance_uuid, string action, string guest_response, string reason, int retries=0 );
int  guestVimApi_svc_event    ( string hostname, string instance_uuid, string state, string status, string timeout );
int  guestVimApi_alarm_event  ( string hostname, string instance_uuid );
int  guestVimApi_getHostInst  ( string hostname, string uuid, libEvent & event );
int  guestVimApi_getHostState ( string hostname, string uuid, libEvent & event );

void guestVimApi_Handler     ( struct evhttp_request *req, void *arg );

#endif /* __INCLUDE_GUESTVIMAPI_H__ */
