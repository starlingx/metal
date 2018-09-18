#ifndef __INCLUDE_HWMON_H__
#define __INCLUDE_HWMON_H__
/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud's Hardware Monitor Service Header
  */

/* TODO: Scrub header list removing stuff we don't need */
#include <string.h>
#include <stdio.h>
#include <signal.h>        /* for .. signaling                */
#include <unistd.h>        /* for .. close and usleep         */
#include <stdlib.h>        /* for .. system                   */
#include <list>            /* for the list of conf file names */
#include <time.h>          /* for ... time                    */
#include <sys/types.h>     /*                                 */
#include <sys/socket.h>    /* for ... socket                  */
#include <sys/stat.h>
#include <netinet/in.h>    /* for ... UDP socket type         */
#include <arpa/inet.h>
#include <sys/ioctl.h>     /* for ... ioctl calls             */
#include <net/if.h>        /* for ... ifreq ifr               */
#include <netdb.h>         /* for ... hostent                 */
#include <errno.h>

using namespace std;

#include "nodeBase.h"
#include "alarmUtil.h"     /* for ... common alarm identities          */
#include "daemon_ini.h"    /* Ini Parser Header                        */
#include "daemon_common.h" /* Common definitions and types for daemons */
#include "daemon_option.h" /* Common options  for daemons              */
#include "msgClass.h"

#include "nodeTimers.h"    /* maintenance timer utilities start/stop   */
#include "nodeUtil.h"      /* common utilities                         */
#include "httpUtil.h"      /* for ... libEvent                         */
#include "hwmonAlarm.h"    /* for ... hwmonAlarm_id_type               */

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "mon"

#define MAX_HOST_SENSORS            (512) // (100)
#define MAX_HOST_GROUPS              (20)
#define MIN_SENSOR_GROUPS             (4)
#define MAX_SIZE_SENSOR_MSG_BYTES   (4096*4)
#define HWMON_DEFAULT_LARGE_INTERVAL (MTC_MINS_15)
#define HWMON_DEFAULT_AUDIT_INTERVAL (MTC_MINS_2)
#define HWMON_MIN_AUDIT_INTERVAL     (10)
#define DEGRADE_AUDIT_TRIGGER        (2)
#define MAX_SENSORS_NOT_FOUND        (5)
#define START_DEBOUCE_COUNT          (1)

/* Daemon Sensor Config Directory - where profile files are stored */
#define CONFIG_DIR    ((const char *)("/etc/hwmon.d"))

#define QUANTA_SENSOR_PROFILE_FILE    ((const char *)("/etc/bmc/server_profiles.d/sensor_quanta_v1_ilo_v4.profile"))
#define QUANTA_SENSOR_GROUPS              (5)
#define QUANTA_PROFILE_SENSORS           (55)
#define QUANTA_PROFILE_SENSORS_REVISED_1 (51)

#define ENTITY_DELIMITER ((const char *)":")
#define SENSOR_DELIMITER ((const char  ) '=')
#define DEFAULT_READING  ((const char *) "unknown")

#define CONFIG_AUDIT_PERIOD  (0x00000001)
#define CONFIG_KEYSTONE_PORT (0x00000002)
#define CONFIG_EVENT_PORT    (0x00000004)
#define CONFIG_CMD_PORT      (0x00000008)
#define CONFIG_TOKEN_REFRESH (0x00000020)
#define CONFIG_AUTH_HOST     (0x00000040)
#define CONFIG_INV_EVENT_PORT (0x00000080)

#define CONFIG_MASK ( CONFIG_AUDIT_PERIOD  | \
                      CONFIG_KEYSTONE_PORT | \
                      CONFIG_EVENT_PORT    | \
                      CONFIG_INV_EVENT_PORT| \
                      CONFIG_TOKEN_REFRESH | \
                      CONFIG_CMD_PORT )

typedef enum
{
    HWMON_SEVERITY_GOOD,
    HWMON_SEVERITY_OFFLINE,
    HWMON_SEVERITY_MINOR,
    HWMON_SEVERITY_MAJOR,
    HWMON_SEVERITY_CRITICAL,
    HWMON_SEVERITY_NONRECOVERABLE,
    HWMON_SEVERITY_RESET,
    HWMON_SEVERITY_POWERCYCLE,
    HWMON_SEVERITY_LAST
} sensor_severity_enum;

