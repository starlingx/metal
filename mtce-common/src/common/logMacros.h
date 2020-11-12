#ifndef __INCLUDE_NODELOG_HH__
#define __INCLUDE_NODELOG_HH__
/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform "Node Log" Header
  */

#include <syslog.h>

#define DEBUG_LEVEL1     0x00000001
#define DEBUG_LEVEL2     0x00000002
#define DEBUG_LEVEL3     0x00000004
#define DEBUG_LEVEL4     0x00000008
#define DEBUG_MEM_LOG    0x00000010
#ifndef __AREA__
#define __AREA__ "---"
#endif

// #include "daemon_common.h"

/* including for getpid */
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** configuration options */
typedef struct
{
    int   scheduling_priority   ; /**< Scheduling priority of this daemon     */
    bool  active                ; /**< Maintenance activity state true|false  */
    int   hbs_pulse_period      ; /**< time (msec) between heartbeat requests */
    int   token_refresh_rate    ; /**< token refresh rate in seconds          */
    int   hbs_minor_threshold   ; /**< heartbeat miss minor threshold         */
    int   hbs_degrade_threshold ; /**< heartbeat miss degrade threshold       */
    int   hbs_failure_threshold ; /**< heartbeat miss failure threshold       */
    char* hbs_failure_action    ; /**< action to take on host heartbeat falure*/

    char* mgmnt_iface           ; /**< management interface name pointer      */
    char* clstr_iface           ; /**< cluster-host interface name pointer    */
    char* multicast             ; /**< Multicast address                      */
    int   ha_port               ; /**< HA REST API Port Number                */
    int   vim_cmd_port          ; /**< Mtce -> VIM Command REST API Port      */
    int   vim_event_port        ; /**< VIM  -> Mtce Event REST API Port       */
    int   mtc_agent_port        ; /**< mtcAgent receive port (from Client)    */
    int   mtc_client_port       ; /**< mtcClient receive port (from Agent)    */

    char* uri_path              ; /**< /mtce/lmon ... for link monitor        */
    int   keystone_port         ; /**< Keystone REST API port number          */
    char* keystone_prefix_path  ; /**< Keystone REST API prefix path          */
    char* keystone_auth_host    ; /**< =auth_host=192.168.204.2               */
    char* keystone_identity_uri ; /**< =http://192.168.204.2:5000/            */
    char* keystone_auth_uri     ; /**< =http://192.168.204.2:5000/            */
    char* keystone_auth_username ; /**< =mtce                                 */
    char* keystone_auth_pw      ; /**< =abc123                                */
    char* keystone_region_name  ; /**< =RegionOne                             */
    char* keystone_auth_project ; /**< =services                              */
    char* keystone_user_domain;   /**< = Default                              */
    char* keystone_project_domain; /**< = Default                             */

    char* sysinv_mtc_inv_label  ; /**< =/v1/hosts/                            */
    int   sysinv_api_port       ; /**< =6385                                  */
    char* sysinv_api_bind_ip    ; /**< =<local floating IP>                   */

    char* barbican_api_host     ; /**< Barbican REST API host IP address      */
    int   barbican_api_port     ; /**< Barbican REST API port number          */

    int   mtc_rx_mgmnt_port     ; /**< mtcClient listens mgmnt nwk cmd reqs   */
    int   mtc_rx_clstr_port     ; /**< mtcClient listens clstr nwk cmd reqs   */
    int   mtc_tx_mgmnt_port     ; /**< mtcClient sends mgmnt nwk cmds/resp's  */
    int   mtc_tx_clstr_port     ; /**< mtcClient sends clstr nwk cmds/resp's  */

    int   hbs_agent_mgmnt_port  ; /**< hbsAgent mgmnt network pulse resp port */
    int   hbs_client_mgmnt_port ; /**< hbsClient mgmnt network pulse req port */
    int   hbs_agent_clstr_port  ; /**< hbsAgent clstr network pulse resp port */
    int   hbs_client_clstr_port ; /**< hbsClient clstr network pulse req port */
    int   daemon_log_port       ; /**< daemon log port                        */

    int   mtcalarm_req_port     ; /**< port daemons send alarm requests to    */

    int   agent_rx_port ;
    int   client_rx_port ;

    bool  clstr_degrade_only    ; /**< Only degrade on clstr heartbeat failure */
    int   mtc_to_hbs_cmd_port   ; /**< mtcAgent to hbsAgent command port      */
    int   mtc_to_guest_cmd_port ; /**< mtcAgent to guestAgent command port    */
    int   hwmon_cmd_port        ; /**< mtcAgent to hwmon command port         */
    int   hbs_to_mtc_event_port ; /**< hbsAgent to mtcAgent event port        */
    int   inv_event_port        ; /**< Port inventory sends change events on  */
    int   per_node              ; /**< Memory usage per node or per resource  */
    int   audit_period          ; /**< daemon specific audit period           */
    int   pm_period             ; /**< Resmon specific pm period              */
    int   ntp_audit_period      ; /**< Resmon specific ntp audit period       */
    int   ntpq_cmd_timeout      ; /**< Resmon specific ntpq command timeout   */
    int   pmon_amon_port        ; /**< active process monitor pulse rx port   */
    int   pmon_event_port       ; /**< process monitor tx event port          */
    int   pmon_pulse_port       ; /**< process Monitor I'm Alive pulse port   */
    int   pmon_cmd_port         ; /**< process Monitor command receive port   */
    int   log_step              ; /**< used to throttle logging at step rate  */
    int   event_port            ; /**< daemon specific event tx port          */
    int   cmd_port              ; /**< daemon specific command rx port        */
    int   sensor_port           ; /**< sensor read value port                 */
    int   sm_server_port        ; /**< port mtce uses to receive data from SM */
    int   sm_client_port        ; /**< port mtce uses to send SM data         */
    int   lmon_query_port       ;
    int   start_delay           ; /**< startup delay, added for pmon          */
    int   api_retries           ; /**< api retries before failure             */
    int   hostwd_failure_threshold ; /**< allowed # of missed pmon/hostwd messages */
    bool  hostwd_reboot_on_err  ; /**< should hostwd reboot on fault detected */
    bool  hostwd_kdump_on_stall ; /**< sysrq crash dump on quorum msg'ing stall */
    bool  hostwd_use_kern_wd    ; /**< use the kernel watchdog for extra safety */
    bool  need_clstr_poll_audit ; /**< true if we need to poll for clstr      */
    char *hostwd_console_path   ; /**< console on which to log extreme events */
    char *mode                  ; /**< Test Mode String                       */
    int   testmode              ; /**< Test Head Test Mode                    */
    int   testmask              ; /**< bit mask of stress tests               */
    unsigned int mask           ; /**< Config init mask                       */


    /* Debug of compute hang issue */
    unsigned int stall_pmon_thld;
    int   stall_mon_period      ;
    int   stall_poll_period     ;
    int   stall_rec_thld        ;
    char* mon_process_1         ;
    char* mon_process_2         ;
    char* mon_process_3         ;
    char* mon_process_4         ;
    char* mon_process_5         ;
    char* mon_process_6         ;
    char* mon_process_7         ;

    int   latency_thld          ; /**< scheduling latency threshold in msec b4 log */

    /** Configurable Timeouts ; unit is 'seconds'                             */
    int   controller_mtcalive_timeout  ; /**< mtcAlive wait timeout           */
    int   compute_mtcalive_timeout     ; /**< mtcAlive wait timeout           */
    int   goenabled_timeout            ; /**< goenabled wait timeout          */
    int   host_services_timeout        ; /**< host services start/stop timeout*/
    int   swact_timeout                ; /**< swact wait timeout              */
    int   sysinv_timeout               ; /**< sysinv reset api timeout secs   */
    int   sysinv_noncrit_timeout       ; /**< sysinv nonc request timeout     */
    int   work_queue_timeout           ; /**< end of action workq complete TO */
    int   loc_recovery_timeout         ; /**< loss of comms recovery timeout  */
    int   node_reinstall_timeout       ; /**< node reinstall timeout          */
    int   dor_mode_timeout             ; /**< dead office recovery timeout    */
    int   dor_recovery_timeout_ext     ; /**< dor recovery timeout extension  */
    int   uptime_period                ; /**< Uptime refresh timer period     */
    int   online_period                ; /**< locked availability refresh     */
    int   insv_test_period             ; /**< insv test period in secs        */
    int   oos_test_period              ; /**< oos test period in secs         */
    int   failsafe_shutdown_delay      ; /**< seconds before failsafe reboot  */
    int   hostwd_update_period         ; /**< expect hostwd to be updated     */
    int   autorecovery_threshold       ; /**< AIO stop autorecovery threshold */

    /**< Auto Recovery Thresholds                                             */
    int   ar_config_threshold          ; /**< Configuration Failure Threshold */
    int   ar_goenable_threshold        ; /**< GoEnable Failure Threshold      */
    int   ar_hostservices_threshold    ; /**< Host Services Failure Threshold */
    int   ar_heartbeat_threshold       ; /**< Heartbeat Soak Failure Threshold*/

    /**< Auto Recovery Retry Intervals                                        */
    int   ar_config_interval           ; /**< Configuration Failure Interval  */
    int   ar_goenable_interval         ; /**< GoEnable Failure Interval       */
    int   ar_hostservices_interval     ; /**< Host Services Failure Interval  */
    int   ar_heartbeat_interval        ; /**< Heartbeat Soak Failure Interval */

    int   debug_all    ;
    int   debug_json   ; /**< Enable jlog (json string  ) output if not false */
    int   debug_timer  ; /**< Enable tlog (timer logs   ) output if not false */
    int   debug_fsm    ; /**< Enable flog (fsm debug    ) output if not false */
    int   debug_http   ; /**< Enable hlog (http logs    ) output if not false */
    int   debug_msg    ; /**< Enable mlog (msg logs     ) output if not false */
    int   debug_work   ; /**< Enable qlog (work Q logs  ) output if not false */
    int   debug_state  ; /**< Enable clog (state changes) output if not false */
    int   debug_alive  ; /**< Enable alog (mtcAlive logs) output if not false */
    int   debug_bmgmt  ; /**< Enable alog (brd mgmt logs) output if not false */
    int   debug_level  ; /**< Enable dlog (debug levels ) output if not 0     */
    char* debug_filter ;
    char* debug_event  ; /**< Event signature to trace                        */
    bool  flush        ; /**< Force log flush in main loop                    */
    int   flush_thld   ; /**< Flush threshold                                 */

    int   fit_code     ; /**< fault insertion code ; nodeBase.h fit_code_enum */
    char* fit_host     ; /**< the host to apply the fault insertion code to   */
} daemon_config_type ;

