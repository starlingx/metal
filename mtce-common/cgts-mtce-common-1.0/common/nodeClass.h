#ifndef __INCLUDE_NODECLASS_H__
#define __INCLUDE_NODECLASS_H__
/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance "Node Manager"
 * class, support structs and enums.
 */

#include <sys/types.h>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <list>
#include <vector>

#define WANT_MTC
#define WANT_HBS

using namespace std;

/* Include base class definition header */
#include "nodeBase.h"
#include "hostUtil.h"     /* for ... server_code and others           */
#include "nodeTimers.h"
#include "threadUtil.h"   /* for ... thread_info_type thread_ctrl_type*/
#include "pingUtil.h"     /* for ... ping_info_type                   */
#include "nodeCmds.h"     /* for ... mtcCmd type                      */
#include "httpUtil.h"     /* for ... libevent stuff                   */
#include "ipmiUtil.h"     /* for ... mc_info_type                     */
#include "mtcHttpUtil.h"  /* for ... libevent stuff                   */
#include "mtcSmgrApi.h"   /*                                          */
#include "alarmUtil.h"    /* for ... SFmAlarmDataT                    */
#include "mtcAlarm.h"     /* for ... MTC_ALARM_ID__xx and utils       */
#include "mtcThreads.h"   /* for ... mtcThread_ipmitool               */

/**Default back-to-back heartbeat failures for disabled-failed condition */
#define HBS_FAILURE_THRESHOLD  10

/** Default back-to-back heartbeat failures for enabled-degraded condition */
#define HBS_DEGRADE_THRESHOLD   6

/** Default back-to-back heartbeat failures for enabled-degraded condition */
#define HBS_MINOR_THRESHOLD     4

/** If Debug, this number of missed heartbeats in a row creates a info log */
#define HBS_DBG_LOG_THRESHOLD   1

/** Clear (reset) heartbeat counter value */
#define HBS_CLEAR_COUNT         0

#ifdef SIMPLEX
#undef SIMPLEX
#endif
#define SIMPLEX \
    ( daemon_is_file_present ( PLATFORM_SIMPLEX_MODE ) == true )

#define THIS_HOST \
    ( node_ptr->hostname == this->my_hostname )

#define NOT_THIS_HOST \
    ( node_ptr->hostname != this->my_hostname )

#define LARGE_SYSTEM \
    ( this->system_type == SYSTEM_TYPE__NORMAL )

#define CPE_SYSTEM \
    ( this->system_type != SYSTEM_TYPE__NORMAL )

#define SIMPLEX_CPE_SYSTEM \
    ( this->system_type == SYSTEM_TYPE__CPE_MODE__SIMPLEX )

/**
 * @addtogroup nodeLinkClass
 * @{
 *
 * This class is used to maintain a linked list of nodes that
 * represent currently provisioned inventory. Its member
 * functions and data members along with the support files
 * in maintenance and heartbeat feature directories blend
 * to create a Higly Available and Reuseable Maintenance system.
 */

class nodeLinkClass
{
private:

    /** A single node entity within the nodeLinkClass that can
     * be spliced in or out of a node linked list
     */
    struct node {

       /**
         * @addtogroup private_Node_variables
         * @{
         *
         * A set of variables that make up a node including linking members.
         */

        /** The name of the host node */
        std::string uuid ;

        /** The name of the host node */
        std::string hostname ;

        /** The IP address of the host node */
        std::string ip  ;

        /** The Mac address of the host node */
        std::string mac ;

        /** The infrastructure network IP address of the host node */
        std::string infra_ip  ;

        /** The Mac address of the host's infra interface */
        std::string infra_mac  ;

        /** The type of node 'controller' or 'compute' node */
        std::string type ;

        /** Short text phrase indicating the operation the FSM is
         *  taking on this host */
        std::string task ;

        /** Administrative action from inventory */
        std::string action ;

        /** The Node Type ; compute or control or storage as a mask */
        string       functions       ; /* comma delimited string of host types */
        unsigned int nodetype        ; /* numeric mask of functions */

        string       function_str    ; /* single host type string representing
                                          the main function of the host        */
        unsigned int function        ; /* numeric representing function_str    */

        string       subfunction_str ; /* single host type string ie "compute" */
        unsigned int subfunction     ; /* numeric representing subfunction_str */

        /** set to true if the host specific sub function enable handler passes */
        bool subf_enabled ;

        /** set true if the BMC is provisioned */
        bool bm_provisioned ;


        /** general retry counter */
        int    retries   ;

        /** number of http rest API retries since last clear */
        int http_retries_cur ;

        /* Command handler retries counter */
        int  cmd_retries ;

        /* Retry counter for power actions (on/off)*/
        int  power_action_retries ;

        /** Generic toggle switch */
        bool toggle ;

        /** back to back health failure counter */
        int    health_threshold_counter ;

        int    mtce_flags ;

        /* true if this node is patching */
        bool patching ;

        /* true if this node is patched but not reset */
        bool patched  ;

        /** The node's reported uptime */
        unsigned int uptime ;
        unsigned int uptime_save ;

        /** Set to true once the host's add FSM is done  */
        bool   add_completed ;

        int uptime_refresh_counter ;

        /** Counts the number of times this node was unlocked.
          * NOTE: This value should be stored in the database.
          *       so that it is not reset to 0 on every swact.
          */
        int    node_unlocked_counter ;

        int    mtcalive_timeout ;

        /* start host service retry controls */
        int  start_services_retries ;

        bool start_services_running_main ;
        bool start_services_running_subf ;

        bool start_services_needed  ;
        bool start_services_needed_subf ; /* for the add handler that defers
                                             start to the inservice test handler.
                                             this provides a means of telling
                                             maintenance that the subfunction
                                             start needs to also be run. */

        /** Pointer to the previous node in the list */
        struct node *prev;

        /** Pointer to the next node in the list */
        struct node *next;

        /** @} private_Node_variables */


        /** @addtogroup private_Maintenance_variables
         * @{
         *
         * Finite State Machine variables and member functions
         *  for 'this' host/node
         *
         *  The CGTS Maintenacne syste follows the X.731 maintenance model
         *  which uses the states below; For full list of states please
         *  refer to nodeBase.h
         *
         *  A brief summary is  (host and node are used inter-changably)
         *
         *  Administrative Action: Actions a user may take on a host at the user
         *                         interface ; i.e. Lock, Unlock, Reset, Reinstall
         *
         *  Administrative State : The state a host enters into when the above
         *                         actions are taken ; i.e. Locked or Unlocked.
         *
         *  Operational State    : The operating state of the node based on the
         *                         administrative actions ; Enabled or Disabled.
         *
         *  Availability State   : The useability state of a host based on the
         *                         two previous states and events that may occur
         *                         over time ; i.e. available, failed, degraded,
         *                         intest.
         */
        mtc_nodeAdminAction_enum  adminAction ; /**< Administrative Action  */
        list<mtc_nodeAdminAction_enum> adminAction_todo_list ; /**< Administrative Action  */

        mtc_nodeAdminState_enum   adminState  ; /**< Administrative State   */
        mtc_nodeOperState_enum    operState   ; /**< Operational State      */
        mtc_nodeAvailStatus_enum  availStatus ; /**< Availability Status    */
        mtc_nodeConfigAction_enum configAction; /**< Configuration Action   */

        mtc_nodeOperState_enum    operState_subf   ; /**< Subfunction Operational State   */
        mtc_nodeAvailStatus_enum  availStatus_subf ; /**< Subfunction Availability Status */

        mtc_nodeOperState_enum    operState_dport  ; /**< Data Port Operational State   */
        mtc_nodeAvailStatus_enum  availStatus_dport; /**< Data Port Availability Status */


        /** Maintains the current handler stage.
          * This is a union of all handler types such as enable,
          * disable, degrade etc. See nodeBase.h for list of union members */
        mtc_stages_union          handlerStage;

        /* Individual FSM handler stages */
        mtc_offlineStages_enum    offlineStage    ;
        mtc_onlineStages_enum     onlineStage     ;
        mtc_swactStages_enum      swactStage      ;
        mtc_addStages_enum        addStage        ;
        mtc_delStages_enum        delStage        ;
        mtc_recoveryStages_enum   recoveryStage   ;
        mtc_oosTestStages_enum    oosTestStage    ;
        mtc_insvTestStages_enum   insvTestStage   ;
        mtc_configStages_enum     configStage     ;
        mtc_resetProgStages_enum  resetProgStage  ;
        mtc_reinstallStages_enum  reinstallStage  ;

        /** Board management specific FSM Stages */
        mtc_powerStages_enum      powerStage         ;
        mtc_powercycleStages_enum powercycleStage    ;
        mtc_subStages_enum        subStage           ;
        mtc_resetStages_enum      resetStage         ;
        mtc_sensorStages_enum     sensorStage        ;


