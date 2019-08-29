#ifndef __INCLUDE_REDFISHUTIL_H__
#define __INCLUDE_REDFISHUTIL_H__

/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Starling-X Common Redfish Utilities Header
  */

#include "bmcUtil.h"       /* for ... mtce-common bmc utility header     */

#define REDFISHTOOL_PATH_AND_FILENAME  ((const char *)("/usr/bin/redfishtool"))
#define REDFISHTOOL_OUTPUT_DIR         ((const char *)("/var/run/bmc/redfishtool/"))

/* generic labels */
#define REDFISH_LABEL__STATUS          ((const char *)("Status"))
#define REDFISH_LABEL__STATE           ((const char *)("State"))
#define REDFISH_LABEL__HEALTH          ((const char *)("Health"))
#define REDFISH_LABEL__COUNT           ((const char *)("Count"))
#define REDFISH_LABEL__MODEL           ((const char *)("Model"))

/* redfish version */
#define REDFISH_MIN_MAJOR_VERSION      (1)
#define REDFISH_LABEL__REDFISH_VERSION ((const char *)("RedfishVersion"))

/* bmc info labels */
#define REDFISH_LABEL__BMC_VERSION     ((const char *)("BmcVersion"))
#define REDFISH_LABEL__SERIAL_NUMBER   ((const char *)("SerialNumber"))
#define REDFISH_LABEL__PART_NUMBER     ((const char *)("PartNumber"))
#define REDFISH_LABEL__MANUFACTURER    ((const char *)("Manufacturer"))
#define REDFISH_LABEL__BIOS_VERSION    ((const char *)("BiosVersion"))
#define REDFISH_LABEL__MODEL_NUMBER    ((const char *)("Model"))
#define REDFISH_LABEL__POWER_STATE     ((const char *)("PowerState"))

/* server memory size labels */
#define REDFISH_LABEL__MEMORY          ((const char *)("MemorySummary"))
#define REDFISH_LABEL__MEMORY_TOTAL    ((const char *)("TotalSystemMemoryGiB"))

/* server processor info label */
#define REDFISH_LABEL__PROCESSOR       ((const char *)("ProcessorSummary"))

/* supported actions */
#define REDFISH_LABEL__ACTIONS         ((const char *)("Actions"))
#define REDFISH_LABEL__ACTION_RESET    ((const char *)("#ComputerSystem.Reset"))
#define REDFISH_LABEL__ACTION_RESET_ALLOWED ((const char *)("ResetType@Redfish.AllowableValues"))

/* maintenance administrative action commands */
#define REDFISHTOOL_ROOT_QUERY_CMD     ((const char *)("root"))
#define REDFISHTOOL_BMC_INFO_CMD       ((const char *)("Systems get"))
#define REDFISHTOOL_POWER_RESET_CMD    ((const char *)("Systems reset GracefulRestart"))
#define REDFISHTOOL_POWER_ON_CMD       ((const char *)("Systems reset On"))
#define REDFISHTOOL_POWER_OFF_CMD      ((const char *)("Systems reset ForceOff"))
#define REDFISHTOOL_BOOTDEV_PXE_CMD    ((const char *)("Systems setBootOverride Once Pxe"))


/* no support response string
 *
 * redfishtool:Transport: Response Error: status_code: 404 -- Not Found
 *
 */
#define REDFISHTOOL_RESPONSE_ERROR                 ((const char *)("Response Error"))
#define REDFISHTOOL_ERROR_STATUS_CODE__NOT_FOUND   ((const char *)("status_code: 404"))
#define REDFISHTOOL_ERROR_STATUS_CODE__NOT_ALLOWED ((const char *)("status_code: 405"))

/* module init */
int redfishUtil_init ( void );

/* create a redfish tool thread request */
string redfishUtil_create_request ( string   cmd,
                                    string & ip,
                                    string & config_file,
                                    string & out );

/* interpret redfish root query response and check version */
bool redfishUtil_is_supported ( string & hostname, string & root_query_response );

/* get, load and log bmc info from redfish info query response */
int redfishUtil_get_bmc_info ( string & hostname,
                               string & response,
                               bmc_info_type & bmc_info );

#endif // __INCLUDE_REDFISHUTIL_H__
