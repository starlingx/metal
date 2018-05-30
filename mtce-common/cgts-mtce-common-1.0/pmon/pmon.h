/*
 * Copyright (c) 2014-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
/*
 * This implements the CGCS process Monitor ; /usr/local/bin/pmond
 *
 * Call trace is as follows:
 *    daemon_init
 *        pmon_timer_init
 *        pmon_hdlr_init
 *        daemon_files_init
 *        daemon_signal_init
 *        daemon_configure
 *            ini_parse
 *            get_debug_options
 *            get_iface_macaddr
 *            get_iface_address
 *            get_iface_hostname
 *        socket_init
 *            pmon_msg_init
 *            event_port_init
 *            pulse_port_init
 *        
 *    daemon_service_run
 *       wait for goenable signal
 *       pmon_send_event ( READY )
 *       pmon_service
 *          _config_dir_load
 *          _config_files_load
 *          _forever
 *             service_events
 *             pmon_send_pulse
 *             pmon_send_hostwd
 *          
 * This daemon waits for a "goenabled" signal an then reads all the process
 * configuration files in /etc/pmon.d and begins monitoring them accordingly.
 * A process confguration file is expected to contain the following information
 * ...
 * ...
 * ...
 *
 * But who watches the watcher ? Well there is a built-in mechanism for that.
 * A 'failing' or 'not running' Process Monitor Daemon (pmond) will lead to a 
 * degrade condition for that host. 
 *
 * Here is how it works ...
 *
 * Step 1: pmond is in inittab so that it will be respawned if it dies.
 *
 * Step 2: While running pmond periodically sends a pulse message to the
 *         the local heartbeat Client (hbsClient). 
 *
 *   Note: The hbsClient pulse response message has a flags field with 1
 *         bit dedicated to indicate the presence of the pmond on that host. 
 *
 * Step 3: Every time the hbsClient receives a pmond pulse message it sets
 *         the pmond bit in the flags field of its pulse response. 
 *
 *   Note: So if the pmond dies it stops sending its pulse message that the
 *         pmond bit in the pulse response flags will not be set.
 *
 * Step 4: The heartbeat agent (hbsAgent) looks at the pulse response flags.
 *         For every response that does not contain a pmond flag it increments
 *         the pmond 'missing' counter for that host. 
 *
 * Step 5: Every time it sees the pmod flag it clears the counter. 
 *         If that counter reaches PMOND_MISSING_THRESHOLD then that host
 *         is set to degraded. The degrade condition is cleared as soon
 *         as a single pmond flag is observed.
 *
 */
 /**
  * @file
  * Wind River CGCS Platform Process Monitor Service Header
  */

#include <iostream>
#include <string.h>
#include <stdio.h>
#include <signal.h>        /* for .. signaling                */
#include <unistd.h>        /* for .. close and usleep         */
#include <stdlib.h>        /* for .. system                   */
#include <dirent.h>        /* for config dir reading          */
#include <list>            /* for the list of conf file names */
#include <syslog.h>        /* for ... syslog                  */
#include <sys/wait.h>      /* for ... waitpid                 */
#include <time.h>          /* for ... time                    */
#include <sys/prctl.h>     /* for program control header      */
#include <sys/types.h>     /*                                 */
#include <sys/socket.h>    /* for ... socket                  */
#include <linux/un.h>      /* for ... domain socket type      */
#include <netinet/in.h>    /* for ... UDP socket type         */
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>         /* for hostent                      */
#include <errno.h>
#include <sys/stat.h>
#include <bits/siginfo.h>   /* for CLD_xxxx si_codes */

using namespace std;

/* external header APIs */
#include "fmAPI.h"          /* for ... EFmAlarmSeverityT               */

#include "nodeBase.h"
#include "daemon_ini.h"    /* Ini Parser Header                        */
#include "daemon_common.h" /* Common definitions and types for daemons */
#include "daemon_option.h" /* Common options  for daemons              */
#include "nodeTimers.h"    /* maintenance timer utilities start/stop   */
#include "nodeUtil.h"      /* common utilities */
#include "msgClass.h"

/**
 * @addtogroup pmon_base
 * @{
 */

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "mon"

#ifndef UNUSED
#define UNUSED(_x_) ((void) _x_)
#endif

