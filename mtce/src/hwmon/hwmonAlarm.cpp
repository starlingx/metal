/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform common Alarm utilities
  */

#include "daemon_common.h" /* for ... daemon_is_file_present */
#include "nodeBase.h"
#include "nodeUtil.h"
#include "hwmonAlarm.h"

#define SENSOR_ALARM_ID    ((const char *)"200.007") /* Sensor read alarm ; i.e. the sensor read value bad  */
#define SENSORCFG_ALARM_ID ((const char *)"200.014") /* Sensor configuration alarm ; i.e. could not add     */

string _getSev_str ( EFmAlarmSeverityT sev )
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

void build_sensor_entity_path ( string & hostname , SFmAlarmDataT * alarm_ptr , string & sensorname )
{
    memset(alarm_ptr, 0, sizeof(SFmAlarmDataT));
    snprintf ( &alarm_ptr->entity_type_id[0] , FM_MAX_BUFFER_LENGTH, "system.host" );
    int num = snprintf ( &alarm_ptr->entity_instance_id[0], FM_MAX_BUFFER_LENGTH , "host=%s.sensor=%s", hostname.data(), sensorname.data());
    alog ("%s Entity Path:%d:%s\n", hostname.c_str(), num, &alarm_ptr->entity_instance_id[0] );
}


