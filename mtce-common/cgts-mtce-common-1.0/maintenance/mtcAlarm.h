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
    MTC_ALARM_ID__LOCK              = 0,
    MTC_ALARM_ID__CONFIG            = 1,
    MTC_ALARM_ID__ENABLE            = 2,
    MTC_ALARM_ID__BM                = 3,
    MTC_ALARM_ID__CH_CONT           = 4, /* Combo Host Controller Failure - with Active Compute */
    MTC_ALARM_ID__CH_COMP           = 5, /* Combo Host Compute Failure - on last Controller     */

    MTC_LOG_ID__EVENT               = 6,
    MTC_LOG_ID__COMMAND             = 7,
    MTC_LOG_ID__STATECHANGE         = 8,
    MTC_ALARM_ID__LAST              = 9,

    MTC_LOG_ID__EVENT_ADD                = 10,
    MTC_LOG_ID__EVENT_RESTART            = 11,
    MTC_LOG_ID__EVENT_DISCOVERED         = 12,
    MTC_LOG_ID__EVENT_MNFA_ENTER         = 13,
    MTC_LOG_ID__EVENT_MNFA_EXIT          = 14,

    MTC_LOG_ID__COMMAND_DELETE           = 19,
    MTC_LOG_ID__COMMAND_UNLOCK           = 20,
    MTC_LOG_ID__COMMAND_FORCE_LOCK       = 21,
    MTC_LOG_ID__COMMAND_SWACT            = 22,
    MTC_LOG_ID__COMMAND_REINSTALL        = 23,
    MTC_LOG_ID__COMMAND_BM_PROVISIONED   = 24,
    MTC_LOG_ID__COMMAND_BM_DEPROVISIONED = 25,
    MTC_LOG_ID__COMMAND_BM_REPROVISIONED = 26,

    MTC_LOG_ID__COMMAND_AUTO_REBOOT      = 30,
    MTC_LOG_ID__COMMAND_MANUAL_REBOOT    = 31,
    MTC_LOG_ID__COMMAND_AUTO_RESET       = 32,
    MTC_LOG_ID__COMMAND_MANUAL_RESET     = 33,
    MTC_LOG_ID__COMMAND_AUTO_POWER_ON    = 34,
    MTC_LOG_ID__COMMAND_MANUAL_POWER_ON  = 35,
    MTC_LOG_ID__COMMAND_AUTO_POWER_OFF   = 36,
    MTC_LOG_ID__COMMAND_MANUAL_POWER_OFF = 37,

    
    MTC_LOG_ID__STATUSCHANGE_ENABLED     = 40,
    MTC_LOG_ID__STATUSCHANGE_DISABLED    = 41,
    MTC_LOG_ID__STATUSCHANGE_ONLINE      = 42,
    MTC_LOG_ID__STATUSCHANGE_OFFLINE     = 43,
    MTC_LOG_ID__STATUSCHANGE_FAILED      = 44,
    MTC_LOG_ID__STATUSCHANGE_REINSTALL_FAILED   = 45,
    MTC_LOG_ID__STATUSCHANGE_REINSTALL_COMPLETE = 46,

    MTC_ALARM_ID__END = 50

} mtc_alarm_id_enum ;

void mtcAlarm_init ( void );
void mtcAlarm_clear_all ( void );

EFmAlarmSeverityT mtcAlarm_state ( string hostname, mtc_alarm_id_enum id );

string mtcAlarm_getId_str ( mtc_alarm_id_enum id );

/** Clear the specified maintenance alarm for specific host */
int  mtcAlarm_clear    ( string hostname, mtc_alarm_id_enum id );

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
int  mtcAlarm_log          ( string hostname, mtc_alarm_id_enum id );

#endif /* __MTCALARM_H__ */
