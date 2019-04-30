#ifndef __INCLUDE_HWMONHTTP_H__
#define __INCLUDE_HWMONHTTP_H__

/** ************************************************************************
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 * ************************************************************************/
 
#include "hwmon.h"
#include "httpUtil.h"      /* for ... libEvent                         */

// Sensor Patch label: "/v1/isensors/"
// Sensor  Add label: "/v1/isensors/"
// Sensor Load label: "/v1/ihosts/<host_uuid>/isensors"


#define SYSINV_ISENSOR_VERSION      "v1"
#define SYSINV_ISENSORGROUPS_LABEL  "isensorgroups"
#define SYSINV_ISENSOR_LABEL        "isensors"
#define SYSINV_ISENSOR_HOST_LABEL   "ihosts"
#define HWMON_USER_AGENT            "hwmon/1.0"

/* This is 2 seconds shorter than the OCF script audit timeout at 10 */
#define HWMOND_HTTP_BLOCKING_TIMEOUT (8)


typedef struct
{
    struct sockaddr_in      addr   ;
    struct event_base     * base   ;
    struct evhttp_request * req    ;
    struct evhttp         * httpd  ;
    int                     fd     ;
    int                     port   ;
} event_type ;


 /** **********************************************************************
  * @file
  * Wind River Titanium Cloud's Hardware Monitor HTTP Server/Client Header
  *
  * This file contains 
  *
  *  1. the HTTP Client API for 
  *     - configuring sysinv with sensor data records as well as modifying
  *       and querying those records.
  *
  *  2. the HTTP Server handler that services sysinv sensor configuration
  *     change notifications.
  *
  * ***********************************************************************/
int  hwmonHttp_server_init  ( int event_port );
void hwmonHttp_server_look  ( void );
void hwmonHttp_server_fini  ( void );

int  hwmonHttp_mod_sensor   ( string & hostname, libEvent & event, string & sensor_uuid, string key , string value );
/* 'PATCH /v1/isensors/2bc0ac2c-d0f1-4cbe-9eac-ce40dd18d4c3 HTTP/1.1\r\nHost: 192.168.204.2:6385\r\nContent-Length: 122\r\nuser-agent: Python-httplib2/0.9.1 (gzip)\r\ncontent-type: application/json\r\naccept-encoding: gzip, deflate\r\naccept: application/json\r\nx-auth-token: aee3002723074af8adacd8687e1f639f\r\n\r\n[{"path": "/state", "value": "enabled", "op": "replace"}, {"path": "/suppress", "value": "force_action", "op": "replace"}]'
reply: 'HTTP/1.0 200 OK\r\n'
header: Date: Sun, 13 Sep 2015 17:42:37 GMT
header: Server: WSGIServer/0.1 Python/2.7.3
header: Content-Length: 1070
header: Content-Type: application/json; charset=UTF-8
DEBUG (http:163) RESP:  {"t_critical_upper": null, "actions_minor": "ignore", "sensorname": "Volt_P12V", "links": [{"href": "http://192.168.204.2:6385/v1/isensors/2bc0ac2c-d0f1-4cbe-9eac-ce40dd18d4c3", "rel": "self"}, {"href": "http://192.168.204.2:6385/isensors/2bc0ac2c-d0f1-4cbe-9eac-ce40dd18d4c3", "rel": "bookmark"}], "updated_at": "2015-09-13T13:32:59.786966+00:00", "path": "/etc/bmc/server_profiles.d/sensor_quanta_v1_ilo_v4.profile", "state_requested": null, "t_major_lower": null, "uuid": "2bc0ac2c-d0f1-4cbe-9eac-ce40dd18d4c3", "t_minor_upper": null, "capabilities": {}, "actions_critical": "alarm", "state": "enabled", "sensorgroup_uuid": "8da729d7-168c-4f81-9616-d420b9e4d1e6", "t_major_upper": null, "actions_major": "log", "status": "offline", "suppress": "False", "sensortype": "voltage", "t_critical_lower": null, "t_minor_lower": null, "unit_rate": null, "unit_modifier": null, "host_uuid": "44a462f0-56d2-47c7-a3e6-30f60df54e6c", "unit_base": null, "algorithm": "debounce-1.v1", "datatype": "discrete", "created_at": "2015-09-13T13:32:54.941199+00:00", "audit_interval": 300}
*/

