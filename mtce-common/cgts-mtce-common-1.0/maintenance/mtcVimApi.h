#ifndef __INCLUDE_MTCVIMAPI_H__
#define __INCLUDE_MTCVIMAPI_H__
/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <iostream>
#include <string>

#include "mtcHttpUtil.h"

 /**
  * @file
  * Wind River CGTS Platform Maintenance VIM Support
  * 
  * Virtual Infrastructure Manager (VIM) API Header
  *
  * This module offers init and cleanup utils along with
  * a single request based utility to inform VIM of Host Failures and when
  * mtce transitions a host into the Enabled or Disabled State due to Sysinv
  * Lock and Unlock requests that complete.
  *
  * int mtcVimApi_state_change ( node_ptr, operation, retries )
  *
  * Operations:
  *
  *  VIM Commands  - Descriptions
  *  -------------   ------------------------------
  *  HOST_DISABLE  - Inform VIM that this host is now Mtce-Disabled
  *  HOST_ENABLE   - Inform VIM that this host is now Mtce-Enabled
  *  HOST_FAIL     - Inform VIM that this host has failed and 
  *                  undergoing auto recovery
  *
  */

#define MTC_VIM_LABEL   "/nfvi-plugins/v1/hosts/"

/** Initializes the module */
int mtcVimApi_init ( string ip, int port );

/** Frees the module's dynamically allocated resources */
void mtcVimApi_fini ( void );

int mtcVimApi_handler ( libEvent & event );

#endif /* __INCLUDE_MTCVIMAPI_H__ */
