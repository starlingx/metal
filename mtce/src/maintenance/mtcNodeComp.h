#ifndef __INCLUDE_MTCNODECOMP_HH__
#define __INCLUDE_MTCNODECOMP_HH__
/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance Client 'mtcClient' Header
 *
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Compute Config mask */
#define CONFIG_CLIENT_MASK  (CONFIG_AGENT_PORT            |\
                             CONFIG_CLIENT_MTC_MGMNT_PORT |\
                             CONFIG_CLIENT_RMON_PORT)

#define MAX_RUN_SCRIPTS         (20)

typedef enum
{
                   NO_SCRIPTS,
       GOENABLED_MAIN_SCRIPTS,
       GOENABLED_SUBF_SCRIPTS,
         HOSTSERVICES_SCRIPTS,
} script_set_enum ;

typedef struct
{
    int             status ; /* script execution exit status                 */
    pid_t           pid    ; /* the script's PID                             */
    bool            done   ; /* set to true when a script has completed      */
    string          name   ; /* the full path/filename of the script         */

    time_debug_type time_start  ; /* time stamps used to measure the         */
    time_debug_type time_stop   ; /*   execution time of                     */
    time_delta_type time_delta  ; /*   the script                            */
} script_exec_type;
void script_exec_init ( script_exec_type * script_exec_ptr );

typedef struct
{
    unsigned int     posted       ; /* posted for execution command          */
    unsigned int     monitor      ; /* set to the previously posted command
                                     * after this commands' scripts have
                                     * been launched.                        */
    int              scripts      ; /* the number of scripts to run          */
    int              scripts_done ; /* number of scripts that completed      */
    struct mtc_timer timer        ; /* the scripts completion timeout timer  */
    script_exec_type script[MAX_RUN_SCRIPTS]; /* array of script exec status */

} script_ctrl_type ;
void script_ctrl_init ( script_ctrl_type * script_ctrl_ptr );

typedef struct
{
    char             hostname [MAX_HOST_NAME_SIZE+1];
    string           macaddr ;
    string           address ;
    string           address_infra ;
    string           who_i_am ;

    string           nodetype_str ;

    string        mgmnt_iface ;
    string        infra_iface ;

    unsigned int     nodetype ;
    unsigned int     function ;
    unsigned int  subfunction ;

    struct mtc_timer timer ; /* mtcAlive timer */

    bool             infra_iface_provisioned ;

    /* tracks the time the level specific goenabled file was last created */
    time_t           goenabled_main_time ;
    time_t           goenabled_subf_time ;

    /* Go Enable Control execution control struct, timing and completion status */
    script_ctrl_type goenabled ;

    /* Start/Stop Hosts Services execution control timing and completion status */
    script_ctrl_type hostservices ;

    /* The script set that is executing */
    script_set_enum active_script_set ;

    /* The list of posted script set requests */
    list<script_set_enum> posted_script_set;

    /* The system type */
    system_type_enum system_type ;

    /* Where to send events */
    string mtcAgent_ip ;

} ctrl_type ;

ctrl_type * get_ctrl_ptr ( void );

bool is_subfunction_worker ( void );
int run_goenabled_scripts ( mtc_socket_type * sock_ptr , string requestor );
int run_hostservices_scripts ( unsigned int cmd );

#endif
