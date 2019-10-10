/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform - Hardware Monitoring and Fault Handling
  * Access to Inventory Database via REST API Interface.
  *
  */

/** This file implements
 *
 * 1. an http client used to register sensors with sysinv
 * 2. an http server used to receive sensor configuration changes from sysinv
 *
 **/

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "hwm"

#include "nodeBase.h"        /* for ... Base Service Header             */
#include "nodeUtil.h"        /* for ... Utility Service Header          */
#include "hostUtil.h"        /* for ... Host Utilities                  */
#include "jsonUtil.h"        /* for ... common Json utilities           */
#include "httpUtil.h"        /* for ... common Http utilities           */
#include "hwmonUtil.h"       /* for ... is_valid_action                 */
#include "hwmonHttp.h"       /* this .. module header                   */
#include "hwmonClass.h"      /* for ... service class definition        */
#include "hwmonSensor.h"     /* for ... hwmonSensor_print               */
#include "hwmonAlarm.h"      /* for ... hwmonAlarm                      */

static event_type hwmon_event ;

/* Cleanup */
void hwmonHttp_server_fini ( void )
{
    if ( hwmon_event.fd )
    {
        if ( hwmon_event.base )
        {
            event_base_free( hwmon_event.base);
        }
        close ( hwmon_event.fd );
    }
}

time_debug_type before  ;
time_debug_type after   ;
time_delta_type delta = { 0, 0 } ;

/* Look for events */
void hwmonHttp_server_look ( void )
{
    /* Look for INV Events */
    if ( hwmon_event.base )
    {
        gettime ( after ) ;
        event_base_loop( hwmon_event.base, EVLOOP_NONBLOCK );
        gettime (before) ;

        timedelta ( before , after, delta  );

        if ( delta.secs > 1 )
        {
            ilog ("-----> SERVICE STALL : did not service sysinv events for %ld.%ld sec\n", delta.secs, delta.msecs/1000);
        }
        //if ( inside_delta > 50000000 )
        //{
        //    ilog ("-----> LIBEVENT STALL: event_base_loop select stalled for %llu msec\n", inside_delta/1000000 );
        //}
    }
}


/* The ',' is needed and the '.' is only fior the end of he full line */
void _create_error_response ( string hostname, string & resp_buffer, int error )
{
    resp_buffer = "{" ;
    resp_buffer.append (" \"status\" : \"fail\"");
    if ( error == HTTP_BADREQUEST )
    {
        resp_buffer.append (",\"reason\" : \"Request appears invalid. Check logs for more detail\"");
        resp_buffer.append (",\"action\" : \"Retry operation or contact next level support.\"");
    }
    else if ( error == MTC_HTTP_FORBIDDEN )
    {
        resp_buffer.append (",\"reason\" : \"Unknown User-Agent specified in request\"");
        resp_buffer.append (",\"action\" : \"Retry operation or contact next level support.\"");
    }
    else if ( error == HTTP_NOTFOUND )
    {
         resp_buffer.append (",\"reason\" : \"Specified sensor or group was not found\"");
         resp_buffer.append (",\"action\" : \"Please retry request, sensor loading may be in progress. ");
         resp_buffer.append ("If problem persists then Lock and Unlock host to force re-read of sensor information. ");
         resp_buffer.append ("Then, if problem continues please contact the system administrator for further assistance.\"");
    }
    else if ( error == MTC_HTTP_CONFLICT )
    {
         resp_buffer.append (",\"reason\" : \"Requested operation failed.\"");
         resp_buffer.append (",\"action\" : \"Retry request and if the problem persists deprovision and then reprovision BMC. ");
         resp_buffer.append ("With the BMC reprovisioned retry the request. ");
         resp_buffer.append ("Then, if the problem continues please contact the system administrator for further assistance.\"");
    }
    else if ( error == MTC_HTTP_LENGTH_REQUIRED )
    {
         resp_buffer.append (",\"reason\" : \"Sensor modify request with no accompanying data\"");
         resp_buffer.append (",\"action\" : \"Retry operation and if problem continues contact next level support.\"");
    }
    else if ( error == FAIL_INVALID_DATA )
    {
         resp_buffer.append (",\"reason\" : \"Requested sensor audit interval is too frequent\"");
         resp_buffer.append (",\"action\" : \"Please use value larger than ");
         resp_buffer.append (itos(HWMON_MIN_AUDIT_INTERVAL));
         resp_buffer.append (" secs.\"");
    }
    else if ( error == FAIL_BAD_STATE )
    {
         resp_buffer.append (",\"reason\" : \"Invalid sensor action\"");
         resp_buffer.append (",\"action\" : \"Please select one of the following supported actions: 'ignore','log' or 'alarm'.\"");
    }
    else
    {
        resp_buffer.append (",\"reason\" : \"Unknown Error code ");
        resp_buffer.append (itos(error));
        resp_buffer.append ("\",\"action\" : \"Undetermined\"");
        wlog ("%s no supported reason/action string for error code %d.\n", hostname.c_str(), error);
    }
    resp_buffer.append ("}");
}


int hwmonJson_load_sensor ( string hostname , string sensor_record , sensor_type & sensor )
{
    int rc = FAIL_STRING_EMPTY ;

    if ( !sensor_record.empty() )
    {
        jlog ("Event Payload: %s", sensor_record.c_str());

        struct json_object *json_obj = json_tokener_parse((char*)sensor_record.data());
        if ( !json_obj )
        {
            elog ("%s No or invalid sysinv sensor record\n", hostname.c_str());
            return (FAIL_JSON_PARSE);
        }

        /* Get all required fields */
        sensor.uuid           = jsonUtil_get_key_value_string ( json_obj, MTC_JSON_INV_UUID );
        sensor.group_uuid     = jsonUtil_get_key_value_string ( json_obj, "sensorgroup_uuid");
        sensor.host_uuid      = jsonUtil_get_key_value_string ( json_obj, "host_uuid"       );
        sensor.sensorname     = jsonUtil_get_key_value_string ( json_obj, "sensorname"      );
        sensor.datatype       = jsonUtil_get_key_value_string ( json_obj, "datatype"        );
        sensor.sensortype     = jsonUtil_get_key_value_string ( json_obj, "sensortype"      );
        sensor.actions_minor  = jsonUtil_get_key_value_string ( json_obj, "actions_minor"   );
        sensor.actions_major  = jsonUtil_get_key_value_string ( json_obj, "actions_major"   );
        sensor.actions_critl  = jsonUtil_get_key_value_string ( json_obj, "actions_critical");
        sensor.algorithm      = jsonUtil_get_key_value_string ( json_obj, "algorithm");
        sensor.status         = jsonUtil_get_key_value_string ( json_obj, "status");
        sensor.state          = jsonUtil_get_key_value_string ( json_obj, "state");
        sensor.path           = jsonUtil_get_key_value_string ( json_obj, "path" );

        /* if there is no stored path then the entity path is just the sensor name */
        if ( sensor.path.empty() )
        {
            sensor.entity_path = sensor.sensorname ;
        }
        else
        {
            /* otherwise prefix the entity path with the path */
            sensor.entity_path = sensor.path ;
            sensor.entity_path.append(ENTITY_DELIMITER);
            sensor.entity_path.append(sensor.sensorname);
        }

        string suppress_string= jsonUtil_get_key_value_string ( json_obj, "suppress" );
        if (( !suppress_string.compare("True")) || ( !suppress_string.compare("true")))
            sensor.suppress = true ;
        else
            sensor.suppress = false ;

        /* Convert actions to lower case */
        sensor.actions_minor = tolowercase (sensor.actions_minor);
        sensor.actions_major = tolowercase (sensor.actions_major);
        sensor.actions_critl = tolowercase (sensor.actions_critl);

        if ( !sensor.datatype.compare("analog") )
        {
            string temp  ;

            // sensor.t_critical_lower = jsonUtil_get_key_value_string ( json_obj, "t_critical_lower");
            temp = jsonUtil_get_key_value_string ( json_obj, "t_major_lower");
            if ( !temp.empty() ) sensor.t_major_lower = atof(temp.data());
            
            temp = jsonUtil_get_key_value_string ( json_obj, "t_minor_lower");
            if ( !temp.empty() ) sensor.t_minor_lower = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_minor_upper");
            if ( !temp.empty() ) sensor.t_minor_upper = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_major_upper");
            if ( !temp.empty() ) sensor.t_major_upper = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_critical_upper");
            if ( !temp.empty() ) sensor.t_critical_upper = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_critical_lower");
            if ( !temp.empty() ) sensor.t_critical_lower = atof(temp.data());

            sensor.unit_base        = jsonUtil_get_key_value_string ( json_obj, "unit_base" );
            sensor.unit_rate        = jsonUtil_get_key_value_string ( json_obj, "unit_rate" );
            sensor.unit_modifier    = jsonUtil_get_key_value_string ( json_obj, "unit_modifier" );
        }

        if (json_obj) json_object_put(json_obj);
        rc = PASS ;
    }
    return (rc);
}

