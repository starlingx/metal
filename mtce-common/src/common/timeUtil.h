#ifndef __INCLUDE_TIMEUTIL_H__
#define __INCLUDE_TIMEUTIL_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Time Utility Header
  */

#include <iostream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

using namespace std;

#include "daemon_common.h" /* */

void timeUtil_sched_init   ( void );
void timeUtil_sched_sample ( void );

#endif
