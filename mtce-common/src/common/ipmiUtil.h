#ifndef __INCLUDE_IPMIUTIL_H__
#define __INCLUDE_IPMIUTIL_H__

/*
 * Copyright (c) 2017, 2024 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud's Maintenance Common IPMI Utilities Header
  */

#include "bmcUtil.h"       /* for ... mtce-common bmc utility header   */

#define THREAD_NAME__BMC             ((const char *)("bmc"))
#define IPMITOOL_PATH_AND_FILENAME   ((const char *)("/usr/bin/ipmitool"))
#define IPMITOOL_OUTPUT_DIR          ((const char *)("/var/run/bmc/ipmitool/"))

#define BMC_INFO_LABEL_DELIMITER        ((const char *)(": "))
#define BMC_INFO_LABEL_FW_VERSION       ((const char *)("Firmware Revision"))
#define BMC_INFO_LABEL_HW_VERSION       ((const char *)("Device Revision"))
#define BMC_INFO_LABEL_DEVICE_ID        ((const char *)("Device ID"))
#define BMC_INFO_LABEL_PRODUCT_ID       ((const char *)("Product ID"))
#define BMC_INFO_LABEL_PRODUCT_NAME     ((const char *)("Product Name"))
#define BMC_INFO_LABEL_MANUFACTURE_ID   ((const char *)("Manufacturer ID"))
#define BMC_INFO_LABEL_MANUFACTURE_NAME ((const char *)("Manufacturer Name"))

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

#define IPMITOOL_BOOTDEV_PXE_CMD       ((const char *)("chassis bootdev pxe"))
#define IPMITOOL_BOOTDEV_PXE_RESP      ((const char *)("Set Boot Device to pxe"))

#define IPMITOOL_BMC_INFO_CMD          ((const char *)("mc info"))

#define BMC__MAX_RECV_RETRIES      (10)

int ipmiUtil_init ( void );

int  ipmiUtil_bmc_info_load ( string hostname, const char * filename, bmc_info_type & mc_info );

int  ipmiUtil_reset_host_now ( string hostname, bmcUtil_accessInfo_type accessInfo, string output_filename );

/* Create the ipmi request */
string ipmiUtil_create_request ( string cmd, string & ip, string & un, string & pw);

#endif
