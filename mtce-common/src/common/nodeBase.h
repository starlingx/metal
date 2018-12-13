#ifndef __INCLUDE_NODEBASE_HH__
#define __INCLUDE_NODEBASE_HH__
/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform "Node Base" Header
  */

#include <iostream>
#include <string.h>
#include <arpa/inet.h>

using namespace std;

#include "fitCodes.h"
#include "logMacros.h"
#include "returnCodes.h"
#include "nodeTimers.h"

#ifndef ALIGN_PACK
#define ALIGN_PACK(x) __attribute__((packed)) x
#endif

/* Out-Of-Service Stress tests */
#define WANT_SYSINV_API_STRESS          0x00000001
#define WANT_SERVICE_UPDOWN_API_STRESS  0x00000002
#define WANT_SMGR_API_STRESS            0x00000004
#define WANT_FM_API_STRESS              0x00000008
#define WANT_TOKEN_REFRESH_STRESS       0x00000010

void daemon_exit ( void );

#define MAX_EVENT_BUF               (2048)
#define MAX_RECV_FAILS_B4_RECONNECT    (4)
#define MAX_INFLIGHT_HTTP_REQUESTS     (7)
#define MAX_FILE_SIZE                (128)
#define MAX_FSM_SSH2_RETRIES          (20)
#define MAX_POWERCYCLE_STAGE_RETRIES   (3)
#define MAX_POWERCYCLE_ATTEMPT_RETRIES (3)
#define MAX_POWERCYCLE_QUERY_RETRIES  (10)
#define MAX_BMC_POWER_CTRL_RETRIES     (5)

/* Added for failure handling offline feature */
#define MIN_OFFLINE_PERIOD_MSECS      (10)
#define MIN_OFFLINE_THRESHOLD          (1)

/* Board management Expect Script return Codes: 1xx range
 * These codes are also defined in /usr/local/sbin/bmvars.exp */
#define FAIL_BM_UNSUPPORTED         (100*256)
#define FAIL_BM_INV_QUERY           (101*256)
#define FAIL_BM_NO_IP               (102*256)

#define FAIL_BM_PING_TEST           (112*256)
#define FAIL_BM_PING_SPAWN          (113*256)
#define FAIL_BM_PING_TIMEOUT        (114*256)
#define FAIL_BM_PING_ZERO_PERCENT   (115*256)
#define FAIL_BM_PING_FIFTY_PERCENT  (116*256)

#define FAIL_BM_USERNAME            (120*256)
#define FAIL_BM_IPADDR              (121*256)
#define FAIL_BM_PASSWORD            (122*256)


#define MTC_PARM_UPTIME_IDX (0)
#define MTC_PARM_HEALTH_IDX (1)
#define MTC_PARM_FLAGS_IDX  (2)
#define MTC_PARM_MAX_IDX    (3)

/** 'I Am <state>' flags for maintenance.
  *
  * These flags are shipped in the parm[2] if the
  * mtcAlive message from each host. */
#define MTC_FLAG__I_AM_CONFIGURED  (0x00000001)
#define MTC_FLAG__I_AM_NOT_HEALTHY (0x00000002)
#define MTC_FLAG__I_AM_HEALTHY     (0x00000004)
#define MTC_FLAG__I_AM_LOCKED      (0x00000008)
#define MTC_FLAG__SUBF_CONFIGURED  (0x00000010)
#define MTC_FLAG__MAIN_GOENABLED   (0x00000020)
#define MTC_FLAG__SUBF_GOENABLED   (0x00000040)
#define MTC_FLAG__SM_DEGRADED      (0x00000080)
#define MTC_FLAG__PATCHING         (0x00000100) /* Patching in progress */
#define MTC_FLAG__PATCHED          (0x00000200) /* Patched but not reset */
#define MTC_FLAG__SM_UNHEALTHY     (0x00001000)

#define MTC_UNHEALTHY_THRESHOLD    (3)

/* Node Health States */
#define NODE_HEALTH_UNKNOWN     (0)
#define NODE_HEALTHY            (1)
#define NODE_UNHEALTHY          (2)

#define AUTO_RECOVERY_FILE_SUFFIX  ((const char *)"_ar_count")
#define TMP_DIR_PATH               ((const char *)"/etc/mtc/tmp/")

#define HOST_IS_VIRTUAL        ((const char *)"/var/run/virtual.host")

/** Configuration Pass/Fail Flag File */
#define CONFIG_PASS_FILE        ((const char *)"/var/run/.config_pass")
#define CONFIG_FAIL_FILE        ((const char *)"/var/run/.config_fail")
#define NODE_LOCKED_FILE        ((const char *)"/var/run/.node_locked")
#define NODE_RESET_FILE         ((const char *)"/var/run/.node_reset")
#define SMGMT_DEGRADED_FILE     ((const char *)"/var/run/.sm_degraded")
#define SMGMT_UNHEALTHY_FILE    ((const char *)"/var/run/.sm_node_unhealthy")

/** path to and module init file name */
#define MTCE_CONF_FILE          ((const char *)"/etc/mtc.conf")
#define MTCE_INI_FILE           ((const char *)"/etc/mtc.ini")
#define NFVI_PLUGIN_CFG_FILE    ((const char *)"/etc/nfv/nfv_plugins/nfvi_plugins/config.ini")
#define SYSINV_CFG_FILE         ((const char *)"/etc/sysinv/sysinv.conf")
#define HWMON_CONF_FILE         ((const char *)"/etc/mtc/hwmond.conf")


#define GOENABLED_DIR           ((const char *)"/etc/goenabled.d")    /* generic */
#define GOENABLED_COMPUTE_DIR   ((const char *)"/etc/goenabled.d/compute")
#define GOENABLED_STORAGE_DIR   ((const char *)"/etc/goenabled.d/storage")
#define GOENABLED_CONTROL_DIR   ((const char *)"/etc/goenabled.d/control")

#define GOENABLED_MAIN_READY    ((const char *)"/var/run/.goenabled")
#define GOENABLED_SUBF_READY    ((const char *)"/var/run/.goenabled_subf")

#define GOENABLED_MAIN_PASS     ((const char *)"/var/run/goenabled")
#define GOENABLED_SUBF_PASS     ((const char *)"/var/run/goenabled_subf")
#define GOENABLED_MAIN_FAIL     ((const char *)"/var/run/goenabled_failed")
#define GOENABLED_SUBF_FAIL     ((const char *)"/var/run/goenabled_subf_failed")

#define CONFIG_COMPLETE_CONTROL ((const char *)"/var/run/.controller_config_complete")
#define CONFIG_COMPLETE_COMPUTE ((const char *)"/var/run/.compute_config_complete")
#define CONFIG_COMPLETE_STORAGE ((const char *)"/var/run/.storage_config_complete")
#define CONFIG_COMPLETE_FILE    ((const char *)"/etc/platform/.initial_config_complete")

#define DISABLE_COMPUTE_SERVICES ((const char *)"/var/run/.disable_compute_services")

#define PATCHING_IN_PROG_FILE   ((const char *)"/var/run/patch_installing")
#define NODE_IS_PATCHED_FILE    ((const char *)"/var/run/node_is_patched")

#define PLATFORM_CONF_FILE      ((const char *)"/etc/platform/platform.conf")
#define PLATFORM_CONF_DIR       ((const char *)"/etc/platform")
#define PLATFORM_SIMPLEX_MODE   ((const char *)"/etc/platform/simplex")
#define SERVICES_DIR            ((const char *)"/etc/services.d")
#define SERVER_PROFILE_DIR      ((const char *)"/etc/bmc/server_profiles.d")
#define PASSWORD_FILE           ((const char *)"/etc/passwd")
#define SHADOW_FILE             ((const char *)"/etc/shadow")
#define USERNAME_ROOT           ("wrsroot")

#define PMON_CONF_FILE_DIR      ((const char *)"/etc/pmon.d")

#define BM_DNSMASQ_FILENAME     ((const char *)"dnsmasq.bmc_hosts")

