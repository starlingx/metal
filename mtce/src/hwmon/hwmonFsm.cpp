/*
 * Copyright (c) 2013, 2016, 2024 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Hardware Monitor Service
  * Connection and Sensor Monitoring FSMs.
  */

#include "hwmon.h"
#include "hwmonClass.h"
#include "hwmonHttp.h"
#include "hwmonSensor.h"
#include "hwmonThreads.h" /* for ... bmc_thread                      */
#include "secretUtil.h"

#ifdef WANT_FIT_TESTING
#include "tokenUtil.h"
static int token_corrupt_holdoff = 0 ;
#endif

/**************************************************************************
 *
 * Name       : hwmon_fsm
 *
 * Description: Loop over host inventory calling connection monitor
 *              handler followed by sensor read handler.
 *
 *              The connection monitor handler verifies connection
 *              to the host before calling to read sensors from it.
 *
 **************************************************************************/
void hwmonHostClass::hwmon_fsm ( void )
{
    struct hwmonHostClass::hwmon_host * host_ptr ;
    std::list<string>::iterator iter_ptr ;

    if ( config_reload == true )
    {
        for ( iter_ptr  = hostlist.begin() ;
              iter_ptr != hostlist.end() ;
              ++iter_ptr )
        {
            string hostname = iter_ptr->c_str();
            host_ptr = getHost ( hostname );
        }
        config_reload = false ;
    }

    for ( iter_ptr  = hostlist.begin() ;
          iter_ptr != hostlist.end() ;
          ++iter_ptr )
    {
        string hostname = iter_ptr->c_str();
        daemon_signal_hdlr ();
        hwmonHttp_server_look ();
        host_ptr = getHost ( hostname );
        if ( host_ptr )
        {
            /* Handle host delete in bmc mode
             *
             * Note: the bmc may have been deprovisioned already
             *       so the delete needs to be deleted up front.
             */
            if ( host_ptr->host_delete == true )
            {
                 /* need to service the thread handler during the delete operation */
                 thread_handler ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );
                 delete_handler ( host_ptr );

                 if ( this->host_deleted == true )
                 {
                      return ;
                 }

                 /* continue with other hosts */
                 continue ;
            }

            if ( host_ptr->bm_provisioned == true )
            {
                /* Run the add handler, but only until its done */
                if ( host_ptr->addStage != HWMON_ADD__DONE )
                {
                    /* first time run after process restart will load sensor model from database */
                    add_host_handler ( host_ptr );
                }
                else
                {
                    /*
                     *   Monitor and Manage active threads
                     *   The bmc thread needs to run to learn the sensors
                     *   to begin with as well as continually monitor them
                     */
                    thread_handler ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );

                    pingUtil_acc_monitor ( host_ptr->ping_info );

                    /* Check to see if sensor monitoring for this host is
                     * disabled or the bm password has not yet been learned */
                    if (( host_ptr->monitor == false ) || ( host_ptr->bm_pw.empty()))
                    {
                        /* ... make sure the thread sits in the
                         *     idle state while disabled or there
                         *     is no pw learned yet */
                        if ( thread_idle ( host_ptr->bmc_thread_ctrl ) == false )
                        {
                            if ( thread_done ( host_ptr->bmc_thread_ctrl ) == true )
                            {
                                host_ptr->bmc_thread_ctrl.done = true ;
                            }
                            else
                            {
                                thread_kill ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );
                            }
                        }
                        /* Only try and get the password if sensor monitoring
                         * is enabled */
                        if (( host_ptr->monitor ) && ( host_ptr->bm_pw.empty( )))
                        {
                            string host_uuid = hostBase.get_uuid(host_ptr->hostname);

                            barbicanSecret_type * secret =
                                secretUtil_manage_secret( host_ptr->secretEvent,
                                                          host_ptr->hostname,
                                                          host_uuid,
                                                          host_ptr->secretTimer,
                                                          hwmonTimer_handler );

                            if ( secret->stage == MTC_SECRET__GET_PWD_RECV )
                            {
                                /* Free the http connection and base resources */
                                httpUtil_free_conn ( host_ptr->secretEvent );
                                httpUtil_free_base ( host_ptr->secretEvent );

                                if ( secret->payload.empty() )
                                {
                                    wlog ("%s failed to acquire bmc password", hostname.c_str());
                                    secret->stage = MTC_SECRET__GET_PWD_FAIL ;
                                }
                                else
                                {
                                    host_ptr->bm_pw = host_ptr->thread_extra_info.bm_pw = secret->payload ;
                                    ilog ("%s bmc credentials received", hostname.c_str());
                                    /* put the FSM back to the start */
                                    secret->stage = MTC_SECRET__START ;
                                }
                            }
                            else
                            {
                                ilog_throttled (host_ptr->empty_secret_log_throttle, 50,
                                    "%s waiting on bm credentials", host_ptr->hostname.c_str());
                            }
                        }
                        continue ;
                    }

                    else if (( host_ptr->accessible == false ) && ( host_ptr->ping_info.ok == true ) && ( !host_ptr->bm_pw.empty() ))
                    {
                        ilog ("%s bmc is accessible ; using %s\n",
                                  host_ptr->hostname.c_str(),
                                  bmcUtil_getProtocol_str(host_ptr->protocol).c_str());
                        host_ptr->accessible = true ;
                    }
                    else if (( host_ptr->accessible == true ) && ( host_ptr->ping_info.ok == false ))
                    {
                        wlog ("%s bmc access lost, changed or being retried ; using %s\n",
                                  host_ptr->hostname.c_str(),
                                  bmcUtil_getProtocol_str(host_ptr->protocol).c_str());
                        thread_kill ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );
                        host_ptr->accessible = false ;
                        host_ptr->sensor_query_count = 0 ;
                        host_ptr->bmc_fw_version.clear();
                        host_ptr->bm_pw.clear();
                        host_ptr->ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
                    }

#ifdef WANT_FIT_TESTING
                    if ( daemon_want_fit ( FIT_CODE__EMPTY_BM_PASSWORD ))
                    {
                        host_ptr->thread_extra_info.bm_pw = "" ;
                    }
#endif
                    if (( host_ptr->accessible ) && ( !host_ptr->bm_pw.empty()))
                    {
                        /* typical success path */
                        hwmonHostClass::bmc_sensor_monitor ( host_ptr );
                    }
                    else if ( !thread_idle( host_ptr->bmc_thread_ctrl ) )
                    {
                         thread_kill ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );
                    }
