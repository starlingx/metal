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
#include "daemon_common.h" /* Common daemon header     */
#include "nodeBase.h"
#include "nodeTimers.h"

void daemon_config_default ( daemon_config_type* config_ptr )
{
    /* init config struct */
    memset ( config_ptr, 0 , sizeof(daemon_config_type));

    config_ptr->debug_filter          = strdup("none");
    config_ptr->debug_event           = strdup("none");
    config_ptr->mon_process_1         = strdup("none");
    config_ptr->mon_process_2         = strdup("none");
    config_ptr->mon_process_3         = strdup("none");
    config_ptr->mon_process_4         = strdup("none");
    config_ptr->mon_process_5         = strdup("none");
    config_ptr->mon_process_6         = strdup("none");
    config_ptr->mon_process_7         = strdup("none");
    config_ptr->uri_path              = strdup("");
    config_ptr->keystone_prefix_path  = strdup("");
    config_ptr->keystone_identity_uri = strdup("");
    config_ptr->keystone_auth_uri     = strdup("");
    config_ptr->keystone_auth_host    = strdup("");
    config_ptr->keystone_region_name  = strdup("none");
    config_ptr->sysinv_mtc_inv_label  = strdup("none");
    config_ptr->mgmnt_iface           = strdup("none");
    config_ptr->clstr_iface           = strdup("none");
    config_ptr->sysinv_api_bind_ip    = strdup("none");
    config_ptr->mode                  = strdup("none");
    config_ptr->fit_host              = strdup("none");
    config_ptr->multicast             = strdup("none");
    config_ptr->barbican_api_host     = strdup("none");

    config_ptr->debug_all    = 0 ;
    config_ptr->debug_json   = 0 ;
    config_ptr->debug_timer  = 0 ;
    config_ptr->debug_fsm    = 0 ;
    config_ptr->debug_http   = 0 ;
    config_ptr->debug_msg    = 0 ;
    config_ptr->debug_work   = 0 ;
    config_ptr->debug_state  = 0 ;
    config_ptr->debug_alive  = 0 ;
    config_ptr->debug_bmgmt  = 0 ;
    config_ptr->debug_level  = 0 ;
}

/* Timeout config read */
int client_timeout_handler (       void * user,
                             const char * section,
                             const char * name,
                             const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("timeouts", "goenabled_timeout"))
    {
        config_ptr->goenabled_timeout = atoi(value);
        ilog ("goEnabled TO: %3d secs\n" , config_ptr->goenabled_timeout);
    }
    else if (MATCH("timeouts", "host_services_timeout"))
    {
        config_ptr->host_services_timeout = atoi(value);
        ilog ("Host Svcs TO: %3d secs\n" , config_ptr->host_services_timeout);
    }
    return (PASS);
}