/* Action strings */
#define HWMON_ACTION_IGNORE      ((const char *)"ignore")
#define HWMON_ACTION_LOG         ((const char *)"log")
#define HWMON_ACTION_ALARM       ((const char *)"alarm")
#define HWMON_ACTION_RESET       ((const char *)"reset")
#define HWMON_ACTION_POWERCYCLE  ((const char *)"power-cycle")

/* Severity              strings */
#define HWMON_MINOR              ((const char *)"minor")
#define HWMON_MAJOR              ((const char *)"major")
#define HWMON_CRITICAL           ((const char *)"critical")

typedef enum
{
    SENSOR_KIND__NONE   = 0x00,
    SENSOR_KIND__TEMP   = 0x01, /* Temperature */
    SENSOR_KIND__VOLT   = 0x02, /* Voltage     */
    SENSOR_KIND__CURR   = 0x03, /* Current     */
    SENSOR_KIND__FAN    = 0x04, /* Fan         */
    SENSOR_KIND__RES1   = 0x05,
    SENSOR_KIND__RES2   = 0x06,
    SENSOR_KIND__CPU    = 0x07,
    SENSOR_KIND__POWER  = 0x08,
    SENSOR_KIND__RES3   = 0x09,
    SENSOR_KIND__RES4   = 0x0A,
    SENSOR_KIND__RES5   = 0x0B,
    SENSOR_KIND__MEM    = 0x0C,
    SENSOR_KIND__DISK   = 0x0D,
    SENSOR_KIND__RES6   = 0x0E,
    SENSOR_KIND__FWPROG = 0x0F,
    SENSOR_KIND__LOG    = 0x10,
    SENSOR_KIND__WDOG   = 0x11,
    SENSOR_KIND__EVENT  = 0x12,
    SENSOR_KIND__INT    = 0x13,
    SENSOR_KIND__BUTTON = 0x14,

} sensor_kind_enum ;

/* Values mimic ipmi_unit_type_e in ipmi_bits.h */
typedef enum
{
    SENSOR_UNIT__NONE   = 0x00,
    SENSOR_UNIT__DEG_C  = 0x01,
    SENSOR_UNIT__DEG_F  = 0x02,
    SENSOR_UNIT__DEG_K  = 0x03,
    SENSOR_UNIT__VOLTS  = 0x04,
    SENSOR_UNIT__AMPS   = 0x05,
    SENSOR_UNIT__WATTS  = 0x06,

    SENSOR_UNIT__RPM    = 18,

    SENSOR_UNIT__BYTES  = 70,
    SENSOR_UNIT__KBYTES,
    SENSOR_UNIT__MBYTES,
    SENSOR_UNIT__GBYTES,
    SENSOR_UNIT__WORDS,
    SENSOR_UNIT__DWORDS,
    SENSOR_UNIT__QWORDS,
    SENSOR_UNIT__LINES,
    SENSOR_UNIT__HITS,
    SENSOR_UNIT__MISSES,
    SENSOR_UNIT__RETRIES = 80,
    SENSOR_UNIT__RESETS,
    SENSOR_UNIT__OVERRUNS,
    SENSOR_UNIT__UNDERRUNS,
    SENSOR_UNIT__COLLISIONS,
    SENSOR_UNIT__PACKETS,
    SENSOR_UNIT__MESSAGES,
    SENSOR_UNIT__CHARACTERS,
    SENSOR_UNIT__ERRORS,
    SENSOR_UNIT__CORRECTABLE_ERRORS,
    SENSOR_UNIT__UNCORRECTABLE_ERRORS = 90,
    SENSOR_UNIT__FATAL_ERRORS

} sensor_unit_enum ;



typedef enum
{
    HWMON_ADD__START = 0,
    HWMON_ADD__STATES,
    HWMON_ADD__WAIT,
    HWMON_ADD__DONE,
    HWMON_ADD__STAGES,
} hwmon_addStages_enum ;

