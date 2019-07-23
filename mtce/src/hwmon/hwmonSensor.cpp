/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Wind River Titanium Cloud Hardware Monitor "Sensor" Utilities
 */

#include "daemon_ini.h"   /* for ... parse_ini and MATCH              */
#include "nodeBase.h"     /* for ... mtce common definitions          */
#include "jsonUtil.h"     /* for ... json utilitiies                  */
#include "nodeUtil.h"     /* for ... mtce common utilities            */
#include "hwmonUtil.h"    /* for ... get_severity                     */
#include "hwmonClass.h"   /* for ... service class definition         */
#include "hwmonHttp.h"    /* for ... hwmonHttp_load_sensors           */
#include "hwmonSensor.h"  /* for ... this module header               */
#include "hwmonGroup.h"   /* for ... bmc_get_grouptype               */
#include "hwmonAlarm.h"   /* for ... hwmonAlarm                       */

#define DELIMITER ((const char)',')

/****************************************************************************
 *
 * Name   : hwmonSensor_print
 *
 * Purpose: Print the contents of the pointed to sensor
 *
 *****************************************************************************/
void hwmonSensor_print ( string & hostname, sensor_type * sensor_ptr )
{
    const char bar [] = {"+--------------------------------------------------\n" } ;
    if ( sensor_ptr )
    {
        syslog ( LOG_INFO, "%s", bar);
        syslog ( LOG_INFO, "| sensor info  : %s '%s' sensor\n", sensor_ptr->hostname.c_str(), sensor_ptr->sensorname.c_str());
        syslog ( LOG_INFO, "| uuid         : %s\n", sensor_ptr->uuid.c_str());
        syslog ( LOG_INFO, "| group uuid   : %s\n", sensor_ptr->group_uuid.c_str());
        syslog ( LOG_INFO, "| sensortype   : %s\n", sensor_ptr->sensortype.c_str());
        syslog ( LOG_INFO, "| datatype     : %s\n", sensor_ptr->datatype.c_str());
        syslog ( LOG_INFO, "| minor        : %s\n", sensor_ptr->actions_minor.c_str());
        syslog ( LOG_INFO, "| major        : %s\n", sensor_ptr->actions_major.c_str());
        syslog ( LOG_INFO, "| critical     : %s\n", sensor_ptr->actions_critl.c_str());
        syslog ( LOG_INFO, "| state:status : %s-%s\n", sensor_ptr->state.c_str(), sensor_ptr->status.c_str());
        syslog ( LOG_INFO, "| command      : %s\n", sensor_ptr->path.c_str());
        syslog ( LOG_INFO, "| algorithm    : %s\n", sensor_ptr->algorithm.c_str());
        syslog ( LOG_INFO, "| suppress     : %s\n", sensor_ptr->suppress ? "True" : "False" );
        if ( !sensor_ptr->datatype.compare("analog") )
        {
            // syslog ( LOG_INFO, "%s",bar);
            syslog ( LOG_INFO, "|    minor thld: %5.3f <-> %5.3f \n", sensor_ptr->t_minor_lower, sensor_ptr->t_minor_upper );
            syslog ( LOG_INFO, "|    major thld: %5.3f <-> %5.3f \n", sensor_ptr->t_major_lower, sensor_ptr->t_major_upper );
            syslog ( LOG_INFO, "| critical thld: %5.3f <-> %5.3f \n", sensor_ptr->t_critical_lower, sensor_ptr->t_critical_upper );
            syslog ( LOG_INFO, "| unit info    : [base:%s] [rate:%s] [modifier:%s]\n",
                       sensor_ptr->unit_base.c_str(),
                       sensor_ptr->unit_rate.c_str(),
                       sensor_ptr->unit_modifier.c_str());
        }
    }
    else
    {
        slog ("%s cannot print a NULL sensor\n", hostname.c_str() );
    }
}


/****************************************************************************
 *
 * Name   : hwmonGroup_print
 *
 * Purpose: Print the contents of the pointed to sensor group
 *
 *****************************************************************************/
void hwmonGroup_print  ( string & hostname, struct sensor_group_type * group_ptr )
{
    const char bar [] = {"+--------------------------------------------------------\n" } ;
    if ( group_ptr )
    {
        syslog ( LOG_INFO, "%s", bar);
        syslog ( LOG_INFO, "| group info            : %s '%s' group\n", hostname.c_str(), group_ptr->group_name.c_str());
        syslog ( LOG_INFO, "| group uuid            : %s\n", group_ptr->group_uuid.c_str());
        syslog ( LOG_INFO, "| sensortype            : %s\n", group_ptr->sensortype.c_str());
        syslog ( LOG_INFO, "| datatype              : %s\n", group_ptr->datatype.c_str());
        syslog ( LOG_INFO, "| group minor choices   : %s\n", group_ptr->actions_minor_choices.c_str());
        syslog ( LOG_INFO, "| group minor actions   : %s\n", group_ptr->actions_minor_group.c_str());
        syslog ( LOG_INFO, "| group major choices   : %s\n", group_ptr->actions_major_choices.c_str());
        syslog ( LOG_INFO, "| group major actions   : %s\n", group_ptr->actions_major_group.c_str());
        syslog ( LOG_INFO, "| group critical choices: %s\n", group_ptr->actions_critical_choices.c_str());
        syslog ( LOG_INFO, "| group critical actions: %s\n", group_ptr->actions_critl_group.c_str());
        syslog ( LOG_INFO, "| group state           : %s\n", group_ptr->group_state.c_str());
        syslog ( LOG_INFO, "| algorithm             : %s\n", group_ptr->algorithm.c_str());
        syslog ( LOG_INFO, "| group audit period    : %d secs\n", group_ptr->group_interval );
        syslog ( LOG_INFO, "| group suppress        : %s\n", group_ptr->suppress ? "True" : "False" );
        syslog ( LOG_INFO, "| group sensor read cmd : %s\n", group_ptr->path.c_str());
        syslog ( LOG_INFO, "| group sensors (count) : %d\n", group_ptr->sensors);
        if ( !group_ptr->sensor_labels.empty() )
        {
            syslog ( LOG_INFO, "| group sensor labels   : %s\n", group_ptr->sensor_labels.c_str());
        }
        if ( group_ptr->sensors )
        {
            for ( int s = 0 ; s < group_ptr->sensors ; ++s )
            {
                if ( group_ptr->sensor_ptr[s] != NULL )
                {
                    syslog ( LOG_INFO, "| group sensor       %02d : %s\n", s, group_ptr->sensor_ptr[s]->sensorname.c_str());
                }
            }
        }
        if ( !group_ptr->datatype.compare("analog") )
        {
            // syslog ( LOG_INFO, "%s",bar);
            syslog ( LOG_INFO, "|    minor thld: %5.3f <-> %5.3f \n",
                       group_ptr->t_minor_lower_group,
                       group_ptr->t_minor_upper_group );
            syslog ( LOG_INFO, "|    major thld: %5.3f <-> %5.3f \n",
                       group_ptr->t_major_lower_group,
                       group_ptr->t_major_upper_group );
            syslog ( LOG_INFO, "| critical thld: %5.3f <-> %5.3f \n",
                       group_ptr->t_critical_lower_group,
                       group_ptr->t_critical_upper_group );

            syslog ( LOG_INFO, "| unit info    : [base:%s] [rate:%s] [modifier:%s]\n",
                       group_ptr->unit_base_group.c_str(),
                       group_ptr->unit_rate_group.c_str(),
                       group_ptr->unit_modifier_group.c_str());
        }
    }
    else
    {
        slog ("%s cannot print a NULL group\n", hostname.c_str());
    }
}

/****************************************************************************
 *
 * Name   : hwmonSensor_init
 *
 * Purpose: Initialize a sensor_type struct to default values
 *
 *****************************************************************************/
void hwmonSensor_init ( string & hostname , sensor_type * sensor_ptr )
{
    if ( sensor_ptr )
    {
        sensor_ptr->hostname = hostname ;
        sensor_ptr->bmc.clear();
        sensor_ptr->uuid.clear();
        sensor_ptr->host_uuid.clear();
        sensor_ptr->group_uuid.clear();
        sensor_ptr->sensorname.clear();
        sensor_ptr->sensortype.clear();
        sensor_ptr->datatype = DISCRETE ; /* should really be ANALOG */
        sensor_ptr->suppress = false ;

        sensor_ptr->debounce_count = 0 ;
        sensor_ptr->want_debounce_log_if_ok = false ;
        sensor_ptr->algorithm = "debounce-1.v1" ;

        sensor_ptr->status = "offline" ;
        sensor_ptr->state  = "disabled" ;

        sensor_ptr->script.clear();
        sensor_ptr->path.clear() ;
        sensor_ptr->entity_path.clear();

        sensor_ptr->unit_base.clear();
        sensor_ptr->unit_rate.clear();
        sensor_ptr->unit_modifier.clear();

        sensor_ptr->prot     = BMC_PROTOCOL__IPMITOOL ;
        sensor_ptr->kind     = SENSOR_KIND__NONE ;
        sensor_ptr->unit     = SENSOR_UNIT__NONE ;

        sensor_ptr->actions_minor = HWMON_ACTION_IGNORE ;
        sensor_ptr->actions_major = HWMON_ACTION_LOG    ;
        sensor_ptr->actions_critl = HWMON_ACTION_ALARM  ;

        sensor_ptr->t_minor_lower =  1.000 ;
        sensor_ptr->t_major_lower =  5.000 ;
        sensor_ptr->t_critical_lower = 10.000 ;
        sensor_ptr->t_minor_upper =  1.000 ;
        sensor_ptr->t_major_upper =  5.000 ;
        sensor_ptr->t_critical_upper = 10.000 ;

        /* PATCHBACK - should patchback to REL3 and earlier */
        sensor_ptr->severity =
        sensor_ptr->sample_severity = HWMON_SEVERITY_GOOD ;

        sensor_ptr->sample_status =
        sensor_ptr->sample_status_last = "ok" ;

        sensor_ptr->updated  = false ;
        sensor_ptr->found    = false ;
        sensor_ptr->degraded = false ;
        sensor_ptr->alarmed  = false ;

        sensor_ptr->not_found_log_throttle = 0 ;
        sensor_ptr->not_updated_status_change_count = 0 ;

        clear_logged_state  ( sensor_ptr );
        clear_ignored_state ( sensor_ptr );
        clear_alarmed_state ( sensor_ptr );

    }
    else
    {
        slog ("%s cannot init a NULL sensor\n", hostname.c_str());
    }
}

