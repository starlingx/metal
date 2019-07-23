/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform - Hardware Monitoring "General Utilities" Implementation
  */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "hwm"

#include "alarmUtil.h"       /* for  ... alarmUtil_getSev_str   */
#include "hwmonUtil.h"       /* this module header               */

string get_key_value_string ( string reading, string key, char delimiter, bool set_tolowercase )
{
    string value = "" ;
    if ( ! reading.empty() )
    {
        /* verify this is the correct sensor to be reading */
        std::size_t start = reading.find(key, 0) ;
        if ( start != std::string::npos )
        {
            start = reading.find( delimiter, start ) ;
            if ( start != std::string::npos )
            {
                std::size_t end = reading.find( '\n', ++start ) ;

                value = reading.substr(start, end-start).c_str();

                if ( set_tolowercase == true )
                    value = tolowercase ( value );

                blog3 ("key:%s - value:%s\n", key.c_str(), value.c_str());
            }
            else
            {
                elog ("value parse error\n");
            }
        }
        else
        {
            elog ("key parse error\n");
        }
    }
    else
    {
        elog ("empty key\n");
    }
    return (value);
}

string get_severity ( sensor_severity_enum severity )
{
    if ( severity == HWMON_SEVERITY_GOOD )
        return "ok" ;
    else if ( severity == HWMON_SEVERITY_OFFLINE )
        return "offline" ;
    else if ( severity == HWMON_SEVERITY_MINOR )
        return "minor" ;
    else if ( severity == HWMON_SEVERITY_MAJOR )
        return "major" ;
    else if ( severity == HWMON_SEVERITY_CRITICAL )
        return "critical" ;
    else if ( severity == HWMON_SEVERITY_NONRECOVERABLE )
        return "critical" ;
    else
        return "unknown" ;
}

sensor_severity_enum get_severity ( string status )
{
    if ( status.compare("ok") == 0 )
       return HWMON_SEVERITY_GOOD ;
    if ( status.compare("offline") == 0 )
        return HWMON_SEVERITY_OFFLINE ;
    if ( status.compare("minor") == 0 )
        return HWMON_SEVERITY_MINOR ;
    if ( status.compare("major") == 0 )
        return HWMON_SEVERITY_MAJOR ;
    if ( status.compare("critical") == 0 )
        return HWMON_SEVERITY_CRITICAL ;
    if ( status.compare("nonrecov") == 0 )
        return HWMON_SEVERITY_NONRECOVERABLE ;

    return HWMON_SEVERITY_OFFLINE ;
}

string get_bmc_severity ( sensor_severity_enum status )
{
    switch ( status )
    {
        case HWMON_SEVERITY_GOOD: return "ok" ;
        case HWMON_SEVERITY_MAJOR: return "nc" ;
        case HWMON_SEVERITY_CRITICAL: return "cr" ;
        case HWMON_SEVERITY_OFFLINE: return "na" ;
        default: return "ns" ;
    }
}

sensor_severity_enum get_bmc_severity ( string status )
{
    if ( status.compare("ok") == 0 )
       return HWMON_SEVERITY_GOOD ;
    if ( status.compare("nc") == 0 )
        return HWMON_SEVERITY_MAJOR ;
    if ( status.compare("cr") == 0 )
        return HWMON_SEVERITY_CRITICAL ;
    if ( status.compare("na") == 0 )
        return HWMON_SEVERITY_OFFLINE ;
    if ( status.compare("ns") == 0 )
        return HWMON_SEVERITY_OFFLINE ;

    /* Separate clauses because they are likelt infrequent if at all */
    if (( status.compare("nr")  == 0 ) ||
        ( status.compare("lnr") == 0 ) ||
        ( status.compare("unr") == 0 ))
    {
        return HWMON_SEVERITY_NONRECOVERABLE ;
    }

    if (( status.compare("lnc") == 0 ) ||
        ( status.compare("unc") == 0 ))
    {
        return HWMON_SEVERITY_MAJOR ;
    }

    if (( status.compare("lcr") == 0 ) ||
        ( status.compare("ucr") == 0 ))
    {
        return HWMON_SEVERITY_CRITICAL ;
    }

    /* Unrecognized status is handled as a minor alarm */
    return HWMON_SEVERITY_MINOR ;
}

