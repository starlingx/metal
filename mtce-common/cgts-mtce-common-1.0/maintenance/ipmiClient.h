#ifndef __INCLUDE_IPMICLIENT_HH__
#define __INCLUDE_IPMICLIENT_HH__
/*
 * Copyright (c) 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform IPMI Client Daemon
  */

 /*
  *
  * ------------------------------
  * sensor_monitor_ready: outgoing message - indicates service just started and needs configuration
  * ------------------------------
  *
  * The sensor monitor will configure itself based off the content of the
  * following formatted configuration message.
  *
  * { "sensor_monitor_ready":
  *    {
  *       "hostname":"compute-0"
  *    }
  * }
  *
  *
  * ------------------------------
  * ipmitool_sensor_monitor_config: incoming message
  * ------------------------------
  *
  * The sensor monitor will configure itself based off the content of the
  * following formatted configuration message.
  *
  * { "ipmitool_sensor_monitor_config":
  *    {
  *       "hostname":"compute-0",
  *       "interval":120,
  *       "analog"  :true,
  *       "discrete":false
  *    }
  * }
  *
  * ---------------------------------------
  * ipmitool_sensor_monitor_config_response: outgoing message
  * ---------------------------------------
  *
  * This is a config response message. Normally a pass but if there
  * is a configuration error then a return code and message are provided.
  *
  * { "ipmitool_sensor_monitor_config_response":
  *    {
  *          "hostname":"compute-0",
  *          "status": <number>,
  *          "status_string":"<pass | error message>"
  *    }
  * }
  *
  * --------------------------------
  * ipmitool_sensor_threshold_config: incoming message - NOT YET SUPPORTED IMPLEMENTATION
  * --------------------------------
  *
  * Specify only the thresholds that need to be changed.
  *
  * { "ipmitool_sensor_threshold_config":
  *    [
  *       {
  *          "hostname":"compute-0",
  *          "n":"Temp_CPU0",
  *          "lcr":"90.000",
  *          "lnc":"85.000"
  *       }
  *    ]
  * }
  *
  *
  * --------------------
  * ipmitool_sensor_data: outgoing message
  * --------------------
  *
  * The sensor data is formatted in a json style string that is sent
  * to the hardware monitor daemon on the active controller as
  * specified by the aformentioned configuration command.
  *
  * The following is a brief 3 sensor example of the expected
  * ipmitool output and json string conversion that is sent to
  * hardware mon.
  *
  * ipmitool output:
  *
  * Temp_CPU0        | 54.000     | % degrees C | ok    | na        | na        | na        | 86.000    | 87.000    | na
  * PSU2 Input       | 0.000      | % Watts     | cr    | na        | 0.000     | na        | na        | na        | na
  * Critical IRQ     | 0x0        | discrete    | 0x0080| na        | na        | na        | na        | na        | na
  * Fan_SYS0_2       | 4700.000   | % RPM       | ok    | na        | 500.000   | 1000.000  | na        | na        | na
  *
  * Message Design Strategy:
  *    1. Maintain all the ipmitool output information so that it is available
  *       to the hardware monitor for future enhancements without the need to
  *       change the client side messaging.
  *    2. Validate the format of the ipmitool output and report on any errors
  *       observed in a status field of the response string.
  *    3. Deliver an industry standard json string formated message
  *    4. Provide an overall status field indicating any formatting errors
  *       detected in the sensor data output format. This is not a summary
  *       status of the sensor data.
  *    5. minimize the amount of data sent
  *       - use short sensor record labels
  *           n = name
  *           v = sensor reading value
  *           u = unit format used when interpreting the data
  *           s = correlated status
  *       - ipmitool labels for thresholds but only include labels for values that are not 'na'
  *         unr = Upper Non-Recoverable
  *         ucr = Upper Critical
  *         unc = Upper Non-Critical
  *         lnc = Lower Non-Critical
  *         lcr = Lower Critical
  *         lnr = Lower Non-Recoverable
  *
  * Json String: sensor data exacluded
  * -----------
  *
  * {
  *   "ipmitool_sensor_data":
  *      {
  *         "hostname"      :"compute-0",
  *         "status"        : 0,
  *         "status_string" : "pass",
  *         "analog"  :
  *         [
  *            { },
  *            { },
  *            { }
  *         ],
  *         "discrete":
  *         [
  *
  *         ]
  *     }
  *}
  *
  * Jason String: full
  * -------------
  *
  *{
  *   "ipmitool_sensor_data":
  *      {
  *         "hostname"     : "compute-0",
  *         "status"       : 0,
  *         "status_string": "pass",
  *         "analog":[
  *            {
  *               "n":"Temp_CPU0",
  *               "v":"54.000",
  *               "u":"% degrees C",
  *               "s":"ok",
  *               "unc":"86.000",
  *               "ucr":"87.000"
  *            },
  *            {
  *               "n":"PSU2 Input",
  *               "v":"0.000",
  *               "u":"% Watts",
  *               "s":"cr",
  *               "lcr":"0.000"
  *            },
  *            {
  *               "n":"Fan_SYS0_2",
  *               "v":"4700.00",
  *               "u":"% RPM",
  *               "s":"ok",
  *               "lcr":"500.000",
  *               "lnc":"1000.000"
  *            }
  *         ],
  *         "discrete":[
  *            {
  *               "n":"Critical IRQ",
  *               "v":"0x0",
  *               "s":"0x0080"
  *            }
  *         ]
  *      }
  *}
  *
  *
  */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace std;