#define THREAD_NAME__IPMITOOL        ((const char *)("ipmitool"))

#define IPMITOOL_PATH_AND_FILENAME   ((const char *)("/usr/bin/ipmitool"))
#define IPMITOOL_OUTPUT_DIR          ((const char *)("/var/run/ipmitool/"))

/** 'lo' interface IP address - TODO: get it from the interface */
#define LOOPBACK_IP "127.0.0.1"
#define LOOPBACK_IPV6 "::1"
#define LOCALHOST   "localhost"

#define NONE (const char *)"none"

/** Largest heartbeat pulse (req/resp) message size */
#define MAX_API_LOG_LEN    (0x1000)
#define MAX_FILENAME_LEN   (100)
#define MAX_SYSTEM_CMD_LEN (200)
#define HBS_PULSES_REQUIRED_FOR_RECOVERY (10)
#define MAX_START_SERVICES_RETRY (20)

#define DEFAULT_MTCALIVE_TIMEOUT    (1200)
#define DEFAULT_GOENABLE_TIMEOUT     (300)
#define DEFAULT_DOR_MODE_TIMEOUT      (20)
#define DEFAULT_DOR_MODE_CPE_TIMEOUT (600)

/** TODO: Convert names to omit JSON part */
#define MTC_JSON_INV_LABEL     "ihosts"
#define MTC_JSON_INV_NEXT      "next"
#define MTC_JSON_INV_UUID      "uuid"
#define MTC_JSON_INV_NAME      "hostname"
#define MTC_JSON_INV_HOSTIP    "mgmt_ip"
#define MTC_JSON_INV_HOSTMAC   "mgmt_mac"
#define MTC_JSON_INV_INFRAIP   "infra_ip"
#define MTC_JSON_INV_AVAIL     "availability"
#define MTC_JSON_INV_OPER      "operational"
#define MTC_JSON_INV_ADMIN     "administrative"
#define MTC_JSON_INV_OPER_SUBF "subfunction_oper"
#define MTC_JSON_INV_AVAIL_SUBF "subfunction_avail"
#define MTC_JSON_INV_TYPE      "personality"
#define MTC_JSON_INV_FUNC      "subfunctions" // personality"
#define MTC_JSON_INV_TASK      "task"
#define MTC_JSON_INV_ACTION    "action"
#define MTC_JSON_INV_UPTIME    "uptime"
#define MTC_JSON_INV_BMIP      "bm_ip"
#define MTC_JSON_INV_BMTYPE    "bm_type"
#define MTC_JSON_INV_BMUN      "bm_username"

#define MTC_JSON_SEVERITY      "severity"

/* These Task strings should not be changed without
 * the corresponding change in Horizon.
 *
 * Task strings must be limited to less than 64 bytes.
 *
 **/
#define MAX_TASK_STR_LEN (63) /* leave room for null termination */
#define MTC_TASK_DISABLE_CONTROL   "Disabling Controller"
#define MTC_TASK_DISABLE_SERVICES  "Disabling Services"
#define MTC_TASK_UNLOCK_FAILED     "Unlock Operation Failed"
#define MTC_TASK_REBOOT_REQUEST    "Reboot Request"
#define MTC_TASK_RESET_REQUEST     "Reset Request"
#define MTC_TASK_REBOOTING         "Rebooting"
#define MTC_TASK_RESETTING         "Resetting"
#define MTC_TASK_REBOOT_FAIL       "Reboot Failed"
#define MTC_TASK_REBOOT_TIMEOUT    "Reboot/Reset Timeout"
#define MTC_TASK_REBOOT_FAIL_RETRY "Reboot Failed, retrying (%d of %d)"
#define MTC_TASK_REBOOT_ABORT      "Reboot Failed, try again when host is 'online'"
#define MTC_TASK_RESET_PROG        "Rebooting/Resetting Host"
#define MTC_TASK_REINSTALL         "Reinstalling Host"
#define MTC_TASK_REINSTALL_FAIL    "Reinstall Failed"
#define MTC_TASK_REINSTALL_SUCCESS "Reinstall Succeeded"
#define MTC_TASK_BOOTING           "Booting"
#define MTC_TASK_BOOT_FAIL         "Boot Failed, rebooting"
#define MTC_TASK_TESTING           "Testing"
#define MTC_TASK_INITIALIZING      "Initializing"
#define MTC_TASK_INIT_FAIL         "Initialization Failed, recovering"
#define MTC_TASK_START_SERVICE_FAIL "Start Services Failed"
#define MTC_TASK_START_SERVICE_TO  "Start Services Timeout"
#define MTC_TASK_ENABLE_WORK_FAIL  "Enable Action Failed"
#define MTC_TASK_ENABLE_WORK_TO    "Enable Action Timeout"
#define MTC_TASK_ENABLE_FAIL_HB    "Enable Heartbeat Failure, re-enabling"
#define MTC_TASK_RECOVERY_FAIL     "Graceful Recovery Failed, re-enabling"
#define MTC_TASK_RECOVERY_WAIT     "Graceful Recovery Wait"
#define MTC_TASK_RECOVERED         "Gracefully Recovered"

#define MTC_TASK_ENABLING          "Enabling"
#define MTC_TASK_MAIN_CONFIG_FAIL  "Configuration Failed, re-enabling"
#define MTC_TASK_MAIN_CONFIG_TO    "Configuration Timeout, re-enabling"
#define MTC_TASK_MAIN_INTEST_FAIL  "In-Test Failed, re-enabling"
#define MTC_TASK_MAIN_INTEST_TO    "In-Test Timeout, re-enabling"
#define MTC_TASK_MAIN_SERVICE_FAIL "Start Services Failed, re-enabling"
#define MTC_TASK_MAIN_SERVICE_TO   "Start Services Timeout, re-enabling"

#define MTC_TASK_ENABLING_SUBF     "Enabling Compute Service"
#define MTC_TASK_SUBF_CONFIG_FAIL  "Compute Configuration Failed, re-enabling"
#define MTC_TASK_SUBF_CONFIG_TO    "Compute Configuration Timeout, re-enabling"
#define MTC_TASK_SUBF_INTEST_FAIL  "Compute In-Test Failed, re-enabling"
#define MTC_TASK_SUBF_INTEST_TO    "Compute In-Test Timeout, re-enabling"
#define MTC_TASK_SUBF_SERVICE_FAIL "Compute Start Services Failed, re-enabling"
#define MTC_TASK_SUBF_SERVICE_TO   "Compute Start Services Timeout, re-enabling"

#define MTC_TASK_AR_DISABLED_CONFIG    "Configuration failure, threshold reached, Lock/Unlock to retry"
#define MTC_TASK_AR_DISABLED_GOENABLE  "In-Test Failure, threshold reached, Lock/Unlock to retry"
#define MTC_TASK_AR_DISABLED_SERVICES  "Service Failure, threshold reached, Lock/Unlock to retry"
#define MTC_TASK_AR_DISABLED_ENABLE    "Enable Failure, threshold reached, Lock/Unlock to retry"
#define MTC_TASK_AR_DISABLED_HEARTBEAT "Heartbeat Failure, threshold reached, Lock/Unlock to retry"

#define MTC_TASK_RESET_FAIL        "Reset Failed"
#define MTC_TASK_RESET_QUEUE       "Reset Failed, retrying (%d of %d)"
#define MTC_TASK_POWERON_FAIL      "Power-On Failed"
#define MTC_TASK_POWERON_QUEUE     "Power-On Failed, retrying (%d of %d)"
#define MTC_TASK_POWEROFF_FAIL     "Power-Off Failed"
#define MTC_TASK_POWEROFF_QUEUE    "Power-Off Failed, retrying (%d of %d)"

#define MTC_TASK_BMC_NOT_PROV      "Request Failed, Management Controller Not Provisioned"
#define MTC_TASK_BMC_NOT_CONNECTED "Request Failed, Management Controlle  Not Accessible"

