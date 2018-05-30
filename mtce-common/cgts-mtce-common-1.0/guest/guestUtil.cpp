/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <stdlib.h>
#include <string.h>
#include <sstream>
using namespace std;

#include "guestBase.h"
#include "guestUtil.h"
#include "guestClass.h"
#include "jsonUtil.h"

#define MAX_NUM_LEN 64
string time_in_secs_to_str ( time_t secs )
{
    char int_str[MAX_NUM_LEN] ;
    string temp ;
    memset  ( &int_str[0], 0, MAX_NUM_LEN );
    sprintf ( &int_str[0], "%ld" , secs );
    temp = int_str ;
    return (temp);
}

/*****************************************************************************
 *
 * Name   : guestUtil_inst_init 
 *
 * Purpose: Init the specified instance
 *
 *****************************************************************************/
void guestUtil_inst_init ( instInfo * instance_ptr )
{
    instance_ptr->uuid.clear(); /* Not used in the server */

    instance_ptr->inotify_file_fd = 0 ;
    instance_ptr->inotify_file_wd = 0 ;

    instance_ptr->chan_fd      = 0     ;
    instance_ptr->chan_ok      = false ;
    instance_ptr->connected    = false ; /* Assume we have not connected to this channel */
    instance_ptr->heartbeating = false ;

    instance_ptr->heartbeat.provisioned = false ;
    instance_ptr->heartbeat.reporting   = false ;
    instance_ptr->heartbeat.failures    = 0     ;

    instance_ptr->heartbeat.state.clear() ;

    instance_ptr->hbState = hbs_server_waiting_init ;
    instance_ptr->vnState = hbs_server_waiting_init ;

    instance_ptr->connect_count  = 0 ;
    instance_ptr->connect_retry_count  = 0 ;
    instance_ptr->select_count  = 0 ;
    instance_ptr->message_count = 0 ;
    instance_ptr->health_count  = 0 ;
    instance_ptr->failure_count = 0 ;
    instance_ptr->corrective_action_count = 0 ;

    instance_ptr->unhealthy_failure = false ;

    instance_ptr->heartbeat_interval_ms = HB_DEFAULT_INTERVAL_MS;

    instance_ptr->vote_secs = HB_DEFAULT_VOTE_MS/1000;
    instance_ptr->vote_to_str = time_in_secs_to_str (instance_ptr->vote_secs);
    
    instance_ptr->shutdown_notice_secs = HB_DEFAULT_SHUTDOWN_MS/1000;
    instance_ptr->shutdown_to_str = time_in_secs_to_str (instance_ptr->shutdown_notice_secs);

    instance_ptr->suspend_notice_secs = HB_DEFAULT_SUSPEND_MS/1000;
    instance_ptr->suspend_to_str = time_in_secs_to_str (instance_ptr->suspend_notice_secs);

    instance_ptr->resume_notice_secs = HB_DEFAULT_RESUME_MS/1000;
    instance_ptr->resume_to_str = time_in_secs_to_str (instance_ptr->resume_notice_secs);

    instance_ptr->restart_secs = HB_DEFAULT_RESTART_MS/1000;
    instance_ptr->restart_to_str = time_in_secs_to_str(instance_ptr->restart_secs);

    instance_ptr->notification_type = GUEST_HEARTBEAT_MSG_NOTIFY_IRREVOCABLE ;
    instance_ptr->event_type        = GUEST_HEARTBEAT_MSG_EVENT_RESUME ;

    instance_ptr->corrective_action = GUEST_HEARTBEAT_MSG_ACTION_LOG  ;

    instance_ptr->unhealthy_corrective_action = GUEST_HEARTBEAT_MSG_ACTION_UNKNOWN  ;
}

/*****************************************************************************
 *
 * Name   : guestUtil_print_instance 
 *
 * Purpose: Print a summary of the instances that are currently provisioned
 *
 *****************************************************************************/
