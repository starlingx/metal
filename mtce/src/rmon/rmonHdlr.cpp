 /*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGCS Platform Resource Monitor Handler
 */
#include "rmon.h"        /* rmon header file */
#include "rmonHttp.h"    /* for rmon HTTP libEvent utilties */
#include "rmonApi.h"     /* vswitch calls */
#include <sys/wait.h>
#include <time.h>
#include <signal.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <vector>        /* for storing dynamic resource names */
#include <dirent.h>
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cctype>
#include <pthread.h>
#include <linux/rtnetlink.h> /* for ... RTMGRP_LINK */
#include "nlEvent.h"       /* for ... open_netlink_socket */
#include "nodeEvent.h"     /* for inotify */
#include <json-c/json.h>   /* for ... json-c json string parsing */
#include "jsonUtil.h"
#include "tokenUtil.h"     /* for ... tokenUtil_new_token */

/* Preserve a local copy of a pointer to the control struct to
 * avoid having to publish a get utility prototype into rmon.h */
static rmon_ctrl_type * _rmon_ctrl_ptr = NULL ;
static interface_resource_config_type interface_resource_config[MAX_RESOURCES] ;
static resource_config_type resource_config[MAX_RESOURCES] ;
static thinmeta_resource_config_type thinmeta_resource_config[MAX_RESOURCES] ;
static registered_clients registered_clt[MAX_CLIENTS];

static libEvent_type ceilometerEvent; // for ceilometer REST API request
static libEvent tokenEvent; // for token request

/* Used to set alarms through the FM API */
static SFmAlarmDataT alarmData;
static struct mtc_timer rmonTimer_event ;
static struct mtc_timer rmonTimer_pm ;
static struct mtc_timer rmonTimer_ntp ;

static struct mtc_timer rtimer[MAX_RESOURCES] ;
static struct mtc_timer thinmetatimer[MAX_RESOURCES] ;

static ntpStage_enum   ntp_stage ; /* The stage the ntp is in within the resource handler fsm */
static int             ntp_status ; /* status returned by the ntpq command */
static int             ntp_child_pid ;

/* for dynamic resources */
bool modifyingResources = false;
vector<string> criticality_resource;
vector<string> dynamic_resource;
vector<string> types;
vector<string> devices;
vector<int> fs_index;
vector<string> fs_state;

/** List of config files */
std::list<string> config_files ;
std::list<string>::iterator string_iter_ptr ;
std::list<string> interface_config_files ;

/* percent or abs value for fs resources */
int fs_percent = 0;
int swact_count = 0;

/* for cpu usage */
time_t t1, t2;
int num_cpus = 0; 
int num_base_cpus = 0;
int included_cpu[MAX_BASE_CPU];

static string hostUUID = "";

/* Initial cpu time */
vector<unsigned long long> cpu_time_initial;
/* Later cpu time */ 
vector<unsigned long long> cpu_time_later;

void save_fs_resource ( string resource_name, string criticality, 
                        int enabled, int percent, int abs_values[3], 
                        int alarm_type, string type, string device, int mounted );
void calculate_fs_usage( resource_config_type * ptr );
void _space_to_underscore (string & str );

struct thread_data
{
   pid_t tid;
   pid_t pid;
   unsigned long long nr_switches_count;
   bool thread_running; 
   double resource_usage;
   resource_config_type  * resource;
};

/* info passed to pthreads */ 
struct thread_data t_data;
pthread_t thread;
pthread_mutex_t lock;

/* strict memory accounting off = 0 or on = 1 */
int IS_STRICT = 0;

void mem_log_ctrl ( rmon_ctrl_type * ptr )
{
#define MAX_LEN 500 
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "%s %s %s\n",
            &ptr->my_hostname[0], 
            ptr->my_address.c_str(), 
            ptr->my_macaddr.c_str() );
    mem_log(str);
}

void mem_log_resource ( resource_config_type * ptr )
{
#define MAX_LEN 500
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "Resource:%-15s Sev:%-8s Tries:%u Debounce:%d\n",
            ptr->resource, ptr->severity, ptr->count, ptr->debounce);
    mem_log(str);
}

void mem_log_interface_resource ( interface_resource_config_type * ptr )
{
#define MAX_LEN 500
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "Resource:%-15s Sev:%-8s Debounce:%d\n", 
            ptr->resource, ptr->severity, ptr->debounce);
    mem_log(str);
}

int _config_dir_load   (void);
int _config_files_load (void);


const char rmonStages_str [RMON_STAGE__STAGES][32] =
{
    "Handler-Init",
    "Handler-Start",
    "Manage-Restart",
    "Monitor-Wait",
    "Monitor-Resource",
    "Restart-Wait",
    "Ignore-Resource",
    "Handler-Finish",
    "Failed-Resource",
    "Failed-Resource-clr",
} ;

const char ntpStages_str [NTP_STAGE__STAGES][32] =
{
    "Begin",
    "Execute-NTPQ",
    "Execute-NTPQ-Wait",
} ;

registered_clients * get_registered_clients_ptr ( int index )
{
    if ( index <= _rmon_ctrl_ptr->clients )
        return ( &registered_clt[index] );
    return ( NULL );
}

rmon_ctrl_type * get_rmon_ctrl_ptr ()
{
    return _rmon_ctrl_ptr;
}

interface_resource_config_type * get_interface_ptr ( int index )
{
    if ( index <= _rmon_ctrl_ptr->interface_resources )
        return ( &interface_resource_config[index] );
    return ( NULL );
}

resource_config_type * get_resource_ptr ( int index )
{
    if ( index >= 0 && index <= _rmon_ctrl_ptr->resources )
        return ( &resource_config[index] );
    return NULL;
}

/*****************************************************************************
 *
 * Name    : get_resource_index 
 *
 * Purpose : Get the resource's index based on the name 
 *
 *****************************************************************************/
int get_resource_index ( const char *resource_name, int *index )
{
    for ( int i = 0 ; i < _rmon_ctrl_ptr->resources ; i++ )
    {
        if ( strcmp(resource_config[i].resource, resource_name) == 0)
        {
           *index = i;
           return (PASS);
        }
    }
    return (FAIL);
}

/*****************************************************************************
 *
 * Name    : rmon_hdlr_fini 
 *
 * Purpose : Clean up the resource monitor module 
 *
 *****************************************************************************/
void rmon_hdlr_fini ( rmon_ctrl_type * ctrl_ptr )
{
    for ( int i = 0 ; i < ctrl_ptr->resources ; i++ )
    {
        // mem_log ('\n');
        mem_log_resource ( &resource_config[i] );
    }
    pthread_mutex_destroy(&lock);
    /* Turn off inotify */
    //set_inotify_close ( ctrl_ptr->fd, ctrl_ptr->wd );
}

/*****************************************************************************
 *
 * Name    : resourceStageChange
 *
 * Purpose : Put a resource in the requested stage for use by the resource handler
 *
 *****************************************************************************/
int resourceStageChange ( resource_config_type * ptr , rmonStage_enum newStage )
{
    if ((   newStage < RMON_STAGE__STAGES ) &&
            ( ptr->stage < RMON_STAGE__STAGES ))
    {
        clog ("%s %s -> %s (%d->%d)\n",
                ptr->resource,
                rmonStages_str[ptr->stage], 
                rmonStages_str[newStage], 
                ptr->stage, newStage);
        ptr->stage = newStage ;
        return (PASS);
    }
    else
    {
        slog ("%s Invalid Stage (now:%d new:%d)\n", 
                ptr->resource, ptr->stage, newStage );
        ptr->stage = RMON_STAGE__FINISH ;
        return (FAIL);
    }
}

/*****************************************************************************
 *
 * Name    : ntpStageChange
 *
 * Purpose : Stage change handler for NTP resource
 *
 *****************************************************************************/
int ntpStageChange ( ntpStage_enum newStage )
{
    if ((newStage < NTP_STAGE__STAGES ) &&
            ( ntp_stage < NTP_STAGE__STAGES ))
    {
        clog ("NTP %s -> %s (%d->%d)\n",
                ntpStages_str[ntp_stage],
                ntpStages_str[newStage],
                ntp_stage, newStage);
        ntp_stage = newStage ;
        return (PASS);
    }
    else
    {
        slog ("NTP Invalid Stage (now:%d new:%d)\n", ntp_stage, newStage );
        ntp_stage = NTP_STAGE__BEGIN ;
        return (FAIL);
    }
}

/*****************************************************************************
 *
 * Name    : _config_files_load
 *
 * Purpose : Load the content of each config file into resource_config[x]
 *
 *****************************************************************************/
int _config_files_load (void)
{
    int i = 0 ;
    /* Run Maintenance on Inventory */
    for ( string_iter_ptr  = config_files.begin () ;
            string_iter_ptr != config_files.end () ;
            string_iter_ptr++ )
    {
        if ( i >= MAX_RESOURCES )
        {
            wlog ("Cannot Monitor more than %d resources\n", MAX_RESOURCES );
            break ;
        }
        /* Read the resource config file */
        resource_config[i].mask = 0 ;
        if (ini_parse( string_iter_ptr->data(), rmon_resource_config, 
                    &resource_config[i]) < 0)
        {
            ilog("Read Failure : %s\n", string_iter_ptr->data() );
        }

        else 
        {
            dlog ("Config File : %s\n", string_iter_ptr->c_str());

            /* Init the timer for this resource */
            mtcTimer_reset ( rtimer[i] ) ;
            rtimer[i].service  = resource_config[i].resource ;

            resource_config[i].i                 = i ;
            /* allow to clear an existing alarm if the first reading is good
               after reboot
            */
            resource_config[i].failed            = false ;
            resource_config[i].count             = 0 ;
            resource_config[i].resource_value    = 0 ;
            resource_config[i].resource_prev     = 0 ;
            resource_config[i].stage             = RMON_STAGE__INIT ;
            resource_config[i].sev               = SEVERITY_CLEARED ;
            resource_config[i].alarm_type        = STANDARD_ALARM;
            resource_config[i].failed_send       = 0;
            resource_config[i].alarm_raised      = false;

            /* add the alarm ids for the FM API per resource monitored */ 
            if (strcmp(resource_config[i].resource, CPU_RESOURCE_NAME) == 0) {
                /* platform cpu utilization */ 
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, CPU_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__CPU_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, V_CPU_RESOURCE_NAME) == 0) {
                /* vswitch cpu utilization */ 
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, V_CPU_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__CPU_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, MEMORY_RESOURCE_NAME) == 0) {
                /* platform memory utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, MEMORY_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__MEMORY_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, V_MEMORY_RESOURCE_NAME) == 0) {
                /* vswitch memory utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, V_MEMORY_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__MEMORY_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, FS_RESOURCE_NAME) == 0) {
                /* platform disk utilization */ 
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, FS_ALARM_ID);
                resource_config[i].mounted = MOUNTED;
                resource_config[i].res_type = RESOURCE_TYPE__FILESYSTEM_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, INSTANCE_RESOURCE_NAME) == 0) {
                /* platform disk utilization */ 
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, INSTANCE_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__FILESYSTEM_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, V_CINDER_THINPOOL_RESOURCE_NAME) == 0) {
                /* platform virtual thin pool utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, V_CINDER_THINPOOL_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__FILESYSTEM_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, V_NOVA_THINPOOL_RESOURCE_NAME) == 0) {
                /* platform virtual thin pool utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, V_NOVA_THINPOOL_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__FILESYSTEM_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, V_PORT_RESOURCE_NAME) == 0) {
                /* vswitch port utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH,
                         V_PORT_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__PORT ;
            }
            else if (!strcmp(resource_config[i].resource, V_INTERFACE_RESOURCE_NAME) ||
                     !strcmp(resource_config[i].resource, V_LACP_INTERFACE_RESOURCE_NAME)) {
                /* vswitch interface(lacp or otherwise) utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH,
                         V_INTERFACE_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__INTERFACE ;
            }
            else if (!strcmp(resource_config[i].resource, V_OVSDB_RESOURCE_NAME)) {
                /* vswitch OVSDB manager utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH,
                         V_OVSDB_MANAGER_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__DATABASE_USAGE ;
            }
            else if (!strcmp(resource_config[i].resource, V_OPENFLOW_RESOURCE_NAME)) {
                /* vswitch Openflow utilization */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH,
                         V_OPENFLOW_CONTROLLER_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__NETWORK_USAGE ;
            }
            else if (strcmp(resource_config[i].resource, REMOTE_LOGGING_RESOURCE_NAME) == 0) {
                /* remote logging connectivity */
                snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH,
                         REMOTE_LOGGING_CONTROLLER_CONNECTIVITY_ALARM_ID);
                resource_config[i].res_type = RESOURCE_TYPE__CONNECTIVITY ;
            }
            else
            {
                resource_config[i].res_type = RESOURCE_TYPE__UNKNOWN ;
            }

            ilog ("Monitoring %2d: %s (%s)\n",
                    i,
                    resource_config[i].resource,
                    resource_config[i].severity);
            mem_log_resource ( &resource_config[i] );
            i++;

        }

    }

    _rmon_ctrl_ptr->resources = i ;
    ilog ("Monitoring %d Resources\n", _rmon_ctrl_ptr->resources );
    return (PASS);
}

/*****************************************************************************
 *
 * Name    : _inter_config_load
 *
 * Purpose : Load the content of each config file into interface_resource_config[x]
 *
 *****************************************************************************/
int _inter_config_load (void)
{
    int i = 0 ;

    for ( string_iter_ptr  = interface_config_files.begin () ;
            string_iter_ptr != interface_config_files.end () ;
            string_iter_ptr++ )
    {
        if ( i >= MAX_RESOURCES )
        {
            wlog ("Cannot Monitor more than %d resources\n", MAX_RESOURCES );
            break ;
        }

        /* Read the interface resource config file */
        resource_config[i].mask = 0 ;
        if (ini_parse( string_iter_ptr->data(), rmon_interface_config, 
                    &interface_resource_config[i]) < 0)
        {
            ilog("Read Failure : %s\n", string_iter_ptr->data() );
        }

        else 
        {
            dlog ("Config File : %s\n", string_iter_ptr->c_str());
            ilog ("Monitoring %2d: %s (%s)\n", i, interface_resource_config[i].resource ,
                    interface_resource_config[i].severity );

            interface_resource_config[i].i          = i ;
            interface_resource_config[i].failed     = false ;
            interface_resource_config[i].stage      = RMON_STAGE__INIT ;
            interface_resource_config[i].sev        = SEVERITY_CLEARED ;
            interface_resource_config[i].failed_send = 0;
            interface_resource_config[i].alarm_raised = false;

            /* add the alarm ids for the FM API per resource monitored */ 
            if (strcmp(interface_resource_config[i].resource, OAM_INTERFACE_NAME) == 0) {
                /* add the alarm id for the FM API per resource monitored */ 
                snprintf(interface_resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, OAM_ALARM_ID);
                snprintf(interface_resource_config[i].alarm_id_port, FM_MAX_BUFFER_LENGTH, OAM_PORT_ALARM_ID);
            }
            else if (strcmp(interface_resource_config[i].resource, MGMT_INTERFACE_NAME) == 0) {
                /* add the alarm id for the FM API per resource monitored */ 
                snprintf(interface_resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, MGMT_ALARM_ID);
                snprintf(interface_resource_config[i].alarm_id_port, FM_MAX_BUFFER_LENGTH, MGMT_PORT_ALARM_ID);
            }
            else if (strcmp(interface_resource_config[i].resource, INFRA_INTERFACE_NAME) == 0) {
                /* add the alarm id for the FM API per resource monitored */ 
                snprintf(interface_resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, INFRA_ALARM_ID);
                snprintf(interface_resource_config[i].alarm_id_port, FM_MAX_BUFFER_LENGTH, INFRA_PORT_ALARM_ID);
            }

            mem_log_interface_resource ( &interface_resource_config[i] );       
            i++;

        }       
    }

    _rmon_ctrl_ptr->interface_resources = i ;
    ilog ("Monitoring %d Interface Resources\n", _rmon_ctrl_ptr->interface_resources );
    return (PASS);
}

/*****************************************************************************
 *
 * Name    : _thinmeta_config_load
 *
 * Purpose : Load the content of each config file into thinmeta_config[x]
 *
 *****************************************************************************/
int _thinmeta_config_load (void)
{
    int i = 0 ;

    /* Set hard-coded defaults for all structures */
    for ( int j = 0; j < MAX_RESOURCES; j++)
    {
        thinmeta_resource_config_type * res;
        res = &thinmeta_resource_config[i];
        res->critical_threshold = THINMETA_DEFAULT_CRITICAL_THRESHOLD;
        res->alarm_on = THINMETA_DEFAULT_ALARM_ON;
        res->autoextend_on = THINMETA_DEFAULT_AUTOEXTEND_ON;
        res->autoextend_by = THINMETA_DEFAULT_AUTOEXTEND_BY;
        res->autoextend_percent = THINMETA_DEFAULT_AUTOEXTEND_PERCENT;
        res->audit_period = THINMETA_DEFAULT_AUDIT_PERIOD;
    }

    /* Load resources */
    for ( string_iter_ptr  = config_files.begin () ;
            string_iter_ptr != config_files.end () ;
            string_iter_ptr++ )
    {
        if ( i >= MAX_RESOURCES )
        {
            wlog ("Cannot Monitor more than %d resources\n", MAX_RESOURCES );
            break ;
        }
        /* Read the resource config file */
        if (ini_parse( string_iter_ptr->data(), rmon_thinmeta_config,
                    &thinmeta_resource_config[i]) < 0)
        {
            ilog("Read Failure : %s\n", string_iter_ptr->data() );
        }
        else
        {
            thinmeta_resource_config_type * res;
            res = &thinmeta_resource_config[i];
            if (!res->section_exists)
            {
                dlog3 ("Config File : %s does not have a [%s] section\n",
                        string_iter_ptr->c_str(), THINMETA_CONFIG_SECTION);
                continue;
            }
            dlog ("Config File : %s\n", string_iter_ptr->c_str());

            /* validate loaded configuration */
            if (!res->vg_name || !res->thinpool_name)
            {
                elog("Invalid VG and/or thinpool names for thinpool metadata "
                     "in config file: %s, disabling monitoring", string_iter_ptr->c_str());
                res->critical_threshold = RESOURCE_DISABLE;
                res->vg_name = THINMETA_INVALID_NAME;
                res->thinpool_name = THINMETA_INVALID_NAME;
            }
            else if (res->critical_threshold > 99)
            {
                elog("Metadata monitoring error in config file: %s. Option critical_threshold > 99%%, "
                     "value in config file: %i, disabling monitoring",
                         string_iter_ptr->c_str(), res->critical_threshold)
                    res->critical_threshold = 0;
            }
            else if (res->alarm_on > 1)
            {
                elog("Metadata monitoring error in config file: %s. Option alarm_on is NOT boolean, "
                     "value in config file: %i, disabling monitoring", string_iter_ptr->c_str(), res->alarm_on);
                res->critical_threshold = RESOURCE_DISABLE;
            }
            else if (res->autoextend_on > 1)
            {
                elog("Metadata monitoring error in config file: %s. Option autoextend_on is NOT boolean, "
                     "value in config file: %i, disabling monitoring",
                         string_iter_ptr->c_str(), res->autoextend_on)
                    res->critical_threshold = RESOURCE_DISABLE;
            }
            else if (res->autoextend_percent > 1)
            {
                elog("Metadata monitoring error in config file: %s. Option autoextend_percent is NOT boolean, "
                     "value in config file: %i, disabling monitoring",
                         string_iter_ptr->c_str(), res->autoextend_percent)
                res->critical_threshold = RESOURCE_DISABLE;
            }
            else if ((res->autoextend_percent && res->autoextend_by > 100) ||
                       (res->autoextend_on && res->autoextend_by < 1))
            {
                elog("Metadata monitoring error in config file: %s. Option autoextend_by not in [1,100] interval, "
                    "value in config file: %i, disabling monitoring",
                        string_iter_ptr->c_str(), res->autoextend_by)
                res->critical_threshold = RESOURCE_DISABLE;
            }
            else if ((res->audit_period < 1) || (res->audit_period > 10000))
            {
                elog("Metadata monitoring error in config file: %s. Option audit_period not in [1,10000] interval, "
                    "value in config file: %i, disabling monitoring",
                        string_iter_ptr->c_str(), res->audit_period)
                res->critical_threshold = RESOURCE_DISABLE;
            }

            ilog ("%s/%s pool metadata monitored; resource index: %2d\n", res->vg_name ,
                    res->thinpool_name, i );
            i++;
        }

    }

    _rmon_ctrl_ptr->thinmeta_resources = i ;
    ilog ("Monitoring %d Thinpool Metadata Resources\n", _rmon_ctrl_ptr->thinmeta_resources );
    return (PASS);
}

/*****************************************************************************
 *
 * Name    : rmon_hdlr_init
 *
 * Purpose : Init the handler but also support re-init that might occur over a SIGHUP
 *
 *****************************************************************************/

#define RMON_TIMER_TYPE__EVENT "event"
#define RMON_TIMER_TYPE__PM    "pm"
#define RMON_TIMER_TYPE__NTP   "ntp"
#define RMON_TIMER_TYPE__RES   "resource"
#define RMON_TIMER_TYPE__THIN  "thinpool"