/****************************************************************************
 *
 * Name   : hwmonGroup_init
 *
 * Purpose: Initialize a sensor_group_type struct to default values
 *
 *****************************************************************************/
void hwmonGroup_init ( string & hostname , struct sensor_group_type * group_ptr )
{
    if ( group_ptr )
    {
        hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr();

        group_ptr->hostname = hostname ;
        group_ptr->group_name.clear();
        group_ptr->host_uuid.clear();
        group_ptr->group_uuid.clear();
        group_ptr->sensortype.clear();
        group_ptr->datatype = DISCRETE ; /* should really be ANALOG */

        group_ptr->status.clear();
        group_ptr->unit_base_group.clear();
        group_ptr->unit_rate_group.clear();
        group_ptr->unit_modifier_group.clear();
        group_ptr->path = "na" ;
        group_ptr->sensor_labels.clear();
        group_ptr->sensor_read_index = 0 ;
        group_ptr->group_interval = HWMON_DEFAULT_AUDIT_INTERVAL ;

        /* Number of sensors in this group followed by an
         * array of those sensor pointers
         * All this is initied here */
        group_ptr->sensors = 0 ;
        for ( int i = 0 ; i < MAX_HOST_SENSORS ; i++ )
            group_ptr->sensor_ptr[i] = NULL ;

        group_ptr->actions_critical_choices = HWMON_ACTION_IGNORE ;
        group_ptr->actions_critical_choices.append(",");
        group_ptr->actions_critical_choices.append(HWMON_ACTION_LOG);
        group_ptr->actions_critical_choices.append(",");
        group_ptr->actions_critical_choices.append(HWMON_ACTION_ALARM);

        /* Don't support reset and power cycle in AIO simplex mode */
        if ( obj_ptr->system_type != SYSTEM_TYPE__CPE_MODE__SIMPLEX )
        {
            group_ptr->actions_critical_choices.append(",");
            group_ptr->actions_critical_choices.append(HWMON_ACTION_RESET);
            group_ptr->actions_critical_choices.append(",");
            group_ptr->actions_critical_choices.append(HWMON_ACTION_POWERCYCLE);
        }

        group_ptr->actions_major_choices = HWMON_ACTION_IGNORE ;
        group_ptr->actions_major_choices.append(",");
        group_ptr->actions_major_choices.append(HWMON_ACTION_LOG);
        group_ptr->actions_major_choices.append(",");
        group_ptr->actions_major_choices.append(HWMON_ACTION_ALARM);

        group_ptr->actions_minor_choices = HWMON_ACTION_IGNORE ;
        group_ptr->actions_minor_choices.append(",");
        group_ptr->actions_minor_choices.append(HWMON_ACTION_LOG);
        group_ptr->actions_minor_choices.append(",");
        group_ptr->actions_minor_choices.append(HWMON_ACTION_ALARM);

        group_ptr->suppress    = false  ;
        group_ptr->algorithm   = "debounce-1.v1" ;
        group_ptr->group_state = "disabled" ;

        group_ptr->active   = false ;
        group_ptr->timeout  = false ;
        group_ptr->failed   = false ;
        group_ptr->alarmed  = false ;

        group_ptr->actions_minor_group = HWMON_ACTION_IGNORE ;
        group_ptr->actions_major_group = HWMON_ACTION_LOG    ;
        group_ptr->actions_critl_group = HWMON_ACTION_ALARM  ;

        group_ptr->t_minor_lower_group    =  1.000 ;
        group_ptr->t_major_lower_group    =  5.000 ;
        group_ptr->t_critical_lower_group = 10.000 ;
        group_ptr->t_minor_upper_group    =  1.000 ;
        group_ptr->t_major_upper_group    =  5.000 ;
        group_ptr->t_critical_upper_group = 10.000 ;
    }
    else
    {
        slog ("%s cannot print a NULL group\n", hostname.c_str());
    }
}

/* *************************************************************************
 *
 * Name       : load_profile_groups
 *
 * Description: Load all the sensor groups from the profile file.
 *
 * Scope      : private hwmonHostClass
 *
 * Parameters : host_ptr , a pointer to a group sensor array with
 *              up to MAX_HOST_GROUPS elements
 *
 * Returns    : The number of groups that were loaded
 *
 * *************************************************************************/
int hwmonHostClass::load_profile_groups ( struct hwmonHostClass::hwmon_host * host_ptr,
                                          struct sensor_group_type * group_array_ptr,
                                          int max , bool & error )
{
    int groups_found = 0 ;

    error = false ;

    if ( ( max <= MAX_HOST_GROUPS ) && ( group_array_ptr ) )
    {
        int rc ;

        string sensor_group_types = "" ;
        std::list<string>  sensor_group_types_list ;
        std::list<string>::iterator types_iter_ptr ;

        /* get the top level group types from the profile file
         * in the SERVER:group_types heading , i.e. TEMPERATURE */
        rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                    "SERVER", "group_types", sensor_group_types , false );
        if ( rc ) /* handle error case */
        {
            elog ("%s failed to find '[SERVER] -> 'group_types' label in %s file\n",
                      host_ptr->hostname.c_str(), QUANTA_SENSOR_PROFILE_FILE);
            return rc ;
        }

        /* Start with a fresh group type list and load the detected group_types into it */
        sensor_group_types_list.clear();
        rc = get_delimited_list ( sensor_group_types, DELIMITER , sensor_group_types_list, true ) ;

        /* Note: badly parsed group_types , sensors are skipped over with error messages logged
         *
         * TODO: keep track of an error and raise the SENSORCFG Alarm
         *
         **/
        /* loop over each 'group_type' looking for the 'groups' */
        for ( types_iter_ptr  = sensor_group_types_list.begin();
              types_iter_ptr != sensor_group_types_list.end() ;
            ++types_iter_ptr )
        {
            string sensor_groups = "" ;
            std::list<string> sensor_groups_list ;

            daemon_signal_hdlr ();

            dlog ("%s [%s]\n", host_ptr->hostname.c_str(), types_iter_ptr->c_str());

            /* get the 'groups' within this 'group type'
             * in the [[SERVER]:group_type] -> 'groups' heading, i.e. TEMPERATURE1 */
            rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                        *types_iter_ptr,
                                        "groups",
                                        sensor_groups, false );
            if ( rc )
            {
                elog ("%s '%s' group type parse error ... skipping (%d)\n",
                          host_ptr->hostname.c_str(), types_iter_ptr->c_str(), rc );
                error = true ;
                continue ;
            }

            /* get the list of groups from the [<GROUP_TYPE>]:groups label */
            sensor_groups_list.clear();
            rc = get_delimited_list ( sensor_groups, DELIMITER , sensor_groups_list, true ) ;
            if ( rc )
            {
                elog ("%s '%s' failed to get group type list ... skipping (%d)\n",
                          host_ptr->hostname.c_str(), types_iter_ptr->c_str(), rc);

                elog ("%s '%s' error string: %s\n", host_ptr->hostname.c_str(),
                          types_iter_ptr->c_str(), sensor_groups.c_str());

                error = true ;
                continue ;
            }

            /******************************************************************
             * Look for the 'groups'
             *****************************************************************/
            std::list<string>::iterator groups_iter_ptr ;

            dlog2 ("%s groups list: %s\n", host_ptr->hostname.c_str(),
                                              sensor_groups.c_str());

            for ( groups_iter_ptr  = sensor_groups_list.begin();
                  groups_iter_ptr != sensor_groups_list.end() ;
                ++groups_iter_ptr )
            {
                /* Start from a fresh default */
                hwmonGroup_init ( host_ptr->hostname, group_array_ptr );

                daemon_signal_hdlr ();

                /* Get the group name for each group */
                group_array_ptr->group_name.clear() ;
                rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                            *groups_iter_ptr, "group",
                                            group_array_ptr->group_name, false );
                if ( rc  )
                {
                    elog ("%s '%s' group parse error ... skipping (%d)\n",
                              host_ptr->hostname.c_str(),
                              groups_iter_ptr->c_str(), rc );

                    error = true ;
                    continue ;
                }

                /*************************************************************************
                 * Read the sensor group attributes
                 *************************************************************************/

                /* sensortype */
                rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                            *groups_iter_ptr, "sensortype",
                                             group_array_ptr->sensortype, false );
                if ( rc )
                {
                    elog ("%s 'sensortype' for '%s' sensor group is unknown\n",
                              host_ptr->hostname.c_str(),
                              group_array_ptr->group_name.c_str());
                    error = true ;
                    continue ;
                }

                /* datatype */
                rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                           *groups_iter_ptr, "datatype",
                                            group_array_ptr->datatype, false );
                if ( rc )
                {
                    elog ("%s 'datatype' for '%s' sensor group is unknown\n",
                               host_ptr->hostname.c_str(),
                               group_array_ptr->group_name.c_str());
                    error = true ;
                    continue ;
                }

                /* interval */
                string interval ;
                rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                           *groups_iter_ptr, "interval",
                                            interval , false );
                if ( rc )
                {
                    elog ("%s 'interval' for '%s' sensor group is unknown\n",
                               host_ptr->hostname.c_str(),
                               group_array_ptr->group_name.c_str());
                    error = true ;
                    continue ;
                }
                group_array_ptr->group_interval = atoi(interval.data()) ; /* seconds */

                /* group read command (cmd)
                 * This may be over ridden by a sensor specific command
                 * TODO: add support for multiple commands
                 **/
                rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                           *groups_iter_ptr, "cmd",
                                            group_array_ptr->path, false );
                if ( rc )
                {
                    elog ("%s 'sensor read command' for '%s' sensor group is unknown ; skipping group\n",
                               host_ptr->hostname.c_str(),
                               group_array_ptr->group_name.c_str());
                    error = true ;
                    continue ;
                }

               /**************************************************************************
                * Get the list of sensors for each group from the profile file
                *
                * SERVER:group_types
                *    <group_type>:groups
                *        <group>:sensors   <-------
                **/
                rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                            *groups_iter_ptr, "sensors",
                                            group_array_ptr->sensor_labels, false );
                if ( rc )
                {
                    elog ("%s failed to read 'sensor' list for '%s' sensor group\n",
                               host_ptr->hostname.c_str(),
                               group_array_ptr->group_name.c_str());
                    error = true ;
                    continue ;
                }

                ilog ("%s  '%s' group loaded from profile (%d) (cmd:%s)\n",
                          host_ptr->hostname.c_str(),
                          group_array_ptr->group_name.c_str(),
                          groups_found, group_array_ptr->path.c_str());

                group_array_ptr++ ;
                groups_found++ ;
            }

        } /* types_iter_ptr for loop end */
    }

    return (groups_found);
}