daemon_config_type * daemon_get_cfg_ptr (void);
int daemon_set_cfg_option ( const char * option , int value );

bool ltc ( void );

/* returns the current log count */
int lc (void);

char * pt ( void ) ; /* returns pointer to the current time      */
char  * _hn ( void ) ; /* returns pointer to the current host name */
void set_hn ( char * hn ); /* set the current host name */

/* copy time (not date) into callers buffer */
void gettime ( char * now_time_ptr ) ;

extern char *program_invocation_name;
extern char *program_invocation_short_name;
#define _pn program_invocation_short_name

#define SYSLOG_OPTION LOG_NDELAY
#define SYSLOG_FACILITY LOG_LOCAL5

/** Open syslog */
#define open_syslog() \
{ \
    openlog(program_invocation_short_name, SYSLOG_OPTION, SYSLOG_FACILITY ) ; \
}

/** Open syslog using filename identifier */
#define open_syslog_args(filename) \
{ \
    openlog(filename, SYSLOG_OPTION, SYSLOG_FACILITY ) ; \
}

/** Close syslog */
#define close_syslog() \
{ \
    closelog(); \
}

/* ltc represents '-f' option for running in forground and means 'log to console' */

/** Scheduling Latency */
#define NSEC_TO_MSEC (1000000)
#define NSEC_TO_SEC  (1000000000)
#define llog(format, args...) \
        { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Latncy: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \

/** Swerr logger macro*/
#define slog(format, args...) { \
    if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Swerr : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Swerr : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}