int hwmonJson_load_group ( string hostname , string group_record , struct sensor_group_type & group )
{
    int rc = FAIL_STRING_EMPTY ;

    if ( !group_record.empty() )
    {
        jlog ("Event Payload: %s", group_record.c_str());

        struct json_object *json_obj = json_tokener_parse((char*)group_record.data());
        if ( !json_obj )
        {
            elog ("%s No or invalid sysinv sensor group record\n", hostname.c_str());
            return (FAIL_JSON_PARSE);
        }
        /* Get all required fields */
        group.group_name          = jsonUtil_get_key_value_string ( json_obj, "sensorgroupname" );
        group.actions_minor_group = jsonUtil_get_key_value_string ( json_obj, "actions_minor_group"   );
        group.actions_major_group = jsonUtil_get_key_value_string ( json_obj, "actions_major_group"   );
        group.actions_critl_group = jsonUtil_get_key_value_string ( json_obj, "actions_critical_group");
        group.algorithm           = jsonUtil_get_key_value_string ( json_obj, "algorithm");
        group.group_uuid          = jsonUtil_get_key_value_string ( json_obj, "uuid");
        group.host_uuid           = jsonUtil_get_key_value_string ( json_obj, "host_uuid");
        group.group_state         = jsonUtil_get_key_value_string ( json_obj, "state");
        group.path                = jsonUtil_get_key_value_string ( json_obj, "path");
        group.status              = jsonUtil_get_key_value_string ( json_obj, "status");
        string suppress_string = jsonUtil_get_key_value_string ( json_obj, "suppress" );
        if (( !suppress_string.compare("True")) || ( !suppress_string.compare("true")))
            group.suppress = true ;
        else
            group.suppress = false ;

        group.group_interval = jsonUtil_get_key_value_int ( json_obj, "audit_interval_group" );

        /* Convert actions to lower case */
        group.actions_minor_group = tolowercase (group.actions_minor_group);
        group.actions_major_group = tolowercase (group.actions_major_group);
        group.actions_critl_group = tolowercase (group.actions_critl_group);

        if ( !group.datatype.compare("analog") )
        {
            string temp  ;

            temp = jsonUtil_get_key_value_string ( json_obj, "t_major_lower_group");
            if ( !temp.empty() ) group.t_major_lower_group = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_minor_lower_group");
            if ( !temp.empty() ) group.t_minor_lower_group = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_minor_upper_group");
            if ( !temp.empty() ) group.t_minor_upper_group = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_major_upper_group");
            if ( !temp.empty() ) group.t_major_upper_group = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_critical_upper_group");
            if ( !temp.empty() ) group.t_critical_upper_group = atof(temp.data());

            temp = jsonUtil_get_key_value_string ( json_obj, "t_critical_lower_group");
            if ( !temp.empty() ) group.t_critical_lower_group = atof(temp.data());

            group.unit_base_group        = jsonUtil_get_key_value_string ( json_obj, "unit_base_group" );
            group.unit_rate_group        = jsonUtil_get_key_value_string ( json_obj, "unit_rate_group" );
            group.unit_modifier_group    = jsonUtil_get_key_value_string ( json_obj, "unit_modifier_group" );
        }
        group.actions_minor_choices = jsonUtil_get_key_value_string ( json_obj, "actions_minor_choices");
        group.actions_major_choices = jsonUtil_get_key_value_string ( json_obj, "actions_major_choices");
        group.actions_critical_choices = jsonUtil_get_key_value_string ( json_obj, "actions_critical_choices");

        if (json_obj) json_object_put(json_obj);
        rc = PASS ;
    }
    return (rc);
}




/********************************************************************
 *
 * Verify this request contains valid client info.
 *
 * i.e. the user-Agent header needs to exist and be set to
 *      CLIENT_SYSINV_1_0
 *
 ********************************************************************/
mtc_client_enum _get_client_id ( struct evhttp_request *req )
{
    mtc_client_enum client_hdr = CLIENT_NONE ;
    mtc_client_enum client_url = CLIENT_NONE ;

    const char * url_ptr = evhttp_request_get_uri (req);
    const char * group_event_ptr = strstr ( url_ptr, SYSINV_ISENSORGROUPS_LABEL);
    const char * sensor_event_ptr = strstr ( url_ptr, SYSINV_ISENSOR_LABEL);
    jlog ("URI: %s\n", url_ptr );
    if ( sensor_event_ptr != NULL )
    {
        client_url = CLIENT_SENSORS ;
    }
    else if (group_event_ptr != NULL)
    {
        client_url = CLIENT_SENSORGROUPS ;
    }
    else
    {
        wlog ("Unsupported URL:%s)\n", url_ptr );
        return CLIENT_NONE;
    }

    /* Parse Headers we care about to verify that it also contains the correct User-Agent header */
    struct evkeyvalq * headers_ptr = evhttp_request_get_input_headers (req);
    const char * header_value_ptr  = evhttp_find_header (headers_ptr, CLIENT_HEADER);
    if ( header_value_ptr ) 
    {
        if ( ! strncmp ( header_value_ptr, CLIENT_SYSINV_1_0, 20 ) )
        {
            dlog3 ("%s\n", header_value_ptr );
            client_hdr = CLIENT_SYSINV ;
        }
    }

    /* Both client_url and client_hdr need to match */
    if ( client_hdr != CLIENT_NONE )
    {
        return (client_url);
    }
    else
    {
        wlog ("Unknown or mismatched client (hdr:%d:%s)\n", client_hdr, header_value_ptr);
        return (CLIENT_NONE);
    }
}
/*
{"status": "ok", "t_critical_upper": null, "actions_minor": null, "sensorname": "5V Rail", "suppress": "False", "updated_at": null, "sensortype": "voltage", "t_critical_lower": null, "unit_base": null, "state_requested": null, "path": "", "unit_rate": null, "actions_critical": null, "id": 8, "t_minor_lower": null, "uuid": "5b019f20-fec1-4173-ab5a-0fb12be34a4d", "unit_modifier": null, "sensor_action_requested": null, "t_minor_upper": null, "datatype": "analog", "capabilities": {}, "t_major_lower": null, "state": "disabled", "sensorgroup_id": null, "host_id": 3, "algorithm": null, "t_major_upper": null, "audit_interval": null, "actions_major": null}
*/

