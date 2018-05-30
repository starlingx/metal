#ifndef __DAEMON_COMMON_H__
#define __DAEMON_COMMON_H__
/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Common Maintenance Header
  */

#include <iostream>
#include <string>

using namespace std ;

#include "logMacros.h"
#include "returnCodes.h"

#ifndef UNUSED
#define UNUSED(_x_) ((void) _x_)
#endif

#ifndef MEMSET_ZERO
#define MEMSET_ZERO(_y_) (memset (&_y_,0,sizeof(_y_)))
#endif

#define DEBUG_HALT ilog ("HALTED !!!!\n"); \
                   for ( ;; ) \
                   { \
                       daemon_signal_hdlr() ; \
                   }


/* List of different types */
typedef enum
{
    SYSTEM_TYPE__NORMAL                  =0,
    SYSTEM_TYPE__CPE_MODE__DUPLEX        =1,
    SYSTEM_TYPE__CPE_MODE__DUPLEX_DIRECT =2,
    SYSTEM_TYPE__CPE_MODE__SIMPLEX       =3,
} system_type_enum ;


/** Called by signal handler on daemon exit
  * Performs cleanup by closing open files 
  * and freeing used memory */
void daemon_exit ( void );

/** daemon_files.cpp cleanup utility */
void daemon_files_fini ( void );

/** daemon_files.cpp init utility
  * Creates log file, process id file
  * and a process fill script */
int  daemon_files_init     ( void );
int  daemon_create_pidfile ( void );
void daemon_remove_pidfile ( void );
void daemon_remove_file    ( const char * filename );
void daemon_rename_file    ( const char * path, const char * old_filename, const char * new_filename );
void daemon_make_dir       ( const char * dir );

string daemon_read_file    ( const char * filename );

void daemon_logfile_close ( void );
void daemon_logfile_open  ( void );

void daemon_log ( const char * filename , const char * str );
void daemon_log_value ( const char * filename , int val );
void daemon_log_value ( const char * filename , const char * str, int val );

/* reads the first line of a file and if it contains a string
 * that represents an integer value then return it */
int  daemon_get_file_int ( const char * filename );
string daemon_get_file_str ( const char * filename );

string daemon_nodetype       ( void );
string daemon_infra_iface    ( void );
string daemon_mgmnt_iface    ( void );
string daemon_sw_version     ( void );
string daemon_bmc_hosts_file ( void );
string daemon_bmc_hosts_dir  ( void );
string daemon_md5sum_file    ( const char * file );

system_type_enum daemon_system_type ( void );

char * daemon_get_iface_master ( char * iface_slave_ptr );

string get_shadow_signature ( char * shadowfile , const char * username,
                              char * shadowinfo, size_t infolen);

void daemon_healthcheck ( const char * sig );
void daemon_health_test ( void );

bool daemon_is_file_present ( const char * filename );
int  daemon_get_rmem_max    ( void );

typedef struct
{
   int count    ;
   int warnings ;
   int errors   ;
} status_type ;

void         daemon_dump_info   ( void ); /**< Common info dump utility       */
const char * daemon_stream_info ( void ); /**< Send the dump info as a string */

void get_debug_options    ( const char * file , daemon_config_type * ptr );


/** 
 * Read and process mtc.ini file settings into the daemon configuration 
 */
int  daemon_configure ( void );

/* Set default config values. 
 * This is especially important for char 8 options that default to null. */
void daemon_config_default ( daemon_config_type * config_ptr );

/**
 * Initialize the daemon main service
 *
 * @param iface
 *- user can overide the management interface via -i option on nthe command line
 *
 */
int daemon_init ( string iface , string nodetype );

/**
 * Run the daemon service
 */
void daemon_service_run ( void );

/* Don't return from this call until the specified file exists
 * or the timeout is exceeded. In the timeout case a FAIL_TIMEOUT
 * is returned. */
int daemon_wait_for_file ( const char * filename, int timeout );

/**
 * Daemon Signal management - init and main loop handler
 */
int  daemon_signal_init  ( void );
void daemon_signal_hdlr  ( void );
void daemon_sigchld_hdlr ( void );

/**
 * Control the enabled state of the signal handler latency monitor
 * true = enabled
 */
void daemon_latency_monitor ( bool state );

void daemon_dump_cfg ( void );

int timeout_config_handler (       void * user,
                             const char * section,
                             const char * name,
                             const char * value);

int debug_config_handler (         void * user,
                             const char * section,
                             const char * name,
                             const char * value);

