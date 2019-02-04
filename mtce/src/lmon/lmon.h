/*
 * Copyright (c) 2019 Wind River Systems, Inc.
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

#define INTERFACES_DIR ((const char *)"/sys/class/net/")
#define PLATFORM_DIR   ((const char *)"/etc/platform/platform.conf")

#define INTERFACES_MAX (3) /* maximum number of interfaces to monitor */

enum interface_type { ethernet = 0, vlan = 1, bond = 2 };
string iface_type ( interface_type type_enum );

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
    #define INFRA_INTERFACE_NAME ((const char *)"infra")
    #define OAM_INTERFACE_NAME   ((const char *)"oam")

    /* name labels used in platform.conf */
    #define MGMT_INTERFACE_FULLNAME  ((const char *)"management_interface")
    #define INFRA_INTERFACE_FULLNAME ((const char *)"infrastructure_interface")
    #define OAM_INTERFACE_FULLNAME   ((const char *)"oam_interface")

    /* true if the interface is configured.
     * i.e. the name label shown above is found in platform.conf */
    bool used ;
    interface_type type_enum ;

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

//    unsigned int     debounce_cnt   ; /* running monitor debounce count         */
//    unsigned int     minorlog_cnt   ; /* track minor log count for thresholding */
//    unsigned int     count          ; /* track the number of times the condition has been occured */
//    bool             failed         ; /* track if the resource needs to be serviced by the resource handler */
//    int              resource_value ; /* 1 if the interface is up and 0 if it is down   */
//    int     resource_value_lagged   ; /* 1 if the interface is up and 0 if it is down for lagged interfaces  */
//    int              sev            ; /* The severity of the failed resource */
//  rmonStage_enum   stage          ; /* The stage the resource is in within the resource handler fsm */
//    char alarm_id[FM_MAX_BUFFER_LENGTH] ; /* Used by FM API, type of alarm being raised */
//    char alarm_id_port[FM_MAX_BUFFER_LENGTH] ; /* Used by FM API, type of alarm being raised for the ports */
//    char errorMsg[ERR_SIZE];
//    rmon_api_socket_type msg;
//    bool link_up_and_running; /* whether the interface is up or down initially */

//    bool alarm_raised;
//    int failed_send; /* The number of times the rmon api failed to send a message */


} interface_ctrl_type ;


/* lmonHdlr.cpp */
void daemon_exit ( void );
void lmon_learn_interfaces ( int ioctl_sock );

/* lmonUtils.cpp */
FMTimeT lmon_fm_timestamp     ( void );
int     lmon_interfaces_init  ( interface_ctrl_type * ptr );
int     lmon_get_link_state   ( int    ioctl_socket,
                                char   iface[IF_NAMESIZE],
                                bool & link_up );
