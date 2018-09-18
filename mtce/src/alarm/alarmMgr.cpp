/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Alarm Manager Daemon Manager
  */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

#define __MODULE_PRIVATE__

#include "alarm.h"         /* module header                                */

int alarmMgr_manage_alarm ( string alarmid,
                            string hostname,
                            string operation,
                            string severity,
                            string entity,
                            string prefix)
{
    int rc = PASS ;
    string action = operation ;
    action.append (" alarm");
    EFmAlarmSeverityT sev ;

    ilog ("Alarm: alarmid:%s hostname:%s operation:%s severity:%s entity:%s prefix:%s\n",
           alarmid.c_str(),
           hostname.c_str(),
           operation.c_str(),
           severity.c_str(),
           entity.c_str(),
           prefix.c_str());

    sev = alarmUtil_getSev_enum ( severity );
    if (!operation.compare("msg"))
    {
        if ( sev == FM_ALARM_SEVERITY_WARNING )
        {
            //if ( prefix.compare("none"))
                alarmUtil_warning_log ( hostname, alarmid, entity, prefix );
            //else
            //    mtcAlarm_warning_log ( hostname, id, entity );
        }
        else if ( sev == FM_ALARM_SEVERITY_MINOR )
        {
            rc = alarmUtil_minor_log ( hostname, alarmid, entity );
        }
        else if ( sev == FM_ALARM_SEVERITY_MAJOR)
        {
            rc = alarmUtil_major_log ( hostname, alarmid, entity );
        }
        else if ( sev == FM_ALARM_SEVERITY_CRITICAL )
        {
            rc = alarmUtil_critical_log ( hostname, alarmid, entity );
        }
        else
        {
            rc = FAIL_INVALID_OPERATION ;
            wlog ("Unsupported log severity '%d:%s'\n", sev, severity.c_str());
        }
        action="create log" ;
    }

    /* Get the state */
    else if ( !operation.compare("clear"))
    {
        rc = alarmUtil_clear ( hostname, alarmid, entity );
    }

    else if ( !operation.compare("set") )
    {
        if ( sev == FM_ALARM_SEVERITY_WARNING )
            rc = alarmUtil_warning ( hostname, alarmid, entity );
        else if ( sev == FM_ALARM_SEVERITY_MINOR )
            rc = alarmUtil_minor ( hostname, alarmid, entity );
        else if ( sev == FM_ALARM_SEVERITY_MAJOR )
            rc = alarmUtil_major ( hostname, alarmid, entity );
        else if ( sev == FM_ALARM_SEVERITY_CRITICAL )
            rc = alarmUtil_critical ( hostname, alarmid, entity );
        else
        {
            rc = FAIL_INVALID_OPERATION ;
        }
    }
    else
    {
        rc = FAIL_BAD_CASE ;
    }
    if ( rc )
    {
        elog ("%s failed to %s '%s:%s'\n", hostname.c_str(), action.c_str(), alarmid.c_str(), entity.c_str() )
    }

    return (rc);
}

