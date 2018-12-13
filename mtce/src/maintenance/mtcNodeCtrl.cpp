/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance Daemon
  */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>          /* for hostent */
#include <iostream>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>          /* for ... close and usleep         */
#include <evhttp.h>          /* for ... HTTP_ status definitions */
#include <linux/rtnetlink.h> /* for ... RTMGRP_LINK              */

using namespace std;

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "mtc"

#include "daemon_common.h" /* */
#include "daemon_ini.h"    /* Init parset header       */
#include "daemon_option.h" /* */

#include "nodeBase.h"      /* Service header */
#include "nodeTimers.h"    /*  */
#include "nodeClass.h"     /* */
#include "nodeUtil.h"      /* */
#include "threadUtil.h"    /* for ... threadUtil_init/fini               */
#include "timeUtil.h"      /* for ... daemon_sample_time_init            */
#include "tokenUtil.h"     /* for ... keystone_config_handler            */
#include "nodeMacro.h"     /* for ... CREATE_REUSABLE_INET_UDP_TX_SOCKET */
#include "nodeEvent.h"     /* for ... inotify utility services           */
#include "mtcNodeFsm.h"    /* */
#include "mtcNodeMsg.h"    /* */
#include "mtcHttpSvr.h"    /* for ... mtcHttpSvr_init/_fini/_look        */
#include "mtcInvApi.h"     /* */
#include "mtcSmgrApi.h"    /* */
#include "nlEvent.h"       /* for ... open_netlink_socket              */

/**************************************************************
 *            Implementation Structure
 **************************************************************
 *
 * Call sequence:
 *
 *    daemon_init
 *       daemon_configure
 *       daemon_signal_init
 *       mtc_hostname_read
 *       mtc_message_init (obsolete ?)
 *       mtc_socket_init
 *
 *    daemon_service_run
 *       forever ( timer_handler )
 *           mtc_fsm_run
 *           mtc_service_inbox
 *
 */

extern void mtcTimer_handler ( int sig, siginfo_t *si, void *uc);
extern int  service_events    ( nodeLinkClass    * obj_ptr,
                                   mtc_socket_type * sock_ptr );
extern bool mtc_get_inventory_in_progress ( void );

int mtc_service_inbox ( nodeLinkClass   *  obj_ptr,
                        mtc_socket_type * sock_ptr,
                        int              interface );

string my_hostname = "" ;

/** Instanciate the NodeLinkClass and pointer to it */
nodeLinkClass       mtcInv ;
nodeLinkClass *     mtcInv_ptr ;
nodeLinkClass * get_mtcInv_ptr ( void )
{
    return (&mtcInv);
}

static event_type mtce_event ;
event_type * get_eventPtr ( void )
{
    return(&mtce_event);
}

int module_init ( void )
{
   mtcInv_ptr = &mtcInv ;
   return (PASS);
}

void daemon_sigchld_hdlr ( void )
{
    ; /* dlog("Received SIGCHLD ... no action\n"); */
}

/**
 * Daemon Configuration Structure - The allocated struct
 * @see mtc.h for daemon_config_type struct format.
 */
static daemon_config_type mtc_config ;
daemon_config_type * daemon_get_cfg_ptr ( void ) { return &mtc_config ; }


/**
 * Heartbeat Daemon Messaging Socket Control Struct - The allocated struct
 * @see bhs.h for mtc_socket_type struct format.
 */
static mtc_socket_type mtc_sock   ;
static mtc_socket_type * sock_ptr ;
mtc_socket_type * get_sockPtr ( void )
{ return ( &mtc_sock ) ; }

msgSock_type * get_mtclogd_sockPtr ( void )
{
    return (&mtc_sock.mtclogd);
}

void daemon_exit ( void )
{
    /* Cancel the uptime timer */
    if (  mtcInv.mtcTimer_uptime.tid )
    {
        mtcTimer_stop ( mtcInv.mtcTimer_uptime );
    }

    daemon_dump_info  ();
    daemon_files_fini ();

    /* Close the watch over the /etc/shadow file */
    set_inotify_close ( mtcInv.inotify_shadow_file_fd,
                        mtcInv.inotify_shadow_file_wd);

    /* Close open sockets */
    if (mtc_sock.mtc_agent_rx_socket)
        delete (mtc_sock.mtc_agent_rx_socket);

    if (mtc_sock.mtc_agent_tx_socket)
        delete (mtc_sock.mtc_agent_tx_socket);

    if (mtc_sock.mtc_client_rx_socket)
        delete(mtc_sock.mtc_client_rx_socket);

    if (mtc_sock.mtc_client_tx_socket)
        delete (mtc_sock.mtc_client_tx_socket);

    if (mtc_sock.mtc_client_infra_rx_socket)
        delete (mtc_sock.mtc_client_infra_rx_socket);

    if (mtc_sock.mtc_client_infra_tx_socket)
        delete (mtc_sock.mtc_client_infra_tx_socket);

    if (mtc_sock.mtc_event_rx_sock)
        delete (mtc_sock.mtc_event_rx_sock);

    if (mtc_sock.mtc_to_hbs_sock)
       delete (mtc_sock.mtc_to_hbs_sock);

    if ( mtc_sock.mtclogd.sock > 0 )
       close (mtc_sock.mtclogd.sock);

    if ( mtc_sock.netlink_sock > 0 )
       close (mtc_sock.netlink_sock);

    if ( mtc_sock.ioctl_sock > 0 )
       close (mtc_sock.ioctl_sock);

    mtcHttpSvr_fini ( mtce_event );

        threadUtil_fini () ;

    exit (0) ;
}


/** Control Config Mask */
#define CONFIG_AGENT_MASK    (CONFIG_AGENT_PORT            |\
                              CONFIG_MTC_TO_HBS_CMD_PORT   |\
                              CONFIG_MTC_TO_HWMON_CMD_PORT |\
                              CONFIG_HBS_TO_MTC_EVENT_PORT |\
                              CONFIG_AGENT_HA_PORT         |\
                              CONFIG_AGENT_KEY_PORT        |\
                              CONFIG_AGENT_TOKEN_REFRESH   |\
                              CONFIG_AGENT_LOC_TIMEOUT     |\
                              CONFIG_AGENT_INV_EVENT_PORT  |\
                              CONFIG_AGENT_API_RETRIES     |\
                              CONFIG_CLIENT_PORT)

static int mtc_nfvi_handler   ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("infrastructure-rest-api", "port"))
    {
        config_ptr->vim_cmd_port = atoi(value);
    }
    else
    {
        return (PASS);
    }
    return (FAIL);
}