        /** This gate is used to block mtcAlive messages from reaching
          * the state machine until its ready to receive them.
          *
          * Issue: The mtcClient on a slave host will continuously send the
          * mtcAlive 'I'm here' messages after a reboot and until that message
          * is acknowledged. This is done to make the recovery of a host more
          * robust in a potentially lossy network. Without this, a single
          * dropped mtcAlive message could result in an unlock-enable timeout
          * which would lead to a disabled-failed state and re-recovery attempt
          * after a recovery timeout (mtcTimers.h:HOST_MTCALIVE_TIMEOUT)
          * period. Besides the system administrator seeing a disabled-failed
          * condition the customer would realize a longer than nessary outage
          * of that host.
          *
          * Fix: By having the mtcClient repeatedly send the mtcAlive message
          * on reset recovery until it is acknowledged by active mtcAgent
          * prevents the above issue. However it has a side affect on the
          * maintenance FSM for that host. This mtcAlive gate prevents
          * the state machine from seeing mtcAlive messages when it does not
          * care about them.
          */
        bool mtcAlive_gate     ;
        int mtcAlive_count     ;
        int mtcAlive_misses    ;
        int mtcAlive_hits      ;
        int mtcAlive_purge     ;

        bool mtcAlive_mgmnt ; /* set true when mtcAlive is rx'd from mgmnt network */
        bool mtcAlive_infra ; /* set true when mtcAlive is rx'd from infra network */

        /* Both of these booleans are set true upon receipt of a mtcAlive message. */
        bool mtcAlive_online  ; /* this is consumed by online and offline handler  */
        bool mtcAlive_offline ; /* this is consumed by reset progression handler   */

        int  offline_search_count ; /* count back-2-back mtcAlive request misses */

        bool offline_log_reported ; /* prevents offline/online log flooding when   */
        bool  online_log_reported ; /*   availStatus switches between these states */
                                    /*   and failed                                */

        /** Host's mtc timer struct. Use to time handler stages.
         *
         *  reset     -> reset command response
         *  reboot    -> then wait for mtcalive message
         *  mtcalive  -> then wait for go enabled message
         */
        struct mtc_timer mtcAlive_timer ;

        /* the fault handling offline handler timer */
        struct mtc_timer offline_timer ;

        /* Host level DOR recovery mode time and bools */
        int              dor_recovery_time  ;
        bool             dor_recovery_mode  ;
        bool         was_dor_recovery_mode  ;

        /** Integer code representing the host health */
        int  health ;

        /** Flag indicating that the unknown health state
         *  has already been reported */
        bool unknown_health_reported ;

        /* Booleans indicating the main or subfunction  has config failure */
        bool config_failed      ;
        bool config_failed_subf ;

        /* Booleans indicating the main or subfunction has passed the OOS test */
        bool goEnabled      ;
        bool goEnabled_subf ;

        /* Booleans indicating the main or subfunction has failed the OOS test */
        bool goEnabled_failed      ;
        bool goEnabled_failed_subf ;

        /* Boolean indicating the main or subfunction has start host services
         * failure. */
        bool hostservices_failed      ;
        bool hostservices_failed_subf ;

        /* Boolean indicating the main or subfunction has inservice failure */
        bool inservice_failed      ;
        bool inservice_failed_subf ;

        /** node has reached enabled state this number of times */
        bool enabled_count ;

        /** Number of OOS tests run so far */
        int  oos_test_count ;

        /** Number of INSV tests run so far */
        int  insv_test_count ;

        /** Used to throttle inservice recovery actions */
        int  insv_recovery_counter ;

        /** when true requests the task for this host be cleared at first opportunity */
        bool clear_task ;

        /** Host's mtc timer struct. Use to time handler stages.
         *
         *  reset     -> reset command response
         *  reboot    -> then wait for mtcalive message
         *  mtcalive  -> then wait for go enabled message
         */
        struct mtc_timer mtcTimer   ;
        struct mtc_timer http_timer ;
        struct mtc_timer mtcCmd_timer ;
        struct mtc_timer oosTestTimer  ;
        struct mtc_timer insvTestTimer ;
        struct mtc_timer mtcSwact_timer ;
        struct mtc_timer mtcConfig_timer ;
        struct mtc_timer power_timer ;
        struct mtc_timer host_services_timer ;

        mtcCmd host_services_req ;
        mtcCmd mtcAlive_req      ;
        mtcCmd reboot_req        ;
        mtcCmd general_req       ;

        /* String that is used in the command handling logs which represents
         * the specific command handling that is in progress */
        string       cmdName ;

        /** Indicates presence of a command request */
        unsigned int cmdReq ;

        /** Indicates presence of a command response */
        unsigned int cmdRsp;

        /** Indicates acknowledgement of the initial host
         *  services command in execution monitoroing mode */
        unsigned int cmdAck;

        /** Command Response Status - Execution Status */
        unsigned int cmdRsp_status ;

        /**  Command Response Data - typically an error details string */
        string       cmdRsp_status_string  ;

        bool reboot_cmd_ack_mgmnt ;
        bool reboot_cmd_ack_infra ;

        /** Tracks back to back Fast Fault Recovery counts */
        int  graceful_recovery_counter;

        /** Reboot acknowledge */
        mtc_client_enum activeClient ;

        /** @} private_Maintenance_variables */

        /**
          * @addtogroup private_libEvent_structs
          * @{
          *
          * libEvent structures used to issue libEvent
          * HTTP REST API Requests to control this host
          * based on each service */

        libEvent sysinvEvent; /**< Sysinv REST API Handling for host */
        libEvent    cfgEvent; /**< Sysinv REST API Handling for config changes */
        libEvent vimEvent   ; /**< VIM Event REST API Handling       */

        libEvent httpReq    ; /**< Http libEvent Request Handling    */
        libEvent thisReq    ; /**< Http libEvent Request Handling    */

        list<libEvent>           libEvent_work_fifo ;
        list<libEvent>::iterator libEvent_work_fifo_ptr;
        list<libEvent>           libEvent_done_fifo ;
        list<libEvent>::iterator libEvent_done_fifo_ptr;

        // bool work_ready ;
        int  oper_sequence ;
        int  oper_failures ;
        int  no_work_log_throttle ;
        int          log_throttle ;

        /* List of queue'ed mtce commands for this host */
        mtcCmd                 cmd;
        list<mtcCmd>           mtcCmd_work_fifo ;
        list<mtcCmd>::iterator mtcCmd_work_fifo_ptr;
        list<mtcCmd>           mtcCmd_done_fifo ;
        list<mtcCmd>::iterator mtcCmd_done_fifo_ptr;

        /** @} private_libEvent_structs and utils */

        /**
          * @addtogroup private_Heartbeat_variables
          * @{
          *
          * A grouping a of private variables at the node level used to
          * control if a node is to be monitored, the monitoring failure
          * counts and next / previous pointers used to create the
          * monitored node pulse linked list
          */

        /** Set 'true' when node minor threshold has exceeded */
        bool hbs_minor[MAX_IFACES] ;

        /** Set 'true' when node is degraded due to back to back heartbeat pulse
         *  misses tha exceed the major threshold */
        bool   hbs_degrade[MAX_IFACES] ;

        /** Set 'true' when node is failed due to back to back heartbeat pulse
         *  misses that exceed the critical threshold */
        bool   hbs_failure[MAX_IFACES] ;

        /** log throttle controls for heartbeat service */
        int stall_recovery_log_throttle ;
        int stall_monitor_log_throttle ;
        int lookup_mismatch_log_throttle ;
        int unexpected_pulse_log_throttle ;

        /** Pulse Next and Previous Link pointers for creating
         *  a per-interface pulse link list */
        struct {

            /** previous pulse pointer used to create the pulse linked list for one interface */
            struct node * prev_ptr ;

            /** next pulse pointer used to create the pulse linked list for one interface  */
            struct node * next_ptr ;

        } pulse_link [MAX_IFACES] ;

        /** The link index number for this node is while in an interface pulse linked list */
        int  linknum [MAX_IFACES] ;

        /** true if this host is to be monitored for this indexed interface */
        bool monitor [MAX_IFACES] ;

        /** Ongoing heartbeat count cleared on HBS_START reset */
        int  hbs_count [MAX_IFACES] ;

        /** Immediate running count of consecutive heartbeat misses */
        int  b2b_misses_count [MAX_IFACES];

        /** Maximum heartbeat misses since node was last brought into service */
        int  max_count [MAX_IFACES];

        /** total times minor count was exceeded */
        int  hbs_minor_count [MAX_IFACES];

        /** total times this host degraded due to heartbeat misses */
        int  hbs_degrade_count [MAX_IFACES];

        /** total times this host failed due to heartbeat loss */
        int  hbs_failure_count [MAX_IFACES];

        /** current state of heartbeat failure per interface for mtcAgent */
        bool heartbeat_failed [MAX_IFACES];

        /** Resource reference identifier, aka resource reference array index */
        int  rri     ;

        /** @} private_Heartbeat_variables */

        /**
          * @addtogroup private_boad_management_variables
          * @{
          *
          * Various host specific board management variables.
          */

        /** The IP address of the host's board management controller */
        string bm_ip ;

        /** The password of the host's board management controller */
        string bm_pw ;

        /** A string label that represents the board management
         *  controller type for this host */
        string bm_type ;

        /** The operator provisioned board management hostname */
        string bm_un ;

        /* Indicates there is a board management test
         * for this host in progress */
        bool bm_test_in_progress ;

        /* Indicates there is a board management operation
         * in progress on this host */
        bool bm_oper_in_progress ;

