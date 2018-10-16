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
                       string log_prefix )
{
    // bool want_log = false ;

    clog1 ("log %d histories", cluster.histories );
    for ( int h = 0 ; h < cluster.histories ; h++ )
    {
        if ( cluster.history[h].entries == MTCE_HBS_HISTORY_ENTRIES )
        {
#define MAX_CLUSTER_LINE_LEN 100
#define MAX_ENTRY_STR_LEN     10 /* "9999:9999 " */
            mtce_hbs_cluster_entry_type e = { 0, 0 } ;
            char str[MAX_CLUSTER_LINE_LEN] ;
            string line  = "";
            int    start = 0 ;
            int    stop  = 0 ;
            bool   newline = false ;
            bool   logit   = false ;
            bool   first   = false ;
            string controller = "" ;

            mtce_hbs_cluster_history_type * history_ptr = &cluster.history[h] ;

            clog1 ("%s %s has %d entries (controller-%d view from %s)", hostname.c_str(),
                    hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                    history_ptr->entries,
                    history_ptr->controller,
                    log_prefix.c_str());


            /* Manage local this_index for log display.
             * Display oldest to newest ; left to right
             *
             * */
            int this_index = history_ptr->oldest_entry_index ;
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

                // want_log = true ;

                if ( count == 0 )
                {
                    snprintf (&str[0], MAX_ENTRY_STR_LEN , "%d:%d ", // -%d",
                               history_ptr->entry[this_index].hosts_enabled,
                               history_ptr->entry[this_index].hosts_responding ); // , this_index );
                    line.append (str);
                    str[0] = '\0' ;
                }
//#ifdef WANT_DOTS
                else if (( history_ptr->entry[this_index].hosts_enabled ==
                           e.hosts_enabled ) &&
                         ( history_ptr->entry[this_index].hosts_responding ==
                           e.hosts_responding ))
                {
                    line.append(". ");
                }
//#endif
                else
                {
                    snprintf (&str[0], MAX_ENTRY_STR_LEN , "%d:%d ", // -%d",
                               history_ptr->entry[this_index].hosts_enabled,
                               history_ptr->entry[this_index].hosts_responding ); // , this_index );
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
                stop++ ;
                if ( newline == true )
                {
                    if ( logit )
                    {
                        SET_CONTROLLER_HOSTNAME(history_ptr->controller);
                        if ( hostname == controller )
                        {
                            clog ("%s view %s %s %02d..%02d: %s,",
                                   hostname.c_str(),
                                   log_prefix.c_str(),
                                   hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                                   start, stop, line.c_str());
                        }
                        else
                        {
                            clog ("%s view from %s %s %s %02d..%02d: %s,",
                                   controller.c_str(),
                                   hostname.c_str(),
                                   log_prefix.c_str(),
                                   hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                                   start, stop, line.c_str());
                        }
                    }
                    start = stop + 1 ;
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
                // ERIC
                if (( logit == false ) && ( was_diff[h] == true ))
                {
                    logit = true ;
                    was_diff[h] = false ;
                }

                if ( logit )
                {
                    if ( first )
                    {
                        clog ("............ %s %s %02d..%02d: %s",
                               log_prefix.c_str(),
                               hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                               start, stop, line.c_str());
                    }
                    else
                    {
                        SET_CONTROLLER_HOSTNAME(history_ptr->controller);
                        if ( hostname == controller )
                        {
                            clog ("%s view %s %s %02d..%02d: %s",
                                   hostname.c_str(),
                                   log_prefix.c_str(),
                                   hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                                   start, stop, line.c_str());
                        }
                        else
                        {
                            clog ("%s view from %s %s %s %02d..%02d: %s",
                                   controller.c_str(),
                                   hostname.c_str(),
                                   log_prefix.c_str(), /* Infra <- */
                                   hbs_cluster_network_name((mtce_hbs_network_enum)history_ptr->network).c_str(),
                                   start, stop, line.c_str());
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
 * name       : hbs_cluster_dump
 *
 * Description: Formatted dump of the vault contents to the log file.
 *
 ***************************************************************************/
void hbs_cluster_dump ( mtce_hbs_cluster_type & vault )
{
    syslog ( LOG_INFO, "Cluster Vault Dump: --------------------------------------------------------------------------------------------");
    syslog ( LOG_INFO, "Cluster Vault: v%d.%d %d msec period ; SM Reqid is %d with storage-0 %s and %d histories in %d bytes",
            vault.version,
            vault.revision,
            vault.period_msec,
            vault.reqid,
            vault.storage0_enabled ? "enabled" : "disabled",
            vault.histories,
            vault.bytes );
    for ( int h = 0 ; h < vault.histories ; h++ )
    {
        #define MAX_LINE_LEN (500)
        char str[MAX_LINE_LEN] ;
        int i = 0 ;
        for ( int e = 0 ; e < vault.history[h].entries_max ; e++ )
        {
            snprintf ( &str[i], MAX_LINE_LEN, "%c[%d:%d]" ,
                       vault.history[h].oldest_entry_index==e ? '>' : ' ',
                       vault.history[h].entry[e].hosts_enabled,
                       vault.history[h].entry[e].hosts_responding);
            i = strlen(str) ;
        }
        syslog ( LOG_INFO, "Cluster Vault: C%d %s S:%s:%s (%d:%d) %s",
            vault.history[h].controller,
            hbs_cluster_network_name((mtce_hbs_network_enum)vault.history[h].network).c_str(),
            vault.storage0_enabled ? "y" : "n",
            vault.history[h].storage0_responding ? "y" : "n",
            vault.history[h].entries_max,
            vault.history[h].entries,
            str);
    }
    // dump_memory ( &vault, 16, vault.bytes );
}


