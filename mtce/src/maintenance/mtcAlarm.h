#ifndef __MTCALARM_H__
#define __MTCALARM_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River Titanium Cloud 'Maintenance Agent' Alarm Header
  */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

#include "alarmUtil.h"   /* for .. alarmUtil_<severity>    */

/** Maintenance Alarm Abstract Reference IDs */
typedef enum
{
    MTC_ALARM_ID__LOCK,
    MTC_ALARM_ID__CONFIG,
    MTC_ALARM_ID__ENABLE,
    MTC_ALARM_ID__BM,
    MTC_ALARM_ID__CH_CONT, /* Combo Host Controller Failure - with Active Compute */
    MTC_ALARM_ID__CH_COMP, /* Combo Host Compute Failure - on last Controller     */

    MTC_LOG_ID__EVENT,
    MTC_LOG_ID__COMMAND,
    MTC_LOG_ID__CONFIG,
    MTC_LOG_ID__STATECHANGE,
    MTC_LOG_ID__SERVICESTATUS,
    MTC_ALARM_ID__LAST,

    MTC_LOG_ID__EVENT_ADD,
    MTC_LOG_ID__EVENT_RESTART,
    MTC_LOG_ID__EVENT_DISCOVERED,
    MTC_LOG_ID__EVENT_MNFA_ENTER,
    MTC_LOG_ID__EVENT_MNFA_EXIT,

    MTC_LOG_ID__COMMAND_DELETE,
    MTC_LOG_ID__COMMAND_UNLOCK,
    MTC_LOG_ID__COMMAND_FORCE_LOCK,
    MTC_LOG_ID__COMMAND_SWACT,
    MTC_LOG_ID__COMMAND_REINSTALL,
    MTC_LOG_ID__COMMAND_BM_PROVISIONED,
    MTC_LOG_ID__COMMAND_BM_DEPROVISIONED,
    MTC_LOG_ID__COMMAND_BM_REPROVISIONED,

    MTC_LOG_ID__CONFIG_HB_ACTION_FAIL,
    MTC_LOG_ID__CONFIG_HB_ACTION_DEGRADE,
    MTC_LOG_ID__CONFIG_HB_ACTION_ALARM,
    MTC_LOG_ID__CONFIG_HB_ACTION_NONE,
    MTC_LOG_ID__CONFIG_HB_PERIOD,
    MTC_LOG_ID__CONFIG_HB_DEGRADE_THRESHOLD,
    MTC_LOG_ID__CONFIG_HB_FAILURE_THRESHOLD,
    MTC_LOG_ID__CONFIG_MNFA_TIMEOUT,
    MTC_LOG_ID__CONFIG_MNFA_THRESHOLD,

    MTC_LOG_ID__COMMAND_AUTO_REBOOT,
    MTC_LOG_ID__COMMAND_MANUAL_REBOOT,
    MTC_LOG_ID__COMMAND_AUTO_RESET,
    MTC_LOG_ID__COMMAND_MANUAL_RESET,
    MTC_LOG_ID__COMMAND_AUTO_POWER_ON,
    MTC_LOG_ID__COMMAND_MANUAL_POWER_ON,
    MTC_LOG_ID__COMMAND_AUTO_POWER_OFF,
    MTC_LOG_ID__COMMAND_MANUAL_POWER_OFF,

    MTC_LOG_ID__STATUSCHANGE_ENABLED,
    MTC_LOG_ID__STATUSCHANGE_DISABLED,
    MTC_LOG_ID__STATUSCHANGE_ONLINE,
    MTC_LOG_ID__STATUSCHANGE_OFFLINE,
    MTC_LOG_ID__STATUSCHANGE_FAILED,
    MTC_LOG_ID__STATUSCHANGE_REINSTALL_FAILED,
    MTC_LOG_ID__STATUSCHANGE_REINSTALL_COMPLETE,

    MTC_ALARM_ID__END

} mtc_alarm_id_enum ;

void mtcAlarm_init ( void );
void mtcAlarm_clear_all ( void );

EFmAlarmSeverityT mtcAlarm_state ( string hostname, mtc_alarm_id_enum id );

string mtcAlarm_getId_str ( mtc_alarm_id_enum id );

/** Clear the specified maintenance alarm for specific host */
int  mtcAlarm_clear    ( string hostname, mtc_alarm_id_enum id );

/** Raise specified severity level alarm for the specified host */
int mtcAlarm_raise ( string hostname, mtc_alarm_id_enum id, EFmAlarmSeverityT severity );

/** Assert a specified mtce alarm against the specified host with a WARNING severity level */
int  mtcAlarm_warning  ( string hostname, mtc_alarm_id_enum id );

/** Assert a specified mtce alarm against the specified host with a MINOR severity level */
int  mtcAlarm_minor    ( string hostname, mtc_alarm_id_enum id );

/** Assert a specified mtce alarm against the specified host with a MAJOR severity level */
int  mtcAlarm_major    ( string hostname, mtc_alarm_id_enum id );

/** Assert a specified mtce alarm against the specified host with a CRITICAL severity level */
int  mtcAlarm_critical ( string hostname, mtc_alarm_id_enum id );


int  mtcAlarm_critical_log ( string hostname, mtc_alarm_id_enum id );

/** Create a MAJOR maintenance log */
int  mtcAlarm_major_log    ( string hostname, mtc_alarm_id_enum id );

/** Create a MINOR maintenance log */
int  mtcAlarm_minor_log    ( string hostname, mtc_alarm_id_enum id );

/** Create a WARNING maintenance log */
int  mtcAlarm_warning_log  ( string hostname, mtc_alarm_id_enum id );

/** Create a maintenance log */
int  mtcAlarm_log  ( string hostname, mtc_alarm_id_enum id, string str = "");

#endif /* __MTCALARM_H__ */