bool is_valid_action ( sensor_severity_enum severity, string & action, bool set_to_lower )
{
    bool rc = false ;
    string lower_case_action = tolowercase ( action );

    if ( set_to_lower == true )
    {
        action = lower_case_action ;
    }

    if ( severity == HWMON_SEVERITY_CRITICAL )
    {
        if ( !lower_case_action.compare(HWMON_ACTION_IGNORE) ||
             !lower_case_action.compare(HWMON_ACTION_ALARM)  ||
             !lower_case_action.compare(HWMON_ACTION_LOG)    ||
             !lower_case_action.compare(HWMON_ACTION_RESET)  ||
             !lower_case_action.compare(HWMON_ACTION_POWERCYCLE))
        {
            rc = true ;
        }
    }
    else if ( !lower_case_action.compare(HWMON_ACTION_IGNORE) ||
              !lower_case_action.compare(HWMON_ACTION_ALARM)  ||
              !lower_case_action.compare(HWMON_ACTION_LOG))
    {
        rc = true ;
    }
    return (rc);
}

bool is_log_action ( string action )
{
    if ( !action.compare(HWMON_ACTION_LOG) )
        return true ;
    return false ;
}


bool is_ignore_action ( string action )
{
    if ( !action.compare(HWMON_ACTION_IGNORE) )
        return true ;
    return false ;
}

bool is_alarm_action ( string action )
{
    if ( !action.compare(HWMON_ACTION_ALARM) )
        return true ;
    return false ;
}

bool is_reset_action ( string action )
{
    if ( !action.compare(HWMON_ACTION_RESET) )
        return true ;
    return false ;
}

bool is_powercycle_action ( string action )
{
    if ( !action.compare(HWMON_ACTION_POWERCYCLE) )
        return true ;
    return false ;
}


void   clear_degraded_state   ( sensor_type * sensor_ptr )
{
    if ( sensor_ptr )
    {
        sensor_ptr->degraded = false ;
    }
}

void   set_degraded_state   ( sensor_type * sensor_ptr )
{
    if ( sensor_ptr )
    {
        // ilog ("%s %s DEGRADE ASSERT ^^^^^^^^^^\n", sensor_ptr->hostname.c_str(), sensor_ptr->sensorname.c_str());
        sensor_ptr->degraded = true ;
    }
}

bool clear_severity_alarm ( string & hostname, hwmonAlarm_id_type id, string & sub_entity, EFmAlarmSeverityT severity, string reason )
{
    if ( hwmon_alarm_query ( hostname, id, sub_entity ) == severity )
    {
        hwmonAlarm_clear ( hostname, id, sub_entity, reason );
        return (true);
    }
    return (false);
}

void clear_asserted_alarm ( string & hostname, hwmonAlarm_id_type id, sensor_type * ptr , string reason )
{
    if ( ptr )
    {
        if ( ptr->alarmed == true )
        {
            hwmonAlarm_clear ( hostname, id, ptr->sensorname, reason );
        }
        clear_logged_state  (ptr);
        clear_ignored_state (ptr);
        clear_alarmed_state (ptr);
        ptr->degraded = false ;
        ptr->alarmed  = false ;
    }
    else
    {
        wlog ("%s null sensor pointer\n", hostname.c_str() );
    }
}


void   clear_alarmed_state  ( sensor_type * sensor_ptr )
{
    if ( sensor_ptr )
    {
        sensor_ptr->minor.alarmed = false ;
        sensor_ptr->major.alarmed = false ;
        sensor_ptr->critl.alarmed = false ;
        sensor_ptr->alarmed = false ;
    }
}

void set_alarmed_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity )
{
    if ( sensor_ptr )
    {
        if ( severity == FM_ALARM_SEVERITY_MINOR )
        {
            sensor_ptr->alarmed = true ;
            sensor_ptr->minor.alarmed = true ;
        }
        else if ( severity == FM_ALARM_SEVERITY_MAJOR )
        {
            sensor_ptr->alarmed = true ;
            sensor_ptr->major.alarmed = true ;
        }
        else if ( severity == FM_ALARM_SEVERITY_CRITICAL )
        {
            sensor_ptr->alarmed = true ;
            sensor_ptr->critl.alarmed = true ;
        }
        else
        {
            slog ("%s alarm status does not apply for severity '%s'\n",
                      sensor_ptr->hostname.c_str(),
                      alarmUtil_getSev_str(severity).c_str());
        }
    }
    else
    {
        slog ("null sensor pointer\n");
    }
}

