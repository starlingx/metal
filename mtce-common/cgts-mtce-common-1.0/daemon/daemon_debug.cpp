/*
* Copyright (c) 2013-2014, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/


#include <iostream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

using namespace std;

#include "daemon_ini.h"    /* Init parset header       */
#include "daemon_common.h" /* Init parset header       */
#include "nodeBase.h"

static char time_buff [50] ;
static const char null_t [25] = "YYYY:MM:DD HH:MM:SS.xxx";

unsigned long long  gettime_monotonic_nsec ( void )
{
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ((unsigned long long) ts.tv_sec) * 1000000000ULL + ts.tv_nsec; 
}
    
int timedelta ( time_debug_type & before , time_debug_type & after, time_delta_type & delta )
{ 
    /* Subtract before from after */

    if ((after.ts.tv_sec < before.ts.tv_sec) ||
        ((after.ts.tv_sec == before.ts.tv_sec) &&
         (after.ts.tv_nsec <= before.ts.tv_nsec))) 
    {
        delta.secs = delta.msecs = 1 ;
    } 
    else 
    {
        delta.secs = after.ts.tv_sec - before.ts.tv_sec ;
        if (after.ts.tv_nsec < before.ts.tv_nsec)
        {
            delta.msecs = after.ts.tv_nsec + 1000000000L - before.ts.tv_nsec ;
            delta.secs-- ;
        }
        else
        {
            delta.msecs = after.ts.tv_nsec - before.ts.tv_nsec ;
        }
        delta.msecs = (delta.msecs/1000);
    }
    return (PASS) ;
}

int gettime ( time_debug_type & p ) 
{   
    int    len ;
    clock_gettime (CLOCK_REALTIME, &p.ts );
    if (localtime_r(&(p.ts.tv_sec), &p.t) == NULL)
    {
        return (FAIL);
    }
    else
    {
        len = strftime(&p.time_buff[0], 30, "%H:%M:%S.", &p.t );
        sprintf (&p.time_buff[len], "%06ld", (p.ts.tv_nsec/1000) );
    }
    return (PASS);
}

/* Log counter */
static int __lc = 0 ;
int lc (void) /* returns the current log count */
{
   return(__lc++);
}


char * pt ( void ) 
{   
    struct timespec ts ;
    struct tm t;
    int    len ;

    clock_gettime (CLOCK_REALTIME, &ts );
    if (localtime_r(&(ts.tv_sec), &t) == NULL)
    {
        return ((char*)&null_t[0]);
    }
    len = strftime(time_buff, 30, "%FT%H:%M:%S.", &t );
    sprintf ( &time_buff[len], "%03ld", (ts.tv_nsec/1000000) );

    return (&time_buff[0]);
}

/*****************************************************************************
 *
 * Name       : future_time
 *
 * Description: Return a future time date:time formatted string
 *              that represents current time + specified seconds.
 *
 *****************************************************************************/

char * future_time ( int secs )
{
    struct timespec ts ;
    struct tm t;
    int    len ;

    clock_gettime (CLOCK_REALTIME, &ts );
    /* add the caller's seconds */
    ts.tv_sec += secs ;

    if (localtime_r(&(ts.tv_sec), &t) == NULL)
    {
        return ((char*)&null_t[0]);
    }
    len = strftime(time_buff, 30, "%FT%H:%M:%S.", &t );
    sprintf ( &time_buff[len], "%03ld", (ts.tv_nsec/1000000) );

    return (&time_buff[0]);
}


