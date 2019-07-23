/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Wind River Titanium Cloud Hardware Monitor Sensor Group Utilities
 */

#include "daemon_common.h" /* for ... daemon utilities                 */
#include "daemon_ini.h"    /* for ... parse_ini and MATCH              */
#include "nodeBase.h"      /* for ... mtce common definitions          */
#include "jsonUtil.h"      /* for ... json utilitiies                  */
#include "nodeUtil.h"      /* for ... mtce common utilities            */
#include "hwmon.h"         /* for ... canned_group_enum                */
#include "hwmonClass.h"    /* for ... service class definition         */
#include "hwmonHttp.h"     /* for ... http podule header               */
#include "hwmonGroup.h"    /* for ... MAX_GROUPING_ERRORS              */
#include "hwmonSensor.h"   /* for ... groupSensors_print               */
#include "hwmonAlarm.h"    /* for ... hwmonAlarm                       */

/* TODO: After initial inspection move all group utilities
 *       from hwmonSensor.cpp / h
 *       into hwmonGroup.cpp / h
 *
 */


/* BMC Sensor Types
 * Each Sensor group gets a unique name from this list
 *
 * Is taken from bmc private file
 */

static const char * canned_group__null =
{
    "null"
};

static const char * canned_group__fans =
{
    "RPM,% RPM,CFM,% CFM"
};

static const char * canned_group__temp =
{
    "degrees"
};

static const char * canned_group__voltage =
{
    "Volts"
};

static const char * canned_group__power =
{
    "Amps,% Amps,Watts,% Watts,Joules,Coulombs"
};

static const char * canned_group__usage =
{
    "TBD"
};

#ifdef WANT_MORE_GROUPS

static const char * canned_group__time =
{
    "microsecond,millisecond,second,minute,hour,day,week"
};

static const char * canned_group__msg =
{
    "overflow,underrun,collision,packets,messages,characters"
};

static const char * canned_group__memory =
{
    "bit,kilobit,megabit,gigabit,byte,kilobyte,megabyte,gigabyte,word,dword,qword,line,hit,miss"
};

static const char * canned_group__errors =
{
    "error,correctable error,uncorrectable error"
};

static const char * canned_group__clocks =
{
    "Hz,Hertz"
};

static const char * canned_group__misc =
{
    "unspecified"
};

#endif

/* Other types from bmc source ********

    "VA",
    "Nits",
    "lumen",
    "lux",
    "Candela",
    "kPa",
    "PSI",
    "Newton",
    "mil",
    "inches", "feet",
    "cu in", "cu feet",
    "mm", "cm", "m", "cu cm", "cu m",
    "liters", "fluid ounce",
    "radians", "steradians",
    "revolutions", "cycles",
    "gravities",
    "ounce", "pound",
    "ft-lb", "oz-in", "gauss",
    "gilberts", "henry",
    "millihenry", "farad",
    "microfarad",
    "ohms",
    "siemens",
    "mole",
    "becquerel",
    "PPM",
    "reserved",
    "Decibels",
    "DbA", "DbC",
    "gray", "sievert",
    "color temp deg K",
    "retry",
    "reset",

********************************************/


/*****************************************************************************
 *
 * Name       : canned_group_array[]      (construct)
 *                canned_group_type       (type definition)
 *                  canned_group_enum     (enumerated identifier)
 *                  canned_group_<name>[] (list of alowable group unit types)
 *
 * Description: This is an array of pre-created (or canned) groups.
 *              Each group has a name that will be the group name displayed
 *              on the GUI or in the CLI. Each canned group then also has an
 *              identifier that is added to the sensor_group_type struct and
 *              used to indicate that group type and then for sensor allocaiton
 *              purposes a list of unit names that qualify a sensor for being
 *              part of that group.
 *
 *              For instance all sensors with a CFM or RPM unit type are
 *              assigned to the HWMON_CANNED_GROUP__FANS group.
 *
 *****************************************************************************/

typedef struct
{
    const char        * group_type  ;
    const char        * group_name  ;
    canned_group_enum   group_enum  ;
    const char        * group_units ;
} canned_group_type ;