/*********************  Log Utilities *****************************/

void   clear_logged_state   ( sensor_type * sensor_ptr )
{
    if ( sensor_ptr )
    {
        sensor_ptr->minor.logged = false ;
        sensor_ptr->major.logged = false ;
        sensor_ptr->critl.logged = false ;
    }
}


void set_logged_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity )
{
    if ( sensor_ptr )
    {
        if ( severity == FM_ALARM_SEVERITY_MINOR )
        {
            sensor_ptr->minor.logged = true ;
        }
        else if ( severity == FM_ALARM_SEVERITY_MAJOR )
        {
            sensor_ptr->major.logged = true ;
        }
        else if ( severity == FM_ALARM_SEVERITY_CRITICAL )
        {
            sensor_ptr->critl.logged = true ;
        }
        else
        {
            slog ("%s %s logged status does not apply for severity '%s'\n",
                      sensor_ptr->hostname.c_str(),
                      sensor_ptr->sensorname.c_str(),
                      alarmUtil_getSev_str(severity).c_str());
        }
    }
    else
    {
        slog ("null sensor pointer\n");
    }
}

void clear_logged_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity )
{
    if ( sensor_ptr )
    {
        if ( severity == FM_ALARM_SEVERITY_MINOR )
        {
            sensor_ptr->minor.logged = false ;
        }
        else if ( severity == FM_ALARM_SEVERITY_MAJOR )
        {
            sensor_ptr->major.logged = false ;
        }
        else if ( severity == FM_ALARM_SEVERITY_CRITICAL )
        {
            sensor_ptr->critl.logged = false ;
        }
        else
        {
            slog ("%s logged status does not apply for severity '%s'\n",
                      sensor_ptr->hostname.c_str(),
                      alarmUtil_getSev_str(severity).c_str());
        }
    }
    else
    {
        slog ("null sensor pointer\n");
    }
}

/*********************  Ignore Utilities *************************/
void   clear_ignored_state  ( sensor_type * sensor_ptr )
{
    if ( sensor_ptr )
    {
        sensor_ptr->minor.ignored = false ;
        sensor_ptr->major.ignored = false ;
        sensor_ptr->critl.ignored = false ;
    }
}

void set_ignored_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity )
{
    if ( sensor_ptr )
    {
        clear_ignored_state ( sensor_ptr );
        if ( severity == FM_ALARM_SEVERITY_MINOR )
        {
            sensor_ptr->minor.ignored = true ;
        }
        else if ( severity == FM_ALARM_SEVERITY_MAJOR )
        {
            sensor_ptr->major.ignored = true ;
        }
        else if ( severity == FM_ALARM_SEVERITY_CRITICAL )
        {
            sensor_ptr->critl.ignored = true ;
        }
        else
        {
            slog ("%s logged status does not apply for severity '%s'\n",
                      sensor_ptr->hostname.c_str(),
                      alarmUtil_getSev_str(severity).c_str());
        }
    }
    else
    {
        slog ("null sensor pointer\n");
    }
}

void clear_ignored_severity (  sensor_type * sensor_ptr , EFmAlarmSeverityT severity )
{
    if ( sensor_ptr )
    {
        if ( severity == FM_ALARM_SEVERITY_MINOR )
        {
            sensor_ptr->minor.ignored = false ;
        }
        else if ( severity == FM_ALARM_SEVERITY_MAJOR )
        {
            sensor_ptr->major.ignored = false ;
        }
        else if ( severity == FM_ALARM_SEVERITY_CRITICAL )
        {
            sensor_ptr->critl.ignored = false ;
        }
        else
        {
            slog ("%s ignored status does not apply for severity '%s'\n",
                      sensor_ptr->hostname.c_str(),
                      alarmUtil_getSev_str(severity).c_str());
        }
    }
    else
    {
        slog ("null sensor pointer\n");
    }
}


