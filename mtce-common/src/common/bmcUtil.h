#ifndef __INCLUDE_BMCUTIL_H__
#define __INCLUDE_BMCUTIL_H__

/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Starling-X BMC Utilities Header
  */

#include <list>

using namespace std;

#include "nodeBase.h"      /* for ...                                  */
#include "threadUtil.h"    /* for ... thread_info_type and utilities   */

#define BMC_OUTPUT_DIR     ((const char *)("/var/run/bmc/"))

/* supported protocol strings */
#define BMC_PROTOCOL__DYNAMIC_STR     ((const char *)("dynamic"))
#define BMC_PROTOCOL__IPMI_STR        ((const char *)("ipmi"))
#define BMC_PROTOCOL__REDFISH_STR     ((const char *)("redfish"))
#define BMC_PROTOCOL__IPMITOOL_STR    ((const char *)("ipmitool"))
#define BMC_PROTOCOL__REDFISHTOOL_STR ((const char *)("redfishtool"))

/* learned graceful and immediate power control command strings */
typedef struct
{
    string graceful   ;
    string immediate  ;
} bmc_power_ctrl_type ;

typedef struct
{
    bmc_power_ctrl_type reset    ;
    bmc_power_ctrl_type poweron  ;
    bmc_power_ctrl_type poweroff ;
} bmc_power_ctrl_cmds_type ;

/* important BMC query info to log and track */
typedef struct
{
    /* common */
    std::string manufacturer      ;

    /*      IPMI Product Info     */
    /* ---------------------------*/
    /* product info */
    std::string product_name      ;
    std::string product_id        ;
    std::string manufacturer_id   ;
    std::string device_id         ;
    std::string serial_number     ;

    /* hw/fw info */
    std::string fw_version        ;
    std::string hw_version        ;

    /*    redfish product info    */
    /* -------------------------- */
    std::string bios_ver ;
    std::string bmc_ver  ;
    std::string pn       ;
    std::string mn       ;
    std::string sn       ;

    /* Used to store bmc power commands and severity levels */
    bmc_power_ctrl_cmds_type power_ctrl ;

    /* state info */
    std::string  restart_cause     ;
    bool         power_on          ;
    unsigned int memory_in_gigs    ;
    unsigned int processors        ;
    std::list<string> links_list   ;

} bmc_info_type ;


/* BMC commands */
typedef enum
{
    BMC_THREAD_CMD__BMC_QUERY = 0,

    BMC_THREAD_CMD__POWER_RESET,
    BMC_THREAD_CMD__POWER_ON,
    BMC_THREAD_CMD__POWER_OFF,
    BMC_THREAD_CMD__POWER_CYCLE,

    BMC_THREAD_CMD__BMC_INFO,
    BMC_THREAD_CMD__POWER_STATUS,
    BMC_THREAD_CMD__RESTART_CAUSE,
    BMC_THREAD_CMD__BOOTDEV_PXE,

    BMC_THREAD_CMD__READ_SENSORS,

    BMC_THREAD_CMD__LAST

} bmc_cmd_enum ;

#define BMC_QUERY_FILE_SUFFIX          ((const char *)("_root_query"))
#define BMC_INFO_FILE_SUFFIX           ((const char *)("_bmc_info"))
#define BMC_POWER_CMD_FILE_SUFFIX      ((const char *)("_power_cmd_result"))
#define BMC_BOOTDEV_CMD_FILE_SUFFIX    ((const char *)("_bootdev"))
#define BMC_RESTART_CAUSE_FILE_SUFFIX  ((const char *)("_restart_cause"))
#define BMC_POWER_STATUS_FILE_SUFFIX   ((const char *)("_power_status"))
#define BMC_SENSOR_OUTPUT_FILE_SUFFIX  ((const char *)("_sensor_data"))
#define BMC_POWER_SENSOR_FILE_SUFFIX   ((const char *)("_power_sensor_data"))
#define BMC_THERMAL_SENSOR_FILE_SUFFIX ((const char *)("_thermal_sensor_data"))

#define BMC_POWER_ON_STATUS       ((const char *)("on"))
#define BMC_POWER_OFF_STATUS      ((const char *)("off"))

#define BMC_MAX_RECV_RETRIES      (10)

/* get the thread command name string */
string bmcUtil_getCmd_str      ( int command );
string bmcUtil_getAction_str   ( int action  );
string bmcUtil_getProtocol_str ( bmc_protocol_enum protocol );
string bmcUtil_chop_system_req ( string request );

/* module initialization */
int bmcUtil_init ( void );

/* bmc info initialization */
void bmcUtil_info_init ( bmc_info_type & bmc_info );

/* create the a password file */
void bmcUtil_create_pw_file ( thread_info_type * info_ptr,
                                        string   pw_file_content,
                             bmc_protocol_enum   protocol );

/* create the output filename */
string bmcUtil_create_data_fn ( string & hostname,
                                string   file_suffix,
                     bmc_protocol_enum   protocol );

/*  Get power state from query response data. */
int bmcUtil_is_power_on ( string              hostname,
                          bmc_protocol_enum   protocol,
                          string            & response,
                          bool              & power_on);

void bmcUtil_remove_files ( string hostname,
                            bmc_protocol_enum protocol );

#include "ipmiUtil.h"      /* for ... mtce-common ipmi utility header    */
#include "redfishUtil.h"   /* for ... mtce-common redfish utility header */

#endif
