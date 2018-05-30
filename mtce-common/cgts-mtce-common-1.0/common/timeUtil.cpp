/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Time Utility Header
  */

#include "timeUtil.h"

typedef struct
{
    bool            init ;

    time_debug_type last_time ;
    time_debug_type this_time ;
    time_delta_type diff_time ;

    unsigned long under_50_msec ;
    unsigned long under_500_msec;
    unsigned long under_1_sec   ;
    unsigned long under_2_sec   ;
    unsigned long under_3_sec   ;
    unsigned long under_5_sec   ;
    unsigned long  over_5_sec   ;

    unsigned long counter ;
} timeUtil_type ;

static timeUtil_type time_struct ;

void timeUtil_sched_init ( void )
{
    gettime ( time_struct.last_time );
    time_struct.under_50_msec  = 0 ;
    time_struct.under_500_msec = 0 ;
    time_struct.under_1_sec    = 0 ;
    time_struct.under_2_sec    = 0 ;
    time_struct.under_3_sec    = 0 ;
    time_struct.under_5_sec    = 0 ;
    time_struct.over_5_sec     = 0 ;
    time_struct.counter        = 0 ;
    time_struct.init           = true ;
}

void scheduling_histogram ( void )
{
    ilog ("Under: 50ms: %ld - 500ms:%ld - 1s:%ld - 2s:%ld - 3s:%ld - 5s:%ld ---- over:%ld\n", 
              time_struct.under_50_msec, 
              time_struct.under_500_msec, 
              time_struct.under_1_sec, 
              time_struct.under_2_sec, 
              time_struct.under_3_sec, 
              time_struct.under_5_sec, 
              time_struct.over_5_sec);
}

void timeUtil_sched_sample ( void )
{
    if ( time_struct.init == false )
    {
        elog ("Time struct not initialized\n");
        return ;
    }
    gettime   ( time_struct.this_time );
    timedelta ( time_struct.last_time, time_struct.this_time, time_struct.diff_time );
    
    if ( time_struct.diff_time.secs == 0 )
    {
             if ( time_struct.diff_time.msecs < 50000  ) time_struct.under_50_msec++  ;
        else if ( time_struct.diff_time.msecs < 500000 ) time_struct.under_500_msec++ ;
        else                                         time_struct.under_1_sec++    ;
    }
    else
    {
        if ( time_struct.diff_time.secs < 2 )
        {
            time_struct.under_2_sec++ ;
        }
        else if ( time_struct.diff_time.secs < 3 )
        {
            wlog (">>> Minor Scheduling delay: %ld.%3ld secs\n",
                       time_struct.diff_time.secs,
                       time_struct.diff_time.msecs );
            time_struct.under_3_sec++ ;
        }
        else if ( time_struct.diff_time.secs < 5 )
        {
            wlog (">>> Major Scheduling delay: %ld.%3ld secs\n",
                       time_struct.diff_time.secs,
                       time_struct.diff_time.msecs );
            time_struct.under_5_sec++ ;
            scheduling_histogram ( );
        }
        else
        {
            wlog (">>> Critical Scheduling delay: %ld.%3ld secs\n",
                       time_struct.diff_time.secs,
                       time_struct.diff_time.msecs ); 
            time_struct.over_5_sec++ ;
            scheduling_histogram ( );
        }
    }
    time_struct.last_time.ts.tv_sec  = time_struct.this_time.ts.tv_sec ;
    time_struct.last_time.ts.tv_nsec = time_struct.this_time.ts.tv_nsec ;

    if ( ++time_struct.counter >= 1000 )
    {
        scheduling_histogram ( );
        time_struct.counter = 0 ;
    }
}
