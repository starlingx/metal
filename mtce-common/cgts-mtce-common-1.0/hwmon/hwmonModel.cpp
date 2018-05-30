/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Wind River Titanium Cloud Hardware Monitor" Sensor Model" Utilities
 *
 *
 * These are the utilities that load, create, group and delete sensor models
 *
 *
 * ipmi_load_sensor_model ....... called by add_host_handler FSM
 *
 * ipmi_create_sensor_model
 *
 *   ipmi_create_sample_model ... create model based on sample data
 *      ipmi_create_groups
 *      ipmi_create_sensors
 *      ipmi_group_sensors
 *
 *   ipmi_create_quanta_model ... create model for Quanta server
 *      ipmi_add_group
 *      load_profile_groups
 *      load_profile_sensors
 *      hwmon_group_sensors
 *
 * ipmi_delete_sensor_model ..... called on model re-create
 *
 *****************************************************************************/

#include "daemon_ini.h"   /* for ... parse_ini and MATCH                     */
#include "nodeBase.h"     /* for ... mtce common definitions                 */
#include "jsonUtil.h"     /* for ... json utilitiies                         */
#include "nodeUtil.h"     /* for ... mtce common utilities                   */
#include "hwmonUtil.h"    /* for ... get_severity                            */
#include "hwmonClass.h"   /* for ... service class definition                */
#include "hwmonHttp.h"    /* for ... http podule header                      */
#include "hwmonSensor.h"  /* for ... this module header                      */
#include "hwmonIpmi.h"    /* for ... QUANTA_SENSOR_PROFILE_CHECKSUM         */

/*****************************************************************************
 *
 * Name       : ipmi_create_sensor_model
 *
 * Description: Top level utility that creates a sensor model based on
 *              sample data.
 *
 *              The caller has already determined if the sample set matches
 *              the special case Quanta server model. If it does then we
 *              use the Quanta sensor profile to create the model. Otherwise,
 *              the model is created based on sensor samples.
 *
 ******************************************************************************/