/* Timeout config read */
int timeout_config_handler (       void * user,
                             const char * section,
                             const char * name,
                             const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("timeouts", "controller_boot_timeout"))
    {
        bool extended = false ;
        config_ptr->controller_mtcalive_timeout = atoi(value);
        /* extend the controller mtcalive timeout when in virtual box.
         * loaded machines boot slower on controllers in this environment
         * and frequently timeout in the first pass */
        if ( daemon_is_file_present ( HOST_IS_VIRTUAL ) == true )
        {
            config_ptr->controller_mtcalive_timeout *= 2 ;
            extended = true ;
        }
        ilog (" mtcAlive TO: %4d secs (controller) %s\n" ,
                config_ptr->controller_mtcalive_timeout,
                extended ? "(doubled for vbox)" : "");
    }
    else if (MATCH("timeouts", "worker_boot_timeout"))
    {
        config_ptr->compute_mtcalive_timeout = atoi(value);
        ilog (" mtcAlive TO: %4d secs (worker)\n"    , config_ptr->compute_mtcalive_timeout);
    }
    else if (MATCH("timeouts", "goenabled_timeout"))
    {
        config_ptr->goenabled_timeout = atoi(value);
        ilog ("goEnabled TO: %3d secs\n" , config_ptr->goenabled_timeout);
    }
    else if (MATCH("timeouts", "host_services_timeout"))
    {
        config_ptr->host_services_timeout = atoi(value);
        ilog ("Host Svcs TO: %3d secs\n" , config_ptr->host_services_timeout);
    }
    else if (MATCH("timeouts", "sysinv_timeout"))
    {
        config_ptr->sysinv_timeout = atoi(value);
        ilog ("Inv crit  TO: %3d secs\n" , config_ptr->sysinv_timeout);
    }
    else if (MATCH("timeouts", "swact_timeout"))
    {
        config_ptr->swact_timeout = atoi(value);
        ilog ("HA Swact TO : %3d secs\n" , config_ptr->swact_timeout);
    }
    else if (MATCH("timeouts", "sysinv_noncrit_timeout"))
    {
        config_ptr->sysinv_noncrit_timeout = atoi(value);
        ilog ("Inv nonc  TO: %3d secs\n" , config_ptr->sysinv_noncrit_timeout);
    }
    else if (MATCH("timeouts", "work_queue_timeout"))
    {
        config_ptr->work_queue_timeout = atoi(value);
        ilog ("WorkQueue TO: %3d secs\n" , config_ptr->work_queue_timeout);
    }
    else if (MATCH("timeouts", "uptime_period"))
    {
        config_ptr->uptime_period = atoi(value);
        ilog ("Uptime Timer: %3d secs\n" , config_ptr->uptime_period);
    }
    else if (MATCH("timeouts", "online_period"))
    {
        config_ptr->online_period = atoi(value);
        ilog ("Online Timer: %3d secs\n" , config_ptr->online_period);
    }
    else if (MATCH("timeouts", "insv_test_period"))
    {
        config_ptr->insv_test_period = atoi(value);
        ilog ("Insvt Period:%4d secs (controller)\n" , config_ptr->insv_test_period);
    }
    else if (MATCH("timeouts", "oos_test_period"))
    {
        config_ptr->oos_test_period = atoi(value);
        ilog (" Oost Period:%4d secs (controller)\n", config_ptr->oos_test_period);
    }
    else if (MATCH("timeouts", "audit_period"))
    {
        config_ptr->audit_period = atoi(value);
        ilog ("Audit Period: %3d secs\n", config_ptr->audit_period );
    }
    else if (MATCH("timeouts", "loc_recovery_timeout"))
    {
        config_ptr->mask |= CONFIG_AGENT_LOC_TIMEOUT ;
        config_ptr->loc_recovery_timeout = atoi(value);
        ilog ("LOC  Timeout: %3d secs\n", config_ptr->loc_recovery_timeout );
    }
    else if (MATCH("timeouts", "node_reinstall_timeout"))
    {
        config_ptr->node_reinstall_timeout = atoi(value);
        if (( config_ptr->node_reinstall_timeout > MTC_REINSTALL_TIMEOUT_MAX ) ||
            ( config_ptr->node_reinstall_timeout < MTC_REINSTALL_TIMEOUT_MIN ))
              config_ptr->node_reinstall_timeout = MTC_REINSTALL_TIMEOUT_DEFAULT ;
        ilog ("Reinstall TO: %3d secs\n", config_ptr->node_reinstall_timeout );
    }
    else if (MATCH("timeouts", "dor_mode_timeout"))
    {
        config_ptr->dor_mode_timeout = atoi(value);
        ilog ("DOR Mode TO : %3d secs\n", config_ptr->dor_mode_timeout );
    }
    else if (MATCH("timeouts", "dor_recovery_timeout_ext"))
    {
        config_ptr->dor_recovery_timeout_ext = atoi(value);
        ilog ("DOR Time Ext: %3d secs\n", config_ptr->dor_recovery_timeout_ext );
    }

    return (PASS);
}