#define AMON_MAGIC_NUM 0x12345678 

#define CONFIG_DIR    ((const char *)"/etc/pmon.d")

#define PMON_CLEAR        (0)
#define PMON_ASSERT       (1)
#define PMON_LOG          (2)

#define PMON_MAX_ALARMS (100)

/* Notification of Death Of Arbitrary Process */

/* New PRCTL Flag
 *
 * Set/get notification for task state changes */
#define PR_DO_NOTIFY_TASK_STATE 17	

/* This is the data structure for requestion process death
  * (and other state change) information.  Sig of -1 means
  * query, sig of 0 means deregistration, positive sig means
  * that you want to set it.  sig and events are value-result
  * and will be updated with the previous values on every
  * successful call.
  */
struct task_state_notify_info 
{
	         pid_t pid   ;
	         int   sig   ;
	unsigned int   events;
};

/* The "events" bits in the struct correspond to the si_code values in the siginfo_t struct 
 * that would normally be sent along with a SIGCHLD to the parent process.  
 * The bits are mapped like this:
 *
 *     1 << (info->si_code & 0xFF)

The possible si_code values are defined in /usr/include/bits/siginfo.h and are:

enum
{
   CLD_EXITED = 1,               Child has exited. 
   CLD_KILLED,                   Child was killed.
   CLD_DUMPED,                   Child terminated abnormally.
   CLD_TRAPPED,                  Traced child has trapped.
   CLD_STOPPED,                  Child has stopped.
   CLD_CONTINUED                 Stopped child has continued.
};
*/

/* So declare a corresponding set of constants: */

#define MON_EXITED 0x2
#define MON_KILLED 0x4
#define MON_DUMPED 0x8
#define MON_TRAPPED 0x10
#define MON_STOPPED 0x20
#define MON_CONTINUED 0x40

typedef enum
{
   PMOND_RECOVERY_METHOD__SYSVINIT = 0,
   PMOND_RECOVERY_METHOD__SYSTEMD  = 1,
} recovery_method_type ;

/*
 * Used to mark a configured process
 * This aids in freeing duped memory over a process re-config
 */
#define PMOND_INIT_CHECK (0xBABE )

/* Note: Any addition to this struct requires explicit
 *       init in daemon_init.
 *       Cannot memset a struct contianing a string type.
 **/
typedef struct
{
    system_type_enum  system_type ; /**< store the system type     */
    system_state_enum system_state; /**< current system state      */
    /* iface attributes ; hostname, ip and mac address */
    char   my_hostname [MAX_HOST_NAME_SIZE+1];
    string my_macaddr     ; /**< MAC address of event port         */
    string my_address     ; /**< IP address daemon is running on   */
    int    pulse_period   ; /**< send_pulse interval in millisecs  */
    int    processes      ; /**< Number of Monitored Processes     */
    bool   run_audit      ; /**< Forces get_events audit to run    */
    struct sigaction info ; /**< This daemon signal action struct  */
    struct sigaction prev ; /**< Action handler that was replaced  */
                            /**< This is put back on the exit      */
    bool   event_mode     ; /**< true=event mode ; false=polling   */
    int    fd             ; /**< inotify file descriptor           */
    int    wd             ; /**< inotify watch descriptor          */

    unsigned int nodetype    ;
    unsigned int function    ;
    unsigned int subfunction ;

    recovery_method_type recovery_method ; /**< How processes are recovered */
    bool reload_config ;
    bool patching_in_progress ;

} pmon_ctrl_type ;
void pmon_set_ctrl_ptr ( pmon_ctrl_type * ctrl_ptr );

#define PMON_RT_SIGNAL (SIGRTMIN+1)
#define PMON_EVENT_FLAGS ( MON_EXITED | MON_KILLED )
                        // MON_DUMPED
                        // MON_STOPPED 
                        // MON_TRAPPED

int setup_signal_handler ( int rt_signal_num );

#define MAX_CONFIG_LEN (256)
#define MAX_PROCESSES  (100)
#define MAX_STATUS_ERROR_TEXT_LEN 128
#define MAX_COMMAND_LEN (512)

