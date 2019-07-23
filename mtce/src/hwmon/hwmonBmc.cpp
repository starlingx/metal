/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * StarlingX Cloud Hardware Monitor BMC Sensor Utilities
 */

#include <stdlib.h>
#include "json-c/json.h"

#include "daemon_ini.h"   /* for ... parse_ini and MATCH              */
#include "nodeBase.h"     /* for ... mtce common definitions          */
#include "nodeUtil.h"     /* for ... mtce common utilities            */
#include "jsonUtil.h"     /* for ... json string parse utilities      */
#include "hwmonUtil.h"    /* for ... get_severity                     */
#include "hwmonBmc.h"    /* for ... this module header               */
#include "hwmonHttp.h"    /* for ... hwmonHttp_mod_sensor             */
#include "hwmonClass.h"   /* for ... service class definition         */
#include "hwmonGroup.h"   /* for ... bmc_get_groupenum               */
#include "hwmonThreads.h" /* for ... BMC_JSON__SENSORS_LABEL     */

#ifdef WANT_CORR_STATUS
#define CORR_STATUS_MAX (6)
static const char *_bmc_status_desc[] =
{
   "ok", // all good - ok
   "nc", // Non-Critical
   "cr", // Critical
   "nr", // Non-Recoverable
   "ns", // Not Specified
   "na", // Not Applicable
};
#endif

#ifdef WANT_CORR_EXTENDED_STATUS
#define CORR_EXTENDED_STATUS_MAX (6)
static const char *_bmc_status_extended_desc[] =
{
   "lnr", // Lower Non-Recoverable
   "unr", // Upper Non-Recoverable
   "ucr", // Upper Critical
   "lcr", // Lower Critical
   "unc", // Upper Non-Critical
   "lnc", // Lower Non-Critical
};
#endif

/*****************************************************************************
 *
 * Name        : sensor_data_init
 *
 * Descrfiption: Initialize an bmc sample data structure
 *
 *****************************************************************************/

void sensor_data_init ( sensor_data_type & data )
{
    data.name.clear();
    data.status.clear();
    data.value.clear();
    data.unit.clear();
    data.lnr.clear();
    data.lcr.clear();
    data.lnc.clear();
    data.unr.clear();
    data.ucr.clear();
    data.unc.clear();
    data.group_enum = HWMON_CANNED_GROUP__NULL ;
    data.found=false ;
    data.ignore=false ;
}

/*****************************************************************************
 *
 * Name        : sensor_data_print
 *
 * Descrfiption: Print an bmc sample data structure
 *
 *****************************************************************************/

void sensor_data_print ( const sensor_data_type & data )
{
    blog3 ("%s is %s : %s (%s) %s %s %s %s %s %s %s\n",
               data.name.c_str(),
               data.status.c_str(),
               data.value.c_str(),
               data.unit.c_str(),
               data.lnr.c_str(),
               data.lcr.c_str(),
               data.lnc.c_str(),
               data.unr.c_str(),
               data.ucr.c_str(),
               data.unc.c_str(),
               data.ignore ? "ignored" : "" );
}

/****************************************************************************
 *
 * Name       : bmc_load_json_sensor
 *
 * Purpose    : Load a json formatted sensor data string into the specifie
 *              sensor data element
 *
 *****************************************************************************/

int bmc_load_json_sensor ( sensor_data_type & sensor_data , string json_sensor_data )
{
    int rc = FAIL_KEY_VALUE_PARSE ;
    // ilog ("sensor data:%s\n", json_sensor_data.c_str() );

    struct json_object *raw_obj = json_tokener_parse( (char*)json_sensor_data.data());
    if ( raw_obj )
    {
        sensor_data.name   = jsonUtil_get_key_value_string ( raw_obj, "n" ) ;
        sensor_data.value  = jsonUtil_get_key_value_string ( raw_obj, "v" ) ;
        sensor_data.unit   = jsonUtil_get_key_value_string ( raw_obj, "u" ) ;
        sensor_data.status = jsonUtil_get_key_value_string ( raw_obj, "s" ) ;
        sensor_data.lnr    = jsonUtil_get_key_value_string ( raw_obj, "lnr" ) ;
        sensor_data.lcr    = jsonUtil_get_key_value_string ( raw_obj, "lcr" ) ;
        sensor_data.lnc    = jsonUtil_get_key_value_string ( raw_obj, "lnc" ) ;
        sensor_data.unr    = jsonUtil_get_key_value_string ( raw_obj, "unr" ) ;
        sensor_data.ucr    = jsonUtil_get_key_value_string ( raw_obj, "ucr" ) ;
        sensor_data.unc    = jsonUtil_get_key_value_string ( raw_obj, "unc" ) ;

        sensor_data_print ( sensor_data );

        json_object_put(raw_obj);
        rc = PASS ;
    }
    return (rc);
}

