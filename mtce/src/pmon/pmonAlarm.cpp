/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud 'Process Monitor' Alarm Module
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
#include "pmonAlarm.h"     /* for ... this module header                */
#include "pmon.h"

alarmUtil_type alarm_list[PMON_ALARM_ID__LAST] ;

void pmonAlarm_init ( void )
{
    alarmUtil_type * ptr ;

    /** Process Failure Alarm ****************************************************/

    ptr = &alarm_list[PMON_ALARM_ID__PMOND];
    memset  (&ptr->alarm, 0, (sizeof(SFmAlarmDataT)));
    snprintf(&ptr->alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", PMOND_ALARM_ID);

    ptr->name = "process failure" ;
    ptr->instc_prefix = "process=" ;

    ptr->critl_reason = "";
    ptr->minor_reason = "";
    ptr->major_reason = "";
    ptr->clear_reason = "";

    ptr->alarm.alarm_type        = FM_ALARM_OPERATIONAL  ;
    ptr->alarm.probable_cause    = FM_ALARM_CAUSE_UNKNOWN;
    ptr->alarm.inhibit_alarms    = FM_FALSE;
    ptr->alarm.service_affecting = FM_TRUE ;
    ptr->alarm.suppression       = FM_TRUE ;

    ptr->alarm.severity          = FM_ALARM_SEVERITY_CLEAR ; /* Dynamic */
    ptr->alarm.alarm_state       = FM_ALARM_STATE_CLEAR    ; /* Dynamic */

    snprintf (ptr->alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
              "If problem consistently occurs after Host is locked and unlocked then "
              "contact next level of support for root cause analysis and recovery.");
}

string _getIdentity ( pmon_alarm_id_enum id )
{
    switch ( id )
    {
        case PMON_ALARM_ID__PMOND    :return (PMOND_ALARM_ID);
        default: return ("200.000");
    }
}

string pmonAlarm_getId_str ( pmon_alarm_id_enum id )
{
    return(_getIdentity(id));
}

string _getInstance ( pmon_alarm_id_enum id )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        return(alarm_list[id].instc_prefix);
    }
    return ("");
}

EFmAlarmSeverityT pmonAlarm_state ( string hostname, pmon_alarm_id_enum id )
{
    string identity = _getIdentity(id) ;
    string instance = _getInstance(id) ;
    return ( alarmUtil_query ( hostname, identity, instance));
}

/******************************************************************************
 *
 * Name       : query_alarms
 *
 * Description: query FM for all the existing process monitor alarms and build
 *              up the callers 'saved_alarm_list' with those process names and
 *              corresponding severity.
 *
 * Updates    : callers saved_alarm_list
 *
 * Returns    : PASS if FM returns no error
 *              FAIL_REQUEST      ... alarmUtil_query_identity failed
 *              FAIL_OPERATION    ... fm_get_fault failed
 *              FAIL_NULL_POINTER ... failed to get memory
 *
 ******************************************************************************/