typedef enum
{
    HWMON_SENSOR_MONITOR__IDLE = 0,
    HWMON_SENSOR_MONITOR__START,
    HWMON_SENSOR_MONITOR__DELAY,
    HWMON_SENSOR_MONITOR__READ,
    HWMON_SENSOR_MONITOR__PARSE,
    HWMON_SENSOR_MONITOR__CHECK,
    HWMON_SENSOR_MONITOR__UPDATE,
    HWMON_SENSOR_MONITOR__HANDLE,
    HWMON_SENSOR_MONITOR__FAIL,
    HWMON_SENSOR_MONITOR__POWER,
    HWMON_SENSOR_MONITOR__RESTART,
    HWMON_SENSOR_MONITOR__STAGES
} monitor_ctrl_stage_enum ;

typedef enum
{
    HWMON_CANNED_GROUP__NULL,
    HWMON_CANNED_GROUP__FANS,
    HWMON_CANNED_GROUP__TEMP,
    HWMON_CANNED_GROUP__VOLT,
    HWMON_CANNED_GROUP__POWER,
    HWMON_CANNED_GROUP__USAGE,
#ifdef WANT_MORE_GROUPS
    HWMON_CANNED_GROUP__MEMORY,
    HWMON_CANNED_GROUP__CLOCKS,
    HWMON_CANNED_GROUP__ERRORS,
    HWMON_CANNED_GROUP__MSG,
    HWMON_CANNED_GROUP__TIME,
    HWMON_CANNED_GROUP__MISC,
#endif
    HWMON_CANNED_GROUPS
} canned_group_enum ;


typedef struct
{
    bool ignored ;
    bool alarmed ;
    bool logged  ;
} action_state_type ;

/* Sensor sample data structure for ipmitool output */
typedef struct
{
    string name   ; /* sensor name             */
    string value  ; /* sensor value            */
    string unit   ; /* sensor unit type        */
    string status ; /* status - ok, nc, cr, nr */
    string lnr    ; /* Lower Non-Recoverable   */
    string lcr    ; /* Lower Critical          */
    string lnc    ; /* Lower Non-Critical      */
    string unc    ; /* Upper Non-Critical      */
    string ucr    ; /* Upper Critical          */
    string unr    ; /* Upper Non-Recoverable   */

    /* the group this sensor will go into */
    canned_group_enum group_enum ;

    /* set to true if we want the system to ignore this sensor */
    bool ignore = true ;

    /* used to find sensor name mismatches */
    bool found    ;
} sensor_data_type;


/* Control structure for ipmi sensor monitoring
 *
 * TODO: The interval is part of the host but
 *       should eventually me moved here.
 */
typedef struct
{
    monitor_ctrl_stage_enum       stage ;
    struct mtc_timer              timer ;

    /* monolithic timestamp of the last/this sensor sample time
     * Not Used - future */
    unsigned long long last_sample_time ;
    unsigned long long this_sample_time ;
} monitor_ctrl_type ;

/** Sensor Information: All the information related to a sensor
 *  what is needed to read, threshold along with back end algorithms
 *  that might suppress or downgrade action handling */