const canned_group_type canned_group_array [HWMON_CANNED_GROUPS] =
{
   /* group type      group name              group enum                 sensor unit types
      ----------      ------------------      ------------------------   --------------------*/
    {"null",         HWMON_GROUP_NAME__NULL,  HWMON_CANNED_GROUP__NULL,  canned_group__null   },
    {"fan",          HWMON_GROUP_NAME__FANS,  HWMON_CANNED_GROUP__FANS,  canned_group__fans   },
    {"temperature",  HWMON_GROUP_NAME__TEMP,  HWMON_CANNED_GROUP__TEMP,  canned_group__temp   },
    {"voltage",      HWMON_GROUP_NAME__VOLTS, HWMON_CANNED_GROUP__VOLT,  canned_group__voltage},
    {"power",        HWMON_GROUP_NAME__POWER, HWMON_CANNED_GROUP__POWER, canned_group__power  },
    {"usage",        HWMON_GROUP_NAME__USAGE, HWMON_CANNED_GROUP__USAGE, canned_group__usage  },

#ifdef WANT_MORE_GROUPS

    /* Enable these when we have discrete correlation */
    {"memory",       HWMON_GROUP_NAME__MEMORY  , HWMON_CANNED_GROUP__MEMORY,canned_group__memory },
    {"clocks",       HWMON_GROUP_NAME__CLOCKS  , HWMON_CANNED_GROUP__CLOCKS,canned_group__clocks },
    {"errors",       HWMON_GROUP_NAME__ERRORS  , HWMON_CANNED_GROUP__ERRORS,canned_group__errors },
    {"messages",     HWMON_GROUP_NAME__MESSAGES, HWMON_CANNED_GROUP__MSG,   canned_group__msg    },
    {"time",         HWMON_GROUP_NAME__TIME    , HWMON_CANNED_GROUP__TIME,  canned_group__time   },
    {"miscellaneous",HWMON_GROUP_NAME__MISC    , HWMON_CANNED_GROUP__MISC,  canned_group__misc   }

#endif

};

/****************************************************************************
 *
 * Name   : groupSensors_print
 *
 * Purpose: Print the sensors associated with a specified group
 *
 *****************************************************************************/
void groupSensors_print ( sensor_group_type * group_ptr )
{
    if ( group_ptr )
    {
        ilog ("%s '%s' group has %d sensors\n",
                  group_ptr->hostname.c_str(),
                  group_ptr->group_name.c_str(),
                  group_ptr->sensors );

        for ( int i = 0 ; i < group_ptr->sensors ; i++ )
        {
            if ( group_ptr->sensor_ptr[i] )
            {
                blog1 ("  > Sensor: %s\n", group_ptr->sensor_ptr[i]->sensorname.c_str());
            }
            else
            {
                blog1 ("  > Sensor: %p\n", group_ptr->sensor_ptr[i] );
            }
        }
    }
    else
    {
        slog ("Null group pointer\n");
    }
}

/*****************************************************************************
 *
 * Name       : bmc_get_grouptype
 *
 * Description: returns the group type ; which is really a baseline
 *              bmc 'sensor type'.
 *
 *****************************************************************************/

string bmc_get_grouptype ( string & hostname,
                            string & unittype,
                            string & sensorname)
{
    canned_group_enum group_enum = bmc_get_groupenum (hostname,
                                                       unittype,
                                                       sensorname);
    if (( group_enum < HWMON_CANNED_GROUPS ) &&
        ( group_enum > HWMON_CANNED_GROUP__NULL ))
    {
        return (canned_group_array[group_enum].group_type);
    }
    else
    {
        return "" ;
    }
}

/*****************************************************************************
 *
 * Name       : bmc_get_groupenum
 *
 * Description: Returns the group enum that the specified unit would
 *              fall into.
 *
 *****************************************************************************/

