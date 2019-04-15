/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Alarm Daemon Utility
  **/

#include <sys/types.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "alm"

#define __MODULE_PRIVATE__

#include "daemon_common.h" /*                                           */
#include "alarm.h"         /* for ... this module header                */

/* TODO: Replace this with YAML Parsing */
static alarmUtil_type alarm_list[HBS_ALARM_ID__LAST] ;

alarmUtil_type * alarmData_getAlarm_ptr ( string alarm_id_str )
{
   alarm_id_enum id = HBS_ALARM_ID__LAST ;
   if ( alarmUtil_getId_enum ( alarm_id_str, id ) == PASS )
   {
       if ( id < HBS_ALARM_ID__LAST )
       {
           return (&alarm_list[id]) ;
       }
   }
   wlog ("failed to find alarm data for '%s'\n", alarm_id_str.c_str() );
   return (NULL);
}

typedef struct
{
    const char *  identity_str ;
    alarm_id_enum identity_num ;
} alarm_id_table_type ;

alarm_id_table_type alarm_id_table[HBS_ALARM_ID__LAST];


void alarmData_init ( void )
{
    alarmUtil_type * ptr ;

    alarm_id_table[HBS_ALARM_ID__HB_MGMNT].identity_str = MGMNT_HB_ALARM_ID ;
    alarm_id_table[HBS_ALARM_ID__HB_MGMNT].identity_num = HBS_ALARM_ID__HB_MGMNT ;
    alarm_id_table[HBS_ALARM_ID__HB_CLSTR].identity_str = CLSTR_HB_ALARM_ID;
    alarm_id_table[HBS_ALARM_ID__HB_CLSTR].identity_num = HBS_ALARM_ID__HB_CLSTR;
    alarm_id_table[HBS_ALARM_ID__PMOND].identity_str = PMOND_ALARM_ID;
    alarm_id_table[HBS_ALARM_ID__PMOND].identity_num = HBS_ALARM_ID__PMOND;
    alarm_id_table[HBS_ALARM_ID__SERVICE].identity_str = SERVICESTATUS_LOG_ID;
    alarm_id_table[HBS_ALARM_ID__SERVICE].identity_num = HBS_ALARM_ID__SERVICE;

    /** Management Network Heartbeat Alarm ************************************/

    ptr = &alarm_list[HBS_ALARM_ID__HB_MGMNT];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", MGMNT_HB_ALARM_ID);

    ptr->name = "Management Network' Heartbeat" ;
    ptr->instc_prefix = "network=" ;

    ptr->critl_reason = "experienced a persistent critical 'Management Network' "
                        "communication failure.";
    ptr->major_reason =
    ptr->minor_reason = "is experiencing intermittent 'Management Network' "
                        "communication failures that have exceeded its lower alarming threshold.";

    ptr->clear_reason = "'Management Network' Heartbeat has 'resumed' if host is 'unlocked' "
                        "or 'stopped' if host is 'locked or deleted'";

    ptr->alarm.alarm_type        = FM_ALARM_COMM ;
    ptr->alarm.probable_cause    = FM_ALARM_LOSS_OF_SIGNAL ;
    ptr->alarm.inhibit_alarms    = FM_FALSE;
    ptr->alarm.service_affecting = FM_TRUE ;
    ptr->alarm.suppression       = FM_TRUE ;

    ptr->alarm.severity          = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state       = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf( ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "Check 'Management Network' connectivity and support for multicast messaging."
              "If problem consistently occurs after that and Host is reset, then"
              "contact next level of support or lock and replace failing Host.");

    /** Cluster-host Network Heartbeat Alarm ************************************/

    ptr = &alarm_list[HBS_ALARM_ID__HB_CLSTR];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", CLSTR_HB_ALARM_ID);

    ptr->name = "Cluster-host Network' Heartbeat" ;
    ptr->instc_prefix = "network=" ;

    ptr->critl_reason = "experienced a persistent critical 'Cluster-host Network' "
                        "communication failure.";

    ptr->major_reason =
    ptr->minor_reason = "is experiencing intermittent 'Cluster-host Network' "
                        "communication failures that have exceeded its lower alarming threshold.";

    ptr->clear_reason = "'Cluster-host Network' Heartbeat has 'resumed' if host is 'unlocked' "
                        "or 'stopped' if host is 'locked or deleted'";

    ptr->alarm.alarm_type        = FM_ALARM_COMM ;
    ptr->alarm.probable_cause    = FM_ALARM_LOSS_OF_SIGNAL ;
    ptr->alarm.inhibit_alarms    = FM_FALSE;
    ptr->alarm.service_affecting = FM_TRUE ;
    ptr->alarm.suppression       = FM_TRUE ;

    ptr->alarm.severity          = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state       = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf( ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "Check 'Cluster-host Network' connectivity and support for multicast messaging."
              "If problem consistently occurs after that and Host is reset, then"
              "contact next level of support or lock and replace failing Host.");

    /** Process Failure Alarm ****************************************************/

    ptr = &alarm_list[HBS_ALARM_ID__PMOND];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", PMOND_ALARM_ID);

    ptr->name = "Process Monitor Failure" ;
    ptr->instc_prefix = "process=" ;

    ptr->critl_reason =
    ptr->minor_reason =
    ptr->major_reason = "'Process Monitor' (pmond) process is not running or functioning properly. "
                        "The system is trying to recover this process." ;
    ptr->clear_reason = "Process Monitor has been successfully recovered and is functioning properly.";

    ptr->alarm.alarm_type        = FM_ALARM_OPERATIONAL  ;
    ptr->alarm.probable_cause    = FM_ALARM_CAUSE_UNKNOWN;
    ptr->alarm.inhibit_alarms    = FM_FALSE;
    ptr->alarm.service_affecting = FM_FALSE;
    ptr->alarm.suppression       = FM_TRUE ;

    ptr->alarm.severity          = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state       = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf (ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "If this alarm does not automatically clear after some time and "
              "continues to be asserted after Host is locked and unlocked then "
              "contact next level of support for root cause analysis and recovery.");

    /** Service Status Log ****************************************************/

    ptr = &alarm_list[HBS_ALARM_ID__SERVICE];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", SERVICESTATUS_LOG_ID);

    ptr->name = "Service Status" ;

    ptr->minor_reason =
    ptr->major_reason =
    ptr->critl_reason =
    ptr->clear_reason = "";

    ptr->alarm.alarm_type         = FM_ALARM_TYPE_UNKNOWN  ;
    ptr->alarm.probable_cause     = FM_ALARM_CAUSE_UNKNOWN ;
    ptr->alarm.inhibit_alarms     = FM_FALSE ;
    ptr->alarm.service_affecting  = FM_FALSE ;
    ptr->alarm.suppression        = FM_FALSE ;

    ptr->alarm.severity           = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state        = FM_ALARM_STATE_MSG      ; /* Dynamic */

    snprintf ( ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH, "%s", "");
}

/* Translate alarm identity enum to alarm identity string */
string _getIdentity ( alarm_id_enum id )
{
    if ( id < HBS_ALARM_ID__LAST )
        return ( alarm_id_table[id].identity_str) ;
    return ("200.000");
}
