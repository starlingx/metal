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

#include "nodeBase.h"      /* for ...                                  */
#include "threadUtil.h"    /* for ... thread_info_type and utilities   */

#define BMC_OUTPUT_DIR  ((const char *)("/var/run/bmc/"))

#define BMC_DEFAULT_INFO ((const char *)("{\"power_state\":\"off\",\"protocol\":\"ipmitool\"}"))


/* important BMC query info to log and track */
typedef struct
{
    std::string product_name      ;
    std::string product_id        ;
    std::string manufacturer_name ;
    std::string manufacturer_id   ;
    std::string device_id         ;
    std::string fw_version        ;
    std::string hw_version        ;
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

#define BMC_MAX_RECV_RETRIES      (10)

/* get the thread command name string */
string bmcUtil_getCmd_str      ( int command );
string bmcUtil_getAction_str   ( int action  );
string bmcUtil_getProtocol_str ( bmc_protocol_enum protocol );

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

#include "ipmiUtil.h"      /* for ... mtce-common ipmi utility header    */
#include "redfishUtil.h"   /* for ... mtce-common redfish utility header */

#endif