#define MTC_TASK_DISABLE_REQ       "Requesting Disable"
#define MTC_TASK_MIGRATE_INSTANCES "Migrating Instances"
#define MTC_TASK_DISABLE_SERVICES  "Disabling Services"
#define MTC_TASK_POWERCYCLE_HOST   "Critical Event Power-Cycle %d; due to critical sensor"
#define MTC_TASK_POWERCYCLE_HOLD   "Critical Event Power-Cycle %d; recovery in %d minute(s)"
#define MTC_TASK_POWERCYCLE_COOL   "Critical Event Power-Cycle %d; power-on in %d minute(s)"
#define MTC_TASK_POWERCYCLE_ON     "Critical Event Power-Cycle %d; power-on host"
#define MTC_TASK_POWERCYCLE_RETRY  "Critical Event Power-Cycle %d; power-on host (retry)"
#define MTC_TASK_POWERCYCLE_BOOT   "Critical Event Power-Cycle %d; host is booting"
#define MTC_TASK_POWERCYCLE_FAIL   "Critical Event Power-Cycle %d; failed"
#define MTC_TASK_POWERCYCLE_DOWN   "Critical Event Power-Down ; due to persistent critical sensor"
#define MTC_TASK_RESETTING_HOST    "Resetting Host, critical sensor"
#define MTC_TASK_CPE_SX_UNLOCK_MSG "Unlocking, please stand-by while the system gracefully reboots"
#define MTC_TASK_SELF_UNLOCK_MSG   "Unlocking active controller, please stand-by while it reboots"
#define MTC_TASK_FAILED_SWACT_REQ  "Critical failure.Requesting SWACT to enabled standby controller"
#define MTC_TASK_FAILED_NO_BACKUP  "Critical failure.Please provision/enable standby controller"

#define COMMAND_RETRY_DELAY         (8)      /* from sshUtil.h */
#define COMMAND_DELAY               (2)      /* from sshUtil.h */

#define MTC_POWER_ACTION_RETRY_DELAY     (20)
#define MTC_POWER_ACTION_RETRY_COUNT     (10)
#define MTC_RESET_ACTION_RETRY_COUNT     (5)

/* number of calls to the bm_handler while bm_access is not confirmed */
#define MTC_MAX_B2B_BM_ACCESS_FAIL_COUNT_B4_ALARM (5)
                                                                         /* string too long for inv */
#define MTC_TASK_DISABLE_REJ       "Lock Rejected: Incomplete Migration" /* Please Enable More Compute Resources" */
#define MTC_TASK_DISABLE_NOHOST    "Lock Rejected: Please Enable More Compute Resources"
#define MTC_TASK_MIGRATE_FAIL      "Lock Failed: Undetermined Reason"
#define MTC_TASK_DISABLE_NOHOSTS   "Insufficient Enabled Resources for Live Migration"
#define MTC_TASK_DISABLE_FORCE     "Force Lock Reset in Progress"
#define MAX_JSON_INV_GET_HOST_NUM (10)

#define MTC_TASK_SWACT_COMPLETE    "Swact: Complete"
#define MTC_TASK_SWACT_NO_COMPLETE "Swact: Did Not Complete"
#define MTC_TASK_SWACT_REQUEST     "Swact: Request"
#define MTC_TASK_SWACT_INPROGRESS  "Swact: In-Progress"
#define MTC_TASK_SWACT_FAILED      "Swact: Failed"
#define MTC_TASK_SWACT_TIMEOUT     "Swact: Timeout"
#define MTC_TASK_SWACT_NOSERVICE   "Swact: No active services"
#define MTC_TASK_SWACT_FAIL_QUERY  "Swact: Query Services Failed"

/** The character length of a UUID */
#ifndef UUID_LEN
#define UUID_LEN (36)
#endif

/** Range of characters in an IP address */
#define COL_CHARS_IN_MAC_ADDR    (17) /**< Colin del'ed xx:xx:xx:xx:xx:xx */
#define MIN_CHARS_IN_IP_ADDR      (3) /**< Min chars in ipv4 or ipv6 address */
#define MAX_CHARS_IN_IP_ADDR     (INET6_ADDRSTRLEN) /**< Max chars in the longer of IPV4 or IPV6 address */

/* root@controller-0:~# getconf HOST_NAME_MAX
 * 64
 */
#define MAX_CHARS_HOSTNAME       (32)  /**< The largest hostname length    */
// #define MAX_CHARS_HOSTNAME       (64)  /**< The largest hostname length    */
#define MAX_CHARS_FILENAME       (256) /**< The largest hostname length    */

#define MAX_CHARS_ON_LINE        (256) /**> max number of chars on a single line */
#define MAX_CHARS_IN_INT         (65)  /**> max number of chars in an integer    */

/** Maximum number of nodes supported by this module */
#define MAX_NODES                (int)(500)

/** Maximum number of nodes supported by CGTS platform */
#define MAX_HOSTS                (MAX_NODES)

/** Longest hostname size */
#define MAX_HOST_NAME_SIZE  (int) (MAX_CHARS_HOSTNAME)

/** maximum number f queued actions supported */
#define MTC_MAX_QUEUED_ACTIONS (2)

/* 50 milliseconds */
#define SOCKET_WAIT 50000

/* 5 milliseconds */
#define MTCAGENT_SELECT_TIMEOUT (5000)

/* dedicate more idle time in CPE ; there is less maintenance to do */
#define MTCAGENT_CPE_SELECT_TIMEOUT (10000)

/** Number of retries maintenance will do when it experiences
 *  a REST API call failure ; any failure */
#define REST_API_RETRY_COUNT (3)

/** Number of mtcAlive misses before transitioning a locked-disabled
 *  host from 'online' to 'offline'.
 *  See mtcTimers.h for the mtcAlive and offline timer periods */
#define MTC_OFFLINE_MISSES   (1)

/** Number of back to back mtcAlive messages before we allow
 *  a power-off to online transition */
#define MTC_MTCALIVE_HITS_TO_GO_ONLINE (5)

#define CONTROLLER_X ((const char *)"controller-x")
#define CONTROLLER_0 ((const char *)"controller-0")
#define CONTROLLER_1 ((const char *)"controller-1")
#define CONTROLLER_2 ((const char *)"controller-2")
#define CONTROLLER   ((const char *)"controller")

#define STORAGE_0   ((const char *)"storage-0")
#define STORAGE_1   ((const char *)"storage-1")

/* The infrastructure networking floating IP
 *
 * Note: If there is no infra then this label will resolve
 *       to another floating IP on the management network.
 *
 * If there is no Infra network then this label is not and should not be used */
#define CONTROLLER_NFS ((const char *)"controller-nfs")

#define CGTS_NODE_TYPES      4
#define CGTS_NODE_TYPE_SIZE 12
#define CGTS_NODE_NULL      (0x00)
#define CONTROLLER_TYPE     (0x01)
#define COMPUTE_TYPE        (0x02)
#define STORAGE_TYPE        (0x04)
#define CGCS_STORAGE_NFS     0
#define CGCS_STORAGE_CEPH    1

#define MAX_SENSOR_NAME_LEN     64
#define MAX_PROCESS_NAME_LEN    64
#define MAX_MTCE_EVENT_NAME_LEN 64
#define MAX_RESOURCE_NAME_LEN   64

/** RMON message codes **/
#define RMON_CRITICAL (3)
#define RMON_MAJOR    (2)
#define RMON_MINOR    (1)
#define RMON_CLEAR    (0)

/** Interface Codes **/
#define MGMNT_INTERFACE (0)
#define INFRA_INTERFACE (1)


/** Maintenance Inventory struct */
typedef struct
{
    unsigned int nodetype ;
    std::string type     ;
    std::string uuid     ;
    std::string name     ;
    std::string ip       ;
    std::string mac      ;
    std::string infra_ip ;
    std::string admin    ;
    std::string oper     ;
    std::string avail    ;
    std::string task     ;
    std::string action   ;
    std::string uptime   ;
    std::string bm_ip    ;
    std::string bm_un    ;
    std::string bm_type  ;
    std::string id       ;

    /* Added to support sub-function state and status */
    std::string func       ;
    std::string oper_subf  ;
    std::string avail_subf ;

} node_inv_type ;
void node_inv_init (node_inv_type & inv);