canned_group_enum bmc_get_groupenum ( string & hostname,
                                       string & unittype,
                                       string & sensorname )
{
    canned_group_enum group_enum = HWMON_CANNED_GROUP__NULL ;
    if ( !unittype.empty() )
    {
        /* search canned groups for one having units that match this
         * sensor sample. */
        for ( int canned_group = (HWMON_CANNED_GROUP__NULL+1) ; canned_group < HWMON_CANNED_GROUPS ; ++canned_group )
        {
            blog3 ("%s search %s:%s \n", hostname.c_str(), canned_group_array[canned_group].group_units, unittype.c_str());
            if ( strstr ( canned_group_array[canned_group].group_units, unittype.data()) ||
               ( strstr ( unittype.data(), canned_group_array[canned_group].group_units)))
            {
                blog2 ("%s %s found\n", hostname.c_str(), unittype.c_str() );
                return(canned_group_array[canned_group].group_enum);
            }
        }

        /*
         *   We always have a group for any sensor
         *   canned or uncanned.
         *
         *   Put uncanned into a miscellaneous group
         */


         /* handle some special cases */

         /* 1. Quanta Power Sensors */
         if  (( unittype.compare("discrete") == 0 ) &&
              ((sensorname.find("PSU Redundancy") != std::string::npos ) ||
               (sensorname.find("PSU1 Status") != std::string::npos ) ||
               (sensorname.find("PSU2 Status") != std::string::npos )))
         {
             group_enum = HWMON_CANNED_GROUP__POWER ;

             blog2 ("%s %-15s group added (for '%s' sensor (translation)\n",
                       hostname.c_str(),
                       canned_group_array[group_enum].group_name,
                       sensorname.c_str());
         }
         else if (( unittype.compare("discrete") == 0 ) &&
                 ((sensorname.find("MB Thermal Trip") != std::string::npos ) ||
                  (sensorname.find("PCH Thermal Trip") != std::string::npos )))
         {
             group_enum = HWMON_CANNED_GROUP__TEMP ;

             blog2 ("%s %-15s group added (for '%s' sensor (translation)\n",
                       hostname.c_str(),
                       canned_group_array[group_enum].group_name,
                       sensorname.c_str());
         }

         /* 1. HP Fans show up as 'percent' sensor type with Fan in the name */
         else if (( unittype.compare("percent") == 0 ) &&
                 ((sensorname.find("Fan") != std::string::npos ) ||
                  (sensorname.find("fan") != std::string::npos )))
         {
             group_enum = HWMON_CANNED_GROUP__FANS ;

             blog2 ("%s %-15s group added (for '%s' sensor (translation)\n",
                       hostname.c_str(),
                       canned_group_array[group_enum].group_name,
                       sensorname.c_str());
         }
         /* 1. HP Fans show up as 'percent' sensor type with Fan in the name */
         else if (( unittype.compare("percent") == 0 ) &&
                 ((sensorname.find("Usage") != std::string::npos ) ||
                  (sensorname.find("usage") != std::string::npos )))
         {
             group_enum = HWMON_CANNED_GROUP__USAGE ;

             blog2 ("%s %-15s group added (for '%s' sensor (translation)\n",
                       hostname.c_str(),
                       canned_group_array[group_enum].group_name,
                       sensorname.c_str());
         }
         else
         {
#ifdef WANT_MORE_GROUPS
             /* Otherwise, uncanned so put the sensor into the miscellaneous group */
             group_enum = HWMON_CANNED_GROUP__MISC ;

             ilog ("%s %-15s group added (for '%s' sensor) (%s:%s)\n",
                       hostname.c_str(),
                       canned_group_array[group_enum].group_name,
                       sensorname.c_str(),
                       unittype.c_str(),
                       canned_group_array[group_enum].group_units);
#else
             blog3 ("%s %-15s is ignored ; no matching sensor group\n", hostname.c_str(), sensorname.c_str());
             group_enum = HWMON_CANNED_GROUP__NULL ;
#endif
         }
    }
    return (group_enum);
}

/*****************************************************************************
 *
 * Name       : bmc_get_groupname
 *
 * Description: returns the group name for the specified group enum.
 *
 *****************************************************************************/

string bmc_get_groupname ( canned_group_enum group_enum )
{
    if ( group_enum < HWMON_CANNED_GROUPS )
    {
        return (canned_group_array[group_enum].group_name);
    }
    return "unknown" ;
}