/** Error log macro */
#define elog(format, args...) { \
    if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Error : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Error : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}

/** Error logger macro with throttling */
#define elog_throttled(cnt,max,format,args...) { \
    if ( ++cnt == 1 ) \
    { \
        if (ltc()) {   printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Error : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Error : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    } \
    if ( cnt >= max ) \
    { \
        cnt = 0 ; \
    } \
}

/** Warning logger macro */
#define wlog(format, args...) { \
    if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Warn : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Warn : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}

/** Warning logger macro with throttling */
#define wlog_throttled(cnt,max,format,args...) { \
    if ( ++cnt == 1 ) \
    { \
        if (ltc()) {   printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Warn : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Warn : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    } \
    if ( cnt >= max ) \
    { \
        cnt = 0 ; \
    } \
}

/** Info logger macro with throttling */
#define ilog_throttled(cnt,max,format,args...) { \
    if ( ++cnt == 1 ) \
    { \
        if (ltc()) {   printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Info : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Info : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    } \
    if ( cnt >= max ) \
    { \
        cnt = 0 ; \
    } \
}

/** Work Queue logger macro with throttling */
#define qlog_throttled(cnt,max,format,args...) { \
    if ( daemon_get_cfg_ptr()->debug_work ) \
    { \
        if ( ++cnt == 1 ) \
        { \
            if (ltc()) {    printf ("%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Work : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
            else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Work : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        } \
        if ( cnt >= max ) \
        { \
            cnt = 0 ; \
        } \
    } \
}

/** Info logger macro*/
#define ilog(format, args...) { \
    if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Info : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Info : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}

/** Info logger macro*/
#define dlog(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&1) \
    { \
        if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    } \
}

/** Debug print macro used to record a "debug log" with file, line and function. */