        /**
         *   The BMC is 'accessible' once provisioning data is available
         *   and bmc is verified pingable.
         **/
        bool bm_accessible;

        /** @} private_boad_management_variables */

        /**
          * @addtogroup private_monitoring_services_variables
          * @{
          *
          * A grouping a of flags, mask and degrade resource lists 
          * used to manage the degrade state of a host for process
          * and resource monitoring services.
          */

        /* Bit mask of degrade reasons */
        unsigned int degrade_mask ;
        unsigned int degrade_mask_save ;

        /** Process Monitor Daemon Flag Missing count */
        int  pmon_missing_count ;

        /** Host degraded due to loss of Process Monitor running flag */
        bool pmon_degraded ;

        /** Process Monitor Ready flag and degrade list */
        bool pmond_ready ;

        /** Hardware Monitor Ready flag and degrade list */
        bool hwmond_ready ;
        bool hwmond_monitor ;

        /** Heartbeat Client process ready to heartbeat flag */
        bool hbsClient_ready ;

        /** hwmon reset and powercycle recovery control structure */
        recovery_ctrl_type hwmon_reset ;
        recovery_ctrl_type hwmon_powercycle ;

        /** Resource Monitor Daemon Flag Missing count */
        int  rmond_missing_count ;

        /** Host degraded due to loss of Resource Monitor running flag */
        bool rmond_degraded ;

        /** Resource Monitor Ready flag and degrade list */
        bool rmond_ready ;
        std::list<string> degraded_resources_list   ;

        /** process or resource list string iterator */
        std::list<string>::iterator string_iter_ptr ;

        /** @} private_monitoring_services_variables */

        /* List of alarms and current severity */
        EFmAlarmSeverityT alarms[MAX_ALARMS];

        /* tracks whether the alarms for this host have been loaded already or not */
        bool alarms_loaded ;

        /** true if this host has recovered before the mnfa timeout period.
         *  This bool flags the graceful recovery handler that this node
         *  is recovering from mnfa and should manage graceful recovery
         *  and uptime accordingly */
        bool mnfa_graceful_recovery ;

        int stress_iteration ;

        /* for bmc ping access monitor */
        ping_info_type bm_ping_info ;

        /* the bmc info struct filled in and log printed by a
         * call to ipmiUtil_mc_info_load. */
        mc_info_type   mc_info    ;

        bool   mc_info_query_active ;
        bool   mc_info_query_done   ;

        bool   reset_cause_query_active ;
        bool   reset_cause_query_done   ;

        bool   power_status_query_active ;
        bool   power_status_query_done   ;
        bool   power_on = false ;

        /* a timer used in the bm_handler to query
         * the mc_info and reset cause */
        struct mtc_timer bm_timer         ;

        /* timer used to manage the bmc access alarm */
        struct mtc_timer bmc_access_timer ;

        /*****************************************************
         *            Maintenance Thread Structs
         *****************************************************/
        /* control data the parent uses to manage the thread */
        thread_ctrl_type ipmitool_thread_ctrl ;

        /*info the thread uses to execute and post results   */
        thread_info_type ipmitool_thread_info  ;

        /* extra thread info for board management control thread */
        thread_extra_info_type thread_extra_info ;

    };

    struct node * head ; /**< Node Linked List Head pointer */
    struct node * tail ; /**< Node Linked List Tail pointer */

   /** Allocate memory for a new node.
    *
    * Preserves the node address in the node_ptr list and increments
    * the memory_allocs counter used by the inservice test audit.
    *
    * @return
    * a pointer to the memory of the newly allocated node */
    struct nodeLinkClass::node * newNode ( void );

   /** Build the Resource Reference Array */
   void build_rra ( void );

   /** Free the memory used by a node.
    *
    * The memory to be removed is found in the node_ptr list, cleared and
    * the memory_allocs counter is decremented.
    * If the memory cannot be found then an error is returned.
    *
    * @param node_ptr
    *  is a pointer to the node to be freed
    * @return
    *  a signed integer of PASS or -EINVAL
    */
    int delNode ( struct nodeLinkClass::node * node_ptr );

   /** Start heartbeating a new node.
    *
    * Node is added to the end of the node linked list.
    *
    * @param node_info_ptr
    *  is a pointer containing pertinent info about the physical node
    * @return
    *  a pointer to the newly added node
    */
    struct nodeLinkClass::node* addNode ( string hostname );
    struct nodeLinkClass::node* addUuid ( string uuid );

   /** Stop heartbeating a node.
    *
    * Node is spliced out of the node linked list.
    *
    * @param node_info_ptr
    *  is a pointer containing info required to find the node in the node list
    * @return
    *  an integer of PASS or  -EINVAL  */
    int remNode ( string hostname );

   /** Get pointer to "hostname" node.
    *
    * Node list lookup by pointer from hostname.
    *
    * @param node_info_ptr
    *  is a pointer containing info required to find the node in the node list
    * @return
    *  a pointer to the hostname's node
    */
    struct nodeLinkClass::node* getNode ( string hostname );

   /** Get the node pointer based on the service and libevent base pointer.
    *
    * Node list lookup by pointer service and libevent base pointer.
    *
    * @param libEvent_enum
    *  service type
    * @param base_ptr
    *  pointer to the libEvent base
    *
    * @return
    *  a pointer to the hostname's node
    */
    struct nodeLinkClass::node* getEventBaseNode ( libEvent_enum service,
                                           struct event_base * base_ptr);

   /** Get a reference to the libEvent containing the supplied
    *  libEvent.base pointer.
    *
    * @param base_ptr
    *  pointer to the libEvent base
    *
    * @return
    *  reference to valid or null libEvent
    */
    libEvent & getEvent ( struct event_base * base_ptr);

    int manage_dnsmasq_bmc_hosts ( struct nodeLinkClass::node * node_ptr );

    /* run the maintenance fsm against a host */
    int fsm ( struct nodeLinkClass::node * node_ptr );

    /* specific handlers called within the fsm */
    int enable_handler     ( struct nodeLinkClass::node * node_ptr );
    int recovery_handler   ( struct nodeLinkClass::node * node_ptr );
    int disable_handler    ( struct nodeLinkClass::node * node_ptr );
    int add_handler        ( struct nodeLinkClass::node * node_ptr );
    int delete_handler     ( struct nodeLinkClass::node * node_ptr );
    int cfg_handler        ( struct nodeLinkClass::node * node_ptr );
    int cmd_handler        ( struct nodeLinkClass::node * node_ptr );
    int swact_handler      ( struct nodeLinkClass::node * node_ptr );
    int reset_handler      ( struct nodeLinkClass::node * node_ptr );
    int reboot_handler     ( struct nodeLinkClass::node * node_ptr );
    int reinstall_handler  ( struct nodeLinkClass::node * node_ptr );
    int power_handler      ( struct nodeLinkClass::node * node_ptr );
    int powercycle_handler ( struct nodeLinkClass::node * node_ptr );
    int offline_handler    ( struct nodeLinkClass::node * node_ptr );
    int online_handler     ( struct nodeLinkClass::node * node_ptr );
    int oos_test_handler   ( struct nodeLinkClass::node * node_ptr );
    int insv_test_handler  ( struct nodeLinkClass::node * node_ptr );
    int stress_handler     ( struct nodeLinkClass::node * node_ptr );
    int bm_handler         ( struct nodeLinkClass::node * node_ptr );
    int degrade_handler    ( struct nodeLinkClass::node * node_ptr );
    int uptime_handler     ( void );

    int host_services_handler ( struct nodeLinkClass::node * node_ptr );

    /* Starts the specified 'reset or powercycle' recovery monitor */
    int hwmon_recovery_monitor ( struct nodeLinkClass::node * node_ptr, int hwmon_event );

    /* server specific power state query handler */
    bool (*is_poweron_handler) (string hostname, string query_response );

    /* Calculate the overall reset progression timeout */
    int calc_reset_prog_timeout ( struct nodeLinkClass::node * node_ptr, int retries );

    /* These interfaces will start and stop the offline FSM if not already active */
    void start_offline_handler ( struct nodeLinkClass::node * node_ptr );
    void stop_offline_handler  ( struct nodeLinkClass::node * node_ptr );

    /*****************************************************************************
     *
     * Name       : ipmi_command_send
     *
     * Description: This utility starts the ipmitool command handling thread
     *              with the specified command.
     *
     * Returns    : PASS if all the pre-start semantic checks pass and the
     *              thread was started.
     *
     *              Otherwise the thread was not started and some non zero
     *              FAIL_xxxx code is returned after a representative design
     *              log is generated.
     *
     *****************************************************************************/

    int ipmi_command_send ( struct nodeLinkClass::node * node_ptr, int command ) ;

    /*****************************************************************************
     *
     * Name       : ipmi_command_recv
     *
     * Description: This utility will check for ipmitool command thread completion.
     *
     * Returns    : PASS       is returned if the thread reports done.
     *              RETRY      is returned if the thread has not completed.
     *              FAIL_RETRY is returned after 10 back-to-back calls return RETRY.
     *
     * Assumptions: The caller is expected to call ipmi_command_done once it has
     *              consumed the results of the thread
     *
     *****************************************************************************/

