#ifndef __INCLUDE_FITCODES_H__
#define __INCLUDE_FITCODES_H__
/*
 * Copyright (c) 2013, 2016, 2024 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Common Fault Insertion Code Definitions
  */

/*************************************************************************************
 *
 * These definitions are used for fault insertion testing.
 *
 * Here are examples of how they are used,
 *
 * - touch the 'no_reboot' file on the mtcClient to cause it to
 *   servie the reboot request but don't actually reboot
 *
 * - touch the 'no_mgmnt_ack' file on the mtcClient to cause
 *   it to handle command requests but drop/not send the ack message
 *   if it came in on the management network ; same for cluster-host
 *
 * - touch the 'no_mtcAlive file to tell mtcClient to stop sending
 *   its mtcAlive messages while this file is present.
 *
 **************************************************************************************/

/**
 *  This is the Fault Insertion Dir - Code that looks for multiple fit files need not
 *  bother if the dir is not present
 **/
#define MTC_CMD_FIT__DIR          ("/var/run/fit")


#define MTC_CMD_FIT__NO_REBOOT    ("/var/run/fit/no_reboot")    /* mtcClient */
#define MTC_CMD_FIT__NO_RESET     ("/var/run/fit/no_reset")     /* mtcClient */
#define MTC_CMD_FIT__NO_WIPEDISK  ("/var/run/fit/no_wipedisk")  /* mtcClient */
#define MTC_CMD_FIT__NO_MGMNT_ACK ("/var/run/fit/no_mgmnt_ack") /* mtcClient */
#define MTC_CMD_FIT__NO_CLSTR_ACK ("/var/run/fit/no_clstr_ack") /* mtcClient */
#define MTC_CMD_FIT__NO_MTCALIVE  ("/var/run/fit/no_mtcalive")  /* mtcClient */
#define MTC_CMD_FIT__PXEBOOT_RXSOCK ("/var/run/fit/pxeboot_rxsock") /* mtcClient */
#define MTC_CMD_FIT__PXEBOOT_TXSOCK ("/var/run/fit/pxeboot_txsock") /* mtcClient */
#define MTC_CMD_FIT__MGMNT_RXSOCK ("/var/run/fit/mgmnt_rxsock") /* mtcClient */
#define MTC_CMD_FIT__MGMNT_TXSOCK ("/var/run/fit/mgmnt_txsock") /* mtcClient */
#define MTC_CMD_FIT__CLSTR_RXSOCK ("/var/run/fit/clstr_rxsock") /* mtcClient */
#define MTC_CMD_FIT__CLSTR_TXSOCK ("/var/run/fit/clstr_txsock") /* mtcClient */
#define MTC_CMD_FIT__AMON_SOCK    ("/var/run/fit/amon_sock")    /* mtcClient */
#define MTC_CMD_FIT__NO_CLSTR_RSP ("/var/run/fit/no_clstr_rsp") /* hbsClient */
#define MTC_CMD_FIT__NO_MGMNT_RSP ("/var/run/fit/no_mgmnt_rsp") /* hbsClient */
#define MTC_CMD_FIT__LINKLIST     ("/var/run/fit/linklist")     /* hbsAgent */
#define MTC_CMD_FIT__HBSSILENT    ("/var/run/fit/hbs_silent_fault") /* hbsAgent */
#define MTC_CMD_FIT__SENSOR_DATA  ("/var/run/fit/sensor_data")      /* hwmond */
#define MTC_CMD_FIT__INLINE_CREDS ("/var/run/fit/inline_creds")     /* mtcAgent */
#define MTC_CMD_FIT__POWER_CMD    ("/var/run/fit/power_cmd_result") /* mtcAgent */
#define MTC_CMD_FIT__ROOT_QUERY   ("/var/run/fit/root_query")       /* mtcAgent */
#define MTC_CMD_FIT__MC_INFO      ("/var/run/fit/mc_info")          /* mtcAgent */
#define MTC_CMD_FIT__POWER_STATUS ("/var/run/fit/power_status")     /* mtcAgent */
#define MTC_CMD_FIT__RESTART_CAUSE ("/var/run/fit/restart_cause")   /* mtcAgent */
#define MTC_CMD_FIT__UPTIME        ("/var/run/fit/uptime")          /* mtcAgent */
#define MTC_CMD_FIT__LOUD_BM_PW    ("/var/run/fit/loud_bm_pw")      /* mtcAgent & hwmond */
#define MTC_CMD_FIT__START_SVCS   ("/var/run/fit/host_services")    /* mtcClient */
#define MTC_CMD_FIT__NO_HS_ACK    ("/var/run/fit/no_hs_ack")        /* mtcClient */
#define MTC_CMD_FIT__GOENABLE_AUDIT ("/var/run/fit/goenable_audit") /* mtcAgent  */
#define MTC_CMD_FIT__JSON_LEAK_SOAK ("/var/run/fit/json_leak_soak") /* mtcAgent  */
#define MTC_CMD_FIT__BMC_ACC_FAIL   ("/var/run/fit/bmc_access_fail")/* mtcAgent  */
#define MTC_CMD_FIT__MEM_LEAK_DEBUG ("/var/run/fit/mem_leak_debug") /* mtcAgent  */
#define MTC_CMD_FIT__FM_ERROR_CODE  ("/var/run/fit/fm_error_code")  /* mtcAgent  */
#define MTC_CMD_FIT__CORRUPT_TOKEN  ("/var/run/fit/corrupt_token")  /* mtcAgent & hwmond */