int hwmonHostClass::ipmi_create_sensor_model ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;
    ilog ("%s creating sensor model\n", host_ptr->hostname.c_str());

    host_ptr->groups = 0 ;

    /* If this is NOT a Quanta Server then ... */
    if ( ! host_ptr->quanta_server )
    {
        /*
         * Dynamically create a model based
         * on the sensor sample reading data.
         */
        rc = ipmi_create_sample_model ( host_ptr );
    }

    /* Otherwise create the model based on the known Quanta sensor profile */
    else
    {
        if ( ( rc = ipmi_create_quanta_model ( host_ptr )) == PASS )
        {
            if ( host_ptr->groups >= MIN_SENSOR_GROUPS )
            {
               /*
                * If this is a Quanta server then the best way to ensure the
                * sensor profile is identical and backward compatible is to
                * load the sensor profile from the legacy Quanta profile file.
                *
                *      QUANTA_SENSOR_PROFILE_FILE
                */
                struct sensor_group_type  group_array [MAX_HOST_GROUPS] ;
                       sensor_type       sensor_array [MAX_HOST_SENSORS];

                int profile_groups ;
                bool error = false ;

                ilog ("%s provisioning Quanta server using %s\n",
                          host_ptr->hostname.c_str(), QUANTA_SENSOR_PROFILE_FILE );

// HP:  Why IMPI sensor model is using smashLoad_Server_info ??
// EM:  To maintain legacy mode for Nokia, don't change this (for now):q
//
//                if (ini_parse ( QUANTA_SENSOR_PROFILE_FILE, smashLoad_server_info, &host_ptr->profile_config ) < 0)
//                {
//                    elog ("Can't load '%s'\n", QUANTA_SENSOR_PROFILE_FILE );
//                    return (FAIL_LOAD_INI);
//                }

                profile_groups = load_profile_groups ( host_ptr, &group_array[0], MAX_HOST_GROUPS, error );
                if (( error == false ) && ( profile_groups == host_ptr->groups ))
                {
                    int profile_sensors;
                    for ( int g = 0 ; g < host_ptr->groups ; ++g )
                    {
                        /*
                         * Add the sensor label list to each host_ptr group[x].
                         *
                         * This list was fetched and attached to the group array
                         * in load_profile_groups.
                         *
                         * Having it prevents the need to parse the profile file
                         * again to associate the sensors to a group all over
                         * again inside load_profile_sensors
                         */

                        host_ptr->group[g].sensor_labels = group_array[g].sensor_labels ;

                        blog ("%s '%s' group sensor list: %s\n",
                                   host_ptr->hostname.c_str(),
                                   host_ptr->group[g].group_name.c_str(),
                                   host_ptr->group[g].sensor_labels.c_str());
                    }

                    ilog ( "%s %d profile groups loaded\n", host_ptr->hostname.c_str(), profile_groups );
                    profile_sensors = load_profile_sensors ( host_ptr, &sensor_array[0], MAX_HOST_SENSORS, error );
                    if (( error == false ) && ( profile_sensors ))
                    {
                        ilog ( "%s %d profile sensors loaded\n", host_ptr->hostname.c_str(), profile_sensors );
                        for ( int s = 0 ; s < profile_sensors ; ++s )
                        {
                            if (( rc = hwmonHttp_add_sensor ( host_ptr->hostname, host_ptr->event, sensor_array[s])) == PASS )
                            {
                                sensor_array[s].uuid  = host_ptr->event.new_uuid ;
                                if (( rc = add_sensor ( host_ptr->hostname, sensor_array[s] )) == PASS )
                                {
                                    blog ( "%s '%s' sensor added\n",
                                               host_ptr->hostname.c_str(),
                                               host_ptr->sensor[s].sensorname.c_str());
                                }
                                else
                                {
                                    wlog ("%s '%s' sensor add failure (to hwmon)\n",
                                              host_ptr->hostname.c_str(),
                                              sensor_array[s].sensorname.c_str());
                                }
                            }
                            else
                            {
                                wlog ("%s '%s' sensor add failure (to sysinv)\n",
                                          host_ptr->hostname.c_str(),
                                          sensor_array[s].sensorname.c_str());
                            }
                        } /* end for loop */
                    }
                    else
                    {
                        elog ( "%s load_profile_sensors failed (rc:%d) (%d)\n",
                                   host_ptr->hostname.c_str(),
                                   error,
                                   profile_sensors );
                    }
                }
                else
                {
                    elog ( "%s load_profile_groups failed (rc:%d) (%d:%d)\n",
                               host_ptr->hostname.c_str(),
                               error,
                               profile_groups,
                               host_ptr->groups );
                }
            }
            else
            {
                elog ("%s too few groups\n", host_ptr->hostname.c_str());
                rc = FAIL_INVALID_DATA ;
            }
        }
        else
        {
            elog ("%s failed to create group model (rc:%d)\n", host_ptr->hostname.c_str(), rc);
        }
    }


    if (( rc == PASS ) && ( host_ptr->quanta_server))
    {
        /* Group all the sensors into the groups specified by the profile file */
        rc = hwmonHostClass::hwmon_group_sensors ( host_ptr );

        if ( rc == PASS )
        {
            ilog ("%s sensors grouped\n", host_ptr->hostname.c_str());
        }
        else
        {
            elog ("%s sensor grouping failed (rc:%d)\n", host_ptr->hostname.c_str(), rc );
        }

        plog ("%s sensor model created\n", host_ptr->hostname.c_str() );
    }

    if (( host_ptr->relearn == true ) ||
        ( host_ptr->interval < HWMON_MIN_AUDIT_INTERVAL ))
    {
        dlog ("%s requesting interval change (%d)\n",
                  host_ptr->hostname.c_str(),
                  host_ptr->interval );

        host_ptr->interval_changed = true ;
    }

    /* make sure all sensors are updated with the group actions */

    return (rc);
}


/******************************************************************************
 *
 * Name       : ipmi_create_sample_model
 *
 * Description: Create a sensor model based on sample data.
 *
 ******************************************************************************/

int hwmonHostClass::ipmi_create_sample_model ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = FAIL ;
    if ( host_ptr->samples )
    {
        /* Start by creating a set of sensor groups based on sample data
         * and specifically sensor type and save those groups in the database */
        if ( ( rc = ipmi_create_groups ( host_ptr ) ) == PASS )
        {
            /* add all the sensors to hwmon and save that in the database */
            if ( ( rc = ipmi_create_sensors ( host_ptr ) ) == PASS )
            {
                /* add the sensors to the groups and save that in the database */
                rc = ipmi_group_sensors ( host_ptr  );
            }
        }
    }
    else
    {
        rc = FAIL_NO_DATA ;
        elog ("%s failed sensor sample model create ; no sensor samples\n", host_ptr->hostname.c_str() );
    }
    return(rc);
}


/******************************************************************************
 *
 * Name       : ipmi_create_quanta_model
 *
 * Description: Create a static Quanta sever sensor group model.
 *
 ******************************************************************************/

