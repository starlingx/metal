#ifndef __INCLUDE_RMON_HH__
#define __INCLUDE_RMON_HH__
/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
/*
 * This implements the CGCS Resource Monitor ; /usr/local/bin/rmond
 * The Resource monitor or rmon is a utility to provide: cpu, memory and 
 * filesystem usage and alarm stats both to the user and to registered client
 * processes on the host it is running on.  
 *
 * Call trace is as follows:
 *    daemon_init
 *        rmon_timer_init
 *        rmon_hdlr_init
 *        daemon_files_init
 *        daemon_signal_init
 *        daemon_configure
 *            ini_parse
 *            get_debug_options
 *            get_iface_macaddr
 *            get_iface_address
 *            get_iface_hostname
 *        socket_init
 *            rmon_msg_init
 *            setup_tx_port
 *        
 *    daemon_service_run
 *       wait for goenable signal
 *       rmon_send_event ( READY )
 *       rmon_service
 *          _config_dir_load
 *          _config_files_load
 *          _forever
 *             service_events: 
 *                 _get_events every audit period seconds
 *                  resource_handler handles the resource values and sends 
 *                  alarm messages through fm api to set or clear resource 
 *                  thresholds as well as notifying registered clients through 
 *                  the rmon client api. 
 *                  
 * 
 *            
 *          
 * This daemon waits for a "goenabled" signal an then reads all the resource
 * configuration files in: /etc/rmon.d and begins monitoring them accordingly.
 * A resource confguration file is expected to contain the following information:
 *
 * [resource]
 * resource  = <string>        ; name of resource being monitored 
 * debounce = <int>            ; number of seconds to wait before degrade clear
 * severity = <string>         ; minor, major, critical
 * minor_threshold = <int>     ; minor resource utilization threshold 
 * major_threshold = <int>     ; major resource utilization threshold 
 * critical_threshold = <int>  ; critical resource utilization threshold
 * num_tries = <int>           ; number of tries before the alarm is raised or cleared 
 * alarm_on = <int>            ; dictates whether maintainance gets alarms from rmon 
 *                               1 for on, 0 for off
 *
 * Here is how it works ...
 *
 * Every audit period seconds the resources defined in the config files get
 * monitored.  If the resource ie. CPU usage crosses a threshold:
 * (minor, major or critical) count times an alarm is raised and message is sent to 
 * all clients registered for the resource.  If the resource usage drops below 
 * that threshold count times, the alarms are cleared and a message is sent to 
 * all registered clients in order to clear the alarm.  The audit period as well as 
 * other rmon config options are specifiedin the: /etc/mtc/rmond.conf file with
 * the following (example) information:
 *
 *  ; CGTS Resource Monitor Configuration File
 * [config]                   ; Configuration
 * audit_period = 10          ; Resource polling period in seconds (1 - 120)
 *                                                
 * rmon_tx_port = 2101        ; Transmit Event and Command Reply Port
 * per_node  = 0              ; enable (1) or disable (0) memory checking per processor node 
 * rmon_api_port = 2300       : Resource Monitor API Receive Port
 *
 * [defaults]
 *
 * [timeouts]
 * start_delay = 10           ; managed range 1 .. 120 seconds
 * 
 * [features]
 *
 * [debug]                    ; SIGHUP to reload
 * debug_timer = 0            ; enable(1) or disable(0) timer logs (tlog)
 * debug_msg = 0              ; enable(1) or disable(0) message logs (mlog)
 * debug_state = 0            ; enable(1) or disable(0) state change logs (clog)
 * debug_level = 0            ; decimal mask 0..15 (8,4,2,1)
 *
 * flush = 0                  ; enable(1) or disable(0) force log flush (main loop)  
 * flush_thld = 2             ; if enabled - force flush after this number of loops
 *
 * debug_event = none         ; Not used
 * debug_filter = none        ; Not used
 * stress_test = 0            ; In-Service Stress test number
 *
 *  To check the alarms that are raised the command:
 *  system alarm-list can be used.  Rmon alarms have the following codes:
 *
 *  100.101: CPU usage threshold crossed
 *  100.102: vSwitch CPU usage threshold crossed
 *  100.103: Memory usage threshold crossed  
 *  100.104: Filesystem usage threshold crossed 
 *
 *  To register your process for rmon notifications using the rmon client api 
 *  please see the files: rmon_api.h for usage of the api as well as:
 *  rmon_api_client_test.cpp and rmon_api_client_test.h for an example 
 *  implementation for your process.  
 *
 */
 /**
  * @file
  * Wind River CGCS Platform Resource Monitor Service Header
  */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>        /* for hostent */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>        /* for close and usleep */