string print_alarmed_severity ( sensor_type * sensor_ptr )
{
    string alarmed_severity = "";
    if ( sensor_ptr->critl.alarmed || sensor_ptr->major.alarmed || sensor_ptr->minor.alarmed )
    {
        alarmed_severity.append(" [");
        if ( sensor_ptr->critl.alarmed )
            alarmed_severity.append("alarmd-critl");
        if ( sensor_ptr->major.alarmed )
            alarmed_severity.append("alarmd-major");
        if ( sensor_ptr->minor.alarmed )
            alarmed_severity.append("alarmd-minor");
        alarmed_severity.append("]");
    }
    return(alarmed_severity);
}

string print_ignored_severity ( sensor_type * sensor_ptr )
{
    string ignored_severity = "";
    if ( sensor_ptr->critl.ignored || sensor_ptr->major.ignored || sensor_ptr->minor.ignored )
    {
        ignored_severity.append(" [");
        if ( sensor_ptr->critl.ignored )
            ignored_severity.append("ignore-critl");
        if ( sensor_ptr->major.ignored )
            ignored_severity.append("ignore-major");
        if ( sensor_ptr->minor.ignored )
            ignored_severity.append("ignore-minor");
        ignored_severity.append("]");
    }
    return(ignored_severity);
}

string print_logged_severity  ( sensor_type * sensor_ptr )
{
    string logged_severity = "";
    if ( sensor_ptr->critl.logged || sensor_ptr->major.logged || sensor_ptr->minor.logged )
    {
        logged_severity.append(" [");
        if ( sensor_ptr->critl.logged )
            logged_severity.append("logged-critl");
        if ( sensor_ptr->major.logged )
            logged_severity.append("logged-major");
        if ( sensor_ptr->minor.logged )
            logged_severity.append("logged-minor");
        logged_severity.append("]");
    }
    return(logged_severity);
}


void sensorState_print ( string & hostname, sensor_type * sensor_ptr )
{
    if ( sensor_ptr->status.compare("ok") ||
         sensor_ptr->degraded      ||
         sensor_ptr->alarmed       ||
         sensor_ptr->suppress      ||
         sensor_ptr->critl.alarmed ||
         sensor_ptr->major.alarmed ||
         sensor_ptr->minor.alarmed ||
         sensor_ptr->critl.ignored ||
         sensor_ptr->major.ignored ||
         sensor_ptr->minor.ignored ||
         sensor_ptr->critl.logged  ||
         sensor_ptr->major.logged  ||
         sensor_ptr->minor.logged  )
    {
         wlog ("%s %-20s %-8s %s%s%s%s%s%s\n",
                   hostname.c_str(),
                   sensor_ptr->sensorname.c_str(),
                   sensor_ptr->status.c_str(),
                   print_alarmed_severity ( sensor_ptr ).c_str(),
                   print_logged_severity  ( sensor_ptr ).c_str(),
                   print_ignored_severity ( sensor_ptr ).c_str(),
                   sensor_ptr->alarmed ? " alarmed"   : "",
                   sensor_ptr->degraded ? " degraded" : "",
                   sensor_ptr->suppress ? " suppressed" : "");
    }
}


bool is_alarmed_state ( sensor_type * sensor_ptr , sensor_severity_enum & hwmon_sev )
{
    int count = 0 ;
    if ( sensor_ptr )
    {
        if ( sensor_ptr->minor.alarmed == true )
        {
            hwmon_sev = HWMON_SEVERITY_MINOR ;
            count++ ;
        }
        if ( sensor_ptr->major.alarmed == true )
        {
            hwmon_sev = HWMON_SEVERITY_MAJOR ;
            count++ ;
        }
        if ( sensor_ptr->critl.alarmed == true )
        {
            hwmon_sev = HWMON_SEVERITY_CRITICAL ;
            count++ ;
        }

        if ( count > 1 )
        {
            slog ("%s '%s' alarm state tracking mismatch [alarm:%s:%s:%s]\n",
                      sensor_ptr->hostname.c_str(),
                      sensor_ptr->sensorname.c_str(),
                      sensor_ptr->minor.alarmed ? "Yes":"No",
                      sensor_ptr->major.alarmed ? "Yes":"No",
                      sensor_ptr->critl.alarmed ? "Yes":"No");
        }
    }
    return (count);
}