void guestUtil_print_instance ( instInfo * instInfo_ptr )
{
    ilog ("%s Heartbeat: Prov-%c Reporting-%c Failures:%d\n",
              instInfo_ptr->uuid.c_str(),
              instInfo_ptr->heartbeat.provisioned ? 'Y':'n' ,
              instInfo_ptr->heartbeat.reporting   ? 'Y':'n',
              instInfo_ptr->heartbeat.failures);
}

/*****************************************************************************
 *
 * Name   : guestUtil_print_instances 
 *
 * Purpose: Print a summary of the instances that are currently provisioned
 *
 *****************************************************************************/
void guestUtil_print_instances ( ctrl_type * ctrl_ptr )
{
    bool found = false ;
    int i = 1 ;

    for ( ctrl_ptr->instance_list_ptr  = ctrl_ptr->instance_list.begin();
          ctrl_ptr->instance_list_ptr != ctrl_ptr->instance_list.end();
          ctrl_ptr->instance_list_ptr++ )
    {
        guestUtil_print_instance ( &(*ctrl_ptr->instance_list_ptr) );
        found = true ;
        i++ ;
    }

    if ( found == false )
    {
        ilog ("no heartbeat channels provisioned\n");
    }
}

string log_prefix ( instInfo * instInfo_ptr )
{
    string prefix = "unknown" ;

    if ( instInfo_ptr )
    {
        if ( instInfo_ptr->name.length() )
        {
            if ( instInfo_ptr->name_log_prefix.empty() )
            {
                instInfo_ptr->name_log_prefix = instInfo_ptr->inst ;
                instInfo_ptr->name_log_prefix.append (" ");
                instInfo_ptr->name_log_prefix.append (instInfo_ptr->name);
            }
            prefix = instInfo_ptr->name_log_prefix ;
        }
        else
        {
            if ( instInfo_ptr->uuid_log_prefix.empty() )
            {
                instInfo_ptr->uuid_log_prefix = instInfo_ptr->uuid ;
            }
            prefix = instInfo_ptr->uuid_log_prefix ;
        }
    }
    return (prefix);
}

string guestUtil_set_inst_info ( string hostname , instInfo * instInfo_ptr )
{
    /* Send one message per instance */
    string payload ("{\"hostname\":\"");
    payload.append (hostname);
    payload.append ("\",\"uuid\":\"");
    payload.append (instInfo_ptr->uuid);
    
    /* Share the reporting state */
    payload.append ("\",\"reporting\":");
    if ( instInfo_ptr->heartbeat.reporting == true )
        payload.append ("\"enabled");
    else
        payload.append ("\"disabled");
    
    /* Share the heartbeating state */
    payload.append ("\",\"heartbeating\":");
    if ( instInfo_ptr->heartbeating == true )
        payload.append ("\"enabled");
    else
        payload.append ("\"disabled");
            
    payload.append ("\",\"repair-action\":\"" );
    if ( instInfo_ptr->unhealthy_failure == true )
    {
        payload.append (instInfo_ptr->unhealthy_corrective_action);
    }
    else
    {
        payload.append (instInfo_ptr->corrective_action);
    }
    /* Add the restart timeout to the message */
    payload.append ("\",\"restart-to\":\"");
    payload.append (instInfo_ptr->restart_to_str);
    payload.append ("\",\"shutdown-to\":\"");
    payload.append (instInfo_ptr->shutdown_to_str);
    payload.append ("\",\"suspend-to\":\"");
    payload.append (instInfo_ptr->suspend_to_str);
    payload.append ("\",\"resume-to\":\"");
    payload.append (instInfo_ptr->resume_to_str);
    payload.append ("\",\"vote-to\":\"");
    payload.append (instInfo_ptr->vote_to_str);
    payload.append ("\"");
    payload.append ("}");

    jlog ("Payload: %s\n", payload.c_str());

    return (payload);
}