/****************************************************************************
 *
 * Name       : sensor_data_copy
 *
 * Purpose    : sopy sensor sample data from one struct to another.
 *
 *****************************************************************************/

void sensor_data_copy ( sensor_data_type & from, sensor_data_type & to )
{
    to.name   = from.name   ;
    to.value  = from.value  ;
    to.unit   = from.unit   ;
    to.status = from.status ;
    to.lnr    = from.lnr    ;
    to.lcr    = from.lcr    ;
    to.lnc    = from.lnc    ;
    to.unr    = from.unr    ;
    to.ucr    = from.ucr    ;
    to.unc    = from.unc    ;
    to.ignore = from.ignore ;
}

/*****************************************************************************
 *
 * Name       : _handle_dup_sensors
 *
 * Description: Logically OR status of duplicate sensors where the highest
 *              severity takes precidence.
 *
 *              Severity order from low to high is
 *
 *              na,ns
 *              ok
 *              nc
 *              cr
 *              nr
 *
 * Returns    : True is returned if this sensor is a duplicate.
 *
 ****************************************************************************/

bool _handle_dup_sensors ( string             hostname,
                           sensor_data_type * samples_ptr,
                           int                samples,
                           sensor_data_type & this_sample )
{
    if ( samples_ptr )
    {
        for ( int i = 0 ; i < samples ; i++, samples_ptr++ )
        {
            if ( samples_ptr->name == this_sample.name )
            {
                bool update = false ;

                /* Treat 'Not Specified' as 'Not Applicable' */
                if ( samples_ptr->status == "ns" )
                    samples_ptr->status = "na" ;

                if ( this_sample.status == "na" )
                {
                    if ( samples_ptr->status != "na" )
                    {
                        ; /* current status is better than last status ; no update */
                    }
                }
                else if ( this_sample.status == "ok" )
                {
                    if ( samples_ptr->status == "na" )
                    {
                        update = true ;
                    }
                }
                else if ( this_sample.status == "nc" )
                {
                    if ( samples_ptr->status == "ok" )
                    {
                        update = true ;
                    }
                    else if ( samples_ptr->status == "na" )
                    {
                        update = true ;
                    }
                }
                else if ( this_sample.status == "cr" )
                {
                    if ( samples_ptr->status == "ok" )
                    {
                        update = true ;
                    }
                    else if ( samples_ptr->status == "na" )
                    {
                        update = true ;
                    }
                    else if ( samples_ptr->status == "nc" )
                    {
                        update = true ;
                    }
                }
                else if ( this_sample.status == "nr" )
                {
                    if ( samples_ptr->status != "nr" )
                    {
                        update = true ;
                    }
                }

                dlog ("%s %s is a duplicate sensor ; ( '%s' %c '%s')\n",
                        hostname.c_str(),
                        this_sample.name.c_str(),
                        samples_ptr->status.c_str(),
                        update ? '>' : ':',
                        this_sample.status.c_str());

                /* update the ORed status */
                if ( update )
                    samples_ptr->status = this_sample.status ;

                return (true) ;
            }
        }
    }
    return (false);
}

/*****************************************************************************
 *
 * Name       : bmc_load_sensor_samples
 *
 * Description: Load all the sensor samples into hardware mon.
 *
 ****************************************************************************/