/*****************************************************
 *           Fault Insertion Codes
 *****************************************************/

/*****************************************************************************
 *
 * the fit /var/run/fit/fitinfo file contains the following format,
 *  - code and process are required
 *  - other fields are optional
 *  - no spaces, exclude <>
 *
 * proc=<process shortname>
 * code=<decimal number>
 * host=<hostname>
 * name=<some string>
 * data=<some string>
 *
 *****************************************************************************/

/***********************   Common FIT Codes **********************************/

#define FIT_CODE__NONE                                (0)
#define FIT_CODE__CORRUPT_TOKEN                       (1)
#define FIT_CODE__ADD_DELETE                          (2)
#define FIT_CODE__STUCK_TASK                          (3)
#define FIT_CODE__AVOID_N_FAIL_BMC_REQUEST            (4)
#define FIT_CODE__THREAD_TIMEOUT                      (5)
#define FIT_CODE__THREAD_SEGFAULT                     (6)
#define FIT_CODE__SIGNAL_NOEXIT                       (7)
#define FIT_CODE__STRESS_THREAD                       (8)
#define FIT_CODE__DO_NOTHING_THREAD                   (9)
#define FIT_CODE__EMPTY_BM_PASSWORD                  (10)
#define FIT_CODE__INVALIDATE_MGMNT_IP                (11)
#define FIT_CODE__INVALIDATE_CLSTR_IP                (12)
#define FIT_CODE__WORK_QUEUE                         (13)
#define FIT_CODE__NO_READY_EVENT                     (14)
#define FIT_CODE__NO_PULSE_REQUEST                   (15)
#define FIT_CODE__NO_PULSE_RESPONSE                  (16)

#define FIT_CODE__TOKEN                              (17)

#define FIT_CODE__FAST_PING_AUDIT_HOST               (20)
#define FIT_CODE__FAST_PING_AUDIT_ALL                (21)

#define FIT_CODE__TRANSLATE_LOCK_TO_FORCELOCK        (30)
#define FIT_CODE__LOCK_HOST                          (31)
#define FIT_CODE__FORCE_LOCK_HOST                    (32)
#define FIT_CODE__UNLOCK_HOST                        (33)
#define FIT_CODE__FAIL_SWACT                         (34)
#define FIT_CODE__FAIL_PXEBOOT_MTCALIVE              (35)