/* Handle the sysinv sensor modify request */
string _sensor_modify_handler ( string hostname,
                                char  * request_ptr,
                                int   & http_status_code)
{
    sensor_type sysinv_sensor ;
    string resp_buffer ;

    int rc ;

    resp_buffer.clear();

    /* Load a local 'sysinv_sensor' variable with the sensor information from the
     * sysinv request so that it can be compared to that same sensor in the host. */
    hwmonSensor_init ( hostname, &sysinv_sensor );
    rc = hwmonJson_load_sensor ( hostname, request_ptr, sysinv_sensor );
    if ( rc == PASS )
    {
        hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr() ;
        resp_buffer = "{ \"status\" : \"pass\" }" ;
        http_status_code = HTTP_OK ;
        sensor_type * sensor_ptr = obj_ptr->get_sensor ( hostname, sysinv_sensor.entity_path );
        if ( sensor_ptr )
        {
            if ( sensor_ptr->suppress != sysinv_sensor.suppress )
            {
                 sensor_ptr->suppress = sysinv_sensor.suppress ;
                 if ( sysinv_sensor.suppress == true )
                 {
                     hwmonLog ( hostname, HWMON_ALARM_ID__SENSOR, FM_ALARM_SEVERITY_CLEAR, sensor_ptr->sensorname, REASON_SUPPRESSED );
                     handle_new_suppression ( sensor_ptr );
                 }
                 else
                 {
                     hwmonLog ( hostname, HWMON_ALARM_ID__SENSOR, FM_ALARM_SEVERITY_CLEAR, sensor_ptr->sensorname, REASON_UNSUPPRESSED );
                     obj_ptr->manage_sensor_state ( hostname, sensor_ptr, get_severity(sensor_ptr->status));
                 }
            }

            /* Currently we don't support Sysinv modifying any of the following sensor attributes */
#ifdef WANT_SENSOR_ATTRIBUTE_MODIFY_SUPPORT

            if ( sensor_ptr->state.compare(sysinv_sensor.state) )
            {
                ilog ("%s '%s' sensor 'state' changed from '%s' to '%s'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->state.c_str(),
                          sysinv_sensor.state.c_str());

                 sensor_ptr->state = sysinv_sensor.state ;
            }

            if ( sensor_ptr->status.compare(sysinv_sensor.status) )
            {
                ilog ("%s '%s' sensor 'status' changed from '%s' to '%s'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->status.c_str(),
                          sysinv_sensor.status.c_str());

                 sensor_ptr->status = sysinv_sensor.status ;
            }

            if ( sensor_ptr->unit_base.compare(sysinv_sensor.unit_base) )
            {
                ilog ("%s '%s' sensor 'unit_base' changed from '%s' to '%s'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->unit_base.c_str(),
                          sysinv_sensor.unit_base.c_str());

                 sensor_ptr->unit_base = sysinv_sensor.unit_base ;
            }

            if ( sensor_ptr->unit_rate.compare(sysinv_sensor.unit_rate) )
            {
                ilog ("%s '%s' sensor 'unit_rate' changed from '%s' to '%s'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->unit_rate.c_str(),
                          sysinv_sensor.unit_rate.c_str());

                 sensor_ptr->unit_rate = sysinv_sensor.unit_rate ;
            }

            if ( sensor_ptr->unit_modifier.compare(sysinv_sensor.unit_modifier) )
            {
                ilog ("%s '%s' sensor 'unit_modifier' changed from '%s' to '%s'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->unit_modifier.c_str(),
                          sysinv_sensor.unit_modifier.c_str());

                 sensor_ptr->unit_modifier = sysinv_sensor.unit_modifier ;
            }

            if ( sensor_ptr->actions_minor.compare(sysinv_sensor.actions_minor))
            {
                 /* action is validated and converted to lower case */
                 if ( is_valid_action ( HWMON_SEVERITY_MINOR, sysinv_sensor.actions_minor , true) )
                 {
                     ilog ("%s '%s' sensor 'minor event action(s)' changed from '%s' to '%s'\n",
                               hostname.c_str(), sensor_ptr->sensorname.c_str(),
                               sensor_ptr->actions_minor.c_str(),
                               sysinv_sensor.actions_minor.c_str());

                     sensor_ptr->actions_minor = sysinv_sensor.actions_minor ;
                 }
                 else
                 {
                     _create_error_response ( hostname, resp_buffer, FAIL_BAD_STATE ) ;
                     http_status_code = HTTP_BADREQUEST ;
                 }
            }

            if ( sensor_ptr->actions_major.compare(sysinv_sensor.actions_major))
            {
                 /* action is validated and converted to lower case */
                 if ( is_valid_action ( HWMON_SEVERITY_MAJOR, sysinv_sensor.actions_major , true ) )
                 {
                     ilog ("%s '%s' sensor 'major event action(s)' changed from '%s' to '%s'\n",
                               hostname.c_str(), sensor_ptr->sensorname.c_str(),
                               sensor_ptr->actions_major.c_str(),
                               sysinv_sensor.actions_major.c_str());

                     sensor_ptr->actions_major = sysinv_sensor.actions_major ;
                 }
                 else
                 {
                     _create_error_response ( hostname, resp_buffer, FAIL_BAD_STATE ) ;
                     http_status_code = HTTP_BADREQUEST ;
                 }
            }

            if ( sensor_ptr->actions_critl.compare(sysinv_sensor.actions_critl))
            {
                 /* action is validated and converted to lower case */
                 if ( is_valid_action ( HWMON_SEVERITY_CRITICAL, sysinv_sensor.actions_critl , true ) )
                 {
                     ilog ("%s '%s' sensor 'critical event action(s)' changed from '%s' to '%s'\n",
                               hostname.c_str(), sensor_ptr->sensorname.c_str(),
                               sensor_ptr->actions_critl.c_str(),
                               sysinv_sensor.actions_critl.c_str());

                     sensor_ptr->actions_critl = sysinv_sensor.actions_critl ;
                 }
                 else
                 {
                     _create_error_response ( hostname, resp_buffer, FAIL_BAD_STATE ) ;
                     http_status_code = HTTP_BADREQUEST ;
                 }
            }

            if ( sensor_ptr->path.compare(sysinv_sensor.path))
            {
                ilog ("%s '%s' sensor 'read command' changed from '%s' to '%s'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->path.c_str(),
                          sysinv_sensor.path.c_str());

                 sensor_ptr->path = sysinv_sensor.path ;
            }

            if ( sensor_ptr->algorithm.compare(sysinv_sensor.algorithm))
            {
                ilog ("%s '%s' sensor 'algorithm' changed from '%s' to '%s'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->algorithm.c_str(),
                          sysinv_sensor.algorithm.c_str());

                 sensor_ptr->algorithm = sysinv_sensor.algorithm ;
            }

            if ( sensor_ptr->t_minor_lower != sysinv_sensor.t_minor_lower )
            {
                ilog ("%s '%s' sensor 'Lower Minor Threshold' changed from '%5.3f' to '%5.3f'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->t_minor_lower,
                          sysinv_sensor.t_minor_lower);

                 sensor_ptr->t_minor_lower = sysinv_sensor.t_minor_lower ;
            }

            if ( sensor_ptr->t_major_lower != sysinv_sensor.t_major_lower )
            {
                ilog ("%s '%s' sensor 'Lower Major Threshold' changed from '%5.3f' to '%5.3f'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->t_major_lower,
                          sysinv_sensor.t_major_lower);

                 sensor_ptr->t_major_lower = sysinv_sensor.t_major_lower ;
            }

            if ( sensor_ptr->t_critical_lower != sysinv_sensor.t_critical_lower )
            {
                ilog ("%s '%s' sensor 'Lower Critical Threshold' changed from '%5.3f' to '%5.3f'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->t_critical_lower,
                          sysinv_sensor.t_critical_lower);

                 sensor_ptr->t_critical_lower = sysinv_sensor.t_critical_lower ;
            }

            if ( sensor_ptr->t_minor_upper != sysinv_sensor.t_minor_upper )
            {
                ilog ("%s '%s' sensor 'Upper Minor Threshold' changed from '%5.3f' to '%5.3f'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->t_minor_upper,
                          sysinv_sensor.t_minor_upper);

                 sensor_ptr->t_minor_upper = sysinv_sensor.t_minor_upper ;
            }

            if ( sensor_ptr->t_major_upper != sysinv_sensor.t_major_upper )
            {
                ilog ("%s '%s' sensor 'Upper Major Threshold' changed from '%5.3f' to '%5.3f'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->t_major_upper,
                          sysinv_sensor.t_major_upper);

                 sensor_ptr->t_major_upper = sysinv_sensor.t_major_upper ;
            }

            if ( sensor_ptr->t_critical_upper != sysinv_sensor.t_critical_upper )
            {
                ilog ("%s '%s' sensor 'Upper Critical Threshold' changed from '%5.3f' to '%5.3f'\n",
                          hostname.c_str(), sensor_ptr->sensorname.c_str(),
                          sensor_ptr->t_critical_upper,
                          sysinv_sensor.t_critical_upper);

                 sensor_ptr->t_critical_upper = sysinv_sensor.t_critical_upper ;
            }
#endif
        }
        else
        {
            elog ("%s '%s' sensor not found (in hwmon)\n", hostname.c_str(), sysinv_sensor.sensorname.c_str());
            _create_error_response ( hostname, resp_buffer, HTTP_NOTFOUND ) ;
            http_status_code = HTTP_NOTFOUND ;
        }
    }
    else
    {
        elog ("%s failed parsing sensor modify request (from sysinv)\n", hostname.c_str());
        http_status_code = HTTP_BADREQUEST ;
        _create_error_response ( hostname, resp_buffer, http_status_code ) ;
    }

    return (resp_buffer);
}

/* Handle the sysinv group modify request */
string _group_modify_handler ( string hostname,
                               char  * request_ptr,
                               int   & http_status_code)
{
    struct sensor_group_type sysinv_group ;
    string resp_buffer ;

    int rc ;

    resp_buffer.clear();

    /* Load a local 'sysinv_group' variable with the sensor information from the
     * sysinv request so that it can be compared to that same group in the host. */
    hwmonGroup_init ( hostname, &sysinv_group );
    rc = hwmonJson_load_group ( hostname, request_ptr, sysinv_group );
    if ( rc == PASS )
    {
        struct sensor_group_type * host_group_ptr = get_hwmonHostClass_ptr()->hwmon_get_group ( hostname, sysinv_group.group_name );
        if ( host_group_ptr )
        {
            hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr() ;
            if ( host_group_ptr->suppress != sysinv_group.suppress )
            {
                hlog ("%s '%s' group 'suppression' changed from '%s' to '%s'\n",
                          hostname.c_str(), host_group_ptr->group_name.c_str(),
                          host_group_ptr->suppress ? "True" : "False",
                             sysinv_group.suppress ? "True" : "False");

                 /* modify all sensors in this group and update this setting */
                 rc = obj_ptr->group_modify ( hostname,
                                              sysinv_group.group_uuid,
                                              "suppress" ,
                                              sysinv_group.suppress ? "True" : "False" );
                 if ( rc )
                 {
                     /* TODO: handle with proper error code */
                     _create_error_response ( hostname, resp_buffer, FAIL ) ;
                     http_status_code = HTTP_BADREQUEST ;
                     return (resp_buffer);
                 }
            }

            if ( host_group_ptr->group_interval != sysinv_group.group_interval )
            {
                hlog ("%s '%s' group 'interval' changed from '%d' to '%d' secs\n",
                          hostname.c_str(),
                          host_group_ptr->group_name.c_str(),
                          host_group_ptr->group_interval,
                          sysinv_group.group_interval);

                 /* modify all sensors in this group and update this setting */
                 rc = obj_ptr->group_modify ( hostname,
                                              sysinv_group.group_uuid,
                                              "audit_interval_group" ,
                                              itos(sysinv_group.group_interval));
                 if ( rc )
                 {
                     _create_error_response ( hostname, resp_buffer, rc ) ;
                     http_status_code = HTTP_BADREQUEST ;
                     return (resp_buffer);
                 }
            }

            if ( host_group_ptr->actions_critl_group != sysinv_group.actions_critl_group )
            {
                 rc = FAIL_BAD_STATE ;
                 if ( is_valid_action ( HWMON_SEVERITY_CRITICAL, sysinv_group.actions_critl_group , true) )
                 {
                     hlog ("%s '%s' group 'actions_critical_group' changed from '%s' to '%s'\n",
                               hostname.c_str(),
                               host_group_ptr->group_name.c_str(),
                               host_group_ptr->actions_critl_group.c_str(),
                                  sysinv_group.actions_critl_group.c_str() );

                      /* modify all sensors in this group and update this setting */
                      rc = obj_ptr->group_modify ( hostname,
                                                   sysinv_group.group_uuid,
                                                   "actions_critical_group" ,
                                                   sysinv_group.actions_critl_group );
                 }
                 if ( rc )
                 {
                     _create_error_response ( hostname, resp_buffer, rc ) ;
                     http_status_code = HTTP_BADREQUEST ;
                     return (resp_buffer);
                 }
            }

            if ( host_group_ptr->actions_major_group != sysinv_group.actions_major_group )
            {
                 rc = FAIL_BAD_STATE ;
                 if ( is_valid_action ( HWMON_SEVERITY_MAJOR, sysinv_group.actions_major_group, true ) )
                 {
                     hlog ("%s '%s' group 'actions_major_group' changed from '%s' to '%s'\n",
                               hostname.c_str(),
                               host_group_ptr->group_name.c_str(),
                               host_group_ptr->actions_major_group.c_str(),
                               sysinv_group.actions_major_group.c_str() );

                      /* modify all sensors in this group and update this setting */
                      rc = obj_ptr->group_modify ( hostname,
                                                   sysinv_group.group_uuid,
                                                   "actions_major_group" ,
                                                   sysinv_group.actions_major_group );
                 }
                 if ( rc )
                 {
                     _create_error_response ( hostname, resp_buffer, rc ) ;
                     http_status_code = HTTP_BADREQUEST ;
                     return (resp_buffer);
                 }
            }

            if ( host_group_ptr->actions_minor_group != sysinv_group.actions_minor_group )
            {
                 rc = FAIL_BAD_STATE ;
                 if ( is_valid_action ( HWMON_SEVERITY_MINOR, sysinv_group.actions_minor_group , true ) )
                 {
                     hlog ("%s '%s' group 'actions_minor_group' changed from '%s' to '%s'\n",
                               hostname.c_str(),
                               host_group_ptr->group_name.c_str(),
                               host_group_ptr->actions_minor_group.c_str(),
                               sysinv_group.actions_minor_group.c_str() );

                     /* modify all sensors in this group and update this setting */
                     rc = obj_ptr->group_modify ( hostname,
                                                  sysinv_group.group_uuid,
                                                  "actions_minor_group" ,
                                                  sysinv_group.actions_minor_group );
                 }
                 if ( rc )
                 {
                     _create_error_response ( hostname, resp_buffer, rc ) ;
                     http_status_code = HTTP_BADREQUEST ;
                     return (resp_buffer);
                 }
            }

            resp_buffer = "{ \"status\" : \"pass\" }" ;
            http_status_code = HTTP_OK ;
        }
        else
        {
            elog ("%s '%s' group not found (in hwmon)\n", hostname.c_str(), sysinv_group.group_name.c_str());
            _create_error_response ( hostname, resp_buffer, HTTP_NOTFOUND ) ;
            http_status_code = HTTP_NOTFOUND ;
        }
    }
    else
    {
        elog ("%s failed parsing group modify request (from sysinv)\n", hostname.c_str());
        http_status_code = HTTP_BADREQUEST ;
        _create_error_response ( hostname, resp_buffer, http_status_code ) ;
    }

    return (resp_buffer);
}