#include <sys/stat.h>
#include <math.h>          /* for round */
#include "nodeBase.h"
#include "daemon_ini.h"    /* Ini Parser Header                        */
#include "daemon_common.h" /* Common definitions and types for daemons */
#include "daemon_option.h" /* Common options  for daemons              */
#include "nodeTimers.h"    /* maintenance timer utilities start/stop   */
#include "nodeUtil.h"      /* common utilities */
#include "tokenUtil.h"     /* for ... keystone_config_handler          */
#include "fmAPI.h"
#include "httpUtil.h"      /* for ... libEvent */
extern "C"
{
    #include "rmon_api.h"  /* for ... resource monitoring utilities    */
}
/**
 * @addtogroup RMON_base
 * @{
 */
using namespace std;

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "mon"

/* openstack Identity version */
#define OS_IDENTITY_VERSION_PREFIX ((const char *)"/v3")

/* Config and resource files used by rmon */
#define CONFIG_DIR            ((const char *)"/etc/rmon.d")
#define INT_CONFIG_DIR        ((const char *)"/etc/rmon_interfaces.d")
#define COMPUTE_VSWITCH_DIR   ((const char *)"/etc/nova/compute_extend.conf")
#define COMPUTE_RESERVED_CONF ((const char *)"/etc/platform/worker_reserved.conf")
#define DYNAMIC_FS_FILE       ((const char *)"/etc/rmonfiles.d/dynamic.conf")
#define STATIC_FS_FILE        ((const char *)"/etc/rmonfiles.d/static.conf")

// this path is different in Wind River Linux vs. CentOS.
// For the latter, we shall look specifically within
// the bonding interface device directory
#define INTERFACES_DIR ((const char *)"/sys/class/net/")
#define PLATFORM_DIR          ((const char *)"/etc/platform/platform.conf")

#define MOUNTS_DIR            ((const char *)"/proc/mounts")
#define COMPUTE_CONFIG_PASS   ((const char *)"/var/run/.config_pass")
#define COMPUTE_CONFIG_FAIL   ((const char *)"/var/run/.config_fail")
#define RMON_FILES_DIR        ((const char *)"/etc/rmonfiles.d")
#define NTPQ_QUERY_SCRIPT     ((const char *)"query_ntp_servers.sh")

/* Constant search keys used to update rmon resource usage */
#define CPU_RESOURCE_NAME         ((const char *)"Platform CPU Usage")
#define V_CPU_RESOURCE_NAME       ((const char *)"vSwitch CPU Usage")
#define MEMORY_RESOURCE_NAME      ((const char *)"Platform Memory Usage")
#define FS_RESOURCE_NAME          ((const char *)"Platform Filesystem Usage")
#define INSTANCE_RESOURCE_NAME    ((const char *)"Platform Nova Instances")
#define V_MEMORY_RESOURCE_NAME    ((const char *)"vSwitch Memory Usage")
#define V_PORT_RESOURCE_NAME      ((const char *)"vSwitch Port Usage")
#define V_INTERFACE_RESOURCE_NAME ((const char *)"vSwitch Interface Usage")
#define V_LACP_INTERFACE_RESOURCE_NAME  ((const char *)"vSwitch LACP Interface Usage")
#define V_OVSDB_RESOURCE_NAME     ((const char *)"vSwitch OVSDB Usage")
#define V_NETWORK_RESOURCE_NAME   ((const char *)"vSwitch Network Usage")
#define V_OPENFLOW_RESOURCE_NAME  ((const char *)"vSwitch Openflow Usage")
#define V_CINDER_THINPOOL_RESOURCE_NAME  ((const char *)"Cinder LVM Thinpool Usage")
#define V_NOVA_THINPOOL_RESOURCE_NAME  ((const char *)"Nova LVM Thinpool Usage")
#define REMOTE_LOGGING_RESOURCE_NAME  ((const char *)"Remote Logging Connectivity")

/* dynamic resources used for thin provisioning monitoring */
#define CINDER_VOLUMES            ((const char *)"cinder-volumes")
#define NOVA_LOCAL                ((const char *)"nova-local")

#define RMON_RESOURCE_NOT         ((const char *)"read_dynamic_file_system")
#define RESPONSE_RMON_RESOURCE_NOT ((const char *)"/var/run/.dynamicfs_registered")

#define POSTGRESQL_FS_PATH         ((const char *)"/var/lib/postgresql")

#define RESOURCE_DISABLE          (0)

/* Thin provisioning metadata monitoring */
#define THINMETA_FSM_RETRY                  3
#define THINMETA_CONFIG_SECTION             "thinpool_metadata"
#define THINMETA_DEFAULT_CRITICAL_THRESHOLD 0  // feature is disabled by default
#define THINMETA_DEFAULT_ALARM_ON           1  // alarm is enabled
#define THINMETA_DEFAULT_AUTOEXTEND_ON      1  // autoextend is enabled (only if monitoring is enabled!)
#define THINMETA_DEFAULT_AUTOEXTEND_BY      20 // autoextend by 20%, same as example in /etc/lvm/lvm.conf
#define THINMETA_DEFAULT_AUTOEXTEND_PERCENT 1  // autoextend by a percentage
#define THINMETA_DEFAULT_AUDIT_PERIOD       10 // seconds to perform audit, same as LVM (broken) audit of lvmetad
#define THINMETA_RESULT_BUFFER_SIZE         (1024) // result for lvm commands may be bigger than default BUFFER_SIZE
#define THINMETA_INVALID_NAME               ((const char *) "invalid name!")