/* Daemon Config Bit Masks */
#define CONFIG_AUDIT_PERIOD        0x1
#define CONFIG_TX_PORT             0x2
#define CONFIG_RX_PORT             0x4
#define CONFIG_PULSE_PORT          0x8
#define CONFIG_MULTICAST           0x10
#define CONFIG_START_DELAY         0x20
#define CONFIG_HOSTWD_PERIOD       0x40
#define CONFIG_CMD_PORT            0x80

/** Daemon Config Mask */
#define CONFIG_MASK   (CONFIG_AUDIT_PERIOD      |\
                       CONFIG_TX_PORT           |\
                       CONFIG_RX_PORT           |\
                       CONFIG_PULSE_PORT        |\
                       CONFIG_START_DELAY       |\
                       CONFIG_CMD_PORT          |\
                       CONFIG_HOSTWD_PERIOD)

/* Monitored Process Config Bit Mask */
#define CONF_PROCESS    (0x0001)
#define CONF_SCRIPT     (0x0002)
#define CONF_STYLE      (0x0004)
#define CONF_PIDFILE    (0x0008)
#define CONF_RESTARTS   (0x0010)
#define CONF_SEVERITY   (0x0020)
#define CONF_INTERVAL   (0x0040)
#define CONF_DEBOUNCE   (0x0080)
#define CONF_STARTTIME  (0x0100)
#define CONF_MODE       (0x0200)
#define CONF_STATUS_ARG (0x0400)
#define CONF_START_ARG  (0x0800)
#define CONF_TIMEOUT    (0x1000)
#define CONF_THRESHOLD  (0x2000)
#define CONF_PERIOD     (0x4000)
#define CONF_PORT       (0x8000)


/* Monitored Passive Process Config Mask */
#define CONF_MASK        (CONF_PROCESS   | \
                          CONF_SCRIPT    | \
                          CONF_STYLE     | \
                          CONF_PIDFILE   | \
                          CONF_SEVERITY  | \
                          CONF_RESTARTS  | \
                          CONF_INTERVAL  | \
                          CONF_DEBOUNCE)

/* Monitored Active Process Config Mask */
#define CONF_AMON_MASK   (CONF_PORT      | \
                          CONF_PERIOD    | \
                          CONF_TIMEOUT   | \
                          CONF_THRESHOLD)

/* Monitored Status Process Config Mask */
#define CONF_STATUS_MON_MASK (CONF_PROCESS   | \
                              CONF_SCRIPT    | \
                              CONF_STYLE     | \
                              CONF_SEVERITY  | \
                              CONF_RESTARTS  | \
                              CONF_INTERVAL  | \
                              CONF_PERIOD    | \
                              CONF_TIMEOUT   | \
                              CONF_START_ARG | \
                              CONF_STATUS_ARG)

#define SEVERITY_CLEAR    0
#define SEVERITY_MINOR    1
#define SEVERITY_MAJOR    2
#define SEVERITY_CRITICAL 3

#define PMON_RESTART_WAIT  (10)
#define MINORLOG_THRESHOLD (20)
#define PIDWAIT_THRESHOLD  (15)
#define MAX_RESPAWN_SECS   (5)

typedef enum
{
    PMON_STAGE__START         = 0,
    PMON_STAGE__MANAGE        = 1,
    PMON_STAGE__RESPAWN       = 2,
    PMON_STAGE__MONITOR_WAIT  = 3,
    PMON_STAGE__MONITOR       = 4,
    PMON_STAGE__RESTART_WAIT  = 5,
    PMON_STAGE__IGNORE        = 6,
    PMON_STAGE__FINISH        = 7,
    PMON_STAGE__POLLING       = 8,
    PMON_STAGE__START_WAIT    = 9,
    PMON_STAGE__TIMER_WAIT    =10,
    PMON_STAGE__STAGES        =11,
}   passiveStage_enum ;

typedef enum
{
    ACTIVE_STAGE__IDLE,
    ACTIVE_STAGE__START_MONITOR,
    ACTIVE_STAGE__PULSE_REQUEST,
    ACTIVE_STAGE__REQUEST_WAIT,
    ACTIVE_STAGE__PULSE_RESPONSE,
    ACTIVE_STAGE__GAP_SETUP,
    ACTIVE_STAGE__GAP_WAIT,
    ACTIVE_STAGE__FAILED,
    ACTIVE_STAGE__DEBOUNCE_SETUP,
    ACTIVE_STAGE__DEBOUNCE,
    ACTIVE_STAGE__FINISH,
    ACTIVE_STAGE__STAGES,
}   activeStage_enum ;