/******************************************************************************
 *
 * Name       : _log_group_add_status
 *
 * Description: Create appropriate group add status log
 *
 * Scope      : Local
 *
 ******************************************************************************/

void _log_group_add_status ( string hostname,
                             string groupname,
                             int    rc )
{
    if ( rc )
    {
        wlog ("%s %s group add failed (to sysinv) (rc:%d)\n",
                   hostname.c_str(), groupname.c_str(), rc );
    }
    else
    {
        ilog ("%s %s group added (to sysinv)\n",
                  hostname.c_str(), groupname.c_str());
    }
}

/********************************************************************************
 *
 * Name       : bmc_add_group
 *
 * Purpose    : Add a new group to hwmon and then to the sysinv database.
 *
 * Description: Write the new group info to the next group index for the
 *              specified host, update its info with passed in attributes
 *              and then call to program that group into the database.
 *
 *              Wait for the response and update the group with its uuid.
 *
 ****************************************************************************/

int hwmonHostClass::bmc_add_group ( struct hwmonHostClass::hwmon_host * host_ptr ,
                                     string datatype,
                                     string sensortype,
                                     canned_group_enum group_enum,
                                     string group_name,
                                     string path )
{
    int rc ;

    int g = host_ptr->groups ;
    hwmonGroup_init ( host_ptr->hostname, &host_ptr->group[g]);
    host_ptr->group[g].datatype   = datatype   ;
    host_ptr->group[g].group_enum = group_enum ;
    host_ptr->group[g].sensortype = sensortype ;
    host_ptr->group[g].group_name = group_name ;
    host_ptr->group[g].path       = path       ;

    /* If we are in learn mode then restore saved group
     * attributes before programming them into the database */
    if ( host_ptr->relearn == true )
    {
        restore_group_actions ( host_ptr, &host_ptr->group[g] );
    }

    if (( rc = hwmonHttp_add_group ( host_ptr->hostname, host_ptr->event, host_ptr->group[g])) == PASS )
    {
        host_ptr->group[g].group_uuid = host_ptr->event.new_uuid ;
        host_ptr->groups++ ;
    }
    _log_group_add_status ( host_ptr->hostname, host_ptr->group[g].group_name, rc );
    return (rc);
}

/*****************************************************************************
 *
 * Name       : bmc_create_groups
 *
 * Description: Perform sensor grouping from sample data.
 *              This is done using similar bmc unit types from canned groups.
 *
 *****************************************************************************/

