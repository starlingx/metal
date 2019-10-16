/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Process Monitor Service Handler
  */

#include <libgen.h>        /* dirname */

using namespace std;

#include "pmon.h"
#include "nodeEvent.h"     /* for ... set_inotify_watch, set_inotify_close */
#include "nodeTimers.h"    /* for ... mtcTimer_init                        */
#include "alarmUtil.h"     /* for ... alarmUtil_getSev_str                 */
#include "pmonAlarm.h"     /* for ... PMON_ALARM_ID__PMOND                 */

/* Preserve a local copy of a pointer to the control struct to
 * avoid having to publish a get utility prototype into pmon.h */
static pmon_ctrl_type * _pmon_ctrl_ptr = NULL ;

void pmon_set_ctrl_ptr ( pmon_ctrl_type * ctrl_ptr )
{
    /* Save the control pointer */
    _pmon_ctrl_ptr = ctrl_ptr ;
}

/* pmonTimer_audit - get_events periodic audit timer */
static struct mtc_timer pmonTimer_audit   ;
static struct mtc_timer pmonTimer_degrade ;
static struct mtc_timer pmonTimer_pulse   ;
static struct mtc_timer pmonTimer_hostwd  ;
static struct mtc_timer ptimer[MAX_PROCESSES] ;

/** List of config files */
std::list<string> config_files ;
std::list<string>::iterator string_iter_ptr ;

/* If there is an alarm in the list that matches one in the process list
 * then update that process with its severity and failed state.
 * If there is a process in the saved list that is not in the process list
 * then clear its alarm as it is no longer valid.
 */
void manage_process_alarms (  list<active_process_alarms_type> & _list,
                              process_config_type * const ptr,
                              int const processes );

static process_config_type process_config[MAX_PROCESSES] ;

/* lookup process control by index  and return its pointer if found.
 * Otherwise if not found return NULL */
process_config_type * get_process_config_ptr ( int index )
{
    if ( index < _pmon_ctrl_ptr->processes )
        return ( &process_config[index] );
    return ( NULL );
}

/* lookup process control by name and return its pointer if found.
 * Otherwise if not found return NULL */
process_config_type * get_process_config_ptr ( string process )
{
    if ( _pmon_ctrl_ptr )
    {
        for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
        {
            if ( process.compare(process_config[i].process) == 0 )
            {
                dlog ("%s process found\n", process.c_str());
                return (&process_config[i]);
            }
        }
    }
    wlog ("%s process not found in control list\n", process.c_str());
    return (NULL);
}

#define _MAX_LEN_ (MAX_FILE_SIZE*2)

/*******************************************************************
 *                   Process Dump Support                          *
 *******************************************************************
 *                                                                 *
 * Utilities that add specific config lines to the dump list       *
 *                                                                 *
 ******************************************************************/

/* Log nostname, ip, mac and pulse period */
void mem_log_ctrl ( pmon_ctrl_type * ptr )
{
    #define MAX_LEN 500
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "%s %s %s Pulse Rate:%d msecs\n",
               &ptr->my_hostname[0],
               ptr->my_address.c_str(),
               ptr->my_macaddr.c_str(),
               ptr->pulse_period );
    mem_log(str);
}

/* Log process specific controls */
void mem_log_process ( process_config_type * ptr )
{
    #define MAX_LEN 500
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "%-25s [%5d] %8s Restarts:%u Interval:%u Debounce:%u Startuptime:%u\n",
               ptr->process,
               ptr->pid,
               ptr->severity,
               ptr->restarts,
               ptr->debounce,
               ptr->interval,
               ptr->startuptime);
    mem_log(str);
}

/* Log process specific state */
void mem_log_pstate ( process_config_type * ptr )
{
    #define MAX_LEN 500
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "  Passive: %10s (%d) Failed:%s  Restart:%s  FCount:%2u  subFunc:%s  Severity:%s %s %s\n",
               get_pmonStage_str(ptr),
               ptr->stage,
               ptr->failed ? "true " : "false",
               ptr->restart ? "true " : "false",
               ptr->failed_cnt,
               ptr->subfunction ? ptr->subfunction : "None",
               alarmUtil_getSev_str(ptr->alarm_severity).c_str(),
               ptr->ignore ? "ignored" : "",
               ptr->stopped ? "stopped" : "" );
    mem_log(str);
}

/* Log process specific active monitor controls */
void mem_log_aconfig ( process_config_type * ptr )
{
    #define MAX_LEN 500
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "  Active : %10s (%d) Pulses:%2u  Seq:%2u  Period:%2u  Timeout:%2u  Thld:%2u %s\n",
              get_amonStage_str(ptr),
              ptr->active_stage,
              ptr->pulse_count,
              ptr->tx_sequence,
              ptr->period,
              ptr->timeout,
              ptr->threshold,
              ptr->waiting ? "... waiting" : "");
    mem_log(str);
}

/* Log process specific active monitor state */
void mem_log_astate ( process_config_type * ptr )
{
    #define MAX_LEN 500
    char str[MAX_LEN] ;
    snprintf (&str[0], MAX_LEN, "           Stats - Failed:%s  Count:%2u  b2bp:%2u  b2bc:%2u  rxer:%2u  txer:%2u  msge:%2u  msgp:%2u\n",
               ptr->active_failed ? "true " : "false",
               ptr->afailed_count,
               ptr->b2b_miss_peak,
               ptr->b2b_miss_count,
               ptr->recv_err_cnt,
               ptr->send_err_cnt,
               ptr->mesg_err_cnt,
               ptr->mesg_err_peak);
    mem_log(str);
}

/* Push daemon state to log file */
void daemon_dump_info ( void )
{
    if ( _pmon_ctrl_ptr )
    {
        daemon_dump_membuf_banner();
        mem_log_ctrl ( _pmon_ctrl_ptr );
        daemon_dump_membuf();
        for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
        {
            process_config_type * ptr = get_process_config_ptr(i);
            mem_log ('\n');
            mem_log_process ( ptr );
            mem_log_pstate  ( ptr );
            if ( ptr->active_monitoring )
            {
                mem_log_aconfig ( ptr );
                mem_log_astate  ( ptr );
            }
        }
        daemon_dump_membuf();
    }
}

/*******************************************************************
 *          Module Initialize and Finalizes Interfaces             *
 ******************************************************************/

/* Initial init of timers. */
/* Not run on a sighup     */
void pmon_timer_init ( void )
{
    mtcTimer_init ( pmonTimer_audit,   _pmon_ctrl_ptr->my_hostname, "audit" ) ;
    mtcTimer_init ( pmonTimer_pulse,   _pmon_ctrl_ptr->my_hostname, "pulse" ) ;
    mtcTimer_init ( pmonTimer_hostwd , _pmon_ctrl_ptr->my_hostname, "hostwd" ) ;
    mtcTimer_init ( pmonTimer_degrade, _pmon_ctrl_ptr->my_hostname, "degrade audit" );

    for ( int i = 0 ; i < MAX_PROCESSES ; i++ )
    {
        /* Bind the process timer to the process struct */
        process_config[i].pt_ptr = &ptimer[i] ;

        /* Init the timer for this process */
        mtcTimer_init ( process_config[i].pt_ptr, _pmon_ctrl_ptr->my_hostname, "process" ) ;
    }
}

void _process_death_hdlr ( int sig_num, siginfo_t * info_ptr, void * context_ptr );

/* Register realtime signal handler with the kernel */
int signal_hdlr_init ( int sig_num )
{
    int rc ;

    memset (&_pmon_ctrl_ptr->info, 0, sizeof(_pmon_ctrl_ptr->info));
    memset (&_pmon_ctrl_ptr->prev, 0, sizeof(_pmon_ctrl_ptr->prev));

    _pmon_ctrl_ptr->info.sa_sigaction = _process_death_hdlr ;
    _pmon_ctrl_ptr->info.sa_flags = (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_SIGINFO) ;

    rc = sigaction ( sig_num, &_pmon_ctrl_ptr->info , &_pmon_ctrl_ptr->prev );
    if ( rc )
    {
        elog("Registering : Realtime Signal %d - (%d) (%s)\n",
              sig_num, errno, strerror(errno));
        rc = FAIL_SIGNAL_INIT ;
    }
    else
    {
        ilog("Registering : Realtime Signal %d\n", sig_num);
    }
    return (rc) ;
}

/*
 * Init the handler
 *    - Must support re-init that might occur over a SIGHUP
 **/
