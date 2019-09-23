#ifndef __INCLUDE_HWMONTHREAD_HH__
#define __INCLUDE_HWMONTHREAD_HH__

/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River Titanium Cloud Hardware Monitor Threads Header
 *
 */

#define IPMITOOL_MAX_LINE_LEN        (200)
#define IPMITOOL_MAX_FIELD_LEN        (64)
#define IPMITOOL_FIT_LINE_LEN       (1000)

#define REDFISH_SENSOR_LABEL_VOLT           "Voltages"
#define REDFISH_SENSOR_LABEL_TEMP           "Temperatures"
#define REDFISH_SENSOR_LABEL_FANS           "Fans"
#define REDFISH_SENSOR_LABEL_POWER_SUPPLY   "PowerSupplies"
#define REDFISH_SENSOR_LABEL_POWER_CTRL     "PowerControl"

#define REDFISH_SENSOR_LABEL_VOLT_READING          "ReadingVolts"
#define REDFISH_SENSOR_LABEL_TEMP_READING          "ReadingCelsius"
#define REDFISH_SENSOR_LABEL_FANS_READING          "Reading"
#define REDFISH_SENSOR_LABEL_POWER_SUPPLY_READING  "None"
#define REDFISH_SENSOR_LABEL_POWER_CTRL_READING    "PowerConsumedWatts"

#define REDFISH_SEVERITY__GOOD     "OK"
#define REDFISH_SEVERITY__MAJOR    "Warning"
#define REDFISH_SEVERITY__CRITICAL "Critical"

#define BMC_SENSOR_DEFAULT_UNIT_TYPE_TEMP    "degrees"
#define BMC_SENSOR_DEFAULT_UNIT_TYPE_VOLT    "Volts"
#define BMC_SENSOR_DEFAULT_UNIT_TYPE_FANS    "RPM"
#define BMC_SENSOR_DEFAULT_UNIT_TYPE_POWER   "Watts"

#define BMC_SENSOR_POWER_GROUP      0
#define BMC_SENSOR_THERMAL_GROUP    1

void * hwmonThread_bmc ( void * );

 /* --------------------
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
  * Json String: sensor data excluded
  * -----------
  *
  * {
  *   "ipmitool_sensor_data":
  *      {
  *         "hostname"      :"compute-0",
  *         "status"        : 0,
  *         "status_string" : "pass",
  *         "sensors"  :
  *         [
  *            { },
  *            { },
  *            { }
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
  *         "sensors":[
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
  *            ...
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

#define THREAD_RETRY_DELAY_SECS  (60)
#define MAX_THREAD_RETRIES       (10)

#define BMC_JSON__SENSOR_DATA_MESSAGE_HEADER ((const char *)("bmc_sensor_data"))

#define BMC_JSON__SENSORS_LABEL              ((const char *)("sensors"))
#define IPMITOOL_SENSOR_QUERY_CMD            ((const char *)(" sensor list"))

#define BMC_POWER_SENSOR_OUTPUT_FILE_SUFFIX   ((const char *)("_power_sensor_data"))
#define BMC_THERMAL_SENSOR_OUTPUT_FILE_SUFFIX ((const char *)("_thermal_sensor_data"))

#define REDFISHTOOL_READ_POWER_SENSORS_CMD   ((const char *)("Chassis Power"))
#define REDFISHTOOL_READ_THERMAL_SENSORS_CMD ((const char *)("Chassis Thermal"))

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
} bmc_sample_type ;

typedef struct
{
    string bm_ip ;
    string bm_un ;
    string bm_pw ;

    int    samples ;
} thread_extra_info_type ;

#endif // __INCLUDE_HWMONTHREAD_HH__