/*****************************************************************************
 *
 * Name:        hwmondHttp_server_handler
 *
 * Description: Receive an http event, extract the event type and buffer from
 *              it and call process request handler.
 *              Send the processed message response back to the connection.
 *
 * Supported events include: PATCH from sysinv
 *
 ******************************************************************************/
void hwmonHttp_server_handler (struct evhttp_request *req, void *arg)
{
    mtc_client_enum client = CLIENT_NONE ;
    int http_status_code = HTTP_NOTFOUND ;

    hwmon_ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
    msgSock_type    * mtclogd_ptr = get_mtclogd_sockPtr () ;

    string hostname = "" ;
    string uuid     = "" ;
    string response = "" ;

    /* default response */
    response = "{" ;
    response.append (" \"status\" : \"fail\"");
    response.append (",\"reason\" : \"not found\"");
    response.append (",\"action\" : \"retry with valid host\"");
    response.append ("}");

    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr () ;

    hwmon_event.req = req ;
    hlog3 ("HTTP Event:%p base:%p Req:%p arg:%p\n", &hwmon_event, hwmon_event.base, hwmon_event.req, arg );

    /* Get sender must be localhost */
    const char * host_ptr = evhttp_request_get_host (req);
    if ( strncmp ( host_ptr , "localhost" , 10 ))
    {
        wlog ("Message received from unknown host '%s' but should be 'localhost'\n", host_ptr );
        evhttp_send_error (hwmon_event.req, http_status_code, response.data() );
    }

    const char * url_ptr = evhttp_request_get_uri (req);
    hlog ("HTTP Request From Sysinv - %p - URL: %s\n", hwmon_event.req, url_ptr );

    /* Extract the operation */
    evhttp_cmd_type http_cmd = evhttp_request_get_command (req);

    snprintf (&ctrl_ptr->log_str[0] , MAX_API_LOG_LEN-1, "\n%s [%5d] %s Request from %s for %s ...",
               pt(), getpid(), getHttpCmdType_str(http_cmd), host_ptr, url_ptr );

    send_log_message ( mtclogd_ptr, ctrl_ptr->my_hostname.data(), &ctrl_ptr->filename[0], &ctrl_ptr->log_str[0] );

    snprintf (&ctrl_ptr->log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] failed response\n", pt(), getpid() );

    /* Acquire the client that sent this event from the url URI */
    client = _get_client_id ( req );
    if ( client != CLIENT_NONE )
    {
        switch ( http_cmd )
        {
            case EVHTTP_REQ_POST:
            {
                if ( client == CLIENT_SENSORGROUPS )
                {
                    /* get the payload */
                    struct evbuffer *in_buf = evhttp_request_get_input_buffer ( req );
                    if ( in_buf )
                    {
                        size_t len = evbuffer_get_length(in_buf) ;
                        if ( len )
                        {
                            ev_ssize_t bytes = 0 ;
                            char * buffer_ptr = (char*)malloc(len+1);
                            memset ( buffer_ptr, 0, len+1 );
                            bytes = evbuffer_remove(in_buf, buffer_ptr, len );

                            if ( bytes > 0 )
                            {
                                struct json_object * json_obj = json_tokener_parse(buffer_ptr);
                                if ( json_obj )
                                {
                                    string host_uuid = jsonUtil_get_key_value_string ( json_obj, "host_uuid" );
                                    if ( hostUtil_is_valid_uuid ( host_uuid ) )
                                    {
                                        /* request sensor model relearn as a
                                         * background operation */
                                        obj_ptr->bmc_learn_sensor_model (host_uuid) ;
                                        http_status_code = HTTP_OK ;
                                        response = "{ \"status\" : \"pass\" }" ;
                                    }
                                    else
                                    {
                                        wlog ("failed to find 'host_uuid' key in HTTP event message\n");
                                        http_status_code = HTTP_BADREQUEST ;
                                        _create_error_response ( hostname, response, http_status_code );
                                    }
                                    if (json_obj) json_object_put(json_obj);
                                }
                                else
                                {
                                    elog ("No or invalid sysinv sensor record\n");
                                    if (( buffer_ptr ) && ( strlen(buffer_ptr) < 1000 ))
                                    {
                                        elog ("event payload: %s\n", buffer_ptr);
                                    }
                                    http_status_code = HTTP_NOTFOUND ;
                                    _create_error_response ( hostname, response, http_status_code );
                                }
                            }
                            else
                            {
                                wlog ("http event request with no payload\n");
                                _create_error_response ( hostname, response, MTC_HTTP_LENGTH_REQUIRED );
                                http_status_code = HTTP_BADREQUEST ;
                            }
                            free ( buffer_ptr );
                        }
                        else
                        {
                            wlog ("http event request with no payload\n");
                            _create_error_response ( hostname, response, MTC_HTTP_LENGTH_REQUIRED );
                            http_status_code = HTTP_BADREQUEST ;
                        }
                    }
                    else
                    {
                        http_status_code = HTTP_BADREQUEST ;
                        _create_error_response ( hostname, response, http_status_code );
                        wlog ("Http event request has no buffer\n");
                    }
                }
                else
                {
                    http_status_code = HTTP_BADREQUEST ;
                    _create_error_response ( hostname, response, http_status_code );
                    elog ("Unexpected POST request ...\n");
                }
                break ;
            }
            case EVHTTP_REQ_PATCH:
            {
                /* get the payload */
                struct evbuffer *in_buf = evhttp_request_get_input_buffer ( req );
                if ( in_buf )
                {
                    size_t len = evbuffer_get_length(in_buf) ;
                    if ( len )
                    {
                        ev_ssize_t bytes = 0 ;
                        char * buffer_ptr = (char*)malloc(len+1);
                        memset ( buffer_ptr, 0, len+1 );
                        bytes = evbuffer_remove(in_buf, buffer_ptr, len );

                        if ( bytes <= 0 )
                        {
                            wlog ("http event request with no payload\n");
                            _create_error_response ( hostname, response, MTC_HTTP_LENGTH_REQUIRED );
                            http_status_code = HTTP_BADREQUEST ;
                        }
                        else
                        {
                            struct json_object * json_obj = json_tokener_parse(buffer_ptr);
                            if ( json_obj )
                            {
                                string host_uuid = jsonUtil_get_key_value_string ( json_obj, "host_uuid" );
                                if ( !host_uuid.empty() )
                                {
                                    hostname = obj_ptr->get_hostname ( host_uuid ) ;

                                    if ( client == CLIENT_SENSORS )
                                    {
                                        snprintf (&ctrl_ptr->log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] Sensor Modify Request : %s", pt(), getpid(), buffer_ptr);
                                        send_log_message ( mtclogd_ptr, ctrl_ptr->my_hostname.data(), &ctrl_ptr->filename[0], &ctrl_ptr->log_str[0] );

                                        response = _sensor_modify_handler ( hostname, buffer_ptr, http_status_code );

                                        snprintf (&ctrl_ptr->log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] Sensor Modify Response: %s\n", pt(), getpid(), response.data());
                                    }
                                    else if ( client == CLIENT_SENSORGROUPS )
                                    {
                                        snprintf (&ctrl_ptr->log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] Group Modify Request : %s", pt(), getpid(), buffer_ptr);
                                        send_log_message ( mtclogd_ptr, ctrl_ptr->my_hostname.data(), &ctrl_ptr->filename[0], &ctrl_ptr->log_str[0] );

                                        response = _group_modify_handler ( hostname, buffer_ptr, http_status_code );

                                        snprintf (&ctrl_ptr->log_str[0], MAX_API_LOG_LEN-1, "%s [%5d] Group Modify Response: %s\n", pt(), getpid(), response.data());
                                    }
                                    else
                                    {
                                        elog ("%s Unknown client\n", hostname.c_str());
                                        http_status_code = HTTP_NOTFOUND ;
                                        _create_error_response ( hostname, response, http_status_code );
                                    }
                                }
                                else
                                {
                                    wlog ("failed to find 'host_uuid' key in HTTP event message\n");
                                    http_status_code = HTTP_BADREQUEST ;
                                    _create_error_response ( hostname, response, http_status_code );
                                }
                                if (json_obj) json_object_put(json_obj);
                            }
                            else
                            {
                                elog ("No or invalid sysinv sensor record\n");
                                if (( buffer_ptr ) && ( strlen(buffer_ptr) < 1000 ))
                                {
                                    elog ("event payload: %s\n", buffer_ptr);
                                }
                                http_status_code = HTTP_NOTFOUND ;
                                _create_error_response ( hostname, response, http_status_code );
                            }
                        }
                        free ( buffer_ptr );
                    }
                    else
                    {
                         http_status_code = HTTP_BADREQUEST  ;
                         _create_error_response ( hostname, response, MTC_HTTP_LENGTH_REQUIRED );
                         wlog ("Http event request has no payload\n");
                    }
                }
                else
                {
                    http_status_code = HTTP_BADREQUEST ;
                    _create_error_response ( hostname, response, http_status_code );
                    wlog ("Http event request has no buffer\n");
                }
                break ;
            }
            default:
            {
                http_status_code = HTTP_BADREQUEST ;
                _create_error_response ( hostname, response, http_status_code );
                wlog ("Unsupported command\n");
            }
        }
    }
    else
    {
        http_status_code = MTC_HTTP_FORBIDDEN ;
        _create_error_response ( hostname, response, http_status_code );
        wlog ("invalid User-Agent specified\n");
    }

    send_log_message ( mtclogd_ptr, ctrl_ptr->my_hostname.data(), &ctrl_ptr->filename[0], &ctrl_ptr->log_str[0] );
    if ( http_status_code == HTTP_OK )
    {
        struct evbuffer *resp_buf = evbuffer_new();
        hlog3 ("Event Response: %s\n", response.c_str());
        evbuffer_add_printf (resp_buf, "%s\n", response.data());
        evhttp_send_reply (hwmon_event.req, http_status_code, "OK", resp_buf );
        evbuffer_free ( resp_buf );
    }
    else
    {
        // _create_error_response ( hostname, response, http_status_code );
        elog ("HTTP Event error:%d ; cmd:%s url:%s response:%s\n",
               http_status_code,
               getHttpCmdType_str(http_cmd),
               url_ptr,
               response.c_str());

        evhttp_send_error (hwmon_event.req, http_status_code, response.data() );
    }
}


