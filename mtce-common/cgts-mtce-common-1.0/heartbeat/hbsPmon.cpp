/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Nodal Health Check Client Daemon
  */

// #include <dirent.h>
#include <fcntl.h>
#include <syslog.h>    /* for ... syslog                  */

using namespace std;

#include "nodeBase.h"
#include "nodeUtil.h"      /* Common utilities                             */
#include "hbsBase.h"       /* Heartbeat Base Header File                   */
#include "nodeTimers.h"    /* mtcTimer utilities                           */
#include "daemon_common.h" /* Common definitions and types for daemons     */

#define TEST_FILE (const char *)"/tmp/hbsClient.test"

int hbs_refresh_pids ( std::list<procList> & proc_list )
{
    int count = 0 ;
    std::list<procList>:: iterator proc_ptr ;
    for ( proc_ptr  = proc_list.begin();
          proc_ptr != proc_list.end();
          proc_ptr++ )
    {
        string procname = proc_ptr->proc.data() ;
        proc_ptr->pid = get_pid_by_name_proc( procname );
        count++ ;
    }
    return (count);
}


#define MAX_SCHEDSTAT_LEN (128)
int hbs_process_monitor ( std::list<procList> & proc_list )
{
    char file_path [MAX_FILENAME_LEN] ;
    char schedstat [MAX_SCHEDSTAT_LEN] ;
    std::list<procList>:: iterator proc_ptr ;

    FILE * fp ;

    for ( proc_ptr  = proc_list.begin();
          proc_ptr != proc_list.end();
          proc_ptr++ )
    {
        proc_ptr->status = FAIL ;

        // ilog ("Monotoring: %s (pid:%d)\n", proc_ptr->proc.c_str(), proc_ptr->pid );
        if ( proc_ptr->pid == -1 )
        {
            continue ;
        }

        snprintf ( &file_path[0], MAX_FILENAME_LEN, "/proc/%d/schedstat", proc_ptr->pid );
        fp = fopen (file_path, "r" );
        if ( fp )
        {
            memset ( schedstat, 0 , MAX_SCHEDSTAT_LEN );
            char * str = fgets ( &schedstat[0], MAX_SCHEDSTAT_LEN, fp );
            UNUSED(str);
            if ( strlen(schedstat) )
            {
                if ( sscanf ( schedstat , "%*s %*s %llu", &(proc_ptr->this_count)) >= 1 )
                {
                    dlog ("%s: %llu\n", proc_ptr->proc.c_str(), proc_ptr->this_count );
                    proc_ptr->status = PASS ;
                }
                else
                {
                    dlog ("Failed to get schedstat from (%s)\n", file_path);
                }
            }
            else
            {
                dlog ("failed to read from (%s)\n", file_path );
            }
            fclose(fp);
        }
        else
        {
            dlog ("Failed to open (%s)\n", file_path);
        }
    }
    return (PASS);
}
    
struct mtc_timer _timer ;

void hbs_recovery_timer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;
   
    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    UNUSED(sig);
    UNUSED(uc); 
    if ( !(*tid_ptr) )
    {
        tlog ("Called with a NULL Timer ID\n");
        return ;
    }
    else if (( *tid_ptr == _timer.tid ) )
    {
        _timer.ring = true ;
    }
}


int hbs_self_recovery ( unsigned int cmd )
{
    // char cmd[2048];
    pid_t pid = 0 ;
    
    /* Reboot Command */
    if ( cmd == STALL_REBOOT_CMD )
    {
        elog ("Forking Self-Recovery Reboot Action\n");

        // Fork child to do the reboot.
        pid = fork();
        if( 0 > pid )
        {
            return (FAIL);
        } 
        else if( 0 == pid )
        {

            char  reboot_cmd[] = "reboot";
            char* reboot_argv[] = {reboot_cmd, NULL};
            char* reboot_env[] = {NULL};
            
            bool close_file_descriptors = true ;
            setup_child ( close_file_descriptors );

            syslog ( LOG_INFO, "child");

            mtcTimer_init ( _timer );
            mtcTimer_start( _timer, hbs_recovery_timer_handler, 10 );

            while( true )
            {
                if ( _timer.ring == true )
                {
                    syslog ( LOG_INFO, "issuing reboot");

                    execve( "/sbin/reboot", reboot_argv, reboot_env );
                    break ;
                }
                syslog ( LOG_INFO, "waiting for reboot timer ...");

                sleep( 10 ); // 10 seconds
            }
            sleep (10); 
        
            // Shouldn't get this far, else there was an error.
            exit(-1);
        }
        else
        {
            /* parent returns */
            return (PASS);
        }
    }
    /* Forced Self Reset Now */
    else if ( cmd == STALL_SYSREQ_CMD )
    {
        fork_sysreq_reboot ( 60 ) ;

        /* parent returns */
        return (PASS);
    }
    else
    {
        return (FAIL);
    }
}