/* *************************************************************************
 *
 * Name       : load_profile_sensors
 *
 * Description: Load all the sensors for each group.
 *
 * Scope      : private hwmonHostClass
 *
 * Parameters : host_ptr , a pointer to a sensor array with
 *              up to MAX_HOST_SENSORS elements
 *
 * Returns    : The number of sensors that were loaded
 *
 * *************************************************************************/
int hwmonHostClass::load_profile_sensors ( struct hwmonHostClass::hwmon_host * host_ptr,
                                           sensor_type * sensor_array_ptr, int max,
                                           bool & error )
{
    int sensors_found = 0 ;

    if ( ( max <= MAX_HOST_SENSORS ) && ( sensor_array_ptr ) )
    {
        std::list<string>    sensor_sensors_list ;
        std::list<string>::iterator sensors_iter_ptr ;

        string sensor = "" ;
        string sensor_read_cmd = "" ;

        for ( int g = 0 ; g < host_ptr->groups ; ++g )
        {
            sensor_sensors_list.clear();
            int rc = get_delimited_list ( host_ptr->group[g].sensor_labels, DELIMITER , sensor_sensors_list, false );
            if ( rc )
            {
                 elog ("%s '%s' failed to get sensor list ... skipping\n",
                           host_ptr->hostname.c_str(),
                           host_ptr->group[g].group_name.c_str());
                 elog ("%s error string: %s\n",
                           host_ptr->hostname.c_str(),
                           host_ptr->group[g].sensor_labels.c_str());
                 error = true ;
                 continue ;
            }

            blog ("%s '%s' has %ld sensors (%s)\n",
                           host_ptr->hostname.c_str(),
                           host_ptr->group[g].group_name.c_str(),
                           sensor_sensors_list.size(),
                           host_ptr->group[g].sensor_labels.c_str());

            for ( sensors_iter_ptr  = sensor_sensors_list.begin();
                  sensors_iter_ptr != sensor_sensors_list.end() ;
                ++sensors_iter_ptr )
            {
                 sensor.clear();

                 dlog1 ("%s  '%s' sensor group label: %s\n", host_ptr->hostname.c_str(),
                                                             host_ptr->group[g].group_name.c_str(),
                                                             sensors_iter_ptr->c_str());

                rc = ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE,
                                             *sensors_iter_ptr, "name", sensor, false  );
                if ( rc )
                {
                    elog ("%s %s '%s' sensor label 'name' parse error ... skipping\n",
                              host_ptr->hostname.c_str(),
                              host_ptr->group[g].group_name.c_str(),
                              sensors_iter_ptr->c_str());
                    error = true ;
                    continue ;
                }
                else
                {
                    hwmonSensor_init ( host_ptr->hostname, sensor_array_ptr );

                    dlog ("%s  '%s' group has '%s' sensor\n",
                             host_ptr->hostname.c_str(),
                             host_ptr->group[g].group_name.c_str(),
                             sensor.c_str());


                    /* fetch the sensor specific read command if there is one (cmd)
                     * maybe_missing parm is true so as to avoid ailing when there is
                     * no sensor specific command ; which is most of he time */
                    sensor_read_cmd.clear();
                    ini_get_config_value ( QUANTA_SENSOR_PROFILE_FILE, *sensors_iter_ptr, "cmd", sensor_read_cmd, true );
                    if ( sensor_read_cmd.empty() )
                    {
                        sensor_array_ptr->entity_path = sensor ;
                    }
                    else
                    {
                        sensor_array_ptr->path = sensor_read_cmd ;
                        host_ptr->group[g].path.clear();

                        sensor_array_ptr->entity_path = sensor_read_cmd ;
                        sensor_array_ptr->entity_path.append(ENTITY_DELIMITER);
                        sensor_array_ptr->entity_path.append(sensor);
                    }

                    sensor_array_ptr->sensorname = sensor ;
                    sensor_array_ptr->sensortype = host_ptr->group[g].sensortype ;
                    sensor_array_ptr->datatype   = host_ptr->group[g].datatype    ;


                    /* group uuid is in host_ptr at this point */
                    sensor_array_ptr->group_uuid = host_ptr->group[g].group_uuid ;

                    if ( host_ptr->group[g].group_uuid.empty() )
                    {
                        wlog ("%s '%s' had empty uuid ; grouping will fail\n",
                                  host_ptr->hostname.c_str(),
                                  host_ptr->group[g].group_name.c_str());
                        error = true ;
                        continue ;

                    }
                    sensor_array_ptr++ ;
                    sensors_found++ ;
                }
            }
        }
    }
    return (sensors_found);
}

int hwmonHostClass::delete_unwanted_sensors ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;
    for ( int s = 0 ; s < host_ptr->sensors ; s++ )
    {
        if (( host_ptr->sensor[s].sensorname == "PCH Thermal Trip" ) ||
            ( host_ptr->sensor[s].sensorname == "MB Thermal Trip" ) ||
            ( host_ptr->sensor[s].sensorname == "Temp_OCP"))
        {
            ilog ("%s %s sensor is being deleted from sensor model\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->sensor[s].sensorname.c_str() );

            clear_asserted_alarm ( host_ptr->hostname,
                                   HWMON_ALARM_ID__SENSOR,
                                  &host_ptr->sensor[s],
                                   REASON_DEPROVISIONED );

            hwmonHttp_del_sensor ( host_ptr->hostname, host_ptr->event, host_ptr->sensor[s]);

            rc = RETRY ;
        }
    }
    return (rc);
}



/* *************************************************************************
 *
 * Name       : hwmon_load_sensors
 *
 * Description: High level work horse procdure called by the Add FSM to
 *
 *              1. load all sensors and groups from the database.
 *                 - hwmonHttp_load_sensors
 *
 *              2. read all sensors and groups from the profile file.
 *
 *              3. ensure that all the sensors and groups in the profile
 *                 file are in the database and hardware monitor.
 *                 - hwmonHttp_add_sensor  (sysinv)
 *                 - add_sensor            (hwmon)
 *
 *              5. verify hat the sensors are read from hwmon correctly.
 *                 - get_sensor (hwmon)
 *
 *              4. group all sensors
 *                 - group_sensors (hwmon) and sysinv inside
 *
 * Scope      : private hwmonHostClass
 *
 * Parameters : host_ptr
 *
 * Returns    : TODO: handle modify errors better.
 *
 * *************************************************************************/