int pmon_hdlr_init ( pmon_ctrl_type * ctrl_ptr )
{
    int rc ;

    /* Save the control pointer */
    _pmon_ctrl_ptr = ctrl_ptr ;

    /* Force running of the audit at the very start */
    _pmon_ctrl_ptr->run_audit = true ;

    rc = signal_hdlr_init ( PMON_RT_SIGNAL );

    /* Log the control setting going into the main loop */
    mem_log_ctrl ( _pmon_ctrl_ptr ) ;

    /* init the inotify file descriptors */
    _pmon_ctrl_ptr->fd = 0 ;
    _pmon_ctrl_ptr->wd = 0 ;

    return (rc) ;
}


/* Module Cleanup */
void pmon_hdlr_fini ( pmon_ctrl_type * ctrl_ptr )
{
    for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
    {
        /* Close any active monitoring sockets */
        close_process_socket ( &process_config[i] );
    }

    /* Turn off inotify */
    set_inotify_close ( ctrl_ptr->fd, ctrl_ptr->wd );
}

void manage_process_failure ( process_config_type * ptr )
{
    /*******************************************************************
     * The next 2 'if' clauses try to prevent raising alarms for
     * process failure detections while the host is shutting down.
     *******************************************************************/

    /* When handling a process failure check to see if we are already in
     * the stopping state.
     * If not, then query the current system state and save it. */
    if ( _pmon_ctrl_ptr->system_state != MTC_SYSTEM_STATE__STOPPING )
    {
        elog ("%s failed (%d) (p:%d a:%d)\n", ptr->process,
                                              ptr->pid,
                                              ptr->failed,
                                              ptr->active_failed);
        /* update current state */
        _pmon_ctrl_ptr->system_state = get_system_state();
    }
    else
    {
        /* Ignore process failures while in stopping (i.e. shutdown) mode */
        /* don't report process failures during system shutdown. */
        wlog ("%s terminated by system shutdown (pid:%d) ; ignoring\n",
                  ptr->process , ptr->pid );
        ptr->ignore = true ;
        return ;
    }

    /* Should not need this clause */
    if ( ptr->stopped == true )
    {
        slog ("%s process is in the stopped state\n", ptr->process);
    }

    passiveStageChange   ( ptr, PMON_STAGE__MANAGE) ;

    if ( ptr->failed == false )
    {
        ptr->failed = true ;
        ptr->restart = false ;
        // pmon_send_event ( MTC_EVENT_PMON_LOG, ptr ) ;
    }

    /* TODO: Consider clearing active_failed flag regardless */
    if ( ptr->active_monitoring == true )
    {
        activeStageChange    ( ptr, ACTIVE_STAGE__PULSE_REQUEST ) ;
        ptr->active_failed = true ;
    }
}

/*
 * Manage process config strdup memory over a config/reconfig.
 * On reconfig ; the PMOND_INIT_CHECK should be set and for each
 * config pointed that is not null ; feee the memory.
 */
void init_process_config_memory ( void )
{
    for ( int i = 0 ; i < MAX_PROCESSES ; i++ )
    {
        if ( process_config[i].init_check == PMOND_INIT_CHECK )
        {
            if ( process_config[i].process   ) free ((void*)process_config[i].process);
            if ( process_config[i].service   ) free ((void*)process_config[i].service);
            if ( process_config[i].script    ) free ((void*)process_config[i].script);
            if ( process_config[i].style     ) free ((void*)process_config[i].style);
            if ( process_config[i].pidfile   ) free ((void*)process_config[i].pidfile);
            if ( process_config[i].severity  ) free ((void*)process_config[i].severity);
            if ( process_config[i].mode      ) free ((void*)process_config[i].mode);
            if ( process_config[i].start_arg ) free ((void*)process_config[i].start_arg);
            if ( process_config[i].status_arg) free ((void*)process_config[i].status_arg);

            if ( process_config[i].status_failure_text_file) free ((void*)process_config[i].status_failure_text_file);
            if ( process_config[i].subfunction             ) free ((void*)process_config[i].subfunction);
            if ( process_config[i].recovery_method         ) free ((void*)process_config[i].recovery_method);
        }
        /* init the process config memory ; now that we have freed past strdup allocations*/
        memset ( (char*)&process_config[i], 0, sizeof(process_config_type));
    }
}

/* Read and load process monitor configuration from
 * all the process config files from /etc/pmon.d */
void load_processes ( void )
{
    list<active_process_alarms_type> saved_alarm_list ;

    int rc = PASS ;

    /* 1. Free timers,
     * 2. shutdown sockets
     * 3. track processes with raised alarms
     */
    for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
    {
        mtcTimer_reset ( process_config[i].pt_ptr );
        close_process_socket ( &process_config[i] );
    }

    /* Query fm for existing pmon process alarms and
     * for each that is found store their 'name' and
     * 'severity' in the passed in saved list */
    manage_queried_alarms ( saved_alarm_list );

    /* init the process config memory */
    init_process_config_memory ();

    /* Default to event mode */
    _pmon_ctrl_ptr->event_mode = true ;

    /* Start with zero processes */
    _pmon_ctrl_ptr->processes = 0 ;

    /* Read in the list of config files and their contents */
    load_filenames_in_dir ( CONFIG_DIR, config_files ) ;

    ilog ("Loading Process Configurations\n");
    ilog ("--------------------------------------------------------------\n");

    /* Run Maintenance on Inventory */
    for ( string_iter_ptr  = config_files.begin () ;
          string_iter_ptr != config_files.end () ;
          ++string_iter_ptr )
    {
        process_config_type * ptr = &process_config[_pmon_ctrl_ptr->processes] ;
        rc = process_config_load ( ptr, string_iter_ptr->data() );
        if ( rc )
        {
            memset ((char*)ptr, 0, sizeof(process_config_type));
        }
        else
        {
            /* stages for passive and active monitoring are initially set
             * inside the process_config_load */
            _pmon_ctrl_ptr->processes++ ;
            ptr->init_check = PMOND_INIT_CHECK ;
        }
    }

    pmon_send_event ( MTC_EVENT_PMON_CLEAR, NULL ) ;

    ilog ("Registering Processes With Kernel\n");
    ilog ("---------------------------------------------------------------\n");

    /* Register all the processes with the kernel */
    for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
    {
        process_config[i].restart= false;
        process_config[i].failed = false;
        if ( process_config[i].status_monitoring )
        {
           process_config[i].status_stage = STATUS_STAGE__BEGIN ;
        }
        else if ( process_config[i].stage == PMON_STAGE__MANAGE )
        {
            register_process ( &process_config[i] );
            if ( process_config[i].active_monitoring == true )
            {
                if ( open_process_socket ( &process_config[i] ) != PASS )
                {
                    elog ("%s failed to open process socket\n",
                             process_config[i].process );
                }
            }
        }
    }
    _pmon_ctrl_ptr->reload_config = false ;

    /* If there were process alarms that existed over the reload
     * then ensure that those processes are updated with that information. */
    if ( saved_alarm_list.size () )
    {
        ilog ("there are %ld active alarms over reload\n", saved_alarm_list.size());
        manage_process_alarms ( saved_alarm_list, &process_config[0], _pmon_ctrl_ptr->processes );
    }
}


/* Looks up the timer ID and asserts the corresponding ringer */
void pmon_timer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    UNUSED(sig);
    UNUSED(uc);

    if ( !(*tid_ptr) )
    {
        return ;
    }

    else if ( *tid_ptr == pmonTimer_pulse.tid )
    {
        pmonTimer_pulse.ring = true ;
    }
    else if ( *tid_ptr == pmonTimer_degrade.tid )
    {
        mtcTimer_stop_int_safe ( pmonTimer_degrade );
        pmonTimer_degrade.ring = true ;
        _pmon_ctrl_ptr->patching_in_progress = false ;
    }
    else if ( *tid_ptr == pmonTimer_audit.tid )
    {
        mtcTimer_stop_int_safe ( pmonTimer_audit );
        pmonTimer_audit.ring = true ;
    }
    /* is host watchdog pmon timer */
    else if ( *tid_ptr == pmonTimer_hostwd.tid )
    {
        pmonTimer_hostwd.ring = true ;

        /* we do not stop the timer; instead let it auto-restart */
    }
    else
    {
        bool found = false ;
        for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
        {
            if ( *tid_ptr == process_config[i].pt_ptr->tid )
            {
                mtcTimer_stop_int_safe ( process_config[i].pt_ptr );
                process_config[i].pt_ptr->ring = true ;
                found = true ;
                break ;
            }
        }
        if ( !found )
        {
            //wlog ("Unknown timer\n");
            /* try and cleanup by stopping this unknown timer via its tid */
            mtcTimer_stop_tid_int_safe (tid_ptr);
        }
    }
}