/*****************************************************************
 *
 * Name        : hwmonHttp_server_bind
 *
 * Description : Setup the HTTP server socket
 *
 *****************************************************************/
int hwmonHttp_server_bind (  event_type & event )
{
   int rc     ;
   int flags  ;
   int one = 1;

   event.fd = socket(AF_INET, SOCK_STREAM, 0);
   if (event.fd < 0)
   {
       elog ("HTTP server socket create failed (%d:%m)\n", errno );
       return FAIL_SOCKET_CREATE ;
   }

   /* make socket reusable */
   rc = setsockopt(event.fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int));
   if ( rc < 0 )
   {
      elog ("failed to set HTTP server socket as reusable (%d:%m)\n", errno );
   }

   memset(&event.addr, 0, sizeof(struct sockaddr_in));
   event.addr.sin_family = AF_INET;
   event.addr.sin_addr.s_addr = inet_addr(LOOPBACK_IP) ; /* INADDR_ANY; TODO: Refine this if we can */
   event.addr.sin_port = htons(event.port);

   /* bind port */
   rc = bind ( event.fd, (struct sockaddr*)&event.addr, sizeof(struct sockaddr_in));
   if (rc < 0)
   {
       elog ("HTTP server port %d bind failed (%d:%m)\n", event.port, errno );
       return FAIL_SOCKET_BIND ;
   }

   /* Listen for events */
   rc = listen(event.fd, 10 );
   if (rc < 0)
   {
       elog ("HTTP server listen failed (%d:%m)\n", errno );
       return FAIL_SOCKET_LISTEN;
   }

   /* make non-blocking */
   flags = fcntl ( event.fd, F_GETFL, 0) ;
   if ( flags < 0 || fcntl(event.fd, F_SETFL, flags | O_NONBLOCK) < 0)
   {
       elog ("failed to set HTTP server socket to non-blocking (%d:%m)\n", errno );
       return FAIL_SOCKET_OPTION;
   }

   return PASS;
}


/* Setup the http server */
int hwmonHttp_server_setup ( event_type & event )
{
   int rc = PASS ;
   if ( ( rc = hwmonHttp_server_bind ( event )) != PASS )
   {
       return rc ;
   }
   else if (event.fd < 0)
   {
       wlog ("failed to get http server socket file descriptor\n");
       return RETRY ;
   }

   event.base = event_base_new();
   if (event.base == NULL)
   {
       elog ("failed to get http server event base\n");
       return -1;
   }
   event.httpd = evhttp_new(event.base);
   if (event.httpd == NULL)
   {
       elog ("failed to get httpd server handle\n");
       return -1;
   }

   evhttp_set_allowed_methods (event.httpd, EVENT_METHODS );

   rc = evhttp_accept_socket(event.httpd, event.fd);
   if ( rc == -1)
   {
       elog ("failed to accept on http server socket\n");
       return -1;
   }
   evhttp_set_gencb(event.httpd, hwmonHttp_server_handler, NULL);

   return PASS ;
}

/************************************************************************
 *
 * Name       : hwmonHttp_server_init
 *
 * Description: Incoming HTTP event server on specified port.
 *
 */

int hwmonHttp_server_init  ( int event_port )
{
    int rc = PASS ;
    hwmon_ctrl_type * ctrl_ptr = get_ctrl_ptr() ;
    memset ( &hwmon_event, 0, sizeof(event_type));
    hwmon_event.port = event_port ;

    snprintf (&ctrl_ptr->filename[0], MAX_FILENAME_LEN, "/var/log/%s_event.log", program_invocation_short_name );

    for ( ; ; )
    {
        rc = hwmonHttp_server_setup ( hwmon_event );
        if ( rc == RETRY )
        {
            wlog ("%s bind failed (%d)\n", EVENT_SERVER, hwmon_event.fd );
        }
        else if ( rc != PASS )
        {
            elog ("%s start failed (rc:%d)\n", EVENT_SERVER, rc );
        }
        else if ( hwmon_event.fd > 0 )
        {
            ilog ("Listening for 'http event server ' socket %s:%d\n",
                   inet_ntoa(hwmon_event.addr.sin_addr), hwmon_event.port );
            rc = PASS ;
            break ;
        }
        if ( rc ) mtcWait_secs (5);
    }

    return ( rc ) ;
}