int query_alarms (  list<active_process_alarms_type> & saved_alarm_list, string hostname )
{
    static const char HOSTNAME_LABEL [] = "host=" ;
    static const char PROCNAME_LABEL [] = ".process=" ;

    int rc = FAIL ;
    saved_alarm_list.clear();

    SFmAlarmDataT * alarm_list_ptr = (SFmAlarmDataT*) malloc ((sizeof(SFmAlarmDataT)*PMON_MAX_ALARMS));
    if ( alarm_list_ptr )
    {
        /* Query all the pmon alarms  */
        rc = alarmUtil_query_identity ( pmonAlarm_getId_str(PMON_ALARM_ID__PMOND), alarm_list_ptr, PMON_MAX_ALARMS );
        if ( rc == RETRY )
        {
            dlog ("no %s alarms found",  pmonAlarm_getId_str(PMON_ALARM_ID__PMOND).c_str());
            rc = PASS ;
        }
        else if ( rc == PASS )
        {
            for ( int i = 0 ; i < PMON_MAX_ALARMS ; ++i )
            {
                /* loop over each active alarm and maintain its activity state */
                if ( strnlen ((alarm_list_ptr+i)->entity_instance_id , MAX_FILENAME_LEN ) )
                {
                    AlarmFilter   alarm_filter ;
                    SFmAlarmDataT alarm_query  ;
                    memset(&alarm_query, 0, sizeof(alarm_query));
                    memset(&alarm_filter, 0, sizeof(alarm_filter));

                    snprintf ( &alarm_filter.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", PMOND_ALARM_ID );
                    snprintf ( &alarm_filter.entity_instance_id[0], FM_MAX_BUFFER_LENGTH, "%s", (alarm_list_ptr+i)->entity_instance_id );

                    if (( rc = fm_get_fault ( &alarm_filter, &alarm_query )) == FM_ERR_OK )
                    {
                        rc = PASS ;

                        string entity = alarm_filter.entity_instance_id ;
                        size_t pos_hn = entity.find(HOSTNAME_LABEL);
                        size_t pos_pn = entity.find(PROCNAME_LABEL);

                        if (( pos_hn != std::string::npos ) &&
                            ( pos_pn != std::string::npos ))
                        {
                            string hn = entity.substr(pos_hn+strlen(HOSTNAME_LABEL), pos_pn-strlen(HOSTNAME_LABEL));
                            string pn = entity.substr(pos_pn+strlen(PROCNAME_LABEL));

                            /* verify hostname */
                            if ( ( hn.length() == 0 ) || ( hn != hostname ) )
                            {
                                /* ignore alarms not for this host */
                                dlog ("%s %s %s alarm not for this host",
                                          entity.c_str(),
                                          hn.c_str(),
                                          pn.c_str());
                                continue ;
                            }
                            dlog ("%s alarm is %s (process:%s)\n",
                                      alarm_filter.entity_instance_id,
                                      alarmUtil_getSev_str(alarm_query.severity).c_str(),
                                      pn.c_str());

                            /* filter out 'process=pmond'
                             * ... that alarm is handled by hbsAgent */
                            if ( pn != MTC_SERVICE_PMOND_NAME )
                            {
                                 active_process_alarms_type this_alarm ;
                                 this_alarm.process  = pn ;
                                 this_alarm.severity = alarm_query.severity ;
                                 saved_alarm_list.push_front ( this_alarm  );
                            }
                        }
                    }
                    else
                    {
                        wlog ("fm_get_fault failed (rc:%d)\n", rc );
                        rc = FAIL_OPERATION ;
                        break ;
                    }
                }
                else
                {
                    dlog2 ("last entry %d\n", i);
                    break ;
                }
            } /* for loop */
        }
        else
        {
            wlog("failed to query alarms from fm ; rc:%d", rc);
            rc = FAIL_REQUEST ;
        }
        free(alarm_list_ptr);
    }
    else
    {
        elog ("unable to allocate memory for alarm list");
        rc = FAIL_NULL_POINTER ;
    }
    return (rc);
}

/*************************   A L A R M I N G   **************************/

/* Clear the specified hosts's maintenance alarm */
int pmonAlarm_clear ( string hostname, pmon_alarm_id_enum id, string process )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);
        instance.append(process);

        ilog ("%s clearing '%s' %s alarm (%s.%s)\n",
                  hostname.c_str(),
                  process.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());
        
        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, 
                   "%s '%s' process has been successfully recovered and is now functioning properly.",
                   hostname.data(), process.data());

        return ( alarmUtil_clear ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Assert a specified hosts's mtce alarm with a CRITICAL severity level */
int pmonAlarm_critical ( string hostname, pmon_alarm_id_enum id, string process )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);
        instance.append(process);

        elog ("%s setting critical '%s' %s alarm (%s.%s)\n",
                  hostname.c_str(),
                  process.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                   "%s critical '%s' process has failed and could not be auto-recovered gracefully. "
                   "Auto-recovery progression by host reboot is required and in progress. "
                   "Manual Lock and Unlock may be required if auto-recovery is unsuccessful.",
                   hostname.data(), process.data());

        return ( alarmUtil_critical ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Assert a specified host's mtce alarm with a MAJOR severity level */
int pmonAlarm_major ( string hostname, pmon_alarm_id_enum id, string process )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);
        instance.append(process);

        wlog ("%s setting major '%s' %s alarm (%s.%s)\n",
                  hostname.c_str(),
                  process.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, 
                   "%s is degraded due to the failure of its '%s' process. "
                   "Auto recovery of this major process is in progress.", 
                   hostname.data(), process.data());

        return ( alarmUtil_major ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Assert a specified host's mtce alarm with a MINOR severity level */
int pmonAlarm_minor ( string hostname, pmon_alarm_id_enum id, string process, int restarts  )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);
        instance.append(process);

        wlog ("%s setting minor '%s' %s alarm (%s.%s)\n",
                  hostname.c_str(),
                  process.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, 
                   "%s '%s' process has failed. %s",
                   hostname.data(), process.data(),
                   ((restarts == 0) ? "Manual recovery is required." : "Auto recovery in progress." ) );

        return ( alarmUtil_minor ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/***************************   L O G G I N G   **********************************/

/** Create a CRITICAL maintenance log */
int  pmonAlarm_critical_log ( string hostname, pmon_alarm_id_enum id, string process )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);
        instance.append(process);

        elog ("%s creating critical '%s' %s log (%s.%s)\n",
                  hostname.c_str(),
                  process.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, 
                   "%s critical '%s' process has failed and could not be auto-recovered gracefully.",
                   hostname.data(), process.data());

        return ( alarmUtil_critical_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Create a MAJOR maintenance log */
int  pmonAlarm_major_log    ( string hostname, pmon_alarm_id_enum id, string process )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);
        instance.append(process);

        wlog ("%s creating major '%s' %s log (%s.%s)\n",
                  hostname.c_str(),
                  process.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, 
                   "%s is degraded due to the failure of its '%s' process. "
                   "Auto recovery of this major process is in progress.", 
                   hostname.data(), process.data());

        return ( alarmUtil_major_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}

/** Create a MINOR maintenance log */
int  pmonAlarm_minor_log    ( string hostname, pmon_alarm_id_enum id, string process, int restarts )
{
    if ( id < PMON_ALARM_ID__LAST )
    {
        string identity = _getIdentity(id);
        string instance = _getInstance(id);
        instance.append(process);

        wlog ("%s creating minor '%s' %s log (%s.%s)\n",
                  hostname.c_str(),
                  process.c_str(),
                  alarm_list[id].name.c_str(),
                  identity.c_str(),
                  instance.c_str());

        snprintf ( alarm_list[id].alarm.reason_text, FM_MAX_BUFFER_LENGTH, 
                   "%s '%s' process has failed. %s",
                   hostname.data(), process.data(),
                   ((restarts == 0) ? "Manual recovery is required." : "Auto recovery in progress.") );

        return ( alarmUtil_minor_log ( hostname, identity, instance, alarm_list[id].alarm ));
    }
    return (FAIL_BAD_PARM);
}