int hwmonHostClass::ipmi_create_quanta_model ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int status = PASS ;
    int     rc = PASS ;

    if ( host_ptr )
    {
        if ( host_ptr->quanta_server == true )
        {
            rc = ipmi_add_group ( host_ptr , DISCRETE, "fan" , HWMON_CANNED_GROUP__FANS, "server fans", "show /SYS/fan");
            if (( rc ) && ( !status )) status = rc ;

            rc = ipmi_add_group ( host_ptr , DISCRETE, "fan" , HWMON_CANNED_GROUP__FANS, "power supply fans", "show /SYS/fan");
            if (( rc ) && ( !status )) status = rc ;

            rc = ipmi_add_group ( host_ptr , DISCRETE, "power" , HWMON_CANNED_GROUP__POWER, "server power", "show /SYS/powerSupply");
            if (( rc ) && ( !status )) status = rc ;

            rc = ipmi_add_group ( host_ptr , DISCRETE, "temperature" , HWMON_CANNED_GROUP__TEMP, "server temperature", "show /SYS/temperature");
            if (( rc ) && ( !status )) status = rc ;

            rc = ipmi_add_group ( host_ptr , DISCRETE, "voltage" , HWMON_CANNED_GROUP__VOLT, "server voltage", "show /SYS/voltage");
            if (( rc ) && ( !status )) status = rc ;
        }
    }
    return (status);
}