/* Utility will init the alarm and return the severity if it is currently asserted */
EFmAlarmSeverityT hwmon_alarm_query ( string & hostname, hwmonAlarm_id_type id, string & sensorname )
{
    SFmAlarmDataT alarm_query  ;
    AlarmFilter   alarm_filter ;

    memset(&alarm_query, 0, sizeof(alarm_query));
    memset(&alarm_filter, 0, sizeof(alarm_filter));
    
    alarm_query.severity = FM_ALARM_SEVERITY_CLEAR ;

    switch ( id )
    {
        case HWMON_ALARM_ID__SENSOR: 
            snprintf ( &alarm_filter.entity_instance_id[0], FM_MAX_BUFFER_LENGTH , "host=%s.sensor=%s", hostname.data(), sensorname.data());
            snprintf ( &alarm_filter.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", SENSOR_ALARM_ID);      break ;
        case HWMON_ALARM_ID__SENSORGROUP: 
            snprintf ( &alarm_filter.entity_instance_id[0], FM_MAX_BUFFER_LENGTH , "host=%s", hostname.data() );
            snprintf ( &alarm_filter.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", SENSORGROUP_ALARM_ID); break ;
        case HWMON_ALARM_ID__SENSORCFG: 
            snprintf ( &alarm_filter.entity_instance_id[0], FM_MAX_BUFFER_LENGTH , "host=%s.sensor=%s", hostname.data(), sensorname.data());
            snprintf ( &alarm_filter.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", SENSORCFG_ALARM_ID);   break ;
        default:
        {
            slog ("%s invalid alarm ID (%d)\n", hostname.c_str(), id );
            return (FM_ALARM_SEVERITY_CLEAR);
        }
    }

    EFmErrorT rc = fm_get_fault ( &alarm_filter, &alarm_query ) ;
    if ( rc == FM_ERR_OK )
    {
        alog ("Found with Severity: %d\n", alarm_query.severity );
        return (alarm_query.severity) ;
    }
    else if ( rc != FM_ERR_ENTITY_NOT_FOUND )
    {
        elog ("%s fm_get_fault returned error (%d)\n", hostname.c_str(), rc );
    }
    return (FM_ALARM_SEVERITY_CLEAR);
}

/******************************************************************************
 *
 * Name       : hwmonAlarm_query_entity
 *
 * Description: Query FM for all the sensor alarms that match the specified
 *              entity and update the user supplied list with the entity
 *              instance and its severity level.
 *
 ******************************************************************************/

int hwmonAlarm_query_entity ( string & hostname,
                              string & entity,
 list<hwmonAlarm_entity_status_type> & alarm_list )
{
    if ( hostname.empty() )
    {
        hostname = "unknown" ;
    }

    if ( entity.empty() )
    {
        slog ("no 'entity' string specified\n");
        return (FAIL_STRING_EMPTY);
    }

    alarm_list.clear();

    fm_ent_inst_t entity_instance ;
    SFmAlarmDataT fm_alarm_list [MAX_HOST_SENSORS];

    unsigned int max_alarms = MAX_HOST_SENSORS ;

    MEMSET_ZERO(fm_alarm_list);

    MEMSET_ZERO(entity_instance);
    snprintf ( entity_instance, FM_MAX_BUFFER_LENGTH, "%s", entity.data());

    EFmErrorT rc = fm_get_faults ( &entity_instance, &fm_alarm_list[0], &max_alarms );

#ifdef WANT_FIT_TESTING

    if ((( rc == FM_ERR_OK ) || ( rc == FM_ERR_ENTITY_NOT_FOUND )) &&
         ( daemon_want_fit ( FIT_CODE__FM_QRY_ALARMS, hostname )))
    {
         rc = FM_ERR_NOCONNECT ;
    }

#endif

    if ( rc == FM_ERR_OK )
    {
        hwmonAlarm_entity_status_type alarmed_entity ;
        max_alarms = 0 ;
        for ( int i = 0 ; i < MAX_HOST_SENSORS ; i++ )
        {
            /* loop over each active alarm list loading up the reference list with sensor name and severity */
            if ( strnlen ( fm_alarm_list[i].entity_instance_id , MAX_FILENAME_LEN ) )
            {
                alarmed_entity.entity   = entity ;
                alarmed_entity.instance = fm_alarm_list[i].entity_instance_id ;
                alarmed_entity.severity = fm_alarm_list[i].severity ;

                ilog ("%s found '%s' with '%s' severity\n",
                          hostname.c_str(),
                          alarmed_entity.instance.c_str(),
                          _getSev_str(alarmed_entity.severity).c_str());

                max_alarms++ ;
                alarm_list.push_back(alarmed_entity);
            }
            else
            {
                break ;
            }
        }

        ilog ("%s found %d:%ld alarms for '%s'\n", hostname.c_str(), max_alarms, alarm_list.size(), entity.c_str());
        return (PASS);
    }
    else if ( rc == FM_ERR_ENTITY_NOT_FOUND )
    {
        alog ("%s found no alarms for '%s'\n", hostname.c_str(), entity.c_str());
        return (PASS);
    }
    else
    {
        elog ("%s fm_get_faults for '%s' failed (rc:%d)\n", hostname.c_str(), entity.c_str(), rc );
        return (FAIL);
    }
}







int hwmon_alarm_util ( string           & hostname,
                       hwmonAlarm_id_type id,
                       EFmAlarmStateT     state,
                       EFmAlarmSeverityT  severity,
                       string           & sub_entity ,
                       string             reason )
{
    SFmAlarmDataT     alarm    ; /* local working alarm struct    */

    int rc = PASS ;
    if (( state == FM_ALARM_STATE_MSG ) && ( id == HWMON_ALARM_ID__SENSORCFG ))
    {
        slog ("%s customer logging not supported for sensor config alarm IDs (%d)\n", hostname.c_str(), id );
        return (FAIL_BAD_PARM);
    }

    build_sensor_entity_path ( hostname, &alarm,  sub_entity );

    alarm.alarm_state = state ;
    if ( state == FM_ALARM_STATE_CLEAR )
    {
        severity = FM_ALARM_SEVERITY_CLEAR ;
    }

    switch ( id )
    {
        case HWMON_ALARM_ID__SENSORCFG:
        {
            snprintf (&alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", SENSORCFG_ALARM_ID);
            alarm.alarm_type        = FM_ALARM_OPERATIONAL;
            alarm.service_affecting = FM_FALSE;
            alarm.suppression       = FM_TRUE ;
            alarm.severity          = severity;
            alarm.probable_cause    = FM_ALARM_CAUSE_UNKNOWN ;
            alarm.inhibit_alarms    = FM_FALSE;

            if ( alarm.alarm_state == FM_ALARM_STATE_SET )
            {
                snprintf(alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
                         "Check Board Management Controller (BMC) provisioning. "
                         "Try reprovisioning the BMC. If problem persists try power cycling the host and then "
                         "the entire server including the BMC power. "
                         "If problem persists then contact next level of support.");

                snprintf(alarm.reason_text , FM_MAX_BUFFER_LENGTH,
                         "The Hardware Monitor was unable to load, configure and monitor one or more hardware sensors.");

                wlog ("%s 'sensor config' alarm asserted (%s:%s)\n",
                          hostname.c_str(), alarm.alarm_id, sub_entity.c_str());
            }
            else
            {
                ilog ("%s 'sensor config' alarm cleared (%s:%s))\n", 
                          hostname.c_str(), alarm.alarm_id, sub_entity.c_str());
            }
            break ;
        }

        /* Handle Hardware Monitor Sensor Alarms */
        case HWMON_ALARM_ID__SENSOR:
        {
            bool hostname_prefix_in_reason = true ;

            snprintf (&alarm.alarm_id[0], FM_MAX_BUFFER_LENGTH, "%s", SENSOR_ALARM_ID);
            alarm.alarm_type        = FM_ALARM_OPERATIONAL  ;
            alarm.probable_cause    = FM_ALARM_UNSPECIFIED_REASON;
            alarm.suppression       = FM_FALSE;
            alarm.inhibit_alarms    = FM_FALSE;
            alarm.service_affecting = FM_FALSE;

            snprintf (alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
                      "If problem consistently occurs after Host is power cycled and or reset, "
                      "contact next level of support or lock and replace failing host.");

            snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                             "%s is reporting a '%s' out-of-tolerance reading from the '%s' sensor",
                             hostname.c_str(), _getSev_str (severity).c_str(), sub_entity.c_str());

            if ( state == FM_ALARM_STATE_CLEAR )
            {
                snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                             "%s:%s alarm clear\n",
                              alarm.alarm_id,
                              alarm.entity_instance_id);
                hostname_prefix_in_reason = false ;
            }
            else if ( state == FM_ALARM_STATE_MSG )
            {
                if ( severity == FM_ALARM_SEVERITY_WARNING )
                {
                    snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                              "%s '%s' sensor %s\n",
                              hostname.c_str(),
                              sub_entity.c_str(),
                              reason.c_str());
                }
                else
                {
                    snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                              "%s '%s' sensor %s %s\n",
                              hostname.c_str(),
                              sub_entity.c_str(),
                              reason.c_str(),
                              _getSev_str (severity).c_str());
                }
                hostname_prefix_in_reason = true ;
            }
            else
            {
                snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                             "%s is reporting a '%s' out-of-tolerance reading from the '%s' sensor",
                             hostname.c_str(), _getSev_str (severity).c_str(), sub_entity.c_str());
                hostname_prefix_in_reason = true ;
            }
            /* Override service affecting setting */
            if ( severity == FM_ALARM_SEVERITY_CRITICAL )
            {
                alarm.service_affecting = FM_TRUE;
                if ( reason.compare(REASON_RESETTING) == 0 )
                {
                   snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                             "%s is being auto recovered by 'reset' due to a 'critical' "
                             "out-of-tolerance reading from the '%s' sensor.",
                             hostname.c_str(), sub_entity.c_str());
                   hostname_prefix_in_reason = true ;
                }
                else if ( reason.compare(REASON_POWERCYCLING) == 0 )
                {
                   snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                             "%s is being auto recovered by 'power-cycle' due to a 'critical' "
                             "out-of-tolerance reading from the '%s' sensor.",
                             hostname.c_str(), sub_entity.c_str());
                   hostname_prefix_in_reason = true ;
                }
            }
            else if ( severity == FM_ALARM_SEVERITY_CLEAR )
            {
                alarm.service_affecting = FM_FALSE;

                if ( reason.empty() )
                    reason = "reporting an in-tolerance reading" ;

                snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                    "%s '%s' sensor %s", hostname.c_str(), sub_entity.c_str(), reason.c_str());
                hostname_prefix_in_reason = true ;

            }
            if ( hostname_prefix_in_reason == false )
            {
                ilog ("%s %s\n", hostname.c_str(), alarm.reason_text );
            }
            else
            {
                ilog ("%s\n", alarm.reason_text );
            }

            alarm.severity = severity ;
            break ;
        }

        /* Handle Hardware Monitor Sensor Alarms */
        case HWMON_ALARM_ID__SENSORGROUP:
        {
            memset   ( &alarm, 0, sizeof(SFmAlarmDataT));
            snprintf ( &alarm.entity_type_id[0]    , FM_MAX_BUFFER_LENGTH, "system.host" );
            snprintf ( &alarm.entity_instance_id[0], FM_MAX_BUFFER_LENGTH, "host=%s.sensorgroup=%s", hostname.data(), sub_entity.c_str());
            snprintf ( &alarm.alarm_id[0],           FM_MAX_BUFFER_LENGTH, "%s", SENSORGROUP_ALARM_ID);

            alarm.alarm_type        = FM_ALARM_OPERATIONAL  ;
            alarm.probable_cause    = FM_ALARM_UNSPECIFIED_REASON;
            alarm.suppression       = FM_FALSE;
            alarm.inhibit_alarms    = FM_FALSE;
            alarm.alarm_state       = state   ;

            snprintf (alarm.proposed_repair_action, FM_MAX_BUFFER_LENGTH,
                      "Check board management connectivity and try rebooting the board management controller. "
                      "If problem persists contact next level of support or lock and replace failing host.");

            if ( state == FM_ALARM_STATE_CLEAR )
            {
                alarm.service_affecting = FM_FALSE;

                alarm.severity = FM_ALARM_SEVERITY_CLEAR ;
                ilog ("%s %s:%s alarm clear\n",
                              hostname.c_str(),
                              alarm.alarm_id,
                              alarm.entity_instance_id);

                if ( reason.empty() )
                    reason = "reporting an in-tolerance reading" ;

                snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                     "%s '%s' sensor group %s", hostname.c_str(), sub_entity.c_str(), reason.c_str());
            }
            else if ( state == FM_ALARM_STATE_MSG )
            {
                alarm.severity = FM_ALARM_SEVERITY_CLEAR ;

                snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                     "%s '%s' sensor group %s", hostname.c_str(), sub_entity.c_str(), reason.c_str());

                ilog ("%s %s:%s %s\n",
                          hostname.c_str(),
                          alarm.alarm_id,
                          alarm.entity_instance_id,
                          reason.c_str());
            }
            else
            {
                alarm.severity = FM_ALARM_SEVERITY_MAJOR ;
                alarm.service_affecting = FM_FALSE;

                snprintf(alarm.reason_text, FM_MAX_BUFFER_LENGTH,
                    "%s has one or more board management controller sensor group read failures",
                        hostname.c_str());

                wlog ("%s %s: %s 'major'\n", hostname.c_str(), alarm.alarm_id, sub_entity.c_str() );
            }
            break ;
        }

        default:
        {
            slog ("%s Unsupported Alarm (%d)\n", hostname.c_str(), id );
            return (FAIL_BAD_CASE) ;
        }
    }

    if ( alarm.alarm_state == FM_ALARM_STATE_CLEAR )
    {
        AlarmFilter filter ;
        memset(&filter, 0, sizeof(filter));

        /* Setup the alarm filter */
        snprintf (filter.alarm_id, FM_MAX_BUFFER_LENGTH, "%s", alarm.alarm_id);
        snprintf (filter.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s", alarm.entity_instance_id);

        nodeUtil_latency_log ( hostname, NODEUTIL_LATENCY_MON_START , 0 );
        if ( ( rc = fm_clear_fault ( &filter )) != FM_ERR_OK )
        {
            if ( rc != FM_ERR_ENTITY_NOT_FOUND )
            {
                elog ("%s failed to fm_clear_fault (rc:%d)\n", hostname.c_str(), rc );
                rc = FAIL ;
            }
        }
        nodeUtil_latency_log ( hostname, "fm_clear_fault - hwmon" , LATENCY_1SEC );
    }
    else if ( alarm.alarm_state == FM_ALARM_STATE_SET )
    {
        /* Debug Logs */
        alog ("%s Alarm Reason: %s\n", hostname.c_str(), alarm.reason_text );
        alog ("%s Alarm Action: %s\n", hostname.c_str(), alarm.proposed_repair_action );
        alog ("%s Alarm Ident : %s : %s\n", hostname.c_str(), alarm.entity_type_id, alarm.entity_instance_id );
        alog ("%s Alarm State : state:%d sev:%d type:%d cause:%d sa:%c supp:%c\n",
                  hostname.c_str(),
                  state,
                  alarm.severity,
                  alarm.alarm_type,
                  alarm.probable_cause,
                  alarm.service_affecting ? 'Y' : 'N',
                  alarm.suppression ? 'Y' : 'N' );
        nodeUtil_latency_log ( hostname, NODEUTIL_LATENCY_MON_START , 0 );
        rc = fm_set_fault ( &alarm , NULL );
        nodeUtil_latency_log ( hostname, "fm_set_fault - alarm - hwmon" , LATENCY_1SEC );

        if ( rc != FM_ERR_OK )
        {
            elog ("%s failed to set alarm %s  (rc:%d)\n", hostname.c_str(), alarm.alarm_id, rc);
            rc = FAIL ;
        }
    }
    else if ( alarm.alarm_state == FM_ALARM_STATE_MSG )
    {
        /* Debug Logs */
        alog ("%s Log Reason: %s\n", hostname.c_str(), alarm.reason_text );
        alog ("%s Log Action: %s\n", hostname.c_str(), alarm.proposed_repair_action );
        alog ("%s Log Ident : %s : %s\n", hostname.c_str(), alarm.entity_type_id, alarm.entity_instance_id );
        alog ("%s Log State : state:%d sev:%d type:%d cause:%d sa:%c supp:%c\n",
                  hostname.c_str(),
                  state,
                  alarm.severity,
                  alarm.alarm_type,
                  alarm.probable_cause,
                  alarm.service_affecting ? 'Y' : 'N',
                  alarm.suppression ? 'Y' : 'N' );

        nodeUtil_latency_log ( hostname, NODEUTIL_LATENCY_MON_START , 0 );
        rc = fm_set_fault ( &alarm , NULL );
        nodeUtil_latency_log ( hostname, "fm_set_fault - log - hwmon" , LATENCY_1SEC );

        if ( rc != FM_ERR_OK )
        {
            elog ("%s failed to create customer log %s  (rc:%d)\n", hostname.c_str(), alarm.alarm_id, rc);
            rc = FAIL ;
        }

    }
    else
    {
        slog ("%s internal error ; unsupported alarm state (%d)\n", hostname.c_str(), state );
        rc = FAIL_BAD_CASE ;
    }
    return (rc);
}