#define RECOVERY_STATE__INIT    (0)
#define RECOVERY_STATE__ACTION  (1)
#define RECOVERY_STATE__COOLOFF (2)
#define RECOVERY_STATE__HOLDOFF (3)
#define RECOVERY_STATE__MONITOR (4)
#define RECOVERY_STATE__BLOCKED (5)
typedef struct
{
    int    state    ; /* recovery state       */
    int    holdoff  ; /* holdoff minute count */
    int    queries  ; /* query retries        */
    int    retries  ; /* general retries      */
    int    attempts ; /* unrecovered attempts */
    struct mtc_timer control_timer  ;
    struct mtc_timer recovery_timer ;
} recovery_ctrl_type ;
void recovery_ctrl_init ( recovery_ctrl_type & recovery_ctrl );


const char * get_loopback_header       ( void ) ;
const char * get_hbs_cmd_req_header    ( void ) ;
const char * get_cmd_req_msg_header    ( void ) ;
const char * get_cmd_rsp_msg_header    ( void ) ;
const char * get_msg_rep_msg_header    ( void ) ;
const char * get_compute_msg_header    ( void ) ;
const char * get_mtc_log_msg_hdr       ( void ) ;
const char * get_pmond_pulse_header    ( void ) ;
const char * get_mtce_event_header     ( void ) ;
const char * get_heartbeat_loss_header ( void ) ;
const char * get_heartbeat_event_header( void ) ;
const char * get_heartbeat_ready_header( void ) ;

#define MSG_HEADER_SIZE (18) /* this is the length of the first bytes
                                of every message as a string. Its the message
                                signature or label */

#define MAX_MSG            (1024)

/************************************************
 * Common Maintenace Message Structure Layout   *
 ************************************************
  +--------------------------------------------+
  |             Message Signature              |  18 bytes - MSG_HEADER_SIZE - string return from the above procs
  +--------------------------------------------+
  |        Free Format Header Buffer           |  74 bytes - for 64 byte hostname and a bit extra - 128 bytes - 18 - 36 = 74 bytes
  +--------------------------------------------+
  |        Message Version and Cmd Parms       |  36 bytes - ver/rev, res, cmd, num, parm[5]
  +--------------------------------------------+
              above is 128 bytes 
  +--------------------------------------------+
  |             Message Buffer                 | BUF_SIZE
  +--------------------------------------------+
       full message is 1024 bytes max

 ** Maintenance Message header byte size minus the ver/rev,res,cmd.num,parm[5] */
#define HDR_SIZE ((128)-(sizeof(unsigned int)*8)-(sizeof(unsigned short)*2)-28)
#define BUF_SIZE ((MAX_MSG)-(HDR_SIZE))

#define MTC_CMD_VERSION (1)
#define MTC_CMD_REVISION (0)

#define MTC_CMD_FEATURE_VER__MACADDR_IN_CMD (1)

typedef struct
{
   char hdr[HDR_SIZE] ;
   unsigned short ver ; /* major version number  */
   unsigned short rev ; /* minor revision number */
   unsigned int   res ; /*     a reserved field  */
   unsigned int   cmd ;
   unsigned int   num ; 
   unsigned int   parm[5] ;
   char buf[BUF_SIZE] ;
} ALIGN_PACK(mtc_message_type);

#define MTC_CMD_TX (0)
#define MTC_CMD_RX (1)

int print_mtc_message ( mtc_message_type * msg_ptr );
int print_mtc_message ( mtc_message_type * msg_ptr , bool force );
void print_mtc_message ( string hostname, int direction, mtc_message_type & msg, const char * iface, bool force );

#define MAX_LOG_MSG (6000-HDR_SIZE-MAX_HOST_NAME_SIZE-MAX_FILENAME_LEN)
typedef struct
{
   char header   [HDR_SIZE] ;
   char filename [MAX_FILENAME_LEN+1] ;
   char hostname [MAX_HOST_NAME_SIZE+1] ;
   char logbuffer[MAX_LOG_MSG] ;
} log_message_type;

/** Generic Maintenance Commands */
#define MTC_CMD_ADD_HOST                (0x11110010) /* Add              Host */
#define MTC_CMD_DEL_HOST                (0x11110011) /* Delete           Host */
#define MTC_CMD_MOD_HOST                (0x11110012) /* Query            Host */
#define MTC_CMD_QRY_HOST                (0x11110013) /* Modify           Host */
#define MTC_CMD_START_HOST              (0x11110014) /* Start Monitoring Host */
#define MTC_CMD_STOP_HOST               (0x11110015) /* Stop Monitoring  Host */
#define MTC_CMD_ACTIVE_CTRL             (0x11110016) /* Active Controller     */

#define MTC_CMD_ADD_INST                (0x11110020) /* Add              Inst */
#define MTC_CMD_DEL_INST                (0x11110021) /* Delete           Inst */
#define MTC_CMD_MOD_INST                (0x11110022) /* Query            Inst */
#define MTC_CMD_QRY_INST                (0x11110023) /* Modify           Inst */

#define MTC_CMD_VOTE_INST               (0x11110024) /* Vote             Inst */
#define MTC_CMD_NOTIFY_INST             (0x11110025) /* Notify           Inst */

#define MTC_SERVICE_PMOND               (0xB00BF00D)
#define MTC_SERVICE_RMOND               (0xFAABF00D)
#define MTC_SERVICE_HWMOND              (0xF00BF00D)
#define MTC_SERVICE_HEARTBEAT           (0xBABEF00D)

/** process to process loopback command */
#define MTC_EVENT_LOOPBACK              (0x01010101)

#define MTC_EVENT_GOENABLE_FAIL         (0x7AB00BAE)

#define MTC_ENHANCED_HOST_SERVICES      (0x1B0000B1)

/********************************************************
 * The following 4 definitions are Events signatures 
 * the process monitor service sends to maintenance.
 ********************************************************/

/* Generic Monitor Service ready event */
#define MTC_EVENT_MONITOR_READY         (0xf0f0f0f0)

/* TODO: Obsolete code */
#define MTC_EVENT_RMON_READY            (0x0f0f0f0f)

/** Process Monitor Event codes */ 
#define MTC_EVENT_PMON_CLEAR            (0x02020202) /**< Clear Action         */
#define MTC_EVENT_PMON_CRIT             (0x04040404) /**< Crit Failed Action   */
#define MTC_EVENT_PMON_MAJOR            (0x05050505) /**< Major Degrade Action */
#define MTC_EVENT_PMON_MINOR            (0x08080808) /**< Minor Log action     */
#define MTC_EVENT_PMON_LOG              (0x03030303) /**< Minor Log action     */

/** Process Monitor Event codes */
#define MTC_EVENT_RMON_CLEAR            (0x10101010) /**< Clear Action         */
#define MTC_EVENT_RMON_CRIT             (0x20202020) /**< Crit Failed Action   */
#define MTC_EVENT_RMON_MAJOR            (0x30303030) /**< Major Degrade Action */
#define MTC_EVENT_RMON_MINOR            (0x40404040) /**< Minor Log action     */
#define MTC_EVENT_RMON_LOG              (0x50505050) /**< Minor Log action     */

/** Process Monitor Daemon Running - Event Raise / Clear Codes */
#define MTC_EVENT_PMOND_CLEAR           (0x06060606)
#define MTC_EVENT_PMOND_RAISE           (0x07070707)

/** Host Appears to be Stalled */
#define MTC_EVENT_HOST_STALLED          (0x66600999)

/** Accelerated Virtual Switch Event Codes - Clear, Major and Critical */
#define MTC_EVENT_AVS_CLEAR             (0x12340000)
#define MTC_EVENT_AVS_MAJOR             (0x12340001)
#define MTC_EVENT_AVS_CRITICAL          (0x12340002)
#define MTC_EVENT_AVS_OFFLINE           (0x12340003)

