/*
 * Copyright (c) 2018 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file Maintenance Heartbeat Utilities Module
 *
 *************************************************************************
 *
 * This module provides heartbeat utilities that are common to both
 * hbsAgent and hbsClient.
 *
 *************************************************************************/

using namespace std;

#include "daemon_common.h" /* common daemon constructs and definitions      */
#include "hbsBase.h"       /* mtce heartbeat constructs and definitions     */

/* hbs_cluster_log utility support. log control array.                      */
bool first_log[MTCE_HBS_MAX_HISTORY_ELEMENTS]; /* has first history log out */
bool was_diff [MTCE_HBS_MAX_HISTORY_ELEMENTS]; /* was there a history diff  */


/****************************************************************************
 *
 * Name        : hbs_utils_init
 *
 * Description : Module Init function
 *
 ***************************************************************************/

void hbs_utils_init ( void )
{
    MEMSET_ZERO ( first_log );
    MEMSET_ZERO ( was_diff  );
}


/****************************************************************************
 *
 * Name        : hbs_cluster_history_init
 *
 * Description : Initialize a cluster history element.
 *
 * Parameters  : Reference to a mtce_hbs_cluster_history_type (history element)
 *
 * Returns     : Nothing
 *
 ***************************************************************************/

void hbs_cluster_history_init ( mtce_hbs_cluster_history_type & history )
{
    MEMSET_ZERO(history);
    history.entries_max = MTCE_HBS_HISTORY_ENTRIES ;
}


/****************************************************************************
 *
 * Name        : hbs_cluster_history_clear
 *
 * Description : Clear all history in the cluster vault.
 *
 * Parameters  : mtce_hbs_cluster_type instance : the vault.
 *
 * Returns     : Nothing
 *
 ***************************************************************************/

void hbs_cluster_history_clear ( mtce_hbs_cluster_type & cluster )
{
    if ( cluster.histories )
    {
        for ( int h = 0 ; h < cluster.histories ; h++ )
            hbs_cluster_history_init ( cluster.history[h] ) ;
    }
}


/****************************************************************************
 *
 * Name        : cluster_network_name
 *
 * Description : converts what is a heartbeat cluster network id to
 *               network name.
 *
 * Parameters  : network id
 *
 * Returns     : network name as a string
 *
 ***************************************************************************/

string hbs_cluster_network_name ( mtce_hbs_network_enum network )
{
    switch ( network )
    {
        case MTCE_HBS_NETWORK_MGMT:
            return ("Mgmnt");
        case MTCE_HBS_NETWORK_INFRA:
            return ("Infra");

#ifdef MONITORED_OAM_NETWORK
        case MTCE_HBS_NETWORK_OAM:
            return ("Oam");
#endif

        default:
            slog ("invalid network enum (%d)", network );
            return ("unknown");
    }
}

/****************************************************************************
 *
 * Name       : hbs_cluster_copy
 *
 * Descrition : Copies cluster from src to dst.
 *
 * Parameters : cluster type.
 *
 * Returns    : Nothing.
 *
 ***************************************************************************/

void hbs_cluster_copy ( mtce_hbs_cluster_type & src, mtce_hbs_cluster_type & dst )
{
    dst.version      = src.version ;
    dst.revision     = src.revision ;
    dst.magic_number = src.magic_number ;
    dst.period_msec  = src.period_msec ;
    dst.histories    = src.histories ;
    dst.storage0_enabled = src.storage0_enabled ;
    for ( int h = 0 ; h < dst.histories ; h++ )
    {
        memcpy( &dst.history[h],
                &src.history[h],
                 sizeof(mtce_hbs_cluster_history_type));
    }
    dst.bytes = BYTES_IN_CLUSTER_VAULT(dst.histories);
}


/****************************************************************************
 *
 * Name        : hbs_cluster_log
 *
 * Description : logs changes to the heartbeat cluster
 *
 * Parameters  : The heartbeat cluster structure
 *
 * Returns     : Nothing
 *
 ***************************************************************************/

