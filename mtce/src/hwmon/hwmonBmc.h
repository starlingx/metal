#ifndef __INCLUDE_HWMONBMC_H__
#define __INCLUDE_HWMONBMC_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * StarlingX Cloud's Hardware Monitor "BMC Sensor" Header
  */

#include "hwmon.h"         /* for ... sensor_data_type                      */
#include "hwmonClass.h"    /* for ... hwmonHostClass                        */

#define QUANTA_SENSOR_PROFILE_CHECKSUM             (0xb35b) /* pre 13.58 loaded from database */
#define QUANTA_SENSOR_PROFILE_CHECKSUM_13_53       (0x5868) /* 13.53 loaded from database     */

/*
 * There is no real difference between the 13.50 and 13.53.
 * 13.50 is considered having the Temp_HBA_LSI sensor while
 * 13.53 doesn't
 */
#define QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_53   (0x76b9) /* no LSI sensor */
#define QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_53b  (0xfb12) /* with LSI sensor */
#define QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_50   (0x81a3)
#define QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_47   (0xd92a)
#define QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13___   (0x5868)
#define QUANTA_SAMPLE_PROFILE_CHECKSUM_VER_13_42   (0xf6e4)
#define QUANTA_SAMPLE_PROFILE_CHECKSUM_VER__3_29   (0x4d31)

#define QUANTA_SAMPLE_PROFILE_SENSORS_VER_13_53       (54) /* no LSI sensor */
#define QUANTA_SAMPLE_PROFILE_SENSORS_VER_13_50       (55)
#define QUANTA_SAMPLE_PROFILE_SENSORS_VER_13_47       (57)
#define QUANTA_SAMPLE_PROFILE_SENSORS_VER_13_42       (57)
#define QUANTA_SAMPLE_PROFILE_SENSORS_VER__3_29       (58)

#define MAX_IPMITOOL_PARSE_ERRORS                     (20)

void sensor_data_init  (       sensor_data_type & data );
void sensor_data_print ( string & hostname, const sensor_data_type & data );
void sensor_data_copy  ( sensor_data_type & from, sensor_data_type & to );

int bmc_load_json_sensor ( string & hostname, sensor_data_type & sensor_data , string json_sensor_data );

#endif