int rmon_hdlr_init ( rmon_ctrl_type * ctrl_ptr )
{
    /* Save the control pointer */
    _rmon_ctrl_ptr = ctrl_ptr ;

    mtcTimer_init ( rmonTimer_event, LOCALHOST, RMON_TIMER_TYPE__EVENT) ;
    mtcTimer_init ( rmonTimer_pm,    LOCALHOST, RMON_TIMER_TYPE__PM ) ;

    if (is_controller())
       mtcTimer_init ( rmonTimer_ntp,LOCALHOST, RMON_TIMER_TYPE__NTP ) ;

    for ( int i = 0 ; i < MAX_RESOURCES ; i++ )
        mtcTimer_init ( rtimer[i], LOCALHOST, RMON_TIMER_TYPE__RES );
    ctrl_ptr->resources = 0 ;

    for ( int i = 0 ; i < MAX_RESOURCES ; i++ )
        mtcTimer_init ( thinmetatimer[i], LOCALHOST, RMON_TIMER_TYPE__THIN  );
    ctrl_ptr->thinmeta_resources = 0 ;

    /* Initialize the Resource Monitor Array */
    memset ( (char*)&resource_config[0], 0, sizeof(resource_config_type)*MAX_RESOURCES);
    memset ( (char*)&interface_resource_config[0], 0, sizeof(interface_resource_config_type)*MAX_RESOURCES);
    memset ( (char*)&thinmeta_resource_config[0], 0, sizeof(thinmeta_resource_config_type)*MAX_RESOURCES);
    memset ( (char*)&registered_clt[0], 0, sizeof(registered_clients)*MAX_CLIENTS);

    /* Read in the list of config files and their contents */
    load_filenames_in_dir ( CONFIG_DIR, config_files ) ;
    /* Read in the list of interface config files and their contents */
    load_filenames_in_dir ( INT_CONFIG_DIR, interface_config_files ) ;

    _thinmeta_config_load();
    _config_files_load ();
    _inter_config_load ();

    /* init Thin Metadata Monitoring after config reload - including timers */
    thinmeta_init(thinmeta_resource_config, thinmetatimer, ctrl_ptr->thinmeta_resources);

    /* Log the control setting going into the main loop */
    mem_log_ctrl ( _rmon_ctrl_ptr );

    /* Initialize instance mount monitoring */ 
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        elog("mutex init failed \n");
    }

    t_data.thread_running = false;
    t_data.resource_usage = MOUNTED;
    t_data.nr_switches_count = 0;
    t_data.pid = getpid();

    return (PASS) ;
}

/*****************************************************************************
 *
 * Name    : _set_severity
 *
 * Purpose : Restores the resource value and the severity of the alarm 
 *
 *****************************************************************************/
void _set_resource_usage ( string reason_text, resource_config_type * ptr )
{
    unsigned int found;
    string res_val;
    size_t last_index;
    string temp_val;
    char resource_usage[10];

    /* extract the resource value from the reason text */ 
    found = reason_text.find_last_of( ' ' );
    temp_val = reason_text.substr(found+1);
    last_index = temp_val.find_first_not_of("0123456789");
    res_val = temp_val.substr(0, last_index);
    snprintf (resource_usage, sizeof(resource_usage), res_val.c_str());
    sscanf(resource_usage, "%lf", &ptr->resource_value);
}

/*****************************************************************************
 *
 * Name    : build_entity_instance_id
 *
 * Purpose : build the alarm's entity_instance_id based on the
 *           resource type and alarm type.
 *
 *****************************************************************************/
void build_entity_instance_id ( resource_config_type *ptr,  char *entity_instance_id )
{
    dlog ("resource name: %s, resource type: %s, alarm type: %d \n", ptr->resource, ptr->type, ptr->alarm_type);

    // Make certain the id is cleared
    entity_instance_id[0] = 0;

    if ( ptr->alarm_type == DYNAMIC_ALARM )
    {
        if ((ptr->type != NULL) && (strcmp(ptr->type, "lvg") == 0 ))
        {
            /* This case covers volume groups */
            /* Use host=<x>.volumegroup=type for id*/
            snprintf((char*)entity_instance_id, FM_MAX_BUFFER_LENGTH,  "%s.volumegroup=%s", _rmon_ctrl_ptr->my_hostname, ptr->resource);
        }
        else
        {
            /* Use host=<x>.filesystem=type for id*/
             snprintf(entity_instance_id, FM_MAX_BUFFER_LENGTH,  "%s.filesystem=%s", _rmon_ctrl_ptr->my_hostname, ptr->resource);
        }
    }
    else if  ( ptr->alarm_type == STATIC_ALARM )
    {
        /* Use host=<x>.filesystem=type for id*/
        snprintf(entity_instance_id, FM_MAX_BUFFER_LENGTH,  "%s.filesystem=%s", _rmon_ctrl_ptr->my_hostname, ptr->resource);
    }
    else if ((ptr->alarm_type == STANDARD_ALARM) && (strstr(ptr->resource, V_MEMORY_RESOURCE_NAME) != NULL))
    {
        /* AVS memory */
        snprintf(alarmData.entity_instance_id, FM_MAX_BUFFER_LENGTH,  "%s.processor=%d", _rmon_ctrl_ptr->my_hostname, ptr->socket_id);
    }
    else if (strstr(ptr->resource, V_CINDER_THINPOOL_RESOURCE_NAME) != NULL)
    {
        /* Cinder thin pool alarm should not be raised against a specific host */
        /* as the volumes are synced between controllers through drbd. */
        /* Instead we use a common entity instance id for both controllers. */
        snprintf(entity_instance_id, FM_MAX_BUFFER_LENGTH, "host=controller");
    }
    else
    {
        /* Use hostname for alarm */
        snprintf(entity_instance_id, FM_MAX_BUFFER_LENGTH, _rmon_ctrl_ptr->my_hostname);
    }

    dlog ("resource %s entity instance id: %s\n", ptr->resource, entity_instance_id);

    return;
}


/*****************************************************************************
 *
 * Name    : thinpool_virtual_space_usage_init
 *
 * Purpose : Determine if we should monitor virtual usage or not: no purpose
 *           in doing so if thin provisioning is not used.
 *
 * Params  : index - the index of the virtual space resource
 *
 * Return  : None.
 *
 *****************************************************************************/
void thinpool_virtual_space_usage_init(int index,
                                       const char *poolName,
                                       const char *poolOwner) {

    if (!poolName or !poolOwner) {
        slog ("No poolName or poolOwner provided");
        return;
    }
    ilog("index = %d, poolName = %s, poolOwner = %s", index, poolName, poolOwner);

    /* Buffer (and its size) for keeping the initial result after executing
       the above command. */
    char current_pool_type[BUFFER_SIZE];
    const unsigned int buffer_size = BUFFER_SIZE;
    /* The command for seeing if the pool type is thin. */
    char lvm_thin_cmd[BUFFER_SIZE];
    const char *thin_pool_expected_result = NULL;

    MEMSET_ZERO(current_pool_type);
    MEMSET_ZERO(lvm_thin_cmd);

    if (strcmp(poolName, "nova-local-pool") == 0) {
        const char *nova_thin_pool_expected_result = "thin-pool";
        thin_pool_expected_result = nova_thin_pool_expected_result;
        sprintf(lvm_thin_cmd, "lvs --segments | grep \"%s\" | awk '{print $5}'", poolName);
    }
    else if (strcmp(poolName, "cinder-volumes-pool") == 0) {
        const char *cinder_thin_pool_expected_result = "thin";
        thin_pool_expected_result = cinder_thin_pool_expected_result;
        sprintf(lvm_thin_cmd, "cat /etc/cinder/cinder.conf | awk -F = '/^lvm_type.*=.*/ { print $2; }' | tail -n 1 | tr -d ' '");
    }
    else {
        slog("Invalid pool name given.");
        return;
    }

    /* Result code. */
    int rc;

    /* Execute the command. */
    rc = execute_pipe_cmd(lvm_thin_cmd, current_pool_type, buffer_size);

    /* If the command has been executed successfuly, continue. */
    if (rc == PASS) {
        if (current_pool_type != NULL) {
            /* If the pool type is not thin, disable the alarm for virtual
               usage. */
            ilog("%s current pool type is set to = %s", poolOwner, current_pool_type);
            if(strcmp(current_pool_type, thin_pool_expected_result) != 0) {
                resource_config[index].alarm_status = ALARM_OFF;
                ilog("%s LVM Thinpool Usage alarm off: thin provisioning not used", poolOwner);
            } else {
                resource_config[index].alarm_status = ALARM_ON;
                ilog("%s LVM Thinpool Usage alarm on: thin provisioning used", poolOwner);
            }
        }
    } else {
        resource_config[index].alarm_status = ALARM_OFF;
        elog("%s LVM Thinpool monitoring state unknown ; alarm disabled (rc:%i)",
             poolOwner, rc);
    }
}

/*****************************************************************************
 *
 * Name    : virtual_space_usage_init
 *
 * Purpose : Determine if we should monitor virtual usage or not: no purpose
 *           in doing so if thin provisioning is not used.
 *
 * Return  : None.
 *
 *****************************************************************************/

void virtual_space_usage_init(const char* resource_name) {

    ilog ("Initialize thin pools for resource %s\n", resource_name);
    int index;
    if ( get_resource_index( resource_name, &index ) == PASS ) {

        if (strcmp(resource_name, V_CINDER_THINPOOL_RESOURCE_NAME) == 0) {
            thinpool_virtual_space_usage_init(index,"cinder-volumes-pool","Cinder");

        } else if (strcmp(resource_name, V_NOVA_THINPOOL_RESOURCE_NAME) == 0) {
            thinpool_virtual_space_usage_init(index, "nova-local-pool","Nova");
        }
    }
    else {
        wlog ("failed get_resource_index for resource %s\n", resource_name);
    }
}

/*****************************************************************************
 *
 * Name    : rmon_alarming_init
 *
 * Purpose : Clears any previously raised rmon alarms if rmon is restarted
 *
 *****************************************************************************/
void rmon_alarming_init ( resource_config_type * ptr ) 
{
    dlog ("resource name: %s, resource type: %s, alarm type: %d \n", ptr->resource, ptr->type, ptr->alarm_type);

    AlarmFilter alarmFilter;

    SFmAlarmDataT *active_alarm = (SFmAlarmDataT*) calloc (1, sizeof (SFmAlarmDataT));
    if (active_alarm == NULL)
    {
       elog("Failed to allocate memory for SFmAlarmDataT\n");
       return;
    }

    build_entity_instance_id (ptr, alarmData.entity_instance_id);

    snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, ptr->alarm_id);
    snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, alarmData.entity_instance_id);

    if (fm_get_fault( &alarmFilter, active_alarm) == FM_ERR_OK) 
    {
        if (active_alarm != NULL) {

            string reasonText(active_alarm->reason_text);
            /* Set the resource severity */ 
            ptr->failed = true;
            ptr->alarm_raised = true;
            ptr->count = ptr->num_tries;
            if ( active_alarm->severity == FM_ALARM_SEVERITY_MINOR )
            {
                ptr->sev = SEVERITY_MINOR;
            }
            else if ( active_alarm->severity == FM_ALARM_SEVERITY_MAJOR )
            {
                ptr->sev = SEVERITY_MAJOR;
                if ( ptr->res_type == RESOURCE_TYPE__FILESYSTEM_USAGE )
                {
                    string err_res_name(ptr->resource);
                    _space_to_underscore(err_res_name);

                    /* clear host degrade for fs usage alarms */
                    snprintf(ptr->errorMsg, sizeof(ptr->errorMsg), "%s %s:",
                             err_res_name.c_str(),
                             DEGRADE_CLEAR_MSG );

                    rmon_send_request ( ptr, _rmon_ctrl_ptr->clients );
                }
            }
            else 
            {
                ptr->sev = SEVERITY_CRITICAL;
            }
            resourceStageChange ( ptr, RMON_STAGE__MONITOR_WAIT );

            if (strcmp(ptr->resource, INSTANCE_RESOURCE_NAME) != 0) 
            {              
                /* Set the resource severity */ 
                _set_resource_usage( reasonText, ptr );
                ilog ("%s setting previously failed resource alarm id: %s entity_instance_id: %s usage: %0.2f\n",
                          ptr->resource, ptr->alarm_id, alarmFilter.entity_instance_id, ptr->resource_value);
            }
            else 
            {       
                ilog ("%s setting previously failed resource alarm id: %s entity_instance_id: %s\n",
                          ptr->resource, ptr->alarm_id, alarmFilter.entity_instance_id);
            }
        }
    }
    free(active_alarm);
}

/*****************************************************************************
 *
 * Name    : send_clear_msg
 *
 * Purpose : Send a message to all registered clients to set the node to 
 * available (clear the degrade)
 *
 *****************************************************************************/
void send_clear_msg ( int index )
{
    int count = 0;
    AlarmFilter alarmFilter;

    SFmAlarmDataT *active_alarm = (SFmAlarmDataT*) calloc (1, sizeof (SFmAlarmDataT));
    if (active_alarm == NULL)
    {
       elog("Failed to allocate memory for SFmAlarmDataT\n");
       return;
    }

    string err_res_name(resource_config[index].resource);
    _space_to_underscore(err_res_name);
    snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, resource_config[index].alarm_id);

    build_entity_instance_id (&resource_config[index], alarmData.entity_instance_id);

    snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, alarmData.entity_instance_id);

    /* Notify rmon clients of fault being cleared */
    snprintf(resource_config[index].errorMsg, sizeof(resource_config[index].errorMsg),
             "%s cleared_alarms_for_resource:", err_res_name.c_str());

    /* check if there is an alarm first for this resource. If there is not then the node  */
    /* should not be in a degrade state                                                   */
    EFmErrorT ret = fm_get_fault( &alarmFilter, active_alarm);
    if ( (ret == FM_ERR_OK) && (active_alarm != NULL) )
    {
        while (( rmon_send_request ( &resource_config[index], _rmon_ctrl_ptr->clients ) != PASS ) && (count < 3 ))
        {
            wlog ("%s request send failed \n", resource_config[index].resource);
            count++;
        }
        if (count > 2)
        {
            wlog ("%s request send failed, count:%d \n", resource_config[index].resource, count);
            resource_config[index].failed_send++;
        }
        if ((resource_config[index].failed_send == MAX_FAIL_SEND) || (count < 3))
        {
            /* Reset the values to defaults */
            swact_count = 0;
            ilog("Setting resource: %s back to defaults \n", resource_config[index].resource);
            resource_config[index].failed = false ;
            resource_config[index].alarm_raised = false ;
            resource_config[index].count = 0 ;
            resource_config[index].sev = SEVERITY_CLEARED ;
            resource_config[index].stage = RMON_STAGE__START ;
            resource_config[index].failed_send = 0;
        }
    }
    else //alarm not found or error
    {
        if (ret == FM_ERR_ENTITY_NOT_FOUND)
        {
            dlog ("Alarm not found for resource: %s entity_instance_id: %s \n", alarmFilter.alarm_id, alarmFilter.entity_instance_id);
        }
        else
        {
            wlog ("fm_get_fault failed for resource: %s entity_instance_id: %s err: %d\n", alarmFilter.alarm_id,
            alarmFilter.entity_instance_id, ret);
        }

        if (active_alarm == NULL)
        {
           elog("fm_get_fault returned null active_alarm\n");
        }

        swact_count++;
        if (swact_count == MAX_SWACT_COUNT)
        {
            /* Reset the values to defaults */
            while (( rmon_send_request ( &resource_config[index], _rmon_ctrl_ptr->clients ) != PASS ) && (count < 3 ))
            {      
                wlog ("%s request send failed \n", resource_config[index].resource);
                count++;
            } 
            swact_count = 0;
            ilog("Setting resource: %s back to defaults \n", resource_config[index].resource);
            resource_config[index].failed = false ;
            resource_config[index].alarm_raised = false ;
            resource_config[index].count = 0 ;
            resource_config[index].sev = SEVERITY_CLEARED ;
            resource_config[index].stage = RMON_STAGE__START ; 
            resource_config[index].failed_send = 0;
        }
    }
    free(active_alarm);
}

/*****************************************************************************
 *
 * Name    : read_fs_file
 *
 * Purpose : read the memory mapped dynamic file system file 
 *****************************************************************************/
void read_fs_file ( vector<string> & dynamic_resources )
{
    FILE * pFile;
    char buf[MAX_LEN];
    int fd; 
    string delimiter = ",";
    size_t pos;
    string token;
    struct stat fileInfo;
    struct flock fl;

    memset ((char *)&fileInfo, 0 , sizeof(fileInfo));

    fl.l_whence = SEEK_SET; 
    fl.l_start  = 0;        
    fl.l_len    = 0;        
    fl.l_pid    = getpid(); 

    pFile = fopen (DYNAMIC_FS_FILE , "r");
    if (pFile != NULL) {    

        fd = fileno(pFile);
        /* lock the file */ 
        fl.l_type   = F_RDLCK;  

        /* lock the file for read and write */ 
        fcntl(fd, F_SETLKW, &fl);  

        if (fd == -1)
        {
            elog("Error opening file for reading");
        }          

        if (fstat(fd, &fileInfo) == -1)
        {
            elog("Error getting the file size");
        }

        char *map = static_cast<char*>( mmap(0, fileInfo.st_size, PROT_READ, MAP_SHARED, fd, 0));
        if (map == MAP_FAILED)
        {
            elog("Error mmapping the file");
        }
        string str(map);

        snprintf( buf, MAX_LEN, str.c_str());
        /* free the mmapped memory */
        if (munmap(map, fileInfo.st_size) == -1)
        {
            elog("Error un-mmapping the file");
        }
        fclose(pFile);
        /* unlock the file */
        fl.l_type = F_UNLCK;  
        fcntl(fd, F_SETLK, &fl);

        while ((pos = str.find(delimiter)) != string::npos) {
            /* separate the resources from the file */ 
            token = str.substr(0, pos);
            dynamic_resources.push_back(token);
            dlog("reading resource %s \n", token.c_str());
            str.erase(0, pos + delimiter.length());
        }
    }
}

/*****************************************************************************
 *
 * Name    : add_dynamic_fs_resource
 *
 * Purpose : Add the dynamic file system resources 
 *****************************************************************************/
void add_dynamic_fs_resource ( bool send_response )
{
#ifdef WANT_FS_MONITORING
    char resource[50];
    char temp_resource[50];
    char device [50];
    char mount_point[50];
    char temp_state[20];
    char type [50];
    char buf[200];
    string criticality = "critical";
    vector<string> resource_list;
    int absolute_thresholds[3];

    memset(absolute_thresholds, 0, sizeof(absolute_thresholds));
    fs_index.clear();
    fs_state.clear();

    /* get a list of all the dynamic fs mounts */ 
    read_fs_file(resource_list);

    for(std::vector<string>::iterator it = resource_list.begin(); it != resource_list.end(); ++it) 
    {
        string str = *it;
        snprintf(buf, sizeof(buf), str.c_str());
        
        // For resources without mounts the mount_point will be NULL
        memset(&mount_point[0], 0, sizeof(mount_point));
        sscanf(buf, "%49s %19s %49s %49s %49s", temp_resource, temp_state, type, device, mount_point);
        string state(temp_state);

        bool found = false;

        if (mount_point[0] != '\0')
        {
           // for resources with mounts, the resource name is the mount value
           snprintf(resource, FM_MAX_BUFFER_LENGTH, mount_point);
        }
        else
        {
           // for resources without mounts, the resource name is the device value
           snprintf(resource, FM_MAX_BUFFER_LENGTH, device);
        }

        /* the dynamic file system is enabled, add it if need be */   
        for (int i=0; i<_rmon_ctrl_ptr->resources; i++) 
        {
            if ( strcmp(resource, resource_config[i].resource) == 0)
            {
                dlog ("resource %s already exists, update the state to %s \n", resource, state.c_str());
                /* resource already exists no need to add it again */
                /* update the state, it may have changed            */
                fs_index.push_back(i);
                fs_state.push_back(state);
                found = true;
                break;
            }
        }

        if (!found) // new resource to monitor, lets add it
        { 
            int enabled_resource = ALARM_OFF;
            if (strcmp(temp_state,"enabled") == 0)
            {
                enabled_resource = ALARM_ON;
            }

            if (mount_point[0] != '\0')
            {
                save_fs_resource ( resource, criticality, enabled_resource, fs_percent, absolute_thresholds, DYNAMIC_ALARM, type, device, MOUNTED );
            }
            else
            {
                save_fs_resource ( resource, criticality, enabled_resource, fs_percent, absolute_thresholds, DYNAMIC_ALARM, type, device, NOT_MOUNTED );
            }

            if (enabled_resource == ALARM_ON) {
                calculate_fs_usage( &resource_config[_rmon_ctrl_ptr->resources - 1] ); 
                rmon_alarming_init( &resource_config[_rmon_ctrl_ptr->resources - 1] ); 
            }
        }
    }
#endif
    if (send_response)
    {
#ifdef WANT_FS_MONITORING
        ilog ("sending response to dynamic FS add, to the rmon client\n");
#else
        ilog("dynamic filesystem monitoring moved to collectd\n");
#endif
        /* let the rmon client know that we are done with the file */
        rmon_resource_response(_rmon_ctrl_ptr->clients);
    }
}

/*****************************************************************************
 *
 * Name    : clear_alarm_for_resource
 *
 * Purpose : Clear the alarm of the resource passed in
 *
 *****************************************************************************/
void clear_alarm_for_resource ( resource_config_type * ptr )
{
    dlog ("resource name: %s, resource type: %s, alarm type: %d \n", ptr->resource, ptr->type, ptr->alarm_type);
    AlarmFilter alarmFilter;

    build_entity_instance_id (ptr, alarmData.entity_instance_id);

    snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, ptr->alarm_id);
    snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, alarmData.entity_instance_id);

    int ret = rmon_fm_clear(&alarmFilter);
    if (ret == FM_ERR_OK)
    {
       ilog ("Cleared stale alarm %s for entity instance id: %s", alarmFilter.alarm_id, alarmFilter.entity_instance_id);
    }
    else if (ret == FM_ERR_ENTITY_NOT_FOUND)
    {
       dlog ("Stale alarm %s for entity instance id: %s was not found", alarmFilter.alarm_id, alarmFilter.entity_instance_id);
    }
    else
    {
       wlog ("Failed to clear stale alarm %s for entity instance id: %s error: %d", alarmFilter.alarm_id, alarmFilter.entity_instance_id, ret);
    }
}