/* Constant search keys used to update rmon interface usage */ 
#define MGMT_INTERFACE_NAME  ((const char *)"mgmt")
#define INFRA_INTERFACE_NAME ((const char *)"infra")
#define OAM_INTERFACE_NAME   ((const char *)"oam")
#define MGMT_INTERFACE_FULLNAME  ((const char *)"management_interface")
#define OAM_INTERFACE_FULLNAME  ((const char *)"oam_interface")
#define INFRA_INTERFACE_FULLNAME ((const char *)"infrastructure_interface")

 /* Daemon Config Constants */
#define CONFIG_AUDIT_PERIOD 1
#define PM_AUDIT_PERIOD     15
#define NTP_AUDIT_PERIOD    600  //10 minutes
#define NTPQ_CMD_TIMEOUT    60   //1 minute
#define CONFIG_TX_PORT      2
#define CONFIG_RX_PORT      4
#define CONFIG_CRITICAL_THR 5
#define CONFIG_NODE        12
#define CONFIG_START_DELAY 20

/* rmon resource default percent thresholds */ 
#define DEFAULT_MINOR              (80)
#define DEFAULT_MAJOR              (90)
#define DEFAULT_CRITICAL           (95)
#define UNUSED_CRITICAL            (101)

/* processor node0 default memory thresholds */
#define DEFAULT_MINOR_ABS_NODE0    (512)
#define DEFAULT_MAJOR_ABS_NODE0    (307)
#define DEFAULT_CRITICAL_ABS_NODE0 (102)
#define UNUSED_CRITICAL_ABS_NODE0  (0)

/* processor node1 default memory thresholds */
#define DEFAULT_MINOR_ABS_NODE1    (0)
#define DEFAULT_MAJOR_ABS_NODE1    (0)
#define DEFAULT_CRITICAL_ABS_NODE1 (0)

/* absolute threshold array index */
#define RMON_MINOR_IDX      (0)
#define RMON_MAJOR_IDX      (1)
#define RMON_CRITICAL_IDX   (2)

/* Defualt startup settings */
#define DEFAULT_NUM_TRIES     (2) /* Number of tries before an alarm is set or cleared */
#define DEFAULT_ALARM_STATUS  (1) /* Alarms are on by default */ 
#define DEFAULT_PERCENT       (1) /* Percentage thresholds are used by default */
#define PERCENT_USED          (1) /* Percent is used for the resource */
#define PERCENT_UNUSED        (0) /* Absolute values are used for the resource */
#define DYNAMIC_ALARM         (1) /* Filesystem alarm is a dynamic alarm, persisting among nodes */
#define STATIC_ALARM          (2) /* Filesystem alarm is a local, static resource */
#define STANDARD_ALARM        (3) /* Alarm is not a filesystem alarm */

#define HUGEPAGES_NODE     0  /* 0 or 1 for per hugepages node memory stats */ 
#define PROCESSOR_NODE     0  /* 0 or 1 for per processor node memory stats */ 
#define ALARM_OFF          0  /* Do not notify maintainance if alarm off */ 
#define ALARM_ON           1  /* Notify maintainance if alarm on */
#define PASS              (0)
#define FAIL              (1)

/* Monitored Resource severity levels */
#define SEVERITY_MINOR    0
#define SEVERITY_MAJOR    1
#define SEVERITY_CRITICAL 2
#define SEVERITY_CLEARED  3
#define MINORLOG_THRESHOLD (20)
#define PROCLOSS_THRESHOLD (5)
#define MAX_RESOURCES  (100)
#define MAX_FILESYSTEMS (100)
#define MAX_BASE_CPU (100)

#define DEGRADE_CLEAR_MSG ((const char *)("cleared_degrade_for_resource"))

/* File System Custum Thresholds */
#define TMPFS_MINOR (8)
#define TMPFS_MAJOR (6)
#define TMPFS_CRITICAL (4)
#define BOOTFS_MINOR (200)
#define BOOTFS_MAJOR (100)
#define BOOTFS_CRITICAL (50)

#define MAX_FAIL_SEND (10)
#define MAX_SWACT_COUNT (10)

/* Percent thresholds Database monitoring */
#define FS_MINOR    (70)
#define FS_MAJOR    (80)
#define FS_CRITICAL (90)

