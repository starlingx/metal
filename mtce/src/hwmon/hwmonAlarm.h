#ifndef __HWMONALARM_H__
#define __HWMONALARM_H__

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Maintenance 'Alarm' Header
  */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list>

using namespace std;

#include "alarmUtil.h"
#include "hwmon.h"
// #include "fmAPI.h"

typedef enum
{
    HWMON_ALARM_ID__SENSOR      = 0,
    HWMON_ALARM_ID__SENSORCFG   = 1,
    HWMON_ALARM_ID__SENSORGROUP = 2,
    HWMON_ALARM_ID__LAST        = 3
} hwmonAlarm_id_type ;

typedef struct
{
    string            entity     ;
    string            instance   ;
    EFmAlarmSeverityT severity   ;
} hwmonAlarm_entity_status_type  ;


#define REASON_SUPPRESSED        ((const char *)"is 'suppressed'")
#define REASON_UNSUPPRESSED      ((const char *)"is 'unsuppressed'")
#define REASON_OK                ((const char *)"is 'ok'")
#define REASON_DEGRADED          ((const char *)"is 'degraded'")
#define REASON_DEPROVISIONED     ((const char *)"is 'deprovisioned'")
#define REASON_IGNORED           ((const char *)"is 'ignored'")
#define REASON_OOT               ((const char *)"is 'out-of-tolerance'")
#define REASON_SET_TO_ALARM      ((const char *)"is 'set to alarm'")
#define REASON_SET_TO_LOG        ((const char *)"is 'set to log'")
#define REASON_SET_TO_POWERCYCLE ((const char *)"is 'set to powercycle'")
#define REASON_SET_TO_RESET      ((const char *)"is 'set to reset'")
#define REASON_SET_TO_IGNORE     ((const char *)"is 'set to ignore'")
#define REASON_RESETTING         ((const char *)"is 'resetting'")
#define REASON_POWERCYCLING      ((const char *)"is 'power-cycling'")
#define REASON_OFFLINE           ((const char *)"is 'offline'")
#define REASON_ONLINE            ((const char *)"is 'online'")

int hwmon_alarm_util ( string           & hostname,
                       hwmonAlarm_id_type id,
                       EFmAlarmStateT     state,
                       EFmAlarmSeverityT  severity,
                       string           & sub_entity ,
                       string             reason );

/* Clear the specified maintenance alarm for specific host */
int hwmonAlarm_clear    ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonAlarm_critical ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonAlarm_major    ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonAlarm_minor    ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonAlarm_warning  ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );

int hwmonLog_clear      ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonLog_critical   ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonLog_major      ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonLog_minor      ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );
int hwmonLog_warning    ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason );

/* generate a customer log for the specified severity */
int hwmonLog            ( string & hostname, hwmonAlarm_id_type id, EFmAlarmSeverityT sev, string sub_entity, string reason );

/* Utility will init the alarm and return the severity if it is currently asserted */
EFmAlarmSeverityT hwmon_alarm_query ( string & hostname, hwmonAlarm_id_type id, string & sensorname );

int               hwmonAlarm_query_entity ( string & hostname, string & entity, list<hwmonAlarm_entity_status_type> & alarm_list );


#endif /* __HWMONALARM_H__ */