/** Hardware Monitor (hwmond) Action Request Codes 
 *  Action based event messages that hwmond sends to maintenance              */
#define MTC_EVENT_HWMON_CONFIG          (0x11110000) /* Sensor Config Log     */
#define MTC_EVENT_HWMON_CLEAR           (0x11110001) /* Clear Event           */
#define MTC_EVENT_HWMON_MINOR           (0x11110002) /* Raise Minor Alarm     */
#define MTC_EVENT_HWMON_MAJOR           (0x11110003) /* ... Major             */
#define MTC_EVENT_HWMON_CRIT            (0x11110004) /* ... Critical          */
#define MTC_EVENT_HWMON_RESET           (0x11110005) /* Reset the Host        */
#define MTC_EVENT_HWMON_LOG             (0x11110006) /* Create a log          */
#define MTC_EVENT_HWMON_reserved        (0x11110007) /* Reinstall the Host    */
#define MTC_EVENT_HWMON_POWERDOWN       (0x11110008) /* Power Down the Host   */
#define MTC_EVENT_HWMON_POWERCYCLE      (0x11110009) /* Power Cycle the Host  */

/* Specialized Heartbeat Commands */
#define MTC_RESTART_HBS     (0x0000f11f) /**< Restart monitoring specified host   */
#define MTC_BACKOFF_HBS     (0x0000f00f) /**< Cmd Hbs to reduce heartbeat period  */
#define MTC_RECOVER_HBS     (0x00000ff0) /**< Recover to default heartbeat period */

#define MTC_DEGRADE_RAISE   (0x77770001) /**< command to trigger host degrade  */
#define MTC_DEGRADE_CLEAR   (0x77770000) /**< command to clear host degrade    */

/*******************************************************
 * The following 4 definitions are Events signatures 
 * the heartbeat service sends to maintenance 
 *******************************************************/

/** Inform maintenance that the heartbeat service is runing 
 *  and ready to accept control commands */
#define MTC_EVENT_HEARTBEAT_READY       (0x5a5a5a5a)

/** Specified Host has exceeded the heartbeat-miss FAILURE threshold */
#define MTC_EVENT_HEARTBEAT_LOSS        (0x0000fead)
#define MTC_EVENT_HEARTBEAT_RUNNING     (0x0110fead)
#define MTC_EVENT_HEARTBEAT_ILLHEALTH   (0x0001fead)
#define MTC_EVENT_HEARTBEAT_STOPPED     (0x0100fead)

/** Specified Host has exceeded the heartbeat-miss DEGRADE threshold */
#define MTC_EVENT_HEARTBEAT_DEGRADE_SET (0xbeefbeef)

/** Specified Host has exceeded the heartbeat-miss MINOR threshold */
#define MTC_EVENT_HEARTBEAT_MINOR_SET   (0xdadeedad)

/** Specified Host has recovered from MINOR assertion */
#define MTC_EVENT_HEARTBEAT_MINOR_CLR   (0xdad00dad)

/** A degraded but not failed host host has responsed to a heartbeat 
 *  So we can clear its degrade condition */
#define MTC_EVENT_HEARTBEAT_DEGRADE_CLR (0xf00df00d)

/** Response received for voting and notification */
#define MTC_EVENT_VOTE_NOTIFY        (0xfeedfeed)

#define PMOND_MISSING_THRESHOLD    (100) /**< Count before degrade                  */
#define NULL_PULSE_FLAGS    (0xffffffff) /**< Unknown flags value                   */
#define PMOND_FLAG          (0x00000001) /**< Process Monitor O.K. Flag             */
#define INFRA_FLAG          (0x00000002) /**< Infrastructure iface provisioned Flag */

#define CTRLX_MASK          (0x00000300) /**< From/To Controller-0/1/2/3 Number     */
#define CTRLX_BIT      ((unsigned int)8) /**< used to shift right mask into bit 0   */

#define STALL_MON_FLAG      (0x00010000) /**< Flag indicating hang monitor running  */
#define STALL_REC_FLAG      (0x00020000) /**< Flag indicating hbsClient took action */
#define STALL_ERR1_FLAG     (0x00100000) /**< Error 1 Flag                          */
#define STALL_ERR2_FLAG     (0x00200000) /**< Error 2 Flag                          */
#define STALL_ERR3_FLAG     (0x00400000) /**< Error 3 Flag                          */
#define STALL_ERR4_FLAG     (0x00800000) /**< Error 4 Flag                          */
#define STALL_PID1_FLAG     (0x01000000) /**< Monitored process 1 is stalled        */
#define STALL_PID2_FLAG     (0x02000000) /**< Monitored process 2 is stalled        */
#define STALL_PID3_FLAG     (0x04000000) /**< Monitored process 3 is stalled        */
#define STALL_PID4_FLAG     (0x08000000) /**< Monitored process 4 is stalled        */
#define STALL_PID5_FLAG     (0x10000000) /**< Monitored process 5 is stalled        */
#define STALL_PID6_FLAG     (0x20000000) /**< Monitored process 6 is stalled        */
#define STALL_PID7_FLAG     (0x40000000) /**< Monitored process 7 is stalled        */
#define STALL_REC_FAIL_FLAG (0x80000000) /**< Auto recover failed, still running    */

#define STALL_ERROR_FLAGS   (STALL_ERR1_FLAG | \
                             STALL_ERR2_FLAG | \
                             STALL_ERR3_FLAG | \
                             STALL_ERR4_FLAG | \
                             STALL_PID1_FLAG | \
                             STALL_PID2_FLAG | \
                             STALL_PID3_FLAG | \
                             STALL_PID4_FLAG | \
                             STALL_PID5_FLAG | \
                             STALL_PID6_FLAG | \
                             STALL_PID7_FLAG | \
                             STALL_REC_FAIL_FLAG)

#define STALL_MSG_THLD     (20)

#define STALL_SYSREQ_CMD   (0x66006600) /**< Stall SYSREQ Recovery Command */
#define STALL_REBOOT_CMD   (0x00990099) /**< Stall REBOOT Recovery Command */

/* MD5_DIGEST_LENGTH is 16 and need space for *2 plus cr */
#define MD5_STRING_LENGTH ((MD5_DIGEST_LENGTH*2)+1)


#define MTC_CMD_NONE                   0
#define MTC_CMD_LOOPBACK               1  /*   to host */
#define MTC_CMD_REBOOT                 2  /*   to host */
#define MTC_CMD_WIPEDISK               3  /*   to host */
#define MTC_CMD_RESET                  4  /*   to host */
#define MTC_MSG_MTCALIVE               5  /* from host */
#define MTC_REQ_MTCALIVE               6  /*   to host */
#define MTC_MSG_MAIN_GOENABLED         7  /* from host */
#define MTC_MSG_SUBF_GOENABLED         8  /* from host */
#define MTC_REQ_MAIN_GOENABLED         9  /*   to host */
#define MTC_REQ_SUBF_GOENABLED        10  /*   to host */
#define MTC_MSG_MAIN_GOENABLED_FAILED 11  /* from host */
#define MTC_MSG_SUBF_GOENABLED_FAILED 12  /* from host */
#define MTC_MSG_LOCKED                13  /*   to host */
#define MTC_CMD_STOP_CONTROL_SVCS     14  /*   to host */
#define MTC_CMD_STOP_COMPUTE_SVCS     15  /*   to host */
#define MTC_CMD_STOP_STORAGE_SVCS     16  /*   to host */
#define MTC_CMD_START_CONTROL_SVCS    17  /*   to host */
#define MTC_CMD_START_COMPUTE_SVCS    18  /*   to host */
#define MTC_CMD_START_STORAGE_SVCS    19  /*   to host */
#define MTC_CMD_LAZY_REBOOT           20  /*   to host */
#define MTC_CMD_HOST_SVCS_RESULT      21  /*   to host */
#define MTC_CMD_LAST                  22