/* Resource Alarm ids */
#define CPU_ALARM_ID             ((const char *)"100.101")
#define V_CPU_ALARM_ID           ((const char *)"100.102")
#define MEMORY_ALARM_ID          ((const char *)"100.103")
#define FS_ALARM_ID              ((const char *)"100.104")
#define INSTANCE_ALARM_ID        ((const char *)"100.105")
#define OAM_PORT_ALARM_ID        ((const char *)"100.106")
#define OAM_ALARM_ID             ((const char *)"100.107")
#define MGMT_PORT_ALARM_ID       ((const char *)"100.108")
#define MGMT_ALARM_ID            ((const char *)"100.109")
#define INFRA_PORT_ALARM_ID      ((const char *)"100.110")
#define INFRA_ALARM_ID           ((const char *)"100.111")
#define VRS_PORT_ALARM_ID        ((const char *)"100.112")  //used for HP branch only
#define VRS_ALARM_ID             ((const char *)"100.113")  //used for HP branch only
#define NTP_ALARM_ID             ((const char *)"100.114")
#define V_MEMORY_ALARM_ID        ((const char *)"100.115")
#define V_CINDER_THINPOOL_ALARM_ID      ((const char *)"100.116")
#define V_NOVA_THINPOOL_ALARM_ID   ((const char *)"100.117")
#define THINMETA_ALARM_ID        ((const char *)"800.103")

// ripped from fm-api constants for Neutron AVS alarms
// being moved over to RMON 
#define V_PORT_ALARM_ID       ((const char *)"300.001")
#define V_INTERFACE_ALARM_ID  ((const char *)"300.002")

// remote logging alarm ID
#define REMOTE_LOGGING_CONTROLLER_CONNECTIVITY_ALARM_ID       ((const char *)"100.118")

// SDN specific alarms
#define V_OPENFLOW_CONTROLLER_ALARM_ID ((const char *)"300.012")
#define V_OPENFLOW_NETWORK_ALARM_ID   ((const char *)"300.013")
#define V_OVSDB_MANAGER_ALARM_ID      ((const char *)"300.014")
#define V_OVSDB_ALARM_ID      ((const char *)"300.015")

#define INTERFACE_NAME_LEN (10)
#define INTERFACE_UP (1)
#define INTERFACE_DOWN (0)
#define MAX_CLIENTS (100)
#define RMON_MAX_LEN (100)
#define MOUNTED     (1)
#define NOT_MOUNTED (0)
#define MTC_EVENT_RMON_READY (0x0f0f0f0f)

#define NTP_ERROR (255)

/** Daemon Config Mask */
#define CONF_MASK   (CONFIG_AUDIT_PERIOD  |\
                     PM_AUDIT_PERIOD      |\
                     NTP_AUDIT_PERIOD     |\
                     NTPQ_CMD_TIMEOUT     |\
                     CONFIG_NODE          |\
                     CONFIG_START_DELAY   |\
                     CONFIG_TX_PORT       |\
                     CONFIG_RX_PORT       |\
                     CONFIG_CRITICAL_THR) 

#define CONF_RMON_API_MASK   (CONF_PORT      | \
                              CONF_PERIOD    | \
                              CONF_TIMEOUT   | \
                              CONF_THRESHOLD)



#define RMON_MIN_START_DELAY (1)
#define RMON_MAX_START_DELAY (120)

#define RMON_MIN_AUDIT_PERIOD (10)   /* Minimum audit period for resource if none specified */
#define RMON_MAX_AUDIT_PERIOD (120)  /* Maximum audit period for resource if none specified */

#define RMON_MIN_PM_PERIOD (60)   /* Minimum pm period for resource if none specified */
#define RMON_MAX_PM_PERIOD (600)  /* Maximum pm period for resource if none specified */

#define RMON_MIN_NTP_AUDIT_PERIOD (10)    /* Minimum audit period for resource if none specified */
#define RMON_MAX_NTP_AUDIT_PERIOD (1200)  /* Maximum audit period for resource if none specified */

/* Monitored Resource Config Bit Mask */
#define CONF_RESOURCE   (0x01)
#define CONF_STYLE      (0x04)
#define CONF_SEVERITY   (0x20)
#define CONF_INTERVAL   (0x40)
#define CONF_DEBOUNCE   (0x80)

/* Usual buffer sizes */
#define RATE_THROTTLE   (6)
#define BUFFER_SIZE     (128)
/* Monitored Resource stages for resource handler fsm */
typedef enum
{
    RMON_STAGE__INIT,
    RMON_STAGE__START,
    RMON_STAGE__MANAGE,
    RMON_STAGE__MONITOR_WAIT,
    RMON_STAGE__MONITOR,
    RMON_STAGE__RESTART_WAIT,
    RMON_STAGE__IGNORE,
    RMON_STAGE__FINISH,
	RMON_STAGE__FAILED,
    RMON_STAGE__FAILED_CLR,
    RMON_STAGE__STAGES,
} rmonStage_enum ;

