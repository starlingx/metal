#ifndef __INCLUDE_MTCKEYAPI_H__
#define __INCLUDE_MTCKEYAPI_H__
/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <iostream>
#include <string>

#include "mtcHttpUtil.h"

//#define MTC_POST_KEY_ADDR  "localhost"
//#define MTC_POST_KEY_PORT  5000
#define MTC_POST_KEY_LABEL "/v3/auth/tokens"

int  mtcKeyApi_init ( string ip, int port );

int mtcKeyApi_handler ( libEvent & event );

void corrupt_token ( keyToken_type & key );

#endif /* __INCLUDE_MTCKEYAPI_H__ */