/* ***********************************************************************
 *
 * Name        : get_hbs_failure_action
 *
 * Desctription: Convert already loaded heartbeat failure action config
 *               string into its equivalent enumerated type.
 *               See code comments below for more detail.
 *
 * Assumptions : Both mtcAgent and hbsAgent need this conversion.
 *
 * Returns     : Converted enum value ; error/default is 'fail' action
 *
 * ***********************************************************************/
hbs_failure_action_enum get_hbs_failure_action (
        daemon_config_type & config )
{
    /* push the Heartbeat Failure Action character array into string
     * for easy/safe compare */
    string hbs_failure_action = config.hbs_failure_action ;

    /* default action is 'fail' */
    hbs_failure_action_enum action_enum = HBS_FAILURE_ACTION__FAIL ;

    /* look for 'none' action - hbsAgent only cares about this one
     * so that it knows to clear or not to raise any alarms for heartbeat
     * failures ; or degrades for that matter */
    if ( hbs_failure_action == HBS_FAILURE_ACTION__NONE_STR )
        action_enum = HBS_FAILURE_ACTION__NONE ;

    /* look for degrade action - alarms are still managed in this mode */
    else if ( hbs_failure_action == HBS_FAILURE_ACTION__DEGRADE_STR )
        action_enum = HBS_FAILURE_ACTION__DEGRADE ;

    /* look for 'alarm' action - no host degrade in this case */
    else if ( hbs_failure_action == HBS_FAILURE_ACTION__ALARM_STR )
        action_enum = HBS_FAILURE_ACTION__ALARM ;

    ilog("HBS Action  : %s\n", config.hbs_failure_action );
    return (action_enum);
}


/* System Inventory Config Reader */
int sysinv_config_handler (       void * user,
                            const char * section,
                            const char * name,
                            const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("DEFAULT", "MTC_INV_LABEL")) // MTC_INV_LABEL=/v1/hosts/
    {
        config_ptr->sysinv_mtc_inv_label = strdup(value);
        ilog("Sysinv Label: %s\n", config_ptr->sysinv_mtc_inv_label );
    }
    else if (MATCH("DEFAULT", "sysinv_api_port")) // sysinv_api_port=6385
    {
        config_ptr->sysinv_api_port = atoi(value);
        ilog("Sysinv Port : %d\n", config_ptr->sysinv_api_port );
    }
    else if (MATCH("DEFAULT", "sysinv_api_bind_ip")) // sysinv_api_bind_ip=192.168.204.2
    {
        config_ptr->sysinv_api_bind_ip = strdup(value);
        ilog("Sysinv IP   : %s\n", config_ptr->sysinv_api_bind_ip );
    }
    return (PASS);
}

/* Openstack Barbican Config Reader */
int barbican_config_handler (     void * user,
                            const char * section,
                            const char * name,
                            const char * value)
{
    daemon_config_type* config_ptr = (daemon_config_type*)user;

    if (MATCH("DEFAULT", "bind_port")) // bind_port=9311
    {
        config_ptr->barbican_api_port = atoi(value);
        ilog("Barbican Port : %d\n", config_ptr->barbican_api_port );
    }
    else if (MATCH("DEFAULT", "bind_host")) // bind_host=192.168.204.2
    {
        config_ptr->barbican_api_host = strdup(value);
        ilog("Barbican Host : %s\n", config_ptr->barbican_api_host );
    }
    return (PASS);
}

#define EMPTY "----"

