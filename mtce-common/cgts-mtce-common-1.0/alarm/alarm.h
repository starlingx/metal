#ifndef __INCLUDE_ALARM_H__
#define __INCLUDE_ALARM_H__

/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Alarm Service Header
  */

#include "nodeBase.h"
#include "nodeUtil.h"      /* for ... common utilities                     */

#include "msgClass.h"     /* for ... msgClassSock type definition */

/* external APIs */
#include "fmAPI.h"

#define ENTITY_PREFIX        ((const char *)"host=")

#define MAX_ALARMS           (10)
#define MAX_ALARM_REQ_PER_MSG                (4)
#define MAX_ALARM_REQ_MSG_SIZE               (500)
#define MAX_ALARM_REQ_SIZE                   (MAX_ALARM_REQ_PER_MSG*MAX_ALARM_REQ_MSG_SIZE)

#define SWERR_ALARM_ID       ((const char *)"200.000") /* Do No Use */
#define LOCK_ALARM_ID        ((const char *)"200.001")
#define ENABLE_ALARM_ID      ((const char *)"200.004")
#define MGMNT_HB_ALARM_ID    ((const char *)"200.005")
#define PMOND_ALARM_ID       ((const char *)"200.006")
#define SENSOR_ALARM_ID      ((const char *)"200.007") /* Sensor read alarm ; i.e. the sensor read value bad  */
#define INFRA_HB_ALARM_ID    ((const char *)"200.009")
#define BM_ALARM_ID          ((const char *)"200.010")
#define CONFIG_ALARM_ID      ((const char *)"200.011")
#define CH_CONT_ALARM_ID     ((const char *)"200.012") /* Combo Host Controller Failure - with Active Compute */
#define CH_COMP_ALARM_ID     ((const char *)"200.013") /* Combo Host Compute Failure - on last Controller     */
#define SENSORCFG_ALARM_ID   ((const char *)"200.014") /* Sensor configuration alarm ; i.e. could not add     */
#define SENSORGROUP_ALARM_ID ((const char *)"200.015") /* Sensor Group Read Error                             */

#define EVENT_LOG_ID         ((const char *)"200.020")
#define COMMAND_LOG_ID       ((const char *)"200.021")
#define STATECHANGE_LOG_ID   ((const char *)"200.022")
#define SERVICESTATUS_LOG_ID ((const char *)"200.023") /* log used to report service failure events against   */


/** Heartbeat Alarm Abstract Reference IDs */
typedef enum
{
    HBS_ALARM_ID__HB_MGMNT    = 0,
    HBS_ALARM_ID__HB_INFRA    = 1,
    HBS_ALARM_ID__PMOND       = 2,
    HBS_ALARM_ID__SERVICE     = 3,
    HBS_ALARM_ID__LAST        = 4,
} alarm_id_enum ;

string            alarmUtil_getId_str     ( alarm_id_enum alarm_id_num );
int               alarmUtil_getId_enum    ( string alarm_id_str, alarm_id_enum & alarm_id_num );

/** Converts FM severity to representative string */
string            alarmUtil_getSev_str    ( EFmAlarmSeverityT severity );
EFmAlarmSeverityT alarmUtil_getSev_enum   ( string            severity );

#ifndef __MODULE_PRIVATE__

int alarm_register_user ( msgClassSock * sock_ptr );

/* Public API */
int  alarm_ ( string hostname, const char * id, EFmAlarmStateT state, EFmAlarmSeverityT severity, const char * entity, string prefix );
int  alarm_clear        ( string hostname, const char * id_ptr, string entity );
int  alarm_warning      ( string hostname, const char * id_ptr, string entity );
int  alarm_minor        ( string hostname, const char * id_ptr, string entity );
int  alarm_major        ( string hostname, const char * id_ptr, string entity );
int  alarm_critical     ( string hostname, const char * id_ptr, string entity );
int  alarm_critical_log ( string hostname, const char * id_ptr, string entity );
int  alarm_major_log    ( string hostname, const char * id_ptr, string entity );
int  alarm_minor_log    ( string hostname, const char * id_ptr, string entity );
int  alarm_warning_log  ( string hostname, const char * id_ptr, string entity, string prefix );
int  alarm_log          ( string hostname, const char * id_ptr, string entity );

#else


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


#define MAX_FAILED_B2B_RECEIVES_B4_RESTART   (5)


/* Test Commandss
 *
STR="{\"mtcalarm\":[{\"alarmid\":\"200.009\",\"hostname\":\"compute-3\",\"operation\":\"clear\",\"severity\":\"clear\",\"entity\":\"Infrastructure\",\"prefix\":\"service=heartbeat\"}, {\"alarmid\":\"200.005\",\"hostname\":\"compute-3\",\"operation\":\"set\",\"severity\":\"major\",\"entity\":\"Management\",\"prefix\":\"service=heartbeat\"}]}"
PROTOCOL="UDP4-DATAGRAM"
ADDRESS="127.0.0.1"
port="2122"
echo "${STR}" | socat - ${PROTOCOL}:${ADDRESS}:${port}
*/

#define MTCALARM_REQ_LABEL  ((const char *)"mtcalarm")

#define MTCALARM_REQ_KEY__OPERATION ((const char *)"operation")
#define MTCALARM_REQ_KEY__HOSTNAME  ((const char *)"hostname")
#define MTCALARM_REQ_KEY__ALARMID   ((const char *)"alarmid")
#define MTCALARM_REQ_KEY__SEVERITY  ((const char *)"severity")
#define MTCALARM_REQ_KEY__ENTITY    ((const char *)"entity")
#define MTCALARM_REQ_KEY__PREFIX    ((const char *)"prefix")

/* in alarmData.cpp */
void             alarmData_init ( void );
alarmUtil_type * alarmData_getAlarm_ptr ( string alarm_id_str );

/* in alarmUtil.cpp */
// EFmAlarmSeverityT mtcAlarm_state ( string hostname, alarm_id_enum id );


/* in alarmHdlr.cpp */
int alarmHdlr_request_handler ( char * msg_ptr );

/* in alarmMgr.cpp */
int alarmMgr_manage_alarm ( string alarmid ,
                            string hostname,
                            string operation,
                            string severity,
                            string entity,
                            string prefix);

/* Clear all alarms against this host */
void alarmUtil_clear_all ( string hostname );

/**
 *  Query the specified alarm severity level.
 *  Severity levels are specified in fmAPI.h
 **/
EFmAlarmSeverityT alarmUtil_query ( string hostname,
                                    string identity,
                                    string instance );

int alarmUtil_query_identity ( string          identity,
                               SFmAlarmDataT * alarm_list_ptr,
                               unsigned int    alarms_max );

int alarmUtil_clear        ( string hostname, string alarm_id, string entity );
int alarmUtil_critical     ( string hostname, string alarm_id, string entity );
int alarmUtil_major        ( string hostname, string alarm_id, string entity );
int alarmUtil_minor        ( string hostname, string alarm_id, string entity );
int alarmUtil_warning      ( string hostname, string alarm_id, string entity );
int alarmUtil_critical_log ( string hostname, string alarm_id, string entity );
int alarmUtil_major_log    ( string hostname, string alarm_id, string entity );
int alarmUtil_minor_log    ( string hostname, string alarm_id, string entity );
int alarmUtil_warning_log  ( string hostname, string alarm_id, string entity, string prefix );

#endif // _MODULE_PRIVATE_
#endif // __INCLUDE_ALARM_H__
