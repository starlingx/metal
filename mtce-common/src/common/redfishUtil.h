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
#define REDFISH_LABEL__COUNT           ((const char *)("Count"))
#define REDFISH_LABEL__MODEL           ((const char *)("Model"))
#define REDFISH_LABEL__HEALTH          ((const char *)("Health"))
#define REDFISH_LABEL__HEALTHROLLUP    ((const char *)("HealthRollup"))

typedef struct
{
   /* Enabled indicates the resource is available.
    * Disabled indicates the resource has been intentionally made unavailable
    * but it can be enabled.
    * Offline indicates the resource is unavailable intentionally and requires
    * action to be made available.
    * InTest indicates that the component is undergoing testing.
    * Starting indicates that the resource is on its way to becoming available.
    * Absent indicates the resources is physically unavailable */
    string state        ;

    /* Health State of the resource without dependents */
    string health       ;

    /* Health State of the resource and     dependents */
    string health_rollup ;

} redfish_entity_status ;

/*
 *
 * Redfish Data Model Specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0268_2022.2.pdf
 *
 * Redfish Resource and Schema Guide:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP2046_2022.2.pdf
 *
 */

/* Redfish version format is #.#.# or major.minor.revision
 * This feature does not care about revision.
 * The following are the minimum version numbers for major and minor
 * for maintenance to accept it as a selectable option */
#define REDFISH_MIN_MAJOR_VERSION      (1)
#define REDFISH_MIN_MINOR_VERSION      (0)

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

/* maintenance administrative action commands */
#define REDFISHTOOL_ROOT_QUERY_CMD     ((const char *)("root"))
#define REDFISHTOOL_BMC_INFO_CMD       ((const char *)("Systems get"))


/***********  supported reset/power command actions queries **************

Method 1: Single level query through root query 'Actions'

    #ComputerSystem.Reset
        ResetType@Redfish.AllowableValues [list]

Method 1: payload response ( level 1 )

    "Actions": {
        "#ComputerSystem.Reset": {
            "ResetType@Redfish.AllowableValues": [
                "On",
                "ForceOff",
                "GracefulRestart",
                "PushPowerButton",
                "Nmi"
            ],
            "target": "/redfish/v1/Systems/System.Embedded.1/Actions/ComputerSystem.Reset"
        }
    },

               -----or-----

Method 2: Double level query through root query 'Actions'

    #ComputerSystem.Reset
        @Redfish.ActionInfo -> /redfish/v1/Systems/1/ResetActionInfo
            arameters
               AllowableValues [list]

Method 2 level 1 payload response

    "Actions": {
        "#ComputerSystem.Reset": {
            "target": "/redfish/v1/Systems/1/Actions/ComputerSystem.Reset",
            "@Redfish.ActionInfo": "/redfish/v1/Systems/1/ResetActionInfo"
        }
    },

Method 2 level 2 payload response through @Redfish.ActionInfo target

    GET: /redfish/v1/Systems/1/ResetActionInfo

    "@odata.type": "#ActionInfo.v1_1_2.ActionInfo",
    "Name": "Reset Action Info",
    "Parameters": [
        {
            "DataType": "String",
            "AllowableValues": [
                "On",
                "ForceOff",
                "GracefulShutdown",
                "GracefulRestart",
                "ForceRestart",
                "Nmi",
                "ForceOn"
            ],
            "Required": true,
            "Name": "ResetType"
        }
    ],
    "@odata.id": "/redfish/v1/Systems/1/ResetActionInfo",
    "Oem": {},
    "Id": "ResetActionInfo"
} */

/* First level reset/power control GET labels */
#define REDFISH_LABEL__ACTIONS              ((const char *)("Actions"))                           /* level 0 */
#define REDFISH_LABEL__ACTION_RESET         ((const char *)("#ComputerSystem.Reset"))             /* level 1 */
#define REDFISH_LABEL__ACTION_RESET_ALLOWED ((const char *)("ResetType@Redfish.AllowableValues")) /* level 2 */

/* Second level reset/power control GET labels */
#define REDFISH_LABEL__ACTION_INFO      ((const char *)("@Redfish.ActionInfo"))
#define REDFISH_LABEL__PARAMETERS       ((const char *)("Parameters"))
#define REDFISH_LABEL__ALLOWABLE_VALUES ((const char *)("AllowableValues"))

#define REDFISHTOOL_RAW_GET_CMD        ((const char *)("raw GET "))
#define REDFISHTOOL_POWER_RESET_CMD    ((const char *)("Systems reset "))

typedef enum
{
    REDFISH_ACTION__RESET,
    REDFISH_ACTION__POWER_ON,
    REDFISH_ACTION__POWER_OFF,
} redfish_action_enum ;

/* Reset sub-commands */
#define REDFISHTOOL_RESET__GRACEFUL_RESTART      ((const char *)("GracefulRestart"))  /* Perform a graceful shutdown followed by a restart of the system. */
#define REDFISHTOOL_RESET__FORCE_RESTART         ((const char *)("ForceRestart"))     /* Perform an immediate (non-graceful) shutdown, followed by a restart */

/* Power off sub-commands */
#define REDFISHTOOL_POWER_OFF__GRACEFUL_SHUTDOWN ((const char *)("GracefulShutdown")) /* Perform a graceful shutdown and power off. */
#define REDFISHTOOL_POWER_OFF__FORCE_OFF         ((const char *)("ForceOff"))         /* Perform a Non-Graceful immediate power off */

/* Power On sub-commands */
#define REDFISHTOOL_POWER_ON__ON                 ((const char *)("On"))               /* Turn the unit on. */
#define REDFISHTOOL_POWER_ON__FORCE_ON           ((const char *)("ForceOn"))          /* Turn the unit on immediately. */

/* Power Cycle sub-commands */
#define REDFISHTOOL_POWER_CYCLE__POWER_CYCLE     ((const char *)("PowerCycle"))       /*  Perform a power cycle of the unit. */

/* Diagnostic sub-commands */
#define REDFISHTOOL_DIAG__NMI                    ((const char *)("Nmi")               /* Generate a Diagnostic Interrupt to halt the system. */
#define REDFISHTOOL_RESET__PUSH_BUTTON           ((const char *)("PushPowerButton"))  /* Simulate the pressing of the physical power button on this unit */


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

string redfishUtil_get_cmd_option ( redfish_action_enum action,
                                    std::list<string>  host_action_list );

void redfishUtil_load_actions ( string & hostname,
                                bmc_info_type & bmc_info,
                                std::list<string> & host_action_list);

#endif // __INCLUDE_REDFISHUTIL_H__