/*****************************************************************************
 *
 * Name    : process_dynamic_fs_file
 *
 * Purpose : read the dynamic files directory and add the dynamic filesystem 
 * resources when the file is updated  
 *****************************************************************************/
void process_dynamic_fs_file()
{
    int index = 0;

    pthread_mutex_lock(&lock);
    modifyingResources = true;
    pthread_mutex_unlock(&lock);

    add_dynamic_fs_resource(true);

    pthread_mutex_lock(&lock);
    modifyingResources = false;
    pthread_mutex_unlock(&lock);

    /* deal with changes of dynamic file system enabled state */
    for (unsigned int i=0; i<fs_index.size(); i++)
    {
        index = fs_index.at(i);
        if ( strcmp(fs_state.at(i).c_str(), "disable") == 0 )
        {
            /* resource has been disabled, stop alarming on it */
            ilog("%s is no longer enabled\n", resource_config[index].resource);

            if ( resource_config[index].failed == true )
            {
                resource_config[index].alarm_status = ALARM_OFF;

                if ( _rmon_ctrl_ptr->clients > 0 )
                {
                   //send a clear degrade node
                   send_clear_msg(index);
                }

                // we need to clear the resource's alarm if there was any set for this resource
                clear_alarm_for_resource(&resource_config[index]);
            }
            else 
            { 
                /* There was no active alarm to clear */ 
                ilog("Setting resource: %s back to defaults \n", resource_config[index].resource);
                resource_config[index].alarm_status = ALARM_OFF;
                resource_config[index].failed = false;
                resource_config[index].alarm_raised = false;
                resource_config[index].count = 0 ;
                resource_config[index].sev = SEVERITY_CLEARED ;
                resource_config[index].stage = RMON_STAGE__START ;
            }
        }
        else if ( strcmp(fs_state.at(i).c_str(), "enabled") == 0 )
        { 
            // resource has been enabled
            if ( resource_config[index].alarm_status == ALARM_OFF )
            {
                /* Turn the resource checking back on if it was off */
                resource_config[index].alarm_status = ALARM_ON;

                //reset values
                resource_config[index].failed = false;
                resource_config[index].alarm_raised = false;
                resource_config[index].count = 0 ;
                resource_config[index].sev = SEVERITY_CLEARED ;
                resource_config[index].stage = RMON_STAGE__START ;

                rmon_alarming_init( &resource_config[index] );

                ilog("%s is now enabled \n", resource_config[index].resource);
                if (strcmp(resource_config[index].resource, CINDER_VOLUMES) == 0)
                {
                    virtual_space_usage_init(V_CINDER_THINPOOL_RESOURCE_NAME);
                }
                if (strcmp(resource_config[index].resource, NOVA_LOCAL) == 0)
                {
                    virtual_space_usage_init(V_NOVA_THINPOOL_RESOURCE_NAME);
                }
            }
            else // alarm aready on (enabled)
            {
                ilog("%s is already enabled \n", resource_config[index].resource);
            }
        }
        else
        {
           wlog("%s invalid dynamic file system state: %s \n", resource_config[index].resource, fs_state.at(i).c_str());
        }
    }
}

/*****************************************************************************
 *
 * Name    : process_static_fs_file
 *
 * Purpose : Reads in the list of static file systems for monitoring
 *
 *****************************************************************************/
void process_static_fs_file()
{
    FILE * pFile;
    vector<string> mounts;
    char buf[MAX_LEN];
    char resource[50];
    char type[50];
    char device[50];
    bool found = false;
    int enabled_resource = ALARM_ON; 
    string criticality = "critical";
    int absolute_thresholds[3] = {0};

    pFile = fopen (STATIC_FS_FILE , "r");
    if (pFile != NULL) {    
        ifstream fin( STATIC_FS_FILE );
        string line;

        while( getline( fin, line )) {
            /* process each line */
            mounts.push_back(line);
        }
        fclose(pFile);      


        for(std::vector<string>::iterator it = mounts.begin(); it != mounts.end(); ++it) 
        {
            string str = *it;
            snprintf(buf, MAX_LEN, str.c_str());
            sscanf(buf, "%49s %49s %49s %d %d %d", resource, device, type, &absolute_thresholds[0], &absolute_thresholds[1], &absolute_thresholds[2]);

            if (!found) 
            {
                if (fs_percent == PERCENT_USED)
                {
                    /* do not use the absolute thresholds */ 
                    memset(absolute_thresholds, 0, sizeof(absolute_thresholds));
                }
                /* add the resource */ 
                save_fs_resource ( resource, criticality, enabled_resource, fs_percent, absolute_thresholds, STATIC_ALARM, type, device, MOUNTED );
                calculate_fs_usage( &resource_config[_rmon_ctrl_ptr->resources - 1] ); 
            } 
        }
    } 
    else 
    {
        elog("Error, no static file system file present at: %s\n", STATIC_FS_FILE);
    }
}

/*****************************************************************************
 *
 * Name    : rmon_timer_handler
 *
 * Purpose : Looks up the timer ID and asserts the corresponding ringer
 *
 *****************************************************************************/
void rmon_timer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    UNUSED(sig); 
    UNUSED(uc);

    if ( !(*tid_ptr) )
    {
        // tlog ("Called with a NULL Timer ID\n");
        return ;
    }

    /* is event rmon timer */
    if ( *tid_ptr == rmonTimer_event.tid )
    {
        mtcTimer_stop_int_safe ( rmonTimer_event);
        rmonTimer_event.ring = true ;
    } 

    else if ( *tid_ptr == rmonTimer_pm.tid )
    {
        mtcTimer_stop_int_safe ( rmonTimer_pm);
        rmonTimer_pm.ring = true ;
    } 

    else if ( (is_controller()) && (*tid_ptr == rmonTimer_ntp.tid) )
    {
        mtcTimer_stop_int_safe ( rmonTimer_ntp);
        rmonTimer_ntp.ring = true ;
    }

    else 
    {
        bool found = false ;
        for ( int i = 0 ; i < _rmon_ctrl_ptr->resources ; i++ )
        {
            if ( *tid_ptr == rtimer[i].tid )
            {
                mtcTimer_stop_int_safe ( rtimer[i] );
                rtimer[i].ring = true ;
                found = true ;
                break ;
            }
        }
        if ( !found )
        {
            for ( int i = 0 ; i < _rmon_ctrl_ptr->thinmeta_resources ; i++ )
            {
                if ( *tid_ptr == thinmetatimer[i].tid )
                {
                    mtcTimer_stop_int_safe ( thinmetatimer[i] );
                    thinmetatimer[i].ring = true ;
                    found = true ;
                    break ;
                }
            }
        }
        if ( !found )
        {
            /* try and cleanup by stopping this unknown timer via its tid */
            mtcTimer_stop_tid_int_safe (tid_ptr);
        }
    }
}

/*****************************************************************************
 *
 * Name    : clear_ntp_alarms
 *
 * Purpose : Loop through each current alarms and deleted them if the server
 * is now reachable or the server no longer is assigned to ntpq
 *
 *****************************************************************************/
void clear_ntp_alarms(std::list<string> &non_reachable_ntp_servers, unsigned int alarm_count, SFmAlarmDataT *active_alarms, bool clear_major_alarm)
{
    dlog ("Total NTP alarm_count:%d", alarm_count);
    AlarmFilter alarmFilter;
    char alarm_to_search[FM_MAX_BUFFER_LENGTH];

    fm_alarm_id alarm_id;
    snprintf(alarm_id, FM_MAX_BUFFER_LENGTH, "%s", NTP_ALARM_ID);

    // clear the major alarms if required
    if (clear_major_alarm)
    {
        snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, "%s", NTP_ALARM_ID );
        snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s.ntp", _rmon_ctrl_ptr->my_hostname);

        int ret = rmon_fm_clear(&alarmFilter);
        if (ret != FM_ERR_OK)
        {
            if (ret != FM_ERR_ENTITY_NOT_FOUND)
            {
               wlog ("Failed to clear major alarm %s for entity instance id:%s error:%d", NTP_ALARM_ID, alarmFilter.entity_instance_id, ret);
            }
        }
        else
        {
            ilog ("Cleared major alarm %s for entity instance id:%s", NTP_ALARM_ID, alarmFilter.entity_instance_id);
        }
    }

    if (active_alarms == NULL)
    {
        elog ("Null pointer for active_alarms");
        return;
    }

    // clear minor alarms if required
    bool found;
    std::list<string>::iterator iter;
    std::list<string>::iterator iter_bad_list;

    // for each NTP alarms in the system see if it match any of the invalid NTP servers
    // if it does not match then the alarm must be removed since that NTP server
    // is no longer being monitored or is now valid
    for ( unsigned int i = 0; i < alarm_count; i++ )
    {
        if ( ((active_alarms+i)->severity) == FM_ALARM_SEVERITY_MINOR )
        {
            // Verify that this NTP minor alarm is still valid, This server could no longer exist or is now marked
            // reachable
            dlog ("Verify NTP minor alarm is still valid, entity instance id:%s",  (active_alarms+i)->entity_instance_id);

            found = false;

            // check for stale minor alarm
            for ( iter = non_reachable_ntp_servers.begin (); iter != non_reachable_ntp_servers.end (); iter++ )
            {
                // e.g. host=controller-0.ntp=102.111.2.2
                snprintf(alarm_to_search, FM_MAX_BUFFER_LENGTH,  "%s.ntp=%s", _rmon_ctrl_ptr->my_hostname, iter->c_str());

                dlog ("Non reachable NTP server to search %s", iter->c_str());

                if (strstr((active_alarms+i)->entity_instance_id, iter->c_str()) != NULL)
                {
                   // server is in non reachable list, do not clear it
                   found = true;
                   dlog ("Alarm is still valid %s", iter->c_str());
                   break;
                }
            }

            if (!found)
            {
                // lets clear it but only if it's this controller's alarm, it could be the peer controller's alarm
                if (strstr((active_alarms+i)->entity_instance_id, _rmon_ctrl_ptr->my_hostname) != NULL)
                {
                    snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, "%s", NTP_ALARM_ID);
                    snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s", (active_alarms+i)->entity_instance_id);

                    if (rmon_fm_clear(&alarmFilter) != FM_ERR_OK)
                    {
                        wlog ("Failed to clear minor alarm %s for entity instance id:%s", NTP_ALARM_ID, (active_alarms+i)->entity_instance_id);
                    }
                    else
                    {
                        ilog ("Cleared minor alarm %s for entity instance id:%s", NTP_ALARM_ID, (active_alarms+i)->entity_instance_id);
                    }
                }
            }
        }
    }
}

/*****************************************************************************
 *
 * Name    : ntp_query_results
 *
 * Purpose : Analyze the return code from script query_ntp_servers.sh.
 * Create alarms if the servers are non reachable, Clear alarms if they are
 * now reachable
 *
 *****************************************************************************/
void ntp_query_results (int ntp_query_status )
{
    dlog ("ntp_query_results ntp_query_status:%d", ntp_query_status);

    std::list<string> non_reachable_ntp_servers;

    // if no NTP servers are provisioned on the system, we still need to clear old NTP
    // alarms if there are any. But we do not need to read the tmp server file.
    if (ntp_query_status != NTP_NOT_PROVISIONED)
    {
        // read the temp file which contains a list of reachable and non reachable servers
        // this file is the output from the query_ntp_servers.sh script

        const char *server_info = "/tmp/ntpq_server_info";
        FILE *pFile;
        pFile = fopen(server_info, "r");
        if (pFile != NULL)
        {
            const char * delim = ";\n\r";
            char * ip;
            char line[500];

            int pos = 0;
            while ( memset(line, 0, sizeof(line)) && (fgets((char*) &line, sizeof(line), pFile) != NULL) )
            {
              // the first line in the tmp file is the reachable servers, the second is the non reachable servers
              if (pos == 1)
              {
                  for (ip = strtok (line, delim); ip;  ip = strtok (NULL, delim))
                  {
                    non_reachable_ntp_servers.push_back(ip);
                    dlog("Found non reachable NTP servers:%s\n", ip);
                  }
                  break;
              }
              pos++;
            }
            fclose(pFile);
        }
        else
        {
            elog("Failed to open file: %s\n", server_info);
            return;
        }
    }

    // retreive all the current NTP alarms
    int rc;
    unsigned int max_alarms=75;
    fm_alarm_id alarm_id;
    snprintf(alarm_id, FM_MAX_BUFFER_LENGTH, "%s", NTP_ALARM_ID);
    SFmAlarmDataT *active_alarms = (SFmAlarmDataT*) calloc (max_alarms, sizeof (SFmAlarmDataT));
    if (active_alarms == NULL)
    {
        elog ("Failed to allocate memory for NTP alarms");
        return;
    }

    int ret = fm_get_faults_by_id( &alarm_id, active_alarms, &max_alarms);
    if (!(ret == FM_ERR_OK || ret == FM_ERR_ENTITY_NOT_FOUND))
    {
       elog ("fm_get_faults_by_id failed trying to retreive all the NTP alarms, error:%d", ret);
       free(active_alarms);
       return;
    }

    // Clear alarms if required

    bool clear_major_alarm =  false;
    bool created_major_alarm = false;

    if ( ntp_query_status == NTP_NOT_PROVISIONED || ntp_query_status == NTP_SOME_REACHABLE || ntp_query_status == NTP_OK )
    {
       // We are going to clear the major alarm since there is at least one server selected or
       // no servers are provisioned
       clear_major_alarm = true;
    }

    // fm_get_faults_by_id returns the number of alarms found
    if (max_alarms != 0)
    {
       // verify if alarms need to cleared and clear them
       clear_ntp_alarms(non_reachable_ntp_servers, max_alarms, active_alarms, clear_major_alarm);
    }

    // There are no NTP servers provisioned so there is no alarms to raise
    if (ntp_query_status == NTP_NOT_PROVISIONED)
    {
       return;
    }

    // Raise alarms if required

    // Set up alarms data
    AlarmFilter alarmFilter;
    snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action), "Monitor and if condition persists, contact next level of support.");
    snprintf(alarmData.alarm_id, FM_MAX_BUFFER_LENGTH, "%s", NTP_ALARM_ID);
    strcpy(alarmData.uuid, "");
    snprintf(alarmData.entity_type_id, FM_MAX_BUFFER_LENGTH, "ntp");
    alarmData.alarm_state = FM_ALARM_STATE_SET;
    alarmData.alarm_type = FM_ALARM_COMM;
    alarmData.probable_cause = FM_ALARM_CAUSE_UNKNOWN;
    alarmData.timestamp = 0;
    alarmData.service_affecting = FM_FALSE;
    alarmData.suppression = FM_FALSE;

    // Here we raise the major alarm if required
    if (ntp_query_status == NTP_NONE_REACHABLE || ntp_query_status == NTP_SOME_REACHABLE_NONE_SELECTED)
    {
        wlog("NTP configuration does not contain any valid or reachable NTP servers");

        // Check if alarm is raised already
        snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s.ntp", _rmon_ctrl_ptr->my_hostname);

        bool found = false;
        for ( unsigned int i = 0; i < max_alarms; i++ )
        {
           if ( strncmp((active_alarms+i)->entity_instance_id, alarmFilter.entity_instance_id, sizeof((active_alarms+i)->entity_instance_id)) == 0 )
           {
               // Alarm already exist
               dlog("Alarm %s already raised for entity instance id:%s\n", NTP_ALARM_ID, alarmFilter.entity_instance_id);
               found = true;
               break;
           }
        }

        // Alarm does not exist so raise it
        if (!found && !created_major_alarm)
        {
            // Alarm does not exist so raise it
            alarmData.severity = FM_ALARM_SEVERITY_MAJOR;
            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), "NTP configuration does not contain any valid or reachable NTP servers.");
            snprintf(alarmData.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s", alarmFilter.entity_instance_id);

            rc = rmon_fm_set(&alarmData, NULL);
            if (rc == FM_ERR_OK )
            {
                ilog("Alarm %s created for entity instance id:%s \n", NTP_ALARM_ID, alarmData.entity_instance_id);
                created_major_alarm = true;
             }
             else
             {
                ilog("Failed to create alarm %s for entity instance id:%s error: %d \n", NTP_ALARM_ID, alarmData.entity_instance_id, (int)rc);
             }
        }
    }

    // Here were raise alarms for individual servers
    if (ntp_query_status != NTP_OK)
    {
        wlog("Some or all of the NTP servers are not reachable");
        std::list<string>::iterator iter;
        alarmData.severity = FM_ALARM_SEVERITY_MINOR;

        // Loop through all the non reachable NTP servers
        // Check to see if an alarms is lready raised for the server.
        // If we do not find an alarm for the server then we raise it
        for ( iter = non_reachable_ntp_servers.begin (); iter != non_reachable_ntp_servers.end (); iter++ )
        {
            bool found = false;

            // Build the alarm entity instatance id
            snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s.ntp=%s", _rmon_ctrl_ptr->my_hostname, iter->c_str());

            dlog("Search alarms for entity instance id:%s \n", alarmFilter.entity_instance_id);
            for ( unsigned int i = 0; i < max_alarms; i++ )
            {
                if ( strncmp((active_alarms+i)->entity_instance_id, alarmFilter.entity_instance_id, sizeof((active_alarms+i)->entity_instance_id)) == 0 )
                {
                    dlog("Alarm %s already raised for entity instance id:%s\n", NTP_ALARM_ID, alarmFilter.entity_instance_id);
                    found = true;
                    break;
                }
            }

            // If the NTP alarm was not found then raise one for this NTP server
            if (!found)
            {

                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), "NTP address %s is not a valid or a reachable NTP server.", iter->c_str() );
                snprintf(alarmData.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s", alarmFilter.entity_instance_id);

                rc = rmon_fm_set(&alarmData, NULL);
                if (rc == FM_ERR_OK )
                {
                    ilog("Alarm %s created for entity instance id:%s \n", NTP_ALARM_ID, alarmData.entity_instance_id);
                }
                else
                {
                    ilog("Failed to create alarm %s for entity instance id:%s error:%d \n", NTP_ALARM_ID, alarmData.entity_instance_id, (int)rc);
                }
            }
        }
    }

    free(active_alarms);
    return;
}

/*****************************************************************************
 *
 * Name    : query_ntp_servers
 *
 * Purpose : execute script query_ntp_servers.sh which run the "ntpq -np"
 * which query the healths of the NTP servers. The script will return a
 * status code and also create a temporate file which will save the list
 * of reachable and non reachable NTP servers. This temp file is required
 * to generate proper alarms
 *
 *****************************************************************************/
int query_ntp_servers ( )
{
    pid_t child_pid;

    dlog ("Main Pid:%d \n", getpid() );

    ntp_child_pid = child_pid = fork ();
    if (child_pid == 0)
    {
        dlog ("Child Pid:%d \n", getpid() );

        char* argv[] = {(char*)NTPQ_QUERY_SCRIPT, NULL};
        char cmd[MAX_FILE_SIZE] ;
        memset  (cmd,0,MAX_FILE_SIZE);

        snprintf ( &cmd[0], MAX_FILE_SIZE, "%s/%s", RMON_FILES_DIR, NTPQ_QUERY_SCRIPT );

        bool close_file_descriptors = true ;
        if ( setup_child ( close_file_descriptors ) != PASS )
        {
            exit(NTP_ERROR);
        }

        /* Set child to ignore child exit */
        signal (SIGCHLD, SIG_DFL);

        /* Setup the exec arguement */
        int res = execv(cmd, argv);
        elog ( "Failed to run %s return code:%d error:%s\n", cmd, res, strerror(errno) );
        exit (NTP_ERROR);
    }

    if ( child_pid == -1 )
    {
        elog ("Fork failed (%s)\n", strerror(errno));

        /* TODO: Consider making this a critical fault
         * after 100 retries.
         * All possibilities based on man page are
         * due to resource limitations and if that does
         * not resolve in 100 retries then ip probably will never.
         **/
        return (FAIL);
    }

    return (PASS);
}

/*****************************************************************************
 *
 * Name    : rmonHdlr_ceilometer_handler
 *
 * Purpose : Handles the ceilometer sample create response message
 *
 *****************************************************************************/
void rmonHdlr_ceilometer_handler( struct evhttp_request *req, void *arg )
{
    if ( !req )
    {
        elog (" Request Timeout\n");
        ceilometerEvent.status = FAIL_TIMEOUT;
        goto _ceilometer_handler_done ;
    }

    ceilometerEvent.status = rmonHttpUtil_status(ceilometerEvent);
    if ( ceilometerEvent.status != PASS )
    {
        elog ("ceilometer HTTP request Failed (%d)\n", ceilometerEvent.status);
        rmonHttpUtil_get_response(ceilometerEvent);
        goto _ceilometer_handler_done ;
    }

_ceilometer_handler_done:
    event_base_loopbreak((struct event_base *)arg);
}

/*****************************************************************************
 *
 * Name    : generate_ceilometer_pm 
 *
 * Purpose : Generate ceilometer PMs through the REST API 
 *
 *****************************************************************************/