    int  ipmi_command_recv ( struct nodeLinkClass::node * node_ptr );

    /*****************************************************************************
     *
     * Name       : ipmi_command_done
     *
     * Description: This utility frees the ipmitool command thread for next execution.
     *
     *****************************************************************************/

    void ipmi_command_done ( struct nodeLinkClass::node * node_ptr );

    /* default all the BMC access variaables to the "no access" state */
    void bmc_access_data_init ( struct nodeLinkClass::node * node_ptr );

    /* Combo Host enable handler */
    int enable_subf_handler ( struct nodeLinkClass::node * node_ptr );

    /** set all service readies to false so that when the first one comes in'
     * it will be logged */
    void clear_service_readies ( struct nodeLinkClass::node * node_ptr );

    int update_dport_states ( struct nodeLinkClass::node * node_ptr, int event );

    /* manage deciding to return or issue an immediate reboot if the
     * auto recovery threshold is exceeded. */
    void manage_autorecovery ( struct nodeLinkClass::node * node_ptr );

    /** ***********************************************************************
      *
      * Name       : nodeLinkClass::workQueue_process
      *
      * Description: This is a Per Host Finite State Machine (FSM) that
      *              processes the work queue for the supplied host's
      *              node pointer.
      *
      * Constructs:
      *
      * node_ptr->libEvent_work_fifo - the current work queue/fifo
      * node_ptr->libEvent_done_fifo - queue/fifo of completed requests
      *
      * Operations:
      *
      * requests are added   to   the libEvent_work_fifo with workQueue_enqueue.
      * requests are removed from the libEvent_done_fifo with workQueue_dequeue.
      *
      * Behavior:
      *
      * In process libEvents are copied from the callers work queue to
      * its thisReq.
      *
      * Completed events including execution status are copied to the host's
      * done fifo.
      *
      * Failed events may be retried up to max_retries as specified by
      * the callers libEvent.
      *
      * @param event is a reference to the callers libEvent.
      *
      * @return an integer with values of PASS, FAIL, RETRY
      *
      * Implementation: in maintenance/mtcWorkQueue.cpp
      *
      * ************************************************************************/
    int workQueue_process ( struct nodeLinkClass::node * node_ptr );

    /** ***********************************************************************
      *
      * Name       : nodeLinkClass::workQueue_del_cmd
      *
      * Description: To handle the pathalogical case where an event seems to
      *              have timed out at the callers level then this interface
      *              can be called to delete it from the work queue.
      *
      * @param node_ptr so that the hosts work queue can be found
      * @param sequence to specify the specific sequence number to remove
      * @return always PASS since there is nothing the caller can or needs
      * to do if the command is not present.
      *
      * Implementation: in maintenance/mtcWorkQueue.cpp
      *
      */
    int  workQueue_del_cmd ( struct nodeLinkClass::node * node_ptr, int sequence );

    int  doneQueue_purge   ( struct nodeLinkClass::node * node_ptr );
    int  workQueue_purge   ( struct nodeLinkClass::node * node_ptr );
    int  workQueue_done    ( struct nodeLinkClass::node * node_ptr );
    void workQueue_dump    ( struct nodeLinkClass::node * node_ptr );
    void doneQueue_dump    ( struct nodeLinkClass::node * node_ptr );

    int  mtcCmd_workQ_purge( struct nodeLinkClass::node * node_ptr );
    int  mtcCmd_doneQ_purge( struct nodeLinkClass::node * node_ptr );
    void mtcCmd_workQ_dump ( struct nodeLinkClass::node * node_ptr );
    void mtcCmd_doneQ_dump ( struct nodeLinkClass::node * node_ptr );

    void force_full_enable ( struct nodeLinkClass::node * node_ptr );

    int adminActionChange ( struct nodeLinkClass::node * node_ptr,
                           mtc_nodeAdminAction_enum newActionState );

    /** Host Administrative State Change member function */
    int adminStateChange  ( struct nodeLinkClass::node * node_ptr,
                           mtc_nodeAdminState_enum newAdminState );

    /** Host Operational State Change member function */
    int operStateChange  ( struct nodeLinkClass::node * node_ptr,
                           mtc_nodeOperState_enum newOperState );

    /** Host Availability Status Change member function */
    int availStatusChange  ( struct nodeLinkClass::node * node_ptr,
                             mtc_nodeAvailStatus_enum newAvailStatus );


    int allStateChange ( struct nodeLinkClass::node * node_ptr,
                          mtc_nodeAdminState_enum adminState,
                          mtc_nodeOperState_enum  operState,
                          mtc_nodeAvailStatus_enum availStatus );

    int subfStateChange ( struct nodeLinkClass::node * node_ptr,
                          mtc_nodeOperState_enum   operState_subf,
                          mtc_nodeAvailStatus_enum availStatus_subf );

    /** Host Enable Handler Stage Change member function */
    int enableStageChange   ( struct nodeLinkClass::node * node_ptr,
                              mtc_enableStages_enum newHdlrStage );

    /** Host Disable Handler Stage Change member function */
    int disableStageChange  ( struct nodeLinkClass::node * node_ptr,
                              mtc_disableStages_enum newHdlrStage );

    /** Host configuration stage Change member function */
    int configStageChange ( struct nodeLinkClass::node * node_ptr,
                              mtc_configStages_enum newHdlrStage );

    /** Host Reset Handler Stage Change member function */
    int resetStageChange    ( struct nodeLinkClass::node * node_ptr,
                              mtc_resetStages_enum newHdlrStage );

    /** Host Reinstall Handler Stage Change member function */
    int reinstallStageChange    ( struct nodeLinkClass::node * node_ptr,
                              mtc_reinstallStages_enum newHdlrStage );

    /** Host Fast graceful Recovery Handler Stage Change member function */
    int recoveryStageChange ( struct nodeLinkClass::node * node_ptr,
                              mtc_recoveryStages_enum newHdlrStage );

    /** Host Power control Handler Stage Change member function */
    int powerStageChange    ( struct nodeLinkClass::node * node_ptr,
                              mtc_powerStages_enum newHdlrStage );

    /** Host Powercycle control Handler Stage Change member function */
    int powercycleStageChange ( struct nodeLinkClass::node * node_ptr,
                              mtc_powercycleStages_enum newHdlrStage );

    /** Out-Of-Service Test Stage Change member function */
    int oosTestStageChange  ( struct nodeLinkClass::node * node_ptr,
                              mtc_oosTestStages_enum newHdlrStage );

    /** Inservice Test Stage Change member function */
    int insvTestStageChange ( struct nodeLinkClass::node * node_ptr,
                              mtc_insvTestStages_enum newHdlrStage );

    /** Host Sensor Handler Stage Change member function */
    int sensorStageChange   ( struct nodeLinkClass::node * node_ptr,
                              mtc_sensorStages_enum newHdlrStage );

    /** Generic Substage Stage change member function */
    int subStageChange      ( struct nodeLinkClass::node * node_ptr,
                              mtc_subStages_enum newHdlrStage );

    int failed_state_change ( struct nodeLinkClass::node * node_ptr );

    /* issue a
     *  - one way lazy reboot with
     *  - graceful SM services shutdown and
     *  - failsafe backup sysreq reset
     */
    int lazy_graceful_fs_reboot ( struct nodeLinkClass::node * node_ptr );

    int alarm_enabled_clear   ( struct nodeLinkClass::node * node_ptr, bool force );
    int alarm_enabled_failure ( struct nodeLinkClass::node * node_ptr );

    int alarm_insv_clear      ( struct nodeLinkClass::node * node_ptr, bool force );
    int alarm_insv_failure    ( struct nodeLinkClass::node * node_ptr );

    int alarm_config_clear    ( struct nodeLinkClass::node * node_ptr );
    int alarm_config_failure  ( struct nodeLinkClass::node * node_ptr );

    int alarm_compute_clear   ( struct nodeLinkClass::node * node_ptr, bool force );
    int alarm_compute_failure ( struct nodeLinkClass::node * node_ptr , EFmAlarmSeverityT sev );

    void clear_subf_failed_bools ( struct nodeLinkClass::node * node_ptr );
    void clear_main_failed_bools ( struct nodeLinkClass::node * node_ptr );
    void clear_hostservices_ctls ( struct nodeLinkClass::node * node_ptr );

    /** Find the node that has this timerID in its general mtc timer */
    struct nodeLinkClass::node * get_mtcTimer_timer   ( timer_t tid );
    struct nodeLinkClass::node * get_mtcConfig_timer  ( timer_t tid );
    struct nodeLinkClass::node * get_mtcAlive_timer   ( timer_t tid );
    struct nodeLinkClass::node * get_offline_timer    ( timer_t tid );
    struct nodeLinkClass::node * get_mtcSwact_timer   ( timer_t tid );
    struct nodeLinkClass::node * get_mtcCmd_timer     ( timer_t tid );
    struct nodeLinkClass::node * get_oosTestTimer     ( timer_t tid );
    struct nodeLinkClass::node * get_insvTestTimer    ( timer_t tid );
    struct nodeLinkClass::node * get_power_timer      ( timer_t tid );
    struct nodeLinkClass::node * get_http_timer       ( timer_t tid );
    struct nodeLinkClass::node * get_thread_timer     ( timer_t tid );
    struct nodeLinkClass::node * get_ping_timer       ( timer_t tid );
    struct nodeLinkClass::node * get_bm_timer         ( timer_t tid );
    struct nodeLinkClass::node * get_bmc_access_timer ( timer_t tid );
    struct nodeLinkClass::node * get_host_services_timer ( timer_t tid );