#define RESET_PROG_MAX_REBOOTS_B4_RESET (5)
#define RESET_PROG_MAX_REBOOTS_B4_RETRY (RESET_PROG_MAX_REBOOTS_B4_RESET+2)

const char * get_mtcNodeCommand_str ( int cmd );

typedef enum
{
    PROTOCOL__NONE  = 0,
    PROTOCOL__SMASH = 1,
    PROTOCOL__IPMI  = 2,
    PROTOCOL__MAX   = 3
} protocol_enum ;


/** Maintenance Commands used to specify HTTP REST API Command operations  */
typedef enum
{
    MTC_CMD_NOT_SET,
    MTC_CMD_DISABLE,
    MTC_CMD_ENABLE,
    MTC_CMD_VOTE,
    MTC_CMD_NOTIFY,

    /** HA Service Manager Commands  - Command Descriptions                 */
    CONTROLLER_LOCKED,   /**< specified controller is locked                */
    CONTROLLER_UNLOCKED, /**< specified controller is unlocked              */
    CONTROLLER_DISABLED, /**< specified controller is unlocked-disabled     */
    CONTROLLER_ENABLED,  /**< specified controller is unlocked-enabled      */
    CONTROLLER_SWACT,    /**< swact services away from specified controller */
    CONTROLLER_QUERY,    /**< query active services on specified controller */
} mtc_cmd_enum ;

typedef enum
{
    MTC_SWACT__START = 0,
    MTC_SWACT__QUERY,
    MTC_SWACT__QUERY_FAIL,
    MTC_SWACT__QUERY_RECV,
    MTC_SWACT__SWACT,
    MTC_SWACT__SWACT_FAIL,
    MTC_SWACT__SWACT_RECV,
    MTC_SWACT__SWACT_POLL,
    MTC_SWACT__DONE,
    MTC_SWACT__STAGES,
} mtc_swactStages_enum ;

/** Maintenance Administrative actions */
typedef enum
{
    MTC_ADMIN_ACTION__NONE        = 0,
    MTC_ADMIN_ACTION__LOCK        = 1,
    MTC_ADMIN_ACTION__UNLOCK      = 2,
    MTC_ADMIN_ACTION__RESET       = 3,
    MTC_ADMIN_ACTION__REBOOT      = 4,
    MTC_ADMIN_ACTION__REINSTALL   = 5,
    MTC_ADMIN_ACTION__POWEROFF    = 6,
    MTC_ADMIN_ACTION__POWERON     = 7,
    MTC_ADMIN_ACTION__RECOVER     = 8,
    MTC_ADMIN_ACTION__DELETE      = 9,
    MTC_ADMIN_ACTION__POWERCYCLE  =10,
    MTC_ADMIN_ACTION__ADD         =11,
    MTC_ADMIN_ACTION__SWACT       =12,
    MTC_ADMIN_ACTION__FORCE_LOCK  =13,
    MTC_ADMIN_ACTION__FORCE_SWACT =14,

    /* FSM Actions */
    MTC_ADMIN_ACTION__ENABLE      =15,
    MTC_ADMIN_ACTION__ENABLE_SUBF =16,
    MTC_ADMIN_ACTIONS             =17
} mtc_nodeAdminAction_enum ;

typedef enum
{
    MTC_CONFIG_ACTION__NONE                = 0,
    MTC_CONFIG_ACTION__INSTALL_PASSWD      = 1,
    MTC_CONFIG_ACTION__CHANGE_PASSWD       = 2,
    MTC_CONFIG_ACTION__CHANGE_PASSWD_AGAIN = 3,
    MTC_CONFIG_ACTIONS                     = 4,
} mtc_nodeConfigAction_enum ;


/** Maintenance Administrative states */
typedef enum
{
    MTC_ADMIN_STATE__LOCKED       = 0,
    MTC_ADMIN_STATE__UNLOCKED     = 1,
    MTC_ADMIN_STATES              = 2
} mtc_nodeAdminState_enum ;

/** Maintenance Operational states */
typedef enum
{
    MTC_OPER_STATE__DISABLED      = 0,
    MTC_OPER_STATE__ENABLED       = 1,
    MTC_OPER_STATES               = 2
} mtc_nodeOperState_enum ;

/** Maintenance Availablity status */
typedef enum
{
    MTC_AVAIL_STATUS__NOT_INSTALLED = 0,
    MTC_AVAIL_STATUS__AVAILABLE   = 1,
    MTC_AVAIL_STATUS__DEGRADED    = 2,
    MTC_AVAIL_STATUS__FAILED      = 3,
    MTC_AVAIL_STATUS__INTEST      = 4,
    MTC_AVAIL_STATUS__POWERED_OFF = 5,
    MTC_AVAIL_STATUS__OFFLINE     = 6,
    MTC_AVAIL_STATUS__ONLINE      = 7,
    MTC_AVAIL_STATUS__OFFDUTY     = 8,
    MTC_AVAIL_STATUS              = 9
} mtc_nodeAvailStatus_enum ;


void mtc_stages_init ( void );

typedef enum
{
    MTC_ENABLE__START                =  0,
    MTC_ENABLE__RESERVED_1           =  1,
    MTC_ENABLE__HEARTBEAT_CHECK      =  2,
    MTC_ENABLE__HEARTBEAT_STOP_CMD   =  3,
    MTC_ENABLE__RECOVERY_TIMER       =  4,
    MTC_ENABLE__RECOVERY_WAIT        =  5,
    MTC_ENABLE__RESET_PROGRESSION    =  6,
    MTC_ENABLE__RESET_WAIT           =  7,
    MTC_ENABLE__INTEST_START         =  8,
    MTC_ENABLE__MTCALIVE_PURGE       =  9,
    MTC_ENABLE__MTCALIVE_WAIT        = 10,
    MTC_ENABLE__CONFIG_COMPLETE_WAIT = 11,
    MTC_ENABLE__GOENABLED_TIMER      = 12,
    MTC_ENABLE__GOENABLED_WAIT       = 13,
    MTC_ENABLE__PMOND_READY_WAIT     = 14,
    MTC_ENABLE__HOST_SERVICES_START  = 15,
    MTC_ENABLE__HOST_SERVICES_WAIT   = 16,
    MTC_ENABLE__SERVICES_START_WAIT  = 17,
    MTC_ENABLE__HEARTBEAT_WAIT       = 18,
    MTC_ENABLE__HEARTBEAT_SOAK       = 19,
    MTC_ENABLE__STATE_CHANGE         = 20,
    MTC_ENABLE__WORKQUEUE_WAIT       = 21,
    MTC_ENABLE__WAIT                 = 22,
    MTC_ENABLE__ENABLED              = 23,
    MTC_ENABLE__SUBF_FAILED          = 24,
    MTC_ENABLE__DEGRADED             = 25,
    MTC_ENABLE__DONE                 = 26,
    MTC_ENABLE__FAILURE              = 27,
    MTC_ENABLE__FAILURE_WAIT         = 28,
    MTC_ENABLE__FAILURE_SWACT_WAIT   = 29,
    MTC_ENABLE__STAGES               = 30,
} mtc_enableStages_enum ;

/** Return the string representing the specified 'enable' stage */
string get_enableStages_str ( mtc_enableStages_enum stage );

typedef enum
{
    MTC_DISABLE__START               = 0,
    MTC_DISABLE__HANDLE_FORCE_LOCK   = 1,
    MTC_DISABLE__RESET_HOST_WAIT     = 2,
    MTC_DISABLE__DISABLE_SERVICES    = 3,
    MTC_DISABLE__DIS_SERVICES_WAIT   = 4,
    MTC_DISABLE__HANDLE_CEPH_LOCK    = 5,
    MTC_DISABLE__RESERVED            = 6,
    MTC_DISABLE__TASK_STATE_UPDATE   = 7,
    MTC_DISABLE__WORKQUEUE_WAIT      = 8,
    MTC_DISABLE__DISABLED            = 9,
    MTC_DISABLE__HANDLE_POWERON_SEND =10,
    MTC_DISABLE__HANDLE_POWERON_RECV =11,
    MTC_DISABLE__STAGES              =12,
} mtc_disableStages_enum ;