int guestUtil_get_inst_info ( string hostname, instInfo * instInfo_ptr, char * buf_ptr )
{
    int rc = PASS ;

    string hostname_str = "" ;
    string uuid         = "" ;
    string state        = "" ;
    string status       = "" ;
    string restart_to   = "" ;
    string resume_to    = "" ;
    string suspend_to   = "" ;
    string shutdown_to  = "" ;
    string vote_to      = "" ;
    string repair_str   = "" ;

    if ( !buf_ptr )
    {
       elog   ( "null buffer\n" );
       return ( FAIL_NULL_POINTER );
    }

    jlog ("Payload: %s\n", buf_ptr );

    int rc0 = jsonUtil_get_key_val ( buf_ptr, "hostname",     hostname_str) ;
    int rc1 = jsonUtil_get_key_val ( buf_ptr, "uuid",         uuid        ) ;
    int rc2 = jsonUtil_get_key_val ( buf_ptr, "reporting",    state       ) ;
    int rc3 = jsonUtil_get_key_val ( buf_ptr, "heartbeating", status      ) ;
    int rc4 = jsonUtil_get_key_val ( buf_ptr, "restart-to",   restart_to  ) ;
    int rc5 = jsonUtil_get_key_val ( buf_ptr, "resume-to",    resume_to   ) ;
    int rc6 = jsonUtil_get_key_val ( buf_ptr, "suspend-to",   suspend_to  ) ;
    int rc7 = jsonUtil_get_key_val ( buf_ptr, "shutdown-to",  shutdown_to ) ;
    int rc8 = jsonUtil_get_key_val ( buf_ptr, "vote-to",      vote_to     ) ;
    int rc9= jsonUtil_get_key_val ( buf_ptr, "repair-action",repair_str  ) ;
    if ( rc0 | rc1 | rc2 | rc3 | rc4 | rc5 | rc6 | rc7 | rc8 | rc9 )
    {
        elog ("%s failed parse one or more key values (%d:%d:%d:%d:%d:%d:%d:%d:%d:%d)\n",
                  hostname.c_str(), rc0, rc1, rc2, rc3, rc4, rc5, rc6, rc7, rc8, rc9);

        rc = FAIL_KEY_VALUE_PARSE ;
    }
    else
    {
        if ( hostname.compare(hostname_str) )
        {
            wlog ("%s hostname mismatch - loaded\n", hostname_str.c_str());
        }

        if ( instInfo_ptr )
        {
            /* Update the reporting state */
            if ( !state.compare("enabled") )
                instInfo_ptr->heartbeat.reporting = true ;
            else
                instInfo_ptr->heartbeat.reporting = false ;

            /* update the heartbeating status */
            if ( !status.compare("enabled") )
                instInfo_ptr->heartbeating = true ;
            else
                instInfo_ptr->heartbeating = false ;

            instInfo_ptr->corrective_action = repair_str ;

            /* Update the intance timeout values */
            instInfo_ptr->restart_to_str  = restart_to  ;
            instInfo_ptr->shutdown_to_str = shutdown_to ;
            instInfo_ptr->resume_to_str   = resume_to   ;
            instInfo_ptr->suspend_to_str  = suspend_to  ;
            instInfo_ptr->vote_to_str     = vote_to     ;
        }
        else
        {
            wlog ("%s %s lookup failed\n", hostname.c_str(), uuid.c_str());
            rc = FAIL_INVALID_UUID ;
        }
    }
    return (rc);
}

const char* state_names[] =
    {
    "invalid",
    "server_waiting_init",
    "server_waiting_challenge",
    "server_waiting_response",
    "server_paused",
    "server_nova_paused",
    "server_migrating",
    "server_corrective_action",
    "client_waiting_init_ack",
    "client_waiting_challenge",
    "client_waiting_pause_ack",
    "client_waiting_resume_ack",
    "client_paused",
    "client_waiting_shutdown_ack",
    "client_waiting_shutdown_response",
    "client_shutdown_response_recieved",
    "client_exiting",
    };

const char* hb_get_state_name ( hb_state_t s )
{
    if (s >= hbs_state_max)
        return "???";

    return state_names[s];
}

/*****************************************************************************
 * Convert integer to string
 *****************************************************************************/
string int_to_string(int number)
{
   ostringstream ostr;
   ostr << number;
   return ostr.str();
}