/* Startup config read */
static int mtc_config_handler ( void * user,
                          const char * section,
                          const char * name,
                          const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("agent", "ha_port"))
    {
        config_ptr->ha_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_HA_PORT ;
    }

    else if (MATCH("agent", "inv_event_port"))
    {
        config_ptr->inv_event_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_INV_EVENT_PORT ;
    }

    else if (MATCH("agent", "keystone_port"))
    {
        config_ptr->keystone_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_KEY_PORT ;
    }

    else if (MATCH("agent", "mtc_agent_port"))
    {
        config_ptr->mtc_agent_port = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_PORT ;
    }
    else if (MATCH("agent", "mtc_to_hbs_cmd_port"))
    {
        config_ptr->mtc_to_hbs_cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_MTC_TO_HBS_CMD_PORT ;
    }
    else if (MATCH("agent", "mtc_to_guest_cmd_port"))
    {
        config_ptr->mtc_to_guest_cmd_port = atoi(value);
//        config_ptr->mask |= CONFIG_MTC_TO_GUEST_CMD_PORT ;
    }
    else if (MATCH("agent", "hbs_to_mtc_event_port"))
    {
        config_ptr->hbs_to_mtc_event_port = atoi(value);
        config_ptr->mask |= CONFIG_HBS_TO_MTC_EVENT_PORT ;
    }
    else if (MATCH("client", "hwmon_cmd_port"))
    {
        config_ptr->hwmon_cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_MTC_TO_HWMON_CMD_PORT ;
    }
    else if (MATCH("client", "daemon_log_port"))
    {
        config_ptr->daemon_log_port = atoi(value);
    }
    else if (MATCH("client", "mtc_rx_mgmnt_port"))
    {
        config_ptr->cmd_port = atoi(value);
        config_ptr->mask |= CONFIG_CLIENT_PORT ;
    }
    else if (MATCH("agent", "token_refresh_rate"))
    {
        config_ptr->token_refresh_rate = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_TOKEN_REFRESH ;
    }
    else if (MATCH("agent", "api_retries"))
    {
        config_ptr->api_retries = atoi(value);
        config_ptr->mask |= CONFIG_AGENT_API_RETRIES ;
        mtcInv.api_retries = config_ptr->api_retries ;
    }
    else if (MATCH("timeouts", "failsafe_shutdown_delay"))
    {
        config_ptr->failsafe_shutdown_delay = atoi(value);
        ilog ("Shutdown TO : %d secs\n", config_ptr->failsafe_shutdown_delay );
    }
    else if (MATCH("agent", "autorecovery_threshold"))
    {
        config_ptr->autorecovery_threshold = atoi(value);
        ilog ("AR Threshold: %d\n",
               config_ptr->autorecovery_threshold );
    }
    else if (MATCH("agent", "offline_period"))
    {
        mtcInv.offline_period = atoi(value);
        ilog ("OfflineAudit: %d msecs\n", mtcInv.offline_period );
    }
    else if (MATCH("agent", "offline_threshold"))
    {
        mtcInv.offline_threshold = atoi(value);
        ilog ("OfflineThrsh: %d\n", mtcInv.offline_threshold );
    }

    else if (MATCH("agent", "ar_config_threshold"))
        mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__CONFIG] = atoi(value);
    else if (MATCH("agent", "ar_goenable_threshold"))
        mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__GOENABLE] = atoi(value);
    else if (MATCH("agent", "ar_hostservices_threshold"))
        mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__HOST_SERVICES] = atoi(value);
    else if (MATCH("agent", "ar_heartbeat_threshold"))
        mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__HEARTBEAT] = atoi(value);

    else if (MATCH("agent", "ar_config_interval"))
        mtcInv.ar_interval[MTC_AR_DISABLE_CAUSE__CONFIG] = atoi(value);
    else if (MATCH("agent", "ar_goenable_interval"))
        mtcInv.ar_interval[MTC_AR_DISABLE_CAUSE__GOENABLE] = atoi(value);
    else if (MATCH("agent", "ar_hostservices_interval"))
        mtcInv.ar_interval[MTC_AR_DISABLE_CAUSE__HOST_SERVICES] = atoi(value);
    else if (MATCH("agent", "ar_heartbeat_interval"))
        mtcInv.ar_interval[MTC_AR_DISABLE_CAUSE__HEARTBEAT] = atoi(value);

    else
    {
        return (PASS);
    }
    return (FAIL);
}

static int mtc_ini_handler   ( void * user,
                         const char * section,
                         const char * name,
                         const char * value)
{
    UNUSED(user);

    if (MATCH("agent", "heartbeat_failure_action"))
    {
        string cur_action = "" ;
        string new_action = "" ;

        /* prevent memory leak over a reconfig */
        if ( mtc_config.hbs_failure_action )
        {
            cur_action = mtc_config.hbs_failure_action ;
            free(mtc_config.hbs_failure_action);
        }
        new_action = mtc_config.hbs_failure_action = strdup(value);
        mtcInv.hbs_failure_action = get_hbs_failure_action(mtc_config);
        if (( !cur_action.empty() ) && ( cur_action != new_action))
        {
            mtc_alarm_id_enum alarm_id = MTC_LOG_ID__CONFIG_HB_ACTION_FAIL ;
            if ( mtcInv.hbs_failure_action == HBS_FAILURE_ACTION__NONE )
                alarm_id = MTC_LOG_ID__CONFIG_HB_ACTION_NONE ;
            else if ( mtcInv.hbs_failure_action == HBS_FAILURE_ACTION__ALARM )
                alarm_id = MTC_LOG_ID__CONFIG_HB_ACTION_ALARM ;
            else if ( mtcInv.hbs_failure_action == HBS_FAILURE_ACTION__DEGRADE )
                alarm_id = MTC_LOG_ID__CONFIG_HB_ACTION_DEGRADE ;

            /* re-use cur_action to build the action change string from it */
            cur_action.append(" to ");
            cur_action.append(new_action);
            mtcAlarm_log ( mtcInv.my_hostname, alarm_id, cur_action );
        }
        if (( mtcInv.mnfa_active == true ) &&
            (( mtcInv.hbs_failure_action == HBS_FAILURE_ACTION__NONE ) ||
             ( mtcInv.hbs_failure_action == HBS_FAILURE_ACTION__ALARM )))
        {
            mtcInv.mnfa_cancel ();
        }
    }
    else if (MATCH("agent", "mnfa_threshold"))
    {
        int old = mtcInv.mnfa_threshold ;
        mtcInv.mnfa_threshold = atoi(value);
        if (( old != 0 ) && ( old != mtcInv.mnfa_threshold ))
        {
            string cur_threshold = ""   ;
            cur_threshold.append(itos(old));
            cur_threshold.append(" to ");
            cur_threshold.append(itos(mtcInv.mnfa_threshold));
            mtcAlarm_log ( mtcInv.my_hostname, MTC_LOG_ID__CONFIG_MNFA_THRESHOLD, cur_threshold );
        }
        ilog ("MNFA Threshd: %d\n", mtcInv.mnfa_threshold);
    }
    else if (MATCH("timeouts", "mnfa_timeout"))
    {
        int old = mtcInv.mnfa_timeout ;
        mtcInv.mnfa_timeout = atoi(value);
        if ( old != mtcInv.mnfa_timeout )
        {
            string cur_timeout = ""   ;
            cur_timeout.append(itos(old));
            cur_timeout.append(" to ");
            cur_timeout.append(itos(mtcInv.mnfa_timeout));
            mtcAlarm_log ( mtcInv.my_hostname, MTC_LOG_ID__CONFIG_MNFA_TIMEOUT, cur_timeout );
        }
        if ( mtcInv.mnfa_timeout == 0 )
        {
            ilog ("MNFA Timeout: Never\n");
        }
        else
        {
            ilog ("MNFA Timeout: %3d secs\n", mtcInv.mnfa_timeout );
        }

        /* handle a change in mnfa timeout while MNFA is active */
        if (( mtcInv.mnfa_active  == true ) &&
            ( mtcInv.mnfa_timeout != old ))
        {
            mtcTimer_reset ( mtcInv.mtcTimer_mnfa );
            if (( old == 0 ) || mtcInv.mnfa_timeout != 0 )
            {
                wlog ("MNFA Auto-Recovery in %d seconds\n",
                       mtcInv.mnfa_timeout);

                mtcTimer_start ( mtcInv.mtcTimer_mnfa,
                                 mtcTimer_handler,
                                 mtcInv.mnfa_timeout);
            }
            else if ( mtcInv.mnfa_timeout == 0 )
            {
                ilog ("MNFA timer set to no-timeout ; previous %d sec timer cancelled", old );
            }
        }
    }
    return (PASS);
}


