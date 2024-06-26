/*
 * Copyright (c) 2019, 2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <net/if.h>          /* for ... IF_NAMESIZE                         */

using namespace std;

#include "daemon_ini.h"      /* for ... ini_parse                           */
#include "daemon_common.h"   /* for ... daemon common definitions and types */
#include "nodeBase.h"        /* for ... maintenance base definitions        */
#include "nodeTimers.h"      /* for ... mtcTimer_init/start/stop utilities  */
#include "nodeUtil.h"        /* for ... common utils like open_ioctl_socket */
#include "httpUtil.h"        /* for ... httputil_setup                      */
#include "nlEvent.h"         /* for ... open_netlink_socket                 */
#include "fmAPI.h"           /* for ... FMTimeT                             */

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "mon"

#ifndef INTERFACES_DIR
#define INTERFACES_DIR ((const char *)"/sys/class/net/")
#endif
#define PLATFORM_DIR   ((const char *)"/etc/platform/platform.conf")
#define LMON_DIR       ((const char *)"/etc/lmon/lmon.conf")

#define INTERFACES_MAX (4) /* maximum number of interfaces to monitor */

string iface_type ( iface_type_enum type_enum );

/* daemon only supports the GET request */
#define HTTP_SUPPORTED_METHODS  (EVHTTP_REQ_GET)

typedef struct
{
    int              ioctl_socket     ;
    int              netlink_socket   ;
    libEvent         http_event       ;
    msgSock_type     mtclogd          ;
    int              dos_log_throttle ;
    struct mtc_timer audit_timer      ;

    char my_hostname[MAX_HOST_NAME_SIZE+1];
    char my_address [MAX_CHARS_IN_IP_ADDR+1];

} lmon_ctrl_type ;

typedef struct
{
    const char * name ;  /* pointer to well known primary interface name */

    /* primary interface names */
    #define MGMT_INTERFACE_NAME  ((const char *)"mgmt")
    #define CLUSTER_HOST_INTERFACE_NAME ((const char *)"cluster-host")
    #define OAM_INTERFACE_NAME   ((const char *)"oam")
    #define DATA_NETWORK_INTERFACE_NAME   ((const char *)"data-network")

    /* name labels used in platform.conf */
    #define MGMT_INTERFACE_FULLNAME  ((const char *)"management_interface")
    #define CLUSTER_HOST_INTERFACE_FULLNAME ((const char *)"cluster_host_interface")
    #define OAM_INTERFACE_FULLNAME   ((const char *)"oam_interface")
    #define DATA_NETWORK_INTERFACE_FULLNAME   ((const char *)"data_network_interface")

    /* true if the interface is configured.
     * i.e. the name label shown above is found in platform.conf */
    bool used ;
    iface_type_enum type_enum ;

    /* true if the link is up ; false otherwise */
    bool interface_one_link_up ;
    bool interface_two_link_up ;

    FMTimeT interface_one_event_time ;
    FMTimeT interface_two_event_time ;

    /* Config Items */
    const char * severity  ;              /* MINOR, MAJOR or CRITICAL for each resource */
    unsigned int debounce  ;              /* Period to wait before clearing alarms  */
    unsigned int num_tries ;              /* Number of times a resource has to be in
                                             failed or cleared state before sending alarm */

    /* Dynamic Data */
    char          interface_one[IF_NAMESIZE] ; /* primary interface */
    char          interface_two[IF_NAMESIZE] ; /* second interface if lagged  */
    char          bond[IF_NAMESIZE]          ; /* bonded interface name */
    bool          lagged                     ; /* Lagged interface=true or not=false     */

} interface_ctrl_type ;


/* lmonHdlr.cpp */
void daemon_exit ( void );
void lmon_learn_interfaces ( int ioctl_sock );

/* lmonUtils.cpp */
FMTimeT lmon_fm_timestamp     ( void );
int     lmon_interfaces_init  ( interface_ctrl_type * ptr,
                                string physical_iface_name );
int     lmon_get_link_state   ( int    ioctl_socket,
                                char   iface[IF_NAMESIZE],
                                bool & link_up );
string  get_interface_fullname( const char * iface_name );
int     read_the_lmon_config  ( string iface_fullname,
                                string& physical_interface );
void    set_the_link_state    ( int ioctl_socket,
                                interface_ctrl_type * ptr );
