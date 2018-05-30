#ifndef __INCLUDE_MTCIPMIUTIL_H__
#define __INCLUDE_MTCIPMIUTIL_H__

/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud's Maintenance IPMI Utilities Header
  */

#include "nodeBase.h"         /* for ...                       */

#define MC_INFO_LABEL_DELIMITER        ((const char *)(": "))
#define MC_INFO_LABEL_FW_VERSION       ((const char *)("Firmware Revision"))
#define MC_INFO_LABEL_HW_VERSION       ((const char *)("Device Revision"))
#define MC_INFO_LABEL_DEVICE_ID        ((const char *)("Device ID"))
#define MC_INFO_LABEL_PRODUCT_ID       ((const char *)("Product ID"))
#define MC_INFO_LABEL_PRODUCT_NAME     ((const char *)("Product Name"))
#define MC_INFO_LABEL_MANUFACTURE_ID   ((const char *)("Manufacturer ID"))
#define MC_INFO_LABEL_MANUFACTURE_NAME ((const char *)("Manufacturer Name"))

#define IPMITOOL_POWER_RESET_CMD       ((const char *)("chassis power reset"))
#define IPMITOOL_POWER_RESET_RESP      ((const char *)("Chassis Power Control: Reset"))

#define IPMITOOL_POWER_OFF_CMD         ((const char *)("chassis power off"))
#define IPMITOOL_POWER_OFF_RESP        ((const char *)("Chassis Power Control: Down/Off"))

#define IPMITOOL_POWER_ON_CMD          ((const char *)("chassis power on"))
#define IPMITOOL_POWER_ON_RESP         ((const char *)("Chassis Power Control: Up/On"))

#define IPMITOOL_POWER_CYCLE_CMD       ((const char *)("chassis power cycle"))
#define IPMITOOL_POWER_CYCLE_RESP      ((const char *)("Chassis Power Control: Cycle"))

#define IPMITOOL_POWER_STATUS_CMD      ((const char *)("chassis power status"))
#define IPMITOOL_POWER_ON_STATUS       ((const char *)("Chassis Power is on"))
#define IPMITOOL_POWER_OFF_STATUS      ((const char *)("Chassis Power is off"))

#define IPMITOOL_RESTART_CAUSE_CMD     ((const char *)("chassis restart_cause"))

#define IPMITOOL_MC_INFO_CMD           ((const char *)("mc info"))

#define IPMITOOL_CMD_FILE_SUFFIX            ((const char *)("_power_cmd_result"))
#define IPMITOOL_MC_INFO_FILE_SUFFIX        ((const char *)("_mc_info"))
#define IPMITOOL_RESTART_CAUSE_FILE_SUFFIX  ((const char *)("_restart_cause"))
#define IPMITOOL_POWER_STATUS_FILE_SUFFIX   ((const char *)("_power_status"))

#define IPMITOOL_MAX_RECV_RETRIES      (10)

/* Warning : Changes here require 'mtc_ipmiRequest_str' string array to be updated */
typedef enum
{
    IPMITOOL_THREAD_CMD__NULL = 0,
    IPMITOOL_THREAD_CMD__POWER_RESET,

    IPMITOOL_THREAD_CMD__POWER_ON,
    IPMITOOL_THREAD_CMD__POWER_OFF,
    IPMITOOL_THREAD_CMD__POWER_CYCLE,

    IPMITOOL_THREAD_CMD__MC_INFO,
    IPMITOOL_THREAD_CMD__POWER_STATUS,
    IPMITOOL_THREAD_CMD__RESTART_CAUSE,
    IPMITOOL_THREAD_CMD__LAST

} ipmitool_cmd_enum ;

const char * getIpmiCmd_str ( int command );
const char * getIpmiAction_str ( int command );


typedef struct
{
    std::string product_name      ;
    std::string product_id        ;
    std::string manufacturer_name ;
    std::string manufacturer_id   ;
    std::string device_id         ;
    std::string fw_version        ;
    std::string hw_version        ;
} mc_info_type ;

int  ipmiUtil_mc_info_load ( string hostname, const char * filename, mc_info_type & mc_info );
void ipmiUtil_mc_info_init                                         ( mc_info_type & mc_info );

#endif