/* Read and process mtc.ini file settings into the daemon configuration */
int daemon_configure ( void )
{
    int rc = PASS ;

    timeUtil_sched_init ( );

    /* Read the ini */
    mtc_config.mask = 0 ;
    if (ini_parse(MTCE_CONF_FILE, mtc_config_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(MTCE_INI_FILE, mtc_ini_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_INI_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(MTCE_INI_FILE, keystone_config_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_INI_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(NFVI_PLUGIN_CFG_FILE, mtc_nfvi_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", NFVI_PLUGIN_CFG_FILE );
        return (FAIL_LOAD_INI);
    }

    if (ini_parse(SYSINV_CFG_FILE, sysinv_config_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", SYSINV_CFG_FILE );
        return (FAIL_LOAD_INI);
    }

    /* Loads key Mtce debug values that can override the defaults */
    if (ini_parse(MTCE_CONF_FILE, debug_config_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }

    /* Loads key Mtce timeout values that can override the defaults */
    if (ini_parse(MTCE_CONF_FILE, timeout_config_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_CONF_FILE );
        return (FAIL_LOAD_INI);
    }
    /* Loads key Mtce timeout values that can override the defaults */
    if (ini_parse(MTCE_INI_FILE, timeout_config_handler, &mtc_config) < 0)
    {
        elog ("Can't load '%s'\n", MTCE_INI_FILE );
        return (FAIL_LOAD_INI);
    }

    /* Load the compute enable timeouts */
    if ( mtc_config.compute_mtcalive_timeout )
        mtcInv.compute_mtcalive_timeout = mtc_config.compute_mtcalive_timeout ;
    else
        mtcInv.compute_mtcalive_timeout = DEFAULT_MTCALIVE_TIMEOUT ;

    /* Load the controller enable timeouts */
    if ( mtc_config.controller_mtcalive_timeout )
        mtcInv.controller_mtcalive_timeout = mtc_config.controller_mtcalive_timeout ;
    else
        mtcInv.controller_mtcalive_timeout = DEFAULT_MTCALIVE_TIMEOUT ;

    if ( mtc_config.goenabled_timeout )
        mtcInv.goenabled_timeout = mtc_config.goenabled_timeout ;
    else
        mtcInv.goenabled_timeout = DEFAULT_GOENABLE_TIMEOUT ;

    mtcInv.loc_recovery_timeout  = mtc_config.loc_recovery_timeout ;

    if ( mtc_config.node_reinstall_timeout )
        mtcInv.node_reinstall_timeout = mtc_config.node_reinstall_timeout ;
    else
        mtcInv.node_reinstall_timeout = MTC_REINSTALL_TIMEOUT_DEFAULT ;

    if ( mtc_config.dor_mode_timeout <= 0 )
    {
        slog ("DOR Mode Timeout is invalid (%d), setting to default (%d)\n",
               mtc_config.dor_mode_timeout,
               DEFAULT_DOR_MODE_TIMEOUT);

        mtc_config.dor_mode_timeout = DEFAULT_DOR_MODE_TIMEOUT ;
    }

    if ( mtc_config.swact_timeout )
    {
        if ( mtc_config.swact_timeout < (MTC_SWACT_POLL_TIMER*2))
            mtcInv.swact_timeout = (MTC_SWACT_POLL_TIMER*2);
        else
            mtcInv.swact_timeout = mtc_config.swact_timeout ;
    }

    /* Allow the token refresh rate to be specified in the config file */
    /* but no bigger than every 8 hours - that's all that has been tested */
    mtcInv.token_refresh_rate = mtc_config.token_refresh_rate ;
    if ( mtc_config.token_refresh_rate > MTC_HRS_8 )
    {
        wlog ("Token refresh rate rounded down to 8 hour maximum\n");
        mtcInv.token_refresh_rate = MTC_HRS_8 ;
    }

    mtcInv.uptime_period = mtc_config.uptime_period ;

    if ( mtc_config.online_period < MTC_MIN_ONLINE_PERIOD_SECS )
        mtcInv.online_period = MTC_MIN_ONLINE_PERIOD_SECS ;
    else
        mtcInv.online_period = mtc_config.online_period ;

    if (( mtc_config.sysinv_timeout == 0 ) || ( mtc_config.sysinv_timeout > 127 ))
    {
        mtc_config.sysinv_timeout = HTTP_SYSINV_CRIT_TIMEOUT ;
    }
    mtcInv.sysinv_timeout         = mtc_config.sysinv_timeout ;

    if (( mtc_config.sysinv_noncrit_timeout == 0 ) || ( mtc_config.sysinv_noncrit_timeout > 127 ))
    {
        mtc_config.sysinv_noncrit_timeout = HTTP_SYSINV_NONC_TIMEOUT ;
    }
    mtcInv.sysinv_noncrit_timeout = mtc_config.sysinv_noncrit_timeout ;

    if (( mtc_config.work_queue_timeout == 0 ) || ( mtc_config.work_queue_timeout > 500 ))
    {
        mtc_config.work_queue_timeout = MTC_WORKQUEUE_TIMEOUT ;
    }
    mtcInv.work_queue_timeout = mtc_config.work_queue_timeout ;

    if ( mtcInv.offline_period < MIN_OFFLINE_PERIOD_MSECS )
    {
        ilog ("offline audit too small (%d) ; correcting to %d\n",
               mtcInv.offline_period, MIN_OFFLINE_PERIOD_MSECS );

        mtcInv.offline_period = MIN_OFFLINE_PERIOD_MSECS ;
    }

    if ( mtcInv.offline_threshold == MIN_OFFLINE_THRESHOLD )
    {
        ilog ("offline threshold too small (%d) ; correcting to %d\n",
               mtcInv.offline_threshold, MIN_OFFLINE_THRESHOLD );

        mtcInv.offline_threshold = MIN_OFFLINE_THRESHOLD ;
    }

    /* Load in the In-Service and Out-Of-Service Test Periods */
    mtcInv.insv_test_period = mtc_config.insv_test_period ;
    mtcInv.oos_test_period = mtc_config.oos_test_period ;

    ilog ("TokenRefresh: %3d secs\n" , mtcInv.token_refresh_rate);
    ilog ("API Retries : %3d secs\n" , mtcInv.api_retries);

    /* Verify loaded config against an expected mask
     * as an ini file fault detection method */
    if ( mtc_config.mask != CONFIG_AGENT_MASK )
    {
        elog ("Control configuration failed (%x)\n",
             ((-1 ^ mtc_config.mask) & CONFIG_AGENT_MASK));
        return (FAIL_INI_CONFIG);
    }

    mtc_config.mgmnt_iface = daemon_get_iface_master ( mtc_config.mgmnt_iface );
    ilog("Mgmnt iface : %s\n", mtc_config.mgmnt_iface );

    /* Fetch the infrastructure interface name.
     * calls daemon_get_iface_master inside so the
     * aggrigated name is returned if it exists */
    get_infra_iface (&mtc_config.infra_iface );
    if ( strlen (mtc_config.infra_iface) )
    {
        string infra_ip = "" ;
        rc = get_iface_address ( mtc_config.infra_iface, infra_ip, false );
        if ( rc )
        {
            elog ("failed to get IP address fron infra interface '%s' (rc:%d)\n", mtc_config.infra_iface, rc );
        }
        else
        {
            ilog ("Infra iface : %s\n", mtc_config.infra_iface );
            ilog ("Infra addr  : %s\n", infra_ip.c_str());
        }
        if (!strcmp(mtc_config.infra_iface, mtc_config.mgmnt_iface))
        {
            mtcInv.infra_network_provisioned = false ;
        }
        else
        {
            mtcInv.infra_network_provisioned = true ;
        }
    }

    /* Log the startup settings */
    ilog("Cmd Req Port: %d (tx)\n", mtc_config.cmd_port );
    ilog("Cmd Rsp Port: %d (rx)\n", mtc_config.mtc_agent_port );
    ilog("Events  Port: %d (rx)\n", mtc_config.hbs_to_mtc_event_port );
    ilog("Inv Port    : %d (tx)\n", mtc_config.sysinv_api_port );
    ilog("Inv Address : %s (tx)\n", mtc_config.sysinv_api_bind_ip );
    ilog("Inv Event   : %d (rx)\n", mtc_config.inv_event_port );
    ilog("Keystone Port: %d (rx)\n", mtc_config.keystone_port );
    ilog("Mtce Logger : %d (tx)\n", mtc_config.daemon_log_port );
    ilog("nfv-vim-api : %d (port)\n", mtc_config.vim_cmd_port );
    ilog("hbsAgent    : %d (port)\n", mtc_config.mtc_to_hbs_cmd_port );
    ilog("guestAgent  : %d (port)\n", mtc_config.mtc_to_guest_cmd_port );
    ilog("hwmond      : %d (port)\n", mtc_config.hwmon_cmd_port );
    ilog("auth_host   : %s \n", mtc_config.keystone_auth_host );

    /* log system wide service based auto recovery control values */
    ilog("AR Config   : %d (threshold) %d sec (retry interval)",
          mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__CONFIG],
          mtcInv.ar_interval [MTC_AR_DISABLE_CAUSE__CONFIG]);
    ilog("AR GoEnable : %d (threshold) %d sec (retry interval)",
          mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__GOENABLE],
          mtcInv.ar_interval [MTC_AR_DISABLE_CAUSE__GOENABLE]);
    ilog("AR Host Svcs: %d (threshold) %d sec (retry interval)",
          mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__HOST_SERVICES],
          mtcInv.ar_interval [MTC_AR_DISABLE_CAUSE__HOST_SERVICES]);
    ilog("AR Heartbeat: %d (threshold) %d sec (retry interval)",
          mtcInv.ar_threshold[MTC_AR_DISABLE_CAUSE__HEARTBEAT],
          mtcInv.ar_interval [MTC_AR_DISABLE_CAUSE__HEARTBEAT]);

    /* Get this Controller Activity State */
    mtc_config.active = daemon_get_run_option ("active") ;
    ilog ("Controller  : %s\n",
          mtc_config.active ? "Active" : "In-Active" );

    /* remove any existing fit */
    daemon_init_fit ();

    return (PASS);
}

/* Construct the messaging sockets           *
 * 1. unicast transmit (to compute) socket   *
 * 2. unicast receive (fronm compute) socket */
int mtc_socket_init ( void )
{
    int rc = 0 ;
    int socket_size = 0 ;
    char ip_address[INET6_ADDRSTRLEN];

   /***********************************************************/
   /* Setup UDP Maintenance Command Transmit Socket Mgmnt I/F */
   /***********************************************************/

    /* Read the port config strings into the socket struct */
    mtc_sock.mtc_agent_port  = mtc_config.mtc_agent_port;
    mtc_sock.mtc_cmd_port    = mtc_config.cmd_port;

    /* create transmit socket */
    msgClassAddr::getAddressFromInterface(mtc_config.mgmnt_iface, ip_address, INET6_ADDRSTRLEN);
    sock_ptr->mtc_agent_tx_socket = new msgClassTx(ip_address, mtc_config.mtc_agent_port, IPPROTO_UDP, mtc_config.mgmnt_iface);
    rc = sock_ptr->mtc_agent_tx_socket->return_status;
    if(rc != PASS)
    {
        delete sock_ptr->mtc_agent_tx_socket;
        return rc;
    }

   /***********************************************************/
   /* Setup UDP Maintenance Command Transmit Socket Infra I/F */
   /***********************************************************/
    if ( strlen( mtc_config.infra_iface ) )
    {
        /* create infra transmit socket only if the interface is provisioned */
        msgClassAddr::getAddressFromInterface(mtc_config.infra_iface, ip_address, INET6_ADDRSTRLEN);
        sock_ptr->mtc_agent_infra_tx_socket  = new msgClassTx(ip_address, mtc_config.mtc_agent_port, IPPROTO_UDP, mtc_config.infra_iface);
        rc = sock_ptr->mtc_agent_infra_tx_socket->return_status;
        if(rc != PASS)
        {
            delete sock_ptr->mtc_agent_infra_tx_socket;
            return rc;
        }
    }

    /*********************************************************************
     * Setup Maintenance Command Reply and Event Receiver Socket
     *  - management interface
     *
     * This socket is used to receive command replies over the management
     * interface and asynchronous events from the mtcClient and other
     * maintenance service daemons.
     *********************************************************************/
    sock_ptr->mtc_agent_rx_socket =
    new msgClassRx(CONTROLLER, sock_ptr->mtc_agent_port, IPPROTO_UDP );
    if (( sock_ptr->mtc_agent_rx_socket == NULL ) ||
        ( sock_ptr->mtc_agent_rx_socket->return_status ))
    {
        elog("failed to create mtcClient receive socket on port %d for %s\n",
              sock_ptr->mtc_agent_port,
              mtc_config.mgmnt_iface );

        if ( sock_ptr->mtc_agent_rx_socket )
        {
            delete (sock_ptr->mtc_agent_rx_socket);
            sock_ptr->mtc_agent_rx_socket = NULL ;
        }
        return (FAIL_SOCKET_CREATE);
    }

    /* Set messaging buffer size */
    /* if we need a bigger then default we can use a sysctl to raise the max */
    socket_size = MTC_AGENT_RX_BUFF_SIZE ;
    if (( rc = sock_ptr->mtc_agent_rx_socket->setSocketMemory ( mtc_config.mgmnt_iface, "mtce command and event receiver (Mgmnt network)", socket_size )) != PASS )
    {
        elog ("setsockopt failed for SO_RCVBUF (%d:%m)\n", errno );
        delete (sock_ptr->mtc_agent_rx_socket);
        sock_ptr->mtc_agent_rx_socket = NULL ;
        return (FAIL_SOCKET_OPTION);
    }
    socklen_t optlen = sizeof(sock_ptr->mtc_agent_rx_socket_size);
    getsockopt (   sock_ptr->mtc_agent_rx_socket->getFD(), SOL_SOCKET, SO_RCVBUF,
                  &sock_ptr->mtc_agent_rx_socket_size, &optlen );

    ilog ("Listening On: 'mtc client receive' socket %d (%d rx bytes - req:%d) (%s)\n",
              sock_ptr->mtc_agent_port,
              sock_ptr->mtc_agent_rx_socket_size, MTC_AGENT_RX_BUFF_SIZE,
              mtc_config.mgmnt_iface);


    /*********************************************************************
     * Setup Maintenance message receiver on the infrastructure network
     * if it is provisioned
     *
     *********************************************************************/

    if ( mtcInv.infra_network_provisioned == true )
    {
        sock_ptr->mtc_agent_infra_rx_socket =
        new msgClassRx(CONTROLLER_NFS, sock_ptr->mtc_agent_port, IPPROTO_UDP );
        if (( sock_ptr->mtc_agent_infra_rx_socket == NULL ) ||
            ( sock_ptr->mtc_agent_infra_rx_socket->return_status ))
        {
            elog("failed to create mtcClient receive socket on port %d for %s\n",
                  sock_ptr->mtc_agent_port,
                  mtc_config.infra_iface );

            if ( sock_ptr->mtc_agent_infra_rx_socket )
            {
                delete (sock_ptr->mtc_agent_infra_rx_socket);
                sock_ptr->mtc_agent_infra_rx_socket = NULL ;
            }
            return (FAIL_SOCKET_CREATE);
        }

        /* Set messaging buffer size */
        /* if we need a bigger then default we can use a sysctl to raise the max */
        socket_size = MTC_AGENT_RX_BUFF_SIZE ;
        if (( rc = sock_ptr->mtc_agent_infra_rx_socket->setSocketMemory ( mtc_config.infra_iface, "mtce command and event receiver (Infra network)", socket_size )) != PASS )
        {
            elog ("setsockopt failed for SO_RCVBUF (%d:%m)\n", errno );
            delete (sock_ptr->mtc_agent_infra_rx_socket);
            sock_ptr->mtc_agent_infra_rx_socket = NULL ;
            return (FAIL_SOCKET_OPTION);
        }
        socklen_t optlen = sizeof(sock_ptr->mtc_agent_infra_rx_socket_size);
        getsockopt (   sock_ptr->mtc_agent_infra_rx_socket->getFD(), SOL_SOCKET, SO_RCVBUF,
                      &sock_ptr->mtc_agent_infra_rx_socket_size, &optlen );

        ilog ("Listening On: 'mtc client receive' socket %d (%d rx bytes - req:%d) (%s)\n",
                  sock_ptr->mtc_agent_port,
                  sock_ptr->mtc_agent_infra_rx_socket_size, MTC_AGENT_RX_BUFF_SIZE,
                  mtc_config.infra_iface);
    }


   /***********************************************************/
   /* Setup UDP Hardware Monitor Command Transmit socket      */
   /***********************************************************/

    /* Read the port config strings into the socket struct */
    mtc_sock.hwmon_cmd_port    = mtc_config.hwmon_cmd_port;

    /* create transmit socket */
    msgClassAddr::getAddressFromInterface(mtc_config.mgmnt_iface, ip_address, INET6_ADDRSTRLEN);
    sock_ptr->hwmon_cmd_sock = new msgClassTx(ip_address, mtc_config.hwmon_cmd_port, IPPROTO_UDP, mtc_config.mgmnt_iface);
    rc = sock_ptr->hwmon_cmd_sock->return_status;
    if ( rc!=PASS )
    {
        elog("Failed create socket (%d:%s)\n", errno, strerror(errno));
        return (rc);
    }

    /***********************************************************/
    /* Heartbeat Event Receiver Interface - (UDP over 'lo')    */
    /***********************************************************/

    int port = daemon_get_cfg_ptr()->hbs_to_mtc_event_port ;

    /* listen to this port on any interface so that the hbsAgent running
     * locally or on peer controller can get events into mtcAgent */
    mtc_sock.mtc_event_rx_sock =
    new msgClassRx(mtcInv.my_float_ip.data(), port, IPPROTO_UDP);
    rc = mtc_sock.mtc_event_rx_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup mtce event receive port %d\n", port );
        return (rc) ;
    }

    /* Setup the maintenance event receiver for sysinv and vim requests */
    memset ( &mtce_event, 0, sizeof(event_type));
    mtce_event.port = mtc_config.inv_event_port ;
    rc = mtcHttpSvr_init ( mtce_event );

    /***********************************************************/
    /* UDP Transmit Socket for Sending Heartbeat Commands      */
    /***********************************************************/

    port = daemon_get_cfg_ptr()->mtc_to_hbs_cmd_port ;
    sock_ptr->mtc_to_hbs_sock = new msgClassTx(CONTROLLER, port, IPPROTO_UDP, mtc_config.mgmnt_iface);
    rc = sock_ptr->mtc_to_hbs_sock->return_status;
    if ( rc )
    {
        elog ("Failed to setup mtce to hbs transmit command port %d\n", port );
        return (rc) ;
    }

    sock_ptr->mtclogd.port = port = daemon_get_cfg_ptr()->daemon_log_port ;
    CREATE_REUSABLE_INET_UDP_TX_SOCKET ( LOOPBACK_IP,
                                         port,
                                         sock_ptr->mtclogd.sock,
                                         sock_ptr->mtclogd.addr,
                                         sock_ptr->mtclogd.port,
                                         sock_ptr->mtclogd.len,
                                         "mtc logger message",
                                         rc );
    if ( rc )
    {
        elog ("Failed to setup mtce logger port %d\n", port );
        return (rc) ;
    }

    /* Use the base timer to delay for a time to give
     * the heartbeat service time to init */
    // ilog ("Delay 3 secs allowing Inventory & Heartbeat daemons to be ready\n");
    // mtcWait_secs (3);

    return (rc);
}

int mtc_set_availStatus ( string & hostname, mtc_nodeAvailStatus_enum status )
{
    return ( mtcInv.set_availStatus ( hostname, status ));
}

/* Get and store my hostname */
int mtc_hostname_read ( void )
{
    int rc ;

    /* declare and init a var to hold the queried local hostname */
    char local_hostname[MAX_HOST_NAME_SIZE+1] ;
    memset (&local_hostname[0], 0, MAX_HOST_NAME_SIZE);

    /* read the host name */
    rc = gethostname(&local_hostname[0], MAX_HOST_NAME_SIZE );
    if ( rc == PASS )
    {
        string string_hostname = local_hostname ;
        mtcInv.set_my_hostname ( string_hostname );

        if ( mtcInv.get_my_hostname () == string_hostname )
            return (PASS) ;
    }
    else
    {
        dlog ("gethostname failed (%d)\n", rc );
    }
    return (FAIL);
}

/* The main service loop */
int daemon_init ( string iface, string nodetype )
{
    int rc = PASS ;

    /* Not used presently */
    mtcInv.functions = nodetype ;

    httpUtil_init ();

    /* Initialize socket construct and pointer to it */
    memset ( &mtc_sock,   0, sizeof(mtc_sock));
    sock_ptr = &mtc_sock ;

    /* Assign interface to config */
    mtc_config.mgmnt_iface = (char*)iface.data() ;

    if ( daemon_files_init () != PASS )
    {
        elog ("Pid, log or other files could not be opened\n");
        return ( FAIL_FILES_INIT ) ;
    }

    mtcInv.system_type = daemon_system_type ();

    /* Get and store my hostname */
    if ( mtc_hostname_read () != PASS )
    {
        elog ("Failed hostname setup\n");
        return (FAIL_HOSTNAME_SETUP) ;
    }

    /* init the base timers */
    mtcTimer_init ( mtcInv.mtcTimer, mtcInv.my_hostname, "mtc timer" ); /* Init general mtc timer */
    mtcAlarm_init  ();
    mtc_stages_init ();
    threadUtil_init ( mtcTimer_handler ) ;

    /* Bind signal handlers */
    rc = daemon_signal_init () ;
    if ( rc )
    {
        elog ("daemon_signal_init failed\n");
        return ( FAIL_SIGNAL_INIT) ;
    }

    /* Configure the control */
    rc = daemon_configure ();
    if ( rc )
    {
        elog ("Daemon service configuration failed (%i)\n", rc );
        return ( FAIL_DAEMON_CONFIG ) ;
    }

    daemon_make_dir(IPMITOOL_OUTPUT_DIR) ;

#ifdef WANT_FIT_TESTING
    daemon_make_dir(FIT__INFO_FILEPATH);
#endif

    return (rc);
}

int _self_provision ( void )
{
    int rc ;
    int load_retries ;
    bool waiting_msg = false ;
    node_inv_type my_identity ;
    node_inv_type record_info ;

    node_inv_init ( my_identity );
    node_inv_init ( record_info );

    ilog ("My Hostname : %s\n", mtcInv.my_hostname.c_str());

    for ( ;; )
    {
        get_ip_addresses ( mtcInv.my_hostname, mtcInv.my_local_ip , mtcInv.my_float_ip );
        if ( mtcInv.my_float_ip.empty() || mtcInv.my_float_ip.empty() )
        {
            if ( waiting_msg == false )
            {
                ilog ("Waiting on ip address config ...\n");
                waiting_msg = true ;
            }
            mtcWait_secs (3);
        }
        else
        {
            break ;
        }
        daemon_signal_hdlr ();
    }


    my_identity.name  = mtcInv.my_hostname ;
    my_identity.ip    = mtcInv.my_local_ip ;
    get_iface_macaddr ( mtc_config.mgmnt_iface , my_identity.mac );

    /* Verify interface properties */
    if (   my_identity.mac.empty() ||
         ( my_identity.mac.length() != COL_CHARS_IN_MAC_ADDR ) ||
           my_identity.name.empty() ||
           my_identity.ip.empty ())
    {
        elog ("Failed to acquire mgmt interface (%s) properties\n", mtc_config.mgmnt_iface );
        daemon_exit();
    }

    /* Set the states for the database */
    my_identity.type  = "controller";
    my_identity.func  = mtcInv.functions ;
    my_identity.admin = "unlocked"  ;
    my_identity.oper  = "enabled"   ;
    my_identity.avail = "available" ;

    my_identity.avail_subf = "not-installed" ;
    my_identity.oper_subf  = "disabled"  ;

    my_identity.uuid  = "" ; /* uuid will be learned later */

    if ( mtcInv.add_host ( my_identity ) )
    {
        elog ("Failed to add (%s) host\n", my_identity.name.c_str());
        daemon_exit();
    }

    /* Get the initial token.
     * This call does not return until a token is received */
    tokenUtil_get_first ( mtcInv.tokenEvent, mtcInv.my_hostname );

#ifdef WANT_FIT_TESTING
    if ( daemon_want_fit ( FIT_CODE__CORRUPT_TOKEN, mtcInv.my_hostname ))
        tokenUtil_fail_token ();
#endif

    load_retries = 0 ;
    do
    {
        daemon_signal_hdlr ();

        rc = mtcInv.mtcInvApi_load_host ( my_identity.name, record_info ) ;
        if (( rc == PASS ) || ( rc == HTTP_OK ))
        {
            ilog ("%s found in database (%s)\n",
                      record_info.name.c_str(),
                      record_info.uuid.c_str());

            /* load in the uuid, and board management info */
            mtcInv.set_uuid    ( my_identity.name, record_info.uuid );
            mtcInv.set_task    ( my_identity.name, record_info.task );
            mtcInv.set_bm_un   ( my_identity.name, record_info.bm_un );
            mtcInv.set_bm_ip   ( my_identity.name, record_info.bm_ip );
            mtcInv.set_bm_type ( my_identity.name, record_info.bm_type );

            if ( my_identity.name == record_info.name )
            {

                /* If the active controller was 'locked' and is being auto-corrected
                 * to 'unlocked' then ensure that there is no locked alarm set for it */
                if ( record_info.admin != "locked" )
                {
                        mtcAlarm_clear ( my_identity.name, MTC_ALARM_ID__LOCK );
                        /* this is not required because its already inited to clear */
                        // node_ptr->alarms[MTC_ALARM_ID__LOCK] = FM_ALARM_SEVERITY_CLEAR
                }

//                mtcInv.set_subf_info ( my_identity.name, record_info.func,
//                                                         record_info.oper_subf,
//                                                         record_info.avail_subf );
                if ( my_identity.mac != record_info.mac )
                {
                    wlog ("%s mac address mismatch (%s - %s)\n",
                              my_identity.name.c_str(),
                              my_identity.mac.c_str(),
                              record_info.mac.c_str());
                }

                if ( my_identity.ip != record_info.ip )
                {
                    wlog ("%s ip address mismatch (%s - %s)\n",
                              my_identity.name.c_str(),
                              my_identity.ip.c_str(),
                              record_info.ip.c_str());
                }
            }
        }
        else
        {
            if ( rc == HTTP_NOTFOUND )
            {
                wlog ("%s inventory record not found in database, retrying ... \n",
                          my_identity.name.c_str());
            }
            else if ( rc == FAIL_HTTP_ZERO_STATUS )
            {
                    wlog ("%s inventory record load timeout, retrying ... \n",
                              my_identity.name.c_str());
            }
            else if ( rc == FAIL_RETRY )
            {
                wlog ("%s inventory config dependency not met, retrying ...\n",
                          my_identity.name.c_str());
            }
            else
            {
                wlog ("%s inventory record load failed (rc:%d), retrying ...\n",
                          my_identity.name.c_str(), rc );
            }
            load_retries++ ;
            if ( load_retries > (mtcInv.api_retries+10) )
            {
                elog ("... giving up after %d retries\n", load_retries );
               daemon_exit();
            }
            mtcWait_secs (15);
        }
    } while ( rc != PASS ) ;

    mtcInv.set_active_controller_hostname ( my_identity.name );
    mtcInv.set_activity_state (true);
    mtcInv.set_adminAction ( my_identity.name, MTC_ADMIN_ACTION__ADD );
    mtcInv.ctl_mtcAlive_gate ( my_identity.name, true );

    /* Setup the heartbeat service messaging sockets */
    rc = mtc_socket_init  ( ) ;
    if ( rc != PASS )
    {
        elog ("Socket initialization failed (rc:%d)\n", rc );
        return (FAIL_SOCKET_INIT) ;
    }

    daemon_make_dir(IPMITOOL_OUTPUT_DIR) ;

#ifdef WANT_FIT_TESTING
    daemon_make_dir(FIT__INFO_FILEPATH);
#endif

    return(rc);
}

/* Main FSM Loop */
void nodeLinkClass::fsm ( void )
{
    if ( head )
    {
        int rc ;
        daemon_signal_hdlr ();
        this->uptime_handler ();
        for ( struct node * node_ptr = head ; node_ptr != NULL ; node_ptr = node_ptr->next )
        {
            string hn = node_ptr->hostname ;
            rc = fsm ( node_ptr ) ;
            if ( rc )
            {
                dlog ("%s fsm returned error code %d\n", hn.c_str(), rc );
            }
            if ( this->host_deleted == true )
            {
                this->host_deleted = false ;
                return ;
            }

            daemon_signal_hdlr ();
            mtcHttpSvr_look ( mtce_event );
        }
    }
}

void daemon_service_run ( void )
{
    int rc ;

    /* socket descriptor list */
    std::list<int> socks ;

    /* Set the mode */
    mtcInv_ptr->maintenance = true ;
    mtcInv_ptr->heartbeat   = false ;

    if (( mtc_sock.ioctl_sock = open_ioctl_socket ( )) <= 0 )
    {
        elog ("Failed to create ioctl socket");
        daemon_exit ();
    }

    /* Not monitoring address changes RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR */
    if (( mtc_sock.netlink_sock = open_netlink_socket ( RTMGRP_LINK )) <= 0 )
    {
        elog ("Failed to create netlink listener socket");
        daemon_exit ();
    }

    /* Init HTTP Messaging */
    mtcHttpUtil_init ();

    ilog ("SW VERSION  : %s\n", daemon_sw_version ().c_str());

    /* Collect inventory in active state only */
    if ( mtc_config.active == true )
    {
        /* provision this controller */
        if ( _self_provision () != PASS )
        {
            elog ("Failed to self provision active controller\n");
            daemon_exit ();
        }

        /* The following are base object controller timers ; init them */
        mtcTimer_init ( mtcInv.mtcTimer_token, mtcInv.my_hostname, "token timer" );
        mtcTimer_init ( mtcInv.mtcTimer_uptime,mtcInv.my_hostname, "uptime timer" );
        mtcTimer_init ( mtcInv.mtcTimer_mnfa,  mtcInv.my_hostname, "mnfa timer" );
        mtcTimer_init ( mtcInv.mtcTimer_dor,   mtcInv.my_hostname, "DOR mode timer" );

        if ( get_link_state ( mtc_sock.ioctl_sock, mtc_config.mgmnt_iface, &mtcInv.mgmnt_link_up_and_running ) )

        {
            mtcInv.mgmnt_link_up_and_running = false ;
            wlog ("Failed to query %s operational state ; defaulting to down\n", mtc_config.mgmnt_iface );
        }
        else
        {
            ilog ("Mgmnt %s link is %s\n", mtc_config.mgmnt_iface, mtcInv.mgmnt_link_up_and_running ? "Up" : "Down" );
        }

        if ( mtcInv.infra_network_provisioned == true )
        {
            if ( get_link_state ( mtc_sock.ioctl_sock, mtc_config.infra_iface, &mtcInv.infra_link_up_and_running ) )
            {
                mtcInv.infra_link_up_and_running = false ;
                wlog ("Failed to query %s operational state ; defaulting to down\n", mtc_config.infra_iface );
            }
            else
            {
                ilog ("Infra %s link is %s\n", mtc_config.infra_iface, mtcInv.infra_link_up_and_running ? "Up" : "Down" );
            }
        }

        //wlog ("Waiting 15 seconds before talking to inventory ....\n");
        //mtcWait_secs (15);
        //wlog ("Reading Inventory\n");

        /* start loading inventory */
        int retry_count = 0 ;
        do
        {
            /* Load Inventory */
            rc = mtcInvApi_read_inventory ( MTC_INV_BATCH_MAX );
            if ( rc != PASS )
            {
                retry_count++ ;
                elog ("failed to read inventory records for batch of %d\n", MTC_INV_BATCH_MAX );
                elog ("... retrying in 5 seconds\n");
                mtcWait_secs (5);
            }
            else
            {
                retry_count = 0 ;
            }

            if ( retry_count > 10 )
            {
                elog ("failed to read inventory after %d retries\n", retry_count );
                elog ("... giving up ; exiting \n");
                daemon_exit ();
            }
        } while ( rc == FAIL ) ;

        if ( mtcInv_ptr->token_refresh_rate != 0 )
        {
            ilog ("Starting 'Token' Refresh timer (%d minutes)\n",
                   (mtcInv_ptr->token_refresh_rate/60) );
            if ( mtcTimer_start ( mtcInv_ptr->mtcTimer_token,
                                  mtcTimer_handler,
                                  mtcInv_ptr->token_refresh_rate ) != PASS )
            {
                elog ("Failed to start 'Token' Refresh Timer\n");
                daemon_exit ( ) ;
            }
        }

        ilog ("Starting 'Uptime' Refresh timer (%d seconds)\n",
               MTC_UPTIME_REFRESH_TIMER );
        /* Start a inventory refresh timer */
        if ( mtcTimer_start ( mtcInv.mtcTimer_uptime,
                              mtcTimer_handler,
                              MTC_UPTIME_REFRESH_TIMER+(rand()%10)) != PASS )
        {
            elog ("Failed to start 'Uptime' Refresh Timer\n");
            daemon_exit ( ) ;
        }
    }

    /* Add an inotify watch on the shadow file. */
    set_inotify_watch_file ( SHADOW_FILE,
                             mtcInv.inotify_shadow_file_fd ,
                             mtcInv.inotify_shadow_file_wd );

    /* inform the heartbeat service that this controller is active */
    send_hbs_command ( mtcInv.my_hostname, MTC_CMD_ACTIVE_CTRL );

    /* Add this controller to the heartbeat service so that
     * the peer hbsAgent also gets this controllers inventory
     * and this hbsAgent receives the out-of-band heartbeat 'flags' */
    send_hbs_command ( mtcInv.my_hostname, MTC_CMD_ADD_HOST );
    send_hbs_command ( mtcInv.my_hostname, MTC_CMD_START_HOST );

    socks.clear();
    socks.push_front (mtc_sock.mtc_event_rx_sock->getFD());   // service_events
    socks.push_front (mtc_sock.mtc_agent_rx_socket->getFD()); // mtc_service_inbox

    if ( mtcInv.infra_network_provisioned == true )
    {
        socks.push_front (mtc_sock.mtc_agent_infra_rx_socket->getFD()); // mtc_service_inbox
    }

    socks.push_front (mtc_sock.netlink_sock);

    if ( mtce_event.fd )
        socks.push_front( mtce_event.fd ) ;

    /* Avoid selecting on file descriptors that are 0 */
    if ( mtcInv.inotify_shadow_file_fd )
        socks.push_front (mtcInv.inotify_shadow_file_fd);

    socks.sort();

    mtcInv.print_node_info();

    /* enable the base level signal handler latency monitor */
    daemon_latency_monitor (true);

    /* DOR Mode Check */
    int enabled_nodes = mtcInv.enabled_nodes();
    struct timespec ts ;
    clock_gettime (CLOCK_MONOTONIC, &ts );

#ifdef WANT_FIT_TESTING
    /* Support low uptime FIT for testing */
    if ( daemon_is_file_present ( MTC_CMD_FIT__UPTIME ))
    {
        ts.tv_sec = daemon_get_file_int ( MTC_CMD_FIT__UPTIME );
        slog ("FIT: Uptime %ld secs or %ld min %ld secs\n",
               ts.tv_sec,
               ts.tv_sec/60,
               ts.tv_sec%60);
    }
#endif

    if ( ts.tv_sec < MTC_MINS_15 )
    {
        /* CPE DOR window is much greater in CPE since heartbeat
         * cannot start until the inactive CPE has run both manifests */
        int timeout = DEFAULT_DOR_MODE_CPE_TIMEOUT ;

        /* override the timeout to a smaller value for normal system */
        if ( mtcInv.system_type == SYSTEM_TYPE__NORMAL )
        {
            /* calculate time from config variable and number of enabled hosts */
            timeout = mtc_config.dor_mode_timeout + (enabled_nodes);
        }

        mtcInv.dor_mode_active = true ;
        mtcInv.dor_start_time  = ts.tv_sec ;

        ilog ("%-12s ---------- ; DOR Recovery ---------------------- -------------------\n", mtcInv.my_hostname.c_str());
        ilog ("%-12s is ACTIVE  ; DOR Recovery %2d:%02d mins (%4d secs) (duration %3d secs)\n",
                mtcInv.my_hostname.c_str(),
                mtcInv.dor_start_time/60,
                mtcInv.dor_start_time%60,
                mtcInv.dor_start_time,
                timeout );
        ilog ("%-12s ---------- ; DOR Recovery ---------------------- -------------------\n", mtcInv.my_hostname.c_str());
        ilog ("%-12s host state ; DOR Recovery    controller uptime         host uptime    \n", mtcInv.my_hostname.c_str());
        ilog ("%-12s ---------- ; DOR Recovery ---------------------- -------------------\n", mtcInv.my_hostname.c_str());
        mtcTimer_start ( mtcInv.mtcTimer_dor, mtcTimer_handler, timeout );
    }

    /* Run Maintenance service forever */
    for ( ; ; )
    {
        daemon_signal_hdlr ();
        /**
         *  Can't just run 'mtcHttpSvr_look' off select as it is seen to miss events.
         *  Would like to use event_base_loopexit with event_base_loopcontinue
         *  but the continue API is not available until 2.1.2-alpha.
         *  In the meantime we will have to continue to service it all the time
         *  mtcHttpSvr_work ( mtce_event );
         **/
        mtcHttpSvr_look ( mtce_event );
        tokenUtil_manage_token ( mtcInv.tokenEvent, mtcInv.my_hostname, mtcInv.token_refresh_rate, mtcInv.mtcTimer_token, mtcTimer_handler );
        tokenUtil_log_refresh ();

        if ( mtcInv_ptr->num_hosts () == 0 )
        {
            sleep (1);
            continue ;
        }

        mtcInv.fsm ( );

        /* Initialize the master fd_set */
        FD_ZERO(&mtc_sock.readfds);
        FD_SET(mtc_sock.mtc_event_rx_sock->getFD(),        &mtc_sock.readfds);
        FD_SET(mtc_sock.mtc_agent_rx_socket->getFD(),      &mtc_sock.readfds);
        if ( mtcInv.infra_network_provisioned == true )
        {
            FD_SET(mtc_sock.mtc_agent_infra_rx_socket->getFD(),&mtc_sock.readfds);
        }

        if ( mtce_event.fd )
        {
            FD_SET(mtce_event.fd, &mtc_sock.readfds);
        }
        if ( mtcInv.inotify_shadow_file_fd )
        {
            FD_SET(mtcInv.inotify_shadow_file_fd, &mtc_sock.readfds);
        }
        if ( mtc_sock.netlink_sock )
        {
            FD_SET(mtc_sock.netlink_sock, &mtc_sock.readfds);
        }

        /* Initialize the timeval struct  */
        mtc_sock.waitd.tv_sec = 0;
        if ( mtcInv.system_type == SYSTEM_TYPE__NORMAL )
            mtc_sock.waitd.tv_usec = MTCAGENT_SELECT_TIMEOUT ;
        else
            mtc_sock.waitd.tv_usec = MTCAGENT_CPE_SELECT_TIMEOUT ;

        /* This is used as a delay up to select_timeout */
        rc = select( socks.back()+1, &mtc_sock.readfds, NULL, NULL, &mtc_sock.waitd);

        /* If the select time out expired then  */
        if (( rc < 0 ) || ( rc == 0 ))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                elog ( "Select Failed (rc:%d) %s \n", errno, strerror(errno));
            }
        }
        else
        {
            if ( FD_ISSET( mtce_event.fd , &mtc_sock.readfds))
            {
                mtcHttpSvr_look ( mtce_event );
            }
            if (FD_ISSET(mtc_sock.netlink_sock, &mtc_sock.readfds))
            {
                dlog ("netlink socket fired\n");
                if ( mtcInv.service_netlink_events ( mtc_sock.netlink_sock, mtc_sock.ioctl_sock ) != PASS )
                {
                    elog ("service_netlink_events failed (rc:%d)\n", rc );
                }
            }

            if (FD_ISSET(sock_ptr->mtc_event_rx_sock->getFD(), &mtc_sock.readfds))
            {
                if ( (rc = service_events ( &mtcInv, &mtc_sock )) != PASS )
                {
                    elog ("service_events failed (rc:%d)\n", rc );
                }
            }

            if ( FD_ISSET(sock_ptr->mtc_agent_rx_socket->getFD(), &mtc_sock.readfds))
            {
                int cnt = 0 ;
                /* Service up to MAX_RX_MSG_BATCH of messages at once */
                for ( ; cnt < MAX_RX_MSG_BATCH ; cnt++ )
                {
                    rc =  mtc_service_inbox ( &mtcInv, &mtc_sock , MGMNT_INTERFACE) ;
                    if ( rc > RETRY )
                    {
                        mlog2 ("mtc_service_inbox failed (rc:%d) (Mgmnt)\n", rc );
                        break ;
                    }
                    if ( rc == RETRY )
                        break ;
                }
                if ( cnt > 1 )
                {
                   mlog2 ("serviced %d messages in one batch (Mgmnt)\n", cnt );
                }
            }

            if (( mtcInv.infra_network_provisioned == true ) &&
                ( sock_ptr->mtc_agent_infra_rx_socket != NULL ) &&
                ( FD_ISSET(sock_ptr->mtc_agent_infra_rx_socket->getFD(), &mtc_sock.readfds)))
            {
                int cnt = 0 ;
                /* Service up to MAX_RX_MSG_BATCH of messages at once */
                for ( ; cnt < MAX_RX_MSG_BATCH ; cnt++ )
                {
                    rc =  mtc_service_inbox ( &mtcInv, &mtc_sock, INFRA_INTERFACE ) ;
                    if ( rc > RETRY )
                    {
                        mlog2 ("mtc_service_inbox failed (rc:%d) (Infra)\n", rc );
                        break ;
                    }
                    if ( rc == RETRY )
                        break ;
                }
                if ( cnt > 1 )
                {
                   mlog2 ("serviced %d messages in one batch (Infra)\n", cnt ); // ERIC dlog
                }
            }
            if (FD_ISSET(mtcInv.inotify_shadow_file_fd, &mtc_sock.readfds))
            {
                rc = get_inotify_events ( mtcInv.inotify_shadow_file_fd, (IN_MODIFY | IN_CREATE | IN_IGNORED) );
                if ( rc )
                {
                    ilog ("Shadow file has changed (%x)\n", rc );
                    if ( mtcInv.manage_shadow_change ( mtcInv.my_hostname ) != PASS )
                    {
                        elog ("failed to manage shadow file change notification (%d)\n", rc );
                    }
                    if ( rc & IN_IGNORED )
                    {
                        socks.remove(mtcInv.inotify_shadow_file_fd);
                        set_inotify_close ( mtcInv.inotify_shadow_file_fd, mtcInv.inotify_shadow_file_wd );
                        set_inotify_watch_file ( SHADOW_FILE,
                             mtcInv.inotify_shadow_file_fd ,
                             mtcInv.inotify_shadow_file_wd );
                        socks.push_back (mtcInv.inotify_shadow_file_fd);
                        socks.sort();
                        wlog ("Reselecting on %s change (Select:%d)\n", SHADOW_FILE, mtcInv.inotify_shadow_file_fd );
                    }
                }
            }
        }

        daemon_signal_hdlr ();

        /* If the timer is no longer active and we are in DOR mode
         * then exit DOR mode. We do it here instead of  */
        if (( mtcInv.dor_mode_active == true ) && ( mtcInv.mtcTimer_dor.tid == NULL ))
        {
            ilog ("DOR mode disable\n");
            mtcInv.dor_mode_active = false ;
        }
    }
    daemon_exit ();
}