/* Debug config read */
int debug_config_handler (        void * user, 
                            const char * section,
                            const char * name,
                            const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;
    if (MATCH("debug", "debug_timer"))
    {
        config_ptr->debug_timer = atoi(value);
        if ( config_ptr->debug_timer )
        {
            ilog ("Timer Debug : %x\n", config_ptr->debug_timer );
        }
    } 
    else if (MATCH("debug", "debug_json"))
    {
        config_ptr->debug_json = atoi(value);
        if ( config_ptr->debug_json )
        {
            ilog (" Json Debug : %x\n", config_ptr->debug_json );
        }
    } 
    else if (MATCH("debug", "debug_fsm"))
    {
        config_ptr->debug_fsm = atoi(value);
        if ( config_ptr->debug_fsm )
        {
            ilog (" FSM  Debug : %x\n", config_ptr->debug_fsm );
        }
    }
    else if (MATCH("debug", "debug_alive"))
    {
        config_ptr->debug_alive = atoi(value);
        if ( config_ptr->debug_alive )
        {
            ilog ("Alive Debug : %x\n", config_ptr->debug_alive );
        }
    } 
    else if (MATCH("debug", "debug_bm"))
    {
        config_ptr->debug_bmgmt = atoi(value);
        if ( config_ptr->debug_bmgmt )
        {
            ilog ("BMgmt Debug : %x\n", config_ptr->debug_bmgmt );
        }
    } 
    else if (MATCH("debug", "debug_http"))
    {
        config_ptr->debug_http = atoi(value);
        if ( config_ptr->debug_http )
        {
            ilog (" Http Debug : %x\n", config_ptr->debug_http );
        }
    } 
    else if (MATCH("debug", "debug_hdlr"))
    {
        config_ptr->debug_http = atoi(value);
        if ( config_ptr->debug_http )
        {
            ilog (" Http Debug : %x\n", config_ptr->debug_http );
        }
    } 
    else if (MATCH("debug", "debug_msg"))
    {
        config_ptr->debug_msg = atoi(value);
        if ( config_ptr->debug_msg )
        {
            ilog (" Msg  Debug : %x\n", config_ptr->debug_msg );
        }
    }
    else if (MATCH("debug", "debug_work"))
    {
        config_ptr->debug_work = atoi(value);
        if ( config_ptr->debug_work )
        {
            ilog (" Work Debug : %x\n", config_ptr->debug_work );
        }
    }
    else if (MATCH("debug", "debug_state"))
    {
        config_ptr->debug_state = atoi(value);
        if ( config_ptr->debug_state )
        {
            ilog ("State Debug : %x\n", config_ptr->debug_state );
        }
    }    
    else if (MATCH("debug", "debug_level"))
    {
        config_ptr->debug_level = atoi(value);
        if ( config_ptr->debug_level )
        {
            ilog ("Level Debug : %x\n", config_ptr->debug_level );
        }
    }
    else if (MATCH("debug", "debug_all"))
    {
        config_ptr->debug_all = atoi(value) ;
        if ( config_ptr->debug_all )
        {
            ilog ("Globl Debug : %x\n", config_ptr->debug_all );
            config_ptr->debug_timer = atoi(value);
            config_ptr->debug_json  = atoi(value);
            config_ptr->debug_alive = atoi(value);
            config_ptr->debug_bmgmt = atoi(value);
            config_ptr->debug_msg   = atoi(value);
            config_ptr->debug_work  = atoi(value);
            config_ptr->debug_http  = atoi(value);
            config_ptr->debug_state = atoi(value);
            config_ptr->debug_level = atoi(value);
        }
    }
    else if (MATCH("debug", "flush"))
    {
        config_ptr->flush = atoi(value);
    }    
    else if (MATCH("debug", "flush_thld"))
    {
        config_ptr->flush_thld = atoi(value);
    }
    else if (MATCH("debug", "debug_filter"))
    {
        config_ptr->debug_filter = strdup(value);
        if (( config_ptr->debug_filter ) &&
            ( strnlen( config_ptr->debug_filter, 20 ) > 0 ) &&
            ( strcmp ( config_ptr->debug_filter, "none" )))
        {
            ilog ("State Filter: %s\n", config_ptr->debug_filter );
        }
    }
    else if (MATCH("debug", "debug_event"))
    {
        config_ptr->debug_event = strdup(value);
        if (( config_ptr->debug_event ) &&
            ( strnlen( config_ptr->debug_event, 20 ) > 0 ) &&
            ( strcmp ( config_ptr->debug_event, "none" )))
        {
            ilog ("Event Filter: %s\n", config_ptr->debug_event );
        }
    }
    else if (MATCH("debug", "infra_degrade_only"))
    {
        config_ptr->infra_degrade_only = atoi(value);
        if ( config_ptr->infra_degrade_only )
        {
            ilog ("Infra Degrad: true\n" );
        }
    }
    else if (MATCH("debug", "testmode"))
    {
        config_ptr->testmode = atoi(value);
        if ( config_ptr->testmode )
        {
            ilog ("Stress Mode : Enabled\n");
        }
    }
    else if (MATCH("debug", "testmask"))
    {
        config_ptr->testmask = atoi(value);
        if ( config_ptr->testmask )
        {
            ilog ("Stress Mask : %x\n", config_ptr->testmask );
        }
    }
    else if (MATCH("debug", "fit_code"))
    {
        config_ptr->fit_code = atoi(value);
        if ( config_ptr->fit_code )
        {
            ilog ("FIT Code    : %d\n", config_ptr->fit_code );
        }
    }
    else if (MATCH("debug", "fit_host"))
    {
        config_ptr->fit_host = strdup(value);
        if ( config_ptr->fit_host )
        {
            ilog ("FIT host    : %s\n", config_ptr->fit_host );
        }
    }
    else if (MATCH("debug", "stall_pmon_thld"))
    {
        config_ptr->stall_pmon_thld = atoi(value);
    }
    else if (MATCH("debug", "stall_mon_period"))
    {
        config_ptr->stall_mon_period = atoi(value);
    }
    else if (MATCH("debug", "stall_poll_period"))
    {
        config_ptr->stall_poll_period = atoi(value);
    }
    else if (MATCH("debug", "stall_rec_thld"))
    {
        config_ptr->stall_rec_thld = atoi(value);
    }
    else if (MATCH("debug", "mon_process_1"))
    {
        config_ptr->mon_process_1 = strdup(value);
    }
    else if (MATCH("debug", "mon_process_2"))
    {
        config_ptr->mon_process_2 = strdup(value);
    }
    else if (MATCH("debug", "mon_process_3"))
    {
        config_ptr->mon_process_3 = strdup(value);
    }
    else if (MATCH("debug", "mon_process_4"))
    {
        config_ptr->mon_process_4 = strdup(value);
    }
    else if (MATCH("debug", "mon_process_5"))
    {
        config_ptr->mon_process_5 = strdup(value);
    }
    else if (MATCH("debug", "mon_process_6"))
    {
        config_ptr->mon_process_6 = strdup(value);
    }
    else if (MATCH("debug", "mon_process_7"))
    {
        config_ptr->mon_process_7 = strdup(value);
    }
    else if (MATCH("debug", "latency_thld"))
    {
        config_ptr->latency_thld = atoi(value);
    }

    return (PASS);
}

void get_debug_options ( const char * init_file , daemon_config_type * config_ptr )
{
    ilog("Config File : %s\n", init_file );
    if (ini_parse ( init_file, debug_config_handler, config_ptr ) < 0)
    {
        elog("Failed to load '%s'\n", init_file );
    }
}


void daemon_do_segfault ( void )
{
    char * ptr = NULL ;
    ilog ("FIT segfault at %p:%d\n", ptr, *ptr);
}
