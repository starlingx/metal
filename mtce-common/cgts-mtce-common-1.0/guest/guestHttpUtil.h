#ifndef __INCLUDE_GUESTHTTPUTIL_H__
#define __INCLUDE_GUESTHTTPUTIL_H__
/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
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

#include "guestClass.h"     /* for ... maintenance class nodeLinkClass */
#include "httpUtil.h"       /* for ... common http utilities           */

/***********************************************************************/

void guestHttpUtil_init ( void );
void guestHttpUtil_fini ( void );
int  guestHttpUtil_status ( libEvent & event );
int  guestHttpUtil_api_req ( libEvent & event );

#endif /* __INCLUDE_GUESTHTTPUTIL_H__ */