int hwmonHostClass::bmc_create_groups ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;
    int sample_errors = 0 ;
    host_ptr->groups  = 0 ;

    /* for each sample ...
     *   1. create a new group or
     *   2. add sensor to an existing sensor group type
     *     i.e. fan , poer, voltage, temperature, etc.
     *
     *   ... based on that sensors' unit type.
     *
     *   Use the canned unit groups above , i.e. canned_group__voltage
     */
    for ( int s = 0 ; s < host_ptr->samples ; ++s )
    {
        /* canned group array index */
        int canned_group_index = 0 ;

        /*
         *   When set true indicates that hwmon already has a
         *   group type allocated for this sensor type.
         */
        bool group_found = false ;

        /* TODO: allow a MAX number of failures before action failure */
        if ( host_ptr->sample[s].unit.empty() )
        {
            if ( ++sample_errors > MAX_SENSOR_TYPE_ERRORS )
            {
                elog ("%s '%s' sensor has empty unit type ; too many errors (%d) ; aborting\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sample[s].name.c_str(),
                          sample_errors);
                return FAIL_STRING_EMPTY ;
            }
            else
            {
                wlog ("%s '%s' sensor has empty unit type ; skipping\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sample[s].name.c_str());
                continue ;
            }
        }

        /* get the group enum from the sensor type and name */
        canned_group_index = bmc_get_groupenum ( host_ptr->hostname,
                                                  host_ptr->sample[s].unit,
                                                  host_ptr->sample[s].name );

        if ( canned_group_index == HWMON_CANNED_GROUP__NULL )
        {
            host_ptr->sample[s].ignore = true ;
            continue ;
        }

        if ( canned_group_index != host_ptr->sample[s].group_enum )
        {
            slog ("%s %s should already be assigned to a group ; sensor filter broken\n",
                      host_ptr->hostname.c_str(),
                      host_ptr->sample[s].name.c_str());
        }

        /* loop over all the existing groups to see if this group type has already been added. */
        for ( int group = 0 ; group < host_ptr->groups ; ++group )
        {
            if ( host_ptr->group[group].group_enum == canned_group_array[canned_group_index].group_enum )
            {
                group_found = true ;
                break ;
            }
        } /* loop over all the groups */

        /* if no then add the new group ; otherwise ignore it */
        if ( group_found == false )
        {
            hwmonGroup_init ( host_ptr->hostname, &host_ptr->group[host_ptr->groups] );
            host_ptr->group[host_ptr->groups].group_name = canned_group_array[canned_group_index].group_name ;
            host_ptr->group[host_ptr->groups].group_enum = canned_group_array[canned_group_index].group_enum ;
            host_ptr->group[host_ptr->groups].sensortype = canned_group_array[canned_group_index].group_type ;

            if ( host_ptr->relearn == true )
            {
                restore_group_actions ( host_ptr, &host_ptr->group[host_ptr->groups] );
            }
            /* create a new group in sysinv */
            if ( ( rc = hwmonHttp_add_group ( host_ptr->hostname,
                                              host_ptr->event,
                                              host_ptr->group[host_ptr->groups]) ) == PASS )
            {
                /* add the sysinv group uuid to the group */
                host_ptr->group[host_ptr->groups].group_uuid = host_ptr->event.new_uuid ;
            }
            _log_group_add_status ( host_ptr->hostname, host_ptr->group[host_ptr->groups].group_name, rc );

            if ( rc )
            {
                return (FAIL_OPERATION);
            }
            else
            {
                blog ("%s %-15s group created (in hwmon) (for %s sensor)\n",
                          host_ptr->hostname.c_str(),
                          canned_group_array[canned_group_index].group_name,
                          host_ptr->sample[s].name.c_str());
                host_ptr->groups++ ;
            }
        }

        /* Tell the sample with what group it will go in later */
        host_ptr->sample[s].group_enum =
        canned_group_array[canned_group_index].group_enum ;

    } /* end for loop over sensor samples */

    ilog ("%s new sensor group model created with %d groups\n",
              host_ptr->hostname.c_str(),
              host_ptr->groups );

    if (( host_ptr->relearn == true ) &&
        ( host_ptr->model_attributes_preserved.interval ) &&
        ( host_ptr->model_attributes_preserved.interval != host_ptr->interval ))
    {
        host_ptr->interval_changed = true ;
        ilog ("%s audit interval restored to %d seconds (from %d)\n",
                  host_ptr->hostname.c_str(),
                  host_ptr->model_attributes_preserved.interval,
                  host_ptr->interval);
        host_ptr->interval = host_ptr->model_attributes_preserved.interval ;
    }

    host_ptr->interval_changed = true ;
    return rc ;
}

/****************************************************************************
 *
 * Name       : bmc_group_sensors
 *
 * Description: Group the sensors based on the group enum that was assigned
 *              to the sensor during group creation.
 *
 *****************************************************************************/