int hwmonHttp_handler ( libEvent & event )
{
    int rc = PASS ;

    string hn = event.hostname ;

    hlog ("%s handler called\n", event.log_prefix.c_str() );

    /* request shared  string iterator */
    std::list<string>::iterator iter_curr_ptr ;

    if ( event.request == SYSINV_SENSOR_MOD )
    {
        jlog ("Sensor Modify Response: %s\n", event.response.c_str());
    }
    else if ( event.request == SYSINV_SENSOR_MOD_GROUP )
    {
        jlog ("Group Modify Response: %s\n", event.response.c_str());
    }
    /* Handle the sysinv resposne to a sensor load request */
    if ( event.request == SYSINV_SENSOR_LOAD )
    {
        if ( event.status == PASS )
        {
            std::list<string> sensor_list ;
            sensor_list.clear();

            rc = jsonUtil_get_list ( (char*)event.response.data(), SYSINV_ISENSOR_LABEL, sensor_list );
            if ( rc == PASS )
            {
                hlog ("%s has %ld sensors in the database\n", hn.c_str(), sensor_list.size() );
                sensor_type sysinv_sensor ;

                /* Load the list of sensors for this host */
                for ( iter_curr_ptr  = sensor_list.begin();
                      iter_curr_ptr != sensor_list.end() ;
                    ++iter_curr_ptr )
                {
                    hwmonSensor_init ( hn, &sysinv_sensor );
                    rc = hwmonJson_load_sensor ( hn, iter_curr_ptr->data(), sysinv_sensor );
                    if ( rc == PASS )
                    {
                        blog2 ("%s '%s' sensor read (from sysinv)\n", hn.c_str(), sysinv_sensor.sensorname.c_str() );
                        rc = get_hwmonHostClass_ptr()->add_sensor ( hn, sysinv_sensor );
                        if ( rc == PASS )
                        {
                            blog2 ("%s '%s' sensor added (to hwmon)\n",
                                      hn.c_str(), sysinv_sensor.sensorname.c_str());
                        }
                        else
                        {
                            elog ("%s '%s' add sensor failed (to hwmon)\n",
                                      hn.c_str(),
                                      sysinv_sensor.sensorname.c_str());
                            event.status = rc = FAIL ;
                            break ;
                        }
                    }
                    else
                    {
                        elog ("%s failed parsing sensor record (from sysinv)\n", hn.c_str());
                        wlog ("%s ... Raw Sensor Record: \n%s\n",
                                  event.log_prefix.c_str(),
                                  iter_curr_ptr->c_str());
                        event.status = rc =  FAIL_JSON_PARSE ;
                        break ;
                    }
                } /* for loop */
            }
            else
            {
                elog ("%s json sensor list parse error (from sysinv)\n", hn.c_str() );
                wlog ("%s ... Raw Sensor List: \n%s\n",
                          event.log_prefix.c_str(),
                          event.response.c_str());
                event.status = rc = FAIL_JSON_OBJECT ;
            }
        }
        else
        {
            elog ("%s handler called with existing error (status:%d) (sensor)\n",
                      hn.c_str(), event.status );
        }
    }

    else if ( event.request == SYSINV_SENSOR_GROUP_SENSORS )
    {
        hlog ("%s Sensor Group Response: \n%s\n", event.log_prefix.c_str(), event.response.c_str());
        struct json_object * json_obj = json_tokener_parse((char*)event.response.data());
        if ( json_obj )
        {
            /* update the event with the uuid sysinv generated */
            event.new_uuid = jsonUtil_get_key_value_string ( json_obj, "uuid" );
            if (json_obj) json_object_put(json_obj);

            if ( !event.uuid.compare(event.new_uuid))
            {
                // log ("%s sensor grouping passed\n", event.log_prefix.c_str());
                event.status = rc = PASS ;
            }
            else
            {
                elog ("%s failed to sensor grouping - response group uuid mismatch\n", event.log_prefix.c_str());
                event.status = rc = FAIL_INVALID_UUID ;
            }
        }
        else
        {
            elog ("%s failed to sensor grouping - cannot tokenize response\n", event.log_prefix.c_str());
            event.status = rc = FAIL_JSON_OBJECT ;
        }
    }
    /* Handle the sysinv resposne to a sensor group add request */
    else if ( event.request == SYSINV_SENSOR_DEL )
    {
        hlog ("%s '%s' sensor deleted uuid:%s\n",
                  event.hostname.c_str(),
                  event.key.c_str(),
                  event.value.c_str());
    }

    /* Handle the sysinv resposne to a sensor group add request */
    else if ( event.request == SYSINV_SENSOR_DEL_GROUP )
    {
        hlog ("%s '%s' sensor group deleted uuid:%s\n",
                  event.hostname.c_str(),
                  event.key.c_str(),
                  event.value.c_str());
    }

    /* Handle the sysinv resposne to a sensor group add request */
    else if ( event.request == SYSINV_SENSOR_ADD )
    {
        hlog ("%s Add Sensor Response: \n%s\n", event.log_prefix.c_str(), event.response.c_str());
        struct json_object *json_obj = json_tokener_parse((char*)event.response.data());
        if ( json_obj )
        {
            /* update the event with the uuid sysinv generated */
            event.new_uuid = jsonUtil_get_key_value_string ( json_obj, "uuid" );
            if (json_obj) json_object_put(json_obj);

            event.status = rc = PASS ;
        }
        else
        {
            elog ("%s failed to add sensor - cannot tokenize response\n", event.log_prefix.c_str());
            event.status = rc = FAIL_JSON_OBJECT ;
        }
    }

    /* Handle the sysinv resposne to a sensor group add request */
    else if ( event.request == SYSINV_SENSOR_ADD_GROUP )
    {
        hlog ("%s Add Group Response: \n%s\n", event.log_prefix.c_str(), event.response.c_str());

        struct json_object *json_obj = json_tokener_parse((char*)event.response.data());
        if ( json_obj )
        {
             /* update the event with the uuid sysinv generated */
            event.new_uuid = jsonUtil_get_key_value_string ( json_obj, "uuid" );
            if (json_obj) json_object_put(json_obj);

            event.status = rc = PASS ;
        }
        else
        {
            elog ("%s failed to add group - cannot tokenize response\n", event.log_prefix.c_str());
            event.status = rc = FAIL_JSON_OBJECT ;
        }
    }

    /* Handle the sysinv resposne to a sensor group add request */
    else if ( event.request == SYSINV_SENSOR_LOAD_GROUPS )
    {
        if ( event.status == PASS )
        {
            std::list<string> group_list ;
            group_list.clear();

            rc = jsonUtil_get_list ( (char*)event.response.data(), SYSINV_ISENSORGROUPS_LABEL, group_list );
            if ( rc == PASS )
            {
                hlog ("%s has %ld sensor groups in the database\n", hn.c_str(), group_list.size() );

                struct sensor_group_type sysinv_group ;

                /* Load the list of sensors for this host */
                for ( iter_curr_ptr  = group_list.begin();
                      iter_curr_ptr != group_list.end() ;
                    ++iter_curr_ptr )
                {
                    sysinv_group.timer.tid = NULL ;
                    hwmonGroup_init ( hn, &sysinv_group );
                    rc = hwmonJson_load_group ( hn, iter_curr_ptr->data(), sysinv_group );
                    if ( rc == PASS )
                    {
                        blog ("%s '%s' sensor group read (from sysinv) [uuid:%s]\n",
                                  hn.c_str(),
                                  sysinv_group.group_name.c_str(),
                                  sysinv_group.group_uuid.c_str());
                        rc = get_hwmonHostClass_ptr()->hwmon_add_group ( hn, sysinv_group );
                        if ( rc == PASS )
                        {
                            blog ("%s '%s' sensor group added (to hwmon)\n", 
                                      hn.c_str(), sysinv_group.group_name.c_str());
                    
                            if ( daemon_get_cfg_ptr()->debug_bmgmt > 1 )
                            {
                                hwmonGroup_print ( hn, &sysinv_group );
                            }
                        }
                        else
                        {
                            elog ("%s '%s' sensor group add failed (to hwmon)\n", 
                                      hn.c_str(), sysinv_group.group_name.c_str());
                            /* Don't fail the command */
                            // event.status = rc = FAIL ;
                            // break ;
                        }
                    }
                    else
                    {
                        elog ("%s failed parsing sensor group record (from sysinv)\n", hn.c_str());
                        wlog ("%s ... Raw Group Record: \n%s\n",
                                  event.log_prefix.c_str(), 
                                  iter_curr_ptr->c_str());
                        event.status = rc =  FAIL_JSON_PARSE ;
                        break ;
                    }
                } /* for loop */
            }
            else
            {
                elog ("%s json sensor group list parse error (from sysinv)\n", hn.c_str() );
                wlog ("%s ... Raw Group List: \n%s\n", 
                          event.log_prefix.c_str(), 
                          event.response.c_str());
                event.status = rc = FAIL_JSON_OBJECT ;
            }
        }
        else
        {
            elog ("%s handler called with existing error (status:%d) (group) \n", 
                      hn.c_str(), event.status );
        }
    }
    if ( rc || event.status )
    {
        httpUtil_log_event  ( &event );
    }
    return ( rc ? rc : event.status );
}


/*************************************************************************************/
/*****************   S Y S T E M  -  I N V E N T O R Y  - A P I    *******************/
/*************************************************************************************/

/* fetches an authorization token as a blocking request */

/* Load all the sensors for this host from the sysinv database 
 * ------------------------------------------------------------
   { "isensors": 
     [
       {"actions_minor": null, 
        "uuid": "fa9bdd6b-1738-4409-8d85-843510f726e8", 
        "algorithm": null, 
        "updated_at": null, 
        "datatype": "discrete", 
        "suppress": null, 
        "created_at": "2015-08-28T18:49:39.799038+00:00", 
        "sensorgroup_uuid": null, 
        "capabilities": { }, 
        "actions_critical": null, 
        "sensortype": "temperature", 
        "state": null, 
        "host_uuid": "d219a108-959d-462d-9418-bb0ead921e3e", 
        "state_requested": null, 
        "path": null, 
        "audit_interval": null, 
        "actions_major": null, 
        "sensorname": "Inlet_Air" 
        "links": [ { "href": "http:\/\/192.168.204.2\/v1\/isensors\/fa9bdd6b-1738-4409-8d85-843510f726e8", "rel": "self" }, 
                   { "href": "http:\/\/192.168.204.2\/isensors\/fa9bdd6b-1738-4409-8d85-843510f726e8", "rel": "bookmark" } ], 
       },.
       { "t_critical_upper": null, 
         "actions_minor": null, 
         "sensorname": "5V Rail", 
         "links": [{"href": "http://192.168.204.2/v1/isensors/575d0b3a-14ea-412e-84ec-2f46af3e86d9", "rel": "self"}, 
                   {"href": "http://192.168.204.2/isensors/575d0b3a-14ea-412e-84ec-2f46af3e86d9", "rel": "bookmark"}], 
         "updated_at": null, 
         "path": null, 
         "state_requested": null, 
         "t_major_lower": null, 
         "uuid": "575d0b3a-14ea-412e-84ec-2f46af3e86d9", 
         "t_minor_upper": null, 
         "capabilities": {}, 
         "actions_critical": null, 
         "state": null, 
         "sensorgroup_uuid": null, 
         "t_major_upper": null, 
         "actions_major": null, 
         "suppress": null, 
         "sensortype": "voltage", 
         "t_critical_lower": null, 
         "t_minor_lower": null, 
         "unit_rate": null, 
         "unit_modifier": null, 
         "host_uuid": "d219a108-959d-462d-9418-bb0ead921e3e", 
         "unit_base": null, 
         "algorithm": null, 
         "datatype": "analog", 
         "created_at": "2015-08-28T20:10:37.605930+00:00", 
         "audit_interval": null
        }
     ]
   }
*/