/****************************************************************************
 *
 * Name       : service_file_exists
 *
 * Description: Look in some well known places for the specified service file.
 *
 * Returns    : Return true if the specified service file is found.
 *
 * Updates    : If the service file is found then update the supplied
 *              character string buffer with the full path/name of that
 *              service file.
 *
 ****************************************************************************/
bool service_file_exists ( string service_filename,
                           char * path_n_name_ptr,
                           int    max_len )
{
    if ( path_n_name_ptr == NULL ) {
        slog ("Path for service files search is null.\n");
        return false;
    }

    /* load the name of the service file */
    snprintf ( path_n_name_ptr, max_len, "%s/%s",
                                          SYSTEMD_SERVICE_FILE_DIR1,
                                          service_filename.data());
    if (( path_n_name_ptr ) && (strnlen ( path_n_name_ptr, max_len )))
    {
        if ( daemon_is_file_present ( path_n_name_ptr ) == true )
            return true ;
    }
    snprintf ( path_n_name_ptr, max_len, "%s/%s",
                                          SYSTEMD_SERVICE_FILE_DIR2,
                                          service_filename.data());
    if (( path_n_name_ptr ) && ( strnlen ( path_n_name_ptr, max_len )))
    {
        if ( daemon_is_file_present ( path_n_name_ptr ) == true )
            return true ;
    }
    return false ;
}

/*****************************************************************************
 *
 * Name    : process_config_load
 *
 * Purpose : Load the content of a config file
 *
 *****************************************************************************/
int process_config_load (process_config_type * pc_ptr, const char * config_file_ptr )
{
    char    recovery_method_buf  [_MAX_LEN_] ;
    memset (recovery_method_buf,0, sizeof(recovery_method_buf));

    if ( _pmon_ctrl_ptr->processes >= MAX_PROCESSES )
    {
        wlog ("Cannot Monitor more than %d processes\n", MAX_PROCESSES );
        return (FAIL);
    }

    /* Read the process config file */
    pc_ptr->mask = 0 ;
    pc_ptr->amask = 0 ;
    pc_ptr->status_mask = 0 ;
    pc_ptr->status_monitoring = false;
    pc_ptr->passive_monitoring = false;
    pc_ptr->audit_alarm_refresh_count = 0 ;

    if (ini_parse( config_file_ptr, pmon_process_config, pc_ptr) < 0)
    {
        elog("Read Failure : %s\n", config_file_ptr );
        return (FAIL);
    }

    /* Set some defaults just in case they were not specified */
    if ( !pc_ptr->mode )
    {
        pc_ptr->mode = strdup("Passive") ;
    }
    if ( !pc_ptr->startuptime )
    {
        pc_ptr->startuptime = PMON_MIN_START_DELAY ;
    }

    /* Many process conf files came from a sysvinit origin and might not
     * have a service file label. Account for that in the following
     * load of recovery_method_buf.
     * Accept a script name if the service name is missing. */
    bool recovery_method_found = false ;

    /* look for the service file */
    if ( pc_ptr->service )
    {
        string service = pc_ptr->service ;
        if ( service.find(".service") == string::npos )
            service.append(".service");
        if ( service_file_exists(service, &recovery_method_buf[0], _MAX_LEN_) == true )
            recovery_method_found = true ;
    }
    else if ( pc_ptr->script )
    {
        string script = basename((char*)pc_ptr->script);
        if ( script.find(".service") == string::npos )
            script.append(".service");
        if ( service_file_exists(script, &recovery_method_buf[0], _MAX_LEN_) == true )
           recovery_method_found = true ;
        else
        {
            /* resort to the script file only */
            /* load the name of the process init script */
            snprintf ( &recovery_method_buf[0], _MAX_LEN_, "%s", pc_ptr->script );
            if ( daemon_is_file_present ( recovery_method_buf ) == true )
            {
                recovery_method_found = true ;
            }
            else
            {
                wlog ("%s has script but not found (%s)\n",
                          pc_ptr->process, recovery_method_buf );
            }
        }
    }
    else
    {
        /* print a log if we have no recovery method */
        wlog ("%s has no recovery method ; process not monitored\n", pc_ptr->process );
        wlog ("... conf file has no 'service' or 'script' recovery entry\n");
        return (FAIL_NOT_FOUND);
    }

    if ( recovery_method_found == false )
    {
        wlog ("%s has no recovery method found ; process not monitored\n", pc_ptr->process );
        return (FAIL_NOT_FOUND);
    }

    update_config_option ( &pc_ptr->recovery_method , recovery_method_buf );

    if ( !strcmp ( pc_ptr->mode, "status" ) )
    {
        pc_ptr->status_monitoring = true;

        if (( pc_ptr->status_mask == CONF_STATUS_MON_MASK  ) &&
            ( pc_ptr->process[0] != '\0' ) &&
            ( pc_ptr->severity[0] != '\0'))
        {
            dlog1 ("Config File : %s\n", string_iter_ptr->c_str());

            if ( !strcmp ( pc_ptr->severity, "critical" ))
            {
                pc_ptr->sev = SEVERITY_CRITICAL ;
            }
            else if ( !strcmp ( pc_ptr->severity, "major" ))
            {
                pc_ptr->sev = SEVERITY_MAJOR ;
            }
            else if ( !strcmp ( pc_ptr->severity, "minor" ))
            {
                pc_ptr->sev = SEVERITY_MINOR ;
            }
            else
            {
                wlog ("%s has invalid severity ; ignoring\n", pc_ptr->process );
                pc_ptr->ignore = strdup ("ignored");
            }

            /* Bind the process timer to the process struct */
            pc_ptr->pt_ptr = &ptimer[_pmon_ctrl_ptr->processes] ;

            /* set the timer service owner to the process name */
            pc_ptr->pt_ptr->service = pc_ptr->process ;

            pc_ptr->restarts_cnt = 0 ;
            pc_ptr->pid          = 0 ;
            pc_ptr->child_pid    = 0 ;
            pc_ptr->restart      = false ;
            pc_ptr->failed       = false ;
            pc_ptr->status_failed = false ;
            pc_ptr->was_failed   = false ;
            pc_ptr->sigchld_rxed = false ;

            ilog ("%7s Mon : %-27s %-8s\n", pc_ptr->mode,
                                            pc_ptr->process,
                                            pc_ptr->ignore ? "ignored" : pc_ptr->severity);
            pc_ptr->status_stage = STATUS_STAGE__BEGIN ;
        }
        else
        {
            wlog ("Status Parse Failure: %s\n", string_iter_ptr->c_str());
            wlog ("Status Mask Expected: %x Detected: %x\n", CONF_STATUS_MON_MASK, pc_ptr->status_mask );
            return (FAIL);
        }

        return (PASS);
    }


    if (( pc_ptr->mask == CONF_MASK  ) &&
        ( pc_ptr->process[0] != '\0' ) &&
        ( pc_ptr->severity[0] != '\0'))
    {
        dlog1 ("Config File : %s\n", string_iter_ptr->c_str());

        if ( !strcmp ( pc_ptr->severity, "critical" ))
        {
            pc_ptr->sev = SEVERITY_CRITICAL ;
        }
        else if ( !strcmp ( pc_ptr->severity, "major" ))
        {
            pc_ptr->sev = SEVERITY_MAJOR ;
        }
        else if ( !strcmp ( pc_ptr->severity, "minor" ))
        {
            pc_ptr->sev = SEVERITY_MINOR ;
        }
        else
        {
            wlog ("%s has invalid severity ; ignoring\n", pc_ptr->process );
            pc_ptr->ignore = strdup ("ignored");
        }

        /* Bind the process timer to the process struct */
        pc_ptr->pt_ptr = &ptimer[_pmon_ctrl_ptr->processes] ;

        /* Init the timer for this process */
        mtcTimer_init ( pc_ptr->pt_ptr ) ;
        pc_ptr->pt_ptr->hostname = pc_ptr->process ;
        pc_ptr->pt_ptr->service  = pc_ptr->process ;

        pc_ptr->restarts_cnt = 0 ;
        pc_ptr->debounce_cnt = 0 ;
        pc_ptr->pid          = 0 ;
        pc_ptr->child_pid    = 0 ;
        pc_ptr->restart      = false ;
        pc_ptr->failed       = false ;
        pc_ptr->sigchld_rxed = false ;
        pc_ptr->stopped      = false ;

        pc_ptr->alarm_severity = FM_ALARM_SEVERITY_CLEAR ;

        if (( _pmon_ctrl_ptr->system_type != SYSTEM_TYPE__NORMAL ) &&
            ( pc_ptr->subfunction != NULL ))
        {
            /* subfunction process monitoring is deferred until
             * that subfunction init is complete */
            ilog ("%7s Def : %-30s %-8s - %s (%s)\n", pc_ptr->mode,
                                                 pc_ptr->process,
                                                 pc_ptr->ignore ? "ignored" : pc_ptr->severity, recovery_method_buf,
                                                 pc_ptr->subfunction);
            /* defer subfunction processes to the FSM to get enabled */
            pc_ptr->stage = PMON_STAGE__POLLING ;
            pc_ptr->pt_ptr->ring = true ;
        }
        else
        {
            /* if not a subfunction then monitoring defaults
             * to true immediately */
            pc_ptr->passive_monitoring = true ;

            ilog ("%7s Mon : %-30s %-8s - %s\n", pc_ptr->mode,
                                                 pc_ptr->process,
                                                 pc_ptr->ignore ? "ignored" : pc_ptr->severity, recovery_method_buf);
            pc_ptr->stage = PMON_STAGE__MANAGE ;
        }
        // mem_log_process ( pc_ptr );
    }
    else
    {
        wlog ("Parse Failure: %s\n", string_iter_ptr->c_str());
        wlog ("Mask Expected: %x Detected: %x\n", CONF_MASK, pc_ptr->mask );
        return (FAIL);
    }

    if ( !strcmp ( pc_ptr->mode, "active" ) )
    {
        if ( pc_ptr->amask == CONF_AMON_MASK )
        {
            if (( pc_ptr->period == 0 ) ||
                ( pc_ptr->period > PMON_MAX_ACTIVE_PERIOD ))
            {
                elog ("%s monitor period out-of-range (%d secs), setting to max\n",
                          pc_ptr->process,
                          pc_ptr->period );

                pc_ptr->period = PMON_MAX_ACTIVE_PERIOD ;
            }
            if ( pc_ptr->timeout > pc_ptr->period )
            {
                elog ("%s monitor 'timeout' longer than 'period' (%d:%d secs), rounding down\n",
                       pc_ptr->process,
                       pc_ptr->timeout,
                       pc_ptr->period );

                pc_ptr->timeout = pc_ptr->period ;
            }

            /* Init the active component */
            pc_ptr->active_stage      = ACTIVE_STAGE__PULSE_REQUEST ;
            pc_ptr->active_monitoring = true  ;
            pc_ptr->active_failed     = false ;
            pc_ptr->pulse_count       = 0 ;
            pc_ptr->b2b_miss_peak     = 0 ;
            pc_ptr->b2b_miss_count    = 0 ;
        }
        else
        {
            wlog ("%s Parse Failure\n", string_iter_ptr->c_str());
            wlog ("%s Active Mask Expected: %x Detected: %x\n",
                   pc_ptr->process,
                   CONF_AMON_MASK,
                   pc_ptr->amask );
            return (FAIL);
        }
   }
   return (PASS);
}