void generate_ceilometer_pm ( string r_id, string m_id, string m_type, 
                              string m_unit, string m_volume, 
                              string m_metadata )
{
    int rc = PASS;
    daemon_config_type * cfg_ptr = daemon_get_cfg_ptr();
    string command_path="";
    string host_ip = cfg_ptr->keystone_auth_host;
    int port = cfg_ptr->ceilometer_port;
    int count = 0;

    rmonHttpUtil_libEvent_init ( &ceilometerEvent, CEILOMETER_EVENT_SIG, host_ip, port);

    ceilometerEvent.address.append("/v2/meters/");
    ceilometerEvent.address.append(m_id);

    ceilometerEvent.user_agent = "ceilometerclient.openstack.common.apiclient";

    ceilometerEvent.payload = "[{";
    ceilometerEvent.payload.append("\"resource_id\":\"");
    ceilometerEvent.payload.append(r_id);
    ceilometerEvent.payload.append("\",\"counter_name\":\"");
    ceilometerEvent.payload.append(m_id);
    ceilometerEvent.payload.append("\",\"counter_type\":\"");
    ceilometerEvent.payload.append(m_type);
    ceilometerEvent.payload.append("\",\"counter_unit\":\"");
    ceilometerEvent.payload.append(m_unit);
    ceilometerEvent.payload.append("\",\"counter_volume\":\"");
    ceilometerEvent.payload.append(m_volume);
    ceilometerEvent.payload.append("\",\"resource_metadata\":");
    // the resource metadata is dictionary of key-value pairs
    ceilometerEvent.payload.append(m_metadata);
    ceilometerEvent.payload.append("}]");
    dlog ("Payload is : %s\n", ceilometerEvent.payload.c_str());

    rc = rmonHttpUtil_api_request (CEILOMETER_SAMPLE_CREATE, ceilometerEvent, command_path);
    do
    {
        if ( rc != PASS )
        {
            count++;
            wlog ("ceilometer failed request (%d) ... retrying (%d)\n", rc, count);
        }
        rmonHttpUtil_log_event (ceilometerEvent);

    } while ( ( rc!=PASS ) && ( count < REST_API_RETRY_COUNT ) );

    if ( rc!= PASS )
    {
        elog ("ceilometer sample create Failed (%d) (cnt:%d)\n", rc, count);
    }
}

void clear_rmon_api_counts ( registered_clients * ptr )
{
    if ( ptr->b2b_miss_count > ptr->b2b_miss_peak )
    {
        ptr->b2b_miss_peak = ptr->b2b_miss_count ;
    }

    if ( ptr->mesg_err_cnt > ptr->mesg_err_peak )
    {
        ptr->mesg_err_peak = ptr->mesg_err_cnt ;
    }
    ptr->b2b_miss_count = 0 ;
    ptr->send_err_cnt   = 0 ;
    ptr->recv_err_cnt   = 0 ;
    ptr->mesg_err_cnt   = 0 ;
}

/*****************************************************************************
 *
 * Name    : _space_to_underscore
 *
 * Purpose : Converts spaces in a string to underscores 
 * *****************************************************************************/
void _space_to_underscore (string & str )
{
    char space = ' ';
    for(unsigned int i = 0; i < str.size(); i++)
    {
        if(str[i] == space)
        { 
           str[i] = '_';
        }
    }
}

/*****************************************************************************
 *
 * Name    : set_alarm_defaults
 *
 * Purpose : Set the defaults for the fm alarms 
 * *****************************************************************************/
void set_alarm_defaults ( resource_config_type * ptr )
{
    strcpy(alarmData.uuid, "");
    /* common data for all alarm messages */ 
    snprintf(alarmData.entity_type_id, FM_MAX_BUFFER_LENGTH, "system.host");

    build_entity_instance_id (ptr, alarmData.entity_instance_id);

    alarmData.alarm_state =  FM_ALARM_STATE_SET;
    alarmData.alarm_type =  FM_ALARM_OPERATIONAL;
    alarmData.probable_cause =  FM_ALARM_THRESHOLD_CROSSED;
    alarmData.timestamp = 0; 
    alarmData.service_affecting = FM_FALSE;
    alarmData.suppression = FM_TRUE;     
    snprintf(alarmData.alarm_id, FM_MAX_BUFFER_LENGTH, ptr->alarm_id);

}

/*****************************************************************************
 *
 * Name    : resource_handler
 *
 * Purpose : Handle the failed resources and raise alarms through
 * the FM API as well as calling a function to notify registered clients 
 *****************************************************************************/
int resource_handler ( resource_config_type * ptr )
{
    int rc = RETRY ;
    AlarmFilter alarmFilter;
    string err_res_name(ptr->resource);
    _space_to_underscore(err_res_name);

    if ( ptr->stage < RMON_STAGE__STAGES )
    {
        dlog2 ("%s %s Stage %d\n", ptr->resource, rmonStages_str[ptr->stage], ptr->stage );
    }
    else
    {
        resourceStageChange ( ptr, RMON_STAGE__FINISH ); 
    }

    switch ( ptr->stage )
    {
        case RMON_STAGE__START:
            {
                dlog ( "%s failed:%d set_cnt:%d debounce_cnt:%d\n", 
                        ptr->resource,  
                        ptr->failed,
                        ptr->count, 
                        ptr->debounce_cnt);
                break ;
            }
        case RMON_STAGE__MANAGE:
            {
                /* send messages to maintnance in thresholds are crossed */
                if (ptr->alarm_status == ALARM_ON)
                {
                    /* set up the fm api alarm defaults */
                    set_alarm_defaults( ptr );
                    if ( strcmp(ptr->resource, MEMORY_RESOURCE_NAME) == 0 )
                    {
                        snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action), 
                                 "Monitor and if condition persists, contact next level of support; may require additional memory on Host.");
                    }
                    else if ( strcmp(ptr->resource, INSTANCE_RESOURCE_NAME) == 0 )
                    {
                        snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action), 
                                 "Check Management and Infrastructure Networks and Controller or Storage Nodes.");
                    }
                    else 
                    {
                      if ((ptr->type != NULL) && (strcmp(ptr->type, "lvg") == 0 ))
                        {
                          snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action), 
                                   "Monitor and if condition persists, consider adding additional physical volumes to the volume group.");
                        }
                      else
                        {
                        snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action), 
                                "Monitor and if condition persists, contact next level of support.");
                        }
                    }

                    if ( ptr->sev == SEVERITY_MINOR )
                    { 
                        alarmData.severity = FM_ALARM_SEVERITY_MINOR;

                        if ( ptr->percent == PERCENT_USED ) {

                            if ( ptr->alarm_type == STANDARD_ALARM )
                            {
                                ilog ("%s threshold exceeded; threshold: %d%%, actual: %.2f%%. \n", 
                                       ptr->resource, ptr->minor_threshold, ptr->resource_value);
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                         "%s threshold exceeded; threshold: %u%%, actual: %.2f%%.",
                                         ptr->resource, ptr->minor_threshold, ptr->resource_value);    
                            } 
                            else {
                                ilog ("Filesystem threshold exceeded; threshold: %d%%, actual: %.2f%%. \n",
                                       ptr->minor_threshold, ptr->resource_value);
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                         "Filesystem exceeded; threshold: %u%%, actual: %.2f%%.",
                                          ptr->minor_threshold, ptr->resource_value);  
                            }
                        } else {
                           if ( ptr->alarm_type == STANDARD_ALARM )
                           {
                                ilog ("%s threshold exceeded; threshold: %dMB, remaining value: %.2fMB. \n", 
                                        ptr->resource, ptr->minor_threshold_abs_node0, ptr->resource_value); 
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                         "%s threshold exceeded; threshold: %uMB, remaining value: %.2fMB.",
                                         ptr->resource, ptr->minor_threshold_abs_node0, ptr->resource_value);  
                            } else {
                                ilog ("Filesystem threshold exceeded; threshold: %dMB, remaining value: %.2fMB. \n",
                                      ptr->minor_threshold_abs_node0, ptr->resource_value); 
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                         "Filesystem threshold exceeded; threshold: %uMB, remaining value: %.2fMB.",
                                          ptr->minor_threshold_abs_node0, ptr->resource_value);  
                            }
                        }
                        snprintf(ptr->errorMsg, sizeof(ptr->errorMsg), 
                                 "%s minor_threshold_set", err_res_name.c_str());
                    }
                    else if ( ptr->sev == SEVERITY_MAJOR )
                    {
                        alarmData.severity = FM_ALARM_SEVERITY_MAJOR;

                        if (strcmp(ptr->resource, INSTANCE_RESOURCE_NAME) != 0) 
                        { 
                            if (ptr->percent == PERCENT_USED){
                                if ( ptr->alarm_type == STANDARD_ALARM )
                                {
                                    ilog ("%s threshold exceeded; threshold: %d%%, actual: %.2f%%. \n", 
                                           ptr->resource, ptr->major_threshold, ptr->resource_value);
                                    snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                             "%s threshold exceeded; threshold: %u%%, actual: %.2f%%.",
                                             ptr->resource, ptr->major_threshold, ptr->resource_value);    
                                } 
                                else {
                                    ilog ("Filesystem threshold exceeded; threshold: %d%%, actual: %.2f%%. \n",
                                           ptr->major_threshold, ptr->resource_value);
                                    snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                             "Filesystem threshold exceeded; threshold: %u%%, actual: %.2f%%.",
                                              ptr->major_threshold, ptr->resource_value);  
                                }
                            } else {
                                if ( ptr->alarm_type == STANDARD_ALARM )
                                {
                                    ilog ("%s threshold exceeded; threshold: %dMB, remaining value: %.2fMB. \n", 
                                           ptr->resource, ptr->major_threshold_abs_node0, ptr->resource_value); 
                                    snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                             "%s threshold exceeded; threshold: %uMB, remaining value: %.2fMB.",
                                    ptr->resource, ptr->major_threshold_abs_node0, ptr->resource_value);  
                                } else {
                                    ilog ("Filesystem threshold exceeded; threshold: %dMB, remaining value: %.2fMB. \n",
                                           ptr->major_threshold_abs_node0, ptr->resource_value); 
                                    snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                             "Filesystem threshold exceeded; threshold: %uMB, remaining value: %.2fMB.",
                                              ptr->major_threshold_abs_node0, ptr->resource_value);  
                                }
                            }
                        }
                        else if (strcmp(ptr->resource, INSTANCE_RESOURCE_NAME) == 0) 
                        { 
                            /* instance alarming is a special case of alarm */ 
                            wlog ("No access to remote VM volumes.\n");
                            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                     "No access to remote VM volumes.");  
                        }

                        if ( ptr->res_type == RESOURCE_TYPE__FILESYSTEM_USAGE )
                        {
                            snprintf(ptr->errorMsg, sizeof(ptr->errorMsg),
                                    "%s %s",err_res_name.c_str(), DEGRADE_CLEAR_MSG );
                        }
                        else
                        {
                            snprintf(ptr->errorMsg, sizeof(ptr->errorMsg),
                                     "%s major_threshold_set",err_res_name.c_str());
                        }
                    }
                    else if ( ptr->sev == SEVERITY_CRITICAL )
                    {
                        alarmData.severity = FM_ALARM_SEVERITY_CRITICAL;

                        if (ptr->percent == PERCENT_USED){
                            if ( ptr->alarm_type == STANDARD_ALARM )
                            {
                                ilog ("%s threshold exceeded; threshold: %d%%, actual: %.2f%%. \n",
                                      ptr->resource, ptr->critical_threshold, ptr->resource_value);
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                         "%s threshold exceeded; threshold: %u%%, actual: %.2f%%.",
                                         ptr->resource, ptr->critical_threshold, ptr->resource_value);
                            }
                            else {
                                ilog ("Filesystem threshold exceeded; threshold: %d%%, actual: %.2f%%. \n",
                                      ptr->critical_threshold, ptr->resource_value);
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                         "Filesystem threshold exceeded; threshold: %u%%, actual: %.2f%%.",
                                         ptr->critical_threshold, ptr->resource_value);
                                }
                        } else {
                            if ( ptr->alarm_type == STANDARD_ALARM )
                            {
                                ilog ("%s threshold exceeded; threshold: %dMB, remaining value: %.2fMB. \n",
                                      ptr->resource, ptr->critical_threshold_abs_node0, ptr->resource_value);
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                         "%s threshold exceeded; threshold: %uMB, remaining value: %.2fMB.",
                                         ptr->resource, ptr->critical_threshold_abs_node0, ptr->resource_value);
                            } else {
                                ilog ("Filesystem threshold exceeded; threshold: %dMB, remaining value: %.2fMB. \n",
                                      ptr->critical_threshold_abs_node0, ptr->resource_value);
                                snprintf(alarmData.reason_text, sizeof(alarmData.reason_text),
                                        "Filesystem threshold exceeded; threshold: %uMB, remaining value: %.2fMB.",
                                        ptr->critical_threshold_abs_node0, ptr->resource_value);
                            }
                        }
                        snprintf(ptr->errorMsg, sizeof(ptr->errorMsg),
                                 "%s major_threshold_set",err_res_name.c_str());
                    }

                    rc = rmon_fm_set(&alarmData, NULL);
                    if (rc == FM_ERR_OK ) {
                        ilog("%s: %s alarm\n",
                                  ptr->resource,
                                  FmAlarmSeverity_to_string(alarmData.severity).c_str());
                        ptr->alarm_raised = true;
                     } else {
                        ilog("%s: %s alarm failed (rc:%d)\n",
                                  ptr->resource,
                                  FmAlarmSeverity_to_string(alarmData.severity).c_str(),
                                  (int)rc);
                     }

                    if (ptr->alarm_raised)
                    {
                        if ((_rmon_ctrl_ptr->clients > 0) && (ptr->failed_send < MAX_FAIL_SEND))
                        {
                            /* If degrade debounce is non-zero then this
                             * alarm condition is candidate for host degrade */
                            if (ptr->debounce)
                            {
                                if ( rmon_send_request ( ptr, _rmon_ctrl_ptr->clients ) != PASS )
                                {
                                    ptr->failed_send++;
                                    wlog ("%s request send failed (count:%d)\n",
                                              ptr->resource,
                                              ptr->failed_send );
                                }
                                else
                                {
                                    ptr->failed_send = 0;
                                }
                            }
                        }
                        else
                        {
                            ptr->failed_send = 0;
                        }
                        resourceStageChange ( ptr, RMON_STAGE__MONITOR_WAIT );
                    }
                }
                else {
                    resourceStageChange ( ptr, RMON_STAGE__FINISH );
                }

                break;
            }

        case RMON_STAGE__IGNORE:
            {

                //nothing to do here, go to the finished stage
                resourceStageChange ( ptr, RMON_STAGE__FINISH );

                break ;
            }

        case RMON_STAGE__MONITOR_WAIT:
            {
                if ((_rmon_ctrl_ptr->clients > 0) && (ptr->failed_send < MAX_FAIL_SEND) && (ptr->failed_send > 0))
                {
                    if ( rmon_send_request ( ptr, _rmon_ctrl_ptr->clients ) != PASS )
                    {
                        wlog ("%s  request send failed \n", ptr->resource);
                        ptr->failed_send++;
                    }
                    else
                    {
                        ptr->failed_send = 0;
                    }
                }
                break;
            }

        case RMON_STAGE__FINISH:
            {
                if ((ptr->alarm_status == ALARM_ON) && (ptr->alarm_raised))
                {
                    snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, ptr->alarm_id);

                    build_entity_instance_id (ptr, alarmData.entity_instance_id);

                    snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, alarmData.entity_instance_id);
                    ilog ("%s alarm clear\n", ptr->resource );

                    /* clear the alarm */
                    EFmErrorT ret = rmon_fm_clear(&alarmFilter);
                    if (( ret == FM_ERR_OK ) || ( ret == FM_ERR_ENTITY_NOT_FOUND ))
                    {
                        if (ret == FM_ERR_ENTITY_NOT_FOUND)
                        {
                            dlog ("%s alarm clear failed, entity '%s' not found",
                                      ptr->resource, alarmData.entity_instance_id);
                        }

                        snprintf(ptr->errorMsg, sizeof(ptr->errorMsg), "%s cleared_alarms_for_resource", err_res_name.c_str());
                        if ( (_rmon_ctrl_ptr->clients > 0) && ( ptr->failed_send < MAX_FAIL_SEND ) && (ret == FM_ERR_OK) )
                        {
                            while (( rmon_send_request ( ptr, _rmon_ctrl_ptr->clients ) != PASS ) &&
                                  ( ptr->failed_send < MAX_FAIL_SEND ))
                            {
                                wlog ("%s request send failed \n", ptr->resource);
                                ptr->failed_send++;
                            }

                            ptr->alarm_raised = false;
                            ptr->failed_send = 0;
                            ptr->failed = false ;
                            ptr->count = 0 ;
                            ptr->sev = SEVERITY_CLEARED ;
                            ptr->stage = RMON_STAGE__START ;
                        }
                        else
                        {
                            ptr->alarm_raised = false;
                            ptr->failed_send = 0;
                            ptr->failed = false ;
                            ptr->count = 0 ;
                            ptr->sev = SEVERITY_CLEARED ;
                            ptr->stage = RMON_STAGE__START ;
                        }
                    }
                    else
                    {
                        wlog("%s alarm clear failed, entity '%s' (rc:%d)\n",
                                 ptr->resource,
                                 alarmData.entity_instance_id,
                                 ret);
                    }
                }
                else
                {
                    ptr->alarm_raised = false;
                    ptr->failed_send = 0;
                    ptr->failed = false ;
                    ptr->count = 0 ;
                    ptr->sev = SEVERITY_CLEARED ;
                    ptr->stage = RMON_STAGE__START ;
                }
                rc = PASS ;
                break ;
            }
        default:
            {
                slog ("%s Invalid stage (%d)\n", ptr->resource, ptr->stage );

                /* Default to finish for invalid case.
                 * If there is an issue then it will be detected */
                resourceStageChange ( ptr, RMON_STAGE__FINISH );
            }
    }
    return rc;
}

/*****************************************************************************
 *
 * Name    : process_failures
 *
 * Purpose : Check whether a percentage resource is to be failed or a failure 
 * threshold is to be cleared by the resource_handler 
 *
 *****************************************************************************/
void process_failures ( resource_config_type * ptr )
{
    if (ptr->stage == RMON_STAGE__INIT)
    {
        /* first time after restart/reboot, clear the alarm if the first reading is good */
        resourceStageChange ( ptr, RMON_STAGE__START );
        if (ptr->resource_value < ptr->minor_threshold)
        {
            // assuming we left as alarm on last time
            ptr->alarm_status = ALARM_ON;
            ptr->alarm_raised = true;
            ptr->failed = true;
            ilog("%s Setting the state to FINISH\n", ptr->resource);
            resourceStageChange ( ptr, RMON_STAGE__FINISH );
        }
        // Now we start counting as normal ...
    }
    else 
    {
        if (ptr->failed)
        {
            /* If the resource is already failed, check to see if it is to be cleared */
            if ((( ptr->sev == SEVERITY_MINOR) && ( ptr->resource_value < ptr->minor_threshold )) ||
                    (( ptr->sev == SEVERITY_MAJOR) && ( ptr->resource_value < ptr->major_threshold )) ||
                    (( ptr->sev == SEVERITY_CRITICAL) && ( ptr->resource_value < ptr->critical_threshold )))
            {
                if (ptr->count > ptr->num_tries)
                    ptr->count = ptr->num_tries;

                if (ptr->count > 0)
                    ptr->count--;

                if (ptr->count == 0) {
                    ptr->sev = SEVERITY_CLEARED;
                    ilog("%s Setting the state to FINISH\n", ptr->resource);
                    resourceStageChange ( ptr, RMON_STAGE__FINISH );
                }
            }
            else
            {
                /* While in failed state, the resource usage must sustain normal level
                 * num_tries number of times before an alarm can be cleared. Keep incrementing the counter
	             * as it will be set to num_tries in the above block as soon as resource usage returns to
                 * normal level.*/
                ptr->count++;

                // rmon needs to send degrade assert message periodically as the
                // condition might be cleared by maintenance over controller swact.
                //
                // added meaning to the debounce config setting.
                //            must be non-zero to degrade the host.
                if ((ptr->alarm_raised) && (ptr->debounce) &&
                    (_rmon_ctrl_ptr->clients > 0))
                {
                    if ( rmon_send_request ( ptr, _rmon_ctrl_ptr->clients ) != PASS )
                    {
                        ptr->failed_send++ ;
                        wlog ("%s request send failed (count:%d)\n",
                                  ptr->resource,
                                  ptr->failed_send);
                    }
                    else
                    {
                        mlog ("%s rmon_send_request ok\n", ptr->resource );
                        ptr->failed_send = 0 ;
                    }
                }
                else
                {
                     /* typical path for resources that
                      *  - do not degrade host
                      *  - do not raise alarms */
                     dlog ("%s: alarm:%d debounce:%d clients:%d\n",
                                ptr->resource,
                               (ptr->alarm_raised),
                               (ptr->debounce),
                               (_rmon_ctrl_ptr->clients));
                }
            }
        }
    }

    /* Check to see if a resource is over the failure thresholds for: minor, major and critical failures */
    if (( ptr->resource_value >= ptr->minor_threshold ) &&
            ( ptr->resource_value < ptr->major_threshold )
            && (ptr->sev != SEVERITY_MINOR))
    {
        ptr->count++;
        if ( ptr->count >= ptr->num_tries) {
            ptr->failed = true;
            ptr->sev = SEVERITY_MINOR;
            resourceStageChange ( ptr, RMON_STAGE__MANAGE);             
        }
    }

    else if (( ptr->resource_value >= ptr->major_threshold ) && 
            ( ptr->resource_value < ptr->critical_threshold ) 
            && (ptr->sev != SEVERITY_MAJOR))
    {
        ptr->count++;
        if ( ptr->count >= ptr->num_tries){
            ptr->failed = true;
            ptr->sev = SEVERITY_MAJOR;  
            resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
        }
    }
    else if (( ptr->resource_value >= ptr->critical_threshold )&&
            (ptr->sev != SEVERITY_CRITICAL))
    {
        ptr->count++;
        if (ptr->count >= ptr->num_tries){
            ptr->failed = true;
            ptr->sev = SEVERITY_CRITICAL;  
            resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
        }
    }        
    else
    {
	/* if the host experienced a resource blip in the previous audit run and usage
	 * is now back at the normal level, decrement the count.*/
	if ((!ptr->failed) && (ptr->count > 0)){
            ptr->count--;
            dlog("Resource %s is back at the normal level, count is set to %d", ptr->resource, ptr->count);
        }
    }
}

/*****************************************************************************
 *
 * Name    : process_failures_absolute
 *
 * Purpose : Check whether an absolute resource is to be failed or a 
 * failure threshold is to be cleared by the resource_handler 
 *
 *****************************************************************************/
