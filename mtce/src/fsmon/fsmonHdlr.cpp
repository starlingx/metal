/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform File System Monitor Service Handler
  */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using namespace std;

#include "fsmon.h"
#include "nodeEvent.h"

#define FILE_TEST_DATA  "TEST-FILE"

typedef char FileNameT[PATH_MAX];

static FileNameT _files[] =
{ "/.fs-test",
  "/scratch/.fs_test",
  "/var/log/.fs_test",
  "/var/run/.fs_test",
  "/var/lock/.fs_test",
  ""
};

static struct mtc_timer mtcTimer_audit ;

/*******************************************************************
 *                   Module Utilities                              *
 ******************************************************************/
/* SIGCHLD handler support - for waitpid */
void daemon_sigchld_hdlr ( void )
{
    ilog("Received SIGCHLD ...\n");
}

/* Looks up the timer ID and asserts the corresponding ringer */
void fsmon_timer_handler ( int sig, siginfo_t *si, void *uc)
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

    /* is event ids fsmon timer */
    if ( *tid_ptr == mtcTimer_audit.tid )
    {
        mtcTimer_stop_int_safe ( mtcTimer_audit );
        mtcTimer_audit.ring = true ;
        return ;
    }
    mtcTimer_stop_tid_int_safe (tid_ptr);
}


// ****************************************************************************
// Do File Test
// ============
static bool do_file_test( char filename[] )
{
    int fd = -1;
    char test_data[sizeof(FILE_TEST_DATA)*2];
    ssize_t result;
    bool success = false;

    memset( test_data, 0, sizeof(test_data) );

    // File write test.
    fd = open( filename, O_RDWR | O_CREAT | O_CLOEXEC,
               S_IRUSR | S_IRGRP | S_IROTH );
    if( 0 > fd )
    {
        dlog( "Failed to open %s for writing, error=%s.",
                 filename, strerror(errno) );
        success = (EINTR == errno);
        goto ERROR;
    }

    result = write( fd, FILE_TEST_DATA, sizeof(FILE_TEST_DATA) ); 
    if( 0 > result )
    {
        dlog( "Write to %s failed, error=%s.", filename,
                 strerror(errno) );
        success = (EINTR == errno);
        goto ERROR;
    }

    close( fd );
    fd = -1;

    // File read test.
    fd = open( filename, O_RDONLY | O_CLOEXEC );
    if( 0 > fd )
    {
        dlog( "Failed to open %s for reading, error=%s.", filename,
                 strerror(errno) );
        success = (EINTR == errno);
        goto ERROR;
    }

    result = read( fd, test_data, sizeof(test_data) );
    if( 0 > result )
    {
        dlog( "Read of %s failed, error=%s.", filename,
                 strerror(errno) );
        success = (EINTR == errno);
        goto ERROR;
    }

    test_data[sizeof(test_data)-1] = '\0';

    if( 0 != strcmp( FILE_TEST_DATA, test_data ) )
    {
        dlog( "Read data from %s does not match, error=%s.", filename,
                 strerror(errno) );
        success = false;
        goto ERROR;
    }

    close( fd );
    fd = -1;

    // Delete file test.
    result = remove( filename );
    if( 0 > result )
    {
        dlog( "Failed to delete %s, error=%s.", filename,
                 strerror(errno) );
        success = (EINTR == errno);
        goto ERROR;
    }

    return( true );

ERROR:
    if( 0 <= fd )
    {
        close( fd );
    }

    remove( filename );

    return( success );
}

void fsmon_service ( unsigned int nodetype )
{
    int flush_thld = 0 ;
    daemon_config_type * cfg_ptr  = daemon_get_cfg_ptr ();

    ilog ("Starting 'Audit' timer (%d secs)\n", cfg_ptr->audit_period );
    mtcTimer_start ( mtcTimer_audit, fsmon_timer_handler, cfg_ptr->audit_period ); 

    for ( ; ; )
    {
        if (mtcTimer_audit.ring == true )
        {
            mtcTimer_audit.ring = false ;

            /* only support stall monitor on computes */
            if (( nodetype & COMPUTE_TYPE) == COMPUTE_TYPE )
            {
                int file_i;
                int rc = PASS ;
                for( file_i=0; '\0' != _files[file_i][0]; ++file_i )
                {
                    if( do_file_test( _files[file_i] ) )
                    {
                        dlog( "File (%s) test passed\n", _files[file_i] );
                    }
                    else
                    {
                        wlog( "File (%s) test failed\n", _files[file_i] );
                        rc = FAIL ;
                    }
                }
                if ( rc == PASS )
                {
                    ilog ("tests passed\n");
                }
            }
            mtcTimer_start ( mtcTimer_audit, fsmon_timer_handler, cfg_ptr->audit_period );
        }

        daemon_signal_hdlr ();

        /* Support the log flush config option */
        if ( cfg_ptr->flush )
        {
            if ( ++flush_thld > cfg_ptr->flush_thld )
            {
                flush_thld = 0 ;
                fflush (stdout);
                fflush (stderr);
            }
        }
        usleep (500000);
    }
}