/** Status monitoring states */
typedef enum
{
    STATUS_STAGE__BEGIN,
    STATUS_STAGE__EXECUTE_STATUS,
    STATUS_STAGE__EXECUTE_STATUS_WAIT,
    STATUS_STAGE__EXECUTE_START,
    STATUS_STAGE__EXECUTE_START_WAIT,
    STATUS_STAGE__INTERVAL_WAIT,
    STATUS_STAGE__STAGES,
}   statusStage_enum ;

#define AMON_MAX_LEN (100)
typedef struct
{
    int                    tx_sock ; /**< socket to monitored process */
    int                    tx_port ; /**< port to monitored process   */
    struct sockaddr_in     tx_addr ; /**< process socket attributes   */
    char       tx_buf[AMON_MAX_LEN]; /**< Server receive buffer      */
    socklen_t                  len ; /**< Socket Length              */
} active_mon_socket_type ;

/* Process Specific Monitor Configuration - Static and Dynamic Data    */
typedef struct
{
    unsigned short init_check; /**< checksum of the process config     */
    unsigned int mask        ; /**< Passive monitor config read mask   */
    passiveStage_enum stage  ; /**< Passive monitor FSM stage control  */

    /* Config Items */
    const char * process     ; /**< The name of the process to monitor  */
    const char * service     ; /**< The name of the service to monitor
                                    This is used in centos systemd and
                                    when it comes to respawning processes
                                    this takes precidence if it is not null */
    const char * script      ; /**< Path to and restart script filename */
    const char * style       ; /**< recovery method ; lsb, ocf, systemd */
    const char * pidfile     ; /**< The path to process pidfile         */
    const char * severity    ; /**< Process failure severity
                                      critical : host is failed
                                      major    : host is degraded
                                      minor    : log is generated       */

    unsigned int restarts    ; /**< Number of back to back unsuccessful
                                    restarts before severity assertion  */

    unsigned int interval    ; /**< Number of seconds to wait between
                                    back-to-back unsuccessful restarts  */

    unsigned int debounce    ; /**< Number of seconds the process needs
                                    to run before declaring it as running
                                    O.K. after a restart. Time after
                                    which back-to-back restart count is
                                    cleared.                            */

    unsigned int startuptime ; /**< Seconds to wait after process start
                                    before starting the debounce monitor*/

    const char * mode        ; /**< Monitor mode passive or active.
                                    Passive mode is always performed and
                                    assumed if setting is not specified */

    const char * subfunction ; /**< contains a string specifying the subfunction
                                    of the host */

    bool         quorum      ; /**< Whether or not the process is in the
                                    system health quorum (for host watchdog) */

    bool         quorum_failure      ; /**< flag indicating that a quorum
                                          process has failed. Implements a
                                          single audit debounce for quorum
                                          process failures */

    bool         quorum_unrecoverable ; /**< flag indicating that a quorum
                                    process has been declared unrecoverable  */

    bool         full_init_reqd ; /**< Whether or not we should wait for full
                                    goenabled tests passing before we should
                                    try to restart process              */

    /** Passive Monitoring Dynamic Data                                      */
    /*  -------------------------------                                      */
    bool   passive_monitoring  ; /**< set true when being monitored          */
    struct mtc_timer * pt_ptr  ; /**< fsm and handler process timer pointer  */

    /** holds the alarm severity state of CLEAR, MINOR, MAJOR, CRITICAL    */
    EFmAlarmSeverityT alarm_severity ;
    bool          restart      ;
    bool          failed       ;
    bool          ignore       ; /**< ignore this process ; debug purposes   */
    bool          stopped      ; /**< process was stopped by command         */
    unsigned int  restarts_cnt ; /**< back to back restarts count            */
    unsigned int  debounce_cnt ; /**< running monitor debounce count         */
    unsigned int  minorlog_cnt ; /**< track minor log count for thresholding */
    unsigned int  pidwait_cnt  ; /**< throttle pidwait logs indicating that
                                      spawned child has not exited yet
                                      preventing respawn of new process      */

    bool          sigchld_rxed ; /**< Child respawn exit received            */
    unsigned int  stage_cnt    ; /**< general stage specific count           */
    unsigned int  failed_cnt   ; /**< number of times process has failed     */
             int  child_pid    ; /**< Restart scriptm chile process ID (obs) */
             int  pid          ; /**< The PID of the this process            */
             int  sev          ; /**< Translated severity code; MAJ,MIN,CRIT */
             int status        ; /**< exit status                            */

    /* Active Monitoring Config Members                                      */
    /* --------------------------------                                      */
    bool     active_monitoring ; /**< true if active monitoring enabled      */

    unsigned int amask         ; /**< Active monitoring config mask          */
             int port          ; /**< Heartbeat period in seconds            */

    /* period and timeout is also used in Status Monitoring */
    unsigned int period        ; /**< Heartbeat period in seconds            */
    unsigned int timeout       ; /**< Heartbeat timeout in seconds           */

    unsigned int threshold     ; /**< Number of back to back heartbeat
                                      failures before action                 */

    /** Active Monitoring UNIX Domain socket                                 */
    /*  ------------------------------------                                 */
    active_mon_socket_type msg ; /**< Active monitoring messaging interface  */

    /* Active Monitoring Dynamic Data                                        */
    /* ------------------------------                                        */
    activeStage_enum  active_stage  ; /**< Passive Monitor FSM Stage Control */
    bool         active_failed      ; /**< Active monitoring failed signal   */
    unsigned int tx_sequence        ; /**< outgoing sequence number          */
    unsigned int rx_sequence        ; /**< incoming sequence number          */
    bool         waiting            ; /**< waiting for response              */

    unsigned int pulse_count        ; /**< running pulse count               */
    unsigned int b2b_miss_peak      ; /**< max number of back to back misses */
    unsigned int b2b_miss_count     ; /**< current back to back miss count   */
    unsigned int afailed_count      ; /**< total active mon'ing failed count */
    unsigned int recv_err_cnt       ; /**< counts the receive errors         */
    unsigned int send_err_cnt       ; /**< counts the transmit errors        */
    unsigned int mesg_err_cnt       ; /**< response message error count      */
    unsigned int mesg_err_peak      ; /**< response message error count      */
    unsigned int adebounce_cnt      ; /**< active monitor debounce counter   */
    bool         active_debounce    ; /**< true = in active mon'ing debounce */
    bool         active_response    ; /**< set true on first active response */

    time_debug_type time_start      ; /**< launch start time                 */
    time_debug_type time_stop       ; /**< launch stop time                  */
    time_delta_type time_delta      ; /**< launch execution time             */

    /* Status Monitoring                                                     */
    const char * start_arg          ; /**< start argument for the script     */
    const char * status_arg         ; /**< status argument for the script    */
    const char * status_failure_text_file; /**< path to status failure text file */

    unsigned int status_mask        ; /**< Status monitoring config mask     */
    statusStage_enum status_stage   ; /**< Status Monitor FSM Stage Control  */

    bool status_monitoring          ; /**< true if status monitoring         */
    bool status_failed              ;
    bool was_failed                 ; /**< indicates the process was in the failed state */

    #define AUDIT_EVENT_SEND_REFESH_THRESHOLD (3)
    int  audit_alarm_refresh_count  ; /**< audit event send refresh counter */

    const char * recovery_method    ; /**< the process/service recovery method */
} process_config_type ;
process_config_type * get_process_config_ptr ( int index );
process_config_type * get_process_config_ptr ( string process );