int hwmonHostClass::bmc_load_sensor_samples ( struct hwmonHostClass::hwmon_host * host_ptr, char * msg_ptr )
{
    int rc ;

    int samples = 0 ;
    host_ptr->samples = 0 ;

    rc = jsonUtil_array_elements ( msg_ptr, BMC_JSON__SENSORS_LABEL, samples ) ;
    if ( rc == PASS )
    {
        string sensor_data ;

        jlog ("%s samples: %d:%d : %s\n", host_ptr->hostname.c_str(), samples, host_ptr->thread_extra_info.samples, msg_ptr );

        if ( samples != host_ptr->thread_extra_info.samples )
        {
            wlog ("%s sample accounting mismatch (%d:%d)\n",
                      host_ptr->hostname.c_str(),
                      samples, host_ptr->thread_extra_info.samples );
        }
        if ( samples >= MAX_HOST_SENSORS )
        {
            wlog ("%s too many sensors (%d); must be error condition ; rejecting\n",
                      host_ptr->hostname.c_str(),
                      samples );
            return (FAIL_OUT_OF_RANGE);
        }

        /****************************************************************************
         * Load samples into hwmond sample sensor list.
         *
         * Warning  : Sample readings from a server that is powered off can
         *            be misleading. The unit type can change. To handle this we
         *            filter out sensors that are not already in the list AND don't
         *            fit into a valid group.
         *
         ****************************************************************************/
        for ( int index = 0 ; index < samples ; index++ )
        {
            sensor_data.clear();
            rc = jsonUtil_get_array_idx ( msg_ptr, BMC_JSON__SENSORS_LABEL, index, sensor_data ) ;
            if ( rc == PASS )
            {
                if ( bmc_load_json_sensor ( host_ptr->sample[host_ptr->samples], sensor_data ) == PASS )
                {
                    bool found = false ;

                    if ( host_ptr->samples > 0 )
                    {
                        if ( _handle_dup_sensors ( host_ptr->hostname,
                                                  &host_ptr->sample[0],
                                                   host_ptr->samples,
                                                   host_ptr->sample[host_ptr->samples]) == true )
                        {
                            continue ;
                        }
                    }
                    for ( int s = 0 ; s < host_ptr->sensors ; ++s )
                    {
                        if ( !host_ptr->sensor[s].sensorname.compare(host_ptr->sample[host_ptr->samples].name))
                        {
                            found = true ;
                            break ;
                        }
                    }
                    if ( found == false )
                    {
                        /* Drop any sensors that don't fall into a valid group */
                        host_ptr->sample[host_ptr->samples].group_enum =
                        bmc_get_groupenum ( host_ptr->hostname,
                                             host_ptr->sample[host_ptr->samples].unit,
                                             host_ptr->sample[host_ptr->samples].name);

                        if ( host_ptr->sample[host_ptr->samples].group_enum == HWMON_CANNED_GROUP__NULL )
                        {
                            blog3 ("%s ignore sensor : %s\n", host_ptr->hostname.c_str(), sensor_data.c_str());
                            continue ;
                        }
                    }
                    blog2 ("%s  valid sensor : %s\n", host_ptr->hostname.c_str(), sensor_data.c_str());
                }
                else
                {
                    wlog ("%s invalid sensor data:%s\n", host_ptr->hostname.c_str(), sensor_data.c_str());
                    host_ptr->bmc_thread_info.status_string =
                    "failed to load sensor sample data from incoming json string" ;
                    host_ptr->bmc_thread_info.status = FAIL_JSON_PARSE ;
                    break ;
                }
            }
            else
            {
                host_ptr->bmc_thread_info.status_string = "sensor data parse error for index '" ;
                host_ptr->bmc_thread_info.status_string.append(itos(host_ptr->thread_extra_info.samples));
                host_ptr->bmc_thread_info.status_string.append("'");
                host_ptr->bmc_thread_info.status = FAIL_JSON_PARSE ;
                break ;
            }
            host_ptr->samples++ ;
        } /* for end */
        blog1 ("%s provided %d sensor samples\n", host_ptr->hostname.c_str(), host_ptr->samples);
    }
    else
    {
        host_ptr->bmc_thread_info.status_string = "failed to find '" ;
        host_ptr->bmc_thread_info.status_string.append(BMC_JSON__SENSORS_LABEL);
        host_ptr->bmc_thread_info.status_string.append("' label") ;
        host_ptr->bmc_thread_info.status = FAIL_JSON_PARSE ;
    }
    return (host_ptr->bmc_thread_info.status);
}