int hwmonHostClass::hwmon_load_sensors ( struct hwmonHostClass::hwmon_host * host_ptr, bool & error )
{
    /*
     *
     * This will load all already provisioned sensors from the sysinv
     * database into this host's host_ptr->sensor array.
     *
     */
    int rc = hwmonHttp_load_sensors ( host_ptr->hostname, host_ptr->event );

    if ( delete_unwanted_sensors ( host_ptr ) == RETRY )
    {
        ilog ("%s reloading sensors list\n", host_ptr->hostname.c_str());
        host_ptr->sensors = 0 ;
        rc = hwmonHttp_load_sensors ( host_ptr->hostname, host_ptr->event );
    }

    if ( rc == PASS )
    {
        sensor_type sensor_array[MAX_HOST_SENSORS] ;
        int profile_sensors = load_profile_sensors ( host_ptr, &sensor_array[0], MAX_HOST_SENSORS, error );

        ilog ("%s has %d sensors in the profile file\n",
                  host_ptr->hostname.c_str(),
                  profile_sensors );

        /**
         *  Loop through each profile file sensor and ensure it
         *  is in the database as well as in this host's control
         *  structure. if already in the sysinv database then
         *  don't try and reload it.
         **/

        /**
         *  TODO:ROBUST: Should have a check for sensors in the
         *              database that are not in the profile file.
         *              What to do ?
         **/
        for ( int i = 0 ; i < profile_sensors ; i++ )
        {
            rc    = PASS  ;
            bool found = false ;

            /* Loop over all the sensors in the database */
            for ( int j = 0 ; j < host_ptr->sensors ; j++ )
            {
                daemon_signal_hdlr ();

                if ( !sensor_array[i].entity_path.compare(host_ptr->sensor[j].entity_path) )
                {
                    found = true ;
                    break ;
                }
            }

            /**
             *  If hwmon does not have it after loading all the
             *  sensors from the database then handle adding this
             *  as a new sensor to the sysinv database here.
             **/
            if ( found == false )
            {
                ilog ( "%s '%s' sensor (add to sysinv)\n", host_ptr->hostname.c_str(), sensor_array[i].sensorname.c_str());
                rc = hwmonHttp_add_sensor ( host_ptr->hostname, host_ptr->event, sensor_array[i] );

                if ( rc != PASS )
                {
                    wlog ("%s '%s' sensor add failed (to sysinv) (rc:%d)\n",
                              host_ptr->hostname.c_str(),
                              sensor_array[i].sensorname.c_str(), rc);
                    break ;
                }
                else
                {
                    sensor_array[i].uuid = host_ptr->event.new_uuid ;
                    blog1 ("%s '%s' sensor added (to sysinv)\n",
                              host_ptr->hostname.c_str(),
                              sensor_array[i].sensorname.c_str());
                }
            }
            else
            {
                blog1 ("%s '%s' sensor already provisioned (in sysinv) (rc:%d)\n",
                           host_ptr->hostname.c_str(),
                           sensor_array[i].sensorname.c_str(), rc);
            }

            /**
             *  Only add this sensor to hwmon if it was
             *  successfully added to the sysinv database.
             **/
            if (( rc == PASS ) && ( found == false ))
            {
                // ilog ( "%s '%s' is in %s (add to hwmond)\n", host_ptr->hostname.c_str(), sensor_array[i].sensorname.c_str(), sensor_array[i].group_uuid.c_str());
                rc = add_sensor ( host_ptr->hostname, sensor_array[i] );

#ifdef WANT_FIT_TESTING
                if ( daemon_want_fit ( FIT_CODE__HWMON__ADD_SENSOR, host_ptr->hostname ))
                    rc = FAIL ;
#endif

                if ( rc != PASS )
                {
                    wlog ("%s '%s' sensor add failed (to hwmond) (rc:%d)\n",
                              host_ptr->hostname.c_str(),
                              sensor_array[i].sensorname.c_str(), rc);
                    break ;
                }
                else
                {
                    blog1 ("%s '%s' sensor added (to hwmon) uuid:%s\n",
                              host_ptr->hostname.c_str(),
                              sensor_array[i].sensorname.c_str(),
                              sensor_array[i].uuid.c_str());
                }
            }

            /* Associate it with its group */

            /**
             *  Verify that the sensor was loaded into hwmond
             *  correctly and assign it to a group
             **/
            if (( rc == PASS ) && ( i < host_ptr->sensors ))
            {
                sensor_type * sensor_ptr = get_sensor ( host_ptr->hostname, sensor_array[i].entity_path ) ;

#ifdef WANT_FIT_TESTING
                if ( daemon_want_fit ( FIT_CODE__HWMON__GET_SENSOR, host_ptr->hostname ))
                    sensor_ptr = NULL ;
#endif

                /* Verify the sensor content */
                if ( sensor_ptr != NULL )
                {
                    /* Load the sensor with values that are not carried through from the above add */
                    sensor_ptr->group_uuid = sensor_array[i].group_uuid ;

                    sensor_ptr->degraded = false ;
                    sensor_ptr->alarmed  = false ;

                    clear_ignored_state ( sensor_ptr ) ;
                    clear_logged_state  ( sensor_ptr ) ;

                    dlog1 ( "%s '%s' is in group %s (hwmon)\n", host_ptr->hostname.c_str(),
                                                                sensor_ptr->sensorname.c_str(),
                                                                sensor_ptr->group_uuid.c_str());

                    if ( daemon_get_cfg_ptr()->debug_bmgmt > 1 )
                    {
                        hwmonSensor_print ( host_ptr->hostname, sensor_ptr );
                    }
                }
                else
                {
                    wlog ("%s '%s' sensor should have been but was not found (in hwmon)\n",
                              host_ptr->hostname.c_str(),
                              sensor_array[i].sensorname.c_str());
                    rc = FAIL ;
                    break ;
                }
            }
        }

        if ( rc == PASS )
        {
            /* Group all the sensors into the groups specified by the profile file */
            rc = hwmonHostClass::hwmon_group_sensors ( host_ptr );

            if ( rc == PASS )
            {
                blog ("%s sensors grouped\n", host_ptr->hostname.c_str());
            }
            else
            {
                wlog ("%s sensor grouping failed (rc:%d)\n", host_ptr->hostname.c_str(), rc );
            }
        }
    }
    return (rc);
}

/* *************************************************************************
 *
 * Name       : hwmon_load_groups
 *
 * Description: Read all the sensor groups from the database and profile file.
 *              Those in the database that match the profile file are loaded
 *              into hwmon directly. Those found in the profile file but
 *              missing from the database are added to the database and
 *              hardware monitor. Assign the each sensor its correct group
 *              uuid and send sysinv the list of sensor uuids for each group.
 *
 * Scope      : private hwmonHostClass
 *
 * Parameters : host_ptr
 *
 * Returns    :
 *
 * *************************************************************************/
int hwmonHostClass::hwmon_load_groups ( struct hwmonHostClass::hwmon_host * host_ptr , bool & error )
{
    int rc = hwmonHttp_load_groups ( host_ptr->hostname, host_ptr->event );
    if ( rc == PASS )
    {
        struct sensor_group_type group_array[MAX_HOST_GROUPS] ;

        int profile_groups = load_profile_groups ( host_ptr, &group_array[0], MAX_HOST_GROUPS, error );

        ilog ("%s has %d sensor groups in profile file (%s)\n",
                  host_ptr->hostname.c_str(),
                  profile_groups, QUANTA_SENSOR_PROFILE_FILE);

        for ( int i = 0 ; i < profile_groups ; i++ )
        {
            rc    = PASS ;
            bool found = false ;

            daemon_signal_hdlr ();

            for ( int j = 0 ; j < host_ptr->groups ; j++ )
            {
                if ( !host_ptr->group[j].group_name.compare(group_array[i].group_name) )
                {
                    found = true ;
                    break ;
                }
            }

            if ( found == false )
            {
                dlog ("%s '%s' group not found, adding (to sysinv/hwmon)\n",
                          host_ptr->hostname.c_str(),
                          group_array[i].group_name.c_str());

                group_array[i].hostname = host_ptr->hostname ;

                rc = hwmonHttp_add_group ( host_ptr->hostname, host_ptr->event, group_array[i] );

                if ( rc )
                {
                    wlog ("%s '%s' sensor group add failed [%s:%s] (to sysinv/hwmon) (rc:%d)\n",
                    group_array[i].hostname.c_str(),
                    group_array[i].group_name.c_str(),
                    group_array[i].datatype.c_str(),
                    group_array[i].sensortype.c_str(), rc );
                    break ;
                }
                else
                {
                    group_array[i].group_uuid  = host_ptr->event.new_uuid ;
                    ilog ("%s '%s' sensor group added [%s:%s] (to sysinv)\n",
                    group_array[i].hostname.c_str(),
                    group_array[i].group_name.c_str(),
                    group_array[i].datatype.c_str(),
                    group_array[i].sensortype.c_str());
                }

                if ( rc == PASS )
                {
                    /**
                     *  Only add this sensor to hwmon if it was
                     *  successfully added to the sysinv database.
                     **/
                    if (( rc == PASS ) && ( found == false ))
                    {
                        rc = hwmon_add_group ( host_ptr->hostname, group_array[i] );
#ifdef WANT_FIT_TESTING
                        if ( daemon_want_fit ( FIT_CODE__HWMON__ADD_GROUP, host_ptr->hostname ))
                            rc = FAIL ;
#endif

                        if ( rc != PASS )
                        {
                            wlog ("%s '%s' sensor group add failed (to hwmon) (rc:%d)\n",
                                      host_ptr->hostname.c_str(),
                                      group_array[i].group_name.c_str(), rc);
                            break ;
                        }
                        else
                        {
                            blog1 ("%s '%s' sensor group added (to hwmon) uuid:%s\n",
                                      host_ptr->hostname.c_str(),
                                      group_array[i].group_name.c_str(),
                                      group_array[i].group_uuid.c_str());
                        }
                    }
                }
                /* error log is already printed */
            }

            /* tack on a few important elements */
            if (( rc == PASS ) && ( i < host_ptr->groups ))
            {
                struct sensor_group_type * group_ptr = hwmon_get_group ( host_ptr->hostname, group_array[i].group_name ) ;

#ifdef WANT_FIT_TESTING
                if ( daemon_want_fit ( FIT_CODE__HWMON__GET_GROUP, host_ptr->hostname ))
                    group_ptr = NULL ;
#endif

                /* Verify the sensor content */
                if ( group_ptr != NULL )
                {
                    /* Add the sensor label list for this group to the host_ptr group.
                     * This list was fetched and attached to the group array in load_profile_groups.
                     * Having it prevents the need to parse the profile file again to associate
                     * the sensors to a group all over again inside load_profile_sensors */
                    group_ptr->sensor_labels = group_array[i].sensor_labels ;

                    if ( daemon_get_cfg_ptr()->debug_bmgmt > 1 )
                    {
                        hwmonGroup_print ( host_ptr->hostname, group_ptr );
                    }
                }
                else
                {
                    wlog ("%s '%s' sensor group should have been but was not found (in hwmon)\n",
                              host_ptr->hostname.c_str(),
                              group_array[i].group_name.c_str());
                    rc = FAIL ;
                    break ;
                }
            }
        }
    }
    return(rc);
}