int  hwmonHttp_disable_sensor (  string & hostname, libEvent & event, string & sensor_uuid );

int  hwmonHttp_add_sensor   ( string & hostname, libEvent & event, sensor_type & sensor );
int  hwmonHttp_del_sensor   ( string & hostname, libEvent & event, sensor_type & sensor );
int  hwmonHttp_load_sensors ( string & hostname, libEvent & event );

int  hwmonHttp_mod_group    ( string & hostname, libEvent & event, string & group_uuid, string key , string value );

/* 'PATCH /v1/isensorgroups/8da729d7-168c-4f81-9616-d420b9e4d1e6 HTTP/1.1\r\nHost: 192.168.204.2:6385\r\nContent-Length: 122\r\nuser-agent: Python-httplib2/0.9.1 (gzip)\r\ncontent-type: application/json\r\naccept-encoding: gzip, deflate\r\naccept: application/json\r\nx-auth-token: 6f8981354dee423eb45fd882f386a377\r\n\r\n[{"path": "/state", "value": "enabled", "op": "replace"}, {"path": "/suppress", "value": "force_action", "op": "replace"}]'
reply: 'HTTP/1.0 200 OK\r\n'
header: Date: Sun, 13 Sep 2015 17:48:35 GMT
header: Server: WSGIServer/0.1 Python/2.7.3
header: Content-Length: 1321
header: Content-Type: application/json; charset=UTF-8
DEBUG (http:163) RESP:  {"audit_interval_group": 30, "links": [{"href": "http://192.168.204.2:6385/v1/isensorgroups/8da729d7-168c-4f81-9616-d420b9e4d1e6", "rel": "self"}, {"href": "http://192.168.204.2:6385/isensorgroups/8da729d7-168c-4f81-9616-d420b9e4d1e6", "rel": "bookmark"}], "t_critical_upper_group": null, "updated_at": null, "isensors": [{"href": "http://192.168.204.2:6385/v1/isensorgroups/8da729d7-168c-4f81-9616-d420b9e4d1e6/isensors", "rel": "self"}, {"href": "http://192.168.204.2:6385/isensorgroups/8da729d7-168c-4f81-9616-d420b9e4d1e6/isensors", "rel": "bookmark"}], "t_critical_lower_group": null, "t_minor_upper_group": null, "t_minor_lower_group": null, "uuid": "8da729d7-168c-4f81-9616-d420b9e4d1e6", "unit_modifier_group": null, "capabilities": {}, "state": "enabled", "unit_rate_group": null, "actions_major_group": "log", "suppress": "False", "actions_minor_group": "ignore", "sensorgroupname": "server voltage", "path": "show /SYS/voltage", "sensors": null, "actions_critical_choices": "alarm,ignore,log,reset,powercycle", "actions_major_choices": "alarm,ignore,log", "actions_minor_choices": "ignore,log,alarm",, "host_uuid": "44a462f0-56d2-47c7-a3e6-30f60df54e6c", "t_major_lower_group": null, "unit_base_group": null, "sensortype": "voltage", "algorithm": "debounce-1.v1", "datatype": "discrete", "possible_states": null, "created_at": "2015-09-13T13:32:54.517511+00:00", "actions_critical_group": "alarm", "t_major_upper_group": null}
*/ 

int  hwmonHttp_add_group    ( string & hostname, libEvent & event, struct sensor_group_type & sensor_group );
int  hwmonHttp_del_group    ( string & hostname, libEvent & event, struct sensor_group_type & sensor_group );
int  hwmonHttp_load_groups  ( string & hostname, libEvent & event );    
int  
hwmonHttp_group_sensors( string & hostname, libEvent & event, string & group_uuid, string & sensor_list );

#endif /* __INCLUDE_HWMONHTTP_H__ */
