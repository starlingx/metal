#ifndef __PMONALARM_H__
#define __PMONALARM_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River Titanium Cloud Process Monitor 'Alarm' Header
  */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list>

using namespace std;

#include "alarmUtil.h"   /* for .. alarmUtil_<severity>    */

/** Alarm Abstract Reference IDs */
typedef enum
{
    PMON_ALARM_ID__PMOND       = 0,
    PMON_ALARM_ID__LAST        = 1,
} pmon_alarm_id_enum ;

/* Keep track of queried alarms and severities */
typedef struct
{
    string            process  ;
    EFmAlarmSeverityT severity ;
} active_process_alarms_type   ;

/* Query FM for a list of Process Monitor (200.006) alarms */
int query_alarms (  list<active_process_alarms_type> & alarm_list, string hostname="" );

void alarmed_process_audit ( void );

void pmonAlarm_init ( void );

EFmAlarmSeverityT pmonAlarm_state ( string hostname, pmon_alarm_id_enum id );

string pmonAlarm_getId_str ( pmon_alarm_id_enum id );

/** Clear the specified process monitor alarm for specific host */
int  pmonAlarm_clear    ( string hostname, pmon_alarm_id_enum id, string process );

/** Assert a specified alarm or log against the specified host with a MINOR severity level */
int  pmonAlarm_minor    ( string hostname, pmon_alarm_id_enum id, string process, int restarts );
int  pmonAlarm_minor_log( string hostname, pmon_alarm_id_enum id, string process, int restarts );

/** Assert a specified alarm or log against the specified host with a MAJOR severity level */
int  pmonAlarm_major    ( string hostname, pmon_alarm_id_enum id, string process );
int  pmonAlarm_major_log( string hostname, pmon_alarm_id_enum id, string process );

/** Assert a specified alarm or log against the specified host with a CRITICAL severity level */
int  pmonAlarm_critical    ( string hostname, pmon_alarm_id_enum id, string process );
int  pmonAlarm_critical_log( string hostname, pmon_alarm_id_enum id, string process );

#endif /* __PMONALARM_H__ */