/* *************************************************************************
 *
 * Name       : hwmon_group_sensors
 *
 * Description: Assign the each sensor its correct group uuid and send
 *              sysinv the list of sensor uuids for each group.
 *
 *              To do this we have to loop over the groups 3 times
 *              1. init the sensors in each group
 *              2. assigned the sensors to the groups ; might not be linear
 *              3. create sensor list
 *
 * Scope      : private hwmonHostClass
 *
 * Parameters : host_ptr
 *
 * Returns    : TODO: handle modify errors better.
 *
 * *************************************************************************/

/* TODO: make this a hardware independed implementation */
int hwmonHostClass::hwmon_group_sensors ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;

    string sensor_list = "" ;

    for ( int g = 0 ; g < host_ptr->groups ; ++g )
    {
        host_ptr->group[g].sensors = 0 ;
    }

    ilog ("%s has %d sensors across %d groups\n",
              host_ptr->hostname.c_str(),
              host_ptr->sensors,
              host_ptr->groups);

    for ( int g = 0 ; g < host_ptr->groups ; ++g )
    {
        /* search through all the sensors and put them in their correct group */
        for ( int s = 0 ; s < host_ptr->sensors ; ++s )
        {
            if ( !host_ptr->group[g].group_uuid.compare(host_ptr->sensor[s].group_uuid))
            {
                /* Update this group with the pointers to is sensors */
                host_ptr->group[g].sensor_ptr[host_ptr->group[g].sensors] = &host_ptr->sensor[s] ;
                host_ptr->group[g].sensors++ ;

                dlog ("%s '%s' is assigned '%s' (%d)\n", host_ptr->hostname.c_str(),
                                                         host_ptr->group[g].group_name.c_str(),
                                                         host_ptr->sensor[s].sensorname.c_str(),
                                                         host_ptr->group[g].sensors);
            }
        }
    }

    /* Build up the sensor uuid list for each
     * group and send it to sysinv */
    for ( int g = 0 ; g < host_ptr->groups ; ++g )
    {
        int count = 0    ;
        bool first = true ;

        sensor_list.clear();

        for ( int s = 0 ; s < host_ptr->sensors ; ++s )
        {
            daemon_signal_hdlr ();

            /* only add it to the list got this group */
            if ( !host_ptr->group[g].group_uuid.compare(host_ptr->sensor[s].group_uuid) )
            {
                /* make sure there is a sensor read command at the group or sensor level */
                if ( host_ptr->group[g].path.empty() && host_ptr->sensor[s].path.empty() )
                {
                    elog ("%s '%s:%s' no read command for this combo ; ignoring sensor\n",
                              host_ptr->hostname.c_str(),
                              host_ptr->group[g].group_name.c_str(),
                              host_ptr->sensor[s].sensorname.c_str());
                    elog ("... group cmd:(%s) sensor cmd:(%s)\n",
                              host_ptr->group[g].path.c_str(),
                              host_ptr->sensor[s].path.c_str());
                }
                else
                {
                    /* Default each sensor to its groups action */
                    host_ptr->sensor[s].actions_minor = host_ptr->group[g].actions_minor_group ;
                    host_ptr->sensor[s].actions_major = host_ptr->group[g].actions_major_group ;
                    host_ptr->sensor[s].actions_critl = host_ptr->group[g].actions_critl_group ;

                    count++ ;
                    if ( first == false )
                    {
                        sensor_list.append(",");
                    }
                    else
                    {
                        first = false  ;
                    }
                    sensor_list.append(host_ptr->sensor[s].uuid);

                    if ( count == host_ptr->group[g].sensors )
                        break ;
                }
            }
        }

        if ( sensor_list.empty() )
        {
            wlog ("%s no sensors found for '%s' group ; should have %d\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->group[g].group_name.c_str(),
                      host_ptr->group[g].sensors);
        }
        else if ( host_ptr->group[g].sensors != count )
        {
            wlog ("%s incorrect number of sensors found for '%s' group ; has %d but should have %d\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->group[g].group_name.c_str(),
                      count,
                      host_ptr->group[g].sensors);
        }
        else
        {
            groupSensors_print ( &host_ptr->group[g] );
            rc = hwmonHttp_group_sensors ( host_ptr->hostname,
                                           host_ptr->event,
                                           host_ptr->group[g].group_uuid,
                                           sensor_list );
        }
    }

    return (rc);
}

void handle_new_suppression ( sensor_type * sensor_ptr )
{
    clear_asserted_alarm ( sensor_ptr->hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr, REASON_SUPPRESSED );
}

action_state_type * _get_action_state_ptr ( sensor_type * sensor_ptr,
                                            sensor_severity_enum hwmon_sev )
{
    if ( sensor_ptr )
    {
        if ( hwmon_sev == HWMON_SEVERITY_CRITICAL )
            return ( &sensor_ptr->critl ) ;
        else if ( hwmon_sev == HWMON_SEVERITY_MAJOR )
            return ( &sensor_ptr->major ) ;
        else if ( hwmon_sev == HWMON_SEVERITY_MINOR )
            return ( &sensor_ptr->minor ) ;
    }
    slog ("invalid parms (%p:%s)\n", sensor_ptr, get_severity(hwmon_sev).c_str());
    return (NULL);
}

int _update_group_action ( struct sensor_group_type * group_ptr, sensor_severity_enum hwmon_sev, string new_action )
{
    if ( hwmon_sev == HWMON_SEVERITY_CRITICAL )
        group_ptr->actions_critl_group = new_action ;
    else if ( hwmon_sev == HWMON_SEVERITY_MAJOR )
        group_ptr->actions_major_group = new_action ;
    else
        group_ptr->actions_minor_group = new_action ;

    return (PASS);
}

int _update_sensor_action ( sensor_type * sensor_ptr, sensor_severity_enum hwmon_sev, string new_action )
{
    if ( hwmon_sev == HWMON_SEVERITY_CRITICAL )
    {
        dlog ("%s %s critical action update '%s' -> '%s'\n",
                  sensor_ptr->hostname.c_str(),
                  sensor_ptr->sensorname.c_str(),
                  sensor_ptr->actions_critl.c_str(),
                  new_action.c_str());
        sensor_ptr->actions_critl = new_action ;
    }
    else if ( hwmon_sev == HWMON_SEVERITY_MAJOR )
    {
        dlog ("%s %s major action update '%s' -> '%s'\n",
                  sensor_ptr->hostname.c_str(),
                  sensor_ptr->sensorname.c_str(),
                  sensor_ptr->actions_major.c_str(),
                  new_action.c_str());
        sensor_ptr->actions_major = new_action ;
    }
    else
    {
        dlog ("%s %s minor action update '%s' -> '%s'\n",
                  sensor_ptr->hostname.c_str(),
                  sensor_ptr->sensorname.c_str(),
                  sensor_ptr->actions_minor.c_str(),
                  new_action.c_str());
        sensor_ptr->actions_minor = new_action ;
    }
    return (PASS);
}


bool severity_match ( EFmAlarmSeverityT fm_severity, sensor_severity_enum hwmon_sev )
{
    if ((( fm_severity == FM_ALARM_SEVERITY_MINOR ) && ( hwmon_sev == HWMON_SEVERITY_MINOR )) ||
        (( fm_severity == FM_ALARM_SEVERITY_MAJOR ) && ( hwmon_sev == HWMON_SEVERITY_MAJOR )) ||
        (( fm_severity == FM_ALARM_SEVERITY_CRITICAL ) && ( hwmon_sev == HWMON_SEVERITY_CRITICAL )) ||
        (( fm_severity == FM_ALARM_SEVERITY_CRITICAL ) && ( hwmon_sev == HWMON_SEVERITY_NONRECOVERABLE )))
        return (true);
    return (false);
}