int get_process_pid ( process_config_type * ptr )
{
    int pid = 0 ;
    if ( ptr )
    {
        if ( daemon_is_file_present ( ptr->pidfile ) == true )
        {
            pid = daemon_get_file_int ( ptr->pidfile );
        }
    }
    return (pid);
}

/* search the process list for the child_pid in
 * order to find the parent it is associated with */
process_config_type * find_parent_process ( int child_pid )
{
    for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
    {
        if ( process_config[i].child_pid == child_pid )
        {
            return (&process_config[i]);
        }
    }
    /* look based on PID */
    for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
    {
        if ( process_config[i].pid == child_pid )
        {
            return (&process_config[i]);
        }
    }

    return (NULL);
}

/* search the process list for the child_pid in
 * order to find the parent it is associated with */
bool want_degrade_clear ( void )
{
    int i ;
    bool clear = true ;
    for ( i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
    {
        /* Don't report current or previous status on
         * processes that are not being monitored */
        if (( !process_config[i].passive_monitoring ) &&
            ( !process_config[i].status_monitoring ))
        {
            continue ;
        }
        if (( process_config[i].failed == true ) || ( process_config[i].active_failed == true ))
        {
            if (( process_config[i].alarm_severity == FM_ALARM_SEVERITY_MAJOR ) ||
                ( process_config[i].alarm_severity == FM_ALARM_SEVERITY_CRITICAL ))
            {
                wlog ("%s is still failed '%s' ; degrade assert\n",
                          process_config[i].process,
                          alarmUtil_getSev_str(process_config[i].alarm_severity).c_str());

                /* Resend the process event to maintenance every threshold count */
                if ( ++process_config[i].audit_alarm_refresh_count > AUDIT_EVENT_SEND_REFESH_THRESHOLD )
                {
                    process_config[i].audit_alarm_refresh_count = 0 ;
                    if ( process_config[i].alarm_severity == FM_ALARM_SEVERITY_MAJOR )
                        pmon_send_event ( MTC_EVENT_PMON_MAJOR, &process_config[i] ) ;
                    else
                        pmon_send_event ( MTC_EVENT_PMON_CRIT, &process_config[i] ) ;
                }
                clear = false ;
            }
        }
    }
    return (clear);
}

static char unknown_process[] = "unknown process" ;
bool kill_running_process ( int pid )
{
    bool rc = false ;
    if ( pid )
    {
        int result = kill ( pid, 0 );
        if ( result == 0 )
        {
            char * proc_name_ptr = &unknown_process[0] ;
            process_config_type * ptr = find_parent_process ( pid ) ;
            if ( ptr )
            {
               daemon_remove_file ( ptr->pidfile );
               proc_name_ptr = (char*)ptr->process ;
            }
            result = kill ( pid, SIGKILL );
            if ( ptr && ( result == 0 ) )
            {
                if ( daemon_is_file_present ( ptr->pidfile ) )
                {
                    if ( get_process_pid ( ptr ) == pid )
                    {
                        ilog ("%s removing stale pidfile (%d) %s\n", ptr->process, pid, ptr->pidfile );
                        daemon_remove_file ( ptr->pidfile );
                    }
                }
                wlog ("%s Killed     (%d)\n", proc_name_ptr, pid );
                rc = true ;
            }
            else
            {
                ilog ("%s kill failed (%d)\n", proc_name_ptr, pid );
            }
        }
    }
    return (rc);
}

/* if the child (startup script) pid is still running then kill it */
void kill_running_child ( process_config_type * ptr )
{
    if ( ptr->child_pid )
    {
        if ( kill_running_process ( ptr->child_pid ) == true )
        {
            wlog ("%s start script still running (%d) ; killed\n", ptr->process, ptr->child_pid );
        }
        ptr->child_pid = 0 ;
    }
}

bool process_running ( process_config_type * ptr )
{
    int pid = get_process_pid ( ptr );
    if ( pid )
    {
        int result = kill (pid, 0 );
        ptr->pid = pid ;
        if ( result == 0 )
        {
            if (( ptr->pid != 0 ) && ( ptr->pid != pid ))
            {
                wlog ("%s pid changed (was:%d now:%d)\n",
                          ptr->process ,
                          ptr->pid,
                          pid);

                ptr->pid = 0 ;
                return (false);
            }
            else if (( ptr->pid == 0 ) && ( pid ))
            {
                ilog ("%s Running   (%d)\n", ptr->process, ptr->pid);
            }
            else
            {
                dlog1 ("%s Running   (%d) (%d)\n",  ptr->process, pid, ptr->pid );
            }
            return (true) ;
        }
        else
        {
            dlog ("%s process not running (kill 0 result:%d) (get_process_pid:%d)\n", ptr->process, result, pid );
        }
    }
    else
    {
        ilog ("%s process not running\n", ptr->process );
    }
    ptr->pid = 0 ;
    return (false);
}

/* Temporary till we get kernel event */
void _get_events ( void )
{
    int pid = 0 ;
    for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
    {
        bool running = false ;

        /* ignore is ignore */
        if ( process_config[i].ignore == true )
        {
            process_config[i].failed = false ;
            process_config[i].restart= false ;
            continue ;
        }

        /* only look for events for process that are
         * - in the managed state and
         * - not monitored by 'status
         */
        else if (( process_config[i].stage != PMON_STAGE__MANAGE ) ||
                 ( process_config[i].status_monitoring ))
        {
            continue ;
        }

        /* Skip already failed processes */
        else if ( process_config[i].failed == false )
        {
            if ((pid = get_process_pid ( &process_config[i] )))
            {
                int result = kill (pid, 0 );
                process_config[i].pid = pid ;
                if ( result == 0 )
                {
                    dlog3 ("%s (%d) is running\n", process_config[i].process, pid);
                    running = true ;
                }
                else
                {
                    dlog ("%s (%d) not running (%d:%d) (%s)\n",
                              process_config[i].process, pid,
                              result, errno, strerror(errno)) ;
                }
            }
            else
            {
                dlog ("%s Pid (unknown) - no pidfile\n", process_config[i].process )
            }

            /* If not running then fail the process
             * to trigger auto-recovery */
            if ( running == false )
            {
                wlog ("%s Not Running\n", process_config[i].process );

                manage_process_failure ( &process_config[i] );
            }
        }
    }

    /* turn off the audit */
    _pmon_ctrl_ptr->run_audit = false ;
}

/* This is the data structure for requestion process death
 * (and other state change) information.  Sig of -1 means
 * query, sig of 0 means deregistration, positive sig means
 * that you want to set it.  sig and events are value-result
 * and will be updated with the previous values on every
 * successful call. */

int unregister_process ( process_config_type * ptr )
{
    dlog1 ("%s pid %d\n", ptr->process, ptr->pid );
    if ( ptr->pid )
    {
        struct task_state_notify_info info ;
        info.pid    = ptr->pid ;
        info.sig    = 0 ;
        info.events = PMON_EVENT_FLAGS ;
        if ( prctl (PR_DO_NOTIFY_TASK_STATE, &info ))
        {
            if ( errno != ESRCH )
            {
                wlog ("%s unregister pid:%d (%d:%s)\n",
                          ptr->process,
                          ptr->pid,
                          errno,
                          strerror(errno) );
            }
        }
        else
        {
             ilog ("%s Unregister (%d)\n", ptr->process, ptr->pid );
        }
    }
    ptr->registered = false ;
    return (PASS);
}

int register_process ( process_config_type * ptr )
{
    int pid = get_process_pid ( ptr );
    if ( pid )
    {
        ptr->pid = pid ;
        ptr->restart= false ;
        if (( _pmon_ctrl_ptr->event_mode ) && ( !ptr->ignore ))
        {
            struct task_state_notify_info info ;
            info.pid    = pid ;
            info.sig    = PMON_RT_SIGNAL ;
            info.events = PMON_EVENT_FLAGS;
            if ( prctl (PR_DO_NOTIFY_TASK_STATE, &info ) )
            {
                elog ("%s failed to register pid:%d (%d:%s)\n", ptr->process, pid, errno, strerror(errno));
                if ( errno == EINVAL )
                {
                    _pmon_ctrl_ptr->event_mode = false ;
                    wlog ( "%s Switching to Polling mode\n", ptr->process);
                }
                else
                {
                    ptr->failed = true ;
                }
            }
            else
            {
                ilog ("%s Registered (%d)\n", ptr->process , pid );
                ptr->failed = false ;
                ptr->registered = true ;
                passiveStageChange ( ptr, PMON_STAGE__MANAGE ) ;
                if ( ptr->active_monitoring == false )
                {
                    manage_alarm ( ptr, PMON_CLEAR );
                }
            }
        }
        /* Don't 'else' because event mode might
         * change in the above clause */
        if ( _pmon_ctrl_ptr->event_mode == false )
        {
            wlog ("%s Registered (%d) in polling mode\n",
                      ptr->process , pid);

            /* prevent infinite reg retry in polling mode */
            ptr->registered = true ;

            if ( process_running ( ptr ) == false )
            {
                ptr->failed = true ;
            }
            else
            {
                ptr->failed = false ;
                manage_alarm ( ptr, PMON_CLEAR );
                passiveStageChange ( ptr, PMON_STAGE__MANAGE ) ;
            }
        }
    }
    else
    {
        ilog ("%s is not running\n", ptr->process );
        ptr->failed = true ;
    }

    if ( ptr->failed )
    {
        manage_process_failure ( ptr );
        return (FAIL);
    }
    else
    {
        return (PASS);
    }
}


/* This respawns a process through the 'script' string from the process config file.
 * The pmond log files are first closed so their fd's are not duped to the child.
 * The syslog facility is used to log child messages to user.log
 * The waitpid interface is used to manage acknowledging the exit of the child process */

#define PMOND_EXECV_ARGS (4)

int respawn_process ( process_config_type * ptr )
{
    pid_t pid ;

    int  rc      = PASS  ;
    bool restart = false ;

    unregister_process ( ptr );
    if ( process_running ( ptr ) == true )
    {
        dlog ("%s still running\n", ptr->process );
        restart = true ;
        kill_running_process ( ptr->pid );
    }

    ptr->restarts_cnt++ ;

    /* default restart result and ponitoring controls */
    ptr->status       = RETRY ; /* keep looking       */
    ptr->pidwait_cnt  = 0     ; /* TODO: should be a timer .... start count        */
    ptr->sigchld_rxed = false ; /* sigchild handler did not run    */

    /* Fork the daemon to trigger the process specific restart */
    ptr->child_pid = pid = fork () ;
    if (pid == 0)
    {
        /* execv arg list */
        char * argv[PMOND_EXECV_ARGS] ;
        for ( int i = 0 ; i < PMOND_EXECV_ARGS ; i++ ) argv[i] = NULL ;

        char recovery_cmd[_MAX_LEN_] ;

        bool close_file_descriptors = true ;
        if ( setup_child ( close_file_descriptors ) != PASS )
            exit(EXIT_FAILURE);

        signal (SIGCHLD, SIG_DFL);

        openlog ((char*)ptr->process, LOG_PID, LOG_USER );

        /* Default File Creation Mask */
        umask(022);

        memset (recovery_cmd,0,sizeof(recovery_cmd));

        ilog ("Service:%s\n", ptr->service ? ptr->service : "unknown");

        #define SYSTEMCTL_CMD "/usr/bin/systemctl"
        #define   RESTART_CMD "restart"
        #define     START_CMD "start"
        if ( get_ctrl_ptr()->recovery_method == PMOND_RECOVERY_METHOD__SYSTEMD )
        {

            /* systemd recovery method - if the service is specified then it takes precidence */
            if ( ptr->service )
                sprintf ( &recovery_cmd[0], "%s", ptr->service );
            else
                sprintf ( &recovery_cmd[0], "%s", ptr->process );

            argv[0] =  (char*)&SYSTEMCTL_CMD ; /* path to executable   */
            argv[1] =  (char*)&RESTART_CMD   ; /* the recovery command */
            argv[2] =  &recovery_cmd[0]      ; /* the process name     */
        }
        else
        {
            /* init script method */
            snprintf( &recovery_cmd[0], _MAX_LEN_, "%s", ptr->script ) ;
            argv[0] = &recovery_cmd[0] ; /* path to script   */
            argv[1] = (restart ? (char*)&RESTART_CMD : (char*)&START_CMD) ; /* the process name */
        }

        rc = execv(argv[0], argv );
        if ( 0 > rc )
        {
            syslog ( LOG_WARNING, "%s recovery failed with method '%s': (%s %s %s) (%d:%m)\n",
                     ptr->process,
                     ptr->recovery_method,
                     argv[0],
                     argv[1],
                     argv[2] ? "" : argv[2] ,
                     errno );
        }
        else
        {
            syslog ( LOG_INFO, "%s recovered witb method '%s': (%s %s %s)\n",
                     ptr->process,
                     ptr->recovery_method,
                     argv[0],
                     argv[1],
                     argv[2] ? "" : argv[2] );
        }

        closelog();
        exit (rc);
    }
    if ( pid == -1 )
    {
        elog ("%s fork failed (%s)\n", ptr->process , strerror(errno));

        /* TODO: Consider making this a critical fault
         * after 100 retries.
         * All possibilities based on man page are
         * due to resource limitations and if that does
         * not resolve in 100 retries then it probably will never.
         **/
        return (FAIL);
    }

    gettime ( ptr->time_start );

    ilog ("%s Spawn      (%d)\n", ptr->process, ptr->child_pid );

    return (PASS);
}

/*****************************************************************************
 *
 * Name    : execute_start_command
 *
 * Purpose : execute start script command

 *****************************************************************************/
int execute_start_command(process_config_type * ptr)
{
    pid_t child_pid;

    wlog("%s process(es) start\n", ptr->process);

    dlog ("Main Pid:%d \n", getpid() );

    ptr->sigchld_rxed = false ; /* sigchild handler did not run    */

    ptr->child_pid = child_pid = fork ();
    if (child_pid == 0)
    {
        dlog ("Child Pid:%d \n", getpid() );

        char* argv[] = { basename((char*)ptr->script), (char*)ptr->start_arg, NULL};
        char cmd[MAX_FILE_SIZE] ;
        memset (cmd,0,sizeof(cmd));

        snprintf ( &cmd[0], MAX_FILE_SIZE, "%s", ptr->script);

        bool close_file_descriptors = true ;
        if ( setup_child ( close_file_descriptors ) != PASS )
        {
            exit(255);
        }

        /* Set child to ignore child exit */
        signal (SIGCHLD, SIG_DFL);

        /* Setup the exec arguement */
        int res = execv(cmd, argv);
        elog ( "Failed to run %s return code:%d error:%s\n", cmd, res, strerror(errno) );
        exit (255);
    }

    if ( child_pid == -1 )
    {
        elog ("Fork failed (%s)\n", strerror(errno));
        return (FAIL);
    }

    gettime ( ptr->time_start );

    return (PASS);
}

/*****************************************************************************
 *
 * Name    : execute_status_command
 *
 * Purpose : execute status script command

 *****************************************************************************/
int execute_status_command (process_config_type * ptr)
{
    pid_t child_pid;

    dlog("%s process(es) status query\n", ptr->process);
    dlog ("Main Pid:%d \n", getpid() );

    ptr->sigchld_rxed = false ; /* sigchild handler did not run    */

    ptr->child_pid  = child_pid = fork ();
    if (child_pid == 0)
    {
        dlog ("Child Pid:%d \n", getpid() );

        char* argv[] = {basename((char*)ptr->script), (char*)ptr->status_arg, NULL};
        char cmd[MAX_FILE_SIZE] ;
        memset  (cmd,0,sizeof(cmd));

        snprintf ( &cmd[0], MAX_FILE_SIZE, "%s", ptr->script);

        bool close_file_descriptors = true ;
        if ( setup_child ( close_file_descriptors ) != PASS )
        {
            exit(255);
        }

        /* Set child to ignore child exit */
        signal (SIGCHLD, SIG_DFL);

        /* Setup the exec arguement */
        int res = execv(cmd, argv);
        elog ( "Failed to run %s return code:%d error:%s\n", cmd, res, strerror(errno) );
        exit (255);
    }

    if ( child_pid == -1 )
    {
        elog ("Fork failed (%s)\n", strerror(errno));
        return (FAIL);
    }

    gettime ( ptr->time_start );

    return (PASS);
}

void daemon_sigchld_hdlr ( void )
{
    pid_t tpid = 0 ;
    bool found = 0 ;
    int status = 0 ;

    dlog("Received SIGCHLD ...\n");

    while ( 0 < ( tpid = waitpid ( -1, &status, WNOHANG | WUNTRACED )))
    {
        process_config_type * process_ptr = find_parent_process ( tpid ) ;
        if ( process_ptr )
        {
            process_ptr->sigchld_rxed = true ;

            if (WIFEXITED(status))
            {
                if ( process_ptr->status_monitoring == false )
                {
                  dlog ("%s spawn script exited properly (%d)\n", process_ptr->process, tpid );
                }
                else
                {
                   /* with status mode we do not need to wait for a timeout since we got a response */
                   /* force a ring                                                                  */
                   process_ptr->pt_ptr->ring = true;
                }

                gettime   ( process_ptr->time_stop );
                timedelta ( process_ptr->time_start,
                            process_ptr->time_stop,
                            process_ptr->time_delta );

                /* only print log if there is an error */
                process_ptr->status = WEXITSTATUS(status) ;

                if ( process_ptr->status )
                {
                    if ( process_ptr->status_monitoring == false )
                    {
                        ilog ("%s spawn failed (rc:%d:%x) (%ld.%03ld secs)\n",
                               process_ptr->process,
                               process_ptr->status,
                               process_ptr->status,
                               process_ptr->time_delta.secs,
                               process_ptr->time_delta.msecs/1000);
                    }
                }
                else
                {
                    if ( process_ptr->status_monitoring == false )
                    {
                        /* only print this log if the spawn time took longer than 1 second */
                        if ( process_ptr->time_delta.secs )
                        {
                            ilog ("%s spawned in %ld.%03ld secs\n",
                                      process_ptr->process,
                                      process_ptr->time_delta.secs,
                                      process_ptr->time_delta.msecs/1000);
                        }
                    }
                }
            }
            else if (WIFSIGNALED(status))
            {
                process_ptr->status = FAIL ;
                wlog ("%s test uncaught signal\n", process_ptr->process );
            }
            else if (WIFSTOPPED(status))
            {
                process_ptr->status = FAIL ;
                wlog ("%s test stopped.\n", process_ptr->process );
            }
        }
        else
        {
            dlog ("parent process for PID:%d lookup failed ; reaped likely after timeout\n", tpid );
            return ;
        }
    }
    if ( ( tpid > 0 ) && ( found == false ) )
    {
        wlog ("PID:%d found no corresponding process\n", tpid );
    }
}

int manage_alarm ( process_config_type * ptr, int action )
{
    int rc = PASS ;

    pmon_ctrl_type * ctrl_ptr = get_ctrl_ptr () ;

    string processInfo = ptr->process;
    // check for  extra text
    if((ptr->status_monitoring ) && (ptr->status_failure_text_file))
    {
        string extra_text = get_status_failure_text(ptr);
        if(!extra_text.empty())
        {
            processInfo.append(" (");
            processInfo.append(extra_text);
            processInfo.append(")");
        }
    }

    if ( action == PMON_CLEAR )
    {
        if ( ptr->alarm_severity != FM_ALARM_SEVERITY_CLEAR )
        {
            ilog ("%s from '%s' to 'clear'\n", ptr->process, alarmUtil_getSev_str(ptr->alarm_severity).c_str());
            pmonAlarm_clear ( ctrl_ptr->my_hostname, PMON_ALARM_ID__PMOND, processInfo );
            ptr->alarm_severity = FM_ALARM_SEVERITY_CLEAR ;
        }
        ptr->failed = false ;
    }
    else if ( action == PMON_LOG )
    {
        /* CGTS 4010: Pmon logs and alarm ID should not be identical.
         *            Choice was made to not raise pmon logs for process
         *            failures. If we do in the future then we should
         *            use a different number from 200.006
         * pmonAlarm_minor_log ( ctrl_ptr->my_hostname, PMON_ALARM_ID__PMOND, processInfo, ptr->restarts );
         */
        ilog ("%s process has failed ; %s\n", ptr->process,
                 (ptr->restarts == 0) ? "Manual recovery is required." : "Auto recovery in progress.");

        /* Unlike the above call to pmonAlarm_minor_log, this call only creates a log entry in mtcAgent.log */
        pmon_send_event ( MTC_EVENT_PMON_LOG, ptr ) ;
    }
    else
    {
        if ( ptr->restart == true )
        {
            /* handle as error now rather than command */
            ptr->restart = false ;
        }
        switch ( ptr->sev )
        {
            case SEVERITY_CRITICAL:
            {
                wlog ("%s Critical Assert\n", ptr->process );
                ptr->failed = true ;
                if ( ptr->alarm_severity != FM_ALARM_SEVERITY_CRITICAL )
                {
                     pmonAlarm_critical ( ctrl_ptr->my_hostname, PMON_ALARM_ID__PMOND, processInfo );
                     ptr->alarm_severity = FM_ALARM_SEVERITY_CRITICAL ;
                }
                break ;
            }
            case SEVERITY_MAJOR:
            {
                wlog ("%s Major Assert\n", ptr->process );
                ptr->failed = true ;
                if ( ptr->alarm_severity != FM_ALARM_SEVERITY_MAJOR )
                {
                     pmonAlarm_major ( ctrl_ptr->my_hostname, PMON_ALARM_ID__PMOND, processInfo );
                     ptr->alarm_severity = FM_ALARM_SEVERITY_MAJOR ;
                }
                break ;
            }
            case SEVERITY_MINOR:
            {
                wlog ("%s Minor Assert\n", ptr->process );
                ptr->failed = true ;
                if ( ptr->alarm_severity != FM_ALARM_SEVERITY_MINOR )
                {
                     pmonAlarm_minor ( ctrl_ptr->my_hostname, PMON_ALARM_ID__PMOND, processInfo, ptr->restarts );
                     ptr->alarm_severity = FM_ALARM_SEVERITY_MINOR ;
                }
                break ;
            }
            default:
            {
                slog ("%s has Invalid Severity", ptr->process);
                ptr->sev = SEVERITY_CLEAR ;
                ptr->failed = false ;
                rc = RETRY ;
                break ;
            }
        }
    }
    return (rc);
}


/*********************************************************************************
 *
 * Name       : _process_death_hdlr
 *
 * Purpose    : Handle realtime signal events from "Notification of death
 *              of arbitrary process" (NODOAP) service in the kernel.
 *
 * Description: This handler is bound into the kernel with signal_hdlr_init
 *              Monitored processes are registered with the NODOAP feature
 *
 *               1. when service starts
 *               2. after a process is re-spawned and deemed stable and recovered
 *
 * The kernel passes the pid of the dead process in through info_ptr->si_pid.
 * This handler searches the process list for that pid. If found then it triggers
 * that process to be recovered by the fsm. if that process for some crazy reason
 * is already in the failed state then this handler deferrs to allowing the fsm
 * to complete.
 *
 * If the pid is not found in the process control structure then the pidfiles
 * are searched. if the process is not fould in that secondary search then the
 * handler forces the get_events audit to run as a catch all.
 *
 * Note: The _get_events audit already runs periodically but at a much slower rate.
 *
 * Update: emacdona: commented out debug logs as we should not be logging
 *                   in a signal handler
 *
 */
void _process_death_hdlr ( int sig_num, siginfo_t * info_ptr, void * context_ptr )
{
    UNUSED(context_ptr);
    UNUSED(sig_num) ;

    if ( info_ptr )
    {
        process_config_type * ptr = &process_config[0] ;
        bool found = false ;
        dlog ("Sig:%d  Pid:%d  Code:%d  Exit:%d\n",
                info_ptr->si_signo,
                info_ptr->si_pid,
                info_ptr->si_code,
                info_ptr->si_status );

        for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
        {
            ptr = &process_config[i] ;

            if ( ptr->pid == info_ptr->si_pid )
            {
                found = true ;

                if ( ptr->failed != true )
                {
                    ptr->failed = true ;
                    manage_process_failure ( ptr );
                }
                break ;
            }
        }
        if ( !found )
        {
            for ( int i = 0 ; i < _pmon_ctrl_ptr->processes ; i++ )
            {
                int pid ;
                ptr = &process_config[i] ;

                if ((pid = get_process_pid ( ptr )))
                {
                    if ( pid == info_ptr->si_pid )
                    {
                        found = true ;
                        if ( ptr->failed != true )
                        {
                            /* One notification from the kernel is all we need */
                            manage_process_failure ( ptr );
                        }
                        break ;
                    }
                }
            }
        }
        if ( !found )
        {
            /* Failed to find process for pid */
            /* Forcing _get_events audit */
            _pmon_ctrl_ptr->run_audit = true ;
        }
    }
    else
    {
        /* Handler called with NULL siginfo pointer */
        /* Forcing _get_events audit */
        _pmon_ctrl_ptr->run_audit = true ;
    }
}

/************************************************************************
 *
 * Name :       manage_process_alarms
 *
 * Description: This interface manages process alarms over a process
 *              configuration reload
 *
 * Steps:
 *
 * 1. Loop over each item in the list and mark the process as failed
 *    with the specified severity level.
 *
 * 2. If the process is not found then clear its alarm as it is no
 *    longer a valid process in the new profile and we don't want a
 *    lingering stuck alarm.
 *
 *************************************************************************/

void manage_process_alarms (  list<active_process_alarms_type> & _list,
                              process_config_type * const ptr,
                              int const processes )
{
    /* get out if the list is empty ; should not have been called if
     * empty but ... just in case */
    if ( ! _list.empty() )
    {
        list<active_process_alarms_type>::iterator _iter_ptr ;

        /* loop over the list ... */
        for ( _iter_ptr=_list.begin(); _iter_ptr!=_list.end(); ++_iter_ptr )
        {
            /* for each item assum it is not found */
            bool found = false ;

            /* try and find this process in the new process profile */
            for ( int i = 0 ; i < processes ; i++ )
            {
                if ( ! _iter_ptr->process.compare((ptr+i)->process) )
                {
                    /* If the process is found then mark it as failed and update its severity.
                     * At this point we then assume that there is an alarm raised for this process. */
                    found = true ;

                   (ptr+i)->failed = false ;
                    wlog ("%s process was failed critical ; clearing existing alarm\n", _iter_ptr->process.c_str() );
                    pmonAlarm_clear ( get_ctrl_ptr()->my_hostname, PMON_ALARM_ID__PMOND, _iter_ptr->process );
                }
            }

            /* if not found then just clear the alarm */
            if ( found == false)
            {
                wlog ("%s process alarm clear ; not in current process profile\n", _iter_ptr->process.c_str() );
                pmonAlarm_clear ( get_ctrl_ptr()->my_hostname, PMON_ALARM_ID__PMOND, _iter_ptr->process );
            }
        }
    }
}

void pmon_service ( pmon_ctrl_type * ctrl_ptr )
{
    std::list<int> socks ;
    struct timeval waitd;
    fd_set readfds;
    int  select_fail_count = 0 ;
    int  flush_thld        = 0 ;
    int  rc                = PASS ;
    int  shutdown_log_throttle = 0;

    /* iNotify stuff */
    bool inotify_fault = false ;

    daemon_config_type * cfg_ptr  = daemon_get_cfg_ptr ();
    pmon_socket_type   * sock_ptr = pmon_getSock_ptr ();
    int select_timeout = (cfg_ptr->audit_period*100);
    int audit_period   = (cfg_ptr->audit_period/10);
    int pulse_period   = cfg_ptr->audit_period ;
    int hostwd_period  = (cfg_ptr->hostwd_update_period);
    int degrade_period = (cfg_ptr->audit_period/50);

    if ( audit_period == 0 ) audit_period = 10 ;
    if ( degrade_period == 0 ) degrade_period = 10 ;

    ilog ("Starting to monitor processes\n");
    pmon_send_hostwd ( );

    /* Load and register generic processes - not subfunction processes */
    load_processes ();

    /* Setup inotify to watch CONFIG_DIR */
    if ( set_inotify_watch ( CONFIG_DIR, ctrl_ptr->fd, ctrl_ptr->wd ) )
        inotify_fault = true ;

    socks.clear();
    socks.push_front (sock_ptr->cmd_sock->getFD());
    socks.push_front (sock_ptr->event_sock->getFD());
    socks.push_front (sock_ptr->amon_sock);
    socks.sort();

    ilog ("Starting 'Audit' timer (%d secs)\n", audit_period );
    mtcTimer_start ( pmonTimer_audit, pmon_timer_handler, audit_period );

    ilog ("Starting 'Degrade Audit' timer (%d secs)\n", degrade_period );
    mtcTimer_start ( pmonTimer_degrade, pmon_timer_handler, degrade_period );

    ilog ("Starting 'Pulse' timer (%d secs)\n", pulse_period );
    mtcTimer_start_msec ( pmonTimer_pulse, pmon_timer_handler, pulse_period );

    ilog ("Starting 'Host Watchdog' timer (%d secs)\n", hostwd_period );
    mtcTimer_start ( pmonTimer_hostwd, pmon_timer_handler, hostwd_period );

    for ( ; ; )
    {
        /* Accomodate for hup reconfig */
        select_timeout = (cfg_ptr->audit_period*100);
        audit_period   = (cfg_ptr->audit_period/10);
        degrade_period = (cfg_ptr->audit_period/50);

        if (   audit_period < 1 ) audit_period   = 10 ;
        if ( degrade_period < 1 ) degrade_period = 10 ;

        daemon_signal_hdlr ();

        /* Initialize the master fd_set */
        FD_ZERO(&readfds);
        if ( sock_ptr->cmd_sock->getFD() )
        {
            FD_SET(sock_ptr->cmd_sock->getFD(), &readfds);
        }
        if ( sock_ptr->event_sock->getFD() )
        {
            FD_SET(sock_ptr->event_sock->getFD(), &readfds);
        }
        if ( sock_ptr->amon_sock )
        {
            FD_SET(sock_ptr->amon_sock, &readfds);
        }

        waitd.tv_sec  = 0;
        waitd.tv_usec = select_timeout ;

        /* This is used as a delay up to select_timeout */
        rc = select( socks.back()+1, &readfds, NULL, NULL, &waitd);
        /* If the select time out expired then  */
        if (( rc < 0 ) || ( rc == 0 ))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                wlog_throttled ( select_fail_count, 20,
                                 "Socket Select Failed (rc:%d) %s \n",
                                 errno, strerror(errno));
            }
        }
        else
        {
            if ( FD_ISSET(sock_ptr->cmd_sock->getFD(), &readfds))
            {
                pmon_service_inbox ();
            }

            if (FD_ISSET(sock_ptr->amon_sock, &readfds))
            {
                amon_service_inbox  ( _pmon_ctrl_ptr->processes );
            }
        }

        if (pmonTimer_pulse.ring == true )
        {
            pmonTimer_pulse.ring = false ;
            /* Send a I'm Alive message to the pulse interface */
            /* Robustness Update: Added an event_mode bool that will
             * be true if the kernel supports notification of death
             * of arbitrary process patch. If that feature is not present
             * then allow pmon to operate but in a degraded state. Eventually
             * we can turn this into a customer alarm/log.
             * Degrade is acheived by not sending the pulses to the watcher.
             */
            if ( ctrl_ptr->event_mode == true )
            {
                pmon_send_pulse ( );
            }
        }

        /* Avoid pmond thrashing trying to recover processes during
         * system shutdown. */
        if ( _pmon_ctrl_ptr->system_state == MTC_SYSTEM_STATE__STOPPING )
        {
            wlog_throttled ( shutdown_log_throttle, 500,
                             "process monitoring disabled during system shutdown\n");
            usleep (500);
            continue ;
        }
        if ( shutdown_log_throttle ) shutdown_log_throttle = 0 ;

        if ( inotify_fault == false )
        {
            if ( get_inotify_events ( ctrl_ptr->fd ) == true )
            {
                if ( _pmon_ctrl_ptr->reload_config == false )
                {
                    _pmon_ctrl_ptr->reload_config = true ;
                    ilog ("Setting config reload flag\n");

                    /* Hijack the audit timer for the next period for config reload */
                    mtcTimer_reset (pmonTimer_degrade);
                    if ( daemon_is_file_present ( PATCHING_IN_PROG_FILE ) == true )
                    {
                        _pmon_ctrl_ptr->patching_in_progress = true ;
                        wlog ("Patching in progress ; delaying config reload by 30 secs...\n");
                        mtcTimer_start ( pmonTimer_degrade, pmon_timer_handler, (degrade_period + 30) );
                    }
                    else
                    {
                        mtcTimer_start ( pmonTimer_degrade, pmon_timer_handler, degrade_period );
                    }
                }
            }
        }

        if ( pmonTimer_hostwd.ring == true )
        {
            /* inservice recovery from hostw connection failures */
            if ( sock_ptr->hostwd_sock == 0 )
            {
                hostwd_port_init();
            }
            if ( ctrl_ptr->event_mode == true )
            {
                pmon_send_hostwd ( );
                pmonTimer_hostwd.ring = false;
            }
        }

        /* Run Get Events by audit timer */
        if (pmonTimer_audit.ring == true )
        {
            _get_events ();
            mtcTimer_start ( pmonTimer_audit, pmon_timer_handler, audit_period );
        }

        /* Run the degrade set/clear by audit */
        if (pmonTimer_degrade.ring == true )
        {
            /* run the degrade clear audit */
            if ( want_degrade_clear () == true )
            {
                dlog ("sending degrade clear\n");
                pmon_send_event ( MTC_EVENT_PMON_CLEAR, NULL ) ;
            }
            else
            {
                dlog ("sending degrade assert\n");
                // pmon_send_event ( MTC_EVENT_PMON_MAJOR, &process_config[0] ) ;
            }

            /* Check for config reload state request */
            if ( _pmon_ctrl_ptr->reload_config == true )
            {
                /* But defer it while there is a process in the
                 * manually requested restart state */
                bool restart_request_active = false ;
                for ( int i = 0 ; i < ctrl_ptr->processes ; i++ )
                {
                    if ( process_config[i].restart == true )
                    {
                        /* Added as fix */
                        wlog ("deferring process config reload to next audit\n");
                        wlog ("... while manual restart of '%s' is in progress\n",
                               process_config[i].process );
                        restart_request_active = true ;
                        break ;
                    }
                }
                if ( restart_request_active == false )
                {
                    load_processes ();
                }
            }
            mtcTimer_start ( pmonTimer_degrade, pmon_timer_handler, degrade_period );
        }

        /* Get_events run by forced audit or not in event mode */
        else if (( ctrl_ptr->run_audit == true ) ||
                 ( ctrl_ptr->event_mode == false ))
        {
            _get_events ( );
        }

        /* Monitor Processes */
        for ( int i = 0 ; i < ctrl_ptr->processes ; i++ )
        {
            /* Allow a process to be ignored */
            if ( process_config[i].ignore == true )
            {
                process_config[i].failed = false ;
                process_config[i].active_failed = false ;

                /* Handle process auto recovery from stopped state */
                if (( process_config[i].pt_ptr->ring == true ) && ( process_config[i].stopped == true ))
                {
                    elog ("%s process was stopped but never restarted ; auto recovery in progress\n", process_config[i].process );
                    process_config[i].stopped = false ;
                    process_config[i].ignore  = false ;
                    passiveStageChange ( &process_config[i], PMON_STAGE__MANAGE );
                }
                continue ;
            }
            else if ( process_config[i].status_monitoring )
            {
                pmon_status_handler ( &process_config[i] );
            }
            else if (( process_config[i].stage == PMON_STAGE__POLLING ) ||
                     ( process_config[i].stage == PMON_STAGE__START_WAIT ) ||
                     ( process_config[i].restart == true ) ||
                     ( process_config[i].failed == true ))
            {
                /* Run the FSM for this failed process */
                pmon_passive_handler ( &process_config[i] ) ;
            }
            else if (( process_config[i].active_monitoring ) &&
                     ( process_config[i].stage == PMON_STAGE__MANAGE ))
            {
                // if ( process_config[i].active_failed == false )
                if ( process_config[i].failed == false )
                {
                    pmon_active_handler ( &process_config[i] );
                }
                else
                {
                    elog ("%s Failed Active Monitoring ... recovering.\n", process_config[i].process );
                    manage_process_failure ( &process_config[i]) ;
                }
            }

            /* Audit to ensure that running processes are
             * registered with the kernel */
            if (( process_config[i].stage == PMON_STAGE__MANAGE ) &&
                ( process_config[i].registered == false ) &&
                ( _pmon_ctrl_ptr->event_mode ) &&
                ( process_config[i].restart == false ) &&
                ( process_config[i].failed == false ) &&
                ( process_config[i].ignore == false ))
            {
                int pid = get_process_pid ( &process_config[i] );
                if ( pid )
                {
                    if ( kill (pid, 0 ) == 0 )
                    {
                        process_config[i].pid = pid ;
                        register_process ( &process_config[i] );
                    }
                }
            }
        }

        /* Debugging */
        if (daemon_get_cfg_ptr()->debug_level & 1 )
        {
            char proc_mask [MAX_PROCESSES*2] ;
            bool somefailed = false ;
            memset (&proc_mask[0], 0, sizeof(proc_mask));
            for ( int x = 0 , y = 0 ; x < ctrl_ptr->processes ; x++, y+=2 )
            {
                if ( process_config[x].failed )
                {
                    proc_mask[y] = '1' ;
                    somefailed = true ;
                }
                else
                    proc_mask[y] = '0' ;
                proc_mask[y+1] = ' ' ;
            }
            if ( somefailed )
            {
                alog ( "Process Mask: %s\n", &proc_mask[0] );
            }
        }

        /* Support the log flush config option */
        if ( cfg_ptr->flush )
        {
            if ( ++flush_thld > cfg_ptr->flush )
            {
                flush_thld = 0 ;
                fflush (stdout);
                fflush (stderr);
            }
        }

    }
}