typedef struct
{
    string      hostname  ; /**< the board management controller type string */
    string           bmc  ; /**< the board management controller type string */

    string           uuid ; /**< sensor uuid                                 */
    string      host_uuid ; /**< host uuid                                   */
    string     group_uuid ; /**< The UUID of the group this sensor is in     */
    string     sensorname ; /**< sensor name as a string                     */
    string     sensortype ; /**< sensor type string 'voltage', 'fan' etc     */
    string       datatype ; /**< discrete or analog                          */

    bool         suppress ; /**< True to allow action handling               */
    string  actions_minor ; /**< One of the following actions                */
    string  actions_major ; /**<    Ignore, Log, Alarm and for critical only */
    string  actions_critl ; /**<    we add Reset and Powercycle              */

    string         script ; /**< script that can read the sensor             */
    string           path ; /**< sensor read path                            */
    string    entity_path ; /**< entity path is "path:sensorname"            */

    string      algorithm ; /**< unique string representing a mgmt algorithm */
    string          status; /**< offline, ok, minor, major, critical         */
    string          state ; /**< enabled or disabled                         */

    float t_critical_lower; /**< lower threshold for critical alarm assertion*/
    float t_major_lower; /**< lower threshold for major alarm assertion   */
    float t_minor_lower; /**< lower threshold for minor alarm assertion   */

    float t_minor_upper; /**< upper threshold for minor alarm assertion   */
    float t_major_upper; /**< upper threshold for major alarm assertion   */
    float t_critical_upper; /**< upper threshold for critical alarm assertion*/

    string  unit_modifier ; /**< 10^2 , per second or x/sec or x/hr          */
    string  unit_base     ; /**< Celcius, Revolutions                        */
    string  unit_rate     ; /**< Minute                                      */

    protocol_enum    prot ; /**< protocol to use for this sensor             */
    sensor_kind_enum kind ; /**< the kind of sensor ; see definition         */
    sensor_unit_enum unit ; /**< the units the sensor should be displayed in */

    sensor_severity_enum severity      ;
    sensor_severity_enum sample_severity ;
    string sample_status ;
    string sample_status_last ;
    bool degraded ;
    bool alarmed  ;

    int  debounce_count ;
    bool want_debounce_log_if_ok ;

    action_state_type minor ;
    action_state_type major ;
    action_state_type critl ;

    bool                         updated ;
    int  not_updated_status_change_count ;
    bool                           found ;
    canned_group_enum         group_enum ;
    int           not_found_log_throttle ;
} sensor_type ;

#define NOT_FOUND_COUNT_BEFORE_MINOR (3)
#define NOT_FOUND_LOG_THROTTLE   (1)


/******************************************************************************
 * A structure containing sensor model settings that need to be
 * preserved over a model relearn
 ******************************************************************************/
typedef struct
{
    string name  ; /* group name */
    string minor ;
    string major ;
    string critl ;
} group_actions_type ;

typedef struct
{
    int                groups   ;
    int                interval ;
    group_actions_type group_actions[MAX_HOST_GROUPS] ;

} model_attr_type ;

void init_model_attributes ( model_attr_type & attr );

/** Sensor Group Information: All the group information related to a group
 *  of sensors, group actions, group thresholds, etc */
struct sensor_group_type
{
    string            hostname   ; /**< the host this group is assigned to          */
    string            host_uuid  ; /**< sensor name as a string                     */
    string           group_uuid  ; /**< The UUID of the group this sensor is in     */
    string           group_name  ; /**< sensor name as a string                     */
    string           sensortype  ; /**< sensor type string 'voltage', 'fan' etc     */
    canned_group_enum group_enum ; /**< index into group type ; fans,voltage,power  */
    string             datatype  ; /**< discrete or analog                          */
    string            algorithm  ; /**< unique string representing a mgmt algorithm */
    string actions_critical_choices  ; /**< list of actions for critical pull down  */
    string actions_major_choices  ; /**< list of actions for major pull down      */
    string actions_minor_choices  ; /**< list of actions for minor pull down      */
    bool               suppress  ; /**< True to allow action handling               */

    /** pointers to the sensors in this group */
    sensor_type * sensor_ptr[MAX_HOST_SENSORS] ;
    int                 sensors  ; /**< number of sensors in this group             */

    string        sensor_labels  ; /**< list of sensor labels fetched from profile  */

    string                path   ; /**< sensor group read path                      */

    /* current sensor read index within this group ; used by the group monitor FSM
     * This member is only used when we are reading group sensors individually      */
    int        sensor_read_index ;

    string               status  ; /**< group status                                */

    string actions_minor_group   ; /**< One of the following actions                */
    string actions_major_group   ; /**<  Ignore, Log, Alarm, and for critical only  */
    string actions_critl_group   ; /**<    we add Reset and Powercycle              */

    string group_state           ; /**< disabled, minor, major, critical            */
    int    group_interval        ; /**< audit interval                              */

    float  t_critical_lower_group; /**< lower threshold for critical alarm assertion*/
    float  t_major_lower_group   ; /**< lower threshold for major alarm assertion   */
    float  t_minor_lower_group   ; /**< lower threshold for minor alarm assertion   */