int pmon_process_config ( void * user,
                    const char * section,
                    const char * name,
                    const char * value);


/* pmonHdlr.cpp API */
void pmon_timer_init( void );
int  pmon_hdlr_init ( pmon_ctrl_type * ctrl_ptr );
void pmon_hdlr_fini ( pmon_ctrl_type * ctrl_ptr );
void pmon_service   ( pmon_ctrl_type * ctrl_ptr );

/* pmonMsg.cpp API */
int  pmon_inbox_init    ( void );
void pmon_service_inbox ( void );

/** Daemon Service messaging socket control structure */
typedef struct
{
    /** PMON Command Receive Interface - (UDP over 'lo')            */
    msgClassSock*       cmd_sock; /**< receive pmon commands socket */
    int                 cmd_port; /**< command receive port         */

    /** UDP socket used to send pmond events to maintenance         */
    msgClassSock*       event_sock; /**< Tx Event Socket            */
    int                 event_port; /**< Tx Event Port number       */

    /** UDP Inet "Im Alive" pulse message interface                 */
    int                 pulse_port; /**< Pmon I'm Alive Pulse Port  */
    msgClassSock*       pulse_sock; /**< Pmon I'm Alive Pulse Sock  */
    mtc_message_type    pulse     ; /**< Static pulse message       */
    int                 msg_len   ; /**< Pulse message length       */

    /** UDP socket used to send pmond events to maintenance         */
    int                   amon_sock; /**< Active monitor Rx Socket  */
    int                   amon_port; /**< Active Monitor Rx Port    */
    struct sockaddr_in    amon_addr; /**< Active Monitor Attributes */

    /** Unix socket used to send pmond status to hostwd             */
    int                  hostwd_sock;
    char*                hostwd_path;
    struct sockaddr_un   hostwd_addr;

//   msgClassSock*       mtclogd_sock; /**< sage to mtclogd          */
    msgSock_type       mtclogd ;

} pmon_socket_type ;
pmon_socket_type * pmon_getSock_ptr ( void );