string get_status_failure_text ( process_config_type * ptr )
{
    string extra_text("");
    if(( ptr->status_failure_text_file != NULL ) &&
       ( ptr->status_failure_text_file[0] != '\0'))
    {
        FILE * status_text_file_stream  =
                fopen ( ptr->status_failure_text_file, "r" );
        if ( status_text_file_stream == NULL )
        {
           wlog (" Failed to get extra alam text from file  %s\n",
                 ptr->status_failure_text_file );
        }
        else
        {
            char buffer[MAX_STATUS_ERROR_TEXT_LEN];
            if ( fgets(buffer, MAX_STATUS_ERROR_TEXT_LEN,
                 status_text_file_stream) != NULL)
            {
               extra_text = buffer;
            }
            fclose(status_text_file_stream);
        }
    }
    return extra_text;
}

/****************************************************************************
 *
 * Name       : quorum_process_failure
 *
 * Description: manage debounce and log report of quorum process failure
 *
 * Warnings   : Only call this when there is a quorum process faiure
 *              that has exceeded the threshold count.
 *
 ****************************************************************************/
void quorum_process_failure ( process_config_type * ptr )
{
    wlog ("%s quorum process %s\n",
              ptr->process,
              ptr->quorum_failure ? "unrecoverable" : "failed" );

    if ( ptr->quorum_failure == true )
    {
        ptr->quorum_unrecoverable = true;
    }
    else
    {
        ptr->quorum_failure = true;
    }
}