#define FIT_CODE__FM_SET_ALARM                       (40)
#define FIT_CODE__FM_GET_ALARM                       (41)
#define FIT_CODE__FM_CLR_ALARM                       (42)
#define FIT_CODE__FM_QRY_ALARMS                      (43)

#define FIT_CODE__BMC_COMMAND_SEND                   (60)
#define FIT_CODE__BMC_COMMAND_RECV                   (61)

#define FIT_CODE__START_HOST_SERVICES                (70)
#define FIT_CODE__STOP_HOST_SERVICES                 (71)

#define FIT_CODE__SOCKET_SETUP                       (72)
#define FIT_CODE__READ_JSON_FROM_FILE                (73)

#define FIT_CODE__HTTP_WORKQUEUE_OPERATION_FAILED    (75)
#define FIT_CODE__HTTP_WORKQUEUE_REQUEST_TIMEOUT     (76)
#define FIT_CODE__HTTP_WORKQUEUE_CONNECTION_LOSS     (77)

/*****************      Process Fit Codes     ********************************/

/* Hardware Monitor FIT Codes */
#define FIT_CODE__HWMON__CORRUPT_TOKEN               (101)
#define FIT_CODE__HWMON__AVOID_TOKEN_REFRESH         (102)
#define FIT_CODE__HWMON__THREAD_TIMEOUT              (103)
#define FIT_CODE__HWMON__AVOID_SENSOR_QUERY          (104)
#define FIT_CODE__HWMON__SENSOR_STATUS               (105)
#define FIT_CODE__HWMON__STARTUP_STATES_FAILURE      (106)

#define FIT_CODE__HWMON__HTTP_LOAD_SENSORS           (120)
#define FIT_CODE__HWMON__HTTP_ADD_SENSOR             (121)
#define FIT_CODE__HWMON__HTTP_DEL_SENSOR             (122)
#define FIT_CODE__HWMON__HTTP_MOD_SENSOR             (123)

#define FIT_CODE__HWMON__ADD_SENSOR                  (130)
#define FIT_CODE__HWMON__BAD_SENSOR                  (131)
#define FIT_CODE__HWMON__GET_SENSOR                  (132)

#define FIT_CODE__HWMON__CREATE_ORPHAN_SENSOR_ALARM  (136)


#define FIT_CODE__HWMON__HTTP_LOAD_GROUPS            (140)
#define FIT_CODE__HWMON__HTTP_ADD_GROUP              (141)
#define FIT_CODE__HWMON__HTTP_DEL_GROUP              (142)
#define FIT_CODE__HWMON__HTTP_MOD_GROUP              (143)
#define FIT_CODE__HWMON__HTTP_GROUP_SENSORS          (144)

#define FIT_CODE__HWMON__ADD_GROUP                   (150)
#define FIT_CODE__HWMON__BAD_GROUP                   (151)
#define FIT_CODE__HWMON__GET_GROUP                   (152)

#define FIT_CODE__HWMON__CREATE_ORPHAN_GROUP_ALARM   (156)

#define FIT_CODE__HWMON__NO_DATA                     (160)

#define FIT_CODE__HWMON__RAISE_SENSOR_ALARM          (170)
#define FIT_CODE__HWMON__CLEAR_SENSOR_ALARM          (171)
#define FIT_CODE__HWMON__RAISE_GROUP_ALARM           (172)
#define FIT_CODE__HWMON__CLEAR_GROUP_ALARM           (173)

#define FIT_CODE__HWMON__SET_DB_SENSOR_STATUS        (175)
#define FIT_CODE__HWMON__SET_DB_SENSOR_STATE         (176)
#define FIT_CODE__HWMON__SET_DB_GROUP_STATUS         (177)
#define FIT_CODE__HWMON__SET_DB_GROUP_STATE          (178)


#define TESTMASK__MSG__MTCALIVE_STRESS               (0x00000001)

#endif /* __INCLUDE_FITCODES_H__ */