int sysinv_config_handler  (       void * user,
                             const char * section,
                             const char * name,
                             const char * value);

int client_timeout_handler (       void * user,
                             const char * section,
                             const char * name,
                             const char * value);

/** Test Head Entry */
int daemon_run_testhead ( void );
/**
 * Debug API used to set module debug level.
 */
#define CONFIG_AGENT_HBS_PERIOD      0x00000001 /**< Service period            */
#define CONFIG_AGENT_LOC_TIMEOUT     0x00000002 /**< Loss Of Comm Timeout      */
#define CONFIG_AGENT_MULTICAST       0x00000004 /**< Multicase Addr            */
#define CONFIG_SCHED_PRIORITY        0x00000008 /**< Scheduling priority       */
#define CONFIG_AGENT_HBS_MGMNT_PORT  0x00000010 /**< Management Pulse Rx  Port */
#define CONFIG_AGENT_HBS_INFRA_PORT  0x00000020 /**< Infra Pulse Rx Port       */
#define CONFIG_AGENT_HBS_DEGRADE     0x00000040 /**< Heartbeat degrade         */
#define CONFIG_AGENT_HBS_FAILURE     0x00000080 /**< Heartbeat failure         */
#define CONFIG_AGENT_INV_PORT        0x00000100 /**< Inventory Port Number     */
#define CONFIG_AGENT_HA_PORT         0x00000200 /**< HA Framework Port Number  */
#define CONFIG_CLIENT_MTCALARM_PORT  0x00000400 /**< Send alarm requests to    */
#define CONFIG_RESERVED_800          0x00000800 /**<                           */
#define CONFIG_MTC_TO_HWMON_CMD_PORT 0x00001000 /**< HWmon Port Number         */
#define CONFIG_AGENT_KEY_PORT        0x00002000 /**< Keystone HTTP port        */
#define CONFIG_AGENT_HBS_MTC_PORT    0x00004000 /**< Heartbeat Service Port    */
#define CONFIG_AGENT_INV_EVENT_PORT  0x00008000 /**< Inventory Event Port      */
#define CONFIG_AGENT_API_RETRIES     0x00010000 /**< Num api retries b4 fail   */
#define CONFIG_AGENT_MTC_INFRA_PORT  0x00020000 /**< Agent Infr network port   */
#define CONFIG_AGENT_MTC_MGMNT_PORT  0x00040000 /**< Agent Infr network port   */
#define CONFIG_AGENT_TOKEN_REFRESH   0x00080000 /**< Token refresh rate mask   */
#define CONFIG_CLIENT_MTC_INFRA_PORT 0x00100000 /**< Client Infra nwk mtc port */
#define CONFIG_CLIENT_MTC_MGMNT_PORT 0x00200000 /**< Client mgmnt nwk mtc port */ 
#define CONFIG_AGENT_VIM_CMD_PORT    0x00400000 /**< VIM Command Port Mask     */
#define CONFIG_CLIENT_HBS_INFRA_PORT 0x00800000 /**< Infrastructure ntwk Port  */
#define CONFIG_CLIENT_HBS_MGMNT_PORT 0x01000000 /**< Management network Port   */
#define CONFIG_CLIENT_HBS_EVENT_PORT 0x02000000 /**< Heartbeat Event Messaging */
#define CONFIG_MTC_TO_HBS_CMD_PORT   0x04000000 /**< Mtce to Hbs Command Port  */
#define CONFIG_HBS_TO_MTC_EVENT_PORT 0x08000000 /**< Hbs to Mtc Event Port     */
#define CONFIG_CLIENT_PULSE_PORT     0x10000000 /**< Pmon pulse port           */
#define CONFIG_AGENT_VIM_EVENT_PORT  0x40000000 /**< VIM Event Port Mask       */
#define CONFIG_CLIENT_RMON_PORT      0x80000000 /**< Rmon client port          */

#define CONFIG_AGENT_PORT  CONFIG_AGENT_MTC_MGMNT_PORT
#define CONFIG_CLIENT_PORT CONFIG_CLIENT_MTC_MGMNT_PORT

typedef struct {
    struct timespec ts ;
    struct tm t;
    char   time_buff[50];
} time_debug_type ;

typedef struct
{
    long secs  ;
    long msecs ;
} time_delta_type ;

int timedelta ( time_debug_type & before , time_debug_type & after, time_delta_type & delta );
int gettime   ( time_debug_type & nowtime ) ;
unsigned long long  gettime_monotonic_nsec ( void );

