/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Nodal Health Check Agent Stubs
  */


#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>           /* for ... close and usleep   */
#include <sys/stat.h>
#include <linux/rtnetlink.h>  /* for ... RTMGRP_LINK         */

using namespace std;

#include "nodeBase.h"
#include "nodeUtil.h"      /* for ... msgSock_type                       */
#include "nodeMacro.h"     /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET */
#include "daemon_ini.h"    /* Ini Parser Header                          */
#include "daemon_common.h" /* Common definitions and types for daemons   */
#include "daemon_option.h" /* Common options  for daemons                */
#include "nodeClass.h"     /* The main link class                        */
#include "hbsBase.h"       /* Heartbeat Base Header File                 */
#include "mtcAlarm.h"      /* for ... the mtcAlarm stubs                 */

int send_guest_command ( string hostname, int command )
{
    UNUSED(hostname);
    UNUSED(command);
    return(PASS);
}

/* Stub interfaces due to common class definition without inheritance */
int nodeLinkClass::mtcInvApi_update_state  ( string hostname,
                                             string key,
                                             string value )
{
    UNUSED(hostname);
    UNUSED(key);
    UNUSED(value);
    return(PASS);
}

/* Stub interfaces due to common class definition without inheritance */
int nodeLinkClass::mtcInvApi_update_state  ( struct nodeLinkClass::node * node_ptr,
                                             string key,
                                             string value )
{
    UNUSED(node_ptr);
    UNUSED(key);
    UNUSED(value);
    return(PASS);
}