    struct nodeLinkClass::node * get_powercycle_control_timer  ( timer_t tid );
    struct nodeLinkClass::node * get_powercycle_recovery_timer ( timer_t tid );
    struct nodeLinkClass::node * get_reset_control_timer       ( timer_t tid );
    struct nodeLinkClass::node * get_reset_recovery_timer      ( timer_t tid );

    /* Launch the specified host services command start or stop for any host
     * type into the cmd_handler. In support of AIO a subf bool is optional
     * and forces the command to be COMPUTE (subfunction).
     * - requires cmd_handler fsm */
    int launch_host_services_cmd ( struct nodeLinkClass::node * node_ptr, bool start , bool subf=false );

    /* Private SYSINV API */
    int mtcInvApi_update_task       ( struct nodeLinkClass::node * node_ptr, string task );
    int mtcInvApi_update_task_now   ( struct nodeLinkClass::node * node_ptr, string task );
    int mtcInvApi_force_task        ( struct nodeLinkClass::node * node_ptr, string task );
    int mtcInvApi_update_task       ( struct nodeLinkClass::node * node_ptr, const char * task_str_ptr, int one );
    int mtcInvApi_update_task       ( struct nodeLinkClass::node * node_ptr, const char * task_str_ptr, int one, int two );

    int mtcInvApi_update_value      ( struct nodeLinkClass::node * node_ptr, string key, string value );
    int mtcInvApi_update_uptime     ( struct nodeLinkClass::node * node_ptr, unsigned int uptime );

    int mtcInvApi_subf_states       ( struct nodeLinkClass::node * node_ptr, string oper_subf, string avail_subf );
    int mtcInvApi_force_states      ( struct nodeLinkClass::node * node_ptr, string admin, string oper, string avail );
    int mtcInvApi_update_states     ( struct nodeLinkClass::node * node_ptr, string admin, string oper, string avail );
    int mtcInvApi_update_states_now ( struct nodeLinkClass::node * node_ptr, string admin, string oper, string avail, string oper_subf, string avail_subf);
    int mtcInvApi_update_state      ( struct nodeLinkClass::node * node_ptr, string state, string value );

    /* Private SM API */
    int mtcSmgrApi_request          ( struct nodeLinkClass::node * node_ptr, mtc_cmd_enum operation, int retries );

    /* Private VIM API */
    int  mtcVimApi_state_change     ( struct nodeLinkClass::node * node_ptr, libEvent_enum operation, int retries );

    int  set_bm_prov ( struct nodeLinkClass::node * node_ptr, bool state );

    void set_uptime ( struct nodeLinkClass::node * node_ptr, unsigned int uptime, bool force );

    // #endif /* WANT_MTC */

    /** Interface to asser or clear severity specific heartbeat alarms */
    void manage_heartbeat_alarm ( struct nodeLinkClass::node * node_ptr, EFmAlarmSeverityT sev, int iface );

    /** Returns the heartbeat monitoring state for the specified interface */
    bool get_hbs_monitor_state ( string & hostname, int iface );

   /** List of allocated node memory.
    *
    * An array of node pointers.
    */
    nodeLinkClass::node * node_ptrs[MAX_NODES] ;

   /** A memory allocation counter.
    *
    * Should represent the number of nodes in the linked list.
    */
    int memory_allocs ;

   /** A memory used counter
    *
    * A variable storing the accumulated node memory
    */
    int memory_used   ;

   /** Inservice memory management audit.
    *
    * Verifies that the node_ptr list and memory_allocs jive as well
    * as all the node pointers point to a node in the linked list.
    *
    * @return
    *  an integer representing a PASS or TODO: list other error codes.
    */
    int memory_audit   ( void );


    /* Simplex mode auto recovery bools
     *
     * Set to true when the autorecovery threshold is reached
     * and we want to avoid taking further autorecovery action
     * even though it may be requested. */
    bool autorecovery_disabled ;

    /* Set to true by fault detection methods that are
     * autorecoverable when in simplex mode. */
    bool autorecovery_enabled ;

    /** Tracks the number of hosts that 'are currently' in service trouble
     *  wrt heartbeat (above minor threshold).
     *  This is used in multi-host failure avoidance.
     **/
    int mnfa_host_count[MAX_IFACES] ;

    /** Tracks the number of times multi failure avoidance was exited */
    int mnfa_occurances ;

    /** true when the multi node failure count exceeds the multi
     *  node failure avoidance threshold and until there are no more
     *  in service trouble hosts */
    bool mnfa_active ;

    /** Recover or exit from the muli-node failure avoidance state
     *  This involves restarting the heartbeat on all the nodes
     *  that remain hbs_minor and clearing any heartbneat degrade
     *  states that remain. */
    void mnfa_exit  ( bool force );
    void mnfa_enter ( void );
    void mnfa_add_host     ( struct nodeLinkClass::node * node_ptr, iface_enum iface );
    void mnfa_recover_host ( struct nodeLinkClass::node * node_ptr );
    void hbs_minor_clear   ( struct nodeLinkClass::node * node_ptr, iface_enum iface );

    /* Dead Office Recovery - system level controls */
    void manage_dor_recovery ( struct nodeLinkClass::node * node_ptr, EFmAlarmSeverityT severity );
    void report_dor_recovery ( struct nodeLinkClass::node * node_ptr, string node_state_log_prefix );

    struct {
        struct node * head_ptr ; /**< Pulse Linked List Head pointer */
        struct node * tail_ptr ; /**< Pulse Linked List Tail pointer */
        struct node * last_ptr ; /**< Pulse Linked List running last pointer */
    } pulse_list [MAX_IFACES]  ;

    /** General Pulse Pointer used to build pulse linked list */
    struct node * pulse_ptr    ;

    /** Number monitored hosts (nodes) for a specified interface */
    int  pulses[MAX_IFACES] ;

    /** Resource reference Array: An array used to store
     *  resource references for the purpose of fast resource
     *  lookup making thwe heartbat service more scalable.
     *
     *  In this case it is an array of node link pointers
     *  that are in the current active pulse list. */
    struct node * hbs_rra[MAX_NODES];

   /** Pulse list node lookup pointer by hostname.
    *
    * Get pointer to "hostname" node located in the pulse list.
    *
    * @param hostname - a string containing the name of the host
    *                   to be searched for in the pulse list.
    * @param iface    - iface_enum specifying which interface linked
    *                   list to search.
    *
    * @return pointer to the node's control struct
    */
    struct nodeLinkClass::node* getPulseNode ( string & hostname, iface_enum iface );

    /** Manage the heartbeat pulse flags by node pointer
    *
    *  These flags contain service information sent by the replying host.
    *  One example of this is the pmond flag which indicates whether the process
    *  monitor is running on that host.
    *
    *  Flags that are not set are thresholded for degrade or alarm assertion
    *  or cleared when found to be set again.
    *
    * @param pulse_ptr - node's control struct pointer
    * @param flags     - integer containing a bit field set of flags
    *
    *  */
    void manage_pulse_flags ( struct nodeLinkClass::node* pulse_ptr, unsigned int flags );

   /** Remove a node from the pulse list by name, index or node pointer
    *
    * Deal with all the removal cases ; head, tail, full splice
    *
    * @return
    *  an integer of PASS or -FAULT, -ENXIO
    */
    int remPulse_by_name  ( string & hostname,            iface_enum iface, bool clear_b2b_misses_count, unsigned int flags );
    int remPulse_by_index ( string   hostname, int index, iface_enum iface, bool clear_b2b_misses_count, unsigned int flags );
    int remPulse          ( struct node * node_ptr,       iface_enum iface, bool clear_b2b_misses_count, unsigned int flags );


    /** Debug Dump Log Interfaces */
    void mem_log_general   ( void );
    void mem_log_general_mtce_hosts ( void );
    void mem_log_mnfa      ( void );

    void mem_log_dor       ( struct nodeLinkClass::node * node_ptr );
    void mem_log_identity  ( struct nodeLinkClass::node * node_ptr );
    void mem_log_network   ( struct nodeLinkClass::node * node_ptr );
    void mem_log_state1    ( struct nodeLinkClass::node * node_ptr );
    void mem_log_state2    ( struct nodeLinkClass::node * node_ptr );
    void mem_log_alarm1    ( struct nodeLinkClass::node * node_ptr );
    void mem_log_mtcalive  ( struct nodeLinkClass::node * node_ptr );
    void mem_log_stage     ( struct nodeLinkClass::node * node_ptr );
    void mem_log_test_info ( struct nodeLinkClass::node * node_ptr );
    void mem_log_bm        ( struct nodeLinkClass::node * node_ptr );
    void mem_log_heartbeat ( struct nodeLinkClass::node * node_ptr );
    void mem_log_hbs_cnts  ( struct nodeLinkClass::node * node_ptr );
    void mem_log_type_info ( struct nodeLinkClass::node * node_ptr );
    void mem_log_reset_info( struct nodeLinkClass::node * node_ptr );
    void mem_log_power_info( struct nodeLinkClass::node * node_ptr );
    void mem_log_thread_info ( struct nodeLinkClass::node * node_ptr );

