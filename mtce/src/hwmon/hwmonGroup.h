#ifndef __INCLUDE_HWMONGROUP_H__
#define __INCLUDE_HWMONGROUP_H__
/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud's Hardware Monitor "Sensor Grouping" Header
  */

#define MAX_GROUPING_ERRORS (1)

#include "hwmon.h"

#define HWMON_GROUP_NAME__NULL       "null"
#define HWMON_GROUP_NAME__FANS       "server fans"
#define HWMON_GROUP_NAME__TEMP       "server temperature"
#define HWMON_GROUP_NAME__VOLTS      "server voltage"
#define HWMON_GROUP_NAME__POWER      "server power"
#define HWMON_GROUP_NAME__USAGE      "server usage"
#define HWMON_GROUP_NAME__POWER_FANS "power supply fans"
#define HWMON_GROUP_NAME__MEMORY     "server memory"
#define HWMON_GROUP_NAME__CLOCKS     "server clocks"
#define HWMON_GROUP_NAME__ERRORS     "server errors"
#define HWMON_GROUP_NAME__MESSAGES   "server messages"
#define HWMON_GROUP_NAME__TIME       "server time"
#define HWMON_GROUP_NAME__MISC       "miscellaneous"

void groupSensors_print ( sensor_group_type * group_ptr );

string            bmc_get_groupname ( canned_group_enum group_enum );

string            bmc_get_grouptype ( string & hostname,
                                       string & unittype,
                                       string & sensorname);

canned_group_enum bmc_get_groupenum ( string & hostname,
                                       string & unittype,
                                       string & sensorname );

#endif
