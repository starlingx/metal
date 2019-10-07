/*
 * Copyright (c) 2013 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform common Alarm utilities
  */

#define __MODULE_PRIVATE__

#include "daemon_common.h"   /* for ... daemon_is_file_present */

#include "nodeBase.h"
#include "nodeUtil.h"        /* for ... get_mtclogd_sockPtr    */
#include "alarm.h"           /* for ... alarm Utilities        */


string alarmUtil_getSev_str ( EFmAlarmSeverityT sev )
{
    switch ( sev )
    {
        case FM_ALARM_SEVERITY_CLEAR:   return ("clear");
        case FM_ALARM_SEVERITY_WARNING: return ("warning");
        case FM_ALARM_SEVERITY_MINOR:   return ("minor");
        case FM_ALARM_SEVERITY_MAJOR:   return ("major");
        case FM_ALARM_SEVERITY_CRITICAL:return ("critical");
        default :                       return ("unknown");
    }
}

EFmAlarmSeverityT alarmUtil_getSev_enum ( string sev )
{
   if ( !sev.compare("clear"))    return (FM_ALARM_SEVERITY_CLEAR) ;
   if ( !sev.compare("warning"))  return (FM_ALARM_SEVERITY_WARNING);
   if ( !sev.compare("minor"))    return (FM_ALARM_SEVERITY_MINOR);
   if ( !sev.compare("major"))    return (FM_ALARM_SEVERITY_MAJOR);
   if ( !sev.compare("critical")) return (FM_ALARM_SEVERITY_CRITICAL);
   wlog ("Unsupported severity '%s'\n", sev.c_str() );
   return (FM_ALARM_SEVERITY_CLEAR) ;
}

int alarmUtil_getId_enum ( string alarmid, alarm_id_enum & id )
{
    /* Get the ID */
    if ( alarmid.compare(CLSTR_HB_ALARM_ID) == 0 )
    {
        id = HBS_ALARM_ID__HB_CLSTR ;
    }
    else if ( alarmid.compare(MGMNT_HB_ALARM_ID) == 0 )
    {
        id = HBS_ALARM_ID__HB_MGMNT ;
    }
    else if ( alarmid.compare(PMOND_ALARM_ID) == 0 )
    {
        id = HBS_ALARM_ID__PMOND ;
    }
    else if ( alarmid.compare(SERVICESTATUS_LOG_ID) == 0 )
    {
        id = HBS_ALARM_ID__SERVICE ;
    }
    else
    {
       wlog ("Unsupported alarm id '%s'\n", alarmid.c_str());
       return (FAIL_BAD_PARM);
    }
    return (PASS);
}

/* update the passed in alarm struct's instance_id entity path for the specified host */
void _build_entity_path ( string & hostname, string & instance, SFmAlarmDataT & alarm )
{
    snprintf ( &alarm.entity_type_id[0], FM_MAX_BUFFER_LENGTH, "system.host" );

    if ( instance.empty() )
    {
        snprintf ( &alarm.entity_instance_id[0], FM_MAX_BUFFER_LENGTH, "%s%s",
                    ENTITY_PREFIX, hostname.data());
    }
    else
    {
        snprintf ( &alarm.entity_instance_id[0], FM_MAX_BUFFER_LENGTH, "%s%s.%s",
                    ENTITY_PREFIX, hostname.data(), instance.data());
    }
}

void alarmUtil_clear_all ( string hostname )
{
    SFmAlarmDataT alarm ;
    string instance = "" ;

    _build_entity_path ( hostname, instance, alarm );

    /* This will clear all the alarms for this host ;
     * even ones that are raised against this host by other daemons */
    fm_clear_all ( &alarm.entity_instance_id );
}

/******************************************************************************
 *
 * Name       : alarmUtil_query
 *
 * Description: Utility will query a specific alarm for its current severity
 *
 * @param     : identity may be 200.xxx
 *
 * @param     : instance may be
 *
 *              host=<hostname>
 *
 *                  example:
 *
 *                       hostname=compute-1
 *
 *              host=<hostname>.<function>=<specific>
 *
 *                  example:
 *
 *                       hostname=compute-1.process=mtcClient
 *
 *                       hostname=compute-1.sensor=Fan_PSU2
 *
 * Updates    : None
 *
 * Returns    : FM severity code for the specified alarm.
 *              FM_ALARM_SEVERITY_CLEAR if it not set.
 *
 ******************************************************************************/