    void print_node_info   ( struct nodeLinkClass::node * node_ptr );

// #endif

/** Public Interfaces that allow hosts to be
 *  added or removed from maintenance.
 */
public:

     nodeLinkClass();	/**< constructor */
    ~nodeLinkClass();   /**< destructor  */

    system_type_enum system_type ;

    string functions ;  /**< comma delimited string list of functions supported */
    bool maintenance ;
    bool heartbeat   ;

    /* offline_handler tuning controls */
    int offline_threshold ; /* number of back to back mtcAlive misses before offline */
    int offline_period    ; /* offline handler mtcAlive request period */

    /* dor mode data ; state and start time
     * - start time is used to compare how long slave hosts take to come up
     *   after the active controller has entered dor mode */
    bool dor_mode_active ;
    unsigned int dor_start_time  ;
    int  dor_mode_active_log_throttle ;

    bool hbs_disabled          ; /**< Control heartbeat service state    */
    bool hbs_state_change      ; /**< Flag service state change          */
    int  hbs_pulse_period      ; /**< The curent pulse period in msec    */
    int  hbs_pulse_period_save ; /**< preserved copy of hbs_pulse_period */

    /** a loop counter used to detect when the heartbeat service is silently failing */
    int  hbs_silent_fault_detector ;

    /* prevents flooding FM with the silent_fault detected log */
    int  hbs_silent_fault_logged ;

    /* tracks the number of pulse requests set on each interface */
    int  pulse_requests[MAX_IFACES] ;

    /** The number of heartbeat misses that result in a
     *  minor notification to maintenance */
    int  hbs_minor_threshold ;
    /** The number of heartbeat misses that result in a degraded state */
    int  hbs_degrade_threshold ;
    /** The number of heartbeat misses that result in a failed state   */
    int  hbs_failure_threshold ;

    /** Running Resource Reference Identifier */
    int  rrri ;

    bool     active ;
    bool is_active ( void )
    { return (active); }
    void   set_activity_state ( bool state )
    { active = state ; }

    /** Store the hostname of this controller */
    string my_hostname ; /**< */
    string my_local_ip ; /**< Primary IP address              */
    string my_float_ip ; /**< Secondary (floating) IP address */

    /*********  New Public Constructs for IPMI Comamnd Handling ***********/

    /* the main fsm entrypoint to service all hosts */
    void fsm ( void ) ;

   /** This controller's hostname set'er */
    void   set_my_hostname ( string hostname );

    /** This controller's hostname get'er */
    string get_my_hostname ( void );

    /** This controller's local ip addr set'er */
    void   set_my_local_ip ( string & hostname );

    /** This controller's local ip addr get'er */
    string get_my_local_ip ( void );

    /** This controller's local ip addr set'er */
    void   set_my_float_ip ( string & hostname );

    /** This controller's local ip addr get'er */
    string get_my_float_ip ( void );

    /** get ip address for any hostname */
    string get_hostaddr ( string & hostname );

    /** get mac address for any hostname and specified interface */
    string get_hostIfaceMac ( string & hostname, int iface );

    /** get infrastructure network ip address for any hostname */
    string get_infra_hostaddr ( string & hostname );

    /** set a node's ip address */
    int set_hostaddr ( string & hostname, string & ip );

    /** set a node's infrastructure ip address */
    int set_infra_hostaddr ( string & hostname, string & ip );

    /** get hostname for any hostname */
    string get_hostname ( string & hostaddr );

    /******************************/
    /* NODE TYPE Member Functions */
    /******************************/

    /** Fetch the node type (compute or controller) by hostname */
    int get_nodetype ( string & hostname );

    /** Check if a node is a controller */
    bool is_controller ( struct nodeLinkClass::node * node_ptr );

    /** Check if a node is a compute */
    bool is_compute             ( struct nodeLinkClass::node * node_ptr );
    bool is_compute_subfunction ( struct nodeLinkClass::node * node_ptr );

    string get_node_function_str    ( string hostname );
    string get_node_subfunction_str ( string hostname );

    /** Check if a node is a storage */
    bool is_storage ( struct nodeLinkClass::node * node_ptr );

    /** Check if a node is a controller by hostname */
    bool is_controller ( string & hostname );

    /** Check if a node is a compute by hostname */
    bool is_compute             ( string & hostname );
    bool is_compute_subfunction ( string & hostname );

    /** Check if a node is a storage by hostname */
    bool is_storage ( string & hostname );

    /** Sets a hosts's function and subfunction members */
    int update_host_functions ( string hostname , string functions );

    /***********************************************************/

    /** Number of provisioned hosts (nodes) */
    int hosts  ;

    /** Host has been deleted */
    bool host_deleted ;

    /** Host Administrative State Change public member function */
    int admin_state_change ( string hostname,
                             string newAdminState );

    /** Host Operational State Change public member function */
    int oper_state_change ( string hostname,
                            string newOperState );

    /** Host Availability Status Change public member function */
    int avail_status_change ( string hostname,
                              string newAvailStatus );

    /** Host Subfunction Operational State Change public member function */
    int oper_subf_state_change ( string hostname,
                                 string newOperState );

    /** Host Subfunction Availability Status Change public member function */
    int avail_subf_status_change ( string hostname,
                                   string newAvailStatus );



    /** Update mtce Key with Value */
    int update_key_value ( string hostname, string key , string value );

    /** This is the list of inventory by hostname.
      * The Maintenance FSM loops over this list
      * to provide maintenance service */
    std::list<string> hostname_inventory ;
    std::list<string>::iterator host ;

    std::list<string>           mnfa_awol_list ;
    void                        mnfa_timeout_handler ( void );

    /** Return the number of inventoried hosts */
    int num_hosts ( void );

    /** **********************************************************************
      *
      * Name       : nodeLinkClass::workQueue_enqueue
      *
      * Description: Adds the next sequence number to the supplied event
      *              reference, creates a log prefix based on the event's
      *              hostname, service, operation and sequence number
      *              (to avoid repeated recreation) and then copies that
      *              event to the work queue.
      *
      * @param event is a reference to the callers libEvent.
      * @return an integer with value of PASS.
      *
      * Implementation: in maintenance/mtcWorkQueue.cpp
      *
      * *********************************************************************/
    int workQueue_enqueue ( libEvent & event );

    /** **********************************************************************
      *
      * Name       : nodeLinkClass::doneQueue_dequeue
      *
      * Description: Searches the done queue for the event matching the supplied
      *              event reference , specifically the sequence number. If found
      *              it pulls the execution status information and then proceeds
      *              to remove it from the done queue.
      *
      * If the event is found then the event status is returned.
      * If not found then a RETRY is returned.
      * If the done event status is RETRY then a FAIL is returned since
      * it should not be on the done queue with a retry status.
      *
      * @param event is a reference to the callers libEvent
      * @return an integer with values of PASS, FAIL, RETRY
      *
      * Implementation: in maintenance/mtcWorkQueue.cpp
      *
      * ************************************************************************/
    int doneQueue_dequeue ( libEvent & event );

    bool workQueue_present ( libEvent & event );
    void workQueue_dump_all    ( void );
    void doneQueue_dump_all    ( void );
    void mtcCmd_workQ_dump_all ( void );
    void mtcCmd_doneQ_dump_all ( void );


    /** Add a host to the Node list */
    int add_host              ( node_inv_type & inv );
    int mod_host              ( node_inv_type & inv );
    int set_host_failed       ( node_inv_type & inv );

    /** Check to see if the node list already contains any of the following
      * information and reject the add or modify if it does
      *
      *    uuid
      *    hostname
      *    ip address
      *    mac address
      *
      **/
    int add_host_precheck ( node_inv_type & inv );

    int del_host ( string uuid );

    /** Returns empty string if not provisioned or the name of the host if it is */
    string get_host ( string uuid );
    string get_uuid ( string hostname );
    void   set_uuid ( string hostname, string uuid );
    void   set_task ( string hostname, string task );

    /** Updates the hostname and resource reference identifier
     *  based on the next one in the cycle */
    void   get_rris ( string & hostname, int & rri );

    /** Performs a service affecting symantic check on whether
      * the specified uuid can be locked.
      * In the case of a compute node it asks Nova.
      * In the case of a controller it verifies that there is
      * another controller active and inservice.
      *
      * @params uuid   string
      * @params reason int
      *
      * @returns true if locked and false otherwise
      *
      */
    bool can_uuid_be_locked ( string uuid , int & reason );

//#ifdef WANT_HBS
    /** Add a host to the Node list */
    int add_heartbeat_host ( const node_inv_type &inv );
// #endif

    void host_print (  struct nodeLinkClass::node * node_ptr );

    /** Remove a host from Node list */
    int rem_host ( string & hostname );

    /* Returns the active client. */
    mtc_client_enum get_activeClient ( string hostname );

    /* Sets the active client for this particular host. The first use of this
     * is or reset/reboot acknowledge to the VIm over an evacuate reset request
     * from within the reboot handler. */
    int set_activeClient ( string hostname, mtc_client_enum client );