#include "msgClass.h" /* for ... msgClassSock      */

#define MAX_HOST_SENSORS (100)

/* Control structure used for ipmitool related functions ; like sensor monitoring */
#define DEFAULT_IPMITOOL_SENSOR_MONITORING_PERIOD_SECS (120)         /* 2 minutes */

#define IPMITOOL_JSON__MONITOR_READY_HEADER       ((const char *)("sensor_monitor_ready"))
#define IPMITOOL_JSON__CONFIG_REQUEST_HEADER      ((const char *)("ipmitool_sensor_monitor_config"))
#define IPMITOOL_JSON__CONFIG_RESPONSE_HEADER     ((const char *)("ipmitool_sensor_monitor_config_response"))
#define IPMITOOL_JSON__SENSOR_DATA_MESSAGE_HEADER ((const char *)("ipmitool_sensor_data"))

#define IPMITOOL_JSON__ANALOG_LABEL   ((const char *)("analog"))
#define IPMITOOL_JSON__DISCRETE_LABEL ((const char *)("discrete"))
#define IPMITOOL_SENSOR_QUERY_CMD     ((const char *)(" sensor list"))
// #define IPMITOOL_SENSOR_OUTPUT_FILE   ((const char *)("/tmp/ipmitool_sensor_data"))
#define IPMITOOL_SENSOR_OUTPUT_FILE   ((const char *)("/var/run/ipmitool_sensor_data"))
#define IPMITOOL_PATH_AND_FILENAME    ((const char *)("/usr/bin/ipmitool"))
#define IPMITOOL_PATH_AND_FILENAME_V  ((const char *)("/home/wrsroot/test/ipmitool"))

#define IPMITOOL_MAX_FIELD_LEN      (64)

typedef struct
{
    char name   [IPMITOOL_MAX_FIELD_LEN] ; /* sensor name             */
    char value  [IPMITOOL_MAX_FIELD_LEN] ; /* sensor value            */
    char unit   [IPMITOOL_MAX_FIELD_LEN] ; /* sensor unit type        */
    char status [IPMITOOL_MAX_FIELD_LEN] ; /* status - ok, nc, cr, nr */
    char lnr    [IPMITOOL_MAX_FIELD_LEN] ; /* Lower Non-Recoverable   */
    char lcr    [IPMITOOL_MAX_FIELD_LEN] ; /* Lower Critical          */
    char lnc    [IPMITOOL_MAX_FIELD_LEN] ; /* Lower Non-Critical      */
    char unc    [IPMITOOL_MAX_FIELD_LEN] ; /* Upper Non-Critical      */
    char ucr    [IPMITOOL_MAX_FIELD_LEN] ; /* Upper Critical          */
    char unr    [IPMITOOL_MAX_FIELD_LEN] ; /* Upper Non-Recoverable   */
} ipmitool_sample_type ;

#define IPMITOOL_FIT_LINE_LEN (1000)
typedef struct
{
    bool enable                       ;
    bool exclude_discrete_sensors     ;
    bool include_discrete_sensors     ;
    bool exclude_analog_sensors       ;
    bool include_analog_sensors       ;
    bool exclude_sensors              ;
    int  code                         ;
    char json [IPMITOOL_FIT_LINE_LEN] ;
} ipmiClient_fit_type ;

typedef struct
{
    bool                    init ; /**< service initialized                  */
    bool              configured ; /**< config command was received          */
    int                 interval ; /**< audit interval in seconds            */
    struct       mtc_timer timer ; /**< interval audit timer                 */

    bool     want_analog_sensors ; /**< true to send analog sensor data      */
    bool   want_discrete_sensors ; /**< true to send discrete sensor data    */

    int           analog_sensors ; /**< number of analog sensors in a dump   */
    int         discrete_sensors ; /**< number of discrete sensors in a dump */

    string              hostname ; /**< this hosts name                      */
    string        config_request ; /**< original config request string       */
    string         query_request ; /**< sensor query system call request     */

    string         status_string ; /**< empty or error log message           */
    int             parse_errors ; /**< parse or unreadable sensor count     */
    int                   status ; /**< configuration request exec status    */

    msgClassSock* sensor_tx_sock ; /**< sensor data tx socket interface      */
    int           sensor_rx_port ; /**< the hwmond port to send data to      */

    ipmiClient_fit_type      fit ; /**< manage fault insertion testing       */
} ipmiClient_ctrl_type ;

/* module open and close */
void    ipmiClient_init         ( char * hostname );
void    ipmiClient_fini         ( void );
void    ipmiClient_configure    ( void ); /* called by daemon_configure      */

/* service utilities */
int     ipmiClient_config       ( char * config_ptr );
int     ipmiClient_ready        ( string   hostname );
int     ipmiClient_query        ( void );

/* These are interfaces used to manage the socket used
 * to transmit sensor data to the Hardware Monitor.
 *
 * ipmiClient_socket_open passes in the Hardware
 * Monitor's receive port number.
 */
int     ipmiClient_socket_open  ( int sensor_rx_port , string & iface );
void    ipmiClient_socket_close ( void );
bool    ipmiClient_socket_ok    ( void );

/* returns the sensor monitor timer id */
timer_t ipmiClient_tid          ( void );

#endif