typedef enum
{
    NTP_STAGE__BEGIN,
    NTP_STAGE__EXECUTE_NTPQ,
    NTP_STAGE__EXECUTE_NTPQ_WAIT,
    NTP_STAGE__STAGES,
}   ntpStage_enum ;

 /* The return values from the ntpq querie */
typedef enum
{
    NTP_OK                                 = 0, /* All NTP servers are reachable and one is selected */
    NTP_NOT_PROVISIONED                    = 1, /* No NTP servers are provisioned */
    NTP_NONE_REACHABLE                     = 2, /* None of the NTP servers are reachable */
    NTP_SOME_REACHABLE                     = 3, /* Some NTP servers are reachable and one selected */
    NTP_SOME_REACHABLE_NONE_SELECTED       = 4  /* Some NTP servers are reachable but none is selected, will treat at as none reachable */
} NTPQueryStatus;

typedef enum
{
    RESOURCE_TYPE__UNKNOWN,
    RESOURCE_TYPE__FILESYSTEM_USAGE,
    RESOURCE_TYPE__MEMORY_USAGE,
    RESOURCE_TYPE__CPU_USAGE,
    RESOURCE_TYPE__DATABASE_USAGE,
    RESOURCE_TYPE__NETWORK_USAGE,
    RESOURCE_TYPE__PORT,
    RESOURCE_TYPE__INTERFACE,
    RESOURCE_TYPE__CONNECTIVITY,
} resType_enum ;

/* Structure to store memory stats (KiB) */
typedef struct 
{
   unsigned long int MemTotal;
   unsigned long int MemFree;
   unsigned long int Buffers;
   unsigned long int Cached;
   unsigned long int SlabReclaimable;
   unsigned long int CommitLimit;
   unsigned long int Committed_AS;
   unsigned long int HugePages_Total;
   unsigned long int HugePages_Free;
   unsigned long int FilePages;
   unsigned long int Hugepagesize;
   unsigned long int AnonPages;
} memoryinfo;

#define RMON_API_MAX_LEN (100)
typedef struct
{
    int                    tx_sock ;     /**< socket to monitored process */
    int                    tx_port ;     /**< port to monitored process   */
    struct sockaddr_in     tx_addr ;     /**< process socket attributes   */
    char       tx_buf[RMON_API_MAX_LEN]; /**< Server receive buffer      */
    socklen_t                  len ;     /**< Socket Length              */
} rmon_api_socket_type ;

typedef struct
{
 
    /* Config Items */
	unsigned int mask      ;
    resType_enum res_type  ;              /* specifies the generic resource type */
    const char * resource  ;              /* The name of the Resource being monitored   */ 
    const char * severity  ;              /* MINOR, MAJOR or CRITICAL for each resource */ 
    unsigned int debounce  ;              /* Period to wait before clearing alarms  */
    unsigned int minor_threshold;         /* % Value for minor threshold crossing  */
    unsigned int major_threshold;         /* % Value for major threshold crossing  */
    unsigned int critical_threshold;      /* % Value for critical threshold crossing  */
    unsigned int minor_threshold_abs_node0;     /* Absolute value for minor threshold crossing processor node 0 */
    unsigned int major_threshold_abs_node0;     /* Absolute value for major threshold crossing processor node 0  */
    unsigned int critical_threshold_abs_node0;  /* Absolute value for critical threshold crossing processor node 0 */
	unsigned int minor_threshold_abs_node1;     /* Absolute value for minor threshold crossing processor node 1  */
    unsigned int major_threshold_abs_node1;     /* Absolute value for major threshold crossing processor node 1  */
    unsigned int critical_threshold_abs_node1;  /* Absolute value for critical threshold crossing processor node 1  */
    unsigned int num_tries  ;             /* Number of times a resource has to be in 
                                             failed or cleared state before sending alarm */
    unsigned int alarm_status ;           /* 1 or 0. If it is 0 threshold crossing alarms are not sent */
    unsigned int percent ;                /* 1 or 0.  If it is 1, the percentage is used, otherwise if 0, 
										     the absolute value is used for thresholds crossing values */
    unsigned int alarm_type;              /* standard, dynamic or static */

    /* Dynamic Data */
    const char *     type           ;
    const char *     device         ;
    int              i              ; /* timer array index */
    unsigned int     debounce_cnt   ; /* running monitor debounce count         */
    unsigned int     minorlog_cnt   ; /* track minor log count for thresholding */
    unsigned int     count          ; /* track the number of times the condition has been occured */ 
    bool             failed         ; /* track if the resource needs to be serviced by the resource handler */ 
    double           resource_value ; /* Usage for the Linux blades: controller, worker and storage  */
    double           resource_prev  ; /*       the previous resource_value */
    int              sev            ; /* The severity of the failed resource */ 
    rmonStage_enum   stage          ; /* The stage the resource is in within the resource handler fsm */ 
 	char alarm_id[FM_MAX_BUFFER_LENGTH] ; /* Used by FM API, type of alarm being raised */
	char errorMsg[ERR_SIZE];
	rmon_api_socket_type msg;   
    bool alarm_raised              ;
    int failed_send                ; /* The number of times the rmon api failed to send a message */ 
    int mounted                    ;  /* 1 or 0 depending on if the dynamic fs resource is mounted */ 
    int socket_id                  ; /* socket id corresponding to a physical processor */
    int response_error_log_throttle; /* log throttle counter for error in receiving response for resource info */
    int parse_error_log_throttle   ; /* log throttle counter for failing to parse resource info */
    int key_error_log_throttle     ; /* log throttle counter for failing to obtain resource info */
    int resource_monitor_throttle  ; /* log throttle for the this resource being monitored */
} resource_config_type ;