pmon_ctrl_type * get_ctrl_ptr ( void ) ;

void pmon_msg_init ( void );
void pmon_msg_fini ( void );

int  pulse_port_init ( void ) ;
int  event_port_init ( const char * iface , int port );
int  amon_port_init ( int port );
int  hostwd_port_init ( void );

int  pmon_send_event ( unsigned int event_cmd , process_config_type * ptr );

void close_process_socket ( process_config_type * ptr );
int  open_process_socket  ( process_config_type * ptr );


void manage_process_failure ( process_config_type * ptr );
int  register_process       ( process_config_type * ptr );
int  unregister_process     ( process_config_type * ptr );
int  respawn_process        ( process_config_type * ptr );
int  get_process_pid        ( process_config_type * ptr );
bool process_running        ( process_config_type * ptr );
int  manage_alarm         ( process_config_type * ptr, int action );
int  process_config_load    ( process_config_type * ptr, const char * config_file_ptr);
bool want_degrade_clear     ( void ) ;
string get_status_failure_text ( process_config_type * ptr );
void kill_running_child     ( process_config_type * ptr );
bool kill_running_process   ( int pid );

int  amon_service_inbox     ( int processes );

/** Process monitor timer handler */
void pmon_timer_handler ( int sig, siginfo_t *si, void *uc);

/** FSM Handler for Passive Monitoring */
int pmon_passive_handler ( process_config_type * ptr );

/** Passive Monitoring FSM Stage Transition Utility */
int passiveStageChange ( process_config_type * ptr ,  passiveStage_enum newStage );
const char * get_pmonStage_str ( process_config_type * ptr );

/** FSM Handler for Active Monitoring */
int pmon_active_handler  ( process_config_type * ptr );

/** Active Monitoring FSM Stage Transition Utility */
int activeStageChange ( process_config_type * ptr ,   activeStage_enum newStage );
const char * get_amonStage_str ( process_config_type * ptr );

int   amon_send_request   ( process_config_type * ptr );

/** FSM Handler for Status Monitoring */
int pmon_status_handler ( process_config_type * ptr );

/** Status Monitoring FSM Stage Transition Utility */
int statusStageChange ( process_config_type * ptr ,  statusStage_enum newStage );

/** Status Monitoring Commands */
int execute_status_command ( process_config_type * ptr );
int execute_start_command ( process_config_type * ptr );

void quorum_process_failure ( process_config_type * ptr );

#define PMON_MIN_ACTIVE_PERIOD (1)
#define PMON_MAX_ACTIVE_PERIOD (120)

#define PMON_MIN_START_DELAY (1)
#define PMON_MAX_START_DELAY (120)

#define PMON_MIN_AUDIT_PERIOD (50)
#define PMON_MAX_AUDIT_PERIOD (999)

int  pmon_send_pulse ( void ) ;
int  pmon_send_hostwd ( void ) ;

/** Message versions */
#define MTC_MSG_VERSION_15_12_GA_PMON  (1)
#define MTC_MSG_REVISION_15_12_GA_PMON (1)


/**
 * @} pmon_base
 */