int _manage_action_change ( string hostname,
                            struct sensor_group_type * group_ptr,
                            string cur_action,
                            string new_action,
                            sensor_severity_enum hwmon_sev,
                            EFmAlarmSeverityT fm_severity )
{

    bool new_action__log        = false ;
    bool new_action__alarm      = false ;
    bool new_action__ignore     = false ;
    bool new_action__reset      = false ;
    bool new_action__powercycle = false ;

    bool cur_action__log        = false ;
    bool cur_action__alarm      = false ;
    bool cur_action__ignore     = false ;
    bool cur_action__reset      = false ;
    bool cur_action__powercycle = false ;

    if ( is_ignore_action     ( new_action ))  new_action__ignore     = true ;
    if ( is_log_action        ( new_action ))  new_action__log        = true ;
    if ( is_alarm_action      ( new_action ))  new_action__alarm      = true ;
    if ( is_reset_action      ( new_action ))  new_action__reset      = true ;
    if ( is_powercycle_action ( new_action ))  new_action__powercycle = true ;

    if ( is_alarm_action      ( cur_action ))  cur_action__alarm      = true ;
    if ( is_ignore_action     ( cur_action ))  cur_action__ignore     = true ;
    if ( is_log_action        ( cur_action ))  cur_action__log        = true ;
    if ( is_reset_action      ( cur_action ))  cur_action__reset      = true ;
    if ( is_powercycle_action ( cur_action ))  cur_action__powercycle = true ;


    /* TODO: change these return codes to PASS once we know we don't get them */
    if (( new_action__log        && cur_action__log        ) ||
        ( new_action__alarm      && cur_action__alarm      ) ||
        ( new_action__ignore     && cur_action__ignore     ) ||
        ( new_action__reset      && cur_action__reset      ) ||
        ( new_action__powercycle && cur_action__powercycle ))
    {
        elog ("%s null '%s' sensor group action change for severity '%s' ; no action (%s to %s)\n",
                  hostname.c_str(),
                  group_ptr->group_name.c_str(),
                  get_severity(hwmon_sev).c_str(),
                  cur_action.c_str(),
                  new_action.c_str());
        return (FAIL_INVALID_OPERATION);
    }

    ilog ("%s modifying '%s' sensor group '%s' action from '%s' to '%s'\n",
              hostname.c_str(),
              group_ptr->group_name.c_str(),
              get_severity(hwmon_sev).c_str(),
              cur_action.c_str(),
              new_action.c_str());

    /*********************************************************************
     * There are 5 possible actions
     *
     *   - alarm
     *   - log
     *   - ignore
     *   - reset
     *   - powercycle
     *
     * Any action can be changed to any other action
     * Checks above ensure that no null action changes make it here
     *
     *********************************************************************/
    string reason   = get_severity(hwmon_sev) + " severity level action ";

    if      ( new_action__alarm      ) reason.append(REASON_SET_TO_ALARM);
    else if ( new_action__log        ) reason.append(REASON_SET_TO_LOG);
    else if ( new_action__ignore     ) reason.append(REASON_SET_TO_IGNORE);
    else if ( new_action__reset      ) reason.append(REASON_SET_TO_RESET);
    else if ( new_action__powercycle ) reason.append(REASON_SET_TO_POWERCYCLE);

    /* ... now all the sensors in that group */
    for ( int i = 0 ; i < group_ptr->sensors ; i++ )
    {
        daemon_signal_hdlr();

        sensor_type * sensor_ptr = group_ptr->sensor_ptr[i] ;

        if ( sensor_ptr->group_uuid != group_ptr->group_uuid )
        {
            slog ("%s %s group:sensor uuid mismatch ; auto correcting\n",
                      hostname.c_str(),
                      sensor_ptr->sensorname.c_str() );

            slog ("%s ... group:sensor [%s:%s]\n",
                      hostname.c_str(),
                      group_ptr->group_uuid.c_str(),
                      sensor_ptr->group_uuid.c_str());

            sensor_ptr->group_uuid = group_ptr->group_uuid ;
        }

        /* Only run change handling when the severity matches current status */
        if ( !severity_match ( fm_severity, sensor_ptr->severity ) )
        {
            /* but we still need to update the action for each sensor */
            _update_sensor_action ( sensor_ptr, hwmon_sev, new_action );
            continue ;
        }

//      string severity = get_severity(sensor_ptr->severity);

        /* get correct action state bools */
        action_state_type * action_state_ptr = _get_action_state_ptr ( sensor_ptr, hwmon_sev );
        if ( action_state_ptr == NULL )
        {
            slog ("%s %s has invalid action state %d\n",
                      hostname.c_str(),
                      sensor_ptr->sensorname.c_str(),
                      hwmon_sev );

            return (FAIL_INVALID_DATA);
        }

        hwmonLog_clear ( hostname, HWMON_ALARM_ID__SENSORGROUP, group_ptr->group_name, reason );

        /* Handle Action change away from ............. ALARM */
        if ( cur_action__alarm )
        {
           /*************************************************************
            *
            * From 'alarm' to 'log' case
            * --------------------------
            *
            * If alarm is asserted then clear it in favor of a log
            *
            *************************************************************/
            if ( new_action__log )
            {
                dlog ("%s %s action change from 'alarm' to 'log'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                /* Produce a log if the sensor is reporting error status */
                if (( sensor_ptr->suppress == false )   &&
                    ( sensor_ptr->status.compare("ok")) &&
                    ( sensor_ptr->status.compare("offline")))
                {
                    hwmonLog ( hostname, HWMON_ALARM_ID__SENSOR, fm_severity, sensor_ptr->sensorname, REASON_OOT );
                    set_logged_severity ( sensor_ptr , fm_severity );
                }
            }

           /*************************************************************
            *
            * From 'alarm' to 'reset'
            * ----------------------------
            *
            * If alarm is asserted then clear it and let it get
            * generated again in the handler if the severity condition
            * persists.
            *
            *************************************************************/
            else if ( new_action__reset )
            {
                dlog ("%s %s action change from 'alarm' to 'reset'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }
            }

           /*************************************************************
            *
            * From 'alarm' to 'powercycle'
            * ----------------------------
            *
            * If alarm is asserted then clear it and let it get
            * generated again in the handler  if the severity condition
            * persists.
            *
            *************************************************************/
            else if ( new_action__powercycle )
            {
                dlog ("%s %s action change from 'alarm' to 'powercycle'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }
            }

           /*************************************************************
            *
            * From 'alarm' to 'ignore' case
            * -----------------------------
            *
            * If alarm is asserted then clear it.
            *
            *************************************************************/
            else /* ignore as default case */
            {
                dlog ("%s %s action change from 'alarm' to 'ignore'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason) ;
                    set_ignored_severity ( sensor_ptr, fm_severity );
                }
            }
            /* Clear alarm and degrade state */
            clear_alarmed_state  ( sensor_ptr );
            clear_degraded_state ( sensor_ptr );
        }


        /* Handle Action change away from ............. LOG */
        else if ( cur_action__log )
        {
            /* Do auto correction / garbage collection */
            clear_alarmed_state  ( sensor_ptr );
            clear_degraded_state ( sensor_ptr );
            clear_ignored_state  ( sensor_ptr );

           /**************************************************************
            *
            * From 'log' -> 'alarm' case
            * --------------------------
            *
            * If it was a log action and was logged and is now alarm
            * action then send a log indicating that the current log is
            * cleared.
            *
            * Allow the alarm to get raised on the next status reading
            *
            **************************************************************/
            if ( new_action__alarm )
            {
                dlog ("%s %s action change from 'log' to 'alarm'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                if ( action_state_ptr->logged == true )
                {
                    hwmonLog_clear ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
                }
            }

           /**********************************************************
            *
            * From 'log' to 'reset' case
            * --------------------------
            *
            * Allow the alarm to get raised on the next status reading
            *
            *********************************************************/
            else if ( new_action__reset )
            {
                dlog ("%s %s action change from 'log' to 'reset'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                if ( action_state_ptr->logged == true )
                {
                    hwmonLog_clear ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
                }
            }

           /**********************************************************
            *
            * From 'log' to 'powercycle' case
            * -------------------------------
            *
            * Allow the alarm to get raised on the next status reading
            *
            *********************************************************/
            else if ( new_action__powercycle )
            {
                dlog ("%s %s action change from 'log' to 'powercycle'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                if ( action_state_ptr->logged == true )
                {
                    hwmonLog_clear ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
                }
            }

           /**********************************************************
            *
            * From 'log -> 'ignore' case
            * ---------------------------
            *
            * If it was a log action and was logged and is now ignore
            * then send a log indicating that it is now ignored.
            *
            ***********************************************************/
            else /* ignore as default case */
            {
                dlog ("%s %s action change from 'log' to 'ignore'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                if ( action_state_ptr->logged == true )
                {
                    hwmonLog_clear ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
                    set_ignored_severity ( sensor_ptr, fm_severity );
                }
            }
            clear_logged_state ( sensor_ptr );
        }


        /* Handle Action change away from ............. IGNORE */
        else if ( cur_action__ignore )
        {
            /* Do auto correction / garbage collection */
            clear_alarmed_state  ( sensor_ptr );
            clear_degraded_state ( sensor_ptr );

            /**************************************************************
            *
            * From 'ignore' to 'alarm' case
            * -----------------------------
            *
            * If was ignore and is now alarm then just take it out of
            * ignore and let the alarm get raised on the next audit.
            *
            **************************************************************/
            if ( new_action__alarm )
            {
                dlog ("%s %s action change from 'ignore' to 'alarm'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* additional garbage collection and cleanup */
                clear_logged_state  ( sensor_ptr );

                /* take it out of ignore state */
                clear_ignored_state ( sensor_ptr );

                /* do nothing and allow the sensor event handler to act */
            }

           /**************************************************************
            *
            * From 'ignore' to 'reset'      case
            * ----------------------------------
            *
            **************************************************************/
            else if ( new_action__reset )
            {
                dlog ("%s %s action change from 'ignore' to 'reset'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* additional garbage collection and cleanup */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* do nothing and allow the sensor event handler to act */
            }

           /**************************************************************
            *
            * From 'ignore' to 'powercycle' case
            * ----------------------------------
            *
            **************************************************************/
            else if ( new_action__powercycle )
            {
                dlog ("%s %s action change from 'ignore' to 'powercycle'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* additional garbage collection and cleanup */
                clear_logged_state  ( sensor_ptr );

                /* take it out of ignore state */
                clear_ignored_state  ( sensor_ptr );

                /* do nothing and allow the sensor event handler to act */
            }

           /**************************************************************
            *
            * From 'ignore' to 'log' case
            * -----------------------------
            *
            * If was ignore then raise log if status is not ok or offline
            *
            ***************************************************************/
            else /* log as default case */
            {
                dlog ("%s %s action change from 'ignore' to 'log'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                clear_ignored_state  ( sensor_ptr );

                /* take it out of logged state */
                clear_logged_state  ( sensor_ptr );

                /* Produce a log if the sensor is reporting error status */
                if (( sensor_ptr->suppress == false )   &&
                    ( sensor_ptr->status.compare("ok")) &&
                    ( sensor_ptr->status.compare("offline")))
                {
                    hwmonLog ( hostname, HWMON_ALARM_ID__SENSOR, fm_severity, sensor_ptr->sensorname, REASON_OOT );
                    set_logged_severity ( sensor_ptr , fm_severity );
                }
            }
        }

        /* Handle Action change away from ............. RESET */
        else if ( cur_action__reset )
        {
           /*************************************************************
            *
            * From 'reset' to 'alarm' case
            * -------------------------------
            *
            * If alarm is asserted then clear it only to allow the
            * it to be raised in the handler by failed severity status.
            *
            *************************************************************/
            if ( new_action__alarm )
            {
                dlog ("%s %s action change from 'reset' to 'alarm'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                /* Clear alarm and degrade state */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }

           /*************************************************************
            *
            * From 'reset' to 'powercycle' case
            * ---------------------------------
            *
            * If alarm is asserted then clear it only to allow the
            * powercycle case alarm to be raised in the handler.
            *
            *************************************************************/
            else if ( new_action__powercycle )
            {
                dlog ("%s %s action change from 'reset' to 'powercycle'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                /* Clear alarm and degrade state */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }

           /*************************************************************
            *
            * From 'reset' to 'log' case
            * --------------------------
            *
            * Clear the reset alarm if it is raised and set the
            * corresponding log.
            *
            *************************************************************/
            else if ( new_action__log )
            {
                dlog ("%s %s action change from 'reset' to 'log'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* do garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                /* Produce a log if the sensor is reporting error status */
                if (( sensor_ptr->suppress == false )   &&
                    ( sensor_ptr->status.compare("ok")) &&
                    ( sensor_ptr->status.compare("offline")))
                {
                    hwmonLog ( hostname, HWMON_ALARM_ID__SENSOR, fm_severity, sensor_ptr->sensorname, REASON_OOT );
                    set_logged_severity ( sensor_ptr , fm_severity );
                }

                /* Clear alarm and degrade state if Do auto correction / garbage collection */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }


           /*************************************************************
            *
            * From 'reset' to 'ignore' case
            * ----------------------------------
            *
            * Clear the reset alarm if it is raised
            *
            *************************************************************/
            else /* ignore as default case */
            {
                dlog ("%s %s action change from 'reset' to 'ignore'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* do garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                if (( sensor_ptr->suppress == false ) && ( sensor_ptr->status.compare(HWMON_CRITICAL)))
                {
                    set_ignored_severity ( sensor_ptr, fm_severity );
                }

                /* Clear alarm and degrade state if Do auto correction / garbage collection */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }
        }

        /* Handle Action change away from ............. POWERCYCLE */
        else if ( cur_action__powercycle )
        {
           /*************************************************************
            *
            * From 'powercycle' to 'alarm' case
            * ---------------------------------
            *
            * If alarm is asserted then clear it only to allow it to
            * be raised in the handler if the status persists.
            *
            *************************************************************/
            if ( new_action__alarm )
            {
                dlog ("%s %s action change from 'powercycle' to 'alarm'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                /* Clear alarm and degrade state */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }

           /*************************************************************
            *
            * From 'powercycle' to 'log' case
            * -------------------------------
            *
            * If alarm is asserted then clear it in favor of a log
            *
            *************************************************************/
            else if ( new_action__log )
            {
                dlog ("%s %s action change from 'powercycle' to 'log'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                /* Produce a log if the sensor is reporting error status */
                if (( sensor_ptr->suppress == false )   &&
                    ( sensor_ptr->status.compare("ok")) &&
                    ( sensor_ptr->status.compare("offline")))
                {
                    hwmonLog ( hostname, HWMON_ALARM_ID__SENSOR, fm_severity, sensor_ptr->sensorname, REASON_OOT );
                    set_logged_severity ( sensor_ptr , fm_severity );
                }
                /* Clear alarm and degrade state */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }

           /*************************************************************
            *
            * From 'powercycle' to 'reset' case
            * -------------------------------
            *
            * If alarm is asserted then clear it only to allow the reset
            * case alarm to be raised in the handler if the status
            * persists as critical.
            *
            *************************************************************/
            else if ( new_action__reset )
            {
                dlog ("%s %s action change from 'powercycle' to 'reset'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                /* Clear alarm and degrade state */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }

           /*************************************************************
            *
            * From 'powercycle' to 'ignore' case
            * ----------------------------------
            *
            * If alarm is asserted then clear it.
            *
            *************************************************************/
            else /* ignore as default case */
            {
                dlog ("%s %s action change from 'powercycle' to 'ignore'\n", hostname.c_str(), sensor_ptr->sensorname.c_str());

                /* cleanup and garbage collection */
                clear_ignored_state ( sensor_ptr );
                clear_logged_state  ( sensor_ptr );

                /* clear the current alarm if it exists */
                if ( action_state_ptr->alarmed == true )
                {
                    clear_severity_alarm ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, fm_severity, reason ) ;
                }

                if (( sensor_ptr->suppress == false ) && ( sensor_ptr->status.compare(HWMON_CRITICAL)))
                {
                    set_ignored_severity ( sensor_ptr, fm_severity );
                }

                /* Clear alarm and degrade state */
                clear_alarmed_state  ( sensor_ptr );
                clear_degraded_state ( sensor_ptr );
            }
        }
        else
        {
            elog ("%s no '%s' sensor group action change for severity '%s' ; no action (%s to %s)\n",
                      hostname.c_str(),
                      group_ptr->group_name.c_str(),
                      get_severity(hwmon_sev).c_str(),
                      cur_action.c_str(),
                      new_action.c_str());

            return (FAIL_INVALID_OPERATION) ;
        }
        _update_sensor_action ( sensor_ptr, hwmon_sev, new_action );
    }
    _update_group_action ( group_ptr, hwmon_sev, new_action );

    return (PASS);
}

/* *************************************************************************
 *
 * Name       : group_modify
 *
 * Description: For a limited number of attributes, modify each sensor
 *              that is part of the specified group for that attribute.
 *
 * Modifiable attributes are:
 *
 *              suppress               -> suppress
 *              audit_interval_group   -> audit_interval
 *              actions_minor_group    -> actions_minor
 *              actions_major_group    -> actions_major
 *              actions_critical_group -> actions_critical
 *
 * Scope      : public hwmonHostClass
 *
 * Assumptions: action (as value) has already been verified by calling procedure
 *
 * Returns    : PASS, FAIL or FAIL_...
 *
 * *************************************************************************/

/* TODO:FEATURE: manage alarms when the actions are changed */

int hwmonHostClass::group_modify ( string hostname, string group_uuid, string key, string value )
{
    int rc = PASS ;

    if ( ( !group_uuid.empty() ) && ( !hostname.empty()) )
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            sensor_group_type * group_ptr = NULL ;

            for ( int i = 0 ; i < host_ptr->groups ; i++ )
            {
                if ( !host_ptr->group[i].group_uuid.compare(group_uuid) )
                {
                    group_ptr = &host_ptr->group[i] ;
                    break ;
                }
            }

            if ( group_ptr == NULL )
            {
                slog ("%s '%s' group not found value:%s (uuid:%s) \n",
                          hostname.c_str(),
                          key.c_str(),
                          value.c_str(),
                          group_uuid.substr(0,8).c_str());

                return (FAIL_NOT_FOUND);
            }

            /* Look for Suppression Modify */
            if ( !key.compare("suppress") )
            {
                /* modify the group suppression */
                if ( ( value.compare("True") ) && ( value.compare("true") ))
                {
                    hwmonLog ( hostname, HWMON_ALARM_ID__SENSORGROUP, FM_ALARM_SEVERITY_CLEAR, group_ptr->group_name, REASON_UNSUPPRESSED );
                    group_ptr->suppress = false ;
                }
                else
                {
                    hwmonLog ( hostname, HWMON_ALARM_ID__SENSORGROUP, FM_ALARM_SEVERITY_CLEAR, group_ptr->group_name, REASON_SUPPRESSED );
                    group_ptr->suppress = true ;
                }
                ilog ("%s '%s' sensor group is '%ssuppressed'\n",
                          host_ptr->hostname.c_str(),
                          group_ptr->group_name.c_str(),
                          group_ptr->suppress ? "" : "un");

                /* ... now all the sensors in that group */
                for ( int s = 0 ; s < group_ptr->sensors ; s++ )
                {
                    /* modify all sensors to match the group suppression state */
                    sensor_type * sensor_ptr = group_ptr->sensor_ptr[s] ;
                    if ( sensor_ptr->suppress != group_ptr->suppress )
                    {
                        if ( group_ptr->suppress == true )
                        {
                            handle_new_suppression ( sensor_ptr );
                        }
                        else
                        {
                            manage_sensor_state ( hostname, sensor_ptr, get_severity(sensor_ptr->status));
                        }
                        sensor_ptr->suppress = group_ptr->suppress ;
                    }
                }
            }

            /* Look for Audit Interval Modify */
            if ( !key.compare("audit_interval_group") )
            {
                hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr() ;

                int interval = atoi(value.data());

                if ( interval < HWMON_MIN_AUDIT_INTERVAL )
                {
                    wlog ("%s invalid audit interval (%d:%s)\n", hostname.c_str(), interval, value.c_str());
                    return (FAIL_INVALID_DATA);
                }

                /* modify the group interval */
                /* This just sets a flag so that the audit interval
                * group changes sent back to sysinv are done at base level
                * rather that inside this http request, which would create
                * a deadlock */
                obj_ptr->modify_audit_interval ( hostname, interval );
            }

            /* Look for Critical Action Group Modify */
            if ( !key.compare("actions_critical_group") )
            {
                sensor_severity_enum hwmon_sev =    HWMON_SEVERITY_CRITICAL ;
                EFmAlarmSeverityT fm_severity  = FM_ALARM_SEVERITY_CRITICAL ;

                string cur_action = group_ptr->actions_critl_group ;
                string new_action = value ;

                rc = _manage_action_change ( hostname, group_ptr, cur_action, new_action, hwmon_sev, fm_severity );
#ifdef WANT_MANAGE_SENSOR_STATE_ON_ACTION_CHANGE
                /* force evaluation of all sensors in this group */
                for ( int i = 0 ; i < group_ptr->sensors ; i++ )
                {
                    sensor_severity_enum sev = get_severity(group_ptr->sensor_ptr[i]->status) ;
                    if ( sev == HWMON_SEVERITY_CRITICAL )
                    {
                        manage_sensor_state ( hostname, group_ptr->sensor_ptr[i], sev );
                    }
                }
#endif
            }

            /* Look for Major Action Group Modify */
            if ( !key.compare("actions_major_group") )
            {
                sensor_severity_enum hwmon_sev =    HWMON_SEVERITY_MAJOR ;
                EFmAlarmSeverityT fm_severity  = FM_ALARM_SEVERITY_MAJOR ;

                string cur_action = group_ptr->actions_major_group ;
                string new_action = value ;

                rc = _manage_action_change ( hostname, group_ptr, cur_action, new_action, hwmon_sev, fm_severity );

#ifdef WANT_MANAGE_SENSOR_STATE_ON_ACTION_CHANGE
                /* force evaluation of all sensors in this group */
                for ( int i = 0 ; i < group_ptr->sensors ; i++ )
                {
                    sensor_severity_enum sev = get_severity(group_ptr->sensor_ptr[i]->status) ;
                    if ( sev == HWMON_SEVERITY_MAJOR )
                    {
                        manage_sensor_state ( hostname, group_ptr->sensor_ptr[i], sev );
                    }
                }
#endif
            }

            /* Look for Minor Action Group Modify */
            if ( !key.compare("actions_minor_group") )
            {
                sensor_severity_enum hwmon_sev =    HWMON_SEVERITY_MINOR ;
                EFmAlarmSeverityT fm_severity  = FM_ALARM_SEVERITY_MINOR ;

                string cur_action = group_ptr->actions_minor_group ;
                string new_action = value ;

                rc = _manage_action_change ( hostname, group_ptr, cur_action, new_action, hwmon_sev, fm_severity );

#ifdef WANT_MANAGE_SENSOR_STATE_ON_ACTION_CHANGE
                /* force evaluation of all sensors in this group */
                for ( int i = 0 ; i < group_ptr->sensors ; i++ )
                {
                    sensor_severity_enum sev = get_severity(group_ptr->sensor_ptr[i]->status) ;
                    if ( sev == HWMON_SEVERITY_MINOR )
                    {
                        manage_sensor_state ( hostname, group_ptr->sensor_ptr[i], sev );
                    }
                }
#endif
            }
            monitor_now ( host_ptr );
        }
        else
        {
            elog ("%s hostname is unknown\n", hostname.c_str());
            rc = FAIL_UNKNOWN_HOSTNAME ;
        }
    }
    else
    {
        slog ("empty hostname or group uuid\n");
        rc = FAIL_STRING_EMPTY ;
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : bmc_create_sensors
 *
 * Description: Add sample sensors to the sysinv database.
 *
 *****************************************************************************/

int hwmonHostClass::bmc_create_sensors ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;
    int sensor_errors = 0 ;
    host_ptr->sensors = 0 ;

    for ( int s = 0 ; s < host_ptr->samples ; ++s )
    {
        string sensortype = bmc_get_grouptype ( host_ptr->hostname,
                                                 host_ptr->sample[s].unit,
                                                 host_ptr->sample[s].name);

#ifdef WANT_FIT_TESTING
        /* sysinv does not allow adding a sensor with no type ; will reject with a 400 */
        if ( daemon_want_fit ( FIT_CODE__HWMON__BAD_SENSOR, host_ptr->hostname, host_ptr->sample[s].name))
            sensortype = "" ;
#endif

        if ( sensortype.empty() )
        {
            if ( ++sensor_errors > MAX_SENSOR_TYPE_ERRORS )
            {
                rc = FAIL_STRING_EMPTY ;
                elog ("%s '%s' not added ; sample sensor create failed ; too many sensor type errors (rc:%d)\n",
                         host_ptr->hostname.c_str(),
                         host_ptr->sample[s].unit.c_str(), rc);
            }
            else
            {
                wlog ("%s %s %s %s%s not added ; empty or unsupported type classification\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sample[s].name.c_str(),
                          host_ptr->sample[s].status.c_str(),
                          host_ptr->sample[s].unit.c_str(),
                          host_ptr->sample[s].ignore ? " ignored" : "");
            }
        }
        else
        {
            /* add the sensor to hwmon */
            hwmonSensor_init ( host_ptr->hostname, &host_ptr->sensor[host_ptr->sensors] );
            clear_ignored_state (&host_ptr->sensor[host_ptr->sensors]);
            clear_alarmed_state (&host_ptr->sensor[host_ptr->sensors]);
            clear_logged_state  (&host_ptr->sensor[host_ptr->sensors]);
            host_ptr->sensor[host_ptr->sensors].sensorname = host_ptr->sample[s].name ;
            host_ptr->sensor[host_ptr->sensors].sensortype = sensortype ;

#ifdef WANT_FIT_TESTING
            /* sysinv does not allow adding a sensor with no type ; will reject with a 400 */
            if ( daemon_want_fit ( FIT_CODE__HWMON__ADD_SENSOR, host_ptr->hostname, host_ptr->sample[s].name))
                host_ptr->sensor[host_ptr->sensors].sensortype = "" ;
#endif

            /* add it to to sysinv */
            if ( ( rc = hwmonHttp_add_sensor ( host_ptr->hostname,
                                               host_ptr->event,
                                               host_ptr->sensor[host_ptr->sensors] )) == PASS )
            {
                /* add the sysinv uuid for this sensor to the sensor in hwmon */
                host_ptr->sensor[host_ptr->sensors].uuid = host_ptr->event.new_uuid ;
                host_ptr->sensor[host_ptr->sensors].group_enum = host_ptr->sample[s].group_enum ;
                // hwmonSensor_print ( host_ptr->hostname, &host_ptr->sensor[host_ptr->sensors] );
                ilog ("%s '%s' sensor added\n", host_ptr->hostname.c_str(),
                      host_ptr->sensor[host_ptr->sensors].sensorname.c_str());
                host_ptr->sensors++ ;
            }
            else
            {
                elog ("%s '%s' sensor add failed (rc:%d)\n", host_ptr->hostname.c_str(),
                          host_ptr->sensor[host_ptr->sensors].sensorname.c_str(), rc);
                hwmonSensor_print ( host_ptr->hostname, &host_ptr->sensor[s] );
            }
        }

        if ( rc )
            break ;

    } /* end for loop over sensor samples */
    return (rc);
}



/*****************************************************************************
 *
 * Name       : bmc_disable_sensors
 *
 * Purpose    : With the introduction of bmc monitoring, all groups are
 *              monitored at once. Therefore all should be in the same state.
 *
 * Description: Set all sensors to specified state.
 *              If disabled then also set to offline.
 *
 ******************************************************************************/

int  hwmonHostClass::bmc_disable_sensors  ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = FAIL_NULL_POINTER ;
    if ( host_ptr )
    {
        rc = PASS ;

        /* don't send  requests to sysinv if we are in the middle of
         * deleting a host because sysinv has already gotten rid of the
         * sensor model */
        if ( host_ptr->host_delete == true )
            return (PASS);

        for ( int s = 0 ; s < host_ptr->sensors ; ++s )
        {
            sensor_type * sensor_ptr = &host_ptr->sensor[s] ;
            if (( sensor_ptr->state.compare("disabled")) ||
                ( sensor_ptr->status.compare("offline")))
            {
                sensor_ptr->state = "disabled" ;
                sensor_ptr->status = "offline" ;

                int status = hwmonHttp_disable_sensor ( host_ptr->hostname,
                                                        host_ptr->event,
                                                        sensor_ptr->uuid );
                if ( status )
                {
                    elog ( "%s failed to disable '%s' sensor\n",
                               host_ptr->hostname.c_str(),
                               sensor_ptr->sensorname.c_str());

                    if ( rc == PASS  )
                        rc = RETRY ;
                }
                clear_logged_state ( sensor_ptr ) ;
            }
        }
    }
    return (rc);
}