typedef struct
{
 
    /* Config Items */
	unsigned int mask      ;
    const char * resource  ;              /* The name of the Resource being monitored   */ 
    const char * severity  ;              /* MINOR, MAJOR or CRITICAL for each resource */ 
    unsigned int debounce  ;              /* Period to wait before clearing alarms  */
    unsigned int num_tries ;              /* Number of times a resource has to be in 
                                             failed or cleared state before sending alarm */
    unsigned int alarm_status ;           /* 1 or 0. If it is 0 threshold crossing alarms are not sent */

    /* Dynamic Data */
    int              i              ; /* timer array index */
    char          interface_one[20] ; /* primary interface */ 
    char          interface_two[20] ; /* second interface if lagged  */
    char          bond[20]          ; /* bonded interface name */ 
    bool             lagged         ; /* Lagged interface=true or not=false     */
    unsigned int     debounce_cnt   ; /* running monitor debounce count         */
    unsigned int     minorlog_cnt   ; /* track minor log count for thresholding */
    unsigned int     count          ; /* track the number of times the condition has been occured */ 
    bool             failed         ; /* track if the resource needs to be serviced by the resource handler */ 
    int              resource_value ; /* 1 if the interface is up and 0 if it is down   */ 
    int     resource_value_lagged   ; /* 1 if the interface is up and 0 if it is down for lagged interfaces  */ 
    int              sev            ; /* The severity of the failed resource */ 
    rmonStage_enum   stage          ; /* The stage the resource is in within the resource handler fsm */ 
    char int_name[INTERFACE_NAME_LEN]   ; /* Name of the tracked interface ex: eth1 */
 	char alarm_id[FM_MAX_BUFFER_LENGTH] ; /* Used by FM API, type of alarm being raised */
    char alarm_id_port[FM_MAX_BUFFER_LENGTH] ; /* Used by FM API, type of alarm being raised for the ports */
	char errorMsg[ERR_SIZE];
	rmon_api_socket_type msg;     
    bool link_up_and_running; /* whether the interface is up or down initially */ 
    bool interface_used;  /* true if the interface is configured */
    bool alarm_raised;     
    int failed_send; /* The number of times the rmon api failed to send a message */ 


} interface_resource_config_type ;

typedef struct
{

    /* Config Items */
    const char * vg_name            ; /* LVM Volume Group name */
    const char * thinpool_name      ; /* LVM Thin Pool in VG to monitor */
    unsigned int critical_threshold ; /* critical alarm threshold percentage for metadata utilization,
                                         0 to disable monitoring*/
    unsigned int alarm_on           ; /* 1 or 0. 1 to enable critical alarm, 0 to disable it */
    unsigned int autoextend_on      ; /* 1 or 0. 1 to first try extending the metadata before
                                         raising alarm, 0 for autoextend off */
    unsigned int autoextend_by       ; /* autoextend by percentage or absolute value in MiB */
    unsigned int autoextend_percent ; /* use percent or MiB in autoexent_by */
    unsigned int audit_period       ; /* frequency at which resources are polled, in seconds */

    /* Dynamic Data */
    bool section_exists             ; /* will be 1 if [THINMDA_CONFIG_SECTION] section is defined in
                                         configuration file */
    double resource_value           ; /* metadata usage percent */
    double resource_prev            ; /*    the previous value  */
    bool alarm_raised               ; /* track if alarm is raised to avoid re-raising */
    bool first_run                  ; /* to check for state consistency on first run */
    rmonStage_enum stage            ; /* The stage the resource is in within the resource handler fsm */

} thinmeta_resource_config_type;

/** Daemon Service messaging socket control structure **/
typedef struct
{
    int                   rmon_tx_sock; /**< RMON API Tx Socket  */
    int                   rmon_tx_port; /**< RMON API Tx Port    */
    struct sockaddr_in    rmon_tx_addr; /**< RMON API Tx Address */
    int                   rmon_rx_sock; /**< RMON API Rx Socket  */
    int                   rmon_rx_port; /**< RMON API Rx Port    */
    struct sockaddr_in    rmon_rx_addr; /**< RMON API Rx Address */
    int                   netlink_sock; /**< Netlink event socket */             
    int                   ioctl_sock;
    msgSock_type          mtclogd ;
} rmon_socket_type ;
rmon_socket_type * rmon_getSock_ptr ( void );