    float  t_minor_upper_group   ; /**< upper threshold for minor alarm assertion   */
    float  t_major_upper_group   ; /**< upper threshold for major alarm assertion   */
    float  t_critical_upper_group; /**< upper threshold for critical alarm assertion*/

    string unit_modifier_group   ; /**< 10^2 , per second or x/sec or x/hr          */
    string unit_base_group       ; /**< Celcius, Revolutions                        */
    string unit_rate_group       ; /**< Minute                                      */

    bool  active                 ; /**< true if this sensor request is in progress  */
    bool timeout                 ; /**< true if the last request timed-out          */
    bool failed                  ; /**< true if group read failed                   */
    bool alarmed                 ; /**< true if the group alarm is asserted         */
    struct mtc_timer        timer; /**< group audit timer in seconds                */

    /**< Sensor Read Data Handler
     *
     * Parms: group_ptr - the sensor group pointer
     *        index     - index into the group's sensor_ptr table
     *        response  - the sensor read data as a string
     *
     * Returns: sensor_severity type ; see hwmon.h
     *          > ok, minor, major or critical
     *
     **/
    sensor_severity_enum (*server_handler) (struct sensor_group_type *, int , string );
} ;

/* The Hardware Monitor Messaging Socket Structure                    */
typedef struct
{
    int                 event_port ; /**< hwmon event transmit port    */
    msgClassSock*       event_sock ; /**< ... socket                   */

    int                 cmd_port   ; /**< hwmon command receive port   */
    msgClassSock*       cmd_sock   ; /**< ... socket                   */

    msgSock_type       mtclogd     ; /**< messaging into to mtclogd    */

} hwmon_socket_type ;

/* Note: Any addition to this struct requires explicit
 *       init in daemon_init.
 *       Cannot memset a struct contianing a string type.
 **/
typedef struct
{
    string my_macaddr  ; /**< MAC address of event port       */
    string my_hostname ; /**< My hostname                     */
    string my_local_ip ; /**< Primary IP address              */
    string my_float_ip ; /**< Secondary (floating) IP address */

    bool   active        ; /**< Monitor hardware when true. This is set by
                                either the -a run option on daemon startup
                                or is controlled by the ...HWMON_MON_START
                                and HWMON_MON_STOP commands from maintenance */
    int    audit_period  ;

    struct libEvent        httpEvent ;

    char log_str [MAX_API_LOG_LEN];
    char filename[MAX_FILENAME_LEN];

} hwmon_ctrl_type ;
hwmon_ctrl_type * get_ctrl_ptr ( void ) ;

hwmon_socket_type * getSock_ptr ( void );

void hwmon_stages_init ( void );

/* hwmonHdlr.cpp API */
void hwmon_timer_init   ( void );
int  hwmon_hdlr_init    ( hwmon_ctrl_type * ctrl_ptr );
void hwmon_hdlr_fini    ( hwmon_ctrl_type * ctrl_ptr );
void hwmon_service      ( hwmon_ctrl_type * ctrl_ptr );

/* hwmonInit.cpp API */
int  hwmon_profile_read ( string hostname, const char * profile_name );

/* hwmonMsg.cpp API */
void hwmon_msg_init ( void );
void hwmon_msg_fini ( void );

int   event_tx_port_init ( int port , const char * iface );
int     cmd_rx_port_init ( int port );
int mtclogd_tx_port_init ( void );

int hwmon_log_message  ( const char * hostname,
                         const char * filename,
                         const char * log_str );

int  hwmon_send_event ( string hostname, unsigned int event_code , const char * sensor_ptr );
int  hwmon_service_inbox  ( void );


/* hwmonFsm.cpp API */
void hwmonTimer_handler           ( int sig, siginfo_t *si, void *uc);
extern void timer_handler ( int sig, siginfo_t *si, void *uc);

void   sensorState_print      ( string & hostname, sensor_type * sensor_ptr );


/**
 * @} hwmon_base
 */

#endif /* __INCLUDE_HWMON_H__ */
