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

#ifndef __MTCEHBSCLUSTER_H__
#define __MTCEHBSCLUSTER_H__

#include <sys/types.h>

/**************************************************************
 *            Implementation Structure
 *************************************************************/

#define MTCE_HBS_CLUSTER_VERSION       (1)
#define MTCE_HBS_CLUSTER_REVISION      (0)
#define MTCE_HBS_MAGIC_NUMBER     (0x5aa5)

typedef enum
{
    MTCE_HBS_NETWORK_MGMT = 0,
    MTCE_HBS_NETWORK_CLSTR = 1,
#ifdef MONITORED_OAM_NETWORK
    MTCE_HBS_NETWORK_OAM,
#endif
    MTCE_HBS_NETWORKS
} mtce_hbs_network_enum ;

#ifdef THREE_CONTROLLER_SYSTEM
  #define MTCE_HBS_MAX_CONTROLLERS     (3)
#else
  #define MTCE_HBS_MAX_CONTROLLERS     (2)
#endif

#ifdef MONITORED_OAM_NETWORK
  #define MTCE_HBS_MAX_NETWORKS        (3)
#else
  #define MTCE_HBS_MAX_NETWORKS        (2)
#endif

// value of 20 at 100 msec period is 2 seconds of history */
#define MTCE_HBS_HISTORY_ENTRIES    (20)

/* maximum number of history elements permitted in a cluster history summary */
#define MTCE_HBS_MAX_HISTORY_ELEMENTS ((MTCE_HBS_MAX_CONTROLLERS)*(MTCE_HBS_NETWORKS))

#ifndef ALIGN_PACK
#define ALIGN_PACK(x) __attribute__((packed)) x
#endif

/* A single element of Heartbeat Cluster History for one heartbeat period */
typedef struct
{
    unsigned short hosts_enabled         ; /* # of hosts being hb monitored  */
    unsigned short hosts_responding      ; /* # of hosts that responsed to hb*/
}  ALIGN_PACK(mtce_hbs_cluster_entry_type);


/* Heartbeat Cluster History for all monitored networks of a Controller */
typedef struct
{
    unsigned short controller         :4 ; /* value 0 or 1 (and 2 in future) */
    unsigned short network            :4 ; /* see mtce_hbs_network_enum      */
    unsigned short reserved_bits      :6 ; /* future - initted to 0          */
    unsigned short sm_heartbeat_fail  :1 ; /* SM Health
                                              0 = SM Heartbeat OK,
                                              1 = SM Heartbeat Failure       */
    unsigned short storage0_responding:1 ; /* 1 = storage-0 is hb healthy    */
    unsigned short entries               ; /* # of valid values in .entry    */
    unsigned short entries_max           ; /* max size of the enry array     */
    unsigned short oldest_entry_index    ; /* the oldest entry in the array  */

    /* historical array of entries for a specific network */
    mtce_hbs_cluster_entry_type entry [MTCE_HBS_HISTORY_ENTRIES] ;

} ALIGN_PACK(mtce_hbs_cluster_history_type) ;

/* Heartbeat Cluster History for all monitored networks of all Controllers */
typedef struct
{
    /* Header - Static Data - 4 bytes                                        */
    unsigned char  version         ; /* public API MTCE_HBS_CLUSTER_VERSION  */
    unsigned char  revision        ; /* public API MTCE_HBS_CLUSTER_REVISION */
    unsigned short magic_number    ; /* public API MTCE_HBS_MAGIC_NUMBER     */

    /* Control - Dynamic Data - 8 bytes                                      */
    unsigned short reqid           ; /* added from SM cluster request        */
    unsigned short period_msec     ; /* heartbeat period in milliseconds     */
    unsigned short bytes           ; /* total struct size self check         */
    unsigned char  storage0_enabled; /* bool containing true or false        */
    unsigned char  histories       ; /* How many hostory elements follow     */

    /* Array of Cluster History
     *
     * - histories above specifies how many
     *   elements of this array are populated.
     */
    mtce_hbs_cluster_history_type history [MTCE_HBS_MAX_HISTORY_ELEMENTS] ;

} ALIGN_PACK(mtce_hbs_cluster_type) ;

#endif // __HBSCLUSTER_H__