typedef struct 
{    char resource[50];
	char registered_not[NOT_SIZE]     ; /* The api notification the client has registerted for */
    char client_name[NOT_SIZE]        ; /* The api notification the client has registerted for */

    /** RMON API socket                    */
    /*  ------------------------------------                                  */
    rmon_api_socket_type msg ; /**< Resource monitoring messaging interface   */

	/*  RMON API Dynamic Data                                                 */
    /* ------------------------------                                         */  
    bool         resource_failed    ; /**< resource monitoring failed signal   */
    unsigned int tx_sequence        ; /**< outgoing sequence number            */
    unsigned int rx_sequence        ; /**< incoming sequence number            */
    bool         waiting            ; /**< waiting for response                */
    int          port               ;
    unsigned int msg_count          ;/**< running pulse count                  */
    unsigned int b2b_miss_peak      ; /**< max number of back to back misses   */
    unsigned int b2b_miss_count     ; /**< current back to back miss count     */
    unsigned int afailed_count      ; /**< total resouce mon'ing failed count  */
    unsigned int recv_err_cnt       ; /**< counts the receive errors           */
    unsigned int send_err_cnt       ; /**< counts the transmit errors          */
	unsigned int send_msg_count     ; /**< number of messages sent             */
    unsigned int mesg_err_cnt       ; /**< response message error count        */
    unsigned int mesg_err_peak      ; /**< response message error count        */
    unsigned int adebounce_cnt      ; /**< resource monitor debounce counter   */
    bool         resource_debounce  ; /**< true = in resource mon'ing debounce */
    rmon_socket_type rx_sock        ; /* rx socket for that client             */
   
} registered_clients;  

void rmon_msg_init ( void );
void rmon_msg_fini ( void );
int  setup_tx_port  ( const char * iface , const char * mcast , int port );
int  rmon_send_event ( unsigned int event_cmd , const char * process_name_ptr );


/* Note: Any addition to this struct requires explicit 
 *       init in daemon_init. 
 *       Cannot memset a struct contianing a string type. 
 **/
typedef struct
{
    /* iface attributes ; hostname, ip, audit period and mac address */
    char   my_hostname [MAX_HOST_NAME_SIZE+1];
    string my_macaddr   ;
    int audit_period    ; /* Frequency at which resources are polled */ 
    int pm_period       ; /* Frequency at which ceilometer PM's are created */
    int ntp_audit_period; /* Frequency at which we check if the NTP servers are still reachable */
    int ntpq_cmd_timeout; /* Max amount of time in seconds to wait for the ntpq command to complete */
    string my_address   ;
    int    resources    ; /**< Number of Monitored resources    */
    int interface_resources  ; /**< Number of monitored interface resources    */
    int thinmeta_resources; /**< Number of monitored thinpool metadata resources    */
	int    per_node     ; /* Memory checking per node enabled: 1 or disabled: 0 */ 
    int    clients      ;
    int rmon_critical_thr   ;
    int fd; /* Used for inotify */ 
    int wd; /* Used for inotify */ 

    unsigned int function    ;
    unsigned int subfunction ;
    unsigned int nodetype    ;

} rmon_ctrl_type ;

bool is_controller ( void );

/* Init tx message */
void rmon_msg_init ( void );

/* Delete tx message */
void rmon_msg_fini ( void );

/* Initizialize the settings from the rmond.conf file */
int  rmon_hdlr_init ( rmon_ctrl_type * ctrl_ptr );

/* Initialize the timers */ 
void rmon_timer_init( void );

/* Service client register and deregister requests
 * when rmon was not alive */
void rmon_alive_notification (int & clients); 

/* Service inbox when rmon is born */
int rmon_service_file_inbox ( int clients, char buf[RMON_MAX_LEN], bool add=true );

/* rmon_api functions */
int  rmon_service_inbox ( int clients );

/* Send set or clear alarm notification to registered clients */
int rmon_send_request ( resource_config_type * ptr, int clients);

/* send rmon interface resource set and clear alarm messages to registered client processes */
int send_interface_msg ( interface_resource_config_type * ptr, int clients);

/* Init rmon api tx and rx  ports */
int  rmon_port_init ( int tx_port );

/* Main loop to poll and handle resource monitoring */ 
void rmon_service (rmon_ctrl_type * ctrl_ptr);

/* Update the number of registered clients */ 
void update_total_clients (int total_clients);

/* Add a registered client to the list of clients */ 
void add_registered_client (registered_clients client);

/* Read in the per resource specific thresholds */
int rmon_resource_config ( void * user, 
                    const char * section,
                    const char * name,
                    const char * value);

