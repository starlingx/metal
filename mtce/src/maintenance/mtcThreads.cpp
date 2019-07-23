
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
#include "mtcThreads.h"    /* for ... IPMITOOL_THREAD_CMD__RESET ...   */
#include "bmcUtil.h"       /* for ... mtce-common bmc utility header   */


/**************************************************************************
 *
 * Name       : mtcThread_bmc
 *
 * Purpose    : Maintenance thread used to submit a service request to the BMC
 *
 * Description: The thread ...
 *
 *  1. determine protocol to use.
 *  2. create password temp file
 *  3. create output data file name
 *  4. create the tool specific command request
 *  5. launch blocking request
 *  6. parse response against protocol used and pass back to main process
 *
 ************************************************************************/

void * mtcThread_bmc ( void * arg )
{
    thread_info_type       * info_ptr  ;
    thread_extra_info_type * extra_ptr ;

    /* Pointer Error Detection and Handling */
    if ( !arg )
    {
        slog ("thread called with null arg pointer\n");
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

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__DO_NOTHING_THREAD, info_ptr->hostname ))
    {
        info_ptr->progress++ ;
        info_ptr->runcount++ ;
        pthread_exit (&info_ptr->status );
        return NULL ;
    }
#endif

    /* allow the parent to confirm thread id */
    info_ptr->id = pthread_self() ;
    if ( extra_ptr != NULL )
    {
        unsigned int rc = PASS ;
        string command  = "" ;
        string response = "" ;
        string suffix   = "" ;
        string datafile = "" ;

        if ( info_ptr->proto == BMC_PROTOCOL__REDFISHTOOL )
        {
            switch ( info_ptr->command )
            {
                /* state commands */
                case BMC_THREAD_CMD__BMC_QUERY:
                {
                    command = REDFISHTOOL_ROOT_QUERY_CMD ;
                    suffix  = BMC_QUERY_FILE_SUFFIX ;
                    break ;
                }
                case BMC_THREAD_CMD__BMC_INFO:
                {
                    command = REDFISHTOOL_BMC_INFO_CMD ;
                    suffix  = BMC_INFO_FILE_SUFFIX ;
                    break ;
                }

                /* control commands */
                case BMC_THREAD_CMD__POWER_RESET:
                {
                    command  = REDFISHTOOL_POWER_RESET_CMD  ;
                    suffix   = BMC_POWER_CMD_FILE_SUFFIX  ;
                    break ;
                }
                case BMC_THREAD_CMD__POWER_ON:
                {
                    command  = REDFISHTOOL_POWER_ON_CMD    ;
                    suffix   = BMC_POWER_CMD_FILE_SUFFIX ;
                    break ;
                }
                case BMC_THREAD_CMD__POWER_OFF:
                {
                    command  = REDFISHTOOL_POWER_OFF_CMD   ;
                    suffix   = BMC_POWER_CMD_FILE_SUFFIX ;
                    break ;
                }
                case BMC_THREAD_CMD__BOOTDEV_PXE:
                {
                    /* json response */
                    command  = REDFISHTOOL_BOOTDEV_PXE_CMD     ;
                    suffix   = BMC_BOOTDEV_CMD_FILE_SUFFIX ;
                    break ;
                }

                default:
                {
                    rc = info_ptr->status = FAIL_BAD_CASE ;
                    info_ptr->data = "unsupported redfishtool command: " ;
                    info_ptr->data.append(bmcUtil_getCmd_str(info_ptr->command).c_str());
                    break ;
                }
            }/* end redfishtool switch */
        } /* end if */
        else
        {
            switch ( info_ptr->command )
            {
                /* control commands */
                case BMC_THREAD_CMD__POWER_RESET:
                {
                    command  = IPMITOOL_POWER_RESET_CMD  ;
                    response = IPMITOOL_POWER_RESET_RESP ;
                    suffix   = BMC_POWER_CMD_FILE_SUFFIX  ;
                    break ;
                }
                case BMC_THREAD_CMD__POWER_ON:
                {
                    command  = IPMITOOL_POWER_ON_CMD    ;
                    response = IPMITOOL_POWER_ON_RESP   ;
                    suffix   = BMC_POWER_CMD_FILE_SUFFIX ;
                    break ;
                }
                case BMC_THREAD_CMD__POWER_OFF:
                {
                    command  = IPMITOOL_POWER_OFF_CMD   ;
                    response = IPMITOOL_POWER_OFF_RESP  ;
                    suffix   = BMC_POWER_CMD_FILE_SUFFIX ;
                    break ;
                }
                case BMC_THREAD_CMD__POWER_CYCLE:
                {
                    command  = IPMITOOL_POWER_CYCLE_CMD  ;
                    response = IPMITOOL_POWER_CYCLE_RESP ;
                    suffix   = BMC_POWER_CMD_FILE_SUFFIX  ;
                    break ;
                }
                case BMC_THREAD_CMD__BOOTDEV_PXE:
                {
                    command  = IPMITOOL_BOOTDEV_PXE_CMD     ;
                    response = IPMITOOL_BOOTDEV_PXE_RESP    ;
                    suffix   = BMC_BOOTDEV_CMD_FILE_SUFFIX ;
                    break ;
                }

                /* Status commands */
                case BMC_THREAD_CMD__POWER_STATUS:
                {
                    command = IPMITOOL_POWER_STATUS_CMD ;
                    suffix = BMC_POWER_STATUS_FILE_SUFFIX ;
                    break ;
                }
                case BMC_THREAD_CMD__RESTART_CAUSE:
                {
                    command = IPMITOOL_RESTART_CAUSE_CMD ;
                    suffix = BMC_RESTART_CAUSE_FILE_SUFFIX ;
                    break ;
                }
                case BMC_THREAD_CMD__BMC_INFO:
                {
                    command = IPMITOOL_BMC_INFO_CMD ;
                    suffix = BMC_INFO_FILE_SUFFIX ;
                    break ;
                }

                default:
                {
                    rc = info_ptr->status = FAIL_BAD_CASE ;
                    info_ptr->data = "unsupported ipmitool command: " ;
                    info_ptr->data.append(bmcUtil_getCmd_str(info_ptr->command).c_str());
                    break ;
                }
            } /* end ipmitool switch */
        } /* end else */

        if ( rc != PASS )
        {
            if ( info_ptr->status_string.empty() )
            {
                info_ptr->status_string = "failure ; see logs";
            }
            if ( info_ptr->status == PASS )
            {
                info_ptr->status = rc ;
            }
            goto bmc_thread_done ;
        }
        else if ( info_ptr->proto == BMC_PROTOCOL__REDFISHTOOL )
        {
            dlog_t ("%s '%s' command\n", info_ptr->log_prefix, command.c_str());

            /*************** create the password file ***************/
            /* password file contains user name and password in format
             *
             * {"username":"<username>","password":"<password>"}
             *
             * FIXME: Need to settle on username or user.
             *        Support both for now.
             */
            string config_file_content = "{\"username\":\"" ;
            config_file_content.append(extra_ptr->bm_un);
            config_file_content.append("\",\"user\":\"");
            config_file_content.append(extra_ptr->bm_un);
            config_file_content.append("\",\"password\":\"");
            config_file_content.append(extra_ptr->bm_pw);
            config_file_content.append("\"}");
            bmcUtil_create_pw_file ( info_ptr,
                                     config_file_content,
                                     BMC_PROTOCOL__REDFISHTOOL);

            if ( info_ptr->password_file.empty() )
            {
                info_ptr->status_string = "failed to get a temporary password filename" ;
                info_ptr->status = FAIL_FILE_CREATE ;
                goto bmc_thread_done ;
            }

            dlog_t ("%s password file: %s\n", info_ptr->log_prefix, info_ptr->password_file.c_str());

            /* *********** create the output filename ****************/
            datafile = bmcUtil_create_data_fn ( info_ptr->hostname,
                                                suffix,
                                                BMC_PROTOCOL__REDFISHTOOL );

            dlog_t ("%s datafile:%s\n",
                        info_ptr->hostname.c_str(),
                        datafile.c_str());

            /************** Create the redfishtool request **************/
            string request =
                redfishUtil_create_request (command,
                                            extra_ptr->bm_ip,
                                            info_ptr->password_file,
                                            datafile);

            blog1_t ("%s %s", info_ptr->hostname.c_str(), request.c_str());

#ifdef WANT_FIT_TESTING
            bool bypass_request = false ;
            if ( daemon_is_file_present ( MTC_CMD_FIT__DIR ) == true )
            {
                if (( command == REDFISHTOOL_ROOT_QUERY_CMD ) &&
                    ( daemon_is_file_present ( MTC_CMD_FIT__ROOT_QUERY )))
                {
                    bypass_request = true ;
                    rc = PASS ;
                }
                if (( command == REDFISHTOOL_BMC_INFO_CMD ) &&
                    ( daemon_is_file_present ( MTC_CMD_FIT__MC_INFO )))
                {
                    bypass_request = true ;
                    rc = PASS ;
                }
            }
            if ( bypass_request )
                ;
            else
#endif
            {
                daemon_remove_file ( datafile.data() ) ;

                nodeUtil_latency_log ( info_ptr->hostname, NODEUTIL_LATENCY_MON_START, 0 );
                rc = system ( request.data()) ;
                if ( rc != PASS )
                {
                    if ( info_ptr->command != BMC_THREAD_CMD__BMC_QUERY )
                    {
                        elog_t ("%s redfishtool system call failed (%s) (%d:%d:%m)\n",
                                    info_ptr->hostname.c_str(),
                                    request.c_str(),
                                    rc, errno );
                    }
                    info_ptr->status = FAIL_SYSTEM_CALL ;
                    if ( daemon_is_file_present ( datafile.data() ))
                    {
                        /* load in the error. stdio is redirected to the datafile */
                        info_ptr->status_string = daemon_read_file(datafile.data());
                    }
                }
                /* produce latency log if command takes longer than 5 seconds */
                nodeUtil_latency_log ( info_ptr->hostname, "redfishtool system call", 5000 );
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
                info_ptr->status_string = "failed redfishtool command : " ;
                info_ptr->status_string.append(bmcUtil_getCmd_str(info_ptr->command));
                info_ptr->status = FAIL_SYSTEM_CALL ;

#ifdef WANT_PW_FILE_LOG
                if ( request.length () )
                {
                    string _temp = request ;
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
                                  request.c_str());
                    }
                }
#endif
            }
        }

        else if ( info_ptr->proto == BMC_PROTOCOL__IPMITOOL )
        {
            dlog_t ("%s '%s' command\n", info_ptr->log_prefix, command.c_str());

            /*************** create the password file ***************/
            bmcUtil_create_pw_file ( info_ptr,
                                     extra_ptr->bm_pw,
                                     BMC_PROTOCOL__IPMITOOL);

            if ( info_ptr->password_file.empty() )
            {
                info_ptr->status_string = "failed to get a temporary password filename" ;
                info_ptr->status = FAIL_FILE_CREATE ;
                goto bmc_thread_done ;
            }

            dlog_t ("%s password file: %s\n", info_ptr->log_prefix, info_ptr->password_file.c_str());

            /* *********** create the output filename ****************/
            datafile = bmcUtil_create_data_fn ( info_ptr->hostname,
                                                suffix,
                                                BMC_PROTOCOL__IPMITOOL );

            dlog_t ("%s datafile:%s\n",
                        info_ptr->hostname.c_str(),
                        datafile.c_str());

            /************** Create the ipmitool request **************/
            string request = ipmiUtil_create_request ( command,
                                                       extra_ptr->bm_ip,
                                                       extra_ptr->bm_un,
                                                       info_ptr->password_file,
                                                       datafile );

            dlog_t ("%s %s", info_ptr->hostname.c_str(), request.c_str());

            /* assume pass */
            info_ptr->status_string = "pass" ;
            info_ptr->status = rc = PASS ;

#ifdef WANT_FIT_TESTING
            bool bypass_request = false ;
            if ( daemon_is_file_present ( MTC_CMD_FIT__DIR ) == true )
            {
                if (( command == IPMITOOL_BMC_INFO_CMD ) &&
                    ( daemon_is_file_present ( MTC_CMD_FIT__MC_INFO )))
                {
                    bypass_request = true ;
                    rc = PASS ;
                }
                else if (( command == IPMITOOL_POWER_STATUS_CMD ) &&
                         ( daemon_is_file_present ( MTC_CMD_FIT__POWER_STATUS )))
                {
                    bypass_request = true ;
                    rc = PASS ;
                }
                else if (( command == IPMITOOL_RESTART_CAUSE_CMD ) &&
                         ( daemon_is_file_present ( MTC_CMD_FIT__RESTART_CAUSE )))
                {
                    bypass_request = true ;
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
                    bypass_request = true ;
                    rc = PASS ;
                }
                else if ( daemon_want_fit ( FIT_CODE__AVOID_N_FAIL_BMC_REQUEST, info_ptr->hostname ))
                {
                    slog ("%s FIT FIT_CODE__AVOID_N_FAIL_BMC_REQUEST\n", info_ptr->hostname.c_str());
                    bypass_request = true ;
                    rc = FAIL_FIT ;
                }
                else if ( daemon_want_fit ( FIT_CODE__STRESS_THREAD, info_ptr->hostname ))
                {
                    slog ("%s FIT FIT_CODE__STRESS_THREAD\n", info_ptr->hostname.c_str());
                    bypass_request = true ;
                    rc = PASS ;
                }
            }
            if ( bypass_request )
                ;
            else
#endif
            {
                daemon_remove_file ( datafile.data() ) ;

                nodeUtil_latency_log ( info_ptr->hostname, NODEUTIL_LATENCY_MON_START, 0 );
                rc = system ( request.data()) ;
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
                info_ptr->status_string.append(bmcUtil_getCmd_str((bmc_cmd_enum)info_ptr->command));
                info_ptr->status = FAIL_SYSTEM_CALL ;

#ifdef WANT_PW_FILE_LOG
                if ( request.length () )
                {
                    string _temp = request ;
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
                                  request.c_str());
                    }
                }
#endif
            }
        }
        if ( rc == PASS )
        {
            bool datafile_present = false ;

            /* look for the output data file */
            for ( int i = 0 ; i < 10 ; i++ )
            {
                pthread_signal_handler ( info_ptr );
                if ( daemon_is_file_present ( datafile.data() ))
                {
                    datafile_present = true ;
                    break ;
                }
                info_ptr->progress++ ;
                sleep (1);
            }

            if ( datafile_present )
            {
                if ( info_ptr->command == BMC_THREAD_CMD__BMC_INFO )
                {
                    /* tell the main process the name of the file containing the mc info data */
                    info_ptr->data = datafile ;
                }
                else
                {
                    info_ptr->data = daemon_read_file (datafile.data()) ;
                }
            }
            else
            {
                info_ptr->status_string = "command did not produce output file ; timeout" ;
                info_ptr->status = FAIL_FILE_ACCESS ;
            }
        }
        else
        {
            info_ptr->status_string = "system call failed" ;
            info_ptr->status = FAIL_SYSTEM_CALL ;
        }
    }
    else
    {
        info_ptr->status_string = "null 'extra info' pointer" ;
        info_ptr->status = FAIL_NULL_POINTER ;
    }

bmc_thread_done:

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