/** Return the string representing the specified 'disable' stage */
string get_disableStages_str ( mtc_disableStages_enum stage );

typedef enum
{
    MTC_ADD__START = 0,
    MTC_ADD__START_DELAY,
    MTC_ADD__START_SERVICES,
    MTC_ADD__START_SERVICES_WAIT,
    MTC_ADD__MTC_SERVICES,
    MTC_ADD__CLEAR_TASK,
    MTC_ADD__WORKQUEUE_WAIT,
    MTC_ADD__DONE,
    MTC_ADD__STAGES
} mtc_addStages_enum ;


/** Return the string representing the specified 'add' stage */
string get_addStages_str ( mtc_addStages_enum stage );


typedef enum
{
    MTC_DEL__START = 0,
    MTC_DEL__WAIT,
    MTC_DEL__DONE,
    MTC_DEL__STAGES
} mtc_delStages_enum ;

string get_delStages_str ( mtc_delStages_enum stage );


#define MTC_MAX_FAST_ENABLES (2)
typedef enum
{
    MTC_RECOVERY__START =  0,
    MTC_RECOVERY__REQ_MTCALIVE,
    MTC_RECOVERY__REQ_MTCALIVE_WAIT,
    MTC_RECOVERY__RESET_RECV_WAIT,
    MTC_RECOVERY__RESET_WAIT,
    MTC_RECOVERY__MTCALIVE_TIMER,
    MTC_RECOVERY__MTCALIVE_WAIT,
    MTC_RECOVERY__GOENABLED_TIMER,
    MTC_RECOVERY__GOENABLED_WAIT,
    MTC_RECOVERY__HOST_SERVICES_START,
    MTC_RECOVERY__HOST_SERVICES_WAIT,

    /* Subfunction stages */
    MTC_RECOVERY__CONFIG_COMPLETE_WAIT,
    MTC_RECOVERY__SUBF_GOENABLED_TIMER,
    MTC_RECOVERY__SUBF_GOENABLED_WAIT,
    MTC_RECOVERY__SUBF_SERVICES_START,
    MTC_RECOVERY__SUBF_SERVICES_WAIT,

    MTC_RECOVERY__HEARTBEAT_START,
    MTC_RECOVERY__HEARTBEAT_SOAK,
    MTC_RECOVERY__STATE_CHANGE,
    MTC_RECOVERY__ENABLE_START,
    MTC_RECOVERY__FAILURE,
    MTC_RECOVERY__WORKQUEUE_WAIT,
    MTC_RECOVERY__ENABLE_WAIT,
    MTC_RECOVERY__STAGES,
} mtc_recoveryStages_enum ;

/** Return the string representing the specified 'recovery' stage */
string get_recoveryStages_str ( mtc_recoveryStages_enum stage );

/* mtce support for sysinv driven configuration changes */
typedef enum {
    MTC_CONFIG__START,
    MTC_CONFIG__SHOW,
    MTC_CONFIG__MODIFY,
    MTC_CONFIG__VERIFY,
    MTC_CONFIG__FAILURE,
    MTC_CONFIG__TIMEOUT,
    MTC_CONFIG__DONE,
    MTC_CONFIG__STAGES
} mtc_configStages_enum ;

/** Return the string representing the specified 'add' stage */
string get_configStages_str ( mtc_configStages_enum stage );


/** Service Degrade Mask
  *
  * Hosts can become degraded for more than one reason.
  * The following are bit field definitions that represent
  * various degrade reasons ; heartbeat, process error,
  * inservice test, etc. */
#define DEGRADE_MASK_NONE             0x00000000
#define DEGRADE_MASK_HEARTBEAT_MGMNT  0x00000001
#define DEGRADE_MASK_HEARTBEAT_INFRA  0x00000002
#define DEGRADE_MASK_PMON             0x00000004
#define DEGRADE_MASK_INSV_TEST        0x00000008
#define DEGRADE_MASK_AVS_MAJOR        0x00000010
#define DEGRADE_MASK_AVS_CRITICAL     0x00000020
#define DEGRADE_MASK_RESMON           0x00000040
#define DEGRADE_MASK_HWMON            0x00000080
#define DEGRADE_MASK_SUBF             0x00000100
#define DEGRADE_MASK_SM               0x00000200
#define DEGRADE_MASK_CONFIG           0x00000400
#define DEGRADE_MASK_COLLECTD         0x00000800
#define DEGRADE_MASK_ENABLE           0x00001000
#define DEGRADE_MASK_RES4             0x00002000
#define DEGRADE_MASK_RES5             0x00004000
#define DEGRADE_MASK_RES6             0x00008000

/* future masks up to             0x80000000 */

/* FSM Stages for handling host 'reset' through
 * board management controller interface */
typedef enum
{
    MTC_RESET__START = 0,
    MTC_RESET__REQ_SEND,
    MTC_RESET__RESP_WAIT,
    MTC_RESET__QUEUE,
    MTC_RESET__OFFLINE_WAIT,
    MTC_RESET__DONE,
    MTC_RESET__FAIL,
    MTC_RESET__FAIL_WAIT,
    MTC_RESET__STAGES
} mtc_resetStages_enum ;


/* FSM Stages for handling host 'reset' through
 * board management controller interface */
typedef enum
{
    MTC_RESETPROG__START = 0,
    MTC_RESETPROG__REBOOT,
    MTC_RESETPROG__WAIT,
    MTC_RESETPROG__FAIL,
    MTC_RESETPROG__STAGES   
} mtc_resetProgStages_enum ;

/** Return the string representing the specified 'reset' stage */
string get_resetStages_str ( mtc_resetStages_enum stage );

/* FSM Stages for handling host 'reinstall' */
typedef enum
{
    MTC_REINSTALL__START = 0,
    MTC_REINSTALL__RESP_WAIT,
    MTC_REINSTALL__OFFLINE_WAIT,
    MTC_REINSTALL__ONLINE_WAIT,
    MTC_REINSTALL__FAIL,
    MTC_REINSTALL__MSG_DISPLAY,
    MTC_REINSTALL__DONE,
    MTC_REINSTALL__STAGES
} mtc_reinstallStages_enum ;

/** Return the string representing the specified 'reinstall' stage */
string get_reinstallStages_str ( mtc_reinstallStages_enum stage );

typedef enum
{
    MTC_POWERON__START = 0,
    MTC_POWERON__POWER_STATUS,
    MTC_POWERON__POWER_STATUS_WAIT,
    MTC_POWERON__REQ_SEND,
    MTC_POWERON__RETRY_WAIT,
    MTC_POWERON__RESP_WAIT,
    MTC_POWERON__DONE,
    MTC_POWERON__FAIL,
    MTC_POWERON__FAIL_WAIT,
    MTC_POWERON__QUEUE,

    MTC_POWEROFF__START,
    MTC_POWEROFF__REQ_SEND,
    MTC_POWEROFF__RESP_WAIT,
    MTC_POWEROFF__DONE,
    MTC_POWEROFF__FAIL,
    MTC_POWEROFF__FAIL_WAIT,
    MTC_POWEROFF__QUEUE,

    MTC_POWER__DONE, /* clear power action */
    MTC_POWER__STAGES       
}   mtc_powerStages_enum ;

/** Return the string representing the specified 'power' stage */
string get_powerStages_str ( mtc_powerStages_enum stage );

/* FSM Stages for handling host 'powercycle' through
 * board management controller interface */