void process_failures_absolute ( resource_config_type * ptr )
{
    int node = 0;

    if (strcmp(ptr->resource,"processor_node1") == 0)
    {
        /* per node memory checking is enabled */
        node = 1;
    } 

    if (ptr->failed) {
        /* If the resource is already failed, check to see if it is to be cleared */
        if (node == 0) {

            if ((( ptr->sev == SEVERITY_MINOR) && ( ptr->resource_value > ptr->minor_threshold_abs_node0 )) || 
                    (( ptr->sev == SEVERITY_MAJOR) && ( ptr->resource_value > ptr->major_threshold_abs_node0 )) ||
                    (( ptr->sev == SEVERITY_CRITICAL) && ( ptr->resource_value > ptr->critical_threshold_abs_node0 )))
            {
                if (ptr->count > ptr->num_tries)
                    ptr->count = ptr->num_tries;
                if (ptr->count > 0)
                    ptr->count--;

                if (ptr->count == 0) {                           
                    ptr->sev = SEVERITY_CLEARED;
                    resourceStageChange ( ptr, RMON_STAGE__FINISH ); 
                }
            } 
            else
	    {
		/* While in failed state, the resource usage must sustain normal level
		 * num_tries number of times before an alarm can be cleared. Keep incrementing the counter
		 * as it will be set to num_tries in the above block as soon as resource usage returns to
		 * normal level.*/
		ptr->count++;
            }
        }
        else {

            if ((( ptr->sev == SEVERITY_MINOR) && ( ptr->resource_value > ptr->minor_threshold_abs_node1 )) || 
                    (( ptr->sev == SEVERITY_MAJOR) && ( ptr->resource_value > ptr->major_threshold_abs_node1 )) ||
                    (( ptr->sev == SEVERITY_CRITICAL) && ( ptr->resource_value > ptr->critical_threshold_abs_node1 )))
            {   
                if (ptr->count > ptr->num_tries)
                    ptr->count = ptr->num_tries;
                if (ptr->count > 0)
                    ptr->count--;

                if (ptr->count == 0) {
                    ptr->sev = SEVERITY_CLEARED;
                    resourceStageChange ( ptr, RMON_STAGE__FINISH ); 
                }
            } 
 	    else
	    {
		/* While in failed state, the resource usage must sustain normal level
		 * num_tries number of times before an alarm can be cleared. Keep incrementing the counter
		 * as it will be set to num_tries in the above block as soon as resource usage returns to
		 * normal level.*/
		ptr->count++;
            }
        }
    }

    if (node == 0) {
        /* Check to see if a resource is over the failure thresholds for: minor, major and critical failures node 0  */ 
        if (( ptr->resource_value <= ptr->minor_threshold_abs_node0 ) &&
                ( ptr->resource_value > ptr->major_threshold_abs_node0 ) &&
                (ptr->sev != SEVERITY_MINOR))
        {
            ptr->count++;
            if ( ptr->count >= ptr->num_tries){
                ptr->failed = true;
                ptr->sev = SEVERITY_MINOR;
                resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
            }
        } 

        else if (( ptr->resource_value <= ptr->major_threshold_abs_node0 ) && 
                ( ptr->resource_value > ptr->critical_threshold_abs_node0 ) &&
                (ptr->sev != SEVERITY_MAJOR))
        {
            ptr->count++;
            if ( ptr->count >= ptr->num_tries){
                ptr->failed = true;
                ptr->sev = SEVERITY_MAJOR;  
                resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
            }
        }   
        else if (( ptr->resource_value < ptr->critical_threshold_abs_node0 )&&
                (ptr->sev != SEVERITY_CRITICAL))
        {
            ptr->count++;
            if (ptr->count >= ptr->num_tries){
                ptr->failed = true;
                ptr->sev = SEVERITY_CRITICAL;  
                resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
            }
        }  
	else
	{
	    /* if the host experienced a resource blip in the previous audit run and usage
	     * is now back at the normal level, decrement the count.*/
	    if ((!ptr->failed) && (ptr->count > 0)){
                ptr->count--;
                dlog("Resource %s is back at the normal level, count is set to %d", ptr->resource, ptr->count);
            }
        }
    } else {

        /* Check to see if a resource is over the failure thresholds for: minor, major and critical failures node 1 */ 
        if (( ptr->resource_value <= ptr->minor_threshold_abs_node1 ) && 
                ( ptr->resource_value > ptr->major_threshold_abs_node1 ) && 
                (ptr->sev != SEVERITY_MINOR))
        {   
            ptr->count++;
            if ( ptr->count >= ptr->num_tries){               
                ptr->failed = true;
                ptr->sev = SEVERITY_MINOR;
                resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
            }
        }          
        else if (( ptr->resource_value <= ptr->major_threshold_abs_node1 ) && 
                ( ptr->resource_value > ptr->critical_threshold_abs_node1 ) && 
                (ptr->sev != SEVERITY_MAJOR))
        {
            ptr->count++;
            if ( ptr->count >= ptr->num_tries){       
                ptr->failed = true;
                ptr->sev = SEVERITY_MAJOR;  
                resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
            }  
        }   
        else if (( ptr->resource_value < ptr->critical_threshold_abs_node1 )&&
                (ptr->sev != SEVERITY_CRITICAL))
        {      
            ptr->count++;
            if (ptr->count >= ptr->num_tries){          
                ptr->failed = true;
                ptr->sev = SEVERITY_CRITICAL;  
                resourceStageChange ( ptr, RMON_STAGE__MANAGE);            
            }   
        }   
	else
	{
	    /* if the host experienced a resource blip in the previous audit run and usage
	     * is now back at the normal level, decrement the count.*/
	    if ((!ptr->failed) && (ptr->count > 0)){
                ptr->count--;
                dlog("Resource %s is back at the normal level, count is set to %d", ptr->resource, ptr->count);
            }
        }
    }
}

void update_total_clients (int total_clients) 
{
    _rmon_ctrl_ptr->clients = total_clients; 
}

void add_registered_client (registered_clients client)
{

    registered_clt[_rmon_ctrl_ptr->clients] = client; 
    ilog("added registered client: %s \n", client.client_name);
}

/*****************************************************************************
 *
 * Name    : add_fs_resource
 *
 * Purpose : Add a dynamic or static fs resource by reading 
 * the: /etc/rmonfiles.d/dynamic.conf file 
 *****************************************************************************/
void add_fs_resource ( int resource_index, int criticality_index, int enabled, 
                       int percent, int abs_values[3], int alarm_type, 
                       int types_index, int devices_index, int mounted )
{ 
    int fs_resource_index;
    get_resource_index( FS_RESOURCE_NAME, &fs_resource_index );
    
    int i = _rmon_ctrl_ptr->resources;

    if (i >= MAX_RESOURCES) {
        wlog ("Cannot Monitor more than %d resources\n", MAX_RESOURCES ); 
    }
    else {

        resource_config[i].resource = dynamic_resource.at(resource_index).c_str();  
        resource_config[i].severity = criticality_resource.at(criticality_index).c_str();
        resource_config[i].type = types.at(types_index).c_str();
        resource_config[i].device = devices.at(devices_index).c_str();
        resource_config[i].critical_threshold = UNUSED_CRITICAL; // initialization
        resource_config[i].critical_threshold_abs_node0 = UNUSED_CRITICAL_ABS_NODE0;

        resource_config[i].num_tries = DEFAULT_NUM_TRIES;
        resource_config[i].alarm_status = enabled;
        resource_config[i].percent = percent;
        resource_config[i].mounted = mounted;
        resource_config[i].alarm_type = alarm_type;
        resource_config[i].debounce = resource_config[fs_resource_index].debounce;

        // percentage based threshold measure
        switch (percent) {
            case PERCENT_USED:
                if (abs_values[0] == 0) {
                    // if this is a static mounted file system resource
                    // then use common threshold values provided for the
                    // File System Resource
                    if ( (alarm_type == STATIC_ALARM) && (mounted == MOUNTED) ) {
                        resource_config[i].minor_threshold =
                            resource_config[fs_resource_index].minor_threshold;

                        resource_config[i].major_threshold =
                            resource_config[fs_resource_index].major_threshold;

                        if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                            resource_config[i].critical_threshold =
                                resource_config[fs_resource_index].critical_threshold;
                        }
                        resource_config[i].num_tries =
                            resource_config[fs_resource_index].num_tries;
                    }
                    else {
                        /* There are no specific percent thresholds for
                           the dynamic resource, use defaults */
                        resource_config[i].minor_threshold = FS_MINOR;
                        resource_config[i].major_threshold = FS_MAJOR;
                        if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                            resource_config[i].critical_threshold = FS_CRITICAL;
                        }
                    }
                }
                else if (abs_values[0] != 0) {
                    /* Specific percent thresholds are defined for the dynamic resource */
                    resource_config[i].minor_threshold = abs_values[0];
                    resource_config[i].major_threshold = abs_values[1];     
                    if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                        resource_config[i].critical_threshold = abs_values[2]; 
                    }
                }
                break;
            
            case PERCENT_UNUSED:
                if (abs_values[0] == 0) {
                    // if this is a static mounted file system then use common
                    // threshold values provided for the File System Resource
                    if ( (alarm_type == STATIC_ALARM) && (mounted == MOUNTED) ) {
                        resource_config[i].minor_threshold_abs_node0 = 
                            resource_config[fs_resource_index].minor_threshold_abs_node0;

                        resource_config[i].major_threshold_abs_node0 = 
                            resource_config[fs_resource_index].major_threshold_abs_node0;

                        if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                            resource_config[i].critical_threshold_abs_node0 = DEFAULT_CRITICAL_ABS_NODE0;
                        }
                        resource_config[i].num_tries =
                            resource_config[fs_resource_index].num_tries;
                    }
                    else {
                        /* If the percent thresholds are selected 
                         * use the default thresholds for the absolute
                         * value thresholds for the dynamic resource */
                        resource_config[i].minor_threshold_abs_node0 = DEFAULT_MINOR_ABS_NODE0;
                        resource_config[i].major_threshold_abs_node0 = DEFAULT_MAJOR_ABS_NODE0; 
                        if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                            resource_config[i].critical_threshold_abs_node0 = DEFAULT_CRITICAL_ABS_NODE0;  
                        }
                    }
                } 
                else if (abs_values[0] != 0) {
                    /* Specific absolute value thresholds are specified for the dynamic resource */ 
                    resource_config[i].minor_threshold_abs_node0 = abs_values[0];
                    resource_config[i].major_threshold_abs_node0 = abs_values[1];
                    if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                        resource_config[i].critical_threshold_abs_node0 = abs_values[2];  
                    }
                }
                break;
        }

        ilog ("Monitoring %2d: %-20s (%s) (%s)\n", i, resource_config[i].resource ,
                resource_config[i].severity, (enabled ? "enabled" : "disabled") );

        /* Init the timer for this resource */
        mtcTimer_init ( rtimer[i] ) ;

        rtimer[i].hostname = "localhost" ;
        rtimer[i].service  = resource_config[i].resource ;
        resource_config[i].i                 = i;
        resource_config[i].failed            = false ;
        resource_config[i].count             = 0 ;
        resource_config[i].stage             = RMON_STAGE__START ; 
        resource_config[i].sev               = SEVERITY_CLEARED ;
        resource_config[i].failed_send       = 0;
        resource_config[i].alarm_raised      = false;
        resource_config[i].res_type          = RESOURCE_TYPE__FILESYSTEM_USAGE ;

        /* add the alarm id for the FM API per resource monitored */
        snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, FS_ALARM_ID);

        mem_log_resource ( &resource_config[i] );  

        i++;
        _rmon_ctrl_ptr->resources = i;  
    }
}

/*****************************************************************************
 *
 * Name    : save_dynamic_resource
 *
 * Purpose : Loops through resources and only adds a dynamic file system 
 * resource if it does not yet exist 
 ******************************************************************************/
void save_fs_resource ( string resource_name, string criticality, 
                        int enabled, int percent,
                        int abs_values[3], int alarm_type,
                        string type, string device, int mounted)
{

    size_t resource_index; 
    size_t criticality_index; 
    size_t types_index;
    size_t devices_index;

    bool newResource = true;

    for (int k=0; k< _rmon_ctrl_ptr->resources; k++) {

        if (strcmp(resource_config[k].resource, resource_name.c_str()) == 0) {
            newResource = false; 
            break;
        }
    }

    if (newResource == true) {
        dlog ("%s(%s) fs resource add in %s state\n", resource_name.c_str(),
              criticality.c_str(), (enabled) ? "enabled" : "disabled");
        dynamic_resource.push_back(resource_name);
        resource_index =  dynamic_resource.size() - 1;
        /* add the criticality value to a vector for permenant storage */ 
        criticality_resource.push_back(criticality);
        criticality_index =  criticality_resource.size() - 1;  
        types.push_back(type);
        types_index = types.size() - 1;
        devices.push_back(device);
        devices_index = devices.size() - 1;
        add_fs_resource ( resource_index, criticality_index, enabled, percent, abs_values, alarm_type, types_index, devices_index, mounted );
    }
}

/*****************************************************************************
 *
 * Name    : add_dynamic_mem_resource
 *
 * Purpose : Add a dynamic memory resource at runtime based on the name and criticality.
 * The resource has both custom or default percent and absolute thresholds. 
 * *****************************************************************************/
int add_dynamic_mem_resource ( int resource_index, int criticality_index,
                               double r_value, int percent, int abs_values[3],
                               const char * alarm_id, int socket_id=0 )
{

    int i = _rmon_ctrl_ptr->resources;
    int new_index = i;
    if (i >= MAX_RESOURCES) {
        wlog ("Cannot Monitor more than %d resources\n", MAX_RESOURCES ); 
    }
    else {

        resource_config[i].resource = dynamic_resource.at(resource_index).c_str();  
        resource_config[i].severity = criticality_resource.at(criticality_index).c_str();

        if ((percent == 1) && (abs_values[0] == 0)) {
            /* There are no specific percent thresholds for the dynamic resource, use defaults */ 
            resource_config[i].minor_threshold = DEFAULT_MINOR;
            resource_config[i].major_threshold = DEFAULT_MAJOR;     
            if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                resource_config[i].critical_threshold = DEFAULT_CRITICAL; 
            } else {
                resource_config[i].critical_threshold = UNUSED_CRITICAL;
            }
        } 
        else if ((percent == 1) && (abs_values[0] != 0)) {
            /* Specific percent thresholds are defined for the dynamic resource */
            resource_config[i].minor_threshold = abs_values[0];
            resource_config[i].major_threshold = abs_values[1];     
            if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                resource_config[i].critical_threshold = abs_values[2]; 
            } else {
                resource_config[i].critical_threshold = UNUSED_CRITICAL;
            }
        } 

        if ((percent == 0) && (abs_values[0] == 0)) {
            /* If the percent thresholds are selected use the default thresholds for the absolute
             * value thresholds for the dynamic resource */ 
            resource_config[i].minor_threshold_abs_node0 = DEFAULT_MINOR_ABS_NODE0;
            resource_config[i].major_threshold_abs_node0 = DEFAULT_MAJOR_ABS_NODE0; 
            if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                resource_config[i].critical_threshold_abs_node0 = DEFAULT_CRITICAL_ABS_NODE0;  
            } else {
                resource_config[i].critical_threshold_abs_node0 = UNUSED_CRITICAL_ABS_NODE0; 
            }
            resource_config[i].minor_threshold_abs_node1 = DEFAULT_MINOR_ABS_NODE1;
            resource_config[i].major_threshold_abs_node1 = DEFAULT_MAJOR_ABS_NODE1;     
            resource_config[i].critical_threshold_abs_node1 = DEFAULT_CRITICAL_ABS_NODE1;
        } 
        else if ((percent == 0) && (abs_values[0] != 0)) {
            /* Specific absolute value thresholds are specified for the dynamic resource */ 
            resource_config[i].minor_threshold_abs_node0 = abs_values[0];
            resource_config[i].major_threshold_abs_node0 = abs_values[1];
            if (_rmon_ctrl_ptr->rmon_critical_thr == 1) {
                resource_config[i].critical_threshold_abs_node0 = abs_values[2];  
            } else {
                resource_config[i].critical_threshold_abs_node0 = UNUSED_CRITICAL_ABS_NODE0;
            }
            resource_config[i].minor_threshold_abs_node1 = DEFAULT_MINOR_ABS_NODE1;
            resource_config[i].major_threshold_abs_node1 = DEFAULT_MAJOR_ABS_NODE1;     
            resource_config[i].critical_threshold_abs_node1 = DEFAULT_CRITICAL_ABS_NODE1;
        }

        resource_config[i].num_tries = DEFAULT_NUM_TRIES;       
        resource_config[i].alarm_status = DEFAULT_ALARM_STATUS;
        resource_config[i].percent = percent;

        ilog ("Monitoring %2d: Dynamic Resource- %s (%s)\n", i, resource_config[i].resource ,
                resource_config[i].severity );

        /* Init the timer for this resource */
        mtcTimer_init ( rtimer[i] ) ;

        rtimer[i].hostname = "localhost" ;
        rtimer[i].service  = resource_config[i].resource ;
        resource_config[i].i                 = i;
        resource_config[i].failed            = false ;
        resource_config[i].count             = 0 ;
        resource_config[i].resource_value    = r_value ;
        resource_config[i].resource_prev     = r_value ;
        resource_config[i].stage             = RMON_STAGE__START ; 
        resource_config[i].sev               = SEVERITY_CLEARED ;
        resource_config[i].alarm_type        = STANDARD_ALARM;
        resource_config[i].failed_send       = 0;
        resource_config[i].alarm_raised      = false;
        resource_config[i].socket_id         = socket_id;

        /* add the alarm id for the FM API per resource monitored */
        snprintf(resource_config[i].alarm_id, FM_MAX_BUFFER_LENGTH, alarm_id);

        mem_log_resource ( &resource_config[i] );     
        i++;
        _rmon_ctrl_ptr->resources = i;  
    }
    return new_index;
}

/*****************************************************************************
 *
 * Name    : save_dynamic_mem_resource 
 *
 * Purpose : Loops through resources and only adds a memory resource if it does not yet 
 * exist 
 ******************************************************************************/
int save_dynamic_mem_resource ( string resource_name, string criticality,
                                double r_value, int percent, int abs_values[3],
                                const char * alarm_id, int socket_id=0 )
{

    size_t resource_index; 
    size_t criticality_index; 
    bool newResource = true;
    int updated_index;

    for (int k=0; k< _rmon_ctrl_ptr->resources; k++) {

        if (strcmp(resource_config[k].resource, resource_name.c_str()) == 0) {
            resource_config[k].resource_value=
            resource_config[k].resource_prev = r_value;
            updated_index = k;
            newResource = false; 
            break;
        }
    }

    if (newResource == true) {
        dynamic_resource.push_back(resource_name);
        resource_index =  dynamic_resource.size() - 1;
        /* add the criticality value to a vector for permenant storage */ 
        criticality_resource.push_back(criticality);
        criticality_index =  criticality_resource.size() - 1;     
        updated_index = add_dynamic_mem_resource(resource_index, criticality_index,
                                                 r_value, percent, abs_values,
                                                 alarm_id, socket_id);
        rmon_alarming_init( &resource_config[updated_index] );
        resource_config[updated_index].resource_prev =
        resource_config[updated_index].resource_value= r_value;
    }
    return updated_index;
}

/*****************************************************************************
 *
 * Name    : calculate_fs_usage
 *
 * Purpose : Calculate the file system usage as a percentage or an absolute value 
 * for the number of MiB remaining overall and in a specific fs. The calculation
 * is done by executing the df command and getting the response for each type
 * of filesystem being monitored.  
 *****************************************************************************/