/* Push daemon state to log file */
void daemon_dump_info ( void )
{
    daemon_dump_membuf_banner ();

    mtcTimer_mem_log ();
    mtcInv.print_node_info ();
    daemon_dump_membuf (); /* write mem_logs to log file and clear log list */

    //
    // These calls can lead to a segfault if the lists they are
    // iterating over change as a result of a http reception interrupt.
    //
    // If these calls are to be re-enabled then there needs to be MUTEX.
    //
    // mtcInv.doneQueue_dump_all ();
    // mtcInv.mtcCmd_doneQ_dump_all ();
    // daemon_dump_membuf ();
    // mtcInv.workQueue_dump_all ();
    // mtcInv.mtcCmd_workQ_dump_all ();
    // daemon_dump_membuf ();

    mtcInv.memDumpAllState ();
    daemon_dump_membuf (); /* write mem_logs to log file and clear log list */
}

const char MY_DATA [100] = { "eieio\n" } ;
const char * daemon_stream_info ( void )
{
    return (&MY_DATA[0]);
}
/***************************************************************************
 *                                                                         *
 *                       Module Test Head                                  *
 *                                                                         *
 ***************************************************************************/

extern int mtcJsonInv_testhead ( void );

/** Teat Head Entry */
int daemon_run_testhead ( void )
{
    int rc    = PASS;

    mtc_config.testmode = true ;

    nodeLinkClass * mtcInv_testhead_ptr = new nodeLinkClass ;

    printf  ("\n\n");
    printf  (TESTHEAD_BAR);

    printf  ("| Node Class Test Head - Private and Public Member Functions\n");
    printf  (TESTHEAD_BAR);
    for ( int i = 0 ; i < 11 ; i++ )
    {
 	    if ( mtcInv_testhead_ptr->testhead ( i+1 ) )
        {
            FAILED_STR ;
            rc = FAIL ;
        }
        else
           PASSED ;
    }
    free(mtcInv_testhead_ptr);

    printf  (TESTHEAD_BAR);
    printf  ("| Maintenance Timer Test Head\n");
    printf  (TESTHEAD_BAR);
    return (rc);
}

int send_event ( string & hostname, unsigned int event_cmd, iface_enum iface )
{
    UNUSED(hostname) ;
    UNUSED(event_cmd) ;
    UNUSED(iface);
    return PASS ;
}