    /** Get the number of compute hosts that are operationally 'enabled' */
    int enabled_compute_nodes ( void );

    /** Get the number of storage hosts that are operationally 'enabled' */
    int enabled_storage_nodes ( void );

    /** get the number of hosts that are enabled excluding the active controller */
    int enabled_nodes ( void );

    /** Get the system's storage backend type */
    int get_storage_backend ( void );

    /** Returns true if the storage pool has a monitor running on
     *  an unlocked-enabled storage host */
    bool is_storage_mon_enabled ( void ) ;

    /** true if the management link's operational state is up and running */
    bool mgmnt_link_up_and_running ;
    bool infra_link_up_and_running ;

    /** A boolean that is used to quickly determine if the infrastructure
      * network is provisioned and configured for this daemon to use */
    bool infra_network_provisioned ;

    /** A debug bool hat allows infrastructure heartbeat failures to only
     *  cause host degrade rather than failure */
    bool infra_degrade_only ;

    int  service_netlink_events   ( int nl_socket  , int ioctl_socket );
    void manage_heartbeat_minor   ( string hostname, iface_enum iface, bool clear_event );
    void manage_heartbeat_degrade ( string hostname, iface_enum iface, bool clear_event );
    void manage_heartbeat_failure ( string hostname, iface_enum iface, bool clear_event );

    /* Clear heartbeat failed flag for all interfaces */
    void manage_heartbeat_clear   ( string hostname, iface_enum iface );

   /** Test and Debug Members and Variables */

    /** Print node info banner */
    void print_node_info ( void );

    int testhead ( int test );

    int testmode ;

// #ifdef WANT_MTC

    /** Hostname of the Active Controller */
    std::string   active_controller_hostname ;

    /** Hostname of the Inactive Controller */
    std::string inactive_controller_hostname ;

    bool inactive_controller_is_patched ( void );
    bool inactive_controller_is_patching ( void );

    string get_inactive_controller_hostname ( void );
    void   set_inactive_controller_hostname ( string hostname );

    string get_active_controller_hostname ( void );
    void   set_active_controller_hostname ( string hostname );

    /** Returns 'true' if inactive controller main/subfunction is in-service
     *
     *  In-Service if "unlocked-enabled-available or
     *  unlocked-enabled-degraded
     */
    bool is_inactive_controller_main_insv ( void );
    bool is_inactive_controller_subf_insv ( void );

    /** Returns true if the specified hostname is the active controller */
    bool is_active_controller ( string hostname );

    /** Returns number of enabled controllers */
    int num_controllers_enabled ( void );

    /** Run the FSM against the specified host */
    int run_fsm ( string hostname );

    /** Post a specific enable handler stage */
    int set_enableStage ( string & hostname, mtc_enableStages_enum stage );

    /** Get a posted enable handler stage */
    mtc_enableStages_enum get_enableStage ( string & hostname );

    /* Set the reboot stage */
    int set_rebootStage ( string & hostname, mtc_resetProgStages_enum stage );



    /** handle an expired timer. Find the node with this
      * timer ID and set its ringer */
    void timer_handler   ( int sig, siginfo_t *si, void *uc);

    struct mtc_timer mtcTimer         ;
    struct mtc_timer mtcTimer_mnfa    ;
    struct mtc_timer mtcTimer_token   ;
    struct mtc_timer mtcTimer_uptime  ;

    /* System Level DOR recovery timer
     * Note: tid != NULL represents DOR Mode Active */
    struct mtc_timer mtcTimer_dor     ;

    unsigned int get_cmd_resp ( string & hostname );
    void         set_cmd_resp ( string & hostname, mtc_message_type & msg );

    void         set_uptime ( string & hostname, unsigned int uptime, bool force );
    unsigned int get_uptime ( string & hostname );

    void set_uptime_refresh_ctr ( string & hostname, int value );
    int  get_uptime_refresh_ctr ( string & hostname );


    /** Returns true when a 'maintenance alive' message for that
      * hostnamed node is received */
    void set_mtcAlive      ( string & hostname, int iface  );
    bool get_mtcAlive_gate ( string & hostname );
    void ctl_mtcAlive_gate ( string & hostname, bool gated );

    /** Store the latest mtce flags for the specified host
      * current flags are defined in nodebase.h
        #define MTC_FLAG__I_AM_CONFIGURED  (0x00000001)
        #define MTC_FLAG__I_AM_NOT_HEALTHY (0x00000002)
        #define MTC_FLAG__I_AM_HEALTHY     (0x00000004)
        #define MTC_FLAG__I_AM_LOCKED      (0x00000008)
    */
    void set_mtce_flags ( string hostname, int flags );

    /** Updates the node's health code
      * Codes are found in nodeBase.h
      *
      * - NODE_HEALTH_UNKNOWN    (0)
      * - NODE_HEALTHY           (1)
      * - NODE_UNHEALTHY         (2)
      *
      * */
    void set_health ( string & hostname, int health );

    /** Returns true when a 'go enabled' message for that
      * hostnamed node is received */
    void set_goEnabled_failed      ( string & hostname );
    void set_goEnabled             ( string & hostname );
    bool get_goEnabled             ( string & hostname );

    void set_goEnabled_failed_subf ( string & hostname );
    void set_goEnabled_subf        ( string & hostname );
    bool get_goEnabled_subf        ( string & hostname );

    int    set_subf_info           ( string hostname,
                                     string functions,
                                     string operState_subf,
                                     string availState_subf );

    /** Board management variable setter and getter utilities
     *  Only the bm_ip is propped through to the database */

    int    set_bm_ip   ( string hostname , string bm_ip );
    int    set_bm_type ( string hostname , string bm_type );
    int    set_bm_un   ( string hostname , string bm_un );

    bool   is_bm_ip_already_used  ( string bm_ip  );

    int    manage_bmc_provisioning ( struct node * node_ptr );

    string get_bm_ip   ( string hostname );
    string get_bm_un   ( string hostname );
    string get_bm_type ( string hostname );

    string get_hostname_from_bm_ip ( string bm_ip );

    string get_hwmon_info ( string hostname );

    int get_server_code ( string hostname );

    void set_hwmond_monitor_state ( string & hostname, bool state );
    bool get_hwmond_monitor_state ( string & hostname );

    int  manage_shadow_change ( string hostname );
    int  inotify_shadow_file_fd ;
    int  inotify_shadow_file_wd ;

    /** The multi node failure avoidance type */
    #define MNFA_NUMBER  0
    #define MNFA_PERCENT 1
    int mnfa_threshold_type    ;

    /** % of hosts that need to simultaneously fail before 'mnfa' kicks in */
    int mnfa_threshold_percent ;

    /** # of hosts that need to simultaneously fail before 'mnfa' kicks in */
    int mnfa_threshold_number  ;

    /** the calculated threshold */
    int mnfa_threshold ;

    /** Calculates and returns the mnfa threshold based on enabled hosts */
    int mnfa_calculate_threshold ( string hostname );

    /* collectd event handler */
    int collectd_notify_handler ( string & hostname,
                                  string & resource,
                                  string & state );

    /*****************************************
     ** Process Monitor Event Utilities API **
     *****************************************/

    /** Interface to declare that a key service on the
      * specified host is up, running and ready */
    int declare_service_ready  ( string & hostname, unsigned int service );

    /** Process Monitor 'Clear' Event handler.
      *
      * The process specified will be removed from the
      * 'degraded_processes_list' and 'critical_processes_list' for
      * the specified host.
      * if there are no other degraded/critical processes or other
      * degraded services/reasons against that host then
      * this handler will clear the degrade state for the
      * specified host all together. */
    int degrade_pmond_clear  ( string & hostname );

    /** Resource Monitor 'Clear' Event handler.
      *
      * The resource specified will be removed from the
      * 'degraded_resources_list' for specified host.
      * if there are no other degraded resources or other
      * degraded services/reasons against that host then
      * this handler will clear the degrade state for the
      * specified host all together. */
    int degrade_resource_clear  ( string & hostname, string & resource );

    /**
      *  If the pmond degrade flag is not set then do so.
      *  if the host is not degraded then set it to degraded. */
    int degrade_process_raise  ( string & hostname, string & process );

    /** if host is unlocked-enabled generate a process failure log */
    int log_process_failure  ( string & hostname, string & process );

    /** if host is unlocked-enabled generate a process failure alarm */
    int alarm_process_failure  ( string & hostname, string & process );

    /** Resource Monitor Raise Event handler.
     *
     *  The host will enter degrade state due to the specified resource
     *  not running properly. The resource name is recorded in the
     *  'degraded_resources_list' for specified host.
     *  Clearing degrade against this resource requires that host to
     *  send a clear event against that resource or for that host to
     *  fully re-enable */
    int degrade_resource_raise  ( string & hostname, string & resource );

    /** Generate a resource failure log if the host is unlocked */
    int log_resource_failure  ( string & hostname, string & resource );

    /** Hardware Process Monitor Degrade Event handler.
     *  see implementation for details */
    int node_degrade_control ( string & hostname, int state, string service );