void calculate_fs_usage ( resource_config_type * ptr )
{
    dlog("%s, is mounted resource: %d is enabled: %d\n", ptr->resource, ptr->mounted, ptr->alarm_status);

    FILE *pFile; 
    int last_index;
    char fsLine[128];
    char buf[200];
    double fsUsage = 0;
    char mounted_on[50], file_system[50], capacity[10];
    unsigned long long size, used, available; 
    string res_val;
    double cap_percent;
    double MiB = 1024.0;
    double free_units = 0;
    double usage_percents = 0;
    double total_units = 0;

    if (ptr->mounted == MOUNTED)
    {
        if (strcmp(ptr->resource, FS_RESOURCE_NAME) == 0)
        {
            // We do not calculate the total for filesystem
            // Resource FS_RESOURCE_NAME represents the total filesystem
            return;
        }
        else 
        {
            snprintf(buf, sizeof(buf), "timeout 2 df -T -P --local %s 2>/dev/null", ptr->resource);
        }

        /* convert output of "df -P" from KiB to MiB */
        if(!(pFile = popen(buf, "r")))
        {
            elog("Error, command df is not executed on resource: %s\n", ptr->resource);
        }
        else 
        {
            while (memset(fsLine, 0, sizeof(fsLine)) && (fgets((char*) &fsLine, sizeof(fsLine), pFile) != NULL)) 
            {
                sscanf(fsLine, "%49s %*s %llu %llu %llu %9s %49s", file_system, &size, &used, &available, capacity, mounted_on);
                if (strcmp(mounted_on, ptr->resource) == 0)
                {
                    string temp_val(capacity);
                    // exclude percentage (%) sign
                    last_index = temp_val.find_first_not_of("0123456789");
                    res_val = temp_val.substr(0, last_index);
                    snprintf(capacity, sizeof(capacity), res_val.c_str());
                    sscanf(capacity, "%lf", &cap_percent);

                    if (ptr->percent == PERCENT_USED)
                    {
                        fsUsage = cap_percent;
                        ptr->resource_value = fsUsage;
                        if ( log_value ( ptr->resource_value,
                                         ptr->resource_prev,
                                         DEFAULT_LOG_VALUE_STEP ) )
                        {
                            plog("filesystem: %s usage: %.2f%%\n",
                                  ptr->resource, ptr->resource_value);
                        }
                    }
                    else
                    {
                        fsUsage = (double) (((100 - cap_percent) / 100) * size);
                        fsUsage = fsUsage / MiB;
                        ptr->resource_value = fsUsage;
                        if ( log_value ( ptr->resource_value,
                                         ptr->resource_prev,
                                         DEFAULT_LOG_VALUE_STEP ) )
                        {
                            plog("filesystem: %s has %f (MiB) (free)\n",
                                  ptr->resource, ptr->resource_value);
                        }
                    }

                    // The size of the file system is 2X the user specified size to allow upgrades.
                    // Currently we are alarming on the used size but instead the alarming should be based on used size /2.
                    // As a result there is no indication to the user that they have may have eaten into the reserved space
                    // for upgrades resulting in an aborted upgrade.
                    if (strcmp(mounted_on, POSTGRESQL_FS_PATH) == 0)
                    {
                        ptr->resource_value = ptr->resource_value / 2;
                    }
                }
            }
        }
        pclose(pFile); 
    }
    else if(strcmp(ptr->resource, NOVA_LOCAL) == 0)
    {
        /*rmon queries the thin pool usage if the volume group is nova-local*/
        snprintf(buf, sizeof(buf), "timeout 2 lvdisplay -C --noheadings --nosuffix -o data_percent --units m "
                                   "/dev/nova-local/nova-local-pool 2>/dev/null");

        if(!(pFile = popen(buf, "r")))
        {
            elog("Error, command lvdisplay free units is not executed \n");
        }
        else
        {
            while (memset(fsLine, 0, sizeof(fsLine)) && (fgets((char*) &fsLine, sizeof(fsLine), pFile) != NULL))
            {
                usage_percents = atof(fsLine);
            }
            pclose(pFile);
        }
        ptr->resource_value = usage_percents;
        if ( log_value ( ptr->resource_value,
                         ptr->resource_prev,
                         DEFAULT_LOG_VALUE_STEP ))
        {
            plog("filesystem: %s, usage: %f%% \n", ptr->resource, ptr->resource_value);
        }
    }
    else if(strcmp(ptr->resource, CINDER_VOLUMES) == 0)
    {
        /*rmon queries the thin pool usage if the volume group is cinder-volumes*/
        snprintf(buf, sizeof(buf), "timeout 2 lvdisplay -C --noheadings --nosuffix -o data_percent --units m "
                                   "/dev/cinder-volumes/cinder-volumes-pool 2>/dev/null");

        if(!(pFile = popen(buf, "r")))
        {
            elog("Error, command lvdisplay free units is not executed \n");
        }
        else
        {
            while (memset(fsLine, 0, sizeof(fsLine)) && (fgets((char*) &fsLine, sizeof(fsLine), pFile) != NULL))
            {
                usage_percents = atof(fsLine);
            }
            pclose(pFile);
        }
        ptr->resource_value = usage_percents;
        if ( log_value ( ptr->resource_value,
                         ptr->resource_prev,
                         DEFAULT_LOG_VALUE_STEP ))
        {
            plog("filesystem: %s, usage: %.2f%% \n", ptr->resource, ptr->resource_value);
        }
    }
    else
    {
        /* for the unmounted dynamic file system resources, use the vgdisplay command to get vg free units */
        snprintf(buf, sizeof(buf), "timeout 2 vgdisplay -C --noheadings --nosuffix -o vg_free --units m %s 2>/dev/null", ptr->resource);

        if(!(pFile = popen(buf, "r")))
        {
            elog("Error, command vgdisplay free units is not executed \n");
        }
        else
        {
            while (memset(fsLine, 0, sizeof(fsLine)) && (fgets((char*) &fsLine, sizeof(fsLine), pFile) != NULL))
            {
                free_units = atof(fsLine);
            }
            pclose(pFile);
        }

        /* for the unmounted dynamic file system resources, use the vgdisplay command to get vg size */
        snprintf(buf, sizeof(buf), "timeout 2 vgdisplay -C --noheadings --nosuffix -o vg_size --units m %s 2>/dev/null", ptr->resource );

        if(!(pFile = popen(buf, "r")))
        {
            elog("Error, command vgdisplay total units is not executed \n");
        }
        else
        {
            while (memset(fsLine, 0, sizeof(fsLine)) && (fgets((char*) &fsLine, sizeof(fsLine), pFile) != NULL))
            {
                total_units = atof(fsLine);
            }
            pclose(pFile);
        }

        if ( ptr->percent == PERCENT_USED )
        {
            if (total_units != 0)
            {
                ptr->resource_value = (double) (( (total_units - free_units) / total_units ) * 100);
            }
            else
            {
                ptr->resource_value = 0;
            }
            if ( log_value ( ptr->resource_value,
                             ptr->resource_prev,
                             DEFAULT_LOG_VALUE_STEP ))
            {
                plog("volume-group: %s, usage: %.2f%%\n", ptr->resource, ptr->resource_value);
            }
        }
        else
        {
            ptr->resource_value = free_units;
            if ( log_value ( ptr->resource_value,
                             ptr->resource_prev,
                             DEFAULT_LOG_VALUE_STEP ))
            {
                plog("volume-group: %s, %.2f (MiB) free\n", ptr->resource, ptr->resource_value);
            }
        }
    }
}

/*****************************************************************************
 *
 * Name    : init_memory_checking
 *
 * Purpose : Get the memory accounting used either 0: overcommit or 1: strict
 *****************************************************************************/
void init_memory_accounting()
{

    const char *strict_memory_file = "/proc/sys/vm/overcommit_memory";

    ifstream mem_file ( strict_memory_file );
    string strict_line;

    if (mem_file.is_open())
    {

        while ( getline (mem_file, strict_line) ) {
            IS_STRICT = atoi(strict_line.c_str());
        }
        mem_file.close();

    }

}

/*****************************************************************************
 *
 * Name    : thinpool_calcVirtUsage
 *
 * Purpose : Obtain the percentage of the used virtual space in thin
 *           provisioning.
 *
 * Params  : index - the index of the monitored resource (virtual space)
 *
 * Return  : PASS/FAIL
 *
 *****************************************************************************/
int thinpool_calcVirtUsage(int index,
                           const char *poolName,
                           const char *poolOwner,
                           const char *allocParam) {

    /* Initialize the variables used in calculating the virtual usage. */
    double provisioned_capacity = 0;
    double total_capacity = 0;
    double allocation_ratio = 1;
    double ratio = 0;
    double MiB = 1024.0;

    /* Buffer (and its size) for keeping the initial result after executing
       the above commands. */
    char result[BUFFER_SIZE];
    const unsigned int buffer_size = BUFFER_SIZE;

    /* Return code. */
    int rc;

    /* Save the necessary commands for obtaining the information about virtual
       thin pool usage: provisioned capacity, total capacity and maximum
       oversubscription ratio. */
    const char *provisioned_capacity_cmd = NULL;
    const char *allocation_ratio_cmd = NULL;
    char total_capacity_cmd[BUFFER_SIZE];

    snprintf(total_capacity_cmd, sizeof(total_capacity_cmd),
             "lvs --units m --segments | grep \"%s\" | awk '{print $6}' | sed '$s/.$//'",
             poolName);

    if (strcmp (poolOwner, "Cinder") == 0) {
        const char *cinder_provisioned_capacity_cmd ="lvs --units m | grep \"volume-[.]*\" | awk '{ sum+=$4} END {print sum}'";
        const char *cinder_allocation_ratio_cmd = "cat /etc/cinder/cinder.conf | grep \"^max_over_subscription_ratio\" | cut -d '=' -f 2";
        provisioned_capacity_cmd = cinder_provisioned_capacity_cmd;
        allocation_ratio_cmd = cinder_allocation_ratio_cmd;
    } else if (strcmp (poolOwner, "Nova") == 0) {
        const char *nova_provisioned_capacity_cmd = "lvs --units m | grep \"[.]*_disk\" | awk '{ sum+=$4} END {print sum}'";
        provisioned_capacity_cmd = nova_provisioned_capacity_cmd;
    }
    /* Determine the provisioned capacity. */
    rc = execute_pipe_cmd(provisioned_capacity_cmd, result, buffer_size);
    if (rc != PASS) {
        wlog("%s LVM Thinpool ; unable to query provisioned capacity (rc:%i)",
             poolOwner, rc);
        return (FAIL);
    }
    provisioned_capacity = atof(result);
    dlog("%s LVM Thinpool provisioned capacity is %f", poolOwner, provisioned_capacity);

    /* If the threshold is of percentage type, then also determine the total
       thin pool capacity and the max oversubscription ratio. */
    rc = execute_pipe_cmd(total_capacity_cmd, result, buffer_size);
    if (rc != PASS) {
        elog("%s LVM Thinpool ; unable to query total capacity (rc:%i)",
             poolOwner, rc);
        return (FAIL);
    }
    total_capacity = atof(result);
    dlog("%s LVM Thinpool total capacity is %f",
         poolOwner, total_capacity);

    if (strcmp (poolOwner, "Cinder") == 0) {
        rc = execute_pipe_cmd(allocation_ratio_cmd, result, buffer_size);
        if (rc != PASS) {
            elog("%s LVM Thinpool %s ratio could not be determined (rc:%i)",
                 allocParam, poolOwner, rc);
            return (FAIL);
        }
        allocation_ratio = atof(result);
    } else if (strcmp (poolOwner, "Nova") == 0) {
        allocation_ratio = 1.0;
    }
    dlog("%s LVM Thinpool %s is %f", poolOwner, allocParam, allocation_ratio);

    /* If the allocation_ratio is 0 or hasn't been found, its default
       value should be 1. */
    if (allocation_ratio == 0)
        allocation_ratio = 1;

    /* Compute the current virtual space usage of the thin pool. */
    if (total_capacity != 0){
        ratio = provisioned_capacity / (total_capacity * allocation_ratio) * 100;
    } else {
        /*3 minutes (30 sec * rate_throttle = 180 sec)*/
        /* Change the warning log to a debug log to avoid generating this log in
           rmond.log when Cinder is Ceph backended. Once the repackaging of cinder_virtual_resource.conf
           and nova_virtual_resource.conf is done, we will change it back to warning log. */
        dlog("%s LVM Thinpool total capacity is 0\n", poolOwner);
        return (FAIL);
    }

    /* Update the resource value configuration. */
    if (resource_config[index].percent == 1) {
        resource_config[index].resource_value = ratio;
        if ( log_value ( resource_config[index].resource_value,
                         resource_config[index].resource_prev,
                         DEFAULT_LOG_VALUE_STEP ))
        {
            plog("%s LVM Thinpool Usage: %.2f%%", poolOwner, ratio);
        }
    }
    else {
        resource_config[index].resource_value =
            ((total_capacity * allocation_ratio) - provisioned_capacity) * MiB;
        if ( log_value ( resource_config[index].resource_value,
                         resource_config[index].resource_prev,
                         DEFAULT_LOG_VALUE_STEP ))
        {
            plog("%s LVM Thinpool has %.2f (MiB) free",
                     poolOwner,
                     resource_config[index].resource_value);
        }
    }
    return (PASS);
}
/*****************************************************************************
 *
 * Name    : calculate_virtual_space_usage
 *
 * Purpose : Obtain the percentage of the used virtual space in thin
 *           provisioning.
 *
 * Params  : index - the index of the monitored resource (virtual space)
 *
 * Return  : PASS/FAIL
 *
 *****************************************************************************/
int calculate_virtual_space_usage(int index, const char* constant) {
    int rc = 0;
    if (strcmp(constant, V_CINDER_THINPOOL_RESOURCE_NAME) == 0) {
        rc = thinpool_calcVirtUsage(index,
                                    "cinder-volumes-pool",
                                    "Cinder",
                                    "max_over_subscription_ratio");
    } else if (strcmp(constant, V_NOVA_THINPOOL_RESOURCE_NAME) == 0) {
        rc = thinpool_calcVirtUsage(index,
                                    "nova-local-pool",
                                    "Nova",
                                    "disk_allocation_ratio");
    }

    return rc;
}

/*****************************************************************************
 *
 * Name    : calculate_memory_usage
 *
 * Purpose : Calculate the memory usage as a percentage or absolute value for the
 * number of MiB left.  The overall average memory usage as well as the per NUMA
 * node memory usage is computed.  
 *****************************************************************************/
void calculate_memory_usage( int index ) {

    const char *mem_info = "/proc/meminfo";
    FILE *pFile; 
    char memoryLine[40];
    char attribute_name[30];
    double memUsage, memUsageHuge;
    char *line0 = &memoryLine[0];
    char *line3 = &memoryLine[3];
    char *line10 = &memoryLine[10];
    unsigned long int value;
    unsigned long int avail = 0;
    unsigned long int memTotal;
    int resource_name_size = 100;
    string resource_name_huge = "processor_hugepages_";
    string resource_name = "processor_";
    char numa_node[resource_name_size]; 
    string criticality = "critical";
    double MiB = 1024.0; 
    int absolute_thresholds[3];
    memoryinfo memInfo;
    struct dirent *ent;
    DIR *numa_node_dir;
    vector<string> numa_files;
    vector<string> node_files;

    memset ( (char*)&memInfo, 0, sizeof(memoryinfo));

    if ((pFile = fopen(mem_info, "r")) == NULL){
        dlog("failed to open: /proc/meminfo \n");
    }

    else {

        while (memset(memoryLine, 0, sizeof(memoryLine)) && (fgets((char*) &memoryLine, sizeof(memoryLine), pFile) != NULL)) {

            if (*line3 == 'T') {
                /* match MemTotal */  
                value = 0UL;
                if (sscanf(memoryLine, "MemTotal: %lu", &value) == 1) {
                    memInfo.MemTotal = value;
                    continue;
                }
            } else if (*line3 == 'F') {
                /* match MemFree */
                value = 0UL;
                if (sscanf(memoryLine, "MemFree: %lu", &value) == 1) {
                    memInfo.MemFree = value;
                    continue;
                } 
            } else if (*line3 == 'f') {
                /* match Buffers */
                value = 0UL;
                if (sscanf(memoryLine, "Buffers: %lu", &value) == 1) {
                    memInfo.Buffers = value;
                    continue;
                } 
            } else if (*line3 == 'h') {
                /* match Cached */
                value = 0UL;
                if (sscanf(memoryLine, "Cached: %lu", &value) == 1) {
                    memInfo.Cached = value;
                    continue;
                }
            } else if ((*line0 == 'S') && (*line3 == 'c')) {
                /* match Slab Reclaimable */
                value = 0UL;
                if (sscanf(memoryLine, "SReclaimable: %lu", &value) == 1) {
                    memInfo.SlabReclaimable = value;
                    continue;
                }
            } else if ((*line0 == 'C') && (*line10 == 't')) {
                /* match CommitLimit */
                value = 0UL;
                if (sscanf(memoryLine, "CommitLimit: %lu", &value) == 1) {
                    memInfo.CommitLimit = value;
                    continue;
                } 
            } else if ((*line0 == 'C') && (*line10 == 'A')) {
                /* match Committed_AS */ 
                value = 0UL;
                if (sscanf(memoryLine, "Committed_AS: %lu", &value) == 1) {
                    memInfo.Committed_AS = value;
                    continue;
                }
            } else if ((*line0 == 'H') && (*line10 == 'T')) {
                /* match Hugepages_Total */
                value = 0UL;
                if (sscanf(memoryLine, "HugePages_Total: %lu", &value) == 1) {
                    memInfo.HugePages_Total = value;
                    continue;
                }
            }
            else if ((*line0 == 'H') && (*line10 == 'z')) {
                /* match Hugepagesize */
                value = 0UL;
                if (sscanf(memoryLine, "Hugepagesize: %lu", &value) == 1) {
                    memInfo.Hugepagesize = value;
                    continue;
                }
            }
            else if ((*line0 == 'A') && (*line3 == 'n')) {
                /* match AnonPages      */
                value = 0UL;
                if (sscanf(memoryLine, "AnonPages: %lu", &value) == 1) {
                    memInfo.AnonPages = value;
                    continue;
                }
            }
        }
        fclose(pFile);
    }

    avail = memInfo.MemFree + memInfo.Buffers + memInfo.Cached + memInfo.SlabReclaimable;
    memTotal = avail + memInfo.AnonPages;
    dlog("memTotal: %lu\n", memTotal);

    /* average memory utilization */
    if (IS_STRICT == 1) {
        /* strict memory checking enabled */
        if (resource_config[index].percent == 1) {
            memUsage = (double) memInfo.Committed_AS / memInfo.CommitLimit;
            memUsage = memUsage * 100;
        } else {
            memUsage = (double) (memInfo.CommitLimit - memInfo.Committed_AS) / MiB;
        }
    } else {
        if (resource_config[index].percent == 1)
        {
            memUsage = (double) memInfo.AnonPages / memTotal;
            memUsage = memUsage * 100;
        } else
        {
            memUsage = (double) avail / MiB;
        }
    }
    resource_config[index].resource_value = memUsage;
    if (resource_config[index].percent == 1)
    {
        if ( log_value ( resource_config[index].resource_value,
                         resource_config[index].resource_prev,
                         DEFAULT_LOG_VALUE_STEP ))
        {
            plog("%s: %.2f%%\n",
                      resource_config[index].resource, memUsage);
        }
    }
    else
    {
        if ( log_value ( resource_config[index].resource_value,
                         resource_config[index].resource_prev,
                         DEFAULT_LOG_VALUE_STEP ))
        {
            plog("%s: %.2f (MiB) free\n",
                      resource_config[index].resource, memUsage);
        }
    }
    if ((numa_node_dir= opendir ("/sys/devices/system/node/")) != NULL) {
        /* print all the files and directories within directory */
        while ((ent = readdir (numa_node_dir)) != NULL) {
            if (strstr(ent->d_name, "node") != NULL) {
                numa_files.push_back(ent->d_name);
            }
        }
        closedir (numa_node_dir);
    }

    /* loop through all NUMA nodes to get memory usage per NUMA node */
    for (unsigned int p=0; p<numa_files.size(); p++) {

        snprintf(numa_node, resource_name_size, "/sys/devices/system/node/%s/meminfo",numa_files.at(p).c_str());
        resource_name += numa_files.at(p);
        resource_name_huge += numa_files.at(p);
        pFile = fopen (numa_node, "r");

        while (memset(memoryLine, 0, sizeof(memoryLine)) && (fgets((char*) &memoryLine, sizeof(memoryLine), pFile) != NULL)) {

            sscanf(memoryLine, "Node %*d %29s %lu", attribute_name, &value);

            if (strstr(attribute_name, "MemTotal") != NULL) {      
                /* match MemTotal */
                memInfo.MemTotal = value;    

            } else if (strstr(attribute_name, "MemFree") != NULL) {   
                /* match MemFree */
                memInfo.MemFree = value;

            } else if (strstr(attribute_name, "FilePages") != NULL) {   
                /* match FilePages */
                memInfo.FilePages = value;

            } else if (strstr(attribute_name, "SReclaimable") != NULL) {         
                /* match SReclaimable */
                memInfo.SlabReclaimable = value;

            } else if (strstr(attribute_name, "HugePages_Total") != NULL) {         
                /* match HugePages_Total */
                memInfo.HugePages_Total = value;     

            } else if (strstr(attribute_name, "HugePages_Free") != NULL) {
                /* match HugePages_Free */
                memInfo.HugePages_Free = value;

            } else if (strstr(attribute_name, "AnonPages") != NULL) {
                /* match AnonPages    ) */
                memInfo.AnonPages = value;
            }
        }

        fclose(pFile);

        if (_rmon_ctrl_ptr->per_node == 1) {
            /* if set to 1 get the per NUMA node memory values */ 
            memset(absolute_thresholds, 0, sizeof(absolute_thresholds));
            avail = memInfo.MemFree + memInfo.FilePages + memInfo.SlabReclaimable;
            memTotal = avail + memInfo.AnonPages;
            /* NUMA node memory usage */ 
            if (resource_config[index].percent == 1) {
                memUsage = (double) memInfo.AnonPages / memTotal;
                memUsage = memUsage * 100;
                dlog("Memory Usage %s: %.2f%% \n", resource_name.c_str(), memUsage);
            } else {
                memUsage = (double) avail / MiB;
                dlog("Memory Available %s: %.2f MB \n", resource_name.c_str(), memUsage);
            }
            /* initialize a new dynamic resource for the NUMA node if it does not already exist */    
            save_dynamic_mem_resource ( resource_name, criticality, memUsage, resource_config[index].percent, 
                    absolute_thresholds, MEMORY_ALARM_ID );
        } 


        if (HUGEPAGES_NODE == 1) {
            /* huge pages memory usage for the NUMA node */
            if (memInfo.HugePages_Total != 0){
                if (resource_config[index].percent == 1){
                    memUsageHuge = (double) (memInfo.HugePages_Total - memInfo.HugePages_Free) / memInfo.HugePages_Total; 
                    memUsageHuge = memUsageHuge * 100;
                    dlog("Memory Usage %s: %.2f%% \n", resource_name_huge.c_str(), memUsageHuge);
                } else {
                    memUsageHuge = (double) memInfo.HugePages_Free * (memInfo.Hugepagesize/MiB) ;
                    dlog("Memory Available %s: %.2f MB \n", resource_name_huge.c_str(), memUsageHuge);
                }
                save_dynamic_mem_resource ( resource_name_huge, criticality, memUsageHuge, resource_config[index].percent,
                        absolute_thresholds, MEMORY_ALARM_ID );    
            }
        }
        resource_name_huge = "processor_hugepages_";
        resource_name = "processor_";
    }         
}

/*****************************************************************************
 *
 * Name    : get_cpu_time
 *

 * Purpose : Parse per-cpu hi-resolution scheduling stats
 *
 *****************************************************************************/