/* Load all sensors for the specified host from the sysinv database */
int hwmonHttp_load_sensors ( string & hostname, libEvent & event )
{
    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr ();

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_load_sensors",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = obj_ptr->hostBase.get_uuid (hostname);
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_HOST_LABEL);
    event.address.append("/");
    event.address.append(event.uuid);
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_LABEL);

    event.request     = SYSINV_SENSOR_LOAD ;
    event.type        = EVHTTP_REQ_GET ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "load" ;
    event.information = event.operation ;
    event.blocking    = true ;
    event.noncritical = true ;
    event.service     = "sensors" ;

    event.payload.clear();

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_LOAD_SENSORS, hostname ))
        return (FAIL) ;
#endif

    return ( httpUtil_api_request ( event ) );
}

/******************************************************************************

                           #    ######  ######
                          # #   #     # #     #
                         #   #  #     # #     #
                        #     # #     # #     #
                        ####### #     # #     #
                        #     # #     # #     #
                        #     # ######  ######

******************************************************************************/

/*****************************************************************************
 *
 * Name       : hwmonHttp_add_sensor
 *
 * Description: Add a sensor to the sysinv database
 *
 *****************************************************************************/
int hwmonHttp_add_sensor (    string & hostname,
                            libEvent & event,
                         sensor_type & sensor )
{
    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr ();

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_add_sensor",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = obj_ptr->hostBase.get_uuid (hostname);
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_LABEL);
    event.address.append("/");

    event.request     = SYSINV_SENSOR_ADD ;
    event.type        = EVHTTP_REQ_POST ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "add" ;
    event.information = event.operation ;
    event.blocking    = true ; /* revert back to blocking */
    event.noncritical = true ;
    event.service     = "sensor" ;

    event.payload = "{" ;
    event.payload.append ("\"sensortype\":\"") ;
    event.payload.append (sensor.sensortype) ;

    event.payload.append ("\",\"datatype\":\"");
    event.payload.append (sensor.datatype);

    event.payload.append ("\",\"sensorname\":\"");
    event.payload.append (sensor.sensorname);

    event.payload.append ("\",\"host_uuid\":\"") ;
    event.payload.append (event.uuid);

    event.payload.append ("\",\"path\":\"") ;
    event.payload.append (sensor.path);

    event.payload.append ("\",\"state\":\"") ;
    event.payload.append (sensor.state);

    event.payload.append ("\",\"status\":\"") ;
    event.payload.append (sensor.status);
    event.payload.append ("\"");

    if ( !sensor.datatype.compare("analog"))
    {
        event.payload.append (",");

        event.payload.append ("\"t_critical_lower\":\"") ;
        event.payload.append (ftos(sensor.t_critical_lower,3));

        event.payload.append ("\",\"t_major_lower\":\"") ;
        event.payload.append (ftos(sensor.t_major_lower,3));

        event.payload.append ("\",\"t_minor_lower\":\"") ;
        event.payload.append (ftos(sensor.t_minor_lower,3));

        event.payload.append ("\",\"t_minor_upper\":\"") ;
        event.payload.append (ftos(sensor.t_minor_upper,3));

        event.payload.append ("\",\"t_major_upper\":\"") ;
        event.payload.append (ftos(sensor.t_major_upper,3));

        event.payload.append ("\",\"t_critical_upper\":\"") ;
        event.payload.append (ftos(sensor.t_critical_upper,3));

        event.payload.append ("\",\"unit_base\":\"") ;
        event.payload.append (sensor.unit_base);

        event.payload.append ("\",\"unit_rate\":\"") ;
        event.payload.append (sensor.unit_rate);

        event.payload.append ("\",\"unit_modifier\":\"") ;
        event.payload.append (sensor.unit_modifier);
        event.payload.append ("\"");

    }

    event.payload.append (",\"actions_minor\":\"") ;
    event.payload.append (sensor.actions_minor);
    event.payload.append ("\",\"actions_major\":\"") ;
    event.payload.append (sensor.actions_major);
    event.payload.append ("\",\"actions_critical\":\"") ;
    event.payload.append (sensor.actions_critl);
    event.payload.append ("\",\"algorithm\":\"") ;
    event.payload.append (sensor.algorithm);

    event.payload.append ("\",\"audit_interval\":") ;
    event.payload.append ("0");

    event.payload.append (",\"suppress\":\"") ;
    if ( sensor.suppress == true )
        event.payload.append ("True\"");
    else
        event.payload.append ("False\"");

    event.payload.append ("}");

    jlog ("%s Payload: %s\n", event.hostname.c_str(), event.payload.c_str());

#ifdef WANT_FIT_TESTING
   if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_ADD_SENSOR, hostname , sensor.sensorname))
        return ( FAIL ) ;
#endif

    return ( httpUtil_api_request ( event ));
}

/*****************************************************************************
 *
 * Name       : hwmonHttp_del_sensor
 *
 * Description: Delete a sensor from the sysinv database
 *
 *****************************************************************************/
int hwmonHttp_del_sensor (    string & hostname,
                            libEvent & event,
                         sensor_type & sensor )
{
    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr ();

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_del_sensor",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = obj_ptr->hostBase.get_uuid (hostname);
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_LABEL);
    event.address.append("/");
    event.address.append(sensor.uuid);

    event.request     = SYSINV_SENSOR_DEL ;
    event.type        = EVHTTP_REQ_DELETE ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "delete" ;
    event.information = event.operation ;
    event.blocking    = true ;
    event.noncritical = true ;
    event.service     = "sensor" ;
    event.value       = sensor.uuid ;
    event.key         = sensor.sensorname ;

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_DEL_SENSOR, hostname, sensor.sensorname ))
        return (FAIL) ;
#endif

    return ( httpUtil_api_request ( event ));
}

/*****************************************************************************
 *
 * Name       : hwmonHttp_add_group
 *
 * Description: Add a sensor group to the sysinv database
 *
 *****************************************************************************/
int hwmonHttp_add_group ( string & hostname,
                          libEvent & event,
                          struct sensor_group_type & sensor_group )
{
    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr ();

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_add_group",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = obj_ptr->hostBase.get_uuid (hostname);
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSORGROUPS_LABEL);
    event.address.append("/");

    event.request     = SYSINV_SENSOR_ADD_GROUP ;
    event.type        = EVHTTP_REQ_POST ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "add" ;
    event.information = event.operation ;
    event.blocking    = true ; /* revert back to blocking */
    event.noncritical = true ;
    event.service     = "group" ;

    event.payload = "{" ;
    event.payload.append ("\"sensortype\":\"") ;
    event.payload.append (sensor_group.sensortype) ;

    event.payload.append ("\",\"datatype\":\"");
    event.payload.append (sensor_group.datatype);

    event.payload.append ("\",\"sensorgroupname\":\"");
    event.payload.append (sensor_group.group_name);

    event.payload.append ("\",\"host_uuid\":\"") ;
    event.payload.append (event.uuid);

    event.payload.append ("\",\"state\":\"") ;
    event.payload.append (sensor_group.group_state);

    event.payload.append ("\",\"path\":\"") ;
    event.payload.append (sensor_group.path);

    event.payload.append ("\",\"actions_critical_choices\":\"") ;
    event.payload.append (sensor_group.actions_critical_choices);

    event.payload.append ("\",\"actions_major_choices\":\"") ;
    event.payload.append (sensor_group.actions_major_choices);

    event.payload.append ("\",\"actions_minor_choices\":\"") ;
    event.payload.append (sensor_group.actions_minor_choices);
    event.payload.append ("\"");

    if ( !sensor_group.datatype.compare("analog"))
    {
        event.payload.append (",");

        event.payload.append ("\"t_critical_lower_group\":\"") ;
        event.payload.append (ftos(sensor_group.t_critical_lower_group,3));

        event.payload.append ("\",\"t_major_lower_group\":\"") ;
        event.payload.append (ftos(sensor_group.t_major_lower_group,3));

        event.payload.append ("\",\"t_minor_lower_group\":\"") ;
        event.payload.append (ftos(sensor_group.t_minor_lower_group,3));

        event.payload.append ("\",\"t_minor_upper_group\":\"") ;
        event.payload.append (ftos(sensor_group.t_minor_upper_group,3));

        event.payload.append ("\",\"t_major_upper_group\":\"") ;
        event.payload.append (ftos(sensor_group.t_major_upper_group,3));

        event.payload.append ("\",\"t_critical_upper_group\":\"") ;
        event.payload.append (ftos(sensor_group.t_critical_upper_group,3));

        event.payload.append ("\",\"unit_base_group\":\"") ;
        event.payload.append (sensor_group.unit_base_group);

        event.payload.append ("\",\"unit_rate_group\":\"") ;
        event.payload.append (sensor_group.unit_rate_group);

        event.payload.append ("\",\"unit_modifier_group\":\"") ;
        event.payload.append (sensor_group.unit_modifier_group);
        event.payload.append ("\"");
    }

    event.payload.append (",\"actions_minor_group\":\"") ;
    event.payload.append (sensor_group.actions_minor_group);
    event.payload.append ("\",\"actions_major_group\":\"") ;
    event.payload.append (sensor_group.actions_major_group);
    event.payload.append ("\",\"actions_critical_group\":\"") ;
    event.payload.append (sensor_group.actions_critl_group);
    event.payload.append ("\",\"algorithm\":\"") ;
    event.payload.append (sensor_group.algorithm);

    event.payload.append ("\",\"audit_interval_group\":") ;
    event.payload.append (itos(sensor_group.group_interval));

    event.payload.append (",\"suppress\":\"") ;
    if ( sensor_group.suppress == true )
        event.payload.append ("True\"");
    else
        event.payload.append ("False\"");

    event.payload.append ("}");

    jlog ("%s Payload: %s\n", event.hostname.c_str(), event.payload.c_str());