    /** Hardware Monitor 'Action' Event method
      *
      * The hardware monitor daemon is calling out a sensor that
      * is operating out of spec. The command is the accompanying
      * action that hwmond requested as a recovery action to this failure.
      * The sensor is the sensor name that triggersed the event. */
    int invoke_hwmon_action  ( string & hostname, int action, string & sensor );

    /** Process Monitor Failed Event handler.
      *
      *  The host will go out of service and be reset and
      *  automatically re-enabled. */
    int critical_process_failed( string & hostname, string & process, unsigned int nodetype );

    /** Resource Monitor Failed Event handler.
      *
      *  The host will go out of service and be reset and
      *  automatically re-enabled. */
    int critical_resource_failed( string & hostname, string & resource );

    /************************************************************/

    /**
     *     Node state set'ers and get'ers
     */
    mtc_nodeAdminAction_enum get_adminAction ( string & hostname );
    int set_adminAction ( string & hostname, mtc_nodeAdminAction_enum adminAction );
    mtc_nodeAdminState_enum get_adminState ( string & hostname );
    int set_adminState ( string & hostname, mtc_nodeAdminState_enum adminState );
    mtc_nodeOperState_enum get_operState ( string & hostname );
    int set_operState ( string & hostname, mtc_nodeOperState_enum operState );
    mtc_nodeAvailStatus_enum get_availStatus ( string & hostname );
    int set_availStatus ( string & hostname, mtc_nodeAvailStatus_enum availStatus );

    /** Convert the supplied string to a valid maintenance Admin State enum  */
    mtc_nodeAdminState_enum  adminState_str_to_enum ( const char * admin_string_ptr );
    /** Convert the supplied string to a valid maintenance Oper State enum   */
    mtc_nodeOperState_enum   operState_str_to_enum  ( const char * oper_string_ptr );
    /** Convert the supplied string to a valid maintenance Avail Status enum */
    mtc_nodeAvailStatus_enum availStatus_str_to_enum ( const char * avail_string_ptr );

    /** Convert the supplied enum to the corresponding Admin Action string  */
    string adminAction_enum_to_str ( mtc_nodeAdminAction_enum val );
    /** Convert the supplied enum to the corresponding Admin State string  */
    string adminState_enum_to_str ( mtc_nodeAdminState_enum val );
    /** Convert the supplied enum to the corresponding Oper State string   */
    string operState_enum_to_str  ( mtc_nodeOperState_enum val );
    /** Convert the supplied enum to the corresponding Avail Status string */
    string availStatus_enum_to_str ( mtc_nodeAvailStatus_enum val );

    string get_operState_dport   ( string & hostname );
    string get_availStatus_dport ( string & hostname );

    /********************************************
     ** External Services Control Utilities API *
     ********************************************/

    /** number of times mtce will retry an API before it gives up.
     * Configurable option through mtc.ini */
    int api_retries ;

    /* Inventory APIs */
    int mtcInvApi_cfg_show   ( string hostname );
    int mtcInvApi_cfg_modify ( string hostname, bool install );

    int mtcInvApi_load_host     ( string & hostname , node_inv_type & info );
    int mtcInvApi_update_task   ( string hostname, string task );
    int mtcInvApi_force_task    ( string hostname, string task );
    int mtcInvApi_update_state  ( string hostname, string state, string value );
    int mtcInvApi_update_states ( string hostname, string admin, string oper, string avail );
    int mtcInvApi_force_states  ( string hostname, string admin, string oper, string avail );
    int mtcInvApi_subf_states   ( string hostname, string oper_subf, string avail_subf );

    int mtcInvApi_update_states_now ( string hostname, string admin, string oper, string avail, string oper_subf, string avail_subf );
    int mtcInvApi_update_task_now   ( string hostname, string task );

    int mtcInvApi_update_value  ( string hostname, string key,   string value );
    int mtcInvApi_update_uptime ( string hostname, unsigned int uptime );

    void mtcInvApi_add_handler ( struct evhttp_request *req, void *arg );
    void mtcInvApi_qry_handler ( struct evhttp_request *req, void *arg );
    void mtcInvApi_get_handler ( struct evhttp_request *req, void *arg );


    string mtcVimApi_state_get ( string hostname, int & http_status_code );

    int    mtcVimApi_system_info ( string & response );

    void mtcSmgrApi_handler    ( struct evhttp_request *req, void *arg );

    void mtcHttpUtil_handler   ( struct evhttp_request *req, void *arg );

    /* Update the authentication token as a work queue'd command */
    int mtcKeyApi_refresh_token ( string hostname );

    /* Update the authentication token now ; as a blocking request */
    int mtcKeyApi_get_token     ( string hostname );

    /*********************** Public Heartbeat Interfaces *********************/

    /** Creates a linked list of nodes to heartbeat for the specified port
    *
    * Based on unlocked enabled hosts and provisioned ports
    *
    * @param
    *  iface_enum specifying the port to create the pulse list for
    * @return
    *  a pointer to the head of the burndown checkin list for the specified port
    */
    int  create_pulse_list ( iface_enum iface );

    /** Clear the pulse list */
    void clear_pulse_list  ( iface_enum iface );

    /** Remove a host from an interface's pulse list */
    int remove_pulse ( string & hostname, iface_enum iface, int index, unsigned int flags );

   /** Manage the heartbeat pulse flags by hostname
    *
    *  These flags contain service information sent by the replying host.
    *  One example of this is the pmond flag which indicates whether the process
    *  monitor is running on that host.
    *
    *  Flags that are not set are thresholded for degrade or alarm assertion
    *  or cleared when found to be set again.
    *
    * @param hostname  - a string containing the name of the host
    *                    that sent the flags.
    * @param flags     - integer containing a bit field set of flags
    *
    **/
    void manage_pulse_flags ( string & hostname, unsigned int flags );

    /** Control the heartbeat monitoring state of a host */
    int mon_host ( const string & hostname, iface_enum iface, bool true_false );

    /** Return true if the pulse list is empty */
    bool pulse_list_empty ( iface_enum iface );

    void recalibrate_thresholds ( void );

    /** Handle heartbeat losses
     *
     * Any hosts that remain in the pulse list at the end
     * of the heartbeat period have not responded with a
     * pulse message suggesting a health issue with that host
     * This interface manages thresholding and acting on hosts
     * that exceed preset thresholds.
     *
     */
    int lost_pulses ( iface_enum iface );

    bool monitored_pulse ( string hostname , iface_enum iface );

    /** Print the pulse list */
    void print_pulse_list  ( iface_enum iface );

    /*********************** Public Heartbeat Pulse Data *********************/

    /** How many pulses in the list */
    int  hbs_expected_pulses[MAX_IFACES];

    /** How many pulses have come in */
    int  hbs_detected_pulses[MAX_IFACES];

    /** Flag indicating the hbs service is ready to start monitoring hosts */
    bool hbs_ready ;

    /*************************************************************************/


    void memDumpAllState ( void );
    void memDumpNodeState ( string hostname );

// #endif

    /** Common REST API Structs */

    /* System Management REST API Control Struct */
    libEvent sysinvEvent ;

    /* System Management REST API Control Struct */
    libEvent smgrEvent ;

    /* Keystone Authentication Token Control Struct */
    libEvent tokenEvent ;

    /** /etc/mtc.ini configurable timeouts */

    int compute_mtcalive_timeout;
    int controller_mtcalive_timeout ;
    int goenabled_timeout      ;
    int swact_timeout          ;
    int sysinv_timeout         ;
    int sysinv_noncrit_timeout ;
    int loc_recovery_timeout ; /**< Loss Of Communication Recovery Timeout        */
    int mnfa_recovery_timeout; /**< Multi-Node-Failure Avoidance Recovery Timeout */
    int work_queue_timeout   ;
    int node_reinstall_timeout ;

    /** /etc/mtc.ini configurable audit intervals */
    int insv_test_period  ;
    int oos_test_period   ;
    int uptime_period     ;
    int online_period     ;
    int token_refresh_rate;

    int  unknown_host_throttle ;
    int  invalid_arg_throttle  ;
};

/**
 * @} nodeLinkClass
 */

/* allocates nodeLinkClass node_ptr */
#define GET_NODE_PTR(hostname)                                    \
    nodeLinkClass::node * node_ptr = this->getNode ( hostname ) ; \
    if ( node_ptr == NULL )                                       \
    {                                                             \
        elog ("%s hostname unknown\n", hostname.c_str());         \
        return (FAIL_HOSTNAME_LOOKUP);                            \
    }

#define CHK_NODE_PTR(node_ptr)        \
    if ( node_ptr == NULL )           \
    {                                 \
        slog ("null node_ptr\n");     \
        return (FAIL_NULL_POINTER);   \
    }

nodeLinkClass * inv_init    ( void );
nodeLinkClass * get_mtcInv_ptr ( void );
            int module_init ( void );

const char * get_adminAction_str ( mtc_nodeAdminAction_enum action );
string       bmc_get_ip          ( string hostname, string mac , string & current_bm_ip );
void         clear_host_degrade_causes ( unsigned int & degrade_mask );
bool         sensor_monitoring_supported ( string hostname );
void         autorecovery_clear  ( string hostname );
void         log_mnfa_pool      ( std::list<string> & mnfa_awol_list );

#endif /* __INCLUDE_NODECLASS_H__ */