/* Read in the per interface resource specific values */
int rmon_interface_config ( void * user, 
                    const char * section,
                    const char * name,
                    const char * value);

/* Read in LVM Thinpool metadata resource specific values */
int rmon_thinmeta_config ( void * user,
                    const char * section,
                    const char * name,
                    const char * value);

/* Returns a registered client at a given index */
registered_clients * get_registered_clients_ptr ( int index );

/* read the dynamic file systems file and send a response back */
void process_dynamic_fs_file();

/* send the notification that the file has been read */ 
int rmon_resource_response ( int clients );

/* Updates the interface data structure with the state (up or down) of the interface */
void check_interface_status( interface_resource_config_type * ptr );

/* Check if the node is a worker node */
bool check_worker();

/* Handle failed platform interfaces */ 
void interface_handler( interface_resource_config_type * ptr );

/* Handle LVM thinpool metadata usage */
int thinmeta_handler( thinmeta_resource_config_type * ptr );

/* Compute the thinpool metadata usage for a specific LVM thinpool */
int calculate_metadata_usage(thinmeta_resource_config_type * ptr);

/* Returns the reference to the rmon control pointer */ 
rmon_ctrl_type * get_rmon_ctrl_ptr ();

/* Initialize LVM Thin Pool Metadata monitoring */
void thinmeta_init(thinmeta_resource_config_type * res, struct mtc_timer * timers, int count);

/* Clears any previously raised interface alarms if rmon is restarted */
void interface_alarming_init ( interface_resource_config_type * ptr );

/*  Map an interface (mgmt, oam or infra) to a physical port */
void init_physical_interfaces ( interface_resource_config_type * ptr );

/* returns true if the link is up for the specified interface */
int get_link_state ( int ioctl_socket, char iface[20], bool * running_ptr );

/* Service state changes for monitored interfaces */
int service_interface_events ( int nl_socket , int ioctl_socket );

/* Set the interface resource in the correct state for the interface resource handler */
void service_resource_state ( interface_resource_config_type * ptr );

/* Get the interface resource by index */
interface_resource_config_type * get_interface_ptr ( int index );

/* Get the resource by index */
resource_config_type * get_resource_ptr ( int index );

/* Resource monitor handler cleanup */
void rmon_hdlr_fini ( rmon_ctrl_type * ctrl_ptr );

void build_entity_instance_id ( resource_config_type *ptr, char *entity_instance_id);

/* Resource monitor FM interface */
void rmon_fm_init ( void );
void rmon_fm_handler ( void );
EFmErrorT rmon_fm_set ( const SFmAlarmDataT *alarm, fm_uuid_t *fm_uuid );
EFmErrorT rmon_fm_clear ( AlarmFilter *alarmFilter );
EFmErrorT rmon_fm_get ( AlarmFilter *alarmFilter, SFmAlarmDataT **alarm, unsigned int *num_alarm );

/* Save dynamic memory resource (both system memory and AVS memory) */
int save_dynamic_mem_resource ( string resource_name, string criticality,
                                double r_value, int percent, int abs_values[3],
                                const char * alarm_id, int socket_id /*=0*/ );
    
/* Resource failure processing for percentage based thresholds */
void process_failures ( resource_config_type * ptr );
/* Resource failure processing for absolute based thresholds */
void process_failures_absolute ( resource_config_type * ptr );


// convert Severity level into literal defination
static inline string FmAlarmSeverity_to_string(EFmAlarmSeverityT severity)
{
    switch (severity) {
        case FM_ALARM_SEVERITY_CLEAR:
            return "clear";
        case FM_ALARM_SEVERITY_WARNING:
            return "warning";
        case FM_ALARM_SEVERITY_MINOR:
            return "minor";
        case FM_ALARM_SEVERITY_MAJOR:
            return "major";
        case FM_ALARM_SEVERITY_CRITICAL:
            return "critical";
        default:
            return NULL;
    }
}

/****************************************************************************
 *
 * Name       : log_value
 *
 * Purpose    : Log resource state values while avoiding log flooding for
 *              trivial fluxuations.
 *
 * Description: Recommends whether the current resource state value should
 *              be logged based on current, previous and step values.
 *
 * Caller should not generate such log if a false is returned.
 *
 * A true is returned if the currrent and previous resource values differ
 * by +/- step amount.
 *
 * The caller specifies the step that can be overridden by a smaller value
 * in rmond.conf:log_step value.
 *
 * If step is zero then a true is always returned in support of a debug mode
 * where we get the current reading as a log on every audit.
 *
 * The callers previous value is updated to current whenever true is returned.
 *
 ****************************************************************************/

/* a default step value ; change of + or - 5 triggers log */
#define DEFAULT_LOG_VALUE_STEP (5)

bool log_value ( double & current, double & previous, int step );


#endif /* __INCLUDE_RMON_HH__ */
