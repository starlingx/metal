#ifndef __INCLUDE_TOKENUTIL_H__
#define __INCLUDE_TOKENUTIL_H__
/*
 * Copyright (c) 2013, 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/*
 * This module contains a single static __token__ object,
 * an interface that updates/refreshes it with a valid token
 * an interface that queries keystone service list uuids
 * an interface tht queries the specified service admin 
 * endpoint using its service uuid.
 *
 *
 *  tokenUtil_get_svc_uuid - returns the service uuid for the
 *                           specified service.
 *  tokenUtil_get_endpoint - returns the admin endpoint for the 
 *                           specified service uuid.
 */

#include <iostream>
#include <string>

using namespace std;

#include "logMacros.h" 
#include "httpUtil.h"        /* for ... libEvent                   */

#define MTC_POST_KEY_LABEL "/v3/auth/tokens"

#define KEYSTONE_SIG       "token"

/* The invalidation window is 5 minutes according
 * to the testing of token expiration time */
#define STALE_TOKEN_DURATION 300 //5 minutes

/* returns the static token object for this module */
keyToken_type * tokenUtil_get_ptr      ( void );
keyToken_type   tokenUtil_get_token    ( void );

int             tokenUtil_handler      ( libEvent & event );
int             tokenUtil_new_token    ( libEvent & event, string hostname, bool blocking=true );
void            tokenUtil_get_first    ( libEvent & event, string & hostname   );
int             tokenUtil_token_refresh( libEvent & event, string hostname     );
int             tokenUtil_get_endpoints( libEvent & event, string service_uuid );
string          tokenUtil_get_svc_uuid ( libEvent & event, string service_name );

void            tokenUtil_fail_token   ( void );
void            tokenUtil_log_refresh  ( void );

int keystone_config_handler ( void * user,
                        const char * section,
                        const char * name,
                        const char * value);

void tokenUtil_manage_token ( libEvent         & event,
                              string           & hostname,
                              int              & refresh_rate,
                              struct mtc_timer & token_refresh_timer,
                              void (*handler)(int, siginfo_t*, void*));

#endif /* __INCLUDE_TOKENUTIL_H__ */