int hwmonHostClass::bmc_group_sensors ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = FAIL ;
    int grouping_errors = 0 ;

    for ( int s = 0 ; s < host_ptr->sensors ; ++s )
    {
        bool found = false ;

        if (( host_ptr->sensor[s].group_enum > HWMON_CANNED_GROUP__NULL ) &&
            ( host_ptr->sensor[s].group_enum < HWMON_CANNED_GROUPS ))
        {
            for ( int g = 0 ; g < host_ptr->groups ; ++g )
            {
                if ( host_ptr->group[g].group_enum == host_ptr->sensor[s].group_enum )
                {
                    if ( !host_ptr->group[g].sensor_labels.empty() )
                    {
                        host_ptr->group[g].sensor_labels.append(",");
                    }
                    host_ptr->group[g].sensor_labels.append(host_ptr->sensor[s].sensorname);

                    /* add the sensor pointer to the group's list */
                    host_ptr->group[g].sensor_ptr[host_ptr->group[g].sensors] = &host_ptr->sensor[s] ;

                    /* formally add the sensor to the group by incrementing the group sensors count */
                    host_ptr->group[g].sensors++ ;

                    /* assign the group uuid to the sensor */
                    host_ptr->sensor[s].group_uuid = host_ptr->group[g].group_uuid ;

                    /* assign group action */
                    host_ptr->sensor[s].actions_minor = host_ptr->group[g].actions_minor_group ;
                    host_ptr->sensor[s].actions_major = host_ptr->group[g].actions_major_group ;
                    host_ptr->sensor[s].actions_critl = host_ptr->group[g].actions_critl_group ;

                    found = true ;
                    break ;
                }
            }
        }

        if ( found == false )
        {
            if ( ++grouping_errors >= MAX_GROUPING_ERRORS )
            {
                elog ("%s '%s' sensor could not be grouped (%d)\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sensor[s].sensorname.c_str(),
                          host_ptr->sensor[s].group_enum );
                rc = FAIL_NOT_FOUND ;
            }
        }
        else
        {
            /* if we found at least one then change rc to PASS */
            rc = PASS ;
        }
    }

    if ( rc == PASS )
    {
        for ( int g = 0 ; g < host_ptr->groups ; ++g )
        {
            groupSensors_print ( &host_ptr->group[g] );
            rc = hwmonHttp_group_sensors ( host_ptr->hostname,
                                           host_ptr->event,
                                           host_ptr->group[g].group_uuid,
                                           host_ptr->group[g].sensor_labels );
            if ( rc )
            {
                break ;
            }
        }
    }

    if ( rc == PASS )
    {
        ilog ("%s sensors grouped\n", host_ptr->hostname.c_str());
    }
    else
    {
        elog ("%s failed to group sensors\n", host_ptr->hostname.c_str());
    }

    return (rc);
}


/*****************************************************************************
 *
 * Name       : bmc_set_group_state
 *
 * Purpose    : With the introduction of bmc monitoring, all groups are
 *              monitored at once. Therefore all should be in the same state.
 *
 * Description: Set all groups to specified state
 *
 * TODO       : Consider setting all sensors offline for failure case.
 *              Would need a bulk HTTP command to sysinv or do it as a
 *              time sliced operation.
 *
 ******************************************************************************/

int  hwmonHostClass::bmc_set_group_state  ( struct hwmonHostClass::hwmon_host * host_ptr , string state )
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

        for ( int g = 0 ; g < host_ptr->groups ; ++g )
        {
            struct sensor_group_type * group_ptr = &host_ptr->group[g] ;
            if ( group_ptr->group_state.compare(state.data()))
            {
                group_ptr->group_state = state ;
                int status = hwmonHttp_mod_group ( host_ptr->hostname,
                                                   host_ptr->event ,
                                                   group_ptr->group_uuid,
                                                   "state",
                                                   group_ptr->group_state );
                if ( status )
                {
                    elog ( "%s failed to set '%s' group state to '%s'\n",
                               host_ptr->hostname.c_str(),
                               group_ptr->group_name.c_str(),
                               state.c_str());

                    if ( rc == PASS  )
                        rc = RETRY ;
                }

                /* handle raising the group alarm if we have a failed state,
                 * its not already faaailed and it is a failed request */
                if ((( status != PASS ) || ( state.compare("failed") == 0 )) && ( group_ptr->failed == false ))
                {
                    hwmonAlarm_major ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, group_ptr->group_name, REASON_DEGRADED );
                    group_ptr->failed = true ;
                }

                /* handle clearing the group alarm */
                else if (( status == PASS ) && ( state.compare("failed")) && ( group_ptr->failed == true ))
                {
                    hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, group_ptr->group_name, REASON_OK );
                    group_ptr->failed = false ;
                }
                else
                {
                    blog ( "%s '%s' group - no alarm action for state '%s' (%d)\n",
                               host_ptr->hostname.c_str(),
                               group_ptr->group_name.c_str(),
                               state.c_str(),
                               group_ptr->failed);
                }
            }
        }
    }
    return (rc);
}
