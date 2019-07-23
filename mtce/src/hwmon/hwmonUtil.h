#ifndef __INCLUDE_HWMONUTIL_H__
#define __INCLUDE_HWMONUTIL_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud's Hardware Monitor "Utility" Header
  */

#include <string.h>
#include <stdio.h>
#include <fmAPI.h>         /* for ... EFmAlarmSeverityT                     */

using namespace std;

#include "hwmon.h"         /* for ... sensor_severity_enum                  */

string get_key_value_string ( string reading, string key, char delimiter, bool set_tolowercase );

bool   clear_severity_alarm ( string & hostname, hwmonAlarm_id_type id, string & sub_entity, EFmAlarmSeverityT severity, string reason );
void   clear_asserted_alarm ( string & hostname, hwmonAlarm_id_type id, sensor_type * ptr,                               string reason );

string               get_severity ( sensor_severity_enum severity );
sensor_severity_enum get_severity ( string status );
string               get_bmc_severity ( sensor_severity_enum status );
sensor_severity_enum get_bmc_severity ( string status );

bool   is_valid_action      ( sensor_severity_enum severity, string & action, bool set_to_lower );
bool   is_ignore_action     ( string action ) ;
bool   is_log_action        ( string action ) ;
bool   is_alarm_action      ( string action ) ;
bool   is_reset_action      ( string action ) ;
bool   is_powercycle_action ( string action ) ;

bool   is_alarmed_state     ( string action , sensor_severity_enum & hwmon_sev ) ;
bool   is_alarmed           ( sensor_type * sensor_ptr );

void   clear_logged_state   ( sensor_type * sensor_ptr );
void   clear_ignored_state  ( sensor_type * sensor_ptr );
void   clear_alarmed_state  ( sensor_type * sensor_ptr );
void   clear_degraded_state ( sensor_type * sensor_ptr );
void   set_degraded_state   ( sensor_type * sensor_ptr );



void   set_alarmed_severity ( sensor_type * sensor_ptr , EFmAlarmSeverityT severity );

void set_logged_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity );
void set_ignored_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity );

void clear_logged_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity );
void clear_ignored_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity );

string print_alarmed_severity ( sensor_type * sensor_ptr );
string print_ignored_severity ( sensor_type * sensor_ptr );
string print_logged_severity  ( sensor_type * sensor_ptr );

unsigned short checksum_sensor_profile ( const string           & hostname,
                                               int                sensors,
                                               sensor_type      * sensor_ptr);

unsigned short checksum_sample_profile ( const string           & hostname,
                                               int                sensors,
                                               sensor_data_type * sensor_ptr);

bool got_delimited_value ( char * buf_ptr,
                           const char * key,
                           const char * delimiter,
                           string & value );

string get_bmc_version_string ( string hostname,
                                const char * filename );

#endif