void daemon_dump_cfg ( void )
{
    daemon_config_type * ptr = daemon_get_cfg_ptr();

    ilog ("Configuration Settings ...\n");
    if ( ptr->scheduling_priority ) { ilog ("scheduling_priority   = %d\n", ptr->scheduling_priority   ); }

    if ( ptr->clstr_degrade_only )    { ilog ("clstr_degrade_only    = %s\n", ptr->clstr_degrade_only ? "Yes" : "No" );}
    if ( ptr->need_clstr_poll_audit ) { ilog ("need_clstr_poll_audit = %s\n", ptr->need_clstr_poll_audit ? "Yes" : "No" );}
    if ( ptr->active )                { ilog ("active                = %s\n", ptr->active ? "Yes" : "No"  );}

    /* hbsAgent */
    if ( ptr->token_refresh_rate    ) { ilog ("token_refresh_rate    = %d\n", ptr->token_refresh_rate    );}
    if ( ptr->hbs_minor_threshold   ) { ilog ("hbs_minor_threshold   = %d\n", ptr->hbs_minor_threshold   );}
    if ( ptr->hbs_degrade_threshold ) { ilog ("hbs_degrade_threshold = %d\n", ptr->hbs_degrade_threshold );}
    if ( ptr->hbs_failure_threshold ) { ilog ("hbs_failure_threshold = %d\n", ptr->hbs_failure_threshold );}
    
    if ( strcmp(ptr->mgmnt_iface, "none" )) { ilog ("mgmnt_iface           = %s\n", ptr->mgmnt_iface    ); }
    if ( strcmp(ptr->clstr_iface, "none" )) { ilog ("clstr_iface           = %s\n", ptr->clstr_iface    );}
    if ( strcmp(ptr->multicast, "none"   )) { ilog ("multicast             = %s\n", ptr->multicast );}

    if ( ptr->ha_port        ) { ilog ("ha_port               = %d\n", ptr->ha_port               );}
    if ( ptr->vim_cmd_port   ) { ilog ("vim_cmd_port          = %d\n", ptr->vim_cmd_port          );}
    if ( ptr->vim_event_port ) { ilog ("vim_event_port        = %d\n", ptr->vim_event_port        );}
    if ( ptr->mtc_agent_port ) { ilog ("mtc_agent_port        = %d\n", ptr->mtc_agent_port        );}
    if ( ptr->mtc_client_port) { ilog ("mtc_client_port       = %d\n", ptr->mtc_client_port       );}
    if ( ptr->keystone_port  ) { ilog ("keystone_port         = %d\n", ptr->keystone_port         );}

    /* mtcAgent & hwmond */
    if ( ptr->sysinv_api_port       ) { ilog ("sysinv_api_port       = %d\n", ptr->sysinv_api_port       );}
    if ( ptr->uri_path  )             { ilog ("uri_path              = %s\n", ptr->uri_path              );}
    if ( ptr->keystone_prefix_path  ) { ilog ("keystone_prefix_path  = %s\n", ptr->keystone_prefix_path  );}
    if ( ptr->keystone_auth_host    ) { ilog ("keystone_auth_host    = %s\n", ptr->keystone_auth_host    );}
    if ( ptr->keystone_identity_uri ) { ilog ("keystone_identity_uri = %s\n", ptr->keystone_identity_uri );}
    if ( ptr->keystone_auth_uri     ) { ilog ("keystone_auth_uri     = %s\n", ptr->keystone_auth_uri     );}
    if ( ptr->keystone_auth_username ) { ilog ("keystone_auth_username = %s\n", ptr->keystone_auth_username );}
    if ( ptr->keystone_auth_project ) { ilog ("keystone_auth_project = %s\n", ptr->keystone_auth_project );}
    if ( ptr->keystone_user_domain  ) { ilog ("keystone_user_domain  = %s\n", ptr->keystone_user_domain );}
    if ( ptr->keystone_project_domain ) { ilog ("keystone_project_domain  = %s\n", ptr->keystone_project_domain );}
    if ( ptr->keystone_region_name  ) { ilog ("keystone_region_name  = %s\n", ptr->keystone_region_name  );}
    if ( ptr->barbican_api_port     ) { ilog ("barbican_api_port     = %d\n", ptr->barbican_api_port     );}
    if ( ptr->barbican_api_host     ) { ilog ("barbican_api_host     = %s\n", ptr->barbican_api_host     );}

    if ( ptr->mtc_rx_mgmnt_port    ) { ilog ("mtc_rx_mgmnt_port     = %d\n", ptr->mtc_rx_mgmnt_port    );}
    if ( ptr->mtc_rx_clstr_port    ) { ilog ("mtc_rx_clstr_port     = %d\n", ptr->mtc_rx_clstr_port    );}
    if ( ptr->mtc_tx_mgmnt_port    ) { ilog ("mtc_tx_mgmnt_port     = %d\n", ptr->mtc_tx_mgmnt_port    );}
    if ( ptr->mtc_tx_clstr_port    ) { ilog ("mtc_tx_clstr_port     = %d\n", ptr->mtc_tx_clstr_port    );}
    if ( ptr->agent_rx_port        ) { ilog ("agent_rx_port         = %d\n", ptr->agent_rx_port        );}
    if ( ptr->client_rx_port       ) { ilog ("client_rx_port        = %d\n", ptr->client_rx_port       );}
    if ( ptr->mtc_to_hbs_cmd_port  ) { ilog ("mtc_to_hbs_cmd_port   = %d\n", ptr->mtc_to_hbs_cmd_port  );}
    if ( ptr->mtc_to_guest_cmd_port) { ilog ("mtc_to_guest_cmd_port = %d\n", ptr->mtc_to_guest_cmd_port);}
    if ( ptr->hwmon_cmd_port       ) { ilog ("hwmon_cmd_port        = %d\n", ptr->hwmon_cmd_port       );}
    if ( ptr->hbs_to_mtc_event_port) { ilog ("hbs_to_mtc_event_port = %d\n", ptr->hbs_to_mtc_event_port);}
    if ( ptr->inv_event_port       ) { ilog ("inv_event_port        = %d\n", ptr->inv_event_port       );}

    if ( ptr->per_node     ) { ilog ("per_node              = %d\n", ptr->per_node             );}
    if ( ptr->audit_period ) { ilog ("audit_period          = %d\n", ptr->audit_period         );}
    if ( ptr->pm_period    ) { ilog ("pm_period             = %d\n", ptr->pm_period            );}

    if ( ptr->pmon_amon_port     ) { ilog ("pmon_amon_port        = %d\n", ptr->pmon_amon_port       );}
    if ( ptr->pmon_event_port    ) { ilog ("pmon_event_port       = %d\n", ptr->pmon_event_port      );}
    if ( ptr->pmon_pulse_port    ) { ilog ("pmon_pulse_port       = %d\n", ptr->pmon_pulse_port      );}
    if ( ptr->event_port         ) { ilog ("event_port            = %d\n", ptr->event_port           );}
    if ( ptr->cmd_port           ) { ilog ("cmd_port              = %d\n", ptr->cmd_port             );}
    if ( ptr->sensor_port        ) { ilog ("sensor_port           = %d\n", ptr->sensor_port          );}
    if ( ptr->start_delay        ) { ilog ("start_delay           = %d\n", ptr->start_delay          );}
    if ( ptr->api_retries        ) { ilog ("api_retries           = %d\n", ptr->api_retries          );}
    if ( ptr->testmode           ) { ilog ("testmode              = %x\n", ptr->testmode             );}
    if ( ptr->testmask           ) { ilog ("testmask              = %x\n", ptr->testmask             );}
    if ( ptr->mask               ) { ilog ("mask                  = %x\n", ptr->mask                 );}
    ilog ("mode                  = %s\n", ptr->mode ? ptr->mode : EMPTY );

    /* pmond */
    if ( ptr->stall_pmon_thld   ) { ilog ("stall_pmon_thld       = %d\n", ptr->stall_pmon_thld      );}
    if ( ptr->stall_mon_period  ) { ilog ("stall_mon_period      = %d\n", ptr->stall_mon_period     );}
    if ( ptr->stall_poll_period ) { ilog ("stall_poll_period     = %d\n", ptr->stall_poll_period    );}
    if ( ptr->stall_rec_thld    ) { ilog ("stall_rec_thld        = %d\n", ptr->stall_rec_thld       );}

    /* mtcAgent */
    if ( ptr->controller_mtcalive_timeout) { ilog ("controller_mtcalive_to= %d\n", ptr->controller_mtcalive_timeout );}
    if ( ptr->compute_mtcalive_timeout   ) { ilog ("compute_mtcalive_to   = %d\n", ptr->compute_mtcalive_timeout    );}
    if ( ptr->goenabled_timeout          ) { ilog ("goenabled_timeout     = %d\n", ptr->goenabled_timeout           );}
    if ( ptr->swact_timeout              ) { ilog ("swact_timeout         = %d\n", ptr->swact_timeout               );}
    if ( ptr->sysinv_timeout             ) { ilog ("sysinv_timeout        = %d\n", ptr->sysinv_timeout              );}
    if ( ptr->sysinv_noncrit_timeout     ) { ilog ("sysinv_noncrit_timeout= %d\n", ptr->sysinv_noncrit_timeout      );}
    if ( ptr->work_queue_timeout         ) { ilog ("work_queue_timeout    = %d\n", ptr->work_queue_timeout          );}
    if ( ptr->loc_recovery_timeout       ) { ilog ("loc_recovery_timeout  = %d\n", ptr->loc_recovery_timeout        );}
    if ( ptr->node_reinstall_timeout     ) { ilog ("node_reinstall_timeout= %d\n", ptr->node_reinstall_timeout      );}
    if ( ptr->uptime_period              ) { ilog ("uptime_period         = %d\n", ptr->uptime_period               );}
    if ( ptr->online_period              ) { ilog ("online_period         = %d\n", ptr->online_period               );}
    if ( ptr->insv_test_period           ) { ilog ("insv_test_period      = %d\n", ptr->insv_test_period            );}
    if ( ptr->oos_test_period            ) { ilog (" oos_test_period      = %d\n", ptr->oos_test_period             );}

    /* mtcClient & hbsClient */
    if ( ptr->failsafe_shutdown_delay ) { ilog ("failsafe_shutdown_dela= %d\n", ptr->failsafe_shutdown_delay     );}

    if ( ptr->debug_all   ) { ilog ("debug_all             = %d\n", ptr->debug_all    );}
    if ( ptr->debug_json  ) { ilog ("debug_json            = %d\n", ptr->debug_json   );}
    if ( ptr->debug_timer ) { ilog ("debug_timer           = %d\n", ptr->debug_timer  );}
    if ( ptr->debug_fsm   ) { ilog ("debug_fsm             = %d\n", ptr->debug_fsm    );}
    if ( ptr->debug_http  ) { ilog ("debug_http            = %d\n", ptr->debug_http   );}
    if ( ptr->debug_msg   ) { ilog ("debug_msg             = %d\n", ptr->debug_msg    );}
    if ( ptr->debug_work  ) { ilog ("debug_work            = %d\n", ptr->debug_work   );}
    if ( ptr->debug_state ) { ilog ("debug_state           = %d\n", ptr->debug_state  );}
    if ( ptr->debug_alive ) { ilog ("debug_alive           = %d\n", ptr->debug_alive  );}
    if ( ptr->debug_bmgmt ) { ilog ("debug_bmgmt           = %d\n", ptr->debug_bmgmt  );}
    if ( ptr->debug_level ) { ilog ("debug_level           = %d\n", ptr->debug_level  );}
    ilog ("debug_filter          = %s\n", ptr->debug_filter );
    ilog ("debug_event           = %s\n", ptr->debug_event  );

}
