/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
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
#include "hwmonThreads.h" /* for ... ipmitool_thread                      */
#include "secretUtil.h"


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
            /* Handle host delete in ipmi mode
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
                     *   The ipmitool thread needs to run to learn the sensors
                     *   to begin with as well as continually monitor them
                     */
                    thread_handler ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );

                    pingUtil_acc_monitor ( host_ptr->ping_info );

                    /* Check to see if sensor monitoring for this host is disabled.
                     * If it is ... */
                    if ( host_ptr->monitor == false )
                    {
                        /* ... make sure the thread sits in the
                         *     idle state while disabled */
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
                        continue ;
                    }

                    if (( host_ptr->accessible == false ) && ( host_ptr->ping_info.ok == true ))
                    {
                        ilog ("%s bmc is accessible\n", host_ptr->hostname.c_str());
                        host_ptr->accessible = host_ptr->connected = true ;
                    }
                    else if (( host_ptr->accessible == true ) && ( host_ptr->ping_info.ok == false ))
                    {
                        wlog ("%s bmc access lost\n", host_ptr->hostname.c_str());
                        thread_kill ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );
                        host_ptr->accessible = host_ptr->connected = false ;
                        host_ptr->sensor_query_count = 0 ;
                        host_ptr->bmc_fw_version.clear();
                        host_ptr->ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
                    }

                    if ( host_ptr->ping_info.ok == false )
                    {
                        /* Auto correct key ping information ; should never occur but if it does ... */
                        if (( host_ptr->ping_info.hostname.empty()) ||
                            ( hostUtil_is_valid_ip_addr(host_ptr->ping_info.ip ) == false ))
                        {
                            slog ("%s host ping info missing ; (%d:%d)\n",
                                      host_ptr->hostname.c_str(),
                                      host_ptr->ping_info.hostname.empty(),
                                      host_ptr->ping_info.ip.empty());

                            host_ptr->ping_info.hostname = host_ptr->hostname ;
                            host_ptr->ping_info.ip       = host_ptr->bm_ip    ;
                        }
                        // pingUtil_acc_monitor ( host_ptr->ping_info );
                    }
#ifdef WANT_FIT_TESTING
                    if ( daemon_want_fit ( FIT_CODE__EMPTY_BM_PASSWORD ))
                    {
                        host_ptr->thread_extra_info.bm_pw = "" ;
                    }
#endif
                    if ( host_ptr->thread_extra_info.bm_pw.empty () )
                    {
                        string host_uuid = hostBase.get_uuid(host_ptr->hostname);
                        wlog_throttled ( host_ptr->empty_secret_log_throttle, 20,
                                         "%s bm password is empty ; learning and forcing reconnect\n",
                                         host_ptr->hostname.c_str());
                        barbicanSecret_type * secret = secretUtil_manage_secret( host_ptr->secretEvent,
                                                                                 host_uuid,
                                                                                 host_ptr->secretTimer,
                                                                                 hwmonTimer_handler );
                        if ( secret->stage == MTC_SECRET__GET_PWD_RECV )
                        {
                            host_ptr->ping_info.ok = false ;
                            host_ptr->thread_extra_info.bm_pw = host_ptr->bm_pw = secret->payload ;
                        }
                    }
                    else if ( host_ptr->accessible )
                    {
                        /* typical success path */
                        hwmonHostClass::ipmi_sensor_monitor ( host_ptr );
                    }
                    else if ( !thread_idle( host_ptr->bmc_thread_ctrl ) )
                    {
                         thread_kill ( host_ptr->bmc_thread_ctrl, host_ptr->bmc_thread_info );
                    }
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


