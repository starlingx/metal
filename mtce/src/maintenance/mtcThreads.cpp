
/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance - Threading implementation"
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <linux/unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <pthread.h>
#define gettid() syscall(SYS_gettid)

using namespace std;

#include "daemon_common.h"

#include "nodeBase.h"
#include "hostUtil.h"      /* for ... hostUtil_mktmpfile               */
#include "nodeUtil.h"
#include "threadUtil.h"
#include "ipmiUtil.h"      /* for ... IPMITOOL_CMD_FILE_SUFFIX   ...   */
#include "mtcThreads.h"    /* for ... IPMITOOL_THREAD_CMD__RESET ...   */

void * mtcThread_ipmitool ( void * arg )
{
    thread_info_type       * info_ptr  ;
    thread_extra_info_type * extra_ptr ;

    /* Pointer Error Detection and Handling */
    if ( !arg )
    {
        slog ("*** ipmitool thread called with null arg pointer *** corruption\n");
        return NULL ;
    }

    /* cast pointers from arg */
    info_ptr  = (thread_info_type*)arg   ;
    extra_ptr = (thread_extra_info_type*)info_ptr->extra_info_ptr ;

    info_ptr->pw_file_fd = 0 ;

    /* Set cancellation option so that a delete operation can
     * kill this thread immediately */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

    if ( daemon_want_fit ( FIT_CODE__DO_NOTHING_THREAD, info_ptr->hostname ))
    {
        info_ptr->progress++ ;
        info_ptr->runcount++ ;
        pthread_exit (&info_ptr->status );
        return NULL ;
    }

    /* allow the parent to confirm thread id */
    info_ptr->id = pthread_self() ;
    if ( extra_ptr != NULL )
    {
        int rc = PASS ;
        string command  = "" ;
        string response = "" ;

        switch ( info_ptr->command )
        {
            /* control commands */
            case IPMITOOL_THREAD_CMD__POWER_RESET:
            {
                command  = IPMITOOL_POWER_RESET_CMD  ;
                response = IPMITOOL_POWER_RESET_RESP ;
                break ;
            }
            case IPMITOOL_THREAD_CMD__POWER_ON:
            {
                command  = IPMITOOL_POWER_ON_CMD  ;
                response = IPMITOOL_POWER_ON_RESP ;
                break ;
            }
            case IPMITOOL_THREAD_CMD__POWER_OFF:
            {
                command  = IPMITOOL_POWER_OFF_CMD  ;
                response = IPMITOOL_POWER_OFF_RESP ;
                break ;
            }
            case IPMITOOL_THREAD_CMD__POWER_CYCLE:
            {
                command  = IPMITOOL_POWER_CYCLE_CMD  ;
                response = IPMITOOL_POWER_CYCLE_RESP ;
                break ;
            }
            case IPMITOOL_THREAD_CMD__BOOTDEV_PXE:
            {
                command  = IPMITOOL_BOOTDEV_PXE_CMD  ;
                response = IPMITOOL_BOOTDEV_PXE_RESP ;
                break ;
            }

            /* Status commands */
            case IPMITOOL_THREAD_CMD__POWER_STATUS:
            {
                command = IPMITOOL_POWER_STATUS_CMD ;
                break ;
            }
            case IPMITOOL_THREAD_CMD__RESTART_CAUSE:
            {
                command = IPMITOOL_RESTART_CAUSE_CMD ;
                break ;
            }
            case IPMITOOL_THREAD_CMD__MC_INFO:
            {
                command = IPMITOOL_MC_INFO_CMD ;
                break ;
            }

            default:
            {
                rc = info_ptr->status = FAIL_BAD_CASE ;
                info_ptr->data = "unsupported command: " ;
                info_ptr->data.append(itos(info_ptr->command));
                break ;
            }
        }

        if ( rc == PASS )
        {
            bool bypass_ipmitool_request = false ;

            dlog_t ("%s '%s' command\n", info_ptr->log_prefix, command.c_str());

            /* create the password file */
            string password_tempfile = IPMITOOL_OUTPUT_DIR ;
            password_tempfile.append(".") ;
            password_tempfile.append(program_invocation_short_name);
            password_tempfile.append("-");
            password_tempfile.append(info_ptr->hostname);
            password_tempfile.append("-");

            info_ptr->pw_file_fd = hostUtil_mktmpfile (info_ptr->hostname,
                                                       password_tempfile,
                                                       info_ptr->password_file,
                                                       extra_ptr->bm_pw );

            if ( info_ptr->pw_file_fd <= 0 )
            {
                info_ptr->status_string = "failed to get an open temporary password filedesc" ;
                info_ptr->status = FAIL_FILE_CREATE ;
                goto ipmitool_thread_done ;
            }

            if ( info_ptr->pw_file_fd > 0)
                close (info_ptr->pw_file_fd);
            info_ptr->pw_file_fd = 0 ;

            if ( info_ptr->password_file.empty() )
            {
                info_ptr->status_string = "failed to get a temporary password filename" ;
                info_ptr->status = FAIL_FILE_CREATE ;
                goto ipmitool_thread_done ;
            }

            dlog_t ("%s password file: %s\n", info_ptr->log_prefix, info_ptr->password_file.c_str());


            /* create the output filename */
            string ipmitool_datafile = IPMITOOL_OUTPUT_DIR ;
            ipmitool_datafile.append(info_ptr->hostname);

            if ( info_ptr->command == IPMITOOL_THREAD_CMD__MC_INFO )
            {
                ipmitool_datafile.append(IPMITOOL_MC_INFO_FILE_SUFFIX);
            }
            else if ( info_ptr->command == IPMITOOL_THREAD_CMD__RESTART_CAUSE )
            {
                ipmitool_datafile.append(IPMITOOL_RESTART_CAUSE_FILE_SUFFIX);
            }
            else if ( info_ptr->command == IPMITOOL_THREAD_CMD__POWER_STATUS )
            {
                ipmitool_datafile.append(IPMITOOL_POWER_STATUS_FILE_SUFFIX);
            }
            else
            {
                ipmitool_datafile.append(IPMITOOL_CMD_FILE_SUFFIX);
            }

            dlog_t ("%s datafile:%s\n", info_ptr->hostname.c_str(), ipmitool_datafile.c_str());

            /************** Create the ipmitool request **************/
            string ipmitool_request =
            ipmiUtil_create_request ( command,
                                      extra_ptr->bm_ip,
                                      extra_ptr->bm_un,
                                      info_ptr->password_file,
                                      ipmitool_datafile );


            if ( daemon_is_file_present ( MTC_CMD_FIT__DIR ) == true )
            {
                if (( command == IPMITOOL_MC_INFO_CMD ) &&
                    ( daemon_is_file_present ( MTC_CMD_FIT__MC_INFO )))
                {
                    bypass_ipmitool_request = true ;
                    rc = PASS ;
                }
                else if (( command == IPMITOOL_POWER_STATUS_CMD ) &&
                         ( daemon_is_file_present ( MTC_CMD_FIT__POWER_STATUS )))
                {
                    bypass_ipmitool_request = true ;
                    rc = PASS ;
                }
                else if (( command == IPMITOOL_RESTART_CAUSE_CMD ) &&
                         ( daemon_is_file_present ( MTC_CMD_FIT__RESTART_CAUSE )))
                {
                    bypass_ipmitool_request = true ;
                    rc = PASS ;
                }
                else if ((( command == IPMITOOL_POWER_RESET_CMD ) ||
                         (  command == IPMITOOL_POWER_OFF_CMD ) ||
                         (  command == IPMITOOL_POWER_ON_CMD ) ||
                         (  command == IPMITOOL_POWER_CYCLE_CMD ) ||
                         (  command == IPMITOOL_BOOTDEV_PXE_CMD)) &&
                         ( daemon_is_file_present ( MTC_CMD_FIT__POWER_CMD )))
                {
                    slog("%s FIT Bypass power or bootdev command", info_ptr->hostname.c_str());
                    bypass_ipmitool_request = true ;
                    rc = PASS ;
                }
                else if ( daemon_want_fit ( FIT_CODE__AVOID_N_FAIL_IPMITOOL_REQUEST, info_ptr->hostname ))
                {
                    slog ("%s FIT FIT_CODE__AVOID_N_FAIL_IPMITOOL_REQUEST\n", info_ptr->hostname.c_str());
                    bypass_ipmitool_request = true ;
                    rc = FAIL_FIT ;
                }
                else if ( daemon_want_fit ( FIT_CODE__STRESS_THREAD, info_ptr->hostname ))
                {
                    slog ("%s FIT FIT_CODE__STRESS_THREAD\n", info_ptr->hostname.c_str());
                    bypass_ipmitool_request = true ;
                    rc = PASS ;
                }
            }

            dlog_t ("%s %s", info_ptr->hostname.c_str(), ipmitool_request.c_str()); /* ERIC */

            if ( ! bypass_ipmitool_request )
            {
                daemon_remove_file ( ipmitool_datafile.data() ) ;

                nodeUtil_latency_log ( info_ptr->hostname, NODEUTIL_LATENCY_MON_START, 0 );
                rc = system ( ipmitool_request.data()) ;
                if ( rc != PASS )
                {
                    wlog_t ("%s ipmitool system call failed (%d:%d:%m)\n", info_ptr->hostname.c_str(), rc, errno );
                }
                nodeUtil_latency_log ( info_ptr->hostname, "ipmitool system call", 1000 );
            }

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__THREAD_TIMEOUT, info_ptr->hostname ) )
            {
                for ( ; ; )
                {
                    sleep (1) ;
                    pthread_signal_handler ( info_ptr );
                }
            }
            if ( daemon_want_fit ( FIT_CODE__THREAD_SEGFAULT, info_ptr->hostname ) )
            {
                daemon_do_segfault();
            }
#endif
            /* clean-up */
            if ( info_ptr->pw_file_fd > 0 )
                close(info_ptr->pw_file_fd);
            info_ptr->pw_file_fd = 0 ;

            unlink(info_ptr->password_file.data());
            daemon_remove_file ( info_ptr->password_file.data() ) ;
            info_ptr->password_file.clear();

            if ( rc != PASS )
            {
                info_ptr->status_string = "failed ipmitool command : " ;
                info_ptr->status_string.append(getIpmiCmd_str(info_ptr->command));
                info_ptr->status = FAIL_SYSTEM_CALL ;

                if ( ipmitool_request.length () )
                {
                    string _temp = ipmitool_request ;
                    size_t pos1 = _temp.find ("-f", 0) ;
                    size_t pos2 = _temp.find (" > ", 0) ;

                    if (( pos1 != std::string::npos ) && ( pos2 != std::string::npos ))
                    {
                        /* don't log the password filename */
                        wlog_t ("%s ... %s%s\n",
                                  info_ptr->hostname.c_str(),
                                  _temp.substr(0,pos1).c_str(),
                                  _temp.substr(pos2).c_str());
                    }
                    else
                    {
                        wlog_t ("%s ... %s\n",
                                  info_ptr->hostname.c_str(),
                                  ipmitool_request.c_str());
                    }
                }
            }
            else
            {
                bool ipmitool_datafile_present = false ;

                /* look for the output data file */
                for ( int i = 0 ; i < 10 ; i++ )
                {
                    pthread_signal_handler ( info_ptr );
                    if ( daemon_is_file_present ( ipmitool_datafile.data() ))
                    {
                        ipmitool_datafile_present = true ;
                        break ;
                    }
                    info_ptr->progress++ ;
                    sleep (1);
                }

                if ( ipmitool_datafile_present )
                {
                    if ( info_ptr->command == IPMITOOL_THREAD_CMD__MC_INFO )
                    {
                        /* tell the main process the name of the file containing the mc info data */
                        info_ptr->data = ipmitool_datafile ;
                        info_ptr->status_string = "pass" ;
                        info_ptr->status = PASS ;
                    }
                    else
                    {
                        info_ptr->data = daemon_read_file (ipmitool_datafile.data()) ;
                        info_ptr->status_string = "pass" ;
                        info_ptr->status = PASS ;
                    }
                }
                else
                {
                    info_ptr->status_string = "command did not produce output file ; timeout" ;
                    info_ptr->status = FAIL_FILE_ACCESS ;
                }
            }
        }
    }
    else
    {
        info_ptr->status_string = "null 'extra info' pointer" ;
        info_ptr->status = FAIL_NULL_POINTER ;
        goto ipmitool_thread_done ;
    }

ipmitool_thread_done:

    if ( info_ptr->pw_file_fd > 0)
        close (info_ptr->pw_file_fd);
    info_ptr->pw_file_fd = 0 ;

    if ( ! info_ptr->password_file.empty() )
    {
        unlink(info_ptr->password_file.data());
        daemon_remove_file ( info_ptr->password_file.data() ) ;
        info_ptr->password_file.clear();
    }

    pthread_signal_handler ( info_ptr );

    if ( info_ptr->status )
    {
        dlog_t ("%s exit with (rc:%d)\n",
                    info_ptr->log_prefix,
                    info_ptr->status);
    }

    info_ptr->progress++ ;
    info_ptr->runcount++ ;
    pthread_exit (&info_ptr->status );
    return NULL ;
}
