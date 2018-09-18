#ifndef __INCLUDE_HWMONSENSOR_H__
#define __INCLUDE_HWMONSENSOR_H__

/** ************************************************************************
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 * ************************************************************************/
 
 /** **********************************************************************
  * @file
  * Wind River Titanium Cloud's Hardware Monitor Sensor Manipulation
  * and Access Methods Header
  *
  * This file contains the private API for the sensor access methods
  * used to display, read and configure sensors and groups
  * on a specific host.
  *
  * ***********************************************************************/

#include <sstream>
#include <iostream>
#include <unistd.h>       /* for ... access                           */
#include <stdlib.h>       /* for ... system                           */
#include <stdio.h>        /* for ... snprintf                         */
#include <string.h>       /* for ... string                           */

using namespace std;

#include "hwmon.h"        /* for ... service module header            */

#define DISCRETE ((const char *)("discrete"))
#define ANALOG ((const char *)("analog"))

#define MAX_SENSOR_TYPE_ERRORS (5)

void hwmonSensor_print ( string & hostname, sensor_type * sensor_ptr );
void hwmonSensor_init  ( string & hostname, sensor_type * sensor_ptr );

void hwmonGroup_print  ( string & hostname, struct sensor_group_type * group_ptr );
void hwmonGroup_init   ( string & hostname, struct sensor_group_type * group_ptr );

void handle_new_suppression ( sensor_type * sensor_ptr ) ;


#endif /* __INCLUDE_HWMONSENSOR_H__ */