/* Clear the specified maintenance alarm for specific host */
int hwmonAlarm_clear ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return(hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_CLEAR, FM_ALARM_SEVERITY_CLEAR, sub_entity , reason ));
}

/* Assert a specified mtce alarm against the specified host with a CRITICAL severity level */
int hwmonAlarm_critical ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return(hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_CRITICAL, sub_entity, reason ));
}

/* Assert a specified mtce alarm against the specified host with a MAJOR severity level */
int hwmonAlarm_major ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return(hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_MAJOR, sub_entity, reason ));
}

/* Assert a specified mtce alarm against the specified host with a MINOR severity level */
int hwmonAlarm_minor ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason)
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_MINOR, sub_entity, reason ));
}

/* Assert a specified mtce alarm against the specified host with a WARNING severity level */
int hwmonAlarm_warning ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_WARNING, sub_entity, reason ));
}

/* generate a customer log for the specified severity */
int hwmonLog ( string & hostname, hwmonAlarm_id_type id, EFmAlarmSeverityT sev, string sub_entity, string reason )
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_MSG, sev, sub_entity, reason ));
}

/* generate a customer alarm that indicates the sensor on this host has recovered */
int hwmonLog_clear ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_CLEAR, sub_entity, reason ));
}

/* generate customer log against the specified host and sensor with a CRITICAL severity level */
int hwmonLog_critical ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_CRITICAL, sub_entity, reason ));
}

/* generate customer log against the specified host and sensor with a MAJOR severity level */
int hwmonLog_major ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_MAJOR, sub_entity , reason ));
}

/* generate customer log against the specified host and sensor with a MINOR severity level */
int hwmonLog_minor ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason)
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_MINOR, sub_entity, reason));
}

/* generate customer log against the specified host and sensor with a WARNING severity level */
int hwmonLog_warning ( string & hostname, hwmonAlarm_id_type id, string sub_entity, string reason )
{
    return ( hwmon_alarm_util ( hostname, id, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_WARNING, sub_entity, reason));
}