bool is_alarmed ( sensor_type * sensor_ptr )
{
    if ( sensor_ptr )
    {
        if (( sensor_ptr->alarmed == true ) ||
            ( sensor_ptr->minor.alarmed == true ) ||
            ( sensor_ptr->major.alarmed == true ) ||
            ( sensor_ptr->critl.alarmed == true ))
        {
            return (true );
        }
    }
    return (false);
}

/*****************************************************************************
 *
 *
 * Name       : checksum_sample_profile
 *
 * Description: Append all the sensor names in a 'sensor_data_type' sensor list
 *              into a long string and checksum that string.
 *
 * Purponse   : The checksum provides a unique signature for a specific sensor
 *              profile. Used to uniquely identify the Quanta sensor model or
 *              a change of a sensor model.
 *
 ******************************************************************************/

unsigned short checksum_sample_profile ( const string           & hostname,
                                               int                sensors,
                                               sensor_data_type * sensor_ptr)
{
    unsigned short sum = 0 ;
    if ( sensors )
    {
        string temp = "" ;
        for ( int i = 0 ; i < sensors ; i++ , sensor_ptr++ )
        {
            temp.append(sensor_ptr->name);
        }
        sum += checksum ( (void*)temp.data(), temp.length());

        blog2 ("%s sensor sample checksum 0x%04x\n", hostname.c_str(), sum);
    }
    return (sum);
}

/*****************************************************************************
 *
 *
 * Name       : checksum_sensor_profile
 *
 * Description: Append all the sensor names in a 'sensor_type' sensor list into
 *              a long string and checksum that string.
 *
 * Purponse   : The checksum provides a unique signature for a specific sensor
 *              profile. Used to uniquely identify the Quanta sensor model or
 *              a change of a sensor model.
 *
 ******************************************************************************/

unsigned short checksum_sensor_profile ( const string      & hostname,
                                               int           sensors,
                                               sensor_type * sensor_ptr)
{
    unsigned short sum = 0 ;
    if ( sensors )
    {
        string temp = "" ;
        for ( int i = 0 ; i < sensors ; i++ , sensor_ptr++ )
        {
            temp.append(sensor_ptr->sensorname);
        }
        sum += checksum ( (void*)temp.data(), temp.length());

        blog2 ("%s sensor profile checksum 0x%04x\n", hostname.c_str(), sum );
    }
    return (sum);
}



/* load the specified key value in buffer line into 'value' */
bool got_delimited_value ( char * buf_ptr,
                           const char * key,
                           const char * delimiter,
                           string & value )
{
    if ( strstr ( buf_ptr, key ))
    {
        string _str = buf_ptr ;
        if ( _str.find(key) != std::string::npos )
        {
            if ( _str.find( delimiter ) != std::string::npos )
            {
                int y = _str.find( delimiter ) ;
                value = _str.substr ( y+strlen(delimiter), std::string::npos) ;
                value.erase ( value.size()-1, std::string::npos ) ;
                return (true);
            }
        }
    }
    return (false);
}

/***************************************************************************
 *
 * Name       : get_bmc_version_string
 *
 * Description: Return the bmc version string from the specified filename.
 *
 * Looking for 'Firmware Revision' label fromspecified file
 *
 *     Firmware Revision         : 3.29
 *
 **************************************************************************/

#define BUFFER (80)

#define BMC_INFO_LABEL_FW_VERSION       ((const char *)("Firmware Revision"))
#define BMC_INFO_LABEL_DELIMITER        ((const char *)(": "))
string get_bmc_version_string ( string hostname,
                                const char * filename )
{
    string bmc_fw_version = "" ;
    if ( daemon_is_file_present ( filename ) )
    {
        FILE * _stream = fopen ( filename, "r" );
        if ( _stream )
        {
            char buffer [BUFFER];
            MEMSET_ZERO(buffer);
            while ( fgets (buffer, BUFFER, _stream) )
            {
                if ( got_delimited_value ( buffer, BMC_INFO_LABEL_FW_VERSION,
                                                   BMC_INFO_LABEL_DELIMITER,
                                                   bmc_fw_version  ))
                {
                    break ;
                }
                MEMSET_ZERO(buffer);
            }
            fclose(_stream);
        }
    }
    else
    {
        elog ("%s failed to open mc info file '%s'\n", hostname.c_str(),
                                                       filename);
    }

    return (bmc_fw_version);
}
