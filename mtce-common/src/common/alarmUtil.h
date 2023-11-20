#ifndef __ALARMUTIL_H__
#define __ALARMUTIL_H__

/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Common Maintenance 'Alarm' Header
  */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//using namespace std;

/* external header APIs */
#include "nodeBase.h"
#include "fmAPI.h"          /* for fm_set_fault, fm_clear_fault, etc */

#define ENTITY_PREFIX        ((const char *)"host=")

#define MAX_ALARMS           (10)

#define SWERR_ALARM_ID       ((const char *)"200.000") /* Do No Use */
#define LOCK_ALARM_ID        ((const char *)"200.001")
#define ENABLE_ALARM_ID      ((const char *)"200.004")
#define MGMNT_HB_ALARM_ID    ((const char *)"200.005")
#define PMOND_ALARM_ID       ((const char *)"200.006")
#define SENSOR_ALARM_ID      ((const char *)"200.007") /* Sensor read alarm ; i.e. the sensor read value bad  */
#define CLSTR_HB_ALARM_ID    ((const char *)"200.009")
#define BM_ALARM_ID          ((const char *)"200.010")
#define CONFIG_ALARM_ID      ((const char *)"200.011")
#define CH_COMP_ALARM_ID     ((const char *)"200.013") /* Combo Host Compute Failure - on last Controller     */
#define SENSORCFG_ALARM_ID   ((const char *)"200.014") /* Sensor configuration alarm ; i.e. could not add     */
#define SENSORGROUP_ALARM_ID ((const char *)"200.015") /* Sensor Group Read Error                             */
#define LUKS_ALARM_ID        ((const char *)"200.016") /* LUKS volume failure alarm                           */

#define EVENT_LOG_ID         ((const char *)"200.020")
#define COMMAND_LOG_ID       ((const char *)"200.021")
#define STATECHANGE_LOG_ID   ((const char *)"200.022")
#define SERVICESTATUS_LOG_ID ((const char *)"200.023") /* log used to report service failure events against   */
#define CONFIG_LOG_ID        ((const char *)"200.024")

/**
 *  TODO: This class is more of a place holder for
 *        more centralized management of alarms
 *        It is useless in its present form.
 **/
class alarmUtilClass
{
    private:

    public:

        alarmUtilClass();
       ~alarmUtilClass();

        char temp_str       [MAX_API_LOG_LEN] ;
        char varlog_filename[MAX_FILENAME_LEN];
};

typedef struct
{
    SFmAlarmDataT alarm ;
    string         name ;
    string instc_prefix ; /* Instance prefix i.e. "=sensor." or "=process." */
    string critl_reason ;
    string minor_reason ;
    string major_reason ;
    string clear_reason ;
} alarmUtil_type ;

/** Converts FM severity to representative string */
string alarmUtil_getSev_str ( EFmAlarmSeverityT sev );
EFmAlarmSeverityT alarmUtil_getSev_enum ( string sev );

/* Clear all alarms against this host */
void alarmUtil_clear_all ( string hostname );

/**
 *  Query the specified alarm severity level.
 *  Severity levels are specified in fmAPI.h
 **/
EFmAlarmSeverityT alarmUtil_query ( string & hostname,
                                    string & identity,
                                    string & instance );

int alarmUtil_query_identity ( string          identity,
                               SFmAlarmDataT * alarm_list_ptr,
                               unsigned int    alarms_max );

int alarmUtil_clear ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/*************************   A L A R M I N G   **************************/

/**
 *  Assert a unique identity alarm or log against specified
 *  hostname/instance using the supplied alarm data
 **/
int  alarmUtil (string & hostname, string & identity, string & instance, SFmAlarmDataT & alarm);

/** Return a string that represents the specified severity enum */
string alarmUtil_getSev_str ( EFmAlarmSeverityT sev );

/** Assert a specified host's alarm with a CRITICAL severity level  */
int alarmUtil_critical ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/** Assert a specified host's alarm with a MAJOR severity level */
int alarmUtil_major ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/** Assert a specified host's alarm with a MINOR severity level */
int alarmUtil_minor ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/** Assert a specified host's mtce alarm with a WARNING severity level */
int alarmUtil_warning ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );


/***************************   L O G G I N G   **********************************/

/** Create a CRITICAL log */
int alarmUtil_critical_log ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/** Create a MAJOR log */
int alarmUtil_major_log    ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/** Create a MINOR log */
int alarmUtil_minor_log    ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/** Create a WARNING log */
int alarmUtil_warning_log  ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

/** Create a neutral customer log */
int alarmUtil_log          ( string hostname, string identity, string instance, SFmAlarmDataT & alarm );

#endif /* __ALARMUTIL_H__ */