#ifdef WANT_FIT_TESTING
   if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_ADD_GROUP, hostname, sensor_group.group_name ))
        return ( FAIL ) ;
#endif

    return ( httpUtil_api_request ( event ));
}

/*****************************************************************************
 *
 * Name       : hwmonHttp_del_group
 *
 * Description: Delete a sensor group from the sysinv database
 *
 *****************************************************************************/

int hwmonHttp_del_group ( string & hostname,
                          libEvent & event,
                          struct sensor_group_type & sensor_group )
{
    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr ();

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_del_group",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = obj_ptr->hostBase.get_uuid (hostname);
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSORGROUPS_LABEL);
    event.address.append("/");
    event.address.append(sensor_group.group_uuid);

    event.request     = SYSINV_SENSOR_DEL_GROUP ;
    event.type        = EVHTTP_REQ_DELETE ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "delete" ;
    event.information = event.operation ;
    event.blocking    = true ;
    event.noncritical = true ;
    event.service     = "group" ;
    event.value       = sensor_group.group_uuid ;
    event.key         = sensor_group.group_name ;

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_DEL_GROUP, hostname, sensor_group.group_name ))
        return (FAIL) ;
#endif

    return ( httpUtil_api_request ( event ));
}

/******************************************************************************

          #     # ####### ######    ###   ####### #     #
          ##   ## #     # #     #    #    #        #   #
          # # # # #     # #     #    #    #         # #
          #  #  # #     # #     #    #    #####      #
          #     # #     # #     #    #    #          #
          #     # #     # #     #    #    #          #
          #     # ####### ######    ###   #          #

********************************************************************************/

/*****************************************************************************
 *
 * Name       : hwmonHttp_mod_sensor
 *
 * Description: Modiy a field for the specified sensor (by sensor uuid)
 *
 *****************************************************************************/
int  hwmonHttp_mod_sensor (  string & hostname,
                           libEvent & event,
                             string & sensor_uuid,
                             string   key,
                             string   value )
{
    if ( key.empty() )
        return (FAIL_STRING_EMPTY);

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_mod_sensor",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = sensor_uuid ;
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_LABEL);
    event.address.append("/");
    event.address.append(sensor_uuid);

    event.request     = SYSINV_SENSOR_MOD ;
    event.type        = EVHTTP_REQ_PATCH ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "modify" ;
    event.information = event.operation ;
    event.blocking    = false ;
    event.noncritical = true ;
    event.service     = "sensor" ;

    event.payload = "[{" ;
    event.payload.append ("\"path\":\"/") ;
    event.payload.append (key) ;
    event.payload.append ("\",\"value\":\"");
    event.payload.append (value);
    event.payload.append ("\",\"op\":\"replace\"}]");

    jlog ("%s Payload: %s\n", event.hostname.c_str(), event.payload.c_str());

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_MOD_SENSOR, hostname ))
        return (FAIL) ;
#endif

    return ( httpUtil_api_request ( event ));
}

/*****************************************************************************
 *
 * Name       : hwmonHttp_mod_group
 *
 * Description: Modify a field for the specified group (by group uuid)
 *
 *****************************************************************************/
int  hwmonHttp_mod_group (  string & hostname,
                          libEvent & event,
                            string & group_uuid,
                            string   key,
                            string   value )
{
    if ( key.empty() )
        return (FAIL_STRING_EMPTY);

    blog ("%s Group [%s] Modify [%s:%s]\n", hostname.c_str(), group_uuid.c_str(), key.c_str(), value.c_str() );

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_mod_group",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = group_uuid;
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSORGROUPS_LABEL);
    event.address.append("/");
    event.address.append(group_uuid);

    event.request     = SYSINV_SENSOR_MOD_GROUP ;
    event.type        = EVHTTP_REQ_PATCH ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "modify" ;
    event.information = event.operation ;
    event.blocking    = false ;
    event.noncritical = true ;
    event.service     = "group" ;

    event.payload = "[{" ;
    event.payload.append ("\"path\":\"/") ;
    event.payload.append (key) ;
    event.payload.append ("\",\"value\":\"");
    event.payload.append (value);
    event.payload.append ("\",\"op\":\"replace\"}]");

    jlog ("%s Payload: %s\n", event.hostname.c_str(), event.payload.c_str());

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_MOD_GROUP, hostname ))
        return (FAIL) ;
#endif

    return ( httpUtil_api_request ( event ));
}


/*****************************************************************************
 *
 * Name       : hwmonHttp_disable_sensor
 *
 * Description: Disable sensor state and set status to offline. (by sensor uuid)
 *
 *****************************************************************************/
int  hwmonHttp_disable_sensor (  string & hostname,
                               libEvent & event,
                                 string & sensor_uuid )
{
    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_disable_sensor",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = sensor_uuid ;
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_LABEL);
    event.address.append("/");
    event.address.append(sensor_uuid);

    event.request     = SYSINV_SENSOR_MOD ;
    event.type        = EVHTTP_REQ_PATCH ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "disable" ;
    event.information = event.operation ;
    event.blocking    = false ;
    event.noncritical = true ;
    event.service     = "sensor" ;

    event.payload = "[" ;
    event.payload.append ("{\"path\":\"/state\",\"value\":\"disabled\",\"op\":\"replace\"},") ;
    event.payload.append ("{\"path\":\"/status\",\"value\":\"offline\",\"op\":\"replace\"}]") ;

    jlog ("%s Payload: %s\n", event.hostname.c_str(), event.payload.c_str());

    return ( httpUtil_api_request ( event ));
}


/**
 *  Send sysinv a GET request for all the sensor groups for this host.
 *  Then load them into this host's sensor group list.
 **/
int  hwmonHttp_load_groups  ( string & hostname, libEvent & event )
{
    hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr ();

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_load_groups",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = obj_ptr->hostBase.get_uuid (hostname);
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_HOST_LABEL);
    event.address.append("/");
    event.address.append(event.uuid);
    event.address.append("/");
    event.address.append(SYSINV_ISENSORGROUPS_LABEL);

    event.request     = SYSINV_SENSOR_LOAD_GROUPS ;
    event.type        = EVHTTP_REQ_GET ;
    event.timeout     = HWMOND_HTTP_BLOCKING_TIMEOUT ; // HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "load" ;
    event.information = event.operation ;
    event.blocking    = true ;
    event.noncritical = true ;
    event.service     = "groups" ;

    event.payload.clear();

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_LOAD_GROUPS, hostname ))
        return (FAIL) ;
#endif

    return (httpUtil_api_request ( event ));
}




/* Add a sensor to the sysinv database */
int hwmonHttp_group_sensors ( string & hostname, libEvent & event, string & group_uuid, string & sensor_list )
{
    /** If there are no sensors to group then just return a PASS
     *
     *  TODO: Maybe remove this or simply FAIL it after integration
     *        so that we know we are trying to group NO sensors
     **/
    if ( sensor_list.empty() )
        return (PASS);

    httpUtil_event_init ( &event,
                           hostname,
                           "hwmonHttp_group_sensors",
                           hostUtil_getServiceIp  (SERVICE_SYSINV),
                           hostUtil_getServicePort(SERVICE_SYSINV));

    event.hostname    = hostname ;
    event.uuid        = group_uuid;
    event.user_agent  = HWMON_USER_AGENT ;

    event.address.clear();
    event.address.append("/");
    event.address.append(SYSINV_ISENSOR_VERSION);
    event.address.append("/");
    event.address.append(SYSINV_ISENSORGROUPS_LABEL);
    event.address.append("/");
    event.address.append(group_uuid);

    event.request     = SYSINV_SENSOR_GROUP_SENSORS ;
    event.type        = EVHTTP_REQ_PATCH ;
    event.timeout     = HTTP_SYSINV_NONC_TIMEOUT ;
    event.handler     = &hwmonHttp_handler ;
    event.operation   = "grouping" ;
    event.information = event.operation ;
    event.blocking    = false ; // true ;
    event.noncritical = true ;
    event.service     = "sensor" ;

    event.payload = "[{" ;
    event.payload.append ("\"path\":\"/sensors\"") ;
    event.payload.append (",\"value\":\"");
    event.payload.append (sensor_list);
    event.payload.append ("\",\"op\":\"replace\"}]");

    jlog ("%s Payload: %s\n", event.hostname.c_str(), event.payload.c_str());

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__HWMON__HTTP_GROUP_SENSORS, hostname ))
        return (FAIL) ;
#endif

    return ( httpUtil_api_request ( event ));
}
