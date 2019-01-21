#ifndef __INCLUDE_MTCSECRETUTIL_H__
#define __INCLUDE_MTCSECRETUTIL_H__

/*
 * Copyright (c) 2019 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform - Maintenance - Openstack Barbican UTIL Header
  */

/**
  * @addtogroup secretUtil
  * @{
  *
  * This file implements the a set of secretUtil utilities that maintenance
  * calls upon to get/read Barbican secrets from the Barbican Secret storage.
  *
  * The UTILs exposed from this file are
  *
  *   secretUtil_get_secret   - gets all the Barbican secrets, filtered by name
  *   secretUtil_read_secret  - reads the payload for a specified secret
  *
  *   See nodeClass.h for these prototypes
  *
  *   Each utility is paired with a private handler.
  *
  *   secretUtil_handler  - handles response for secretUtil_get/read_secret
  *
  * Warning: These calls cannot be nested.
  *
  **/

using namespace std;

#include "logMacros.h"
#include "httpUtil.h"

#define MTC_SECRET_LABEL           "/v1/secrets"    /**< barbican secrets url label        */
#define MTC_SECRET_NAME            "?name="         /**< name of barbican secret prefix    */
#define MTC_SECRET_BATCH           "&limit="        /**< batch read limit specified prefix */
#define MTC_SECRET_BATCH_MAX       "1"              /**< maximum allowed batched read      */
#define MTC_SECRET_PAYLOAD         "payload"        /**< barbican secret payload label     */

#define SECRET_START_DELAY (1)
#define SECRET_REPLY_DELAY (1)
#define SECRET_RETRY_DELAY (8)

barbicanSecret_type * secretUtil_find_secret ( string & host_uuid );
barbicanSecret_type * secretUtil_manage_secret ( libEvent & event,
                                                 string & host_uuid,
                                                 struct mtc_timer & secret_timer,
                                                 void (*handler)(int, siginfo_t*, void*) );

int secretUtil_handler     ( libEvent & event );
int secretUtil_get_secret  ( libEvent & event, string & host_uuid );
int secretUtil_read_secret ( libEvent & event, string & host_uuid );

#endif /* __INCLUDE_MTCSECRETUTIL_H__ */