void hbs_cluster_log ( string & hostname,
                       mtce_hbs_cluster_type & cluster,
                       string log_prefix,
                       bool force )
{
    for ( int h = 0 ; h < cluster.histories ; h++ )
    {
        if ( cluster.history[h].entries == MTCE_HBS_HISTORY_ENTRIES )
        {
#define MAX_CLUSTER_LINE_LEN 100
#define MAX_ENTRY_STR_LEN     10 /* "9999:9999 " */
            mtce_hbs_cluster_entry_type e = { 0, 0 } ;
            char str[MAX_CLUSTER_LINE_LEN] ;
            string line  = "";
            bool   newline = false ;
            bool   logit   = false ;
            bool   first   = false ;
            string controller = "" ;

            mtce_hbs_cluster_history_type * history_ptr = &cluster.history[h] ;

            /* Manage local this_index for log display.
             * Display oldest to newest ; left to right
             *
             * */
            int this_index = history_ptr->oldest_entry_index ;
            int debug = daemon_get_cfg_ptr()->debug_state ;

            for ( int count = 0 ; count < history_ptr->entries ; count++ )
            {
                if (( line.length() + MAX_ENTRY_STR_LEN ) >=
                        MAX_CLUSTER_LINE_LEN )
                {
                    newline = true ;
                }

#ifdef WANT_MINIMAL_LOGS
                /* TODO: enable in final update */
                if (( first_log[h] == true ) && ( newline == false ) &&
                    ( history_ptr->entry[this_index].hosts_enabled ==
                      history_ptr->entry[this_index].hosts_responding ))
                {
                    line.append(". ");
                    continue ;
                }
#endif

                if ( count == 0 )
                {
                    snprintf (&str[0], MAX_ENTRY_STR_LEN , "%d:%d ", // -%d",
                               history_ptr->entry[this_index].hosts_enabled,
                               history_ptr->entry[this_index].hosts_responding );
                    line.append (str);
                    str[0] = '\0' ;
                }
                else if (( history_ptr->entry[this_index].hosts_enabled ==
                           e.hosts_enabled ) &&
                         ( history_ptr->entry[this_index].hosts_responding ==
                           e.hosts_responding ))
                {
                    line.append(". ");
                }
                else
                {
                    snprintf (&str[0], MAX_ENTRY_STR_LEN , "%d:%d ", // -%d",
                               history_ptr->entry[this_index].hosts_enabled,
                               history_ptr->entry[this_index].hosts_responding );
                    line.append (str);
                    str[0] = '\0' ;
                    logit = true ;
                    was_diff[h] = true ;
                }
                if (( logit == false ) && ( first_log[h] == false ))
                {
                    first_log[h] = true ;
                    logit = true ;
                }
                if ( newline == true )
                {
                    if ( logit )
                    {
                        SET_CONTROLLER_HOSTNAME(history_ptr->controller);
                        if (( force ) || ( debug&2 ))
                        {
                            syslog ( LOG_INFO, "%s view from %s %s %s: %s",
                                     controller.c_str(),
                                     hostname.c_str(),
                                     log_prefix.c_str(),
                                     hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                                     line.c_str());
                        }
                    }
                    line.clear();
                    first = true ;
                    newline = false ;
                }
                e = history_ptr->entry[this_index] ;

                /* manage index tracking */
                if ( this_index == (MTCE_HBS_HISTORY_ENTRIES-1))
                    this_index = 0 ;
                else
                    this_index++ ;
            }
            if (( newline == false ) && ( line.length() ))
            {
                if (( logit == false ) && ( was_diff[h] == true ))
                {
                    logit = true ;
                    was_diff[h] = false ;
                }

                if ( logit )
                {
                    if ( first )
                    {
                        if (( force ) || ( debug&2 ))
                        {
                            syslog ( LOG_INFO, "............ %s %s: %s",
                                   log_prefix.c_str(),
                                   hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                                   line.c_str());
                        }
                    }
                    else
                    {
                        SET_CONTROLLER_HOSTNAME(history_ptr->controller);
                        if (( force ) || ( debug&2 ))
                        {
                            syslog ( LOG_INFO, "%s view from %s %s %s: %s",
                                     controller.c_str(),
                                     hostname.c_str(),
                                     log_prefix.c_str(), /* Infra <- */
                                     hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                                     line.c_str());
                        }
                    }
                }
                else
                {
                    was_diff[h] = false ;
                }
            }
        }
    }
}

/****************************************************************************
 *
 * Name        : hbs_cluster_dump
 *
 * Description : Formatted dump of the specified history to the log file.
 *
 * Parameters  :
 *
 *    history is a single history type whose contents will be logged.
 *    storage0_enabled true suggests the storage state should also be logged.
 *
 ***************************************************************************/

void hbs_cluster_dump ( mtce_hbs_cluster_history_type & history, bool storage0_enabled )
{
    #define MAX_LINE_LEN (500)
    char str[MAX_LINE_LEN] ;
    int i = 0 ;
    for ( int e = 0 ; e < history.entries_max ; e++ )
    {
        snprintf ( &str[i], MAX_LINE_LEN, "%c[%d:%d]" ,
                   history.oldest_entry_index==e ? '>' : ' ',
                   history.entry[e].hosts_enabled,
                   history.entry[e].hosts_responding);
        i = strlen(str) ;
    }
    if ( storage0_enabled )
    {
        syslog ( LOG_INFO, "Cluster Vault : C%d %s S:%s %s",
                 history.controller,
                 hbs_cluster_network_name((mtce_hbs_network_enum)history.network).c_str(),
                 history.storage0_responding ? "y" : "n",
                 str);
    }
    else
    {
        syslog ( LOG_INFO, "Cluster Vault : C%d %s %s",
                 history.controller,
                 hbs_cluster_network_name((mtce_hbs_network_enum)history.network).c_str(),
                 str);
    }
}

/****************************************************************************
 *
 * Name        : hbs_cluster_dump
 *
 * Description : Formatted dump of the vault contents to the log file.
 *
 * Parameters  :
 *
 *    vault is a reference to a cluster type whose contents will be logged.
 *    reason is a string induicatig the reason for the dump.
 *
 ***************************************************************************/

void hbs_cluster_dump ( mtce_hbs_cluster_type & vault, string reason )
{
    if (( vault.version == 0 ) || ( vault.histories == 0 ))
        return ;

    /* The reason is cumulative , if long then use a new line */
    if ( reason.length() > 40 )
    {
        syslog ( LOG_INFO, "Cluster Dump  : %s", reason.c_str());
        reason = "" ;
    }
    syslog ( LOG_INFO, "Cluster Vault : v%d.%d %d msec period %s;%d network histories (%d bytes) %s",
             vault.version,
             vault.revision,
             vault.period_msec,
             vault.storage0_enabled ? " with storage-0: enabled " : "",
             vault.histories,
             vault.bytes,
             reason.c_str());

    for ( int h = 0 ; h < vault.histories ; h++ )
    {
        hbs_cluster_dump ( vault.history[h], vault.storage0_enabled );
    }
}


