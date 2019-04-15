#ifndef __HBSALARM_H__
#define __HBSALARM_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud 'Heartbeat Agent' Alarm Header
  */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

/* Consider removing this */
#include "alarm.h"   /* for .. mtcAlarm    */

#define MGMNT_NAME ((const char *)"Management")
#define CLSTR_NAME ((const char *)"Cluster-host")
#define PMON_NAME ((char *)"pmond")

void hbsAlarm_clear_all ( string hostname, bool clstr );

#endif /* __HBSALARM_H__ */