/* get formatted future time for number of seconds from now */
char * future_time ( int secs );


/*****************************************************************************************
 *
 * #######  ###  #######      #####   #     #  ######   ######   #######  ######   #######
 * #         #      #        #     #  #     #  #     #  #     #  #     #  #     #     #
 * #         #      #        #        #     #  #     #  #     #  #     #  #     #     #
 * #####     #      #         #####   #     #  ######   ######   #     #  ######      #
 * #         #      #              #  #     #  #        #        #     #  #   #       #
 * #         #      #        #     #  #     #  #        #        #     #  #    #      #
 * #        ###     #         #####    #####   #        #        #######  #     #     #
 *
 * Allows a single fault insertion condition to be created and monitored in a commo way
 * for any maintenance daemon.
 *
 * Here is how it works.
 *
 * Daemons that want fit support must add daemon_load_fit to its main loop whic will
 * detect and load any new fit requests.
 *
 * Create '/var/run/fit/fitinfo' file with the following labels (with no spaces)
 *
 * proc=hwmond     ; specifies the process name to apply this fit to
 * code=1          ; specifies the unique fit code to loom for
 * hits=2          ; specifies nmber of hits before clearing fit info ; defaults to 1
 *
 *      if ( daemon_want_fit ( MY_FIT_CODE ) == true )
 *           do_fit_condition
 *
 * Add additional labels for further fit refinements ...
 *
 *
 *
 *
 * host=compute-0
 *
 *       if ( daemon_want_fit ( MY_FIT_CODE , hostname ) == true )
 *            do_fit_condition
 *
 *
 *
 * name=Temp_CPU0
 *
 *       if ( daemon_want_fit ( MY_FIT_CODE, hostname, "Temp_CPU0" ) == true )
 *            do_fit_condition
 *
 *
 *
 * data=cr
 *
 *       if ( daemon_want_fit ( MY_FIT_CODE hostname, "Temp_CPU0", data ) == true )
 *            do_fit_condition_with data
 *
 *
 *
 * When the 'daemon_load_fit' sees this file it will load its content and rename
 * /var/run/fit/fitinfo /var/run/fit/fitinfo.renamed.
 *
 * daemon_want_fit returns a true when that fit condition is met and hits is decremented
 * when hits becomes 0 the fit is removed from memory and requires fitinfo.renamed to be
 * recopied to fitinfo for that fit to be seen and loaded again.
 *
 *****************************************************************************************/

// #define WANT_FIT_TESTING

#ifdef WANT_FIT_TESTING

#define FIT__INFO_FILE             ("/var/run/fit/fitinfo")
#define FIT__INFO_FILEPATH         ("/var/run/fit")
#define FIT__INFO_FILENAME         ("fitinfo")
#define FIT__INFO_FILENAME_RENAMED ("fitinfo.renamed")

#define FIT__INIT_FILE             ("/var/run/fit/fitinit")
#define FIT__INIT_FILEPATH         ("/var/run/fit")
#define FIT__INIT_FILENAME         ("fitinit")
#define FIT__INIT_FILENAME_RENAMED ("fitinit.renamed")

/* Common Fault Insertion Structure */
typedef struct
{
    int    code ; /* the unique code specifying the condition to fault  */
    int    hits ; /* how many times to run fit before it auto clears    */
    string proc ; /* the daemon to apply the fit to                     */
    string host ; /* host to apply the fit to                           */
    string name ; /* refinement of the fit code to a specific condition */
    string data ; /* returned fit data for specified named condition    */
} daemon_fit_type ;

#endif

/* Init / Clear the in-memory fit info struct.
 * Automatically called during files init.
 * Can be explicitely called to force remove fit condition. */
void daemon_init_fit ( void );

/* Load fit info from /var/run/fit/fitinfo file.
 * Add a call to this to the daemon's main loop */
int  daemon_load_fit ( void ) ;

/* add hits to fit */
void daemon_hits_fit ( int hits );

/* Check for specific fit enabled conditions */
bool daemon_want_fit ( int code );
bool daemon_want_fit ( int code, string hostname );
bool daemon_want_fit ( int code, string hostname, string name );

/* ... and in this case update fit data reference string when hit */
bool daemon_want_fit ( int code, string hostname, string name, string & data );

/* Prints the in-memory loaded fit data.
 * This is called on new fit info load (and file rename) */
void daemon_print_fit( bool hit );


void daemon_do_segfault ( void );

#endif /* __MTC_COMMON_H__ */
