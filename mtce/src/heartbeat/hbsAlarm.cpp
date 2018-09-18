/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud 'Heartbeat Agent' Alarm Module
  */

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

#include "daemon_common.h" /*                                           */

#include "nodeBase.h"      /*                                           */
#include "nodeTimers.h"    /*                                           */
#include "nodeUtil.h"      /*                                           */
#include "hbsAlarm.h"      /* for ... this module header                */
#include "alarm.h"         /* for ... alarm send message to mtcalarmd   */

void hbsAlarm_clear_all ( string hostname, bool infra )
{
    alarm_clear ( hostname, MGMNT_HB_ALARM_ID, MGMNT_NAME );
    if ( infra )
        alarm_clear ( hostname, INFRA_HB_ALARM_ID, INFRA_NAME );
    alarm_clear ( hostname ,   PMOND_ALARM_ID,  PMON_NAME );
}

#ifdef WANT_OLD_CODE

/** Create a WARNING maintenance log */
int  hbsAlarm_warning_log  ( string hostname, hbs_alarm_id_enum id, string entity )
{
    if ( id < HBS_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        alarm_list[HBS_ALARM_ID__SERVICE].instc_prefix = "service=heartbeat" ;

        instance.append(alarm_list[HBS_ALARM_ID__SERVICE].instc_prefix);

        wlog ("%s %s %s warning log\n",
                  hostname.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), entity.data());

        return ( alarmUtil_warning_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

#endif
