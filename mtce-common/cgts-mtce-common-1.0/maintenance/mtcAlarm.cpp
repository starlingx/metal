/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud 'Maintenance Agent' Alarm Module
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
#include "mtcAlarm.h"      /* for ... this module header                */
#include "hbsAlarm.h"      /* for ... hbsAlarm stubs                    */

alarmUtil_type alarm_list[MTC_ALARM_ID__LAST] ;

void mtcAlarm_init ( void )
{
    alarmUtil_type * ptr ;

    /** Lock Alarm ************************************************************/

    ptr = &alarm_list[MTC_ALARM_ID__LOCK];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", LOCK_ALARM_ID);

    ptr->name = "Lock" ;
    ptr->instc_prefix = "" ;

    ptr->critl_reason =
    ptr->major_reason =
    ptr->minor_reason = "was administratively locked to take it out-of-service.";
    ptr->clear_reason = "was administratively unlocked and is back in-service.";

    ptr->alarm.alarm_type         = FM_ALARM_OPERATIONAL;
    ptr->alarm.probable_cause     = FM_ALARM_OUT_OF_SERVICE   ;
    ptr->alarm.inhibit_alarms     = FM_TRUE ;
    ptr->alarm.service_affecting  = FM_TRUE ;
    ptr->alarm.suppression        = FM_FALSE;

    ptr->alarm.severity           = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state        = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf( ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "Administratively unlock Host to bring it back in-service.");

    /** Enable Alarm ************************************************************/

    ptr = &alarm_list[MTC_ALARM_ID__ENABLE];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", ENABLE_ALARM_ID);

    ptr->name = "In-Service" ;
    ptr->instc_prefix = "" ;

    /* this is for a log */
    ptr->minor_reason = "has experienced a minor In-Service test event. "
                        "No action is required. " ;

    /* this is for an alarm and degrade */
    ptr->major_reason = "Host Services failed to start.";

    ptr->critl_reason = "experienced a service-affecting failure. "
                        "Auto-recovery in progress. "
                        "Manual Lock and Unlock may be required if auto-recovery is unsuccessful.";

    ptr->clear_reason = "was auto recovered through Reboot and is now in-service if 'unlocked-enabled' "
                        "or is otherwise 'locked-disabled' by administrative 'lock' action.";

    ptr->alarm.alarm_type         = FM_ALARM_OPERATIONAL;
    ptr->alarm.probable_cause     = FM_ALARM_APP_SUBSYS_FAILURE ;
    ptr->alarm.inhibit_alarms     = FM_FALSE ;
    ptr->alarm.service_affecting  = FM_TRUE  ;
    ptr->alarm.suppression        = FM_TRUE  ;

    ptr->alarm.severity           = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state        = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf (ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "If auto-recovery is consistently unable to recover host to the unlocked-enabled "
              "state contact next level of support or lock and replace failing Host.");


    /** Configuration Alarm ************************************************************/

    ptr = &alarm_list[MTC_ALARM_ID__CONFIG];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", CONFIG_ALARM_ID);

    ptr->name = "Configuration" ;
    ptr->instc_prefix = "" ;

    ptr->critl_reason =
    ptr->major_reason =
    ptr->minor_reason = "experienced a configuration failure. ";
    ptr->clear_reason = "has been successfully configured and is now in-service if 'unlocked-enabled' "
                        "or is otherwise 'locked-disabled' by administrative 'lock' action.";

    ptr->alarm.alarm_type         = FM_ALARM_OPERATIONAL;
    ptr->alarm.probable_cause     = FM_ALARM_CONFIG_ERROR ;
    ptr->alarm.inhibit_alarms     = FM_FALSE;
    ptr->alarm.service_affecting  = FM_TRUE ;
    ptr->alarm.suppression        = FM_TRUE ;

    ptr->alarm.severity          = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state       = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf (ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "If manual or auto-recovery is consistently unable to recover host to the unlocked-enabled "
              "state contact next level of support or lock and replace failing Host.");

    /** Board Management Controller Access Alarm ************************************/

    ptr = &alarm_list[MTC_ALARM_ID__BM];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", BM_ALARM_ID);

    ptr->name = "Board Management Controller Access" ;
    ptr->instc_prefix = "" ;

    ptr->critl_reason = "board management controller is unresponsive." ;
    ptr->major_reason = "board management controller is unresponsive." ;
    ptr->minor_reason = "access to board management module has failed." ;
    ptr->clear_reason = "access to board management module is established" ;

    ptr->alarm.alarm_type        = FM_ALARM_OPERATIONAL ;
    ptr->alarm.probable_cause    = FM_ALARM_COMM_SUBSYS_FAILURE ;
    ptr->alarm.inhibit_alarms    = FM_FALSE;
    ptr->alarm.service_affecting = FM_FALSE;
    ptr->alarm.suppression       = FM_FALSE;

    ptr->alarm.severity          = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state       = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf( ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "Check Host's board management config and connectivity.");

    /** Controller Failure Alarm ****************************************************/

    ptr = &alarm_list[MTC_ALARM_ID__CH_CONT];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", CH_CONT_ALARM_ID);

    ptr->name = "Controller Function" ;
    ptr->instc_prefix = "" ;

    ptr->critl_reason =
    ptr->major_reason =
    ptr->minor_reason = "controller function has in-service failure while compute services "
                        "remain healthy.";
    ptr->clear_reason = "controller function has recovered";

    ptr->alarm.alarm_type         = FM_ALARM_OPERATIONAL;
    ptr->alarm.probable_cause     = FM_ALARM_APP_SUBSYS_FAILURE ;
    ptr->alarm.inhibit_alarms     = FM_FALSE ;
    ptr->alarm.service_affecting  = FM_TRUE  ;
    ptr->alarm.suppression        = FM_TRUE  ;

    ptr->alarm.severity           = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state        = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf (ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "Lock and then Unlock host to recover. "
              "Avoid using 'Force Lock' action as that will impact compute services "
              "running on this host. If lock action fails then contact next level "
              "of support to investigate and recover.");

    /** Compute Failure Alarm ****************************************************/

    ptr = &alarm_list[MTC_ALARM_ID__CH_COMP];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", CH_COMP_ALARM_ID);

    ptr->name = "Compute Function" ;
    ptr->instc_prefix = "" ;

    ptr->minor_reason =
    ptr->major_reason = "Compute service is not fully operational. Auto recovery in progress." ;
    ptr->critl_reason = "Compute service of the only available controller is not operational. "
                        "Auto-recovery disabled. Degrading host instead.";
    ptr->clear_reason = "compute service has recovered";

    ptr->alarm.alarm_type         = FM_ALARM_OPERATIONAL;
    ptr->alarm.probable_cause     = FM_ALARM_APP_SUBSYS_FAILURE ;
    ptr->alarm.inhibit_alarms     = FM_FALSE ;
    ptr->alarm.service_affecting  = FM_TRUE  ;
    ptr->alarm.suppression        = FM_TRUE  ;

    ptr->alarm.severity           = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state        = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf (ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "If alarm is against the only active controller then Enable second controller "
              "and Switch Activity (Swact) to it as soon as possible. If the alarm "
              "persists then Lock/Unlock host to recover its local compute service.");

    /** Add Event Log ****************************************************/

    ptr = &alarm_list[MTC_LOG_ID__EVENT];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", EVENT_LOG_ID);

    ptr->name = "Maintenance Event" ;

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

string _getIdentity ( mtc_alarm_id_enum id )
{
    switch ( id )
    {
        case MTC_ALARM_ID__LOCK:      return (LOCK_ALARM_ID);
        case MTC_ALARM_ID__CONFIG:    return (CONFIG_ALARM_ID);
        case MTC_ALARM_ID__ENABLE:    return (ENABLE_ALARM_ID);
        case MTC_ALARM_ID__BM:        return (BM_ALARM_ID);
        case MTC_ALARM_ID__CH_CONT:   return (CH_CONT_ALARM_ID);
        case MTC_ALARM_ID__CH_COMP:   return (CH_COMP_ALARM_ID);
        case MTC_LOG_ID__EVENT:       return (EVENT_LOG_ID);
        case MTC_LOG_ID__COMMAND:     return (COMMAND_LOG_ID);
        case MTC_LOG_ID__STATECHANGE: return (STATECHANGE_LOG_ID);
        default: return ("200.000");
    }
}

string mtcAlarm_getId_str ( mtc_alarm_id_enum id )
{
    return(_getIdentity(id));
}

string _getInstance ( mtc_alarm_id_enum id )
{
    id = id ;
    return ("");
}

EFmAlarmSeverityT mtcAlarm_state ( string hostname, mtc_alarm_id_enum id )
{
    string identity = _getIdentity(id) ;
    string instance = _getInstance(id) ;
    return ( alarmUtil_query ( hostname, identity, instance));
}

void mtcAlarm_clear_all ( string hostname )
{
    for ( int i = 0 ; i < MTC_ALARM_ID__LAST ; ++i )
    {
        mtcAlarm_clear ( hostname, (mtc_alarm_id_enum)i );
    }
}

/*************************   A L A R M I N G   **************************/

/* Clear the specified hosts's maintenance alarm */
int mtcAlarm_clear ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        ilog ("%s clearing '%s' alarm (%s%s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_list[id].clear_reason.data());

        return ( alarmUtil_clear ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Assert a specified hosts's mtce alarm with a CRITICAL severity level */
int mtcAlarm_critical ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        elog ("%s setting critical '%s' failure alarm (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_list[id].critl_reason.data());

        return ( alarmUtil_critical ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Assert a specified host's mtce alarm with a MAJOR severity level */
int mtcAlarm_major ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        wlog ("%s setting major '%s' failure alarm (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        if ( id == MTC_ALARM_ID__BM )
        {
            snprintf( alarm_list[id].alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "board managment controller 'reset' or 'power-cycle' is recommended.");
        }

        else if ( id == MTC_ALARM_ID__ENABLE )
        {
            snprintf( alarm_list[id].alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "If alarm persists, host may require lock/unlock to recover. See maintenance logs for more detail.");
        }

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_list[id].major_reason.data());

        return ( alarmUtil_major ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Assert a specified host's mtce alarm with a MINOR severity level */
int mtcAlarm_minor ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        wlog ("%s setting minor '%s' failure alarm (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_list[id].minor_reason.data());

        return ( alarmUtil_minor ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Assert a specified host's mtce alarm with a WARNING severity level */
int mtcAlarm_warning ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        wlog ("%s setting warning '%s' alarm (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        if ( id == MTC_ALARM_ID__BM )
        {
            snprintf( alarm_list[id].alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "Check Host's board management config and connectivity.");
        }

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_list[id].minor_reason.data());

        return ( alarmUtil_warning ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/***************************   L O G G I N G   **********************************/

/** Create a CRITICAL maintenance log */
int  mtcAlarm_critical_log ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        elog ("%s creating critical '%s' log (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_list[id].critl_reason.data());

        return ( alarmUtil_critical_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Create a MAJOR maintenance log */
int  mtcAlarm_major_log    ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        wlog ("%s creating major '%s' log (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_list[id].major_reason.data());

        return ( alarmUtil_major_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Create a MINOR maintenance log */
int  mtcAlarm_minor_log    ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        wlog ("%s creating minor '%s' log (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s", alarm_list[id].minor_reason.data());

        return ( alarmUtil_minor_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Create a WARNING maintenance log */
int  mtcAlarm_warning_log  ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);

        wlog ("%s creating warning '%s' log (%s %s)\n",
                  hostname.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s", alarm_list[id].minor_reason.data());

        return ( alarmUtil_warning_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Create a neutral customer log */
int  mtcAlarm_log  ( string hostname, mtc_alarm_id_enum id )
{
    if ( id < MTC_ALARM_ID__END )
    {
        /* default to command */
        mtc_alarm_id_enum index = MTC_LOG_ID__COMMAND ;
        bool found = false ;

        if ( id == MTC_LOG_ID__EVENT_ADD )
        {
            index = MTC_LOG_ID__EVENT ;
            alarm_list[index].instc_prefix = "event=add" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "has been 'added' to the system" );
            found = true ;

        }
        else if ( id == MTC_LOG_ID__EVENT_MNFA_ENTER )
        {
            index = MTC_LOG_ID__EVENT ;
            alarm_list[index].instc_prefix = "event=mnfa_enter" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "has 'entered' multi-node failure avoidance" );
            found = true ;

        }
        else if ( id == MTC_LOG_ID__EVENT_MNFA_EXIT )
        {
            index = MTC_LOG_ID__EVENT ;
            alarm_list[index].instc_prefix = "event=mnfa_exit" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "has 'exited' multi-node failure avoidance" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__STATUSCHANGE_FAILED )
        {
            index = MTC_LOG_ID__STATECHANGE ;
            alarm_list[index].instc_prefix = "status=failed" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "is 'disabled-failed' to the system" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__STATUSCHANGE_ENABLED )
        {
            index = MTC_LOG_ID__STATECHANGE ;
            alarm_list[index].instc_prefix = "state=enabled" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "is now 'enabled'" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__STATUSCHANGE_DISABLED )
        {
            index = MTC_LOG_ID__STATECHANGE ;
            alarm_list[index].instc_prefix = "state=disabled" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "is now 'disabled'" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__STATUSCHANGE_OFFLINE )
        {
            index = MTC_LOG_ID__STATECHANGE ;
            alarm_list[index].instc_prefix = "status=offline" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "is now 'offline'" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__STATUSCHANGE_ONLINE )
        {
            index = MTC_LOG_ID__STATECHANGE ;
            alarm_list[index].instc_prefix = "status=online" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "is now 'online'" );
            found = true ;
        }

        else if ( id == MTC_LOG_ID__STATUSCHANGE_REINSTALL_FAILED )
        {
            index = MTC_LOG_ID__STATECHANGE ;
            alarm_list[index].instc_prefix = "status=reinstall-failed" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "reinstall failed" );
            found = true ;
        }

        else if ( id == MTC_LOG_ID__STATUSCHANGE_REINSTALL_COMPLETE )
        {
            index = MTC_LOG_ID__STATECHANGE ;
            alarm_list[index].instc_prefix = "status=reinstall-complete" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "reinstall completed successfully" );
            found = true ;
        }

        else if ( id == MTC_LOG_ID__COMMAND_UNLOCK )
        {
            alarm_list[index].instc_prefix = "command=unlock" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'unlock' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_FORCE_LOCK )
        {
            alarm_list[index].instc_prefix = "command=force-lock" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'force-lock' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_SWACT )
        {
            alarm_list[index].instc_prefix = "command=swact" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'controller switchover' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_MANUAL_REBOOT )
        {
            alarm_list[index].instc_prefix = "command=reboot" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'reboot' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_AUTO_REBOOT )
        {
            alarm_list[index].instc_prefix = "action=reboot" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "'reboot' action" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_MANUAL_RESET )
        {
            alarm_list[index].instc_prefix = "command=reset" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'reset' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_AUTO_RESET )
        {
            alarm_list[index].instc_prefix = "action=reset" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "'reset' action" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_REINSTALL )
        {
            alarm_list[index].instc_prefix = "command=reinstall" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'reinstall' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_MANUAL_POWER_ON )
        {
            alarm_list[index].instc_prefix = "command=power-on" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'power-on' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_AUTO_POWER_ON )
        {
            alarm_list[index].instc_prefix = "action=power-on" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "'power-on' action" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_MANUAL_POWER_OFF )
        {
            alarm_list[index].instc_prefix = "command=power-off" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'power-off' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_AUTO_POWER_OFF )
        {
            alarm_list[index].instc_prefix = "action=power-off" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "'power-off' action" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_DELETE )
        {
            alarm_list[index].instc_prefix = "command=delete" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "manual 'delete' request" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_BM_PROVISIONED )
        {
            alarm_list[index].instc_prefix = "command=provision" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "board management controller has been 'provisioned'" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_BM_DEPROVISIONED )
        {
            alarm_list[index].instc_prefix = "command=deprovision" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "board management controller has been 'de-provisioned'" );
            found = true ;
        }
        else if ( id == MTC_LOG_ID__COMMAND_BM_REPROVISIONED )
        {
            alarm_list[index].instc_prefix = "command=reprovision" ;
            snprintf ( alarm_list[index].alarm.reason_text,
                       FM_MAX_BUFFER_LENGTH, "%s %s",
                       hostname.data(),
                       "board management controller has been 're-provisioned'" );
            found = true ;
        }

        if ( found == true )
        {
            int rc ;

            string identity = _getIdentity(index);
            string instance = _getInstance(index);
            instance.append(alarm_list[index].instc_prefix);
            //wlog ("%s '%s' log (%s.%s)\n",
            //          hostname.c_str(),
            //          alarm_list[index].alarm.reason_text,
            //          identity.c_str(),
            //          instance.c_str());

            /* Want to make this log a critical */
            if ( id == MTC_LOG_ID__STATUSCHANGE_REINSTALL_FAILED )
            {
                alarm_list[index].alarm.severity = FM_ALARM_SEVERITY_CRITICAL ;
            }

            rc = alarmUtil_log ( hostname, identity, instance, alarm_list[index].alarm );

            /* Revert the severity of the event log back to Clear ( shows up as N/A ) */
            if ( id == MTC_LOG_ID__STATUSCHANGE_REINSTALL_FAILED )
            {
                alarm_list[MTC_LOG_ID__STATECHANGE].alarm.severity = FM_ALARM_SEVERITY_CLEAR ;
            }
            return (rc);
        }
    }
    return (FAIL_BAD_PARM);
}