/** Info logger macro*/
#define dlog1(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&2) \
    { \
        if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug2: " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug2: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    } \
}

/** Info logger macro*/
#define dlog2(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&4) \
    { \
        if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug4: " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug4: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    } \
}

/** Info logger macro*/
#define dlog3(format, args...) { \
    if(daemon_get_cfg_ptr()->debug_level&8) \
    { \
        if ( ltc() ) { printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug8: " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
        else { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Debug8: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    } \
}

#define blog(format, args...) { \
    if ( ltc() ) { if(daemon_get_cfg_ptr()->debug_bmgmt&1)  printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt : " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { if(daemon_get_cfg_ptr()->debug_bmgmt)   syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}

#define blog1(format, args...) { \
    if ( ltc() ) { if(daemon_get_cfg_ptr()->debug_bmgmt&2) printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt2: " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { if(daemon_get_cfg_ptr()->debug_bmgmt&2) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt2: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}

#define blog2(format, args...) { \
    if ( ltc() ) { if(daemon_get_cfg_ptr()->debug_bmgmt&4) printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt4: " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { if(daemon_get_cfg_ptr()->debug_bmgmt&4) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt4: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}

#define blog3(format, args...) { \
    if ( ltc() ) { if(daemon_get_cfg_ptr()->debug_bmgmt&8) printf ( "%s [%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt8: " format, pt(), getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
    else { if(daemon_get_cfg_ptr()->debug_bmgmt&8) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: BMgt8: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; } \
}


/* This is a progress log with a unique symbol that can be searched on |-| */
/* This log can be used for automated log analysis */
#define plog(format, args...)  { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Info : " format, getpid(), lc(), _hn(), _pn, "|-|", __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define mlog(format, args...)  { if(daemon_get_cfg_ptr()->debug_msg&1 ) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Msg  : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define mlog1(format, args...) { if(daemon_get_cfg_ptr()->debug_msg&2 ) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Msg2 : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define mlog2(format, args...) { if(daemon_get_cfg_ptr()->debug_msg&4 ) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Msg4 : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define mlog3(format, args...) { if(daemon_get_cfg_ptr()->debug_msg&8 ) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Msg8 : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define jlog(format, args...)  { if(daemon_get_cfg_ptr()->debug_json&1) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Json : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define jlog1(format, args...) { if(daemon_get_cfg_ptr()->debug_json&2) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Json2: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define jlog2(format, args...) { if(daemon_get_cfg_ptr()->debug_json&4) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Json4: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define jlog3(format, args...) { if(daemon_get_cfg_ptr()->debug_json&8) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Json8: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define hlog(format, args...)  { if(daemon_get_cfg_ptr()->debug_http&1) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Http : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define hlog1(format, args...) { if(daemon_get_cfg_ptr()->debug_http&2) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Http2: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define hlog2(format, args...) { if(daemon_get_cfg_ptr()->debug_http&4) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Http4: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define hlog3(format, args...) { if(daemon_get_cfg_ptr()->debug_http&8) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Http8: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define alog(format, args...)  { if(daemon_get_cfg_ptr()->debug_alive&1) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Alive : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define alog1(format, args...) { if(daemon_get_cfg_ptr()->debug_alive&2) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Alive2: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define alog2(format, args...) { if(daemon_get_cfg_ptr()->debug_alive&4) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Alive4: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define alog3(format, args...) { if(daemon_get_cfg_ptr()->debug_alive&8) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Alive8: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define qlog(format, args...)  { if(daemon_get_cfg_ptr()->debug_work&1)  syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Work : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define qlog1(format, args...) { if(daemon_get_cfg_ptr()->debug_work&2)  syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Work2: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define qlog2(format, args...) { if(daemon_get_cfg_ptr()->debug_work&4)  syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Work4: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define qlog3(format, args...) { if(daemon_get_cfg_ptr()->debug_work&8)  syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Work8: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define flog(format, args...)  { if(daemon_get_cfg_ptr()->debug_fsm)     syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: FSM  : " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define tlog(format, args...)  { if(daemon_get_cfg_ptr()->debug_timer)   syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Timer: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }

#define clog(format, args...)  { if(daemon_get_cfg_ptr()->debug_state&1) syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Change: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define clog1(format, args...) { if(daemon_get_cfg_ptr()->debug_state&2)  syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Chang2: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define clog2(format, args...) { if(daemon_get_cfg_ptr()->debug_state&4)  syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Chang4: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define clog3(format, args...) { if(daemon_get_cfg_ptr()->debug_state&8)  syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Chang8: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }


#define log_event(format, args...)  { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s: Event: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }
#define log_stress(format, args...) { syslog(LOG_INFO, "[%d.%05d] %s %s %-3s %-18s(%4d) %-24s:Stress: " format, getpid(), lc(), _hn(), _pn, __AREA__, __FILE__, __LINE__, __FUNCTION__, ##args) ; }


#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NODELOG_H__ */