EFmAlarmSeverityT alarmUtil_query ( string hostname,
                                    string identity,
                                    string instance )
{
    SFmAlarmDataT alarm_query  ;
    AlarmFilter   alarm_filter ;
    EFmErrorT     rc           ;

    memset(&alarm_query, 0, sizeof(alarm_query));
    memset(&alarm_filter, 0, sizeof(alarm_filter));

    snprintf ( &alarm_filter.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", identity.data());

    if ( instance.empty() )
    {
        snprintf ( &alarm_filter.entity_instance_id[0], FM_MAX_BUFFER_LENGTH, "%s%s",
                    ENTITY_PREFIX, hostname.data());
    }
    else
    {
        snprintf ( &alarm_filter.entity_instance_id[0], FM_MAX_BUFFER_LENGTH, "%s%s.%s",
                    ENTITY_PREFIX, hostname.data(), instance.data());
    }

    alog ("entity_instance:%s\n", alarm_filter.entity_instance_id );
    if (( rc = fm_get_fault ( &alarm_filter, &alarm_query )) == FM_ERR_OK )
    {
        dlog ("Found with Severity: %d\n", alarm_query.severity );
        return (alarm_query.severity) ;
    }
    else if ( rc != FM_ERR_ENTITY_NOT_FOUND )
    {
        elog ("%s fm_get_fault returned error (%d)\n", hostname.c_str(), rc );
    }
    return (FM_ALARM_SEVERITY_CLEAR);
}

int alarmUtil_query_identity ( string identity, SFmAlarmDataT * alarm_list_ptr, unsigned int max_alarms )
{
    int rc = 0 ;

    if ( max_alarms == 0 )
    {
        slog ("max alarms is zero !\n");
    }
    else if ( identity.empty() )
    {
        slog ("empty alarm 'identity'\n");
    }
    else if ( alarm_list_ptr )
    {
        AlarmFilter alarm_filter ;

        memset(&alarm_filter, 0, sizeof(alarm_filter));
        snprintf ( alarm_filter.alarm_id, FM_MAX_BUFFER_LENGTH, "%s", identity.data());
        rc = fm_get_faults_by_id ( &alarm_filter.alarm_id, alarm_list_ptr, &max_alarms );
        alog ("%s fm_get_faults_by_id rc = %d\n", alarm_filter.alarm_id, rc );
        if ( rc == FM_ERR_OK )
        {
            return (PASS);
        }
        else if ( rc == FM_ERR_ENTITY_NOT_FOUND )
        {
            return (RETRY);
        }
        else
        {
            return (FAIL);
        }
    }
    else
    {
        slog ("caller supplied null alarm_list_ptr\n");
    }
    return (FAIL_NULL_POINTER);
}


/*********************************************************************************
 *
 * Name    : alarmUtil
 *
 * Purpose : Primary module API used to set/clear severity alarms and logs in FM.
 *
 * Description : Other maintenance services are expected to use ths interface to
 *
 *
 ********************************************************************************/

int alarmUtil ( string & hostname,
                string & identity,
                string & instance,
                SFmAlarmDataT & alarm )
{
    int rc = PASS ;

    // msgSock_type * mtclogd_ptr = get_mtclogd_sockPtr() ;

    /* Don't report events while we are in reset mode */
    if ( daemon_is_file_present ( NODE_RESET_FILE ) )
    {
       return (rc);
    }

    _build_entity_path ( hostname, instance, alarm );

#ifdef WANT_ALARM_QUERY

    /* See if the alarm is already in the requested state */
    EFmAlarmSeverityT curr_sev = alarmUtil_query ( hostname, identity, instance ) ;

    /* If its not a log message and we are already at this
     * severity level then ignore the call */
    if (( alarm.alarm_state != FM_ALARM_STATE_MSG ) &&
        ( curr_sev == alarm.severity ))
    {
        ilog ("%s %s %s already at desired (%s) severity level\n",
                  hostname.c_str(),
                  identity.c_str(),
                  instance.c_str(),
                  alarmUtil_getSev_str(alarm.severity).c_str());
        return (rc);
    }
#endif

    snprintf(&alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", identity.data());

    if (( alarm.alarm_state == FM_ALARM_STATE_SET ) ||
        ( alarm.alarm_state == FM_ALARM_STATE_MSG ))
    {
        if ( alarm.alarm_state == FM_ALARM_STATE_SET )
        {
            alog ("%s setting %s %s alarm\n", hostname.c_str(), alarm.alarm_id, alarm.entity_instance_id );
        }
        else
        {
            alog ("%s creating %s %s log\n", hostname.c_str(), alarm.alarm_id, alarm.entity_instance_id );
        }

        /* Debug Logs */
        alog ("%s Alarm Reason: %s\n", hostname.c_str(), alarm.reason_text );
        alog ("%s Alarm Action: %s\n", hostname.c_str(), alarm.proposed_repair_action );
        alog ("%s Alarm Ident : %s : %s\n", hostname.c_str(), alarm.entity_type_id, alarm.entity_instance_id );
        alog ("%s Alarm State : state:%d sev:%d type:%d cause:%d sa:%c supp:%c\n",
                  hostname.c_str(),
                  alarm.alarm_state,
                  alarm.severity,
                  alarm.alarm_type,
                  alarm.probable_cause,
                  alarm.service_affecting ? 'Y' : 'N',
                  alarm.suppression ? 'Y' : 'N' );

#ifdef WANT_FIT_TESTING
        if (( daemon_is_file_present ( MTC_CMD_FIT__FM_ERROR_CODE )) &&
            ( daemon_want_fit ( FIT_CODE__FM_SET_ALARM, hostname )))
        {
            rc = daemon_get_file_int(MTC_CMD_FIT__FM_ERROR_CODE) ;
        }
        else
#endif
        {
            rc = fm_set_fault ( &alarm , NULL );
        }
        if ( rc == FM_ERR_OK )
        {
            ilog  ( "%s %s %s alarm raised (%s)",
                        hostname.c_str(),
                        alarm.alarm_id,
                        alarm.entity_instance_id,
                        alarmUtil_getSev_str(alarm.severity).c_str());
        }
        /* error cases are handled/logged in the caller's ; dequeue API */
    }
    else
    {
        AlarmFilter filter ; memset(&filter, 0, sizeof(filter));

        /* Setup the alarm filter */
        snprintf(filter.alarm_id,           FM_MAX_BUFFER_LENGTH, "%s", alarm.alarm_id);
        snprintf(filter.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s", alarm.entity_instance_id);

        alog ( "fm_clear_fault: %s %s:%s", hostname.c_str(), alarm.entity_instance_id, alarm.alarm_id );

#ifdef WANT_FIT_TESTING
        if (( daemon_is_file_present ( MTC_CMD_FIT__FM_ERROR_CODE )) &&
            ( daemon_want_fit ( FIT_CODE__FM_CLR_ALARM, hostname )))
        {
            rc = daemon_get_file_int(MTC_CMD_FIT__FM_ERROR_CODE) ;
        }
        else
#endif
        {
            rc = fm_clear_fault ( &filter );
        }

        if ( rc == FM_ERR_OK )
        {
            ilog ("%s %s %s alarm cleared\n",
                      hostname.c_str(),
                      alarm.alarm_id,
                      alarm.entity_instance_id);
        }
        /* error cases are handled/logged in the caller's ; dequeue API */
    }

    return (rc);
}

/* Clear the specified hosts's alarm */
int alarmUtil_clear ( string hostname, string alarm_id , string entity )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_CLEAR ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_CLEAR ;
       return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}

/** Assert a specified hosts's alarm with a CRITICAL severity level */
int alarmUtil_critical ( string hostname, string alarm_id , string entity, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_CRITICAL ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_SET ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_ptr->critl_reason.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}


/** Assert a specified host's alarm with a MAJOR severity level */
int alarmUtil_major ( string hostname, string alarm_id , string entity, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_MAJOR ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_SET ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_ptr->major_reason.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}

/** Assert a specified host's alarm with a MINOR severity level */
int alarmUtil_minor        ( string hostname, string alarm_id , string entity, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_MINOR ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_SET ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_ptr->minor_reason.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}

/** Assert a specified host's alarm with a WARNING severity level */
int alarmUtil_warning        ( string hostname, string alarm_id , string entity, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_WARNING ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_SET ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_ptr->minor_reason.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}

/** Create CRITICAL log */
int alarmUtil_critical_log ( string hostname, string alarm_id , string entity, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_CRITICAL ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_MSG ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_ptr->critl_reason.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}


/** Create MAJOR log */
int alarmUtil_major_log ( string hostname, string alarm_id , string entity, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_MAJOR ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_MSG ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_ptr->major_reason.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}
/** Create MINOR log */
int alarmUtil_minor_log        ( string hostname, string alarm_id , string entity, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(entity);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_MINOR ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_MSG ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), alarm_ptr->minor_reason.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}

/** Create WARNING log */
int alarmUtil_warning_log ( string hostname, string alarm_id, string entity, string prefix, FMTimeT & timestamp )
{
    alarmUtil_type * alarm_ptr = alarmData_getAlarm_ptr(alarm_id);
    if ( alarm_ptr )
    {
        string instance = alarm_ptr->instc_prefix ;
        instance.append(prefix);

        alarm_ptr->alarm.severity    = FM_ALARM_SEVERITY_WARNING ;
        alarm_ptr->alarm.alarm_state = FM_ALARM_STATE_MSG ;
        if ( timestamp ) alarm_ptr->alarm.timestamp = timestamp ;

        snprintf ( alarm_ptr->alarm.reason_text, FM_MAX_BUFFER_LENGTH, "%s %s", hostname.data(), entity.data());

        return ( alarmUtil ( hostname, alarm_id, instance, alarm_ptr->alarm ));
    }
    return (FAIL_NULL_POINTER);
}