int get_cpu_time( unsigned long long * cpu_time )
{
#define MAX_STRING_SIZE (19)

    const char *sched_stat = "/proc/schedstat"; 
    FILE * pFile; 
    char cpu_line[500];
    unsigned long long value;
    int version = 0; 
    int index = 0;
    char cpu_time_len[50];

    if ((pFile = fopen(sched_stat, "r")) == NULL){
        dlog("failed to open: /proc/schedstat \n");
        return (FAIL);
    }

    else {
        /* Parse per-cpu hi-resolution scheduling stats */
        while (memset(cpu_line, 0, sizeof(cpu_line)) && (fgets((char*) &cpu_line, sizeof(cpu_line), pFile) != NULL)) {

            if (version != 15){
                /* only version 15 is supported */
                if (sscanf(cpu_line, "version %llu", &value) == 1) {             
                    version = (int) value; 
                }
            }
            else if ((strstr(cpu_line, "cpu") != NULL) && (version == 15))
            {          
                sscanf(cpu_line, "%*s %*s %*s %*s %*s %*s %*s %49s ",cpu_time_len);
                if (((unsigned)strlen(cpu_time_len)) < MAX_STRING_SIZE) {
                    /* get the cpu time values for each cpu which is the 7th field */  
                    sscanf(cpu_line, "%*s %*s %*s %*s %*s %*s %*s %llu ",&value); 
                    cpu_time[index++] = value;     
                }
                else {
                    elog("%s exceeded 2^64 for cpu stats cannot calculate cpu usage\n", cpu_time_len);
                    cpu_time[index++] = 0; 
                }
            }

        }
        fclose(pFile); 
    }

    return (PASS);
}

/*****************************************************************************
 *
 * Name    : cpu_monitoring_init
 *

 * Purpose : Get the base cpu list if running on a compute.  Also get the number
 * of cpus from: /proc/cpuinfo 
 *****************************************************************************/
void cpu_monitoring_init()
{

    string base_cpu=""; 
    FILE * pFile;
    string delimiter = ",", delimiterTwo = "-";
    size_t pos = 0;
    string token;
    char cpu_line[100];
    const char *cpu_info = "/proc/cpuinfo";
    char processor[20]; 

    pFile = fopen (COMPUTE_RESERVED_CONF , "r");
    if (pFile != NULL){
        ilog("File %s is present\n", COMPUTE_RESERVED_CONF);
        ifstream fin( COMPUTE_RESERVED_CONF  );
        string line;

        while( getline( fin, line ) ) {
            /* process each line */
            if( line.find ("PLATFORM_CPU_LIST=") != string::npos ) {
                stringstream ss( line );
                getline( ss, base_cpu, '=' ); // token = string before =
                getline( ss, base_cpu, '=' ); // token = string after =
                ilog("Found PLATFORM_CPU_LIST set to %s in file %s\n", base_cpu.c_str(), COMPUTE_RESERVED_CONF);
            }
        }                                     
        fclose (pFile);         
    }     

    if (base_cpu.compare("") != 0)
    {
        /* get base cpus if they are available */ 
        if ((pos = base_cpu.find(delimiter)) != string::npos) {

            /* if the base cpus are listed with a comma, ex: 1,2 */ 
            base_cpu = base_cpu + delimiter;
            while ((pos = base_cpu.find(delimiter)) != string::npos) {
                token = base_cpu.substr(0, pos);
                included_cpu[num_base_cpus++] = atoi(token.c_str()); 
                base_cpu.erase(0, pos + delimiter.length());
            }
        } else if ((pos = base_cpu.find(delimiterTwo)) != string::npos) {

            /* if the base cpus are listed with a dash, ex: 1-3 */
            base_cpu = base_cpu + delimiterTwo;
            token = base_cpu.substr(0, pos);
            int first_cpu =  atoi(token.c_str()); 
            base_cpu.erase(0, pos + delimiterTwo.length());
            pos = base_cpu.find(delimiterTwo);
            token = base_cpu.substr(0, pos);
            int last_cpu =  atoi(token.c_str()); 

            /* loop through the list of base cpus */
            for (num_base_cpus=0; num_base_cpus<=(last_cpu - first_cpu); num_base_cpus++){
                included_cpu[num_base_cpus++] = first_cpu++; 
            }
        }

        if (num_base_cpus == 0) {
            /* only one base cpu available */ 
            included_cpu[num_base_cpus++] = atoi(base_cpu.c_str());    
        } 
    }

    ilog("Number of base CPUs for this node is %d \n", num_base_cpus);

    /* get the number of cpus */            
    if ((pFile = fopen(cpu_info, "r")) == NULL){
        wlog("failed to open: /proc/cpuinfo \n");
    }

    else {

        /* Parse per-cpu hi-resolution scheduling stats */
        while (memset(cpu_line, 0, sizeof(cpu_line)) && (fgets((char*) &cpu_line, sizeof(cpu_line), pFile) != NULL)) {

            sscanf(cpu_line, "%19s %*s %*s", processor);
            if (strcmp(processor, "processor") == 0) {
                num_cpus++;   
            }           
        }      
        fclose(pFile);       
    }

    ilog("Number of CPUs for this node is %d \n", num_cpus);
}

/*****************************************************************************
 *
 * Name    : calculate_linux_usage
 *
 * Purpose : Calculate the cpu usage for Linux cards: controller, compute, storage 
 * The calculation runs as a delta.  The first time the function is called no 
 * valid cpu calculation occurs.  From the second time onwards, the cpu uasge is
 * calculated by taking the delta from the previous time the function was called 
 *
 *****************************************************************************/
int calculate_linux_usage( resource_config_type * ptr ) 
{

    double delta_seconds; 
    unsigned long long cpu_occupancy[num_cpus];
    unsigned long long cpu_delta_time;
    unsigned long long total_avg_cpu = 0;
    unsigned int counted_cpu=0;
    int rc;
    unsigned long long cpu_time[num_cpus];

    if (cpu_time_initial.size() == 0) {
        /* get the cpu time initially if the first cpu time does not exist */   
        rc = get_cpu_time( cpu_time );
        /* get the first timestamp */
        time(&t1);

        if (rc != PASS)
        {
            wlog("Failed get_cpu_time \n");
            return (FAIL);
        }

        for (int x=0; x<num_cpus; x++){
            /* add the pointer value to a vector for permanent storage */ 
            cpu_time_initial.push_back(cpu_time[x]);
            dlog2("cpu_time_initial for Cpu%d set to %llu \n", x, cpu_time[x]);
        }
        ptr->resource_value = 0;
    }
    else {  
        /* get the later cpu time if the first cpu time exists */ 
        rc = get_cpu_time( cpu_time ); 

        if (rc != PASS)
        {
            wlog("Failed get_cpu_time \n");
            return (FAIL);
        }

        /* get the later timestamp */ 
        time(&t2);       
         
        for (int x=0; x<num_cpus; x++){
            /* add the pointer value to a vector for permanent storage */ 
            cpu_time_later.push_back(cpu_time[x]);
            dlog2("cpu_time_later for Cpu%d %llu \n", x, cpu_time[x]);
        }

        delta_seconds = difftime(t2, t1);

        if (num_base_cpus == 0)
        {
            /*this is a controller or storage node there are no vswitch or pinned cpus */ 
            for (int j=0; j<num_cpus; j++)
            {
                /* calculate the cpu runtime for the specific cpu core */
                dlog("Cpu%d delta_seconds: %f\n",j, delta_seconds);
                dlog("Cpu%d later cycles: %llu , early cycles: %llu \n", j, cpu_time_later.at(j), cpu_time_initial.at(j));

                cpu_delta_time = ((cpu_time_later.at(j) - cpu_time_initial.at(j)) / 1000000 /delta_seconds);     
                cpu_occupancy[j] = (100*(cpu_delta_time))/1000;

                dlog("Cpu%d cpu_delta_time: %llu\n",j, cpu_delta_time);
                dlog("Cpu%d cpu_occupancy: %llu\n",j, cpu_occupancy[j]);

                total_avg_cpu += cpu_occupancy[j];
                counted_cpu++;                    
            }
        } else {
            /* this is a compute node, do not include vswitch or pinned cpus in calculation */
            for (int j=0; j<num_base_cpus; j++)
            {   
                /* calculate the cpu runtime for the specific cpu core */
                dlog("Cpu%d delta_seconds: %f\n", included_cpu[j], delta_seconds);
                dlog("Cpu%d later cycles: %llu , early cycles: %llu \n", included_cpu[j], cpu_time_later.at(included_cpu[j]), cpu_time_initial.at(included_cpu[j]));

                cpu_delta_time = ((cpu_time_later.at(included_cpu[j]) - cpu_time_initial.at(included_cpu[j])) / 1000000 /delta_seconds);     
                cpu_occupancy[j] = (100*(cpu_delta_time))/1000;

                dlog("Cpu%d cpu_delta_time: %llu\n", included_cpu[j], cpu_delta_time);
                dlog("Cpu%d cpu_occupancy: %llu\n", included_cpu[j], cpu_occupancy[j]);

                total_avg_cpu += cpu_occupancy[j];
                counted_cpu++;                    
            }
        }

        dlog("total_avg_cpu: %llu , counted_cpu: %d \n", total_avg_cpu, counted_cpu);

        ptr->resource_value = (double) (total_avg_cpu / counted_cpu);
        /* clear the old cpu times and set the current times as the old times */
        cpu_time_initial.clear();
        for (int x=0; x<num_cpus; x++){
            /* copy the current cpu times to the earlier ones */
            cpu_time_initial.push_back(cpu_time_later.at(x));
        }
        /* clear the current cpu times as they will be updated the next time around */
        cpu_time_later.clear();
        /* set the previous time as the current time */
        t1 = t2;

        #define LINUX_CPU_LOG_VALUE_STEP (10)
        if ( log_value ( ptr->resource_value,
                         ptr->resource_prev,
                         LINUX_CPU_LOG_VALUE_STEP ))
        {
            plog("%s: %.2f%% (average)\n", ptr->resource, ptr->resource_value);
        }
    }

    return (PASS);
}

/* Read the node UUID from the: /etc/platform/platform.conf file */
void _readUUID ()
{
    FILE * pFile;
    const char *platformFile = "/etc/platform/platform.conf";

    pFile = fopen (platformFile , "r");
    if (pFile != NULL) {
        ifstream fin( platformFile );
        string line;

        while( getline( fin, line ) ) {
            /* process each line */
            if( line.find ("UUID=") != string::npos ) {
                stringstream ss( line );
                getline( ss, hostUUID, '=' ); // token = string before =
                getline( ss, hostUUID, '=' ); // token = string after =     
            }        
        }                                     
        fclose (pFile);         
    }    
}

/*****************************************************************************
 *
 * Name    : _load_rmon_interfaces
 *
 * Purpose : Update the monitored network interfaces from the:
 * /etc/plaform/interfaces file 
 *****************************************************************************/
void _load_rmon_interfaces ()
{

    rmon_socket_type   * sock_ptr = rmon_getSock_ptr (); 

    /* initialize interface monitoring */ 
    for ( int j = 0 ; j < _rmon_ctrl_ptr->interface_resources; j++ )
    {   
        init_physical_interfaces ( &interface_resource_config[j] );
    }

    for (int i=0; i<_rmon_ctrl_ptr->interface_resources; i++)
    {
        if ( interface_resource_config[i].interface_used == true )
        {
            /* set the link state for all the primary physical interfaces */ 
            if ( get_link_state ( sock_ptr->ioctl_sock, interface_resource_config[i].interface_one, &interface_resource_config[i].link_up_and_running ) )
            {
                interface_resource_config[i].link_up_and_running = false ;
                interface_resource_config[i].resource_value = INTERFACE_DOWN;
                wlog ("Failed to query %s operational state ; defaulting to down\n", interface_resource_config[i].interface_one) ;
            }
            else
            {
                ilog ("%s link is: %s\n", interface_resource_config[i].interface_one, interface_resource_config[i].link_up_and_running ? "Up" : "Down" );
                if (interface_resource_config[i].link_up_and_running)
                {
                    interface_resource_config[i].resource_value = INTERFACE_UP;
                }
                else 
                {
                    interface_resource_config[i].resource_value = INTERFACE_DOWN;
                    interface_resource_config[i].failed = true;
                }
            }
            if (interface_resource_config[i].lagged == true)
            {
                /* set the link state for all the lagged physical interfaces */ 
                if ( get_link_state ( sock_ptr->ioctl_sock, interface_resource_config[i].interface_two, &interface_resource_config[i].link_up_and_running ) )
                {
                    interface_resource_config[i].link_up_and_running = false ;
                    wlog ("Failed to query %s operational state ; defaulting to down\n", interface_resource_config[i].interface_two) ;
                }
                else
                {
                    ilog ("%s link is: %s\n", interface_resource_config[i].interface_two, interface_resource_config[i].link_up_and_running ? "Up" : "Down" );
                    if (interface_resource_config[i].link_up_and_running)
                    {
                        interface_resource_config[i].resource_value_lagged = INTERFACE_UP;
                    }
                    else 
                    {
                        interface_resource_config[i].resource_value_lagged = INTERFACE_DOWN;
                        interface_resource_config[i].failed = true;
                    }
                }
            }
        }
    }
    
    for ( int j = 0 ; j < _rmon_ctrl_ptr->interface_resources; j++ )
    {   
        interface_alarming_init ( &interface_resource_config[j] );
    }
}

/*****************************************************************************
 *
 * Name    : resource_stall_monitor
 *
 * Purpose : Detects stalls in the resource monitoring threads
 ******************************************************************************/
int resource_stall_monitor ( resource_config_type * ptr, pid_t tid, pid_t pid)
{
    #define MAX_SCHEDSTAT_LEN (128)
    char file_path [MAX_FILENAME_LEN] ;
    char schedstat [MAX_SCHEDSTAT_LEN] ;
    FILE * fp ;
    int rc = PASS;
    unsigned long long nr_switches_old = t_data.nr_switches_count;

    snprintf ( &file_path[0], MAX_FILENAME_LEN, "/proc/%d/task/%d/schedstat", pid, tid );
    fp = fopen (file_path, "r" );
    if ( fp )
    {
        /* check to see if the thread is stalled */ 
        memset ( schedstat, 0 , MAX_SCHEDSTAT_LEN );
        if ( fgets ( &schedstat[0], MAX_SCHEDSTAT_LEN, fp) != NULL)
        {      
            if ( sscanf ( schedstat, "%*s %*s %llu", &t_data.nr_switches_count) >= 1 )
            {
                dlog ("%s: nr_count: %llu, nr_count_old: %llu \n", ptr->resource, t_data.nr_switches_count, nr_switches_old);
                if ((nr_switches_old != t_data.nr_switches_count) && (ptr->failed))
                {
                    /* Clear the stall monitor alarm */ 
                    ilog("%s thread has unstalled \n", ptr->resource);
                    ptr->sev = SEVERITY_CLEARED;
                    t_data.nr_switches_count = 0;
                    resourceStageChange ( ptr, RMON_STAGE__FINISH ); 
                }
            }
            else
            {
                wlog ("Failed to get schedstat from (%s)\n", file_path);
                rc = FAIL;
            }
        }
        else
        {
            wlog ("failed to read from (%s)\n", file_path );
            rc = FAIL;
        }
        fclose(fp);
    }
    else
    {
        wlog ("Failed to open (%s)\n", file_path);
        rc = FAIL;
    }

    if ((((nr_switches_old == t_data.nr_switches_count) && (ptr->sev != SEVERITY_MAJOR))) || 
        (rc == FAIL))
    {
        /* thread has stalled raise alarm */
        elog("%s thread has stalled \n", ptr->resource);
        ptr->sev = SEVERITY_MAJOR;
        ptr->failed = true;
        resourceStageChange ( ptr, RMON_STAGE__MANAGE ); 
    }

    return rc;
}

/*****************************************************************************
 *
 * Name    : check_instance_file
 *
 * Purpose : Thread spawned by rmon to check if: /etc/nova/instances is mounted.
 * It needs to be a thread because of NFS hang issues.
 *
 *****************************************************************************/
void *check_instance_file(void *threadarg)
{
    struct thread_data *res_data;
    FILE * pFile;
    FILE *testFile;
    string line;
    struct stat p;
    const char *instances_dir = "/etc/nova/instances";
    const char *test_file = "/etc/nova/instances/.rmon_test";

    res_data = (struct thread_data *) threadarg;
   
    pthread_mutex_lock(&lock);
    res_data->thread_running = true; 
    res_data->tid = syscall(SYS_gettid);
    pthread_mutex_unlock(&lock);

    dlog("%s process id: %d, thread id: %d \n", res_data->resource->resource, res_data->pid, res_data->tid);
    res_data->resource_usage = NOT_MOUNTED;
    pFile = fopen (MOUNTS_DIR , "r");

    /* query /proc/mounts and make sure the /etc/nova/instances file system is there */ 
    if (pFile != NULL)    
    {
        ifstream fin( MOUNTS_DIR );
        while( getline( fin, line ) )
        {
            /* process each line */
            if( line.find (instances_dir) != string::npos ) 
            {
                /* the mount is present */
                res_data->resource_usage = MOUNTED;
                break;
            }
        }
        fclose (pFile);
    }

    if ( res_data->resource_usage == MOUNTED )
    {
       /* put the test file in and check that it is accessible */ 
       testFile = fopen(test_file, "w");
       if (testFile != NULL)
       {
            fclose (testFile);
            if( remove( test_file ) != 0 )
            {
                elog("Failure in removing rmond test file: %s \n", test_file);
            }
       }
       else 
       {
           res_data->resource_usage = NOT_MOUNTED;
       }
    }

    if (res_data->resource_usage == NOT_MOUNTED)
    {
        /* fail the resource */ 
        stat (COMPUTE_CONFIG_PASS, &p);       
        if ((p.st_ino != 0 ) || (p.st_dev != 0)) 
        {
            pthread_mutex_lock(&lock);
            if (res_data->resource->sev != SEVERITY_MAJOR)
            {
                res_data->resource->sev = SEVERITY_MAJOR;
                res_data->resource->failed = true;
                resourceStageChange ( res_data->resource, RMON_STAGE__MANAGE ); 
            }
            pthread_mutex_unlock(&lock);
        }
    }
    else if ((res_data->resource_usage == MOUNTED) && (res_data->resource->failed))
    {
        pthread_mutex_lock(&lock);
        res_data->resource->sev = SEVERITY_CLEARED;
        resourceStageChange ( res_data->resource, RMON_STAGE__FINISH ); 
        pthread_mutex_unlock(&lock);
    }

    pthread_mutex_lock(&lock);
    res_data->thread_running = false;
    pthread_mutex_unlock(&lock);

   pthread_exit(NULL);
}


/*****************************************************************************
 *
 * Name    : postPMs
 *
 * Purpose : create samples for each resource in Ceilometer    
 *
 *****************************************************************************/
int _postPMs ()
{
    char meta_data[MAX_LEN];
    if ( hostUUID.empty() )
    {
        /* keep trying to get the host UUID if it is not present */
        _readUUID();
    }

    if ( !hostUUID.empty() )
    {
        // indicate the platform hostname as metadata for all resources
        char *hoststring = strdup(_rmon_ctrl_ptr->my_hostname);
        if (hoststring) {
            char *host = strtok(hoststring,"=");
            host = strtok(NULL, "=");
            snprintf(&meta_data[0], MAX_LEN, "{\"host\":\"%s\"}", host);
            free(hoststring);
        }

        for ( int i = 0 ; i < _rmon_ctrl_ptr->resources ; i++ )
        {
            ostringstream strs;
            strs << resource_config[i].resource_value  ;
            string res_val = strs.str();

            if (strcmp(resource_config[i].resource, CPU_RESOURCE_NAME) == 0) {
                /* cpu resource pm */
                generate_ceilometer_pm ( hostUUID, "platform.cpu.util", "delta", "%", 
                                         res_val, string(meta_data) );
            }
            else if (strcmp(resource_config[i].resource, MEMORY_RESOURCE_NAME) == 0) {
                /* memory resource pm */
                if (resource_config[i].percent == 1) {
                    generate_ceilometer_pm ( hostUUID, "platform.mem.util", "delta", "%", 
                                             res_val, string(meta_data) );
                } else {
                    generate_ceilometer_pm ( hostUUID, "platform.mem.util", "gauge", "MB", 
                                             res_val, string(meta_data) );
                }
            }
            else if (strcmp(resource_config[i].resource, FS_RESOURCE_NAME) == 0) {
                /* filesystem resource pm */
                if (resource_config[i].percent == 1) {   
                    generate_ceilometer_pm ( hostUUID, "platform.fs.util", "delta", "%", 
                                             res_val, string(meta_data) );
                } else {
                    generate_ceilometer_pm ( hostUUID, "platform.fs.util", "gauge", "MB", 
                                             res_val, string(meta_data) );
                }     
            }
        } // end of resource loop
    }
    return (PASS);
}

/*****************************************************************************
 *
 * Name    : _get_events
 *
 * Purpose : query each resource and extract the required usage values
 *
 *****************************************************************************/

extern bool is_cpe ( void );
extern bool is_worker ( void );