void _generate_transient_log ( sensor_type * sensor_ptr )
{
    /* debounced */
    string reason = "'" + sensor_ptr->status + "' but got a transient '" + sensor_ptr->sample_status_last + "' reading" ;

    hwmonLog ( sensor_ptr->hostname,
               HWMON_ALARM_ID__SENSOR,
               FM_ALARM_SEVERITY_WARNING,
               sensor_ptr->sensorname,
               reason );
}


int hwmonHostClass::bmc_update_sensors ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    /* Mark all sensors as not being updated only to get changed below when it is updated.
     * This allows us to quickly identify what sensors are missing */
    for ( int i = 0 ; i < host_ptr->sensors ; ++i )
    {
        host_ptr->sensor[i].updated = false ;
    }
    for ( int i = 0 ; i < host_ptr->sensors ; ++i )
    {
        for ( int j = 0 ; j < host_ptr->samples ; j++ )
        {
            if ( host_ptr->sensor[i].sensorname.compare(host_ptr->sample[j].name) == 0 )
            {
                host_ptr->sensor[i].updated = true ;

                blog1 ("%s %s curr:%s this:%s last:%s\n",
                               host_ptr->hostname.c_str(),
                               host_ptr->sensor[i].sensorname.c_str(),
                               host_ptr->sensor[i].status.c_str(),
                               host_ptr->sample[j].status.c_str(),
                               host_ptr->sensor[i].sample_status_last.c_str());

#ifdef WANT_FIT_TESTING
                /* Handle Fault Insertion Test Requests ...
                 * for host and sensor with FIT specified status
                 */
                string fit_status = "" ;
                if ( daemon_want_fit ( FIT_CODE__HWMON__SENSOR_STATUS, host_ptr->hostname, host_ptr->sensor[i].sensorname, fit_status ) )
                {
                    slog ("%s FIT %s sensor with '%s' status (was %s)\n",
                              host_ptr->hostname.c_str(),
                              host_ptr->sensor[i].sensorname.c_str(),
                              fit_status.c_str(),
                              host_ptr->sensor[i].status.c_str());

                    /* override existing status */
                    host_ptr->sample[j].status = fit_status ;
                }
#endif

                /*************************************************************
                 ***************  Sensor Debounce Control Start **************
                 *************************************************************
                 *
                 * If the last severity is the same as this severity then
                 * the state change is persistent ; no debounce.
                 * If the current and last readings are different and the
                 * debounce bool indicating we are in debounce mode.
                 *
                 *************************************************************/
                if ( host_ptr->sensor_query_count > START_DEBOUCE_COUNT )
                {
                    /* ***** Fix this up once verified */

                    /* if the current sensor state is the same as the current
                     * sensor sample then don't clear the debounce count as it
                     * might be indicating that there was a transient */
                    if ( host_ptr->sensor[i].sample_status.compare(host_ptr->sample[j].status) == 0 )
                       ;

                    /* If we get 2 same readings in a row then this is not
                     * a transient or flapper */
                    else if ( host_ptr->sensor[i].sample_status_last.compare(host_ptr->sample[j].status) == 0 )
                       host_ptr->sensor[i].debounce_count = 0 ;

                    /* if this sample reading is different from the last
                     * then this is a transient candidate */
                    else if ( host_ptr->sensor[i].sample_status_last.compare(host_ptr->sample[j].status) )
                    {
                        host_ptr->sensor[i].debounce_count++ ;
                        if ( host_ptr->sensor[i].debounce_count > 1 )
                        {
                            /* do not generate logs for suppressed sensors */
                            if ( host_ptr->sensor[i].suppress == false )
                            {
                                /* debounced */
                                string reason = "'" ;
                                reason.append(host_ptr->sensor[i].status) ;
                                reason.append("' but saw changing readings '") ;
                                reason.append(host_ptr->sensor[i].sample_status_last);
                                reason.append("' to '");
                                reason.append(host_ptr->sample[j].status);
                                reason.append("'");

                                hwmonLog ( host_ptr->hostname,
                                           HWMON_ALARM_ID__SENSOR,
                                           FM_ALARM_SEVERITY_WARNING,
                                           host_ptr->sensor[i].sensorname,
                                           reason );

                                ilog ("%s %s is '%s:%s' status ; flapping '%s' then '%s'\n",
                                          host_ptr->hostname.c_str(),
                                          host_ptr->sensor[i].sensorname.c_str(),
                                          host_ptr->sensor[i].status.c_str(),
                                          host_ptr->sensor[i].sample_status.c_str(),
                                          host_ptr->sensor[i].sample_status_last.c_str(),
                                          host_ptr->sample[j].status.c_str());
                            }
                        }
                        host_ptr->sensor[i].sample_status_last = host_ptr->sample[j].status ;
                        break ; // continue ;
                    }
                }

                /***************** Sensor Debounce Handling ******************/
                host_ptr->sensor[i].want_debounce_log_if_ok = false ;
                if ( host_ptr->sensor[i].debounce_count == 1 )
                {
                    /* do not generate logs for suppressed sensors */
                    if ( host_ptr->sensor[i].suppress == false )
                    {
                        if ( host_ptr->sensor[i].sample_status_last.compare("na"))
                        {
                            host_ptr->sensor[i].want_debounce_log_if_ok = true ;
                        }
                        ilog ("%s %s is '%s' but saw a transient '%s' reading\n",
                                  host_ptr->hostname.c_str(),
                                  host_ptr->sensor[i].sensorname.c_str(),
                                  host_ptr->sensor[i].sample_status.c_str(),
                                  host_ptr->sensor[i].sample_status_last.c_str());
                    }
                }

                host_ptr->sensor[i].debounce_count = 0;

                /*************************************************************/
                /*******************  Sensor Debounce End ********************/
                /*************************************************************/

                /* update sample status now that we are beyond the debounce check.
                 * The last status is updated at the end of this condition */
                host_ptr->sensor[i].sample_status = host_ptr->sample[j].status ;

                /* if we get a match and its status is 'na' then just mark it as 'offline' */
                if ( host_ptr->sample[j].status.compare("na") == 0 )
                {
                    host_ptr->sensor[i].sample_severity =
                    get_bmc_severity (host_ptr->sample[j].status);
                }
                else if ( host_ptr->sample[j].unit.compare(DISCRETE))
                {
                    /* not a descrete sensor */

                    /* get severity level */
                    host_ptr->sensor[i].sample_severity =
                    get_bmc_severity (host_ptr->sample[j].status);

                    /* Check to see if we need to generate the transient log.
                     * Only generate it if want_debounce_log_if_ok is true and
                     * the reading is ok */
                    if (( host_ptr->sensor[i].want_debounce_log_if_ok == true ) &&
                        ( host_ptr->sensor[i].sample_severity == HWMON_SEVERITY_GOOD ))
                    {
                        _generate_transient_log ( &host_ptr->sensor[i] );
                    }

                    /* Minor severity from get_bmc_severity means
                     * that the severity status is unexpected */
                    if ( host_ptr->sensor[i].sample_severity == HWMON_SEVERITY_MINOR )
                    {
                        if ( host_ptr->sensor[i].status.compare("minor") == 0 )
                        {
                            /* only print this log on the first state transition */
                            wlog ("%s '%s' unexpected bmc sensor reading '%s'\n",
                                      host_ptr->hostname.c_str(),
                                      host_ptr->sensor[i].sensorname.c_str(),
                                      host_ptr->sample[j].status.c_str());
                        }
                    }
                }

                /*
                 * The Quanta Air Frame server's power sensors are reported as discrete sensors.
                 * In order to maintain backward compatibility for Quanta we need to search for
                 * these sensor status and prop that status to the sensor list.
                 */
                else if ( host_ptr->quanta_server )
                {
                    /* otherwise if the status is not prefixed with a 0x then the
                     * reading is unknown so set its severity to minor as we do
                     * for all unknown sensor readings */
                    if ( host_ptr->sample[j].status.find("0x", 0 ) == std::string::npos )
                    {
                        wlog ("%s '%s' unexpected discrete status reading '%s'\n",
                                  host_ptr->hostname.c_str(),
                                  host_ptr->sensor[i].sensorname.c_str(),
                                  host_ptr->sample[j].status.c_str());

                        host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_MINOR ;
                    }
                    /* otherwise correlate the status against the sensors we care about */
                    else
                    {
                        unsigned short bmc_status = (unsigned short)strtol((char*)host_ptr->sample[j].status.data(), NULL, 0 );

                        /* interpret discrete sensor readings for known Quanta discrete
                         * sensors that need to be represented with a correlated status */
                        blog3 ("%s '%s' discrete sensor found - need to update status %s:0x%04x ...\n",
                                   host_ptr->hostname.c_str(),
                                   host_ptr->sensor[i].sensorname.c_str(),
                                   host_ptr->sample[j].status.c_str(),
                                   bmc_status );

                        /* treat thermal trip sensors failures as Major.
                         * A good reading is 0x0080 */
                        if (( host_ptr->sensor[i].sensorname.compare("PCH Thermal Trip") == 0 ) ||
                            ( host_ptr->sensor[i].sensorname.compare("MB Thermal Trip") == 0 ))
                        {
                            if ( bmc_status == 0x0080 )
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_GOOD ;
                                if ( host_ptr->sensor[i].want_debounce_log_if_ok == true )
                                {
                                    _generate_transient_log ( &host_ptr->sensor[i] );
                                }
                            }
                            else
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_MAJOR ;
                            }
                        }
                        else if ( host_ptr->sensor[i].sensorname.compare("PSU Redundancy") == 0 )
                        {
                            if ( bmc_status == 0x0180 ) /* Fully Redundant */
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_GOOD ;
                                if ( host_ptr->sensor[i].want_debounce_log_if_ok == true )
                                {
                                    _generate_transient_log ( &host_ptr->sensor[i] );
                                }
                            }
                            else if ( bmc_status == 0x0280 ) /* Redundancy Lost */
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_MAJOR ;
                            }
                            else
                            {
                                wlog ("%s '%s' unexpected discrete status reading '0x%04x'\n",
                                          host_ptr->hostname.c_str(),
                                          host_ptr->sensor[i].sensorname.c_str(),
                                          bmc_status);

                                sensor_data_print (host_ptr->sample[j]);
                                blog3 ("%s ... %s\n", host_ptr->hostname.c_str(), host_ptr->bmc_thread_info.data.c_str());

                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_MINOR ;
                            }
                        }
                        else if (( host_ptr->sensor[i].sensorname.compare("PSU1 Status") == 0 ) ||
                                 ( host_ptr->sensor[i].sensorname.compare("PSU2 Status") == 0 ))
                        {
#define STATUS_BIT_MASK        (0x3F00)
#define NO_PRESENCE_DETECTED   (0x0000)
#define PRESENCE_DETECTED      (0x0100)
#define FAILURE_DETECTED       (0x0200)
#define PREDICTIVE_FAILURE     (0x0400)
#define INPUT_LOST_ACDC        (0x0800)
#define INPUT_LOST_OOR         (0x1000)
#define INPUT_OOR_PRESENT      (0x2000)

                            /* Presence Detected and ok */
                            // if ( bmc_status == 0x0180 )
                            if ( (bmc_status&STATUS_BIT_MASK) == PRESENCE_DETECTED )
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_GOOD ;
                                if ( host_ptr->sensor[i].want_debounce_log_if_ok == true )
                                {
                                    _generate_transient_log ( &host_ptr->sensor[i] );
                                }
                            }

                            /* No Presence Detect */
                            // else if (( bmc_status == 0x0080 ) || ( bmc_status == 0x0000 ))
                            else if ( (bmc_status&STATUS_BIT_MASK) == NO_PRESENCE_DETECTED )
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_MINOR ;
                            }

                            /* Failure Detected with anything else */
                            /* 0x02xx */
                            else if ( (bmc_status&STATUS_BIT_MASK) & FAILURE_DETECTED )
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_CRITICAL ;
                            }

                            /* Presence Detected & Predictive Failure */
                            //else if ( (bmc_status&STATUS_BIT_MASK) == ( PRESENCE_DETECTED | PREDICTIVE_FAILURE ))
                            // TODO: Fix this ...
                            else if ( ( bmc_status == 0x1580 ) || /* Presence Detected & Predictive Failure & Input Lost Or Out Of Range */
                                      ( bmc_status == 0x2580 ) || /* Presence Detected & Predictive Failure & Input Out Of Range         */
                                      ( bmc_status == 0x3580 ) || /* Presence Detected & Predictive Failure & both of the above          */
                                      ( bmc_status == 0x0580 ) || /* Presence Detected & Predictive Failure */
                                      ( bmc_status == 0x0980 ) || /* Presence Detected & Power Supply Input Lost */
                                      ( bmc_status == 0x0d80 ) )  /* Presence Detected & Power Supply Input Out Of Range */
                            {
                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_MAJOR ;
                            }

                            else
                            {
                                wlog ("%s '%s' unexpected discrete status reading '0x%04x'\n",
                                          host_ptr->hostname.c_str(),
                                          host_ptr->sensor[i].sensorname.c_str(),
                                          bmc_status);

                                sensor_data_print (host_ptr->sample[j]);
                                blog3 ("%s ... %s\n", host_ptr->hostname.c_str(), host_ptr->bmc_thread_info.data.c_str());

                                host_ptr->sensor[i].sample_severity = HWMON_SEVERITY_MINOR ;
                            }
                        }
                    }
                }

                /* update last status AFTER the severity interpretation so
                 * that debouce logging can report the transient status correctly */
                host_ptr->sensor[i].sample_status_last = host_ptr->sample[j].status ;

                /* we already found the sample so got to the next sensor */
                break ;
            }
        } /* end for loop over sensor samples */

        if ( host_ptr->sensor[i].updated == true )
        {
            if ( host_ptr->sensor[i].state.compare("enabled") )
            {
                if ( hwmonHttp_mod_sensor ( host_ptr->hostname,
                                       host_ptr->event ,
                                       host_ptr->sensor[i].uuid,
                                       "state", "enabled" ) == PASS )
                {
                    host_ptr->sensor[i].state = "enabled" ;
                }
            }
        }

        /* Take sensors that had no status in this sample set minor */
        else
        {
#ifdef WANT_FAIL_ON_NO_UPDATE
            /* if the sensor was not already enabled at least once then fail
             * it since we have never got any good data from it */
            if ( host_ptr->sensor[i].state.compare("failed"))
            {
                if ( hwmonHttp_mod_sensor ( host_ptr->hostname,
                                       host_ptr->event ,
                                       host_ptr->sensor[i].uuid,
                                       "state", "failed" ) == PASS )
                {
                    host_ptr->sensor[i].state = "failed" ;
                }
            }
#endif
            if ( host_ptr->sensor[i].state.compare("offline"))
            {
                if ( hwmonHttp_mod_sensor ( host_ptr->hostname,
                                       host_ptr->event ,
                                       host_ptr->sensor[i].uuid,
                                       "state", "offline" ) == PASS )
                {
                    host_ptr->sensor[i].state = "offline" ;
                }

                // if alarm is raised ===> clear it
                if ( host_ptr->sensor[i].alarmed == true )
                {
                    clear_asserted_alarm ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, &host_ptr->sensor[i], REASON_OFFLINE );
                }
            }
            else
            {
                blog ("%s '%s' sensor status not found ; already minor\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sensor[i].sensorname.c_str());
            }
        }
    }
    return (PASS);
}