#ifdef WANT_FIT_TESTING
                    if ((host_ptr->hostname == CONTROLLER_0 ) &&
                        (host_ptr->bm_provisioned ) &&
                        (daemon_is_file_present (MTC_CMD_FIT__CORRUPT_TOKEN)))
                    {
                        // The value in /var/run/fit/corrupt_token specifies the corruption cadence in seconds
                        if ( token_corrupt_holdoff == 0 )
                        {

                            token_corrupt_holdoff = daemon_get_file_int (MTC_CMD_FIT__CORRUPT_TOKEN) ;
                            slog ("FIT corrupting token and making sysinv request");
                            tokenUtil_fail_token();
                            hwmonHttp_mod_sensor ( host_ptr->hostname,
                                                   host_ptr->event,
                                                   host_ptr->sensor[0].uuid,
                                                   "status",
                                                   host_ptr->sensor[0].status);
                        }
                        else
                        {
                            token_corrupt_holdoff-- ;
                            sleep (1);
                        }
                    }
#endif
                }
                if ( host_ptr->want_degrade_audit )
                {
                    dlog ("%s degrade audit ...\n", host_ptr->hostname.c_str());
                    degrade_state_audit ( host_ptr ) ;
                    host_ptr->want_degrade_audit = false ;
                }
            }
        }
    }
}