void _get_events (void)
{
    int rc;
    string v_cpu;
    FILE * pFile;

    if ( _rmon_ctrl_ptr->clients == 0 )
    {
        wlog ("Monitoring with no registered clients\n");
    }

    for ( int i = 0 ; i < _rmon_ctrl_ptr->resources ; i++ )
    {
        const char *resource = resource_config[i].resource;
        ilog_throttled ( resource_config[i].resource_monitor_throttle, 120,
                         "Monitoring '%s'\n",
                         resource );

        if (strcmp(resource, CPU_RESOURCE_NAME) == 0)
        {
            /* linux cards: controller, compute and storage cpu utilization */
            rc = calculate_linux_usage( &resource_config[i] );
            if ( rc == PASS )
            {
                /* get if the resource is failed to be used by resource handler */
                process_failures ( &resource_config[i]);
            }
        }
        else if (!strcmp(resource, V_CPU_RESOURCE_NAME) ||
                 !strcmp(resource, V_MEMORY_RESOURCE_NAME) ||
                 !strcmp(resource, V_PORT_RESOURCE_NAME) ||
                 !strcmp(resource, V_INTERFACE_RESOURCE_NAME) ||
                 !strcmp(resource, V_LACP_INTERFACE_RESOURCE_NAME) ||
                 !strcmp(resource, V_OVSDB_RESOURCE_NAME) ||
                 !strcmp(resource, V_NETWORK_RESOURCE_NAME) ||
                 !strcmp(resource, V_OPENFLOW_RESOURCE_NAME))
        {
            /* ensure that configuration has completed before computing
             * vswitch resource utilization */
            if ( !daemon_is_file_present ( CONFIG_COMPLETE_WORKER ) )
                continue ;

            pFile = fopen (COMPUTE_VSWITCH_DIR , "r");
            if (pFile != NULL){
                fclose (pFile);
            }
            else
            {
                wlog ("%s failed to open %s\n", resource, COMPUTE_VSWITCH_DIR);
            }
        }
        else if (strstr(resource_config[i].resource, V_MEMORY_RESOURCE_NAME) != NULL)
        {
            /* vswitch memory with specific sockets */
            /* skip these ones as they are already taken care of above */
        }
        else if(strcmp(resource, REMOTE_LOGGING_RESOURCE_NAME) == 0)
        {
            rmonHdlr_remotelogging_query(&resource_config[i]);
        }
        else if (strcmp(resource, INSTANCE_RESOURCE_NAME) == 0)
        {
            /* do not perform this check if we are not on a compute node.
             * its not valid on storage not combo load */
            if ( !is_worker () )
                continue ;

            if ( !daemon_is_file_present ( CONFIG_COMPLETE_WORKER ) )
                continue ;

            /* nova instances mount check */    
            pFile = fopen (COMPUTE_VSWITCH_DIR , "r");
            if (pFile != NULL)
            {
                rc = PASS ;
                pthread_mutex_lock(&lock);
                if (!t_data.thread_running)          
                {
                    pthread_attr_t attr ;
                    t_data.resource = &resource_config[i];
                    pthread_attr_init (&attr);
                    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                    /* launch a thread to monitor the /etc/nova/instances mount */
                    rc = pthread_create(&thread, &attr, check_instance_file, (void *) &t_data);
                    if (rc) 
                    {
                        elog("%s ERROR; return code from pthread_create() is %d\n", 
                             resource, rc);
                    }
                    pthread_attr_destroy (&attr);
                }
                else 
                {
                    /* If thread is still running check that it is not stalled */ 
                    resource_stall_monitor(&resource_config[i], t_data.tid, t_data.pid);
                }
                pthread_mutex_unlock(&lock);
                fclose (pFile);
            }
        }
        else if (strcmp(resource, MEMORY_RESOURCE_NAME) == 0) {
            /* memory utilization */         
            calculate_memory_usage(i);
            /* get if the resource is failed to be used by resource handler */
            if (resource_config[i].percent == PERCENT_USED) {
                process_failures ( &resource_config[i]);
            } else {
                process_failures_absolute ( &resource_config[i]);
            }

        }
        else if ((strcmp(resource, V_CINDER_THINPOOL_RESOURCE_NAME) == 0) &&
                 (resource_config[i].alarm_status == ALARM_ON)) {
            /* virtual thin pool space utilization */
            rc = calculate_virtual_space_usage(i, V_CINDER_THINPOOL_RESOURCE_NAME);
            /* only check resource for fail and clear if it is active */
            if (rc == PASS) {
                if (resource_config[i].percent == PERCENT_USED) {
                    /* get if the resource is failed to be used by resource handler */
                    process_failures (&resource_config[i]);
                } else {
                    process_failures_absolute (&resource_config[i]);
                }
            }
        }
        else if ((strcmp(resource, V_NOVA_THINPOOL_RESOURCE_NAME) == 0) &&
                 (resource_config[i].alarm_status == ALARM_ON)){
            /* do not perform this check if we are not on a compute node.
             * its not valid on storage not combo load */
            if ( !is_worker () && !is_cpe () )
                continue ;

            if ( !daemon_is_file_present ( CONFIG_COMPLETE_WORKER ) )
                continue ;

            /* virtual thin pool space utilization */
            rc = calculate_virtual_space_usage(i, V_NOVA_THINPOOL_RESOURCE_NAME);
            /* only check resource for fail and clear if it is active */
            if (rc == PASS) {
                if (resource_config[i].percent == PERCENT_USED) {
                    /* get if the resource is failed to be used by resource handler */
                    process_failures (&resource_config[i]);
                } else {
                    process_failures_absolute (&resource_config[i]);
                }
            }
        }
        else if (strcmp(resource, FS_RESOURCE_NAME) == 0) {
            /* file system utilization */
            /* do nothing as we calculate individual file system location and not the total */
        }
        else {
            /* dynamic file system resource */ 

            pthread_mutex_lock(&lock);
            if ((resource_config[i].alarm_status == ALARM_ON) && (modifyingResources == false))
            {
                /* only calculate the resource usage if file systems aren't being added */
                calculate_fs_usage( &resource_config[i] );

                /* only check resource for fail and clear if it is active */
                if (resource_config[i].percent == PERCENT_USED) {
                    /* get if the resource is failed to be used by resource handler */
                    process_failures ( &resource_config[i]);
                } else {
                    process_failures_absolute ( &resource_config[i]);
                }
            }
            else if ((resource_config[i].alarm_status == ALARM_OFF) && (modifyingResources == false) 
                    && (resource_config[i].failed == true))
            {
                //send a clear message
                send_clear_msg(i);

                // we need to clear the resource's alarm if there was any set for this resource
                clear_alarm_for_resource(&resource_config[i]);
            }
            pthread_mutex_unlock(&lock);
        }
    } // end of rmon resources

    /*
     * since interface resources are event based resourcs, i.e.
     * they would only be called when netlink socket reports a 
     * link state event, we need to run a periodic audit on them
     * as part of RMON event audit. 
     * This audit shall resend interface degrade statuses to maintaince
     * if interface is in failed state
     */
    for ( int j = 0; j < _rmon_ctrl_ptr->interface_resources; j++ )
    {
        if ( interface_resource_config[j].interface_used && 
             interface_resource_config[j].failed == true ) 
        {
            send_interface_msg ( &interface_resource_config[j], 
                                _rmon_ctrl_ptr->clients );
        }
    }
}

int kill_running_process ( int pid )
{
    int result = kill ( pid, 0 );
    if ( result == 0 )
    {
        result = kill ( pid, SIGKILL );
        if ( result == 0 )
        {
            wlog ("NTP process kill succeeded (%d)\n", pid );
        }
        else
        {
            elog ("NTP process kill failed (%d)\n", pid );
        }
    }
    return (PASS);
}

/* SIGCHLD handler support - for waitpid */
static bool rmon_sigchld_received = false ;
void daemon_sigchld_hdlr ( void )
{
    dlog("Received SIGCHLD ...\n");

    int status = 0;
    pid_t tpid = 0;

    while ( 0 < ( tpid = waitpid ( -1, &status, WNOHANG | WUNTRACED )))
    {
        dlog("NTP query script returned WIFEXITED:%d and WEXITSTATUS:%d for pid:%d\n", WIFEXITED(status), WEXITSTATUS(status), tpid);

        if (tpid == ntp_child_pid)
        {
            rmon_sigchld_received = true ;

            /* no need to wait for a timeout since we got a response, force a ring */
            rmonTimer_ntp.ring = true;
            ntp_status = WEXITSTATUS(status);
        }
        else
        {
            dlog ("PID:%d lookup failed ; reaped likely after timeout\n", tpid );
            ntp_status = NTP_ERROR;
        }
    }
}

int ntp_audit_handler ( )
{
    if ( ntp_stage >= NTP_STAGE__STAGES )
    {
        wlog ("Invalid ntp_stage (%d) ; correcting\n", ntp_stage );
        ntpStageChange ( NTP_STAGE__BEGIN);
    }

    switch ( ntp_stage )
    {
        // First state
        case NTP_STAGE__BEGIN:
        {
            mtcTimer_start  ( rmonTimer_ntp, rmon_timer_handler, _rmon_ctrl_ptr->ntp_audit_period );
            dlog ("Start NTP period timer (%d secs) %p\n", _rmon_ctrl_ptr->ntp_audit_period, rmonTimer_ntp.tid);
            ntpStageChange ( NTP_STAGE__EXECUTE_NTPQ );
            break ;
        }

        // Execute the ntpq command
        case NTP_STAGE__EXECUTE_NTPQ:
        {
            if ( rmonTimer_ntp.ring == true ) //wake up from NTP period
            {
                ntp_status = PASS;
                mtcTimer_start ( rmonTimer_ntp, rmon_timer_handler, _rmon_ctrl_ptr->ntpq_cmd_timeout );
                dlog ("Start NTPQ command timer (%d secs) %p\n", _rmon_ctrl_ptr->ntpq_cmd_timeout, rmonTimer_ntp.tid);

                // Execute the ntpq command
                int rc = query_ntp_servers();
                if (rc != PASS)
                {
                   elog ("NTP execute_status_command returned a failure (%d)\n", rc);
                   ntp_status = NTP_ERROR;
                }

                ntpStageChange ( NTP_STAGE__EXECUTE_NTPQ_WAIT );
            }
            break ;
        }

        // Wait for the ntpq command to finish and process results
        case NTP_STAGE__EXECUTE_NTPQ_WAIT:
        {
            // Give the command time to execute. The daemon_sigchld_hdlr will force
            // a ring when the command execute successfully or returns a failure
            if ( ( rmonTimer_ntp.ring == true) || (ntp_status == NTP_ERROR ) )
            {
                // Stop the NTP timer if still running
                if ( rmonTimer_ntp.tid )
                {
                    mtcTimer_stop ( rmonTimer_ntp );
                }

                if (( !rmon_sigchld_received) || (ntp_status == NTP_ERROR))
                {
                    if ( rmon_sigchld_received == false )
                    {
                        elog ("NTPQ command execution timed out (pid:%d)\n", ntp_child_pid );
                    }

                    elog ("NTPQ returned an execution failure (rc:%d) (pid:%d)\n", ntp_status, ntp_child_pid);
                    if (ntp_child_pid != 0)
                    {
                       kill_running_process ( ntp_child_pid );
                    }
                }
                else
                {
                    dlog ("NTPQ command was successful ; analyzing results\n");
                    ntp_query_results(ntp_status);
                }

                ntpStageChange ( NTP_STAGE__BEGIN );
                ntp_child_pid = 0;
                rmon_sigchld_received = false;
            }
            break;
        }

        default:
        {
            elog ("NTP invalid ntp_stage (%d)\n", ntp_stage );

            /* Default to first state for invalid case. there is an issue then it will be detected */
            ntpStageChange ( NTP_STAGE__BEGIN );
        }
    }
    return (PASS);
}


/*****************************************************************************
 *
 * Name    : rmon_service
 *
 * Purpose : main loop for monitoring resources 
 *
 *****************************************************************************/
void rmon_service (rmon_ctrl_type * ctrl_ptr)
{
    fd_set readfds;
    struct timeval waitd; 
    std::list<int> socks;
    rmon_socket_type   * sock_ptr = rmon_getSock_ptr ();

    /* initialize FM handler */
    rmon_fm_init();

    /* ignore SIGPIPE on swacts */ 
    signal(SIGPIPE, SIG_IGN);

    /* initialize the memory accounting: either Strict or OOM */
    init_memory_accounting();    
    /* initialize the cpu monitoring defaults */
    cpu_monitoring_init();
    _readUUID();
    /* Start an event timer for the interval of the resources being monitored */ 
    ilog ("Starting 'Event Monitor' timer (%d secs) \n", ctrl_ptr->audit_period);
    mtcTimer_start ( rmonTimer_event, rmon_timer_handler, 1 );

    ilog ("Starting 'PM Monitor' timer (%d secs) \n", ctrl_ptr->pm_period);
    mtcTimer_start ( rmonTimer_pm, rmon_timer_handler,ctrl_ptr->pm_period);

    if (is_controller())
    {
       ntp_stage = NTP_STAGE__BEGIN;
    }

    /* Get an Authentication Token */
    ilog ("%s Requesting initial token\n", ctrl_ptr->my_hostname );
    tokenEvent.status = tokenUtil_new_token ( tokenEvent, ctrl_ptr->my_hostname );
    if ( tokenEvent.status != PASS )
    {
        elog ("Failed to get authentication token (%d)\n", tokenEvent.status);
        if ( tokenEvent.base )
        {
            slog ("%s token base:%p\n",
                      ctrl_ptr->my_hostname,
                      tokenEvent.base);
        }
    }

    /* service all the register and deregister requests in the queue */
    rmon_alive_notification( _rmon_ctrl_ptr->clients );

    ilog ("registered clients: %d\n", _rmon_ctrl_ptr->clients);

#ifdef WANT_FS_MONITORING

    /* Initialize the resource specific configuration */
    for (int j=0; j<_rmon_ctrl_ptr->resources; j++)
    {
        if ( strcmp(resource_config[j].resource, FS_RESOURCE_NAME) == 0 ) {
            /* determine whether percent or absolute values are used */
            /* determine if virtual thin pool memory usage alarm should be on or off */
            fs_percent = resource_config[j].percent;
        }
    }
    /* add the static filesystem resources */
    process_static_fs_file();
    /* initialize the resource alarms */
    for (int j=0; j<_rmon_ctrl_ptr->resources; j++)
    {
        rmon_alarming_init ( &resource_config[j] );
    }

    /* add any dynamic resources from before */
    add_dynamic_fs_resource(false);
#else
    ilog("static filesystem monitoring moved to collectd\n");
#endif

    /* Clear any stale dynamic alarms that can be caused by dynamic resources.                           */
	/* An alarm become stale for example if it was raised against a local volumn group (lvg) and         */
	/* later on the lvg is deleted. The node will come up and the lvg resource will not longer exist and */
	/* it's related alarms not refreshed. Dynamic alarms are any alarms which it's resource can be       */
	/* provisioned.                                                                                      */

    AlarmFilter alarmFilter;
	unsigned int max_alarms=75;
	char alarm_to_search[FM_MAX_BUFFER_LENGTH];

	fm_alarm_id alarm_id;
	snprintf(alarm_id, FM_MAX_BUFFER_LENGTH, FS_ALARM_ID);

    SFmAlarmDataT *active_alarms = (SFmAlarmDataT*) calloc (max_alarms, sizeof (SFmAlarmDataT));
    if (active_alarms != NULL)
    {
        /* get all the current alarms with id of FS_ALARM_ID which are alarms related to the file system */
        /* fm_get_faults_by_id returns the number of alarms found */
        if (fm_get_faults_by_id( &alarm_id, active_alarms, &max_alarms) == FM_ERR_OK)
        {
            bool found = false;
            for ( unsigned int i = 0; i < max_alarms; i++ )
            {
                /* only get the 100.104 alarms */
                if ((strncmp((active_alarms+i)->alarm_id, FS_ALARM_ID, sizeof((active_alarms+i)->alarm_id)) == 0)
                     && (strstr((active_alarms+i)->entity_instance_id, _rmon_ctrl_ptr->my_hostname) != NULL) )
                {
                    found = false;
                    for (int j=0; j<_rmon_ctrl_ptr->resources; j++)
                    {
                        /* since we build the entity_instance_id with multiple data we must recreate it */
                        snprintf(alarm_to_search, FM_MAX_BUFFER_LENGTH,  "%s.volumegroup=%s", _rmon_ctrl_ptr->my_hostname, resource_config[j].resource);
                        if (strncmp(alarm_to_search, (active_alarms+i)->entity_instance_id, sizeof(alarm_to_search)) == 0)
                        {
                            found = true;
                            break;
                        }

                        snprintf(alarm_to_search, FM_MAX_BUFFER_LENGTH,  "%s.filesystem=%s", _rmon_ctrl_ptr->my_hostname, resource_config[j].resource);
                        if (strncmp(alarm_to_search, (active_alarms+i)->entity_instance_id, sizeof(alarm_to_search)) == 0)
                        {
                            found = true;
                            break;
                        }

                        // We found the resource but lets check if the alarm is enable for it, if it's not
                        // we want to clear that alarm
                        if (found)
                        {
                           if (resource_config[j].alarm_status == ALARM_OFF)
                           {
                              found = false;
                           }
                        }
                    }
                    if (!found)
                    {
                        /* the alarm did not match any current resources so let's clear it */
                        snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, (active_alarms+i)->alarm_id );
                        snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, (active_alarms+i)->entity_instance_id);

                        ilog ("Clearing stale alarm %s for entity instance id: %s", (active_alarms+i)->alarm_id, (active_alarms+i)->entity_instance_id);

                        if (rmon_fm_clear(&alarmFilter) != FM_ERR_OK)
                        {
                            wlog ("Failed to clear stale alarm for entity instance id: %s", (active_alarms+i)->entity_instance_id);
                        }
                    }
                }
            }
        }
        free(active_alarms);
    }
    else
    {
        elog ("Failed to allocate memory for clearing stale dynamic alarms");
    }

    if (( sock_ptr->ioctl_sock = open_ioctl_socket ( )) <= 0 )
    {
        elog ("Failed to create ioctl socket");
    }

    /* Not monitoring address changes RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR */  
    if (( sock_ptr->netlink_sock = open_netlink_socket ( RTMGRP_LINK )) <= 0 )
    {
        elog ("Failed to create netlink listener socket");
    }

    /* load the current interfaces for monitoring */
     _load_rmon_interfaces();

    socks.clear(); 
    socks.push_front (sock_ptr->rmon_tx_sock);  
    socks.push_front (sock_ptr->netlink_sock);  
    socks.sort();

    for (;;) {
        /* Accomodate for hup reconfig */
        FD_ZERO(&readfds);
        FD_SET(sock_ptr->rmon_tx_sock, &readfds);
        FD_SET(sock_ptr->netlink_sock, &readfds);
        waitd.tv_sec  = 0;
        waitd.tv_usec = SOCKET_WAIT ;
        tokenUtil_log_refresh ();

        /* This is used as a delay up to select timeout ; SOCKET_WAIT */
        select( socks.back()+1, &readfds, NULL, NULL, &waitd);     
        if (FD_ISSET(sock_ptr->rmon_tx_sock, &readfds)) 
        {  
            _rmon_ctrl_ptr->clients = rmon_service_inbox  ( _rmon_ctrl_ptr->clients );
        }
        else if (FD_ISSET(sock_ptr->netlink_sock, &readfds)) 
        {
            dlog ("netlink socket fired\n");
            if ( service_interface_events ( sock_ptr->netlink_sock, sock_ptr->ioctl_sock ) != PASS )
            {
                elog ("service_interface_events failed \n");
            }
        }

        /* Manage the health of the resources */
        if ( rmonTimer_event.ring == true )
        {          
            // restart the audit period timer 
            mtcTimer_start ( rmonTimer_event, rmon_timer_handler, ctrl_ptr->audit_period );
            /* service all the register and deregister requests in the queue */
            rmon_alive_notification( _rmon_ctrl_ptr->clients );
            _get_events ( );    
        }

        if ( rmonTimer_pm.ring == true )
        {
            mtcTimer_start ( rmonTimer_pm, rmon_timer_handler, ctrl_ptr->pm_period );
            tokenUtil_token_refresh ( tokenEvent, ctrl_ptr->my_hostname );
            _postPMs();
        }

        /* loop through all the resource timers waiting for a ring */ 
        for ( int j = 0 ; j < ctrl_ptr->resources ; j++ )
        {
            if (resource_config[j].failed == true) {
                /* Run the FSM for this failed resource */
                resource_handler ( &resource_config[j]);
            }   
        }         

        /* loop through all the interface resources */ 
        for ( int j = 0 ; j < ctrl_ptr->interface_resources ; j++ )
        {
            if (interface_resource_config[j].failed == true) {
                /* Run the FSM for this failed  interface */
                interface_handler ( &interface_resource_config[j] );
            }   
        }

        /* loop thorough all the LVM thinpool metadata resources waiting for a ring */
        for ( int j = 0; j < ctrl_ptr->thinmeta_resources; j++ )
        {
            if (thinmeta_resource_config[j].critical_threshold) {
                // a threshold of 0 disables monitoring
                if (thinmetatimer[j].ring == true) {
                    // restart the audit period timer
                    mtcTimer_start ( thinmetatimer[j], rmon_timer_handler,
                                     thinmeta_resource_config[j].audit_period );
                    dlog("%s/%s running audit (resource index: %i)",
                         thinmeta_resource_config[j].vg_name,
                         thinmeta_resource_config[j].thinpool_name, j)
                    /* Handle resource */
                    int k;
                    for (k = THINMETA_FSM_RETRY; k > 0; k--) {
                        // call again the FSM in case it instructs us to RETRY
                        if(thinmeta_handler(&thinmeta_resource_config[j]) != RETRY) {
                            break;
                        }
                    }
                    if (k == 0) {
                        dlog("%s/%s too many state changes in FSM at: %i stage!",
                             thinmeta_resource_config[j].vg_name,
                             thinmeta_resource_config[j].thinpool_name,
                             thinmeta_resource_config[j].stage);
                    }
                }
            }
        }

        /* handle RMON FM interface */
        rmon_fm_handler ();

        daemon_signal_hdlr ();
    }

}

/****************************************************************************
 *
 * Name       : log_value
 *
 * Purpose    : Log resource state values while avoiding log flodding for
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
bool log_value ( double & current, double & previous, int step )
{
    /* Support step override for debug purposes
     * Allows for more frequent logging */
    int _step = daemon_get_cfg_ptr()->log_step ;

    /* a lower value from the conf file takes precidence */
    if ( _step > step )
        _step = step ;

    if (( round(current) >= ( round(previous) + _step )) ||
        ( round(current) <= ( round(previous) - _step )))
    {
        previous = current ;
        return true ;
    }
    return false ;
}