int hwmonHostClass::ipmi_delete_sensor_model ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;

    if ( host_ptr->relearn_retry_counter == 0 )
    {
        ilog ("%s ... saving group customizations\n",
                  host_ptr->hostname.c_str());
        this->save_model_attributes ( host_ptr );

        ilog ("%s ... clearing existing assertions\n",
                  host_ptr->hostname.c_str());
        this->clear_bm_assertions ( host_ptr );

        ilog ("%s ... deleting sensor model\n",
                  host_ptr->hostname.c_str());
    }

    /* Delete the groups from the end to the start.
     * If there is a failure then exit and the caller will retry.
     */
    if ( host_ptr->groups )
    {
        for ( int g = host_ptr->groups-1 ;
                      host_ptr->groups != 0 ;
                      host_ptr->groups-- , g-- )
        {
            daemon_signal_hdlr ();
            int rc_temp = hwmonHttp_del_group ( host_ptr->hostname,
                                                host_ptr->event,
                                                host_ptr->group[g] );
            if ( rc_temp )
            {
                elog ("%s %s group delete failed (rc:%d) (%d)\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->group[g].group_name.c_str(),
                          rc_temp, g );
                host_ptr->relearn_retry_counter++ ;
                return (rc_temp);
            }
            else
            {
                blog ("%s %s (index:%d)\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->group[g].group_name.c_str(), g );

                if ( host_ptr->group[g].timer.init == TIMER_INIT_SIGNATURE )
                {
                    mtcTimer_reset ( host_ptr->group[g].timer );
                }
                hwmonGroup_init ( host_ptr->hostname, &host_ptr->group[g]);
            }
        }
    }

    /* Delete the sensors from the end to the start.
     * If there is a failure then exit and the caller will retry.
     */
    if ( host_ptr->sensors )
    {
        for ( int s = host_ptr->sensors-1 ;
                      host_ptr->sensors != 0 ;
                      host_ptr->sensors-- , s-- )
        {
            daemon_signal_hdlr ();
            int rc_temp = hwmonHttp_del_sensor ( host_ptr->hostname,
                                                 host_ptr->event,
                                                 host_ptr->sensor[s] );
            if ( rc_temp )
            {
                elog ("%s %s sensor delete failed (rc:%d) (%d)\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sensor[s].sensorname.c_str(),
                          rc_temp, s );
                host_ptr->relearn_retry_counter++ ;
                return (rc_temp);
            }
            else
            {
                blog ("%s %s (index:%d)\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sensor[s].sensorname.c_str(), s );

                hwmonSensor_init ( host_ptr->hostname, &host_ptr->sensor[s]);
                sensor_data_init ( host_ptr->sample[s] );

                if ( host_ptr->sensors == 1 )
                {
                    host_ptr->quanta_server = false ;
                    host_ptr->sensors =
                    host_ptr->samples =
                    host_ptr->profile_sensor_checksum =
                    host_ptr->sample_sensor_checksum =
                    host_ptr->last_sample_sensor_checksum = 0 ;
                    break ;
                }
            }
        }
    }

    if (( host_ptr->sensors == 0 ) && ( host_ptr->groups == 0 ))
    {
        plog ("%s sensor model deleted\n", host_ptr->hostname.c_str() );
    }
    else
    {
        elog ("%s sensor model delete failed (%d:%d)\n",
                  host_ptr->hostname.c_str(),
                  host_ptr->groups,
                  host_ptr->sensors );

        rc = FAIL ;
    }
    return (rc);
}

/* *************************************************************************
 *
 * Name       : ipmi_load_sensor_model
 *
 * Description: Called from the add_handler to load sensors and groups
 *              for the specified host from the sysinv database.
 *
 * Warnings   : Will return a failure and swerr if called when with an
 *              already loaded sensor profile.
 *
 * Assumptions: Inservice sensor model reprovisioning is done with
 *              ipmi_delete_sensor_model and  ipmi_create_sensor_model API.
 *
 *
 * Scope      : private hwmonHostClass
 *
 * Parameters : host_ptr
 *
 * Returns    : TODO: handle modify errors better.
 *
 * *************************************************************************/
int hwmonHostClass::ipmi_load_sensor_model ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc ;

    if (( host_ptr->sensors ) || ( host_ptr->groups ))
    {
       elog ("%s already has %d sensors across %d groups loaded - reloading\n",
                  host_ptr->hostname.c_str(),
                  host_ptr->sensors,
                  host_ptr->groups );

        this->hwmon_del_sensors ( host_ptr );
        this->hwmon_del_groups ( host_ptr );

        rc = FAIL_INVALID_OPERATION ;
    }
    else
    {
        /*   Load aleady provisioned sensors from the database
         *   into host_ptr->sensor list.
         *
         *   Warning: This is a blocking call and always has been.
         */
        rc = hwmonHttp_load_sensors ( host_ptr->hostname, host_ptr->event );
        if ( rc == PASS )
        {
            daemon_signal_hdlr (); /* service the signals */

            if ( host_ptr->sensors != 0 )
            {
                /* Load aleady provisioned groups from the database
                 * into host_ptr->group list */
                rc = hwmonHttp_load_groups ( host_ptr->hostname, host_ptr->event );
                if ( rc == PASS )
                {
                    /* update sample severity to avoid state change
                     * from fail to ok to fail over a process restart */
                    for ( int s = 0 ; s < host_ptr->sensors ; s++ )
                    {
                        host_ptr->sensor[s].sample_severity    = get_severity(host_ptr->sensor[s].status) ;
                        host_ptr->sensor[s].sample_status      =
                        host_ptr->sensor[s].sample_status_last = host_ptr->sensor[s].status ;
                    }
                    rc = hwmonHostClass::hwmon_group_sensors ( host_ptr );
                    if ( rc == PASS )
                    {
                        blog ("%s sensors grouped\n", host_ptr->hostname.c_str());
                    }
                    else
                    {
                        wlog ("%s sensor grouping failed (in hwmon) (rc:%d)\n", host_ptr->hostname.c_str(), rc );
                    }
                }
                else
                {
                    wlog ("%s sensor group load failed (from sysinv) (rc:%d)\n", host_ptr->hostname.c_str(), rc );
                }
            }
        }
        else
        {
            wlog ("%s sensors load failed (from sysinv) (rc:%d)\n", host_ptr->hostname.c_str(), rc );
        }
    }

    if ( rc == PASS )
    {
        if (( host_ptr->sensors ) && ( host_ptr->groups ))
        {
            ilog ("%s has %d sensors across %d groups (in sysinv)\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->sensors,
                      host_ptr->groups );

            /* initialize sensor data */
            for ( int i = 0 ; i < host_ptr->sensors ; ++i )
            {
                host_ptr->sensor[i].severity = get_severity ( host_ptr->sensor[i].status );
            }

            host_ptr->profile_sensor_checksum =
            checksum_sensor_profile ( host_ptr->hostname,
                                      host_ptr->sensors,
                                     &host_ptr->sensor[0]);

            ilog ("%s database profile checksum : %04x (%d sensors)\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->profile_sensor_checksum,
                          host_ptr->sensors);

            if (((  host_ptr->profile_sensor_checksum == QUANTA_SENSOR_PROFILE_CHECKSUM ) ||
                 (  host_ptr->profile_sensor_checksum == QUANTA_SENSOR_PROFILE_CHECKSUM_13_53 )) &&
                (( host_ptr->sensors == QUANTA_PROFILE_SENSORS ) || (QUANTA_PROFILE_SENSORS_REVISED_1)) &&
                 ( host_ptr->groups == QUANTA_SENSOR_GROUPS ))
            {
                ilog ("%s ---------------------------------------------\n", host_ptr->hostname.c_str());
                ilog ("%s is a Quanta server with legacy sensor profile\n", host_ptr->hostname.c_str());
                ilog ("%s ---------------------------------------------\n", host_ptr->hostname.c_str());
                host_ptr->quanta_server = true ;
            }
            else
            {
                ilog ("%s has unique sensor model\n", host_ptr->hostname.c_str());
            }
        }
        else
        {
            /* Incomplete or no sensor/group model found in database */
            ilog ("%s no valid sensor model found (in sysinv) (sensors:%d groups:%d)\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->sensors,
                      host_ptr->groups );

            if (( host_ptr->sensors ) || (host_ptr->groups ))
            {
                wlog ("%s has a corrupt sensor profile ; deleting ...\n", host_ptr->hostname.c_str());
                ipmi_delete_sensor_model ( host_ptr );
            }
        }
    }
    return (rc);
}