typedef enum
{
    MTC_POWERCYCLE__START = 0,
    MTC_POWERCYCLE__POWEROFF,
    MTC_POWERCYCLE__POWEROFF_CMND_WAIT,
    MTC_POWERCYCLE__POWEROFF_WAIT,
    MTC_POWERCYCLE__POWERON,
    MTC_POWERCYCLE__POWERON_REQWAIT,
    MTC_POWERCYCLE__POWERON_VERIFY,
    MTC_POWERCYCLE__POWERON_VERIFY_WAIT,
    MTC_POWERCYCLE__POWERON_CMND_WAIT,
    MTC_POWERCYCLE__POWERON_WAIT,
    MTC_POWERCYCLE__DONE,
    MTC_POWERCYCLE__FAIL,
    MTC_POWERCYCLE__HOLDOFF,
    MTC_POWERCYCLE__COOLOFF,
    MTC_POWERCYCLE__STAGES,
}   mtc_powercycleStages_enum ;

/** Return the string representing the specified 'powercycle' stage */
string get_powercycleStages_str ( mtc_powercycleStages_enum stage );

typedef enum 
{
    MTC_SUBSTAGE__START  = 0,
    MTC_SUBSTAGE__SEND   = 1,
    MTC_SUBSTAGE__RECV   = 2,
    MTC_SUBSTAGE__WAIT   = 3,
    MTC_SUBSTAGE__DONE   = 4,
    MTC_SUBSTAGE__FAIL   = 5,
    MTC_SUBSTAGE__STAGES = 6
} mtc_subStages_enum ;

/** Return the string representing the specified 'sub' stage */
string get_subStages_str ( mtc_subStages_enum stage );

typedef enum
{
    MTC_OOS_TEST__LOAD_NEXT_TEST    = 0,
    MTC_OOS_TEST__BMC_ACCESS_TEST   = 1,
    MTC_OOS_TEST__BMC_ACCESS_RESULT = 2,
    MTC_OOS_TEST__START_WAIT        = 3,
    MTC_OOS_TEST__WAIT              = 4,
    MTC_OOS_TEST__DONE              = 5,
    MTC_OOS_TEST__STAGES            = 6,
}   mtc_oosTestStages_enum ;

/** Return the string representing the specified 'test' stage */
string get_oosTestStages_str ( mtc_oosTestStages_enum stage );

typedef enum
{
    MTC_INSV_TEST__START  = 0,
    MTC_INSV_TEST__WAIT   = 1,
    MTC_INSV_TEST__RUN    = 2,
    MTC_INSV_TEST__STAGES = 3,
} mtc_insvTestStages_enum ;

/** Return the string representing the specified 'test' stage */
string get_insvTestStages_str ( mtc_insvTestStages_enum stage );

#define MTC_NO_TEST   0
#define MTC_OOS_TEST  1
#define MTC_INSV_TEST 2

typedef enum
{
    MTC_SENSOR__START    = 0,
    MTC_SENSOR__READ_FAN = 1,
    MTC_SENSOR__READ_TEMP= 2,
    MTC_SENSOR__STAGES   = 3,
} mtc_sensorStages_enum ;

/** Return the string representing the specified 'sensor' stage */
string get_sensorStages_str ( mtc_sensorStages_enum stage );

typedef enum
{
    MTC_OFFLINE__IDLE = 0,
    MTC_OFFLINE__START,
    MTC_OFFLINE__SEND_MTCALIVE,
    MTC_OFFLINE__WAIT,
    MTC_OFFLINE__STAGES
} mtc_offlineStages_enum ;

typedef enum
{
    MTC_ONLINE__START = 0,
    MTC_ONLINE__WAITING,
    MTC_ONLINE__RETRYING,
    MTC_ONLINE__STAGES
} mtc_onlineStages_enum ;

#define MTC_ENABLE    0x12345678
#define MTC_DEGRADE   0x87654321
#define MTC_DISABLE   0xdeadbeef
#define MTC_RESET     0xdeadb00b 
#define MTC_WIPEDISK  0xdeadfeed


typedef enum
{
    MTC_STRESS_TEST__START  = 0,
    MTC_STRESS_TEST__DO     = 1,
    MTC_STRESS_TEST__WAIT   = 2,
    MTC_STRESS_TEST__VERIFY = 3,
    MTC_STRESS_TEST__NEXT   = 4,
    MTC_STRESS_TEST__DONE   = 5,
    MTC_STRESS_TEST__STAGES = 6,
} mtc_stressStages_enum ;

typedef struct
{
   mtc_nodeAdminAction_enum adminAction ;
   mtc_nodeAdminState_enum  adminState  ;
   mtc_nodeOperState_enum   operState   ;
   mtc_nodeAvailStatus_enum availStatus ;
}  fsm_states_type ;

/** Maintenance FSM test case codes */
typedef enum
{
    FSM_TC_ENABLED_NOACTION,
    FSM_TC_ENABLED_TO_DISABLED_FAILED,
    FSM_TC_ENABLED_TO_ENABLED_DEGRADED,
    FSM_TC_ENABLED_DEGRADED_TO_ENABLED_DEGRADED,
    FSM_TC_ENABLED_DEGRADED_TO_ENABLED,
    FSM_TC_LAST,
} mtcNodeFsm_tc_enum ;

/* The list of heartbeat interfaces / networks */
typedef enum
{
    MGMNT_IFACE  = 0,
    INFRA_IFACE  = 1,
      MAX_IFACES = 2
} iface_enum ;

/* Auto recovery Disable Causes */
typedef enum
{
    MTC_AR_DISABLE_CAUSE__CONFIG,
    MTC_AR_DISABLE_CAUSE__GOENABLE,
    MTC_AR_DISABLE_CAUSE__HOST_SERVICES,
    MTC_AR_DISABLE_CAUSE__HEARTBEAT,
    MTC_AR_DISABLE_CAUSE__LAST,
    MTC_AR_DISABLE_CAUSE__NONE,
} autorecovery_disable_cause_enum ;

/* Service Based Auto Recovery Control Structure */
typedef struct
{
    unsigned int count     ; /* running back-2-back failure count         */
    bool         disabled  ; /* true if autorecovery is disabled          */
} autorecovery_cause_ctrl_type ;

/** Returns true if the specified admin state string is valid */
bool adminStateOk  ( string admin );

/** Returns true if the specified oper state string is valid */
bool operStateOk   ( string oper  );

/** Returns true if the specified avail status string is valid */
bool availStatusOk ( string avail );

string get_availStatus_str ( mtc_nodeAvailStatus_enum availStatus );
string get_operState_str   ( mtc_nodeOperState_enum   operState   );
string get_adminState_str  ( mtc_nodeAdminState_enum  adminState  );

void log_adminAction ( string hostname,
                       mtc_nodeAdminAction_enum currAction,
                       mtc_nodeAdminAction_enum  newAction );

int send_hbs_command   ( string hostname, int command, string controller=CONTROLLER );
int send_hwmon_command ( string hostname, int command );
int send_guest_command ( string hostname, int command );

int daemon_log_message ( const char * hostname,
                         const char * filename,
                         const char * log_str );

bool is_host_services_cmd ( unsigned int cmd );

void zero_unused_msg_buf ( mtc_message_type & msg, int bytes);

/** Runtime Trace Log Utilities */
void daemon_dump_membuf ( void );
void daemon_dump_membuf_banner ( void );

void mem_log    ( char * log );
void mem_log    ( string log );
void mem_log    ( char   log );
void mem_log    ( string one, string two );
void mem_log    ( string one, string two, string three );
void mem_log    ( string label, int value, string data );

string get_hostname ( void );

#define MTC_FSM_ENABLE_TEST 0x12345678

#define MAX_MEM_LIST_SIZE   (2000)
#define MAX_MEM_LOG_LEN     (1000)
#define MAX_MEM_LOG_DATA    (MAX_MEM_LOG_LEN-100)

#define TESTHEAD_BAR "+--------------------------------------------------------------------+\n"
#define NODEBUG printf ( "\tDebug: Not implemented\n" )
#define FAILED  printf ( "Failed |\n" );
#define PASSED  printf ( "Passed |\n" );
#define PENDING printf ( "To-Do  |\n" );
#define FAILED_STR  printf ( "Failed |\n" );

#endif /* __INCLUDE_NODEBASE_H__ */
