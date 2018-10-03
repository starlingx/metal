/*
 * Copyright (c) 2018 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file StarlingX Maintenance Heartbeat Cluster Manager Module
 *
 *************************************************************************
 *
 * This module provides API for the hbsAgent service to call to
 * collect, store and send heartbeat cluster information to SM
 * upon request. See hbsCluster.h for formal API.
 *
 *************************************************************************/

#ifndef __HBSCLUSTER_H__
#define __HBSCLUSTER_H__

using namespace std;

#include "mtceHbsCluster.h" /* for ... the public API */

/****************************************************************************
 *
 * Name        : BYTES_IN_CLUSTER_VAULT
 *
 * Description : Calculates the number of bytes in the cluster vault based on
 *               the number of valid history array elements included.
 *
 * Parameters  :
 *
 ***************************************************************************/

#define BYTES_IN_CLUSTER_VAULT(e) \
    (sizeof(mtce_hbs_cluster_type)-(sizeof(mtce_hbs_cluster_history_type)*(MTCE_HBS_MAX_HISTORY_ELEMENTS-e)))

/****************************************************************************
 *
 * Name        : CHECK_CTRL_NTWK_PARMS
 *
 * Description :
 *
 * Parameters  :
 *
 ***************************************************************************/

#define CHECK_CTRL_NTWK_PARMS(c,n)               \
    if (( c > MTCE_HBS_MAX_CONTROLLERS ) ||      \
        ( n > MTCE_HBS_NETWORKS ))               \
    {                                            \
        slog ("Invalid parameter: %d:%d", c, n); \
        return ;                                 \
    }

/****************************************************************************
 *
 * Name        : GET_CLUSTER_HISTORY_PTR
 *
 * Description :
 *
 * Parameters  :
 *
 ***************************************************************************/

#define GET_CLUSTER_HISTORY_PTR(cluster, c,n)          \
    for ( int h = 0 ; h < cluster.histories ; h++ )    \
    {                                                  \
        if (( cluster.history[h].controller == c ) &&  \
            ( cluster.history[h].network == n ))       \
        {                                              \
            history_ptr = &cluster.history[h] ;        \
        }                                              \
    }


#define SET_CONTROLLER_HOSTNAME(c)                      \
    if ( c == 0 )                                       \
        controller = CONTROLLER_0 ; \
    else if ( c == 1 ) \
        controller = CONTROLLER_1 ; \
    else if ( c == 2 ) \
        controller = CONTROLLER_2 ; \
    else \
        controller = "unknown" \

#endif // __HBSCLUSTER_H__