int  nodeLinkClass::mtcInvApi_update_task ( string hostname,
                                            string task )
{
    UNUSED(hostname);
    UNUSED(task);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_task ( struct nodeLinkClass::node * node_ptr,
                                           string task )
{
    UNUSED(node_ptr);
    UNUSED(task);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_states ( string hostname,
                                             string admin,
                                             string oper,
                                             string avail )
{
    UNUSED(hostname);
    UNUSED(admin);
    UNUSED(oper);
    UNUSED(avail);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_states ( struct nodeLinkClass::node * node_ptr,
                                             string admin,
                                             string oper,
                                             string avail )
{
    UNUSED(node_ptr);
    UNUSED(admin);
    UNUSED(oper);
    UNUSED(avail);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_force_states ( string hostname,
                                            string admin,
                                            string oper,
                                            string avail )
{
    UNUSED(hostname);
    UNUSED(admin);
    UNUSED(oper);
    UNUSED(avail);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_force_states ( struct nodeLinkClass::node * node_ptr,
                                            string admin,
                                            string oper,
                                            string avail )
{
    UNUSED(node_ptr);
    UNUSED(admin);
    UNUSED(oper);
    UNUSED(avail);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_uptime  ( string hostname,
                                              unsigned int uptime )
{
    UNUSED(hostname);
    UNUSED(uptime);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_uptime  ( struct nodeLinkClass::node * node_ptr,
                                              unsigned int uptime )
{
    UNUSED(node_ptr);
    UNUSED(uptime);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_load_host ( string & hostname ,
                                         node_inv_type & info )
{
    UNUSED(hostname);
    UNUSED(info);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_value ( string hostname,
                                            string key,
                                            string value )
{
    UNUSED(hostname);
    UNUSED(key);
    UNUSED(value);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_value ( struct nodeLinkClass::node * node_ptr,
                                            string key,
                                            string value )
{
    UNUSED(node_ptr);
    UNUSED(key);
    UNUSED(value);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_cfg_show   ( string hostname )
{
    UNUSED(hostname);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_cfg_modify ( string hostname, bool install )
{
    UNUSED(hostname);
    UNUSED(install);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_states_now ( string hostname, string admin, string oper, string avail, string oper_subf, string avail_subf )
{
    UNUSED(hostname);
    UNUSED(admin);
    UNUSED(oper);
    UNUSED(avail);
    UNUSED(oper_subf);
    UNUSED(avail_subf);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_states_now ( struct nodeLinkClass::node * node_ptr, string admin, string oper, string avail, string oper_subf, string avail_subf)
{
    UNUSED(node_ptr);
    UNUSED(admin);
    UNUSED(oper);
    UNUSED(avail);
    UNUSED(oper_subf);
    UNUSED(avail_subf);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_task_now   ( string hostname, string task )
{
    UNUSED(hostname);
    UNUSED(task);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_task_now   ( struct nodeLinkClass::node * node_ptr, string task )
{
    UNUSED(node_ptr);
    UNUSED(task);
    return(PASS);
}

int nodeLinkClass::mtcSmgrApi_request ( struct nodeLinkClass::node * node_ptr, mtc_cmd_enum operation, int retries )
{
    UNUSED(node_ptr);
    UNUSED(operation);
    UNUSED(retries);
    return(PASS);
}

void mtcTimer_handler ( int sig, siginfo_t *si, void *uc)
{
    UNUSED(sig);
    UNUSED(si);
    UNUSED(uc);
}

int mtcSmgrApi_active_services ( string hostname , bool * yes_no_ptr )
{
    UNUSED(hostname);
    UNUSED(yes_no_ptr);
    return(PASS);
}

int send_hbs_command ( string hostname, int command, string controller )
{
    UNUSED(hostname);
    UNUSED(command);
    UNUSED(controller);
    return(PASS);
}

int send_hwmon_command ( string hostname, int command )
{
    UNUSED(hostname);
    UNUSED(command);
    return(PASS);
}

nodeLinkClass * get_mtcInv_ptr (void )
{
    return(NULL);
}

int daemon_log_message ( const char * hostname,
                         const char * filename,
                         const char * log_str )
{
    UNUSED(hostname);
    UNUSED(filename);
    UNUSED(log_str);
    return(PASS);
}

void nodeLinkClass::mnfa_add_host     ( struct nodeLinkClass::node * node_ptr, iface_enum iface )
{ node_ptr = node_ptr ; iface = iface ; }
void nodeLinkClass::mnfa_recover_host ( struct nodeLinkClass::node * node_ptr )
{ node_ptr = node_ptr ; }
void nodeLinkClass::mnfa_enter ( void )
{ }
void nodeLinkClass::mnfa_exit  ( bool force )
{ force = force ; }

int send_mtc_cmd ( string & hostname, int cmd, int interface, string json_dict)
{
    UNUSED(hostname);
    UNUSED(cmd);
    UNUSED(interface);
    UNUSED(json_dict);
    return PASS ;
}

int nodeLinkClass::mtcInvApi_subf_states ( string hostname,
                                           string oper_subf,
                                           string avail_subf )
{
    UNUSED(hostname);
    UNUSED(oper_subf);
    UNUSED(avail_subf);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_subf_states ( struct nodeLinkClass::node * node_ptr,
                                           string oper_subf,
                                           string avail_subf )
{
    UNUSED(node_ptr);
    UNUSED(oper_subf);
    UNUSED(avail_subf);
    return(PASS);
}

int nodeLinkClass::mtcVimApi_state_change ( struct nodeLinkClass::node * node_ptr,
                                            libEvent_enum operation,
                                            int retries )
{
    UNUSED(node_ptr);
    UNUSED(operation);
    UNUSED(retries);
    return(PASS);
}

int nodeLinkClass::mtcInvApi_update_mtcInfo (struct nodeLinkClass::node * node_ptr)
{
    UNUSED(node_ptr);
    return (PASS);
}

int nodeLinkClass::doneQueue_purge    (  struct nodeLinkClass::node * node_ptr ) { node_ptr = node_ptr ; return (PASS) ; }
int nodeLinkClass::workQueue_purge    (  struct nodeLinkClass::node * node_ptr ) { node_ptr = node_ptr ; return (PASS) ; }
int nodeLinkClass::mtcCmd_doneQ_purge (  struct nodeLinkClass::node * node_ptr ) { node_ptr = node_ptr ; return (PASS) ; }
int nodeLinkClass::mtcCmd_workQ_purge (  struct nodeLinkClass::node * node_ptr ) { node_ptr = node_ptr ; return (PASS) ; }

void nodeLinkClass::workQueue_dump    (  struct nodeLinkClass::node * node_ptr ) { node_ptr = node_ptr ; }

int tokenUtil_parse_uri (const string uri, daemon_config_type* config_ptr)
{
    dlog ("%s\n", uri.c_str()) ;
    UNUSED(config_ptr);
    return(PASS);
}

void * mtcThread_bmc ( void * arg ) { UNUSED(arg); return NULL ; }

string bmcUtil_getProtocol_str ( bmc_protocol_enum protocol )
{
    UNUSED(protocol);
    return("unknown");
}

void bmcUtil_info_init ( bmc_info_type & bmc_info )
{
    UNUSED(bmc_info);
}

void bmcUtil_remove_files ( string hostname, bmc_protocol_enum protocol )
{
    UNUSED(hostname);
    UNUSED(protocol);
}
int nodeLinkClass::bmc_command_send ( struct nodeLinkClass::node * node_ptr, int command )
{
    UNUSED(node_ptr);
    UNUSED(command);
    return(PASS);
}

int  nodeLinkClass::bmc_command_recv ( struct nodeLinkClass::node * node_ptr )
{
    UNUSED(node_ptr);
    return(PASS);
}

void nodeLinkClass::bmc_command_done ( struct nodeLinkClass::node * node_ptr )
{
    UNUSED(node_ptr);
}


void bmcUtil_hwmon_info ( string            hostname,
                          bmc_protocol_enum proto,
                          bool              power_on,
                          string            extra )
{
    UNUSED(hostname);
    UNUSED(proto);
    UNUSED(power_on);
    UNUSED(extra);
}

int  mtcAlarm_clear    ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_warning  ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_minor    ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_major    ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_critical ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }


int  mtcAlarm_critical_log ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_major_log    ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_minor_log    ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_warning_log  ( string hostname, mtc_alarm_id_enum id ) { UNUSED(hostname); id = id ; return (PASS); }
int  mtcAlarm_log          ( string hostname, mtc_alarm_id_enum id, string str )
{ UNUSED(hostname); id = id ; UNUSED(str) ; return (PASS); }

string mtcAlarm_getId_str ( mtc_alarm_id_enum id ) { id = id ; return ("stub"); }
