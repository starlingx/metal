/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Nodal Health Check Service Node Implementation
  */

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <errno.h>  /* for ENODEV, EFAULT and ENXIO */
#include <unistd.h> /* for close and usleep */

using namespace std;

#ifdef  __AREA__
#undef  __AREA__
#define __AREA__ "---"
#endif

#include "nodeBase.h"
#include "threadUtil.h"
#include "nodeClass.h"
#include "nodeUtil.h"
#include "mtcNodeMsg.h"    /* for ... send_mtc_cmd         */
#include "nlEvent.h"       /* for ... get_netlink_events   */
#include "daemon_common.h"

#include "alarmUtil.h"
#include "mtcAlarm.h"
#include "alarm.h"
#include "hbsAlarm.h"
#include "hbsBase.h"

/** Initialize the supplied command buffer */
void mtcCmd_init ( mtcCmd & cmd )
{
    cmd.cmd   = 0 ;
    cmd.rsp   = 0 ;
    cmd.ack   = 0 ;
    cmd.retry = 0 ;
    cmd.parm1 = 0 ;
    cmd.parm2 = 0 ;
    cmd.task  = false ;
    cmd.status = RETRY ;
    cmd.status_string.clear();
    cmd.name.clear();
}

extern void mtcTimer_handler ( int sig, siginfo_t *si, void *uc);

const char mtc_nodeAdminAction_str[MTC_ADMIN_ACTIONS][20] =
{
    "none",
    "lock",
    "unlock",
    "reset",
    "reboot",
    "reinstall",
    "power-off",
    "power-on",
    "recovery",
    "delete",
    "powercycle",
    "add",
    "swact",
    "force-lock",
    "force-swact",
    "enable",
    "enable-subf",
};

const char * get_adminAction_str ( mtc_nodeAdminAction_enum action )
{
    if ( action >= MTC_ADMIN_ACTIONS )
    {
        slog ("Invalid admin action (%d)\n", action);
        action = MTC_ADMIN_ACTION__NONE ;
    }

    return ( &mtc_nodeAdminAction_str[action][0] );
}

const char mtc_nodeAdminState_str[MTC_ADMIN_STATES][15] =
{
    "locked",
    "unlocked",
};

string get_adminState_str ( mtc_nodeAdminState_enum adminState )
{
    if ( adminState >= MTC_ADMIN_STATES )
    {
        slog ("Invalid admin state (%d)\n", adminState );
        adminState = MTC_ADMIN_STATE__LOCKED ;
    }
    return ( mtc_nodeAdminState_str[adminState] );
}

bool adminStateOk ( string admin )
{
    if (( admin.compare(mtc_nodeAdminState_str[0])) &&
        ( admin.compare(mtc_nodeAdminState_str[1])))
    {
        wlog ("Invalid 'admin' state (%s)\n", admin.c_str());
        return ( false );
    }
    return (true);
}
const char mtc_nodeOperState_str[MTC_OPER_STATES][15] =
{
    "disabled",
    "enabled"
};

string get_operState_str ( mtc_nodeOperState_enum operState )
{
    if ( operState >= MTC_OPER_STATES )
    {
        slog ("Invalid oper state (%d)\n", operState );
        operState = MTC_OPER_STATE__DISABLED ;
    }
    return ( mtc_nodeOperState_str[operState] );
}

bool operStateOk ( string oper )
{
    if (( oper.compare(mtc_nodeOperState_str[0])) &&
        ( oper.compare(mtc_nodeOperState_str[1])))
    {
        wlog ("Invalid 'oper' state (%s)\n", oper.c_str());
        return ( false );
    }
    return (true);
}

const char mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS][15] =
{
    "not-installed",
    "available",
    "degraded",
    "failed",
    "intest",
    "power-off",
    "offline",
    "online",
    "offduty"
};

bool availStatusOk ( string avail )
{
    if (( avail.compare(mtc_nodeAvailStatus_str[0])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[1])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[2])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[3])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[4])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[5])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[6])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[7])) &&
        ( avail.compare(mtc_nodeAvailStatus_str[8])))
    {
        wlog ("Invalid 'avail' status (%s)\n", avail.c_str());
        return ( false );
    }
    return (true);
}

string get_availStatus_str ( mtc_nodeAvailStatus_enum availStatus )
{
    if ( availStatus > MTC_AVAIL_STATUS )
    {
        slog ("Invalid avail status (%d)\n", availStatus );
        availStatus = MTC_AVAIL_STATUS__FAILED ;
    }
    return ( mtc_nodeAvailStatus_str[availStatus] );
}

#ifdef WANT_nodeClass_latency_log /* Needs to be tied to a node */
#define NODECLASS_LATENCY_MON_START ((const char *)"start")
#define MAX_DELAY_B4_LATENCY_LOG  (1700)
void nodeClass_latency_log ( const char * label_ptr, int msecs )
{
    static unsigned long long prev__time = 0 ;
    static unsigned long long this__time = 0 ;

    this__time = gettime_monotonic_nsec () ;

    /* If label_ptr is != NULL and != start then take the measurement */
    if ( label_ptr && strncmp ( label_ptr, NODECLASS_LATENCY_MON_START, strlen(NODECLASS_LATENCY_MON_START)))
    {
        if ( this__time > (prev__time + (NSEC_TO_MSEC*(msecs))))
        {
            llog ("%4llu.%-4llu msec - %s\n",
                 ((this__time-prev__time) > NSEC_TO_MSEC) ? ((this__time-prev__time)/NSEC_TO_MSEC) : 0,
                 ((this__time-prev__time) > NSEC_TO_MSEC) ? ((this__time-prev__time)%NSEC_TO_MSEC) : 0,
                 label_ptr);
        }
    }
    /* reset to be equal for next round */
    prev__time = this__time ;
}
#endif

/* nodeLinkClass constructor */
nodeLinkClass::nodeLinkClass()
{
    this->is_poweron_handler          = NULL;
    for(unsigned int i=0; i<MAX_NODES; ++i)
    {
        this->node_ptrs[i]            = NULL;
    }

    this->offline_threshold           = 0;
    this->offline_period              = 0;
    /* this->mtcTimer                    = mtc_timer();
     * this->mtcTimer_mnfa               = mtc_timer();
     * this->mtcTimer_token              = mtc_timer();
     * this->mtcTimer_uptime             = mtc_timer();
    */
    this->api_retries                 = 0;
    for(unsigned int i =0; i<MAX_IFACES; ++i)
    {
        this->pulse_requests[i]       = 0;
        this->hbs_expected_pulses[i]  = 0;
        this->hbs_detected_pulses[i]  = 0;
    }
    this->compute_mtcalive_timeout    = 0;
    this->controller_mtcalive_timeout = 0;
    this->goenabled_timeout           = 0;
    this->loc_recovery_timeout        = 0;
    this->node_reinstall_timeout      = 0;
    this->token_refresh_rate          = 0;

    head = tail = NULL;
    memory_allocs = 0 ;
    memory_used   = 0 ;
    hosts = 0 ;
    host_deleted = false ;

    /* Init the base level pulse info and pointers for all interfaces */
    pulse_ptr = NULL ;
    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        pulse_list[i].head_ptr = NULL ;
        pulse_list[i].tail_ptr = NULL ;
        pulse_list[i].last_ptr = NULL ;
        pulses[i] = 0 ;
    }
    /* init the resource reference index to null */
    rrri = 0 ;

    /* Entry of RRA is reserved (not used) and set to NULL */
    hbs_rra[0] = static_cast<struct node *>(NULL) ;

    /* Make no assumption on the service */
    maintenance = false ;
    heartbeat   = false ;
    active      = false ; /* run active */
    active_controller = false ; /* true if this controller is active */

    /* Set some defaults for the hearbeat service */
    hbs_ready = false ;
    hbs_state_change = false ;
    hbs_disabled = true ;
    hbs_pulse_period = hbs_pulse_period_save = 0 ;
    hbs_minor_threshold   = HBS_MINOR_THRESHOLD ;
    hbs_degrade_threshold = HBS_DEGRADE_THRESHOLD ;
    hbs_failure_threshold = HBS_FAILURE_THRESHOLD ;
    hbs_failure_action = HBS_FAILURE_ACTION__FAIL ;
    hbs_silent_fault_detector = 0 ;
    hbs_silent_fault_logged   = false ;

    /* Start with null identity */
    my_hostname.clear() ;
    my_local_ip.clear() ;
    my_float_ip.clear() ;
    active_controller_hostname.clear() ;
    inactive_controller_hostname.clear() ;

    /* MNFA Activity Controls */
    mnfa_threshold  = 2 ; /* 2 hosts    */
    mnfa_timeout    = 0 ; /* no timeout */

    /* Start with no failures */
    mnfa_awol_list.clear();
    mnfa_host_count[MGMNT_IFACE] = 0 ;
    mnfa_host_count[INFRA_IFACE] = 0 ;
    mnfa_occurances = 0 ;
    mnfa_active     = false ;

    mgmnt_link_up_and_running = false ;
    infra_link_up_and_running = false ;
    infra_network_provisioned = false ;
    infra_degrade_only        = false ;

    dor_mode_active = false ;
    dor_start_time  = 0 ;
    dor_mode_active_log_throttle = 0 ;

    swact_timeout                = MTC_MINS_2 ;
    uptime_period                = MTC_UPTIME_REFRESH_TIMER ;
    online_period                = MTC_OFFLINE_TIMER ;
    sysinv_timeout               = HTTP_SYSINV_CRIT_TIMEOUT ;
    sysinv_noncrit_timeout       = HTTP_SYSINV_NONC_TIMEOUT ;
    work_queue_timeout           = MTC_WORKQUEUE_TIMEOUT    ;

    /* Init the auto recovery threshold and intervals to zero until
     * modified by daemon config */
    memset (&ar_threshold, 0, sizeof(ar_threshold));
    memset (&ar_interval, 0, sizeof(ar_interval));

    /* Inservice test periods in seconds - 0 = disabled */
    insv_test_period = 0 ;
    oos_test_period = 0 ;

    /* Init the inotify shadow password file descriptors to zero */
    inotify_shadow_file_fd = 0 ;
    inotify_shadow_file_wd = 0 ;

    /* Ensure that HA Swact gate is open on init.
     * This true gates maintenance commands */
    smgrEvent.mutex = false ;

    /* Init the event bases to null as they have not been allocated yet */
    sysinvEvent.base = NULL ;
    smgrEvent.base   = NULL ;
    tokenEvent.base  = NULL ;
    sysinvEvent.conn = NULL ;
    smgrEvent.conn   = NULL ;
    tokenEvent.conn  = NULL ;
    sysinvEvent.req  = NULL ;
    smgrEvent.req    = NULL ;
    tokenEvent.req   = NULL ;
    sysinvEvent.buf  = NULL ;
    smgrEvent.buf    = NULL ;
    tokenEvent.buf   = NULL ;

    unknown_host_throttle = 0 ;

    testmode = 0 ;
    module_init( );
}

/* nodeLinkClass destructor */
nodeLinkClass::~nodeLinkClass()
{
    /* Free any allocated host memory */
    for ( int i = 0 ; i < MAX_HOSTS ; i++ )
    {
        if ( node_ptrs[i] )
        {
            delete node_ptrs[i] ;
        }
    }
}

/* Clear start host service controls */
void nodeLinkClass::clear_hostservices_ctls ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr )
    {
        node_ptr->start_services_needed  = false ;
        node_ptr->start_services_needed_subf = false ;
        node_ptr->start_services_running_main = false ;
        node_ptr->start_services_running_subf = false ;
        node_ptr->start_services_retries = 0     ;
    }
}

/* Clear all the main function enable failure bools */
void nodeLinkClass::clear_main_failed_bools ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr )
    {
        node_ptr->config_failed             = false ;
        node_ptr->goEnabled_failed          = false ;
        node_ptr->inservice_failed          = false ;
        node_ptr->hostservices_failed       = false ;
        return;
    }
    slog ("null pointer\n");
}

/* Clear all the sub function enable failure bools */
void nodeLinkClass::clear_subf_failed_bools ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr )
    {
        node_ptr->config_failed_subf = false ;
        node_ptr->goEnabled_failed_subf = false ;
        node_ptr->inservice_failed_subf = false ;
        node_ptr->hostservices_failed_subf = false ;
        return;
    }
    slog ("null pointer\n");
}

/*
 * Allocates memory for a new node and stores its the address in node_ptrs
 * @param void
 * @return node pointer to the newly allocted node
 */
struct nodeLinkClass::node * nodeLinkClass::newNode ( void )
{
   struct nodeLinkClass::node * temp_node_ptr = NULL ;

   if ( memory_allocs == 0 )
   {
       memset ( node_ptrs, 0 , sizeof(struct node *)*MAX_NODES);
   }

   // find an empty spot
   for ( int i = 0 ; i < MAX_NODES ; i++ )
   {
      if ( node_ptrs[i] == NULL )
      {
          node_ptrs[i] = temp_node_ptr = new node ;
          memory_allocs++ ;
          memory_used += sizeof (struct nodeLinkClass::node);

          return temp_node_ptr ;
      }
   }
   elog ( "Failed to save new node pointer address\n" );
   return temp_node_ptr ;
}

/* Frees the memory of a pre-allocated node and removes
 * it from the node_ptrs list
 * @param node * pointer to the node memory address to be freed
 * @return int return code { PASS or -EINVAL }
 */
int nodeLinkClass::delNode ( struct nodeLinkClass::node * node_ptr )
{
    if ( memory_allocs > 0 )
    {
        for ( int i = 0 ; i < MAX_NODES ; i++ )
        {
            if ( node_ptrs[i] == node_ptr )
            {
                delete node_ptr ;
                node_ptrs[i] = NULL ;
                memory_allocs-- ;
                memory_used -= sizeof (struct nodeLinkClass::node);
                return PASS ;
            }
        }
        elog ( "Error: Unable to validate memory address being freed\n" );
    }
    else
       elog ( "Error: Free memory called when there is no memory to free\n" );

    return -EINVAL ;
}

 /*
  * Allocate new node and tack it on the end of the node_list
  */
struct
nodeLinkClass::node* nodeLinkClass::addNode( string hostname )
{
    /* verify node is not already provisioned */
    struct node * ptr = getNode ( hostname );
    if ( ptr )
    {
        /* if it is then clean it up and fall through */
        if ( !testmode )
        {
            wlog ("Warning: Node already provisioned\n");
        }
        if ( remNode ( hostname ) )
        {
            /* Should never get here but if we do then */
            /* something is seriously wrong */
            elog ("Error: Unable to remove node during reprovision\n");
            return static_cast<struct node *>(NULL);
        }
    }

    /* allocate memory for new node */
    ptr = newNode ();
    if( ptr == NULL )
    {
        elog ( "Error: Failed to allocate memory for new node\n" );
		return static_cast<struct node *>(NULL);
    }

    /* init the new node */
    ptr->hostname = hostname ;

    ptr->ip        = "" ;
    ptr->mac       = "" ;
    ptr->infra_ip  = "" ;
    ptr->infra_mac = "" ;

    ptr->patching              = false ;
    ptr->patched               = false ;

    /* the goenabled state bool */
    ptr->goEnabled             = false ;
    ptr->goEnabled_subf        = false ;

    clear_hostservices_ctls ( ptr );

    /* clear all the enable failure bools */
    clear_main_failed_bools ( ptr );
    clear_subf_failed_bools ( ptr );

    /* Set the subfunction to disabled */
    ptr->operState_subf   = MTC_OPER_STATE__DISABLED ;
    ptr->availStatus_subf = MTC_AVAIL_STATUS__NOT_INSTALLED ;

    ptr->operState_dport  = MTC_OPER_STATE__DISABLED ;
    ptr->availStatus_dport= MTC_AVAIL_STATUS__OFFDUTY ;

    ptr->enabled_count = 0 ;

    ptr->cmdName      = "";
    ptr->cmdReq       = 0 ;
    ptr->cmdRsp       = 0 ;
    ptr->cmdRsp_status= 0 ;
    ptr->cmdRsp_status_string = "" ;

    ptr->add_completed        = false ;

    /* init the hwmon reset and powercycle recovery control structures */
    recovery_ctrl_init ( ptr->hwmon_reset );
    recovery_ctrl_init ( ptr->hwmon_powercycle );

    /* Default timeout values */
    ptr->mtcalive_timeout  = HOST_MTCALIVE_TIMEOUT  ;

    /* no ned to send a reboot response back to any client */
    ptr->activeClient = CLIENT_NONE ;

    ptr->task   = "none" ;
    ptr->action = "none" ;
    ptr->clear_task = false ;

    ptr->mtcAlive_gate     = true  ;
    ptr->mtcAlive_online   = false ;
    ptr->mtcAlive_offline  = true  ;
    ptr->mtcAlive_misses   = 0     ;
    ptr->mtcAlive_hits     = 0     ;
    ptr->mtcAlive_count    = 0     ;
    ptr->mtcAlive_purge    = 0     ;

    ptr->offline_search_count = 0 ;
    ptr->mtcAlive_mgmnt = false ;
    ptr->mtcAlive_infra = false ;
    ptr->reboot_cmd_ack_mgmnt = false ;
    ptr->reboot_cmd_ack_infra = false ;

    ptr->offline_log_throttle = 0     ;
    ptr->offline_log_reported = true  ;
    ptr->online_log_reported  = false ;

    ptr->dor_recovery_mode    = false ;
    ptr->was_dor_recovery_mode= false ;
    ptr->dor_recovery_time    = 0     ;

    ptr->ar_disabled = false ;
    ptr->ar_cause = MTC_AR_DISABLE_CAUSE__NONE ;
    memset (&ptr->ar_count, 0, sizeof(ptr->ar_count));
    ptr->ar_log_throttle = 0 ;

    mtcTimer_init ( ptr->mtcTimer,         hostname, "mtc timer");       /* Init node's general mtc timer    */
    mtcTimer_init ( ptr->insvTestTimer,    hostname, "insv test timer");
    mtcTimer_init ( ptr->oosTestTimer,     hostname, "oos test timer");  /* Init node's oos test timer       */
    mtcTimer_init ( ptr->mtcSwact_timer,   hostname, "mtcSwact timer");  /* Init node's mtcSwact timer       */
    mtcTimer_init ( ptr->mtcCmd_timer,     hostname, "mtcCmd timer");    /* Init node's mtcCmd timer         */
    mtcTimer_init ( ptr->mtcConfig_timer,  hostname, "mtcConfig timer"); /* Init node's mtcConfig timer      */
    mtcTimer_init ( ptr->mtcAlive_timer ,  hostname, "mtcAlive timer");  /* Init node's mtcAlive timer       */
    mtcTimer_init ( ptr->offline_timer,    hostname, "offline timer");   /* Init node's FH offline timer     */
    mtcTimer_init ( ptr->http_timer,       hostname, "http timer" );     /* Init node's http timer           */
    mtcTimer_init ( ptr->bm_timer,         hostname, "bm timer" );       /* Init node's bm timer             */
    mtcTimer_init ( ptr->bm_ping_info.timer,hostname,"ping timer" );     /* Init node's ping timer           */
    mtcTimer_init ( ptr->bmc_access_timer, hostname, "bmc acc timer" );  /* Init node's bm access timer      */
    mtcTimer_init ( ptr->host_services_timer, hostname, "host services timer" ); /* host services timer      */

    mtcTimer_init ( ptr->hwmon_powercycle.control_timer,  hostname, "powercycle control timer");
    mtcTimer_init ( ptr->hwmon_powercycle.recovery_timer, hostname, "powercycle recovery timer");
    mtcTimer_init ( ptr->hwmon_reset.control_timer,       hostname, "reset control timer");
    mtcTimer_init ( ptr->hwmon_reset.recovery_timer,      hostname, "reset recovery timer");

    mtcCmd_init ( ptr->host_services_req );
    mtcCmd_init ( ptr->mtcAlive_req );
    mtcCmd_init ( ptr->reboot_req );
    mtcCmd_init ( ptr->general_req );

    ptr->configStage       = MTC_CONFIG__START   ;
    ptr->swactStage        = MTC_SWACT__START    ;
    ptr->offlineStage      = MTC_OFFLINE__IDLE   ;
    ptr->onlineStage       = MTC_ONLINE__START   ;
    ptr->addStage          = MTC_ADD__START      ;
    ptr->delStage          = MTC_DEL__START      ;
    ptr->recoveryStage     = MTC_RECOVERY__START ;
    ptr->insvTestStage     = MTC_INSV_TEST__RUN  ; /* Start wo initial delay */
    ptr->oosTestStage      = MTC_OOS_TEST__LOAD_NEXT_TEST ;
    ptr->resetProgStage    = MTC_RESETPROG__START;
    ptr->powerStage        = MTC_POWER__DONE     ;
    ptr->powercycleStage   = MTC_POWERCYCLE__DONE;
    ptr->subStage          = MTC_SUBSTAGE__DONE  ;
    ptr->reinstallStage    = MTC_REINSTALL__DONE ;
    ptr->resetStage        = MTC_RESET__START    ;
    ptr->enableStage       = MTC_ENABLE__START   ;
    ptr->disableStage      = MTC_DISABLE__START  ;

    ptr->oos_test_count = 0 ;
    ptr->insv_test_count = 0 ;
    ptr->insv_recovery_counter = 0 ;

    ptr->uptime = 0 ;
    ptr->uptime_refresh_counter = 0 ;
    ptr->node_unlocked_counter = 0 ;

    /* Good health needs to be learned */
    ptr->mtce_flags = 0 ;

    ptr->graceful_recovery_counter = 0 ;
    ptr->health_threshold_counter = 0 ;
    ptr->unknown_health_reported = false ;
    ptr->mnfa_graceful_recovery = false ;

    /* initialize all board management variables for this host */
    ptr->bm_ip = NONE ;
    ptr->bm_type = NONE ;
    ptr->bm_un = NONE ;
    ptr->bm_pw = NONE ;

    ptr->bm_provisioned = false ; /* assume not provisioned until learned   */
    ptr->power_on       = false ; /* learned on first BMC connection        */
    bmc_access_data_init ( ptr ); /* init all the BMC access vars all modes */

    /* init the alarm array only to have it updated later
     * with current alarm severities */
    for ( int id = 0 ; id < MAX_ALARMS ; id++ )
    {
        ptr->alarms[id] = FM_ALARM_SEVERITY_CLEAR ;
    }
    ptr->alarms_loaded   = false ;

    ptr->cfgEvent.base   = NULL ;
    ptr->sysinvEvent.base= NULL ;
    ptr->vimEvent.base   = NULL ;

    ptr->httpReq.base    = NULL ;
    ptr->libEvent_done_fifo.clear();
    ptr->libEvent_work_fifo.clear();

    ptr->oper_sequence   = 0 ;
    ptr->oper_failures   = 0 ;

    ptr->mtcCmd_work_fifo.clear();
    ptr->mtcCmd_done_fifo.clear();

    ptr->cfgEvent.conn   = NULL ;
    ptr->sysinvEvent.conn= NULL ;
    ptr->vimEvent.conn   = NULL ;
    ptr->httpReq.conn    = NULL ;

    ptr->cfgEvent.req    = NULL ;
    ptr->sysinvEvent.req = NULL ;
    ptr->vimEvent.req    = NULL ;
    ptr->httpReq.req     = NULL ;


    ptr->cfgEvent.buf    = NULL ;
    ptr->sysinvEvent.buf = NULL ;
    ptr->vimEvent.buf    = NULL ;
    ptr->httpReq.buf     = NULL ;

    /* log throttles */
    ptr->stall_recovery_log_throttle = 0 ;
    ptr->stall_monitor_log_throttle = 0 ;
    ptr->unexpected_pulse_log_throttle = 0 ;
    ptr->lookup_mismatch_log_throttle = 0 ;
    ptr->log_throttle = 0 ;
    ptr->no_work_log_throttle = 0 ;
    ptr->no_rri_log_throttle = 0 ;

    ptr->degrade_mask = ptr->degrade_mask_save = DEGRADE_MASK_NONE ;

    ptr->degraded_resources_list.clear () ;
    ptr->pmond_ready = false ;
    ptr->rmond_ready = false ;
    ptr->hwmond_ready = false ;
    ptr->hbsClient_ready = false ;

    ptr->toggle = false ;

    ptr->retries = 0 ;
    ptr->http_retries_cur = 0 ;
    ptr->cmd_retries = 0 ;
    ptr->power_action_retries = 0 ;

    ptr->subf_enabled = false ;

    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        ptr->pulse_link[i].next_ptr = NULL  ;
        ptr->pulse_link[i].prev_ptr = NULL  ;
        ptr->monitor[i]             = false ;
        ptr->hbs_minor[i]           = false ; 
        ptr->hbs_degrade[i]         = false ; 
        ptr->hbs_failure[i]         = false ; 
        ptr->max_count[i]           = 0 ;
        ptr->hbs_count[i]           = 0 ; 
        ptr->hbs_minor_count[i]     = 0 ;
        ptr->hbs_misses_count[i]    = 0 ;
        ptr->b2b_misses_count[i]    = 0 ;
        ptr->b2b_pulses_count[i]    = 0 ;
        ptr->hbs_degrade_count[i]   = 0 ;
        ptr->hbs_failure_count[i]   = 0 ;
        ptr->heartbeat_failed[i]    = false;
    }

    ptr->health = NODE_HEALTH_UNKNOWN ;

    ptr->pmon_missing_count = 0; 
    ptr->pmon_degraded = false ; 

    /* now add it to the node list ; dealing with all conditions */
  
    /* if the node list is empty add it to the head */
    if( head == NULL )
    {
        head = ptr ;  
        tail = ptr ;
        ptr->prev = NULL ;
        ptr->next = NULL ;
    }
    else
    {
        /* link in the new_node to the tail of the node_list
         * then mark the next field as the end of the node_list
         * adjust tail to point to the last node
         */
        tail->next = ptr  ;
        ptr->prev  = tail ;
        ptr->next  = NULL ;
        tail = ptr ; 
    }

    /* start with no action and an empty todo list */
    ptr->adminAction = MTC_ADMIN_ACTION__NONE    ;
    ptr->adminAction_todo_list.clear();

    hosts++ ;

    /* (re)build the Resource Reference Array */
    if ( heartbeat )
        build_rra ();

    return ptr ;
}

struct nodeLinkClass::node* nodeLinkClass::getNode ( string hostname )
{
   /* check for empty list condition */
   if ( head == NULL )
      return NULL ;

   if ( hostname.empty() )
      return static_cast<struct node *>(NULL);

   for ( struct node * ptr = head ;  ; ptr = ptr->next )
   {
       if ( !hostname.compare ( ptr->hostname ))
       {
           return ptr ;
       }
       /* Node can be looked up by ip addr too */
       if ( !hostname.compare ( ptr->ip ))
       {
           return ptr ;
       }
       /* Node can be looked up by infra_ip addr too */
       if ( !hostname.compare ( ptr->infra_ip ))
       {
           return ptr ;
       }
       /* Node can be looked up by uuid too */
       if ( !hostname.compare ( ptr->uuid ))
       {
           return ptr ;
       }

       if (( ptr->next == NULL ) || ( ptr == tail ))
          break ;
    }
    return static_cast<struct node *>(NULL);
}


struct nodeLinkClass::node* nodeLinkClass::getEventBaseNode ( libEvent_enum request, 
                                                       struct event_base * base_ptr)
{
    struct node * ptr = static_cast<struct node *>(NULL) ;

   /* check for empty list condition */
   if ( head == NULL )
       return NULL ;

   if ( base_ptr == NULL )
       return NULL ;

   for ( ptr = head ;  ; ptr = ptr->next )
   {
       switch ( request )
       {
           case SYSINV_HOST_QUERY:
           {
               if ( ptr->sysinvEvent.base == base_ptr )
               {
                   hlog1 ("%s Found Sysinv Event Base Pointer (%p)\n", 
                             ptr->hostname.c_str(), ptr->sysinvEvent.base);

                   return ptr ;
               }
           }
           case VIM_HOST_DISABLED:
           case VIM_HOST_ENABLED:
           case VIM_HOST_OFFLINE:
           case VIM_HOST_FAILED:
           {
               if ( ptr->vimEvent.base == base_ptr )
               {
                   hlog1 ("%s Found vimEvent Base Pointer (%p) \n",
                             ptr->hostname.c_str(), ptr->vimEvent.base);

                   return ptr ;
               }
           }
           default:
               ;
       } /* End Switch */
       
       if (( ptr->next == NULL ) || ( ptr == tail ))
          break ;
    }

    wlog ("%s Event Base Pointer (%p) - Not Found\n",
              ptr->hostname.c_str(), base_ptr);

    return static_cast<struct node *>(NULL);

}

/* Find the node in the list of nodes being heartbeated and splice it out */
int nodeLinkClass::remNode( string hostname )
{
    int rc = -ENODEV ;
    if ( hostname.c_str() == NULL )
        return -EFAULT ;

    if ( head == NULL )
        return -ENXIO ;

    struct node * ptr = getNode ( hostname );

    if ( ptr == NULL )
        return -EFAULT ;

    mtcTimer_fini ( ptr->mtcTimer );
    mtcTimer_fini ( ptr->mtcSwact_timer );
    mtcTimer_fini ( ptr->mtcAlive_timer );
    mtcTimer_fini ( ptr->offline_timer );
    mtcTimer_fini ( ptr->mtcCmd_timer );
    mtcTimer_fini ( ptr->http_timer );

    mtcTimer_fini ( ptr->insvTestTimer );
    mtcTimer_fini ( ptr->oosTestTimer );
    mtcTimer_fini ( ptr->mtcConfig_timer );
    mtcTimer_fini ( ptr->host_services_timer );
    mtcTimer_fini ( ptr->hwmon_powercycle.control_timer );
    mtcTimer_fini ( ptr->hwmon_powercycle.recovery_timer );
    mtcTimer_fini ( ptr->hwmon_reset.control_timer );
    mtcTimer_fini ( ptr->hwmon_reset.recovery_timer );

    mtcTimer_fini ( ptr->bm_timer );
    mtcTimer_fini ( ptr->bmc_access_timer );
    mtcTimer_fini ( ptr->bm_ping_info.timer );

#ifdef WANT_PULSE_LIST_SEARCH_ON_DELETE

    /* Splice the node out of the pulse monitor list */
   
    for ( int i = 0 ; i < MAX_IFACES ; i++ )
    {
        /* Does the pulse monitor list exist ? */
        if ( pulse_list[i].head_ptr != NULL )
        {
            pulse_ptr = ptr ;
            if ( pulse_list[i].head_ptr == pulse_ptr )
            {
                if ( pulse_list[i].head_ptr == pulse_list[i].tail_ptr )
                {
                    pulse_list[i].head_ptr = NULL ;
                    pulse_list[i].tail_ptr = NULL ;
                    dlog ("Pulse: Single Node -> Head Case\n");           
                }
                else
                {
                    dlog ("Pulse: Multiple Nodes -> Head Case\n");
                    pulse_list[i].head_ptr = pulse_list[i].head_ptr->pulse_link[i].next_ptr ;
                    pulse_list[i].head_ptr->pulse_link[i].prev_ptr = NULL ; 
                }
            }  
            else if ( pulse_list[i].tail_ptr == pulse_ptr )
            {
                dlog ("Pulse: Multiple Node -> Tail Case\n");
                pulse_list[i].tail_ptr = pulse_list[i].tail_ptr->pulse_link[i].prev_ptr ;
                pulse_list[i].tail_ptr->pulse_link[i].next_ptr = NULL ;
            }
            else
            {
                dlog ("Pulse: Multiple Node -> Full Splice Out\n");
                pulse_ptr->pulse_link[i].prev_ptr->pulse_link[i].next_ptr = pulse_ptr->pulse_link[i].next_ptr ;
                pulse_ptr->pulse_link[i].next_ptr->pulse_link[i].prev_ptr = pulse_ptr->pulse_link[i].prev_ptr ;
            }
        }
    }

#endif

    /* If the node is the head node */
    if ( ptr == head )
    {
        /* only one node in the list case */
        if ( head == tail )
        {
            dlog ("Single Node -> Head Case\n");
            head = NULL ;
            tail = NULL ;
            delNode ( ptr );
            rc = PASS ;
        }
        else
        {
            dlog ("Multiple Nodes -> Head Case\n");
            head = head->next ;
            head->prev = NULL ; 
            delNode ( ptr );
            rc = PASS ;
        }
    }
    /* if not head but tail then there must be more than one
     * node in the list so go ahead and chop the tail.
     */
    else if ( ptr == tail )
    {
        dlog ("Multiple Node -> Tail Case\n");
        tail = tail->prev ;
        tail->next = NULL ;
        delNode ( ptr );
        rc = PASS ;
    }
    else
    {
        dlog ("Multiple Node -> Full Splice Out\n");
        ptr->prev->next = ptr->next ;
        ptr->next->prev = ptr->prev ;
        delNode( ptr );
        rc = PASS ;
    }
    hosts-- ;

    /* (re)build the Resource Reference Array */
    if ( heartbeat )
        build_rra ();

    return rc ;
}

/**
 *   Node state set'ers and get'ers
 */
mtc_nodeAdminAction_enum nodeLinkClass::get_adminAction ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
       return ( node_ptr->adminAction );

    elog ("Failed getting 'admin action' for '%s'\n", hostname.c_str());
    return (MTC_ADMIN_ACTION__NONE);
}

int nodeLinkClass::set_adminAction ( string & hostname, mtc_nodeAdminAction_enum adminAction )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
    {
        adminActionChange ( node_ptr, adminAction ) ;
        return (PASS) ;
    }
    elog ("Failed setting 'admin action' for '%s'\n", hostname.c_str());
    return (FAIL) ;
}

mtc_nodeAdminState_enum nodeLinkClass::get_adminState ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
       return ( node_ptr->adminState );

    elog ("Failed getting 'admin state' state for '%s'\n", hostname.c_str());
    return (MTC_ADMIN_STATE__LOCKED);
}

int nodeLinkClass::set_adminState ( string & hostname, mtc_nodeAdminState_enum adminState )
{
    int rc = FAIL ;
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
    {
        rc = nodeLinkClass::adminStateChange ( node_ptr , adminState );
    }
    if ( rc )
    {
        elog ("Failed setting 'admin state' for '%s'\n", hostname.c_str());
    }
    return (rc) ;
}

mtc_nodeOperState_enum nodeLinkClass::get_operState ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
       return ( node_ptr->operState );

    elog ("Failed getting 'operational state' for '%s'\n", hostname.c_str());
    return (MTC_OPER_STATE__DISABLED);
}

int nodeLinkClass::set_operState ( string & hostname, mtc_nodeOperState_enum operState )
{
    int rc = FAIL ;
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
    {
        rc = nodeLinkClass::operStateChange ( node_ptr , operState );
    }
    if ( rc )
    {
        elog ("Failed setting 'operational state' for '%s'\n", hostname.c_str());
    }
    return (rc) ;
}

mtc_nodeAvailStatus_enum nodeLinkClass::get_availStatus ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
       return ( node_ptr->availStatus );

    elog ("Failed getting 'availability status' for '%s'\n", hostname.c_str());
    return (MTC_AVAIL_STATUS__OFFDUTY);
}

int nodeLinkClass::set_availStatus ( string & hostname, mtc_nodeAvailStatus_enum availStatus )
{
    int rc = FAIL ;
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr != NULL )
    {
        rc = nodeLinkClass::availStatusChange ( node_ptr , availStatus );
    }
    if ( rc )
    {
        elog ("Failed setting 'availability status' for '%s'\n", hostname.c_str());
    }
    return (FAIL) ;
}

/** Return a string representing the data port operational state
 *  according to the X.731 standard */
string nodeLinkClass::get_operState_dport ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
    {
       return ( operState_enum_to_str(node_ptr->operState_dport));
    }
    elog ("%s failed getting 'operState_dport'\n", hostname.c_str());
    return ("");
}

/** Return a string representing the data port availability status
 *  according to the X.731 standard */
string nodeLinkClass::get_availStatus_dport ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr )
    {
       return ( availStatus_enum_to_str(node_ptr->availStatus_dport));
    }
    elog ("%s failed getting 'availStatus_dport'\n", hostname.c_str());
    return ("");
}

void nodeLinkClass::print_node_info ( nodeLinkClass::node * ptr )
{
    ilog ("%17s (%15s) %8s %8s-%-9s | %s-%s-%s | %s | %0X",
                ptr->hostname.c_str(),
                ptr->ip.c_str(),
                mtc_nodeAdminState_str[ptr->adminState],
                mtc_nodeOperState_str[ptr->operState],
                mtc_nodeAvailStatus_str[ptr->availStatus],
                ptr->subfunction_str.c_str(),
                mtc_nodeOperState_str[ptr->operState_subf],
                mtc_nodeAvailStatus_str[ptr->availStatus_subf],
                mtc_nodeAdminAction_str [ptr->adminAction],
                ptr->degrade_mask);
}

void nodeLinkClass::print_node_info ( void )
{
    if ( maintenance )
    {
        syslog ( LOG_INFO,"+--------------------------------------+-------------------+-----------------+\n");
        for ( struct node * ptr = head ; ptr != NULL ; ptr = ptr->next  )
        {
            if ( LARGE_SYSTEM )
            {
                syslog ( LOG_INFO, "| %36s | %17s | %15s | %8s %8s-%s",
                     ptr->uuid.length() ? ptr->uuid.c_str() : "",
                     ptr->hostname.c_str(),
                     ptr->ip.c_str(),
                     mtc_nodeAdminState_str[ptr->adminState],
                     mtc_nodeOperState_str[ptr->operState],
                     mtc_nodeAvailStatus_str[ptr->availStatus]);
            }
            else
            {
                syslog ( LOG_INFO, "| %36s | %17s | %15s | %8s %8s-%-9s | %s-%s-%s",
                     ptr->uuid.length() ? ptr->uuid.c_str() : "",
                     ptr->hostname.c_str(),
                     ptr->ip.c_str(),
                     mtc_nodeAdminState_str[ptr->adminState],
                     mtc_nodeOperState_str[ptr->operState],
                     mtc_nodeAvailStatus_str[ptr->availStatus],
                     ptr->subfunction_str.c_str(),
                     mtc_nodeOperState_str[ptr->operState_subf],
                     mtc_nodeAvailStatus_str[ptr->availStatus_subf]);
            }
            // syslog ( LOG_INFO, "\n");
        }
        syslog ( LOG_INFO, "+--------------------------------------+-------------------+-----------------+\n\n");
    }

    if ( heartbeat )
    {
        for ( int i = 0 ; i < MAX_IFACES ; i++ )
        {
            if (( i == INFRA_IFACE ) && ( infra_network_provisioned == false ))
                continue ;

            syslog ( LOG_INFO, "+--------------+-----+-------+-------+-------+-------+------------+----------+-----------------+\n");
            syslog ( LOG_INFO, "| %s:  %3d  | Mon |  Mis  |  Max  |  Deg  | Fail  | Pulses Tot |  Pulses  | %s (%4d) |\n" ,
                       get_iface_name_str ((iface_enum)i), hosts, hbs_disabled ? "DISABLED" : "Enabled ", hbs_pulse_period );
            syslog ( LOG_INFO, "+--------------+-----+-------+-------+-------+-------+------------+----------+-----------------+\n");

            for ( struct node * ptr = head ; ptr != NULL ; ptr = ptr->next  )
            {
                syslog ( LOG_INFO, "| %-12s |  %c  | %5i | %5i | %5i | %5i | %10x | %8x | %d msec\n",
                    ptr->hostname.c_str(),
                    ptr->monitor[i] ? 'Y' : 'n',
                    ptr->hbs_misses_count[i],
                    ptr->max_count[i],
                    ptr->hbs_degrade_count[i],
                    ptr->hbs_failure_count[i],
                    ptr->hbs_count[i],
                    ptr->b2b_pulses_count[i],
                    hbs_pulse_period );
            }
        }
        syslog ( LOG_INFO, "+--------------+-----+-------+-------+-------+-------+------------+----------+-----------------+\n");
    }
}

/** Convert the supplied string to a valid maintenance Admin State enum  */
mtc_nodeAdminState_enum  nodeLinkClass::adminState_str_to_enum ( const char * admin_ptr )
{
    /* Default state */
    mtc_nodeAdminState_enum temp = MTC_ADMIN_STATE__LOCKED;
    
    if ( admin_ptr == NULL )
    {
       wlog ("Administrative state is Null\n"); 
    }
    else if ( !strcmp ( &mtc_nodeAdminState_str[MTC_ADMIN_STATE__UNLOCKED][0], admin_ptr ))
       temp = MTC_ADMIN_STATE__UNLOCKED ;

    return (temp) ;
}

/** Convert the supplied string to a valid maintenance Oper State enum   */
mtc_nodeOperState_enum   nodeLinkClass::operState_str_to_enum  ( const char * oper_ptr )
{
    /* Default state */
    mtc_nodeOperState_enum temp = MTC_OPER_STATE__DISABLED;
    
    if ( oper_ptr == NULL )
    {
       wlog ("Operation state is Null\n"); 
    }
    else if ( !strcmp ( &mtc_nodeOperState_str[MTC_ADMIN_STATE__UNLOCKED][0], oper_ptr ))
       temp = MTC_OPER_STATE__ENABLED ;

    return (temp) ;
}

/** Convert the supplied string to a valid maintenance Avail Status enum */
mtc_nodeAvailStatus_enum nodeLinkClass::availStatus_str_to_enum ( const char * avail_ptr )
{
    /* Default state */
    mtc_nodeAvailStatus_enum temp = MTC_AVAIL_STATUS__OFFDUTY;
    
    /* Could do this as a loop but this is more resiliant to enum changes */
    /* TODO: consider using a paired list */
    if ( avail_ptr == NULL )
    {
       wlog ("Availability status is Null\n"); 
    }
    else if ( !strcmp ( &mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS__AVAILABLE][0], avail_ptr ))
       temp = MTC_AVAIL_STATUS__AVAILABLE ;
    else if ( !strcmp ( &mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS__FAILED][0], avail_ptr ))
       temp = MTC_AVAIL_STATUS__FAILED ;
    else if ( !strcmp ( &mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS__INTEST][0], avail_ptr ))
       temp = MTC_AVAIL_STATUS__INTEST ;
    else if ( !strcmp ( &mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS__DEGRADED][0], avail_ptr ))
       temp = MTC_AVAIL_STATUS__DEGRADED ;
    else if ( !strcmp ( &mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS__OFFLINE][0], avail_ptr ))
       temp = MTC_AVAIL_STATUS__OFFLINE ;
    else if ( !strcmp ( &mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS__ONLINE][0], avail_ptr ))
       temp = MTC_AVAIL_STATUS__ONLINE ;
    else if ( !strcmp ( &mtc_nodeAvailStatus_str[MTC_AVAIL_STATUS__POWERED_OFF][0], avail_ptr ))
       temp = MTC_AVAIL_STATUS__POWERED_OFF ;

    return (temp) ;
}

/** Convert the supplied enum to the corresponding Admin State string  */
string nodeLinkClass::adminAction_enum_to_str ( mtc_nodeAdminAction_enum val )
{
   if ( val < MTC_ADMIN_ACTIONS )
   {
      string adminAction_string = &mtc_nodeAdminAction_str[val][0] ;
      return ( adminAction_string );
   }
   return ( NULL );
}

/** Convert the supplied enum to the corresponding Admin State string  */
string nodeLinkClass::adminState_enum_to_str ( mtc_nodeAdminState_enum val )
{
   if ( val < MTC_ADMIN_STATES )
   {
      string adminState_string = &mtc_nodeAdminState_str[val][0] ;
      return ( adminState_string );
   }
   return ( NULL );
}
/** Convert the supplied enum to the corresponding Oper State string   */
string nodeLinkClass::operState_enum_to_str  ( mtc_nodeOperState_enum val )
{
   if ( val < MTC_OPER_STATES )
   {
      string operState_string = &mtc_nodeOperState_str[val][0] ;
      return ( operState_string );
   }
   return ( NULL );
}
/** Convert the supplied enum to the corresponding Avail Status string */
string nodeLinkClass::availStatus_enum_to_str ( mtc_nodeAvailStatus_enum val )
{
   if ( val < MTC_AVAIL_STATUS )
   {
      string availStatus_string = &mtc_nodeAvailStatus_str[val][0] ;
      return ( availStatus_string );
   }
   return ( NULL );
}

void nodeLinkClass::host_print (  struct nodeLinkClass::node * node_ptr )
{
    string uuid ;

    if ( daemon_get_cfg_ptr()->debug_level == 1 )
    {
        const char bar [] = { "+-------------+--------------------------------------+" };
        const char uar [] = { "+- Add  Host -+--------------------------------------+" };
        syslog ( LOG_INFO, "%s\n", &uar[0]);
        syslog ( LOG_INFO, "| uuid      : %s\n", node_ptr->uuid.c_str());
        syslog ( LOG_INFO, "| main      : %s\n", node_ptr->function_str.c_str());
        syslog ( LOG_INFO, "| subf      : %s\n", node_ptr->subfunction_str.c_str());
        syslog ( LOG_INFO, "| name      : %s\n", node_ptr->hostname.c_str());
        syslog ( LOG_INFO, "| ip        : %s\n", node_ptr->ip.c_str());
        syslog ( LOG_INFO, "| admin     : %s\n", adminState_enum_to_str (node_ptr->adminState).c_str());
        syslog ( LOG_INFO, "| oper      : %s\n", operState_enum_to_str  (node_ptr->operState).c_str());
        syslog ( LOG_INFO, "| avail_subf: %s\n", availStatus_enum_to_str (node_ptr->availStatus_subf).c_str());
        syslog ( LOG_INFO, "|  oper_subf: %s\n", operState_enum_to_str (node_ptr->operState_subf).c_str());
        syslog ( LOG_INFO, "%s\n", &bar[0]);
    }
    /* ec3624cb-d80f-4e8b-a6d7-0c11f6937f6a */
    /* Just print the last 4 chars of the uuid */
    if ( node_ptr->uuid.empty() )
        uuid = "---" ;
    else
        uuid = node_ptr->uuid.substr(32) ;

    if ( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE )
    {
        ilog ("%s '%s' %s %s-%s (%s)\n", node_ptr->hostname.c_str(),
                                         functions.c_str(),
                                         node_ptr->ip.c_str(),
                                         adminState_enum_to_str (node_ptr->adminState).c_str(),
                                         operState_enum_to_str  (node_ptr->operState).c_str(),
                                         uuid.c_str());
    }
    else
    {
        ilog ("%s '%s' %s %s-%s-%s (%s)\n",node_ptr->hostname.c_str(),
                                         functions.c_str(),
                                         node_ptr->ip.c_str(),
                                         adminState_enum_to_str (node_ptr->adminState).c_str(),
                                         operState_enum_to_str  (node_ptr->operState).c_str(),
                                         availStatus_enum_to_str(node_ptr->availStatus).c_str(),
                                         uuid.c_str());
    }
}


/** Host Administrative State Change public member function */
int nodeLinkClass::admin_state_change ( string hostname,
                         string newAdminState )
{
    int rc = FAIL ;
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( hostname );
    if ( node_ptr )
    {
        if ( newAdminState.empty() )
        {
            rc = FAIL_STRING_EMPTY ;
        }
        else if (  adminState_str_to_enum ( newAdminState.data() ) == node_ptr->adminState )
        {
            rc = PASS ;
        }
        else if ( adminStateOk ( newAdminState ) )
        {
            clog ("%s %s (from %s)\n", hostname.c_str(), newAdminState.c_str(), adminState_enum_to_str (node_ptr->adminState).c_str());
            node_ptr->adminState = adminState_str_to_enum ( newAdminState.data() );
            rc = PASS ;
        }
        else
        {
            elog ("%s Invalid 'admin' state (%s)\n",
                      hostname.c_str(), newAdminState.c_str() );
        }
    }
    else
    {
        wlog ("Cannot change 'admin' state for unknown hostname (%s)\n",
               hostname.c_str());
    }
    return (rc);
}

/** Host Operational State Change public member function */
int nodeLinkClass::oper_state_change ( string hostname, string newOperState )
{
    int rc = FAIL ;
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( hostname );
    if ( node_ptr )
    {
        if ( newOperState.empty() )
        {
            rc = FAIL_STRING_EMPTY ;
        }
        else if (  operState_str_to_enum ( newOperState.data() ) == node_ptr->operState )
        {
            rc = PASS ;
        }
        else if ( operStateOk ( newOperState ) )
        {
            mtc_nodeOperState_enum oper = operState_str_to_enum ( newOperState.data() );
            if (( node_ptr->operState == MTC_OPER_STATE__DISABLED ) &&
                (           oper      == MTC_OPER_STATE__ENABLED ))
            {
                mtcAlarm_log ( hostname, MTC_LOG_ID__STATUSCHANGE_ENABLED );
            }

            if (( node_ptr->operState == MTC_OPER_STATE__ENABLED ) &&
                (           oper      == MTC_OPER_STATE__DISABLED ))
            {
                mtcAlarm_log ( hostname, MTC_LOG_ID__STATUSCHANGE_DISABLED );
            }
            clog ("%s %s (from %s)\n", hostname.c_str(), newOperState.c_str(), operState_enum_to_str (node_ptr->operState).c_str());
            node_ptr->operState = oper ;
            rc = PASS ;
        }
        else
        {
            elog ("%s Invalid 'oper' state (%s)\n",
                      hostname.c_str(), newOperState.c_str() );
        }
    }
    else
    {
        wlog ("Cannot change 'oper' state for unknown hostname (%s)\n",
                  hostname.c_str() );
    }
    return (rc);
}

/** Host Availability Status Change public member function */
int nodeLinkClass::avail_status_change ( string hostname,
                                         string newAvailStatus )
{
    int rc = FAIL ;
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( hostname );
    if ( node_ptr )
    {
        if ( newAvailStatus.empty() )
        {
            rc = FAIL_STRING_EMPTY ;
        }
        else if (  availStatus_str_to_enum ( newAvailStatus.data() ) == node_ptr->availStatus )
        {
            rc = PASS ;
        }
        else if ( availStatusOk ( newAvailStatus ))
        {
            mtc_nodeAvailStatus_enum avail = availStatus_str_to_enum ( newAvailStatus.data() );

            /* if we go to the failed state then clear all mtcAlive counts
             * so that the last ones don't look like we are online when we
             * might not be - we should relearn the on/off line state */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__FAILED ) &&
                (           avail        == MTC_AVAIL_STATUS__FAILED ))
            {
                node_ptr->mtcAlive_misses = 0 ;
                node_ptr->mtcAlive_hits   = 0 ;
                node_ptr->mtcAlive_gate   = false ;
            }

            /* check for need to generate power on log */
            if (( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) &&
                (           avail       != MTC_AVAIL_STATUS__POWERED_OFF ))
            {
                if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWERON )
                {
                    mtcAlarm_log ( hostname, MTC_LOG_ID__COMMAND_MANUAL_POWER_ON );
                }
                else
                {
                    mtcAlarm_log ( hostname, MTC_LOG_ID__COMMAND_AUTO_POWER_ON );
                }
            }

            /* check for need to generate power off log */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__POWERED_OFF ) &&
                (           avail       == MTC_AVAIL_STATUS__POWERED_OFF ))
            {
                if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWEROFF )
                {
                    mtcAlarm_log ( hostname, MTC_LOG_ID__COMMAND_MANUAL_POWER_OFF );
                }
                else
                {
                    mtcAlarm_log ( hostname, MTC_LOG_ID__COMMAND_AUTO_POWER_OFF );
                }
            }

            /* check for need to generate online log */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__ONLINE ) &&
                (           avail       == MTC_AVAIL_STATUS__ONLINE ))
            {
                if ( node_ptr->offline_log_reported == true )
                {
                    mtcAlarm_log ( hostname, MTC_LOG_ID__STATUSCHANGE_ONLINE );
                    node_ptr->offline_log_reported = false ;
                    node_ptr->online_log_reported = true ;
                }
            }

            /* check for need to generate offline log */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__OFFLINE ) &&
                (           avail       == MTC_AVAIL_STATUS__OFFLINE ))
            {
                if ( node_ptr->online_log_reported == true )
                {
                    mtcAlarm_log ( hostname, MTC_LOG_ID__STATUSCHANGE_OFFLINE );
                    node_ptr->offline_log_reported = true  ;
                    node_ptr->online_log_reported  = false ;
                }
            }

            /* If the availability status is moving away from off or online then
             * be sure we cancel the mtcAlive timer */
            if ((( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE ) ||
                (  node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE )) &&
                ((           avail       != MTC_AVAIL_STATUS__OFFLINE ) &&
                 (           avail       != MTC_AVAIL_STATUS__ONLINE )))
            {
                /* Free the mtc timer if in use */
                if ( node_ptr->mtcAlive_timer.tid )
                {
                    tlog ("%s Stopping mtcAlive timer\n", node_ptr->hostname.c_str());
                    mtcTimer_stop ( node_ptr->mtcAlive_timer );
                    node_ptr->mtcAlive_timer.ring = false ;
                    node_ptr->mtcAlive_timer.tid  = NULL  ;
                }
                node_ptr->onlineStage = MTC_ONLINE__START ;
            }

            clog ("%s %s (from %s)\n", hostname.c_str(), newAvailStatus.c_str(), availStatus_enum_to_str (node_ptr->availStatus).c_str());
            node_ptr->availStatus = avail ;
            rc = PASS ;
        }
        else
        {
            elog ("%s Invalid 'avail' status (%s)\n",
                      hostname.c_str(), newAvailStatus.c_str() );
        }
    }
    else
    {
        wlog ("Cannot change 'avail' status for unknown hostname (%s)\n",
               hostname.c_str());
    }
    return (rc);
}

/** Set host to the disabled failed state and generate the disabled-failed customer log
 *  This interface allows the disabled-failed state change to be combined so as to avoid
 *  setting a 'disabled' AND 'disabled-failed' customer log for the failure case */
int nodeLinkClass::failed_state_change ( struct nodeLinkClass::node * node_ptr )
{
    int rc = FAIL_NULL_POINTER ;
    if ( node_ptr )
    {
        node_ptr->availStatus = MTC_AVAIL_STATUS__FAILED ;
        node_ptr->operState   = MTC_OPER_STATE__DISABLED ;
        mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_FAILED );
        rc = PASS ;
    }
    else
    {
        slog ("Cannot change to disabled-failed state for null node pointer\n");
    }
    return (rc);
}


/*****************************************************************************
 *
 * Name       : lazy_graceful_fs_reboot
 *
 * Description: Issue a lazy reboot and signal SM to shutdown services
 *
 * Assumptions: No return
 *
 *****************************************************************************/

int nodeLinkClass::lazy_graceful_fs_reboot ( struct nodeLinkClass::node * node_ptr )
{
    /* issue a lazy reboot to the mtcClient and as a backup launch a sysreq reset thresd */
    send_mtc_cmd ( node_ptr->hostname, MTC_CMD_LAZY_REBOOT, MGMNT_INTERFACE ) ;
    fork_sysreq_reboot ( daemon_get_cfg_ptr()->failsafe_shutdown_delay );

    /* loop until reboot */
    for ( ; ; )
    {
        for ( int i = 0 ; i < LAZY_REBOOT_RETRY_DELAY_SECS ; i++ )
        {
            daemon_signal_hdlr ();
            sleep (MTC_SECS_1);

            /* give sysinv time to handle the response and get its state in order */
            if ( i == SM_NOTIFY_UNHEALTHY_DELAY_SECS )
            {
                daemon_log ( SMGMT_UNHEALTHY_FILE, "AIO shutdown request" );
            }
        }
        /* Should never get there but if we do resend the reboot request
         * but this time not Lazy */
        send_mtc_cmd ( node_ptr->hostname, MTC_CMD_REBOOT, MGMNT_INTERFACE ) ;
    }
    return (FAIL);
}


/* Generate a log and a critical alarm if the node config failed */
int nodeLinkClass::alarm_config_failure (  struct nodeLinkClass::node * node_ptr )
{
    if ( (node_ptr->degrade_mask & DEGRADE_MASK_CONFIG) == 0 )
    {
        node_ptr->degrade_mask |= DEGRADE_MASK_CONFIG ;
    }

    if ( node_ptr->alarms[MTC_ALARM_ID__CONFIG] != FM_ALARM_SEVERITY_CRITICAL )
    {
        elog ("%s critical config failure\n", node_ptr->hostname.c_str());

        mtcAlarm_critical ( node_ptr->hostname, MTC_ALARM_ID__CONFIG );
        node_ptr->alarms[MTC_ALARM_ID__CONFIG] = FM_ALARM_SEVERITY_CRITICAL ;
    }
    return (PASS);
}

/* Clear the config alarm and degrade flag */
int nodeLinkClass::alarm_config_clear ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->degrade_mask & DEGRADE_MASK_CONFIG )
    {
        node_ptr->degrade_mask &= ~DEGRADE_MASK_CONFIG ;
    }

    if ( node_ptr->alarms[MTC_ALARM_ID__CONFIG] != FM_ALARM_SEVERITY_CLEAR )
    {
        ilog ("%s config alarm clear\n", node_ptr->hostname.c_str());

        mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__CONFIG );
        node_ptr->alarms[MTC_ALARM_ID__CONFIG] = FM_ALARM_SEVERITY_CLEAR ;
    }
    return (PASS);
}

/* Generate a log and a critical alarm if the node enable failed */
int nodeLinkClass::alarm_enabled_failure ( struct nodeLinkClass::node * node_ptr, bool want_degrade )
{
    if ( want_degrade )
    {
        if ( (node_ptr->degrade_mask & DEGRADE_MASK_ENABLE) == 0 )
        {
            node_ptr->degrade_mask |= DEGRADE_MASK_ENABLE ;
        }
    }
    if ( node_ptr->alarms[MTC_ALARM_ID__ENABLE] != FM_ALARM_SEVERITY_CRITICAL )
    {
        elog ("%s critical enable failure\n", node_ptr->hostname.c_str());

        mtcAlarm_critical ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
        node_ptr->alarms[MTC_ALARM_ID__ENABLE] = FM_ALARM_SEVERITY_CRITICAL ;
    }
    return (PASS);
}

/*
 * Generate a major (in-service) enable alarm
 * - don't downgrade the alarm from critical
 * - do nothing if the alarm is already at major level
 *
 **/
int nodeLinkClass::alarm_insv_failure ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr )
    {
        if ( node_ptr->alarms[MTC_ALARM_ID__ENABLE] != FM_ALARM_SEVERITY_MAJOR )
        {
            if ( node_ptr->alarms[MTC_ALARM_ID__ENABLE] != FM_ALARM_SEVERITY_CRITICAL )
            {
                elog ("%s major inservice enable failure\n", node_ptr->hostname.c_str());
                node_ptr->degrade_mask |= DEGRADE_MASK_INSV_TEST ;
                mtcAlarm_major ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
                node_ptr->alarms[MTC_ALARM_ID__ENABLE] = FM_ALARM_SEVERITY_MAJOR ;
            }
        }
    }
    return (PASS);
}

/* Clear the enable alarm and degrade flag */
int nodeLinkClass::alarm_enabled_clear ( struct nodeLinkClass::node * node_ptr, bool force )
{
    if ( node_ptr->degrade_mask & DEGRADE_MASK_ENABLE )
    {
        node_ptr->degrade_mask &= ~DEGRADE_MASK_ENABLE ;
    }

    /* The inservice test degrade flag needs to be cleared too. */
    if ( node_ptr->degrade_mask & DEGRADE_MASK_INSV_TEST )
    {
        node_ptr->degrade_mask &= ~DEGRADE_MASK_INSV_TEST ;
    }

    if (( node_ptr->alarms[MTC_ALARM_ID__ENABLE] != FM_ALARM_SEVERITY_CLEAR ) ||
        ( force == true ))
    {
        ilog ("%s enable alarm clear\n", node_ptr->hostname.c_str());

        mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
        node_ptr->alarms[MTC_ALARM_ID__ENABLE] = FM_ALARM_SEVERITY_CLEAR ;
    }
    return (PASS);
}

/* Generate compute subfunction failure alarm */
int nodeLinkClass::alarm_compute_failure ( struct nodeLinkClass::node * node_ptr, EFmAlarmSeverityT sev )
{
    if ( (node_ptr->degrade_mask & DEGRADE_MASK_SUBF) == 0 )
    {
        node_ptr->degrade_mask |= DEGRADE_MASK_SUBF ;
    }

    if ( node_ptr->alarms[MTC_ALARM_ID__CH_COMP] != sev )
    {
        if ( sev == FM_ALARM_SEVERITY_CRITICAL )
        {
            elog ("%s critical compute subf failure\n", node_ptr->hostname.c_str());
            mtcAlarm_critical ( node_ptr->hostname, MTC_ALARM_ID__CH_COMP );
        }
        else
        {
            elog ("%s major compute subf failure\n", node_ptr->hostname.c_str());
            mtcAlarm_major ( node_ptr->hostname, MTC_ALARM_ID__CH_COMP );
        }
        node_ptr->alarms[MTC_ALARM_ID__CH_COMP] = sev ;
    }
    return (PASS);
}

/* Clear the enable alarm if is at the Major severity level */
int nodeLinkClass::alarm_insv_clear ( struct nodeLinkClass::node * node_ptr, bool force )
{
    if ( node_ptr->degrade_mask & DEGRADE_MASK_INSV_TEST )
    {
        node_ptr->degrade_mask &= ~DEGRADE_MASK_INSV_TEST ;
    }

    if (( node_ptr->alarms[MTC_ALARM_ID__ENABLE] == FM_ALARM_SEVERITY_MAJOR ) ||
        ( force == true ))
    {
        ilog ("%s %s enable alarm clear\n", node_ptr->hostname.c_str(), force ? "force" : "major" );

        mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__ENABLE );
        node_ptr->alarms[MTC_ALARM_ID__ENABLE] = FM_ALARM_SEVERITY_CLEAR ;
    }

    if (  node_ptr->alarms[MTC_ALARM_ID__ENABLE] == FM_ALARM_SEVERITY_CLEAR )
    {
        if ( node_ptr->degrade_mask & DEGRADE_MASK_ENABLE )
        {
            node_ptr->degrade_mask &= ~DEGRADE_MASK_ENABLE ;
        }
    }

    return (PASS);
}

/* Clear the compute subfunction alarm and degrade flag */
int nodeLinkClass::alarm_compute_clear ( struct nodeLinkClass::node * node_ptr, bool force )
{
    if ( node_ptr->degrade_mask & DEGRADE_MASK_SUBF )
    {
        node_ptr->degrade_mask &= ~DEGRADE_MASK_SUBF ;
    }

    if (( node_ptr->alarms[MTC_ALARM_ID__CH_COMP] != FM_ALARM_SEVERITY_CLEAR ) ||
        ( force == true ))
    {
        ilog ("%s major enable alarm clear\n", node_ptr->hostname.c_str());

        mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__CH_COMP );
        node_ptr->alarms[MTC_ALARM_ID__CH_COMP] = FM_ALARM_SEVERITY_CLEAR ;
    }
    return (PASS);
}

/** Host Operational State Change public member function */
int nodeLinkClass::oper_subf_state_change ( string hostname, string newOperState )
{
    int rc = FAIL ;
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( hostname );
    if ( node_ptr )
    {
        if ( newOperState.empty() )
        {
            rc = FAIL_STRING_EMPTY ;
        }
        else if ( operStateOk ( newOperState ) )
        {
            node_ptr->operState_subf = operState_str_to_enum ( newOperState.data() );
            rc = PASS ;
        }
        else
        {
            elog ("%s Invalid subfunction 'oper' state (%s)\n",
                      hostname.c_str(), newOperState.c_str() );
        }
    }
    else
    {
        wlog ("Cannot change subfuction 'oper' state for unknown hostname (%s)\n",
                  hostname.c_str() );
    }
    return (rc);
}

/** Host Subfunction Availability Status Change public member function */
int nodeLinkClass::avail_subf_status_change ( string hostname, string newAvailStatus )
{
    int rc = FAIL ;
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( hostname );
    if ( node_ptr )
    {
        if ( newAvailStatus.empty() )
        {
            rc = FAIL_STRING_EMPTY ;
        }
        else if ( availStatusOk ( newAvailStatus ) )
        {
            node_ptr->availStatus_subf = availStatus_str_to_enum ( newAvailStatus.data() );
            rc = PASS ;
        }
        else
        {
            elog ("%s Invalid subfunction 'avail' status (%s)\n",
                      hostname.c_str(), newAvailStatus.c_str() );
        }
    }
    else
    {
        wlog ("Cannot change subfunction 'avail' status for unknown hostname (%s)\n",
               hostname.c_str());
    }
    return (rc);
}



/** Update the mtce key with value */
int nodeLinkClass::update_key_value ( string hostname, string key , string value )
{
    int rc = PASS ;
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( hostname );
    if ( node_ptr ) 
    {
            /* TODO: Add all database members to this utility */
            if ( !key.compare(MTC_JSON_INV_BMIP) )
                node_ptr->bm_ip = value ;
            else if ( !key.compare(MTC_JSON_INV_TASK) )
                node_ptr->task = value ;
            else
            {
                wlog ("%s Unsupported key '%s' update with value '%s'\n", 
                          hostname.c_str(), key.c_str(), value.c_str());
                rc = FAIL_BAD_PARM ; 
            }
    }
    else
    {
        wlog ("Cannot change 'admin' state for unknown hostname (%s)\n", 
               hostname.c_str()); 
    }
    return (rc);
}

int nodeLinkClass::del_host ( const string uuid )
{
    string hostname = "unknown" ;

    int rc = FAIL_DEL_UNKNOWN ;
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( uuid );
    if ( node_ptr )
    {
        hostname = node_ptr->hostname ;

        if ( node_ptr->mtcTimer.tid )
            mtcTimer_stop ( node_ptr->mtcTimer );

        if ( nodeLinkClass::maintenance == true )
        {

            if ( node_ptr->bm_provisioned == true )
            {
                set_bm_prov ( node_ptr, false);
            }

            doneQueue_purge    ( node_ptr );
            workQueue_purge    ( node_ptr );
            mtcCmd_doneQ_purge ( node_ptr );
            mtcCmd_workQ_purge ( node_ptr );

            /* Cleanup if this is the inactive controller */
            if ( !node_ptr->hostname.compare(inactive_controller_hostname))
            {
                inactive_controller_hostname = "" ;
            }
        }
        rc = rem_host ( hostname );
        if ( rc == PASS )
        {
            plog ("%s Deleted\n", hostname.c_str());

            print_node_info();
        }
        else
        {
            elog ("%s Delete Failed (rc:%d)\n", hostname.c_str(), rc );
        }
        this->host_deleted = true ;
    }
    else
    {
        wlog ("Unknown uuid: %s\n", uuid.c_str());
    }
    return (rc);
}

int nodeLinkClass::set_host_failed ( node_inv_type & inv )
{
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( inv.name );
    if(NULL == node_ptr)
    {
        return FAIL_UNKNOWN_HOSTNAME;
    }

    if( (node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED) &&
        (node_ptr->operState == MTC_OPER_STATE__ENABLED) )
    {
        elog( "%s is being force failed by SM", inv.name.c_str() );
        this->force_full_enable (node_ptr);
    }
    return PASS;
}

int nodeLinkClass::mod_host ( node_inv_type & inv )
{
    int rc = PASS ;
    bool modify    = false ;
    bool modify_bm = false ;

    print_inv (inv);

    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode( inv.uuid );
    if ( node_ptr )
    {
        dlog ("%s Modify\n", node_ptr->hostname.c_str());

#ifdef WANT_FIT_TESTING
       if ( daemon_want_fit ( FIT_CODE__TRANSLATE_LOCK_TO_FORCELOCK , node_ptr->hostname ) )
       {
           if ( inv.action.compare("lock") == 0 )
           {
               slog ("%s FIT action from 'lock' to 'force-lock'\n", node_ptr->hostname.c_str());
               inv.action = "force-lock";
           }
       }
#endif

        /* Handle Administrative state mismatch between SYSINV and Maintenance */
        if ( strcmp ( mtc_nodeAdminState_str[node_ptr->adminState], inv.admin.data()))
        {
            plog ("%s Modify 'Administrative' state %s -> %sed\n", node_ptr->hostname.c_str(),
                         mtc_nodeAdminState_str[node_ptr->adminState], inv.action.c_str());

            modify = true ; /* we have a delta */

            /* Local admin state takes precedence */
            if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
            {
                /* handle a lock request while unlocked */
                if ( !inv.action.compare ( "lock" ) )
                {
                    if ( node_ptr->dor_recovery_mode == true )
                         node_ptr->dor_recovery_mode = false ;

                    /* Set action to LOCK and let the FSM run the disable handler */
                    adminActionChange ( node_ptr , MTC_ADMIN_ACTION__LOCK );
                }
                else
                {
                    ilog ("%s Already Unlocked ; no action required\n", node_ptr->hostname.c_str() );
                }
            }
            else
            {
                if ( node_ptr->patching == true )
                {
                    wlog ("%s cannot unlock host while patching is in progress\n", node_ptr->hostname.c_str());
                    rc = FAIL_PATCH_INPROGRESS ;
                }
                else
                {
                    /* generate command=unlock log */
                    mtcAlarm_log ( inv.name, MTC_LOG_ID__COMMAND_UNLOCK );

                    /* Set action to UNLOCK and let the FSM run the enable handler */
                    adminActionChange ( node_ptr , MTC_ADMIN_ACTION__UNLOCK );
                }
            }
        }
        else if ( (!inv.action.empty()) && (inv.action.compare ( "none" )))
        {
            dlog ("%s Modify Action is '%s'\n", node_ptr->hostname.c_str(), inv.action.c_str() );
            node_ptr->action = inv.action ;
            modify      = true ; /* we have a delta */

            /* Do not permit administrative actions while Swact is in progress */
            /* Note: There is a self corrective clause in the mtcTimer_handler
             * that will auto clear this flag if it gets stuck for 5 minutes */
            if ( smgrEvent.mutex )
            {
                elog ("%s Rejecting '%s' - Swact Operation in-progress\n", 
                          node_ptr->hostname.c_str(), inv.action.c_str());
                rc = FAIL_SWACT_INPROGRESS ;
            }
 
            else if (!inv.action.compare ( "force-lock" ))
            {
                /* TODO: Create customer log of this action */
                ilog ("%s Force Lock Action\n", node_ptr->hostname.c_str());

                if ( node_ptr->dor_recovery_mode == true )
                     node_ptr->dor_recovery_mode = false ;

                if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
                {
                    if ( node_ptr->adminAction == MTC_ADMIN_ACTION__FORCE_LOCK )
                    {
                        ilog ("%s Force Lock Action - already in progress ...\n", node_ptr->hostname.c_str());
                    }
                    else
                    {
                        /* generate command=forcelock log */
                        mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_FORCE_LOCK );

                        /* Set action to LOCK and let the FSM run the disable handler */
                        adminActionChange ( node_ptr , MTC_ADMIN_ACTION__FORCE_LOCK );
                    }
                }
                else
                {
                    wlog ("%s Already Locked\n", node_ptr->hostname.c_str() );
                }
            }
            else if (!inv.action.compare ( "lock" ))
            {
                if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
                {
                    if ( node_ptr->dor_recovery_mode == true )
                         node_ptr->dor_recovery_mode = false ;

                    /* Set action to LOCK and let the FSM run the disable handler */
                    adminActionChange ( node_ptr , MTC_ADMIN_ACTION__LOCK );
                }
                else
                {
                    wlog ("%s Already Locked\n", node_ptr->hostname.c_str() );
                }
            }
            else if (!inv.action.compare ( "unlock" ))
            {
                if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
                {
                    if ( node_ptr->patching == true )
                    {
                        wlog ("%s cannot unlock host while patching is in progress\n", node_ptr->hostname.c_str());
                        rc = FAIL_PATCH_INPROGRESS ;
                    }
                    else
                    {
                        recovery_ctrl_init ( node_ptr->hwmon_reset );
                        recovery_ctrl_init ( node_ptr->hwmon_powercycle );

                        /* Set action to UNLOCK and let the FSM run the enable handler */
                        adminActionChange ( node_ptr , MTC_ADMIN_ACTION__UNLOCK );

                        mtcAlarm_clear ( node_ptr->hostname, MTC_ALARM_ID__LOCK );
                        node_ptr->alarms[MTC_ALARM_ID__LOCK] = FM_ALARM_SEVERITY_CLEAR ;

                        /* generate command=unlock log */
                        mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_UNLOCK );
                    }
                }
                else
                {
                    wlog ("%s Already UnLocked\n", node_ptr->hostname.c_str() );
                }
            }
            else if ( !inv.action.compare ( "swact" ) ||
                      !inv.action.compare ( "force-swact" ) )
            {
                if ( !((get_host_function_mask ( inv.type ) & CONTROLLER_TYPE) == CONTROLLER_TYPE) )
                {
                    elog ("%s Rejecting '%s' - Swact only supported for Controllers\n", 
                              node_ptr->hostname.c_str(),
                              inv.action.c_str());
                    rc = FAIL_NODETYPE ;
                }
                else if ( nodeLinkClass::is_inactive_controller_main_insv() != true )
                {
                    elog ("%s Rejecting '%s' - No In-Service Mate\n", 
                              node_ptr->hostname.c_str(),
                              inv.action.c_str());
                    rc = FAIL_SWACT_NOINSVMATE ;
                }
                else if ( node_ptr->adminAction != MTC_ADMIN_ACTION__NONE )
                {
                    elog ("%s Rejecting '%s' - '%s' In-Progress\n", 
                              node_ptr->hostname.c_str(),
                              inv.action.c_str(),
                              get_adminAction_str( node_ptr->adminAction ));
                    rc = FAIL_OPER_INPROGRESS ;
                }
                else if ( smgrEvent.mutex )
                {
                    elog ("%s Rejecting '%s' - Operation in-progress\n", 
                              node_ptr->hostname.c_str(),
                              inv.action.c_str());
                    rc = FAIL_SWACT_INPROGRESS ;
                }
                // don't run the patching tests during a force-swact action
                else if ( node_ptr->patching == true )
                {
                    wlog ("%s cannot swact active controller while patching is in progress\n", node_ptr->hostname.c_str());
                    rc = FAIL_PATCH_INPROGRESS ;
                }
                else if ( inactive_controller_is_patching() == true )
                {
                    wlog ("%s cannot swact to inactive controller while patching is in progress\n", node_ptr->hostname.c_str());
                    rc = FAIL_PATCH_INPROGRESS ;
                }
                // if this is a force-swact action then allow swact to a
                // patched node that has not been rebooted yet, since 
                // this is a recoverable operation. The other two patching tests
                // (above) need to be done on all swact actions since it may
                // render the system non-recoverable. 
                else if ( !inv.action.compare ( "swact" ) &&
                          inactive_controller_is_patched() == true )
                {
                    wlog ("%s cannot swact to a 'patched' but not 'rebooted' host\n", node_ptr->hostname.c_str());
                    rc = FAIL_PATCHED_NOREBOOT ;
                }
                else
                {
                    plog ("%s Action=%s\n", node_ptr->hostname.c_str(), 
                           inv.action.c_str());
                    if ( !inv.action.compare ( "force-swact" ) )
                        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__FORCE_SWACT );
                    else
                        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__SWACT );

                    /* generate command=swact log */
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_SWACT );

                    smgrEvent.mutex = true ;
                }
            }
            else if ( !inv.action.compare ( "reboot" ) )
            {
                plog ("%s Reboot Action\n", node_ptr->hostname.c_str());
                node_ptr->resetProgStage = MTC_RESETPROG__START ;
                node_ptr->retries = 0 ;
                mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_MANUAL_REBOOT );
                adminActionChange ( node_ptr , MTC_ADMIN_ACTION__REBOOT );
            }
            else if ( !inv.action.compare ( "reinstall" ) )
            {
                plog ("%s Reinstall Action\n", node_ptr->hostname.c_str());
                adminActionChange ( node_ptr , MTC_ADMIN_ACTION__REINSTALL );

                /* generate command=reinstall log */
                mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_REINSTALL );
            }
            else if ( !inv.action.compare ( "reset" ) )
            {
                string bm_ip = NONE ;
                rc = FAIL_RESET_CONTROL ;
                node_ptr->retries = 0 ;
                plog ("%s Reset Action\n", node_ptr->hostname.c_str());

                if ( hostUtil_is_valid_bm_type ( node_ptr->bm_type ) == false )
                {
                    wlog ("%s reset rejected due to unprovisioned bmc\n",
                              node_ptr->hostname.c_str());
                    rc = FAIL_BM_PROVISION_ERR ;
                }
                else if ( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF )
                {
                    wlog ("%s reset rejected for powered off host\n",
                              node_ptr->hostname.c_str());
                    rc = FAIL_RESET_POWEROFF;
                }
                else if ( node_ptr->bm_un.empty() )
                {
                    wlog ("%s reset rejected due to unconfigured 'bm_username'\n",
                              node_ptr->bm_un.c_str());
                    rc = FAIL_BM_PROVISION_ERR ;
                }
                else
                {
                    rc = PASS ;
                    node_ptr->resetStage = MTC_RESET__START ;
                    adminActionChange ( node_ptr , MTC_ADMIN_ACTION__RESET );
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_MANUAL_RESET );
                }
            }
            else if ( !inv.action.compare ( "power-on" ) )
            {
                string bm_ip = NONE ;
                rc = FAIL_POWER_CONTROL ;

                plog ("%s Power-On Action\n", node_ptr->hostname.c_str());

                if ( hostUtil_is_valid_bm_type ( node_ptr->bm_type ) == false )
                {
                    wlog ("%s power-on rejected due to unprovisioned bmc\n",
                              node_ptr->hostname.c_str());
                    rc = FAIL_BM_PROVISION_ERR ;
                }

                else if ( node_ptr->bm_un.empty() )
                {
                    wlog ("%s power-on rejected due to unconfigured 'bm_username'\n",
                              node_ptr->hostname.c_str());
                    rc = FAIL_BM_PROVISION_ERR ;
                }
                else
                {
                    rc = PASS ;
                    node_ptr->powerStage = MTC_POWERON__START ;
                    adminActionChange ( node_ptr , MTC_ADMIN_ACTION__POWERON );
                }
                mtcInvApi_update_task ( node_ptr, "" );
            }
            else if ( !inv.action.compare ( "power-off" ) )
            {
                string bm_ip = NONE ;
                rc = FAIL_POWER_CONTROL ;
                plog ("%s Power-Off Action\n", node_ptr->hostname.c_str());

                if ( hostUtil_is_valid_bm_type ( node_ptr->bm_type ) == false )
                {
                    wlog ("%s power-off rejected due to unprovisioned bmc\n",
                              node_ptr->hostname.c_str());
                    rc = FAIL_BM_PROVISION_ERR ;
                }
                else if ( node_ptr->bm_un.empty() )
                {
                    wlog ("%s power-off rejected due to unconfigured 'bm_username'\n",
                              node_ptr->hostname.c_str());
                    rc = FAIL_BM_PROVISION_ERR ;
                }
                else
                {
                    if (( !hostUtil_is_valid_ip_addr ( node_ptr->bm_ip )) &&
                        ( !hostUtil_is_valid_ip_addr (           bm_ip )))
                    {
                        wlog ("%s power-off may fail ; 'bm_ip' is undiscovered\n",
                              node_ptr->hostname.c_str());
                    }
                    rc = PASS ;
                    node_ptr->powerStage = MTC_POWEROFF__START ;
                    adminActionChange ( node_ptr , MTC_ADMIN_ACTION__POWEROFF );
                }
                mtcInvApi_update_task ( node_ptr, "" );
            }
            else
            {
                wlog ("%s Unsupported action '%s'\n",
                          node_ptr->hostname.c_str(),
                          inv.action.c_str());
                rc = FAIL_ADMIN_ACTION ;
                mtcInvApi_update_task ( node_ptr, "" );
            }
        }
        if ( node_ptr->uuid.compare ( inv.uuid ) )
        {
            send_hwmon_command ( node_ptr->hostname, MTC_CMD_DEL_HOST );
            plog ("%s Modify 'uuid' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->uuid.c_str(), inv.uuid.c_str() );
            node_ptr->uuid = inv.uuid ;
            send_hwmon_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );
            modify = true ; /* we have a delta */
        }
        if ( node_ptr->type.compare ( inv.type ) )
        {
            plog ("%s Modify 'personality' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->type.c_str(), inv.type.c_str() );

            node_ptr->type = inv.type ;
            node_ptr->nodetype = get_host_function_mask ( inv.type );

            modify = true ; /* we have a delta */
        }
        if ( node_ptr->ip.compare ( inv.ip ) )
        {
            plog ("%s Modify 'mgmt_ip' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->ip.c_str(), inv.ip.c_str());
            node_ptr->ip = inv.ip ;

            /* Tell the guestAgent the new IP */
            rc = send_guest_command(node_ptr->hostname,MTC_CMD_MOD_HOST);

            modify = true ; /* we have a delta */
        }
        if ( node_ptr->mac.compare ( inv.mac ) )
        {
            plog ("%s Modify 'mgmt_mac' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->mac.c_str(), inv.mac.c_str() );
            node_ptr->mac = inv.mac ;

            modify = true ; /* we have a delta */
        }
        if ( node_ptr->infra_ip.compare ( inv.infra_ip ) )
        {
            if (( hostUtil_is_valid_ip_addr ( inv.infra_ip )) || ( hostUtil_is_valid_ip_addr ( node_ptr->infra_ip )))
            {
                plog ("%s Modify 'infra_ip' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->infra_ip.c_str(), inv.infra_ip.c_str() );

                modify = true ; /* we have a delta */
            }
            node_ptr->infra_ip = inv.infra_ip ;
        }
        if ( (!inv.name.empty()) && (node_ptr->hostname.compare ( inv.name)) )
        {
            mtcCmd  cmd ;
            mtcCmd_init ( cmd );
            cmd.stage = MTC_CMD_STAGE__START ;
            cmd.cmd = MTC_OPER__MODIFY_HOSTNAME ;
            cmd.name = inv.name ;
            node_ptr->mtcCmd_work_fifo.push_back(cmd);
            plog ("%s Modify 'hostname' to %s (mtcCmd_queue:%ld)\n",
                      node_ptr->hostname.c_str(),
                      cmd.name.c_str() ,
                      node_ptr->mtcCmd_work_fifo.size());

            modify_bm = true ; /* board mgmnt change */
            modify    = true ; /* we have some delta */
        }
        if ( node_ptr->bm_un.compare ( inv.bm_un ) )
        {
            if ( inv.bm_un.empty () )
                    inv.bm_un = "none" ;

            plog ("%s Modify 'bm_username' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->bm_un.c_str(), inv.bm_un.c_str());

            node_ptr->bm_un = inv.bm_un ;

            modify_bm = true ; /* board mgmnt change */
            modify    = true ; /* we have some delta */
        }

        /* PATCHBACK - issue found during BMC refactoring user story
         * where there was a race condition found where the bmc dnsmasq file
         * was updated with a new bm_ip close to when there was an
         * administrative operation (unlock in this case). The newly learned
         * bm_ip was overwritten by the now stale bm_ip that came in from
         * inventory. The bm_ip should never come from sysinv while in
         * internal mode. */
        if (( node_ptr->bm_ip.compare ( inv.bm_ip )))
        {
            if ( inv.bm_ip.empty () )
                    inv.bm_ip = NONE ;

            /* if not empty and not none and already used then reject */
            if ( is_bm_ip_already_used ( inv.bm_ip ) == true )
            {
                wlog ("%s cannot use already provisioned bm ip %s\n",
                          node_ptr->hostname.c_str(),
                          inv.bm_ip.c_str());
                return (FAIL_DUP_IPADDR);
            }
            plog ("%s Modify 'bm_ip' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->bm_ip.c_str(), inv.bm_ip.c_str());

            node_ptr->bm_ip = inv.bm_ip ;

            modify_bm = true ; /* board mgmnt change */
            modify    = true ; /* we have some delta */
        }
        if ( node_ptr->bm_type.compare ( inv.bm_type ) )
        {
            if ( inv.bm_type.empty() )
                inv.bm_type = "none" ;
            else
                inv.bm_type = tolowercase(inv.bm_type) ;

            plog ("%s Modify 'bm_type' from %s -> %s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->bm_type.c_str(), inv.bm_type.c_str());

            modify_bm = true ; /* board mgmnt change */
            modify    = true ; /* we have some delta */
        }

        /* print a log if we find that there was nothing to modify */
        if ( modify == false )
        {
            wlog ("%s Modify request without anything to modify\n", node_ptr->hostname.c_str());
        }
        if ( modify_bm == true )
        {
            wlog ("%s Board Management provisioning has changed\n", node_ptr->hostname.c_str());
            bool bm_type_was_valid = hostUtil_is_valid_bm_type (node_ptr->bm_type) ;
            bool bm_type_now_valid = hostUtil_is_valid_bm_type (inv.bm_type) ;

            /* update bm_type now */
            node_ptr->bm_type = inv.bm_type ;

            /* BM is provisioned */
            if ( bm_type_now_valid == true )
            {
                /* force (re)provision */
                manage_bmc_provisioning ( node_ptr );
            }

            /* BM is already provisioned but is now deprovisioned */
            else if (( bm_type_was_valid == true ) && ( bm_type_now_valid == false ))
            {
                node_ptr->bm_type = NONE ;
                node_ptr->bm_ip   = NONE ;
                node_ptr->bm_un   = NONE ;
                mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_BM_DEPROVISIONED );
                set_bm_prov ( node_ptr, false );
            }

            /* BM was not provisioned and is still not provisioned */
            else
            {
                /* Handle all other provisioning changes ; username, ip address */
                manage_bmc_provisioning ( node_ptr );
            }
        }
    }
    else
    {
        elog ("getNode failed to find uuid: %s\n", inv.uuid.c_str());
    }

    return ( rc );
}

void nodeLinkClass::start_offline_handler ( struct nodeLinkClass::node * node_ptr )
{
    bool already_active = false ;
    mtc_offlineStages_enum offlineStage_saved = node_ptr->offlineStage ;

    if ( node_ptr->offlineStage == MTC_OFFLINE__IDLE )
    {
        node_ptr->offlineStage = MTC_OFFLINE__START ;
    }
    else
    {
        already_active = true ;
    }
    plog ("%s%soffline handler (%s-%s-%s) (stage:%d)\n",
              node_ptr->hostname.c_str(),
              already_active ? " " : " starting ",
              adminState_enum_to_str(node_ptr->adminState).c_str(),
              operState_enum_to_str(node_ptr->operState).c_str(),
              availStatus_enum_to_str(node_ptr->availStatus).c_str(),
              offlineStage_saved);
    node_ptr->offline_log_throttle = 0;
}

void nodeLinkClass::stop_offline_handler ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->offlineStage != MTC_OFFLINE__IDLE )
    {
        plog ("%s stopping offline handler (%s-%s-%s) (stage:%d)\n",
                  node_ptr->hostname.c_str(),
                  adminState_enum_to_str(node_ptr->adminState).c_str(),
                  operState_enum_to_str(node_ptr->operState).c_str(),
                  availStatus_enum_to_str(node_ptr->availStatus).c_str(),
                  node_ptr->offlineStage);
        node_ptr->offlineStage = MTC_OFFLINE__IDLE ;
    }
    node_ptr->offline_log_throttle = 0;
}

string nodeLinkClass::get_host ( string uuid )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( uuid );
    if ( node_ptr != NULL )
    {
        return (node_ptr->hostname) ;
    }
    return ( "" );
}

/** Check to see if the node list already contains any of the following
 * information and reject the add or modify if it does 
 *
 *    uuid
 *    hostname
 *    ip address
 *    mac address
 *
 **/
int nodeLinkClass::add_host_precheck ( node_inv_type & inv )
{
    struct node *      ptr = static_cast<struct node *>(NULL) ;
    struct node * node_ptr = static_cast<struct node *>(NULL) ;
    int rc = PASS ;

    if ( head == NULL )
        return (PASS);

    for ( node_ptr = head ;  ; node_ptr = node_ptr->next )
    {
        /* look or the UUID */
        if ( !node_ptr->uuid.compare(inv.uuid))
        {
             rc = RETRY ;
             dlog ("%s found in mtce\n", node_ptr->uuid.c_str());
             break ;
        }
        else if (( node_ptr->next == NULL ) || ( node_ptr == tail ))
            break ;
    }

    /** If that uuid is not found then make sure there
     *  are no other entries in the list that already
     *  has the same info that we want to create a
     *  new host with. 
     *  If so then reject by returning a failure.
     */
    for ( ptr = head ;  ; ptr = ptr->next )
    {   
       /* if this uuid is found then see if we are being
        * asked to modify and make sure that we are not being
        * asked to modify other members to values that are 
        * used by other hosts 
        * If so then reject by returning a failure.
        * otherwise then allow the modification by returning a retry.
        */

        if (( rc == RETRY ) && ( ptr == node_ptr ))
        {
            dlog ("%s skip\n", ptr->hostname.c_str());
            /* skip the node we found the UUID on
             * but make sure that none of the other nodes
             * have the same data */
        }
        else
        {
            dlog ("%s check\n", ptr->hostname.c_str());

            if ( !ptr->hostname.compare(inv.name))
            {
                wlog ("hostname (%s) already used ; rejecting add / modify\n", inv.name.c_str());
                return(FAIL_DUP_HOSTNAME);
            }
            if ( ptr->ip.compare("none") != 0 && !ptr->ip.compare(inv.ip))
            {
                wlog ("ip address (%s) already used ; rejecting add / modify\n", inv.ip.c_str());
                return(FAIL_DUP_IPADDR);
            }
            if ( !ptr->mac.compare(inv.mac))
            {
                wlog ("mac address (%s) already used ; rejecting add / modify\n", inv.mac.c_str());
                return(FAIL_DUP_MACADDR);
            }
        }
        if (( ptr->next == NULL ) || ( ptr == tail ))
            break ;
    }
    return (rc);
}

int nodeLinkClass::add_host ( node_inv_type & inv )
{
    int rc = FAIL ;
    struct nodeLinkClass::node * node_ptr = static_cast<struct node *>(NULL);

    if ((!inv.name.compare("controller-0")) || 
        (!inv.name.compare("controller-1")))
    {
        dlog ("Adding %s\n", inv.name.c_str());
        node_ptr = nodeLinkClass::getNode(inv.name); 
    }
    else if  (( inv.name.empty())           || 
             ( !inv.name.compare ("none") ) ||
             ( !inv.name.compare ("None") ))
    {
        wlog ("Refusing to add host with 'null' or 'invalid' hostname (%s)\n", 
               inv.uuid.c_str());
        return (FAIL_INVALID_HOSTNAME) ;
    }
    else if  (( inv.uuid.empty())           || 
             ( !inv.uuid.compare ("none") ) ||
             ( !inv.uuid.compare ("None") ))
    {
        wlog ("Refusing to add host with 'null' or 'invalid' uuid (%s)\n", 
               inv.uuid.c_str());
        return (FAIL_INVALID_UUID) ;
    }

    /* Ensure we don't add a host with critical info that is
     * already used by other members of inventory like ; 
     * hostname, uuid, ip, mac, bm_ip */
    else if ( ( rc = add_host_precheck ( inv )) > RETRY )
    {
        return (rc);
    }
    else
    {
        if ( rc == RETRY )
        {
            dlog ("%s modify operation\n", inv.uuid.c_str());
        }
        else
        {
            dlog ("%s add operation\n", inv.uuid.c_str());
        }
        node_ptr = nodeLinkClass::getNode(inv.uuid);
    }

    if ( node_ptr ) 
    {
        dlog ("%s Already provisioned\n", node_ptr->hostname.c_str());

        /* update some of the info */
        node_ptr->adminState  = adminState_str_to_enum  (inv.admin.data());
        node_ptr->operState   = operState_str_to_enum   (inv.oper.data ());
        node_ptr->availStatus = availStatus_str_to_enum (inv.avail.data());

        if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
        {
            node_ptr->operState_subf   = operState_str_to_enum (inv.oper_subf.data());
            node_ptr->availStatus_subf = availStatus_str_to_enum (inv.avail_subf.data());
        }

        /* Send back a retry so that this add is converted to a modify */
        return (RETRY);
    }
    /* Otherwise add it as a new node */
    else
    {
        if ( daemon_get_cfg_ptr()->debug_level != 0 )
            print_inv ( inv );

        unsigned int  nodetype_temp = get_host_function_mask ( inv.type );
        if ( (nodetype_temp & CONTROLLER_TYPE) == CONTROLLER_TYPE )
        {
           if ( inactive_controller_hostname.empty () == false )
           {
               wlog ("Cannot provision more than 2 controllers\n");
               wlog ("%s is already provisioned as inactive\n",
                     inactive_controller_hostname.c_str());
               return (FAIL);
           }
        }

        /* Prevent allowing add of a reserved hostname
         * with and incorrect node type. Reserved names are
         *
         * controller-0 and controller-1 must be a controller type
         * storage-0                     must be a storage    type
         *
         * */

        if ((( !inv.name.compare ("controller-0")) && (( nodetype_temp & CONTROLLER_TYPE) != CONTROLLER_TYPE )) ||
            (( !inv.name.compare ("controller-1")) && (( nodetype_temp & CONTROLLER_TYPE ) != CONTROLLER_TYPE)) ||
            (( !inv.name.compare ("storage-0"   )) && (( nodetype_temp & STORAGE_TYPE) != STORAGE_TYPE)))
        {
            wlog ("Cannot provision '%s' as a '%s' host\n", inv.name.c_str(), inv.type.c_str());
            return (FAIL_RESERVED_NAME);
        }
        node_ptr = nodeLinkClass::addNode(inv.name);
        if ( node_ptr )
        {
            bool validStates = false ;
            node_ptr->hostname    = inv.name ;

            /* set the node type ; string and define code */
            node_ptr->type        = inv.type ;
            node_ptr->nodetype    = get_host_function_mask ( inv.type ) ;

            update_host_functions ( inv.name, inv.func );

            node_ptr->ip   = inv.ip   ;
            node_ptr->mac  = inv.mac  ;
            node_ptr->uuid = inv.uuid ;
            node_ptr->infra_ip  = inv.infra_ip  ;

            if ( inv.uptime.length() )
            {
               sscanf ( inv.uptime.data(), "%u", &node_ptr->uptime );
               dlog2 ("%s Uptime (%s:%u)\n", inv.name.c_str(), inv.uptime.c_str(), node_ptr->uptime );
            }
            else
            {
                node_ptr->uptime = 0 ;
            }

            node_ptr->thread_extra_info.bm_ip  = node_ptr->bm_ip   = inv.bm_ip   ;
            node_ptr->thread_extra_info.bm_un  = node_ptr->bm_un   = inv.bm_un   ;
            node_ptr->thread_extra_info.bm_type= node_ptr->bm_type = inv.bm_type ;

            node_ptr->bm_ping_info.sock = 0 ;

            /* initialize the host power and reset control thread */
            thread_init ( node_ptr->ipmitool_thread_ctrl,
                          node_ptr->ipmitool_thread_info,
                         &node_ptr->thread_extra_info,
                          mtcThread_ipmitool,
                          DEFAULT_THREAD_TIMEOUT_SECS,
                          node_ptr->hostname,
                          THREAD_NAME__IPMITOOL);

            if ( adminStateOk  (inv.admin) &&
                 operStateOk   (inv.oper ) &&
                 availStatusOk (inv.avail))
            {
                validStates = true ;
            }

            clog ("%s subf state %s-%s\n", node_ptr->hostname.c_str(), inv.oper_subf.c_str(), inv.avail_subf.c_str() );

            node_ptr->task = inv.task ;

            /* Add based on 'action' */
            if ((!inv.action.empty()) && (inv.action.compare ("none")))
            {
                /* Save current action */
                node_ptr->action = inv.action ;

                if ( !inv.action.compare ("unlock") && validStates )
                {
                    ilog ("%s Added in 'unlocked' state\n", node_ptr->hostname.c_str());

                    print_inv ( inv );

                    node_ptr->adminState  = adminState_str_to_enum  (inv.admin.data());
                    node_ptr->operState   = operState_str_to_enum   (inv.oper.data ());
                    node_ptr->availStatus = availStatus_str_to_enum (inv.avail.data());

                    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                    {
                        node_ptr->operState_subf   = operState_str_to_enum (inv.oper_subf.data());
                        node_ptr->availStatus_subf = availStatus_str_to_enum (inv.avail_subf.data());
                    }
                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__UNLOCK );
                }
                else if ( !inv.action.compare ("lock") && validStates )
                {
                    ilog ("%s Added in 'locked' state\n", node_ptr->hostname.c_str());

                    print_inv ( inv );

                    node_ptr->adminState  = adminState_str_to_enum  (inv.admin.data());
                    node_ptr->operState   = operState_str_to_enum   (inv.oper.data ());
                    node_ptr->availStatus = availStatus_str_to_enum (inv.avail.data());

                    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                    {
                        node_ptr->operState_subf   = operState_str_to_enum (inv.oper_subf.data());
                        node_ptr->availStatus_subf = availStatus_str_to_enum (inv.avail_subf.data());
                    }

                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__LOCK );
                }
                else if ( !inv.action.compare ("force-lock") && validStates )
                {
                    ilog ("%s Added in 'force-locked' state\n", node_ptr->hostname.c_str());

                    print_inv ( inv );

                    node_ptr->adminState  = adminState_str_to_enum  (inv.admin.data());
                    node_ptr->operState   = operState_str_to_enum   (inv.oper.data ());
                    node_ptr->availStatus = availStatus_str_to_enum (inv.avail.data());

                    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                    {
                        node_ptr->operState_subf   = operState_str_to_enum (inv.oper_subf.data());
                        node_ptr->availStatus_subf = availStatus_str_to_enum (inv.avail_subf.data());
                    }

                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__FORCE_LOCK );
                }
                else if ( !inv.action.compare ("reboot") && validStates )
                {
                    ilog ("%s Added with 'reboot' in 'locked' state\n", node_ptr->hostname.c_str());

                    print_inv ( inv );

                    node_ptr->adminState  = adminState_str_to_enum  (inv.admin.data());
                    node_ptr->operState   = operState_str_to_enum   (inv.oper.data ());
                    node_ptr->availStatus = availStatus_str_to_enum (inv.avail.data());

                    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                    {
                        node_ptr->operState_subf   = operState_str_to_enum (inv.oper_subf.data());
                        node_ptr->availStatus_subf = availStatus_str_to_enum (inv.avail_subf.data());
                    }

                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__REBOOT );
                }
                else if ( !inv.action.compare ("reset") && validStates )
                {
                    ilog ("%s Added with 'reset' in 'locked' state\n", node_ptr->hostname.c_str());

                    print_inv ( inv );

                    node_ptr->adminState  = adminState_str_to_enum  (inv.admin.data());
                    node_ptr->operState   = operState_str_to_enum   (inv.oper.data ());
                    node_ptr->availStatus = availStatus_str_to_enum (inv.avail.data());

                    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                    {
                        node_ptr->operState_subf   = operState_str_to_enum (inv.oper_subf.data());
                        node_ptr->availStatus_subf = availStatus_str_to_enum (inv.avail_subf.data());
                    }

                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__RESET );
                }
                else if ( !inv.action.compare ("power-off") && validStates )
                {
                    ilog ("%s Added in a 'locked' and 'power-off' state\n", node_ptr->hostname.c_str());

                    print_inv ( inv );

                    node_ptr->adminState  = MTC_ADMIN_STATE__LOCKED ;
                    node_ptr->operState   = MTC_OPER_STATE__DISABLED;
                    node_ptr->availStatus = MTC_AVAIL_STATUS__POWERED_OFF ;

                    node_ptr->operState_subf   = MTC_OPER_STATE__DISABLED ;
                    node_ptr->availStatus_subf = MTC_AVAIL_STATUS__POWERED_OFF ;

                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__POWEROFF );
                }
                else if ( !inv.action.compare ("power-on") && validStates )
                {
                    ilog ("%s Added with 'power-on' in 'locked' state\n", node_ptr->hostname.c_str());

                    print_inv ( inv );

                    node_ptr->adminState  = MTC_ADMIN_STATE__LOCKED ;
                    node_ptr->operState   = MTC_OPER_STATE__DISABLED;
                    node_ptr->availStatus = MTC_AVAIL_STATUS__OFFLINE ;

                    node_ptr->operState_subf   = MTC_OPER_STATE__DISABLED ;
                    node_ptr->availStatus_subf = MTC_AVAIL_STATUS__OFFLINE ;

                    node_ptr->onlineStage = MTC_ONLINE__START ;
                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__POWERON );
                }
                else
                {
                    wlog ("%s Need add Action support for '%s' action\n", node_ptr->hostname.c_str(),
                               inv.action.c_str());

                    print_inv ( inv );

                    /* Load in maintenance states */
                    node_ptr->adminState  = MTC_ADMIN_STATE__LOCKED ;
                    node_ptr->operState   = MTC_OPER_STATE__DISABLED ;
                    node_ptr->availStatus = MTC_AVAIL_STATUS__OFFLINE ;

                    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                    {
                        node_ptr->operState_subf   = MTC_OPER_STATE__DISABLED ;
                        node_ptr->availStatus_subf = MTC_AVAIL_STATUS__OFFLINE ;
                    }

                    node_ptr->onlineStage = MTC_ONLINE__START ;

                    wlog ("%s Need '%s' action enabled here\n", node_ptr->hostname.c_str(), 
                              inv.action.c_str());
                }
            }
            else
            {
                node_ptr->adminState  = adminState_str_to_enum  (inv.admin.data());
                node_ptr->operState   = operState_str_to_enum   (inv.oper.data ());
                node_ptr->availStatus = availStatus_str_to_enum (inv.avail.data());

                if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
                {
                    node_ptr->operState_subf   = operState_str_to_enum (inv.oper_subf.data());
                    node_ptr->availStatus_subf = availStatus_str_to_enum (inv.avail_subf.data());
                }
            }

            /* Clear the heartbeat failure conts for this host */
            for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
            {
                node_ptr->hbs_degrade_count[iface] = 0;
                node_ptr->hbs_failure_count[iface] = 0;
            }

            /* Add to the end of inventory */
            hostname_inventory.push_back ( node_ptr->hostname );
            rc = PASS ;
        }
    }

    if (( rc == PASS ) && ( node_ptr ))
    {
        node_ptr->addStage = MTC_ADD__START ;
        adminActionChange ( node_ptr , MTC_ADMIN_ACTION__ADD );
    }
    return (rc);
}

void nodeLinkClass::clear_service_readies ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr )
    {
        if ( node_ptr->hbsClient_ready || node_ptr->pmond_ready )
        {
            ilog ("%s clearing service ready events\n", node_ptr->hostname.c_str());
            node_ptr->hbsClient_ready = false ;
            node_ptr->pmond_ready     = false ;
        }
    }
}

/* Used by the heartbeat service to add a host to its list */
int nodeLinkClass::add_heartbeat_host ( const node_inv_type & inv )
{
    int rc = FAIL ;
    struct nodeLinkClass::node * node_ptr = static_cast<struct node *>(NULL);

    dlog ("%s with nodetype %u\n", inv.name.c_str(), inv.nodetype );

    /* no hostname - no add ! */
    if ( inv.name.length() )
    {
        /* Handle the case where we are adding a node that is already     */
        /* present if so just update the inventory data not the mtc state */
        node_ptr = nodeLinkClass::getNode(inv.name);
        if ( node_ptr ) 
        {
            dlog ("%s already provisioned\n", node_ptr->hostname.c_str());
            rc = RETRY ;
        }
        /* Otherwise add it as a new node */
        else
        {
            node_ptr = nodeLinkClass::addNode(inv.name);
            if ( node_ptr != NULL )
            {
                node_ptr->hostname = inv.name     ;
                node_ptr->nodetype = inv.nodetype ;
                dlog ("%s added to linked list\n", inv.name.c_str());
                rc = PASS ;
            }
            else
            {
                elog ("Failed to addNode %s to heartbeat service\n", inv.name.c_str());
            }
        }
    }
    return (rc);
}

string nodeLinkClass::get_uuid ( string hostname )
{
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode(hostname);
    if ( node_ptr )
    {
        return (node_ptr->uuid);
    }
    else
    {
        return  ("");
    }
}

void nodeLinkClass::set_uuid ( string hostname, string uuid )
{
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode(hostname);
    if ( node_ptr )
    {
        node_ptr->uuid = uuid ;
    }
}

/* Set the task field in the maintenance class object for the specified host */
void nodeLinkClass::set_task ( string hostname, string task )
{
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode(hostname);
    if ( node_ptr )
    {
        node_ptr->task = task ;
    }
}

/* Lock Rules
 *
 * 1. Cannot lock this controller
 * 2. Cannot lock inactive controller if storage-0 is locked
 * 3. Cannot lock storage node with monitor if inactive conroller is locked or not present
 * 4. Cannot lock last storage host.
 */
bool nodeLinkClass::can_uuid_be_locked ( string uuid , int & reason )
{
    struct nodeLinkClass::node * node_ptr = nodeLinkClass::getNode(uuid);
    if ( node_ptr )
    {
        dlog1 ("%s Lock permission query\n", node_ptr->hostname.c_str());
        
        /* Allow lock of already locked 'any' host */
        if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
        {
            ilog ("%s is already 'locked'\n", node_ptr->hostname.c_str());
            return (true);
        }
        else if ( node_ptr->operState == MTC_OPER_STATE__DISABLED )
        {
            ilog ("%s allowing lock of 'disabled' host\n", node_ptr->hostname.c_str() );
            return (true);
        }
        else if (is_controller(node_ptr))
        {
            /* Rule 1 - Cannot lock active controller */
            if ( THIS_HOST )
            {
                elog ("%s Cannot be 'locked' - controller is 'active'\n", node_ptr->hostname.c_str());
                reason = FAIL_UNIT_ACTIVE ;
                return (false);
            }
            /* Rule 2 - Cannot lock inactive controller if the floating storage 
             *          ceph monitor is locked */
            if (( get_storage_backend() == CGCS_STORAGE_CEPH ) && 
                (  is_storage_mon_enabled () == false ))
            {
                wlog ("%s cannot be 'locked' - failed storage redundancy check\n", node_ptr->hostname.c_str());
                reason = FAIL_NEED_STORAGE_MON ;
                return (false);
            }
            ilog ("%s can be locked\n", node_ptr->hostname.c_str());
            return (true);
        }
        else if ( is_compute(node_ptr) )
        {
            if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
            {
                dlog ("%s is 'unlocked' and can be 'locked'\n", node_ptr->hostname.c_str());
            }
            return (true);
        }
        /* Deal with lock of storage cases - Rules 3 and 4 */
        else if ( is_storage(node_ptr) )
        {
            /* Only need to semantic check if this host is unlocked-enabled */
            if ( node_ptr->operState == MTC_OPER_STATE__ENABLED )
            {
                /* Both active controllers path ... */
                if ( num_controllers_enabled () >= 2 )
                {
                    /* If we are locking storage-0 make sure that there
                     * is another enabled storage node */
                    if ( !node_ptr->hostname.compare("storage-0") ) 
                    {
                        /* We already know that this storage node is enabled so
                         * we need to see a count greated than 1 */
                        if ( enabled_storage_nodes () > 1 )
                        {
                            /* We have 2 enabled controllers and 2 enabled
                             * storage nodes so we can allow lock of storage-0 */
                            ilog ("%s can be locked - there is storage redundancy\n", 
                                      node_ptr->hostname.c_str());
                            return (true);
                        }
                        /* Rule 4 - Cannot lock last storage node */
                        else
                        {
                            wlog ("%s cannot be locked - no storage redundancy\n", 
                                      node_ptr->hostname.c_str());
                            reason = FAIL_LOW_STORAGE ;
                            return (false);
                        }
                    }
                    /* O.K. we are trying to lock a storage host tha is not
                     * the floating storage monitor */
                    else if (( is_storage_mon_enabled () == true ) &&
                             ( enabled_storage_nodes() > 1 ))
                    {
                        /* We have - 2 enabled controllers
                         *         - the storage mon is enabled and
                         *         - is not this one. */
                        ilog ("%s can be locked - there is storage redundancy\n", 
                                  node_ptr->hostname.c_str());
                        return (true);
                    }
                    /* Rule 4 - Cannot lock last storage node */
                    else if (enabled_storage_nodes() <= 1)
                    {
                        wlog ("%s cannot be locked - no storage redundancy\n", 
                                      node_ptr->hostname.c_str());
                        reason = FAIL_LOW_STORAGE ;
                        return (false);
                    }
                    else
                    {
                        /* Other redundancy checks here and in SysInv have passed. */
                        ilog ("%s can be locked - storage redundancy filters passed.\n",
                                  node_ptr->hostname.c_str());
                        return (true);
                    }
                }

                /* Rule 3 - Cannot lock storage node with monitor if inactive
                 *          controller is locked or not present and there is
                 *          not another storage node enabled */
                else
                {
                    /* Cannot lock storage-0 if there is only a single enabled controller */
                    if ( !node_ptr->hostname.compare("storage-0") )
                    {
                        wlog ("%s cannot be locked - simplex system\n", 
                                      node_ptr->hostname.c_str());
                        reason = FAIL_NEED_STORAGE_MON ;
                        return (false);
                    }
                    /* Only allow locking of a storage node if there is another in service */
                    else if (( is_storage_mon_enabled () == true ) &&
                             ( enabled_storage_nodes() > 1 ))
                    {
                        ilog ("%s can be locked - there is storage redundancy\n", 
                                  node_ptr->hostname.c_str());
                        return (true);
                    }
                    /* Rule 4 - Cannot lock last storage node */
                    else
                    {
                        wlog ("%s cannot be locked - no redundancy\n", 
                        node_ptr->hostname.c_str());
                        reason = FAIL_LOW_STORAGE ;
                        return (false);
                    }
                }
            }
            else
            {
                ilog ("%s allowing lock of disabled storage host\n", 
                          node_ptr->hostname.c_str());
                return (true);
            }
        }
        else
        {
            elog ("%s unsupported nodetype (%u)\n", 
                      node_ptr->hostname.c_str(),
                      node_ptr->nodetype);
            return (false);
        }
    }
    else
    {
        dlog ("Unknown uuid: %s\n", uuid.c_str());

        /* allowing lock as a means to clear up error */
        return (true);
    }
}

int nodeLinkClass::rem_host ( string & hostname )
{
    int rc = FAIL ;
    if ( ! hostname.empty() )
    {
        hostname_inventory.remove ( hostname );
        rc = nodeLinkClass::remNode ( hostname );
    }
    return ( rc );
}

void nodeLinkClass::set_my_hostname ( string hostname )
{
    struct nodeLinkClass::node * node_ptr ;

    nodeLinkClass::my_hostname = hostname ;

    /* set it in the local inventory as well */
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->hostname = hostname ;
    }
}


string nodeLinkClass::get_my_hostname ( void )
{
    return( nodeLinkClass::my_hostname );
}

void nodeLinkClass::set_my_local_ip ( string & ip )
{
    nodeLinkClass::node* node_ptr ;

    nodeLinkClass::my_local_ip = ip ;

    /* set it in the local inventory as well */
    node_ptr = nodeLinkClass::getNode ( my_hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->ip = ip ;
    }
}

string nodeLinkClass::get_my_local_ip ( void )
{
    return( nodeLinkClass::my_local_ip );
}

void nodeLinkClass::set_my_float_ip ( string & ip )
{
    nodeLinkClass::my_float_ip = ip ;
}

string nodeLinkClass::get_my_float_ip ( void )
{
    return( nodeLinkClass::my_float_ip );
}

static string null_str = "" ;
string nodeLinkClass::get_hostaddr ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->ip );
    }
    return ( null_str );
}

string nodeLinkClass::get_infra_hostaddr ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->infra_ip );
    }
    return ( null_str );
}

string nodeLinkClass::get_hostIfaceMac ( string & hostname, int iface )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        if ( iface == MGMNT_IFACE )
            return ( node_ptr->mac );
        if ( iface == INFRA_IFACE )
            return ( node_ptr->infra_mac );
    }
    ilog ("%s has unknown mac address for %s interface\n", hostname.c_str(), get_iface_name_str(iface));
    return ( null_str );
}

int nodeLinkClass::set_hostaddr ( string & hostname, string & ip )
{
    int rc = FAIL ;

    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->ip = ip ;
        rc = PASS ;
    }
    return ( rc );
}

int nodeLinkClass::set_infra_hostaddr ( string & hostname, string & ip )
{
    int rc = FAIL ;

    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->infra_ip = ip ;
        rc = PASS ;
    }
    return ( rc );
}

string nodeLinkClass::get_hostname ( string & hostaddr )
{
    if (( hostaddr == LOOPBACK_IPV6 ) ||
        ( hostaddr == LOOPBACK_IP ) ||
        ( hostaddr == LOCALHOST ))
    {
        return(my_hostname);
    }
    else
    {
        nodeLinkClass::node* node_ptr ;
        node_ptr = nodeLinkClass::getNode ( hostaddr );
        if ( node_ptr != NULL )
        {
            return ( node_ptr->hostname );
        }
        return ( null_str );
    }
}

string nodeLinkClass::get_hostname_from_bm_ip ( string bm_ip )
{
    if ( head )
    {
        for ( struct node * ptr = head ;  ; ptr = ptr->next )
        {
            if ( ! ptr->bm_ip.compare(bm_ip) )
            {
                return ( ptr->hostname );
            }

            if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return ("") ;
}

int nodeLinkClass::num_hosts ( void )
{
    return ( nodeLinkClass::hosts ) ;
}

void nodeLinkClass::set_cmd_resp ( string & hostname, mtc_message_type & msg )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        if ( is_host_services_cmd ( msg.cmd ) )
        {
            /*****************************************************
             * Host Services Request's Response Handling
             *****************************************************/
            node_ptr->host_services_req.status = msg.parm[0] ;
            if ( msg.cmd == node_ptr->host_services_req.cmd )
            {
                // print_mtc_message ( &msg, true );

                /* if num > 1 then expect a host services result message */
                if ( msg.cmd == MTC_CMD_HOST_SVCS_RESULT )
                {
                    if ( !node_ptr->host_services_req.ack )
                    {
                        slog ("%s %s without initial command ACK\n",
                                  hostname.c_str(),
                                  node_ptr->host_services_req.name.c_str());
                    }
                    node_ptr->host_services_req.rsp = msg.cmd ;
                    if ( msg.buf[0] != '\0' )
                    {
                        node_ptr->host_services_req.status_string = msg.buf ;
                    }
                }

                /* Check to see if the start/stop host services command
                 * response demonstrates support for the enhanced host
                 * services extension. */
                else if (( msg.num > 1 ) && ( msg.parm[1] == MTC_ENHANCED_HOST_SERVICES ))
                {
                    dlog ("%s %s request ack\n",
                              hostname.c_str(),
                              node_ptr->host_services_req.name.c_str());
                    node_ptr->host_services_req.ack = true ;
                }
                else
                {
                    ilog ("%s %s request ack (legacy mode)\n",
                              hostname.c_str(),
                              node_ptr->host_services_req.name.c_str());
                    /* support legacy client by copying the cmd to cmdRsp */
                    node_ptr->host_services_req.status = PASS ;
                    node_ptr->host_services_req.rsp = msg.cmd ;
                    node_ptr->host_services_req.ack = MTC_CMD_NONE ;
                }
            }

            if ( msg.num && ( node_ptr->host_services_req.status != PASS ))
            {
                dlog ("%s %s command failed (rc:%d) [%s]\n",
                          hostname.c_str(),
                          get_mtcNodeCommand_str(msg.cmd),
                          node_ptr->host_services_req.status,
                          node_ptr->host_services_req.status_string.empty() ?
                          "no error string" : node_ptr->host_services_req.status_string.c_str());
            }
        }
        else
        {
            node_ptr->cmdRsp = msg.cmd ;
            if ( msg.num > 0 )
                node_ptr->cmdRsp_status = msg.parm[0] ;
            else
                node_ptr->cmdRsp_status = -1 ;

            dlog ("%s '%s' command response status [%u:%s]\n",
                  hostname.c_str(),
                  node_ptr->cmdName.c_str(),
                  msg.num ? node_ptr->cmdRsp_status : PASS,
                  node_ptr->cmdRsp_status_string.empty() ? "empty" : node_ptr->cmdRsp_status_string.c_str());
        }
    }
}

unsigned int nodeLinkClass::get_cmd_resp ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->cmdRsp ) ;
    }
    return (-1);
}

mtc_client_enum nodeLinkClass::get_activeClient ( string hostname )
{
    nodeLinkClass::node* node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->activeClient ) ;
    }
    else
    {
        slog ("Host lookup failed for '%s'\n", hostname.c_str());
    }
    return (CLIENT_NONE);
}

int nodeLinkClass::set_activeClient ( string hostname, mtc_client_enum client )
{
    nodeLinkClass::node* node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->activeClient = client ;
        return (PASS);
    }
    else
    {
        slog ("Host lookup failed for '%s'\n", hostname.c_str());
    }
    return (FAIL_HOSTNAME_LOOKUP);
}

/*****************************************************************************
 *
 * Name       : set_mtcAlive
 *
 * Description:
 *
 * If mtcAlive is ungated then
 *
 *  1. manage the online/offline state bools
 *  2. increment the mtcAlive count and
 *  3. set the mtcAlive received bool for the specified interface
 *
 *****************************************************************************/
void nodeLinkClass::set_mtcAlive ( string & hostname, int interface )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        if ( node_ptr->mtcAlive_gate == false )
        {
            node_ptr->mtcAlive_online  = true  ;
            node_ptr->mtcAlive_offline = false ;
            node_ptr->mtcAlive_count++ ;

            if ( interface == INFRA_INTERFACE )
            {
                node_ptr->mtcAlive_infra = true ;
            }
            else
            {
                node_ptr->mtcAlive_mgmnt = true ;
            }
        }
    }
}

bool nodeLinkClass::get_mtcAlive_gate ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->mtcAlive_gate ) ;
    }
    /* If we can't find the node then gate off the alive messages */
    return (true);
}

void nodeLinkClass::ctl_mtcAlive_gate ( string & hostname, bool gated )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->mtcAlive_gate = gated ;
        if ( gated == true )
        {
            alog ("%s mtcAlive gated\n", node_ptr->hostname.c_str());
        }
        else
        {
            alog ("%s mtcAlive ungated\n", node_ptr->hostname.c_str());
        }
    }
}

/* Main-Function Go Enabled member Functions */

void nodeLinkClass::set_goEnabled ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->goEnabled = true ;
    }
}

bool nodeLinkClass::get_goEnabled ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->goEnabled ) ;
    }
    return (false);
}

void nodeLinkClass::set_goEnabled_failed ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->goEnabled_failed = true ;
    }
}

/* Sub-Function Go Enabled Member Functions */

void nodeLinkClass::set_goEnabled_subf ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->goEnabled_subf = true ;
    }
}

bool nodeLinkClass::get_goEnabled_subf ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->goEnabled_subf ) ;
    }
    return (false);
}

void nodeLinkClass::set_goEnabled_failed_subf ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->goEnabled_failed_subf = true ;
    }
}

/* Set and Get Uptime Member Function */

void nodeLinkClass::set_uptime ( struct nodeLinkClass::node * node_ptr, unsigned int uptime, bool force )
{
    if ( node_ptr != NULL )
    {
        /* Force the uptime into the database if
         * - passed in value is  0 and current value is !0
         * - passed in value is !0 and current value is  0
         * - if ther force option is used
         * Otherwise allow the audit to push time to the database
         */
        if ((force == true ) ||
            (( uptime != 0 ) && ( node_ptr->uptime == 0 )) ||
            (( node_ptr->uptime != 0 ) && ( uptime == 0 )))
        {
            mtcInvApi_update_uptime ( node_ptr, uptime );
        }
        node_ptr->uptime = uptime ;
    }
}

void nodeLinkClass::set_uptime ( string & hostname, unsigned int uptime, bool force )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    set_uptime ( node_ptr, uptime, force );
}


unsigned int  nodeLinkClass::get_uptime ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->uptime ) ;
    }
    return (0);
}

void nodeLinkClass::set_uptime_refresh_ctr ( string & hostname, int value )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->uptime_refresh_counter = value ;
    } 
}


int  nodeLinkClass::get_uptime_refresh_ctr ( string & hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return ( node_ptr->uptime_refresh_counter ) ;
    }
    return (0);
}

void nodeLinkClass::set_mtce_flags ( string hostname, int flags )
{
    nodeLinkClass::node* node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        /* Deal with host level */
        node_ptr->mtce_flags = flags ;
        if ( flags & MTC_FLAG__MAIN_GOENABLED )
            node_ptr->goEnabled = true  ;
        else
            node_ptr->goEnabled = false ;

        /* Track host patching state by Out-Of-Band flag */
        if ( flags & MTC_FLAG__PATCHING )
        {
            if ( node_ptr->patching == false )
            {
                plog ("%s software patching has begun\n", node_ptr->hostname.c_str());
            }
            node_ptr->patching = true  ;
        }
        else
        {
            if ( node_ptr->patching == true )
            {
                plog ("%s software patching done\n", node_ptr->hostname.c_str());
            }
            node_ptr->patching = false ;
        }

        /* Track host patched state by Out-Of-Band flag.
         * This flag is set when the host is patched but not reset */
        if ( flags & MTC_FLAG__PATCHED )
        {
            if ( node_ptr->patched == false )
            {
                plog ("%s software patched\n", node_ptr->hostname.c_str());
            }
            node_ptr->patched = true  ;
        }
        else
        {
            if ( node_ptr->patched == true )
            {
                plog ("%s software patch is applied\n", node_ptr->hostname.c_str());
            }
            node_ptr->patched = false ;
        }


        /* Deal with sub-function if AIO controller host */
        if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
        {
            if ( flags & MTC_FLAG__SUBF_GOENABLED )
            {
                if ( node_ptr->operState_subf == MTC_OPER_STATE__ENABLED )
                {
                    node_ptr->goEnabled_subf = true  ;
                }
            }
            else
            {
                node_ptr->goEnabled_subf = false ;
            }
        }
    }
}

void nodeLinkClass::set_health ( string & hostname, int health )
{
    switch ( health )
    {
        case NODE_HEALTH_UNKNOWN:
        case NODE_HEALTHY:
        case NODE_UNHEALTHY:
        {
            nodeLinkClass::node* node_ptr ;
            node_ptr = nodeLinkClass::getNode ( hostname );
            if ( node_ptr != NULL )
            {
                if ( health == NODE_UNHEALTHY )
                {
                    if ( node_ptr->health != NODE_UNHEALTHY )
                    {
                        if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
                        {
                            wlog ("%s Health State Change -> UNHEALTHY\n", hostname.c_str());
                        }
                    }
                }
                node_ptr->health = health ;
            }
            break ;
        }
        default:
        {
            wlog ("%s Unexpected health code (%d), defaulting to (unknown)\n", hostname.c_str(), health );
            break ;
        }
    }
}

/*************************************************************************************
 *
 * Name       : manage_bmc_provisioning
 *
 * Description: This utility manages a change in bmc provisioning for
 *              bm region EXTERNAL mode. Creates provisioning logs and
 *              sends START and STOP monitoring commands to the hardware monitor.
 *
 * Warning    : Should only be called when there is a change to BM provisioning.
 *              as it will first always first disable provisioning and then
 *              decides whether it needs to be re-enabled or not.
 *
 *************************************************************************************/

int nodeLinkClass::manage_bmc_provisioning ( struct node * node_ptr )
{
    int rc = PASS ;

    bool was_provisioned = node_ptr->bm_provisioned ;

    set_bm_prov ( node_ptr, false);
    if ((hostUtil_is_valid_ip_addr ( node_ptr->bm_ip )) &&
        (!node_ptr->bm_un.empty()))
    {
        if ( was_provisioned == true )
        {
            mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_BM_REPROVISIONED );
        }
        else
        {
            mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_BM_PROVISIONED );
        }

        set_bm_prov ( node_ptr, true );
    }
    else if ( was_provisioned == true )
    {
       send_hwmon_command(node_ptr->hostname,MTC_CMD_STOP_HOST);
       mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_BM_DEPROVISIONED );
    }

    /* Send hmond updated bm info */
    ilog ("%s sending board management info update to hwmond\n", node_ptr->hostname.c_str() );
    if ( ( rc = send_hwmon_command(node_ptr->hostname,MTC_CMD_MOD_HOST) ) == PASS )
    {
        if ( node_ptr->bm_provisioned == true )
        {
            rc = send_hwmon_command(node_ptr->hostname,MTC_CMD_START_HOST);
        }
        else
        {
            rc = send_hwmon_command(node_ptr->hostname,MTC_CMD_STOP_HOST);
        }
        if ( rc )
        {
            wlog ("%s failed to send START or STOP command to hwmond\n", node_ptr->hostname.c_str());
        }
    }
    else
    {
        wlog ("%s failed to send MODIFY command to hwmond\n", node_ptr->hostname.c_str());
    }
    return (rc);
}

bool nodeLinkClass::is_bm_ip_already_used ( string bm_ip )
{
    if ( hostUtil_is_valid_ip_addr ( bm_ip ) == true )
    {
        for ( struct node * ptr = head ;  ; ptr = ptr->next )
        {
            if ( !bm_ip.compare(ptr->bm_ip) )
            {
                return (true);
            }
            if (( ptr->next == NULL ) || ( ptr == tail ))
                break ;
        }
    }
    return (false);
}

int nodeLinkClass::set_bm_type ( string hostname , string bm_type )
{
    int rc = FAIL_HOSTNAME_LOOKUP ;
    
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->bm_type = bm_type ;
        dlog ("%s '%s' updated to '%s'\n", 
                      hostname.c_str(), 
                      MTC_JSON_INV_BMTYPE, 
                      node_ptr->bm_type.c_str());
        rc = PASS ;
    }
    return (rc);
}

int nodeLinkClass::set_bm_un ( string hostname , string bm_un )
{
    int rc = FAIL_HOSTNAME_LOOKUP ;
    
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        if ( bm_un.length() )
        {
            node_ptr->bm_un = bm_un ;
        }
        else
        {
            node_ptr->bm_un = NONE ;
        }
        dlog ("%s '%s' updated to '%s'\n", 
                      hostname.c_str(), 
                      MTC_JSON_INV_BMUN, 
                      node_ptr->bm_un.c_str());
        rc = PASS ;
    }
    return (rc);
}

int nodeLinkClass::set_bm_ip   ( string hostname , string bm_ip )
{
    int rc = FAIL_HOSTNAME_LOOKUP ;
    
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        node_ptr->bm_ip = bm_ip ;

        dlog ("%s '%s' updated to '%s'\n", 
                      hostname.c_str(), 
                      MTC_JSON_INV_BMIP, 
                      node_ptr->bm_ip.c_str());
        rc = PASS ;
    }
    return (rc);
}

void nodeLinkClass::bmc_access_data_init ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr )
    {
        node_ptr->bm_accessible = false;
        node_ptr->mc_info_query_active = false     ;
        node_ptr->mc_info_query_done = false       ;
        node_ptr->reset_cause_query_active = false ;
        node_ptr->reset_cause_query_done = false   ;
        node_ptr->power_status_query_active = false;
        node_ptr->power_status_query_done = false  ;
    }
}

/*****************************************************************************
 *
 * Name       : set_bm_prov
 *
 * Description: Manage the local provisioning state of the
 *              board management connection.
 *
 * Assumptions: Does not set HTTP requests to sysinv so it is
 *              safe to call from thje modify handler
 *
 *              Does not clear alarms.
 *
 ******************************************************************************/
int nodeLinkClass::set_bm_prov ( struct nodeLinkClass::node * node_ptr, bool state )
{
    int rc = FAIL_HOSTNAME_LOOKUP ;
    if ( node_ptr != NULL )
    {
        ilog ("%s bmc %sprovision request (provisioned:%s)\n", // ERIC blog
                  node_ptr->hostname.c_str(),
                  state ? "" : "de",
                  node_ptr->bm_provisioned ? "Yes" : "No" );

        /* Clear the alarm if we are starting fresh from an unprovisioned state */
        if (( node_ptr->bm_provisioned == false ) && ( state == true ))
        {
            /* BMC is managed by IPMI/IPMITOOL */
            ilog ("%s starting BM ping monitor to address '%s'\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->bm_ip.c_str());

            // mtcTimer_reset ( node_ptr->bm_ping_info.timer );
            node_ptr->bm_ping_info.ip = node_ptr->bm_ip ;
            node_ptr->bm_ping_info.stage = PINGUTIL_MONITOR_STAGE__OPEN ;
            bmc_access_data_init ( node_ptr );
            node_ptr->bm_ping_info.timer_handler = &mtcTimer_handler ;

            node_ptr->thread_extra_info.bm_pw =
            node_ptr->bm_pw =
            get_bm_password (node_ptr->uuid.data());

            node_ptr->thread_extra_info.bm_ip = node_ptr->bm_ip ;
            node_ptr->thread_extra_info.bm_un = node_ptr->bm_un ;

            send_hwmon_command(node_ptr->hostname, MTC_CMD_ADD_HOST);
            send_hwmon_command(node_ptr->hostname, MTC_CMD_START_HOST);
        }

        /* handle the case going from provisioned to not provisioned */
        else if (( node_ptr->bm_provisioned == true ) && ( state == false ))
        {
            /* BMC is managed by IPMI/IPMITOOL */
            ilog ("%s deprovisioning bmc ; accessible:%s\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->bm_accessible ? "Yes" : "No" );

            pingUtil_fini  ( node_ptr->bm_ping_info );
            bmc_access_data_init ( node_ptr );
            node_ptr->bm_accessible = false;

            if ( !thread_idle( node_ptr->ipmitool_thread_ctrl ) )
            {
                 thread_kill ( node_ptr->ipmitool_thread_ctrl , node_ptr->ipmitool_thread_info);
            }
            node_ptr->mc_info_query_active = false     ;
            node_ptr->mc_info_query_done = false       ;
            node_ptr->reset_cause_query_active = false ;
            node_ptr->reset_cause_query_done = false   ;
            node_ptr->power_status_query_active = false;
            node_ptr->power_status_query_done = false  ;

            /* send a delete to hwmon if the provisioning data is NONE */
            if ( hostUtil_is_valid_bm_type ( node_ptr->bm_type ) == false )
            {
                send_hwmon_command(node_ptr->hostname, MTC_CMD_DEL_HOST);
            }
        }
        if (( node_ptr->bm_provisioned == false ) && ( state == true ))
        {
            /* start the connection timer - if it expires before we
             * are 'accessible' then the BM Alarm is raised.
             * Timer is further managed in mtcNodeHdlrs.cpp */
            plog ("%s bmc access timer started (%d secs)\n", node_ptr->hostname.c_str(), MTC_MINS_2);
            mtcTimer_reset ( node_ptr->bmc_access_timer );
            mtcTimer_start ( node_ptr->bmc_access_timer, mtcTimer_handler, MTC_MINS_2 );
        }

        node_ptr->bm_provisioned = state ;
    }
    return (rc);
}

string nodeLinkClass::get_bm_ip   ( string hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
         return (node_ptr->bm_ip);
    }
    elog ("%s bm ip lookup failed\n", hostname.c_str() );
    return ("");
}

string nodeLinkClass::get_bm_un   ( string hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
         return (node_ptr->bm_un);
    }
    elog ("%s bm username lookup failed\n", hostname.c_str() );
    return ("");
}

string nodeLinkClass::get_bm_type ( string hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
         return (node_ptr->bm_type);
    }
    elog ("%s bm type lookup failed\n", hostname.c_str() );
    return ("");
}

string nodeLinkClass::get_hwmon_info ( string hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        string hwmon_info = "" ;

        hwmon_info.append( "{ \"personality\":\"" ) ;
        hwmon_info.append( node_ptr->type );
        hwmon_info.append( "\"");

        hwmon_info.append( ",\"hostname\":\"" ) ;
        hwmon_info.append( node_ptr->hostname );
        hwmon_info.append( "\"");

        hwmon_info.append( ",\"bm_ip\":\"" ) ;
        hwmon_info.append( node_ptr->bm_ip );
        hwmon_info.append( "\"");

        hwmon_info.append( ",\"bm_type\":\"");
        hwmon_info.append( node_ptr->bm_type );
        hwmon_info.append( "\"");

        hwmon_info.append( ",\"bm_username\":\"");
        hwmon_info.append( node_ptr->bm_un );
        hwmon_info.append( "\"");

        hwmon_info.append( ",\"uuid\":\"" ) ;
        hwmon_info.append( node_ptr->uuid );
        hwmon_info.append( "\" }");

        return (hwmon_info);
    }
    elog ("%s hwmon info lookup failed\n", hostname.c_str() );
    return ("");
}



int  nodeLinkClass::manage_shadow_change ( string hostname )
{
    int rc = FAIL ;
    if ( ! hostname.empty() )
    {  
        nodeLinkClass::node* node_ptr ;
        node_ptr = nodeLinkClass::getNode ( hostname );
        if ( node_ptr != NULL )
        {
            rc = PASS ;
            if ( node_ptr->configAction == MTC_CONFIG_ACTION__NONE )
            {
                node_ptr->configStage  = MTC_CONFIG__START ;
                node_ptr->configAction = MTC_CONFIG_ACTION__CHANGE_PASSWD ;
            }
            else
            {
                node_ptr->configAction = MTC_CONFIG_ACTION__CHANGE_PASSWD_AGAIN ;
            }
        }
    }
    return (rc);
}

/** Returns the number of compute hosts that are operationally 'enabled' */
int nodeLinkClass::enabled_compute_nodes ( void )
{
    int temp_count = 0 ;
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if (( is_compute( ptr )) &&
            ( ptr->operState == MTC_OPER_STATE__ENABLED ))
        {
            temp_count++ ;
        }
        else if (( is_compute_subfunction ( ptr )) &&
                 ( ptr->operState_subf == MTC_OPER_STATE__ENABLED ))
        {
            temp_count++ ;
        }

        if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
    }
    return (temp_count);
}

/** Returns the number of storage hosts that are operationally 'enabled' */
int nodeLinkClass::enabled_storage_nodes ( void )
{
    int temp_count = 0 ;
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if (( is_storage( ptr ) )  &&
            ( ptr->operState == MTC_OPER_STATE__ENABLED ))
        {
            temp_count++ ;
        }

        if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
    }
    return (temp_count);
}

int nodeLinkClass::enabled_nodes ( void )
{
    int temp_count = 0 ;
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if ( ptr->operState == MTC_OPER_STATE__ENABLED )
        {
            temp_count++ ;
        }

        if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
    }
    /* Remove the active controller from the count */
    if (temp_count)
        temp_count-- ;

    return (temp_count);
}

/** Returns the system's storage back end type ceph or nfs */
int nodeLinkClass::get_storage_backend ( void )
{
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if ( is_storage(ptr) )
            return ( CGCS_STORAGE_CEPH ) ;

        if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
    }
    return (CGCS_STORAGE_NFS);
}

/** Returns true if the storage pool has a monitor running on
  *  an unlocked-enabled storage host */
bool nodeLinkClass::is_storage_mon_enabled ( void )
{
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if ((  is_storage(ptr) ) &&
            (  ptr->operState == MTC_OPER_STATE__ENABLED ) &&
            ( !ptr->hostname.compare("storage-0")))
        {
            return ( true ) ;
        }
        if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
    }
    return (false);
}

/** Returns number of enabled controllers */
int nodeLinkClass::num_controllers_enabled ( void )
{
    int cnt = 0 ;
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if (( is_controller(ptr) ) &&
            (  ptr->operState == MTC_OPER_STATE__ENABLED ))
        {
            ++cnt ;
        }
        if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
    }
    return (cnt);
}


/** Returns true if the specified hostname is provisioned */
bool nodeLinkClass::hostname_provisioned ( string hostname )
{
    bool provisioned = false ;
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if ( ptr->hostname.compare(hostname) == 0 )
        {
            provisioned = true ;
            break ;
        }
        if (( ptr->next == NULL ) || ( ptr == tail ))
            break ;
    }
    return (provisioned);
}


int nodeLinkClass::service_netlink_events ( int nl_socket , int ioctl_socket )
{
    std::list<string> links_gone_down ;
    std::list<string> links_gone_up   ;
    std::list<string>::iterator iter_curr_ptr ;
    if ( get_netlink_events ( nl_socket, links_gone_down, links_gone_up )) 
    {
         const char * mgmnt_iface_ptr = daemon_get_cfg_ptr()->mgmnt_iface ;
         const char * infra_iface_ptr = daemon_get_cfg_ptr()->infra_iface ;
         bool running = false ;
         if ( !links_gone_down.empty() )
         {
             //wlog ("one or more links have dropped\n");
             /* Look at the down list */
             for ( iter_curr_ptr  = links_gone_down.begin();
                   iter_curr_ptr != links_gone_down.end() ;
                   iter_curr_ptr++ )
             {
                 bool care = false ;
                 if ( iter_curr_ptr->size() == 0 )
                     continue ;

                 if ( !strcmp (mgmnt_iface_ptr, iter_curr_ptr->data()))
                 {
                     care = true ;
                     mgmnt_link_up_and_running = false ; 
                     wlog ("Management link %s is down\n", mgmnt_iface_ptr );
                 }
                 if ( !strcmp (infra_iface_ptr, iter_curr_ptr->data()))
                 {
                     care = true ;
                     infra_link_up_and_running = false ; 
                     wlog ("Infrastructure link %s is down\n", infra_iface_ptr );
                 }

                 if ( care == true )
                 {
                     if ( get_link_state ( ioctl_socket, iter_curr_ptr->data(), &running ) == PASS )
                     {
                         wlog ("%s is down (oper:%s)\n", iter_curr_ptr->c_str(), running ? "up" : "down" );
                     }
                     else
                     {
                         wlog ("%s is down (driver query failed)\n", iter_curr_ptr->c_str() );
                     }
                 }
             }
         }
         if ( !links_gone_up.empty() )
         {
             // wlog ("one or more links have recovered\n");
             /* Look at the up list */
             for ( iter_curr_ptr  = links_gone_up.begin();
                   iter_curr_ptr != links_gone_up.end() ;
                   iter_curr_ptr++ )
             {
                 bool care = false ;
                 if ( iter_curr_ptr->size() == 0 )
                     continue ;
                 if ( !strcmp (mgmnt_iface_ptr, iter_curr_ptr->data()))
                 {
                     mgmnt_link_up_and_running = true ; 
                     wlog ("Management link %s is up\n", mgmnt_iface_ptr );
                 }
                 if ( !strcmp (infra_iface_ptr, iter_curr_ptr->data()))
                 {
                     infra_link_up_and_running = true ; 
                     wlog ("Infrastructure link %s is up\n", infra_iface_ptr );
                 }
                 if ( care == true )
                 {
                     if ( get_link_state ( ioctl_socket, iter_curr_ptr->data(), &running ) == PASS )
                     {
                         wlog ("%s is up (oper:%s)\n", iter_curr_ptr->c_str(), running ? "up" : "down" );
                     }
                     else
                     {
                         wlog ("%s is up (driver query failed)\n", iter_curr_ptr->c_str() );
                     }
                 }
             }
         }
    }
    return (PASS);
}


/* ***************************************************************************
 *
 * Name       : hbs_minor_clear
 *
 * Description: Clear the heartbeat minor state from the specified host.
 * 
 * Manage overall mnfa counts and call mnfa_exit when the number crosses
 * the recovery threwshold.
 *
 ******************************************************************************/
void nodeLinkClass::hbs_minor_clear ( struct nodeLinkClass::node * node_ptr, iface_enum iface )
{
    if ( mnfa_host_count[iface] == 0 )
        return ;

    /* Nothing to do if this host is not in the hbs_minor state */
    if ( node_ptr->hbs_minor[iface] == true )
    {
        /* clear it - possibly temporarily */
        node_ptr->hbs_minor[iface] = false ;

        /* manage counts over heartbeat failure */
        if ( mnfa_host_count[iface] )
        {
            /* If we are mnfa_active AND now below the threshold
             * then trigger mnfa_exit */
            if (( --mnfa_host_count[iface] < mnfa_threshold) &&
                   ( mnfa_active == true ))
            {
                wlog ("%s MNFA exit with graceful recovery (%s:%d)\n",
                          node_ptr->hostname.c_str(),
                          get_iface_name_str(iface),
                          mnfa_host_count[iface] );

                /* re-activate this to true so that it is part
                 * of the recovery group in mnfa_exit */
                node_ptr->hbs_minor[iface] = true ;
                mnfa_exit ( false );
            }

            /* Otherwise this is a single host that has recovered
             * possibly as part of a mnfa group or simply a lone wolf */
            else
            {
                if ( node_ptr->mnfa_graceful_recovery == true )
                {
                    ilog ("%s MNFA removed from pool\n", node_ptr->hostname.c_str() );
                    mnfa_awol_list.remove(node_ptr->hostname);
                }

                mnfa_recover_host ( node_ptr );

                if ( mnfa_active == true )
                {
                    /* Restart the heartbeat for this recovered host */
                    send_hbs_command ( node_ptr->hostname, MTC_RESTART_HBS );

                    /* don't restart graceful recovery for this host if its already in that FSM */
                    if ( node_ptr->adminAction != MTC_ADMIN_ACTION__RECOVER )
                    {
                        recoveryStageChange ( node_ptr, MTC_RECOVERY__START );
                        adminActionChange   ( node_ptr, MTC_ADMIN_ACTION__RECOVER );
                    }
                }
            }
        }
    }

    /* lets clean-up - walk the inventory and make sure the
     * avoidance count meets the number of hosts in the minor
     * degrade state */
    int temp_count = 0 ;
    for ( struct node * ptr = head ;  ; ptr = ptr->next )
    {
        if ( ptr->hbs_minor[iface] == true )
        {
            if ( ptr->operState != MTC_OPER_STATE__ENABLED )
            {
                slog ("%s found hbs_minor set for disabled host\n" , ptr->hostname.c_str() );
            }
            temp_count++ ;
        }
        if (( ptr->next == NULL ) || ( ptr == tail ))
            break ;
    }

     if ( temp_count != mnfa_host_count[iface] )
     {    
         slog ("%s MNFA host tally (%s:%d incorrect - expected %d) ; correcting\n",
                   node_ptr->hostname.c_str(),
                   get_iface_name_str(iface),
                   mnfa_host_count[iface], temp_count );
                   mnfa_host_count[iface] = temp_count ;
         mnfa_host_count[iface] = temp_count ;
     }    
     else
     {
         wlog ("%s MNFA host tally (%s:%d)\n",
                   node_ptr->hostname.c_str(),
                   get_iface_name_str(iface),
                   mnfa_host_count[iface] );
     }
}

/****************************************************************************
 *
 * Name       : manage_dor_recovery
 *
 * Description: Enable DOR recovery mode for this host.
 *              Generate log
 *
 *              The severity parm is used to enhance the logs to indicate what
 *              severity level this utility was called from ;
 *              minor, major, or critical
 *
 ***************************************************************************/

void nodeLinkClass::manage_dor_recovery (  struct nodeLinkClass::node * node_ptr,
                                                    EFmAlarmSeverityT   severity )
{
    if (( severity == FM_ALARM_SEVERITY_CLEAR ) &&
        ( node_ptr->dor_recovery_mode == true ))
    {
        node_ptr->dor_recovery_mode = false ;
        node_ptr->was_dor_recovery_mode = true ;
    }

    else if (( severity == FM_ALARM_SEVERITY_CRITICAL ) &&
             ( node_ptr->dor_recovery_mode == false ))
    {
        struct timespec ts ;
        clock_gettime (CLOCK_MONOTONIC, &ts );
        wlog ("%-12s is waiting ; DOR recovery %2ld:%02ld mins (%4ld secs)\n",
                     node_ptr->hostname.c_str(),
                     ts.tv_sec/60,
                     ts.tv_sec%60,
                     ts.tv_sec);

        node_ptr->dor_recovery_time = 0     ;
        node_ptr->dor_recovery_mode = true  ;
        node_ptr->hbsClient_ready   = false ;
        mtcInvApi_update_task ( node_ptr, MTC_TASK_RECOVERY_WAIT );

        /* don't restart graceful recovery for this host if its already in that FSM */
        if (( node_ptr->adminAction != MTC_ADMIN_ACTION__RECOVER ) &&
            ( node_ptr->adminAction != MTC_ADMIN_ACTION__LOCK ))
        {
            recoveryStageChange ( node_ptr, MTC_RECOVERY__START );
            adminActionChange   ( node_ptr, MTC_ADMIN_ACTION__RECOVER );
        }
    }
}


/** Manage heartbeat failure events */
void nodeLinkClass::manage_heartbeat_failure ( string hostname, iface_enum iface, bool clear_event )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown host\n", hostname.c_str());
        return ;
    }

    /* Handle clear */
    if ( clear_event == true )
    {
        hbs_minor_clear ( node_ptr, iface );

        plog ("%s %s Heartbeat failure clear\n", hostname.c_str(), get_iface_name_str(iface));

        // if (( mnfa_host_count == 0 ) || ( iface == INFRA_IFACE ))
        if ( mnfa_host_count[iface] == 0 ) // || ( iface == INFRA_IFACE ))
        {
             slog ("%s %s Heartbeat failure clear\n", hostname.c_str(), get_iface_name_str(iface)); 
             node_ptr->hbs_failure[iface] = false ;
        }
    }
    else if ( this->mtcTimer_dor.tid )
    {
        manage_dor_recovery ( node_ptr, FM_ALARM_SEVERITY_CRITICAL );
    }
    else
    {
        /* handle auto recovery for heartbeat failure during enable */
        if ( node_ptr->ar_cause == MTC_AR_DISABLE_CAUSE__HEARTBEAT )
            return ;
        else if ( node_ptr->enableStage == MTC_ENABLE__HEARTBEAT_SOAK )
        {
            elog ("%s %s *** Heartbeat Loss *** (during enable soak)\n",
                      hostname.c_str(),
                      get_iface_name_str(iface));

            if ( ar_manage ( node_ptr,
                             MTC_AR_DISABLE_CAUSE__HEARTBEAT,
                             MTC_TASK_AR_DISABLED_HEARTBEAT ) == PASS )
            {
                  mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_FAIL_HB );
                  enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
                  adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
            }
            return ;
        }

        mnfa_add_host ( node_ptr , iface );

        if ( mnfa_active == false )
        {
            elog ("%s %s *** Heartbeat Loss ***\n", hostname.c_str(), get_iface_name_str(iface));
            if ( iface == INFRA_IFACE )
            {
                node_ptr->heartbeat_failed[INFRA_IFACE] = true ;
            }
            else if ( iface == MGMNT_IFACE )
            {
                node_ptr->heartbeat_failed[MGMNT_IFACE] = true ;
            }
            if (mnfa_host_count[iface] < this->mnfa_threshold)
            {
                elog ("%s %s network heartbeat failure\n", hostname.c_str(), get_iface_name_str(iface));

                nodeLinkClass::set_availStatus ( hostname, MTC_AVAIL_STATUS__FAILED );

                if (( node_ptr->adminAction != MTC_ADMIN_ACTION__ENABLE ) &&
                    ( node_ptr->adminAction != MTC_ADMIN_ACTION__UNLOCK ))
                {
                    if ( node_ptr->adminAction == MTC_ADMIN_ACTION__RECOVER )
                    {
                        wlog ("%s restarting graceful recovery\n", hostname.c_str() );
                    }
                    else
                    {
                        wlog ("%s starting graceful recovery\n", hostname.c_str() );
                    }
                    recoveryStageChange ( node_ptr, MTC_RECOVERY__START );
                    adminActionChange   ( node_ptr, MTC_ADMIN_ACTION__RECOVER );
                }
                else
                {
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_ENABLE_FAIL_HB );
                    enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
                    adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                }
            }
        }
    }
}

void nodeLinkClass::manage_heartbeat_clear ( string hostname, iface_enum iface )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown host\n", hostname.c_str());
        return ;
    }
    if ( iface == MAX_IFACES )
    {
        for ( int i = 0 ; i < MAX_IFACES ; i++ )
        {
            node_ptr->heartbeat_failed[i] = false ;
            if ( i == MGMNT_IFACE )
            {
                node_ptr->alarms[HBS_ALARM_ID__HB_MGMNT] = FM_ALARM_SEVERITY_CLEAR ;
                node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_MGMNT ;
            }
            if ( i == INFRA_IFACE )
            {
                node_ptr->alarms[HBS_ALARM_ID__HB_INFRA] = FM_ALARM_SEVERITY_CLEAR ;
                node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_INFRA ;
            }
        }
    }
    else
    {
        node_ptr->heartbeat_failed[iface] = false ;
        if ( iface == MGMNT_IFACE )
        {
            node_ptr->alarms[HBS_ALARM_ID__HB_MGMNT] = FM_ALARM_SEVERITY_CLEAR ;
            node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_MGMNT ;
        }
        else if ( iface == INFRA_IFACE )
        {
            node_ptr->alarms[HBS_ALARM_ID__HB_INFRA] = FM_ALARM_SEVERITY_CLEAR ;
            node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_INFRA ;
        }
    }
}

/** Manage compute host maintenance based on this heartbeat
  * degrade event and others that may be present at this moment */
void nodeLinkClass::manage_heartbeat_degrade ( string hostname, iface_enum iface, bool clear_event )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown host\n", hostname.c_str());
        return ;
    }

    if ( clear_event == true )
    {
        alog ("%s %s Heartbeat Degrade (clear)\n", hostname.c_str(), get_iface_name_str(iface));
        manage_heartbeat_clear ( hostname, iface );

        if ( iface == MGMNT_IFACE )
        {
            node_ptr->no_work_log_throttle = 0 ;
            node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_MGMNT ;
        }

        else if ( iface == INFRA_IFACE )
        {
            node_ptr->no_work_log_throttle = 0 ; 
            node_ptr->degrade_mask &= ~DEGRADE_MASK_HEARTBEAT_INFRA ;
        }

        hbs_minor_clear ( node_ptr, iface );
    }
    else if ( this->mtcTimer_dor.tid )
    {
        manage_dor_recovery ( node_ptr, FM_ALARM_SEVERITY_MAJOR );
    }
    else
    {
        if ( mnfa_active == false )
        {
            wlog ("%s %s *** Heartbeat Miss ***\n", hostname.c_str(), get_iface_name_str(iface) );
        }

        mnfa_add_host ( node_ptr, iface );

        if ( nodeLinkClass::get_operState ( hostname ) == MTC_OPER_STATE__ENABLED )
        {
            if ( iface == MGMNT_IFACE )
            {
                /* Don't raise the alarm again if this host is already degraded */
                if ( !(node_ptr->degrade_mask & DEGRADE_MASK_HEARTBEAT_MGMNT) )
                {
                    node_ptr->degrade_mask |= DEGRADE_MASK_HEARTBEAT_MGMNT ;
                }
            }
            if ( iface == INFRA_IFACE )
            {
                if ( !(node_ptr->degrade_mask & DEGRADE_MASK_HEARTBEAT_INFRA) )
                {
                    node_ptr->degrade_mask |= DEGRADE_MASK_HEARTBEAT_INFRA ;
                }
            }
        }
    }
}

/** Manage heartbeat minor events */
void nodeLinkClass::manage_heartbeat_minor ( string hostname, iface_enum iface, bool clear_event )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown host\n", hostname.c_str());
        return ;
    }

    /* is this a clear event ? */
    if ( clear_event == true )
    {
        alog ("%s %s Heartbeat Minor (clear)\n", hostname.c_str(), get_iface_name_str(iface));
        hbs_minor_clear ( node_ptr, iface );
    }
    /* if not a clear then only set if the host is enabled
     * - we don't care about disabled hosts */
    else if ( nodeLinkClass::get_operState ( hostname ) == MTC_OPER_STATE__ENABLED )
    {
        if ( this->mtcTimer_dor.tid )
        {
            manage_dor_recovery ( node_ptr, FM_ALARM_SEVERITY_MINOR );
        }

        else if ( node_ptr->hbs_minor[iface] != true )
        {
            mnfa_add_host ( node_ptr, iface );
        }
    }
}


/** Interface to declare that a key service on the
  * specified host is up, running and ready */
int nodeLinkClass::declare_service_ready  ( string & hostname,
                                            unsigned int service )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host\n", hostname.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }
    else if ( service == MTC_SERVICE_PMOND )
    {
        node_ptr->pmond_ready = true ;
        plog ("%s got pmond ready event\n", hostname.c_str());

        /* A ready event means that pmond pocess has started.
         * Any previous history is gone. Cleanup mtce.
         * If there are still process issues on this host then
         * they will be reported again.*/
        node_ptr->degrade_mask &= ~DEGRADE_MASK_PMON ;
        return (PASS);
    }
    else if ( service == MTC_SERVICE_HWMOND )
    {
        node_ptr->hwmond_ready = true ;
        plog ("%s got hwmond ready event\n", hostname.c_str());
        if ( node_ptr->bm_provisioned == true )
        {
            send_hwmon_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );
            send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );
        }
        return (PASS);
    }
    else if ( service == MTC_SERVICE_RMOND )
    {
        node_ptr->rmond_ready = true ;
        plog ("%s got rmond ready event\n", hostname.c_str());
        return (PASS);
    }
    else if ( service == MTC_SERVICE_HEARTBEAT )
    {
        if ( node_ptr->hbsClient_ready == false )
        {
            node_ptr->hbsClient_ready = true ;
            plog ("%s got hbsClient ready event\n", hostname.c_str());
        }
        return (PASS);
    }
    else
    {
        return (FAIL_BAD_CASE);
    }
}

/** Clear pmond degrade flag */
int nodeLinkClass::degrade_pmond_clear  ( string & hostname )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host\n", hostname.c_str());
        return (FAIL_UNKNOWN_HOSTNAME) ;
    }
    if ( node_ptr->degrade_mask )
    {
        node_ptr->degrade_mask &= ~DEGRADE_MASK_PMON ;
    }

    /* The only detectable inservice failures are process failures */
    node_ptr->inservice_failed_subf = false ;
    node_ptr->inservice_failed = false ;
    return (PASS);
}

/* This private API handles event messages from collectd */
int nodeLinkClass::collectd_notify_handler ( string & hostname,
                                             string & resource,
                                             string & state )
{
    int rc = PASS ;
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host\n", hostname.c_str());
        return (FAIL_UNKNOWN_HOSTNAME) ;
    }
    if ( state == "clear" )
    {
        if ( node_ptr->degrade_mask & DEGRADE_MASK_COLLECTD )
        {
            ilog("%s collectd degrade state change ; assert -> clear (%s)",
                     hostname.c_str(), resource.c_str());
            node_ptr->degrade_mask &= ~DEGRADE_MASK_COLLECTD ;
        }
        else
        {
            mlog3("%s collectd degrade 'clear' request (%s)",
                      hostname.c_str(), resource.c_str());
        }
    }
    else if ( state == "assert" )
    {
        if ( (node_ptr->degrade_mask & DEGRADE_MASK_COLLECTD) == 0 )
        {
            ilog("%s collectd degrade state change ; clear -> assert (due to %s)",
                     hostname.c_str(), resource.c_str());
            node_ptr->degrade_mask |= DEGRADE_MASK_COLLECTD ;
        }
        else
        {
            mlog3("%s collectd degrade 'assert' request (%s)",
                     hostname.c_str(), resource.c_str());
        }
    }
    else
    {
        wlog ("%s collectd degrade state unknown (%s)\n",
                  hostname.c_str(),
                  state.c_str());
        rc = FAIL_OPERATION ;
    }
    return (rc);
}

/** Resource Monitor 'Clear' Event handler.
  *
  * The resource specified will be removed from the
  * 'degraded_resources_list' for specified host.
  * if there are no other degraded resources or other
  * degraded services/reasons against that host then
  * this handler will clear the degrade state for the
  * specified host all together. */
int nodeLinkClass::degrade_resource_clear  ( string & hostname,
                                             string & resource )
{
    /* lr - Log Prefix Rmon */
    string lr = hostname ;
    lr.append (" rmond:");

    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host\n", lr.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }
    else if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
    {
        /* Clear all resource degrade conditions if there is no resource specified */
        /* this is used as a cleanup audit just in case things get stuck */
        if ( resource.empty() )
        {
            node_ptr->degrade_mask &= ~DEGRADE_MASK_RESMON ;
            node_ptr->degraded_resources_list.clear () ;
        }
        else if (( node_ptr->degraded_resources_list.empty()) ||
             ( node_ptr->degrade_mask == DEGRADE_MASK_NONE ))
        {
            dlog ("%s '%s' Non-Degraded Clear\n", 
                      lr.c_str(), resource.c_str());
        }
        else
        {
            if (is_string_in_string_list (node_ptr->degraded_resources_list, resource))
            {
                node_ptr->degraded_resources_list.remove(resource);
                ilog ("%s '%s' Degrade Clear\n", 
                          lr.c_str(), resource.c_str());
            }
            else
            {
                wlog ("%s '%s' Unexpected Degrade Clear\n", 
                          lr.c_str(), resource.c_str());
            }

            if ( node_ptr->degraded_resources_list.empty() )
            {
                node_ptr->degrade_mask &= ~DEGRADE_MASK_RESMON ; ;
            }
            else
            {
                string degraded_resources = 
                get_strings_in_string_list ( node_ptr->degraded_resources_list );
                wlog ("%s Degraded Resource List: %s\n", 
                          lr.c_str(), degraded_resources.c_str());
            }
        }

    }
    return (PASS);
}

/*********************************************************************************
 *
 * Name       : node_degrade_control
 *
 * Purpose    : Accept and handle degrade raise and clear requests from
 *              external services.
 *
 * Description: Maintenance maintains a degrade mask with a bit representing
 *              various services. The assertion of any one bit causes the host
 *              to be degraded. All bits need to be cleared in orde to exit
 *              the degrade state.
 *
 *              Supported 'services' include
 *
 *              "hwmon" - The Hardware Monitor process
 *
 *
 * Future services might be rmon and pmon
 *
 **********************************************************************************/
int nodeLinkClass::node_degrade_control ( string & hostname, int state, string service  )
{
    int rc  = FAIL_UNKNOWN_HOSTNAME ;

    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr )
    {
        unsigned int service_flag = 0 ;

        /* convert service string to degrade mask flag 
         *  - handle empty string and unsupported service */
        if ( service.empty() )
        {
            slog ("%s service not specified", hostname.c_str());
            return (FAIL_STRING_EMPTY);
        }
        else if ( !service.compare("hwmon") )
        {
            service_flag = DEGRADE_MASK_HWMON ;
        }
        else
        {
            slog ("%s service '%s' not supported\n",
                      hostname.c_str(),
                      service.c_str());
            return (FAIL_INVALID_DATA);
        }

        switch ( state )
        {
            /* Handle clear case */
            case MTC_DEGRADE_CLEAR:
            {
                if ( node_ptr->degrade_mask & service_flag )
                {
                    ilog ("%s degrade 'clear' from '%s'\n", hostname.c_str(), service.c_str() );
                }

                /* clear the mask regardless of host state */
                node_ptr->degrade_mask &= ~service_flag ;
                rc = PASS ;
                break ;
            }

            /* Handle assertion case */
            case MTC_DEGRADE_RAISE:
            {
                if (( node_ptr->degrade_mask & service_flag ) == 0 )
                {
                    wlog ("%s degrade 'assert' from '%s'\n", hostname.c_str(), service.c_str() );
                    node_ptr->degrade_mask |= service_flag ;
                }
                rc = PASS ;
                break ;
            }
            default:
            {
                wlog ("%s invalid degrade control request '%d'\n", hostname.c_str(), state);
                rc = FAIL_BAD_CASE ;
                break ;
            }
        } /* end switch */
    }
    else
    {
        dlog ("%s Unknown Host\n", hostname.c_str());
    }
    return (rc);
}


int nodeLinkClass::hwmon_recovery_monitor ( struct nodeLinkClass::node * node_ptr, int hwmon_event )
{
    int delay = MTC_MINS_15 ;
    if ( hwmon_event == MTC_EVENT_HWMON_POWERCYCLE )
    {
        node_ptr->hwmon_powercycle.retries = 0 ;
        node_ptr->hwmon_powercycle.queries = 0 ;
        node_ptr->hwmon_powercycle.state = RECOVERY_STATE__MONITOR ;

        mtcTimer_reset ( node_ptr->hwmon_powercycle.recovery_timer );
        mtcTimer_start ( node_ptr->hwmon_powercycle.recovery_timer, mtcTimer_handler, delay );

        ilog ("%s starting hwmon 'powercycle' recovery monitor", node_ptr->hostname.c_str());
        ilog ("%s ... uninterrupted completion time: %s", node_ptr->hostname.c_str(), future_time(delay));
    }
    else if ( hwmon_event == MTC_EVENT_HWMON_RESET )
    {
        node_ptr->hwmon_reset.retries = 0 ;
        node_ptr->hwmon_reset.queries = 0 ;
        node_ptr->hwmon_reset.state = RECOVERY_STATE__MONITOR ;

        mtcTimer_reset ( node_ptr->hwmon_reset.recovery_timer );
        mtcTimer_start ( node_ptr->hwmon_reset.recovery_timer, mtcTimer_handler, delay );

        ilog ("%s starting hwmon 'reset' recovery monitor", node_ptr->hostname.c_str());
        ilog ("%s ... uninterrupted completion time: %s", node_ptr->hostname.c_str(), future_time(delay));
    }
    return (PASS);
}

/* Hardware Monitor 'Action' Event method
 *
 * The hardware monitor daemon is calling out a sensor that
 * is operating out of spec. The command is the accompanying
 * action that hwmond requested as a recovery action to this failure.
 * The sensor is the sensor name that triggersed the event. */
int nodeLinkClass::invoke_hwmon_action  ( string & hostname, int action, string & sensor )
{
    int rc = PASS ;
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;

    dlog ("%s request to '%s' due to critical sensor '%s' reading\n",
              hostname.c_str(),
              get_event_str(action).c_str(),
              sensor.c_str());

    if ( node_ptr )
    {
        if ( node_ptr->bm_accessible == false )
        {
            wlog ("%s rejecting %s hwmon action request for '%s' sensor ; BMC not accessible\n",
                      hostname.c_str(),
                      get_event_str(action).c_str(),
                      sensor.c_str());

            return (PASS);
        }
        if ( action == MTC_EVENT_HWMON_RESET )
        {
            if ( is_active_controller (hostname) == true )
            {
                wlog ("%s refusing to 'reset' self due to critical '%s' sensor event\n",
                          hostname.c_str(), sensor.c_str());
                recovery_ctrl_init ( node_ptr->hwmon_reset );
                return(rc);
            }

            /* Avoid interrupting higher priority powercycle action */
            else if (( node_ptr->adminAction == MTC_ADMIN_ACTION__POWERCYCLE ) ||
                     ( node_ptr->hwmon_powercycle.state != RECOVERY_STATE__INIT ))
            {
                wlog ("%s bypassing 'reset' request while 'powercycle' already in progress (%s)\n",
                          hostname.c_str(), sensor.c_str());
            }
            else if ( node_ptr->adminAction != MTC_ADMIN_ACTION__NONE )
            {
                wlog ("%s bypassing 'reset' request while '%s' action in progress (%s)\n",
                          hostname.c_str(), get_adminAction_str(node_ptr->adminAction), sensor.c_str());
            }
            else if ( node_ptr->hwmon_reset.state )
            {
                wlog ("%s rejecting 'reset' request while already in progress (%s)\n",
                          hostname.c_str(), sensor.c_str());
            }
            else
            {
                if (( node_ptr->adminState  == MTC_ADMIN_STATE__UNLOCKED ) &&
                    ( node_ptr->operState   == MTC_OPER_STATE__ENABLED ))
                {
                    mtcTimer_reset ( node_ptr->hwmon_reset.recovery_timer );
                    mtcTimer_start ( node_ptr->hwmon_reset.recovery_timer, mtcTimer_handler, MTC_MINS_15 );

                    force_full_enable ( node_ptr );
                }
                else
                {
                    if ( node_ptr->adminAction != MTC_ADMIN_ACTION__RESET )
                    {
                        elog ("%s starting 'reset' FSM\n", hostname.c_str());

                        mtcTimer_reset ( node_ptr->hwmon_reset.recovery_timer );
                        mtcTimer_start ( node_ptr->hwmon_reset.recovery_timer, mtcTimer_handler, MTC_MINS_15 );

                        adminActionChange ( node_ptr , MTC_ADMIN_ACTION__RESET );
                    }
                    else
                    {
                        wlog ("%s mtce 'reset' action already in progress\n", hostname.c_str());
                    }
                }
                node_ptr->hwmon_reset.state = RECOVERY_STATE__HOLDOFF ;
            }
        }
        else if ( action == MTC_EVENT_HWMON_POWERCYCLE )
        {
            if ( node_ptr->hwmon_powercycle.attempts > MAX_POWERCYCLE_ATTEMPT_RETRIES )
            {
                wlog ("%s ignoring 'powercycle' request ; too many failed attempts (%d)\n",
                        node_ptr->hostname.c_str(), node_ptr->hwmon_powercycle.attempts );
            }
            else if ( is_active_controller (hostname) == true )
            {
                wlog ("%s refusing to 'powercycle' self due to critical '%s' sensor event\n",
                          hostname.c_str(), sensor.c_str());
                recovery_ctrl_init ( node_ptr->hwmon_powercycle ) ;
            }
            else
            {
                if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWERCYCLE )
                {
                    wlog ("%s bypassing 'powercycle' request while already in progress (%s)\n",
                              hostname.c_str(), sensor.c_str());
                }
                else if ( node_ptr->adminAction != MTC_ADMIN_ACTION__NONE )
                {
                    wlog ("%s bypassing 'powercycle' request while '%s' action in progress (%s)\n",
                              hostname.c_str(),
                              get_adminAction_str(node_ptr->adminAction),
                              sensor.c_str());
                }
                else if ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__COOLOFF )
                {
                    wlog ("%s avoiding 'powercycle' request while in powercycle recovery cooloff (%s)\n",
                              hostname.c_str(), sensor.c_str());
                }
                else if ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__HOLDOFF )
                {
                    wlog ("%s avoiding 'powercycle' request while in powercycle recovery holdoff (%s)\n",
                              hostname.c_str(), sensor.c_str());
                }
                else if ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__ACTION )
                {
                    wlog ("%s avoiding 'powercycle' request while already handling powercycle (%s)\n",
                              hostname.c_str(), sensor.c_str());
                }
                else if ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__BLOCKED )
                {
                    wlog ("%s avoiding 'powercycle' request ; host is powered off due to protect hardware from damage due to critical '%s' sensor\n",
                              hostname.c_str(), sensor.c_str());
                }
                else
                {
                    if ( node_ptr->hwmon_powercycle.state == RECOVERY_STATE__MONITOR )
                    {
                        wlog ("%s 'powercycle' request while in monitor phase (%s)\n",
                                  hostname.c_str(), sensor.c_str());
                    }

                    /* Cancel the recovery timer only to have it started once the
                     * next power cycle phase is complete */
                    mtcTimer_reset ( node_ptr->hwmon_powercycle.recovery_timer );

                    wlog ("%s invoking 'powercycle' due to critical '%s' sensor assertion\n", hostname.c_str(), sensor.c_str());
                    powercycleStageChange ( node_ptr, MTC_POWERCYCLE__START );
                    subStageChange        ( node_ptr, MTC_SUBSTAGE__START );
                    adminActionChange     ( node_ptr, MTC_ADMIN_ACTION__POWERCYCLE );
                }
            }
        }
        else
        {
            slog ("%s '%s' action not supported as request from hwmond\n",
                      hostname.c_str(),
                      get_event_str(action).c_str());
            rc = FAIL_BAD_PARM ;
        }
    }
    else
    {
        slog ("%s cannot '%s' due to unknown host\n", hostname.c_str(), get_event_str(action).c_str());
        rc = FAIL_UNKNOWN_HOSTNAME ;
    }
    return (rc);
}

/* Generate a log for the reported failed process if that host is
 * unlocked */
int nodeLinkClass::log_process_failure  ( string & hostname, string & process )
{
    /* lp - Log Prefix */
    string lp = hostname ;
    lp.append (" pmon:");

    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host ; '%s' failed (minor)\n",
                  lp.c_str(), process.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }
    else if ( node_ptr->operState == MTC_OPER_STATE__ENABLED )
    {
        if ( process.compare("ntpd") )
        {
            wlog ("%s '%s' process failed and is being auto recovered\n",
                      lp.c_str(),
                      process.c_str());
        }
        else
        {
            wlog ("%s '%s' process has failed ; manual recovery action required\n",
                      lp.c_str(),
                      process.c_str());
        }
    }
    return (PASS);
}

/* if unlocked-enabled generate an alarm for the reported failed process */
int nodeLinkClass::alarm_process_failure  ( string & hostname, string & process )
{
    /* lp - Log Prefix */
    string lp = hostname ;
    lp.append (" pmon:");

    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host ; '%s' failed (minor)\n", 
                  lp.c_str(), process.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }
    else if ( node_ptr->operState == MTC_OPER_STATE__ENABLED )
    {
        /* TODO: Generate Alarm here */

        wlog ("%s '%s' failed (minor)\n", lp.c_str(), process.c_str());
    }
    return (PASS);
}

/* Generate a log for the reported failed resource if that host is
 * unlocked */
int nodeLinkClass::log_resource_failure  ( string & hostname, string & resource )
{
    /* lr - Log Prefix Rmond */
    string lr = hostname ;
    lr.append (" rmond:");
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host ; '%s' failed (minor)\n", 
                  lr.c_str(), resource.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }
    else if ( node_ptr->operState == MTC_OPER_STATE__ENABLED )
    {
       ilog ("%s '%s' failed (minor)\n", 
                 lr.c_str(), resource.c_str());
    }
    return (PASS);
}

/** Process Monitor Degrade Event handler.
 *
 *  The host will enter degrade state due to the specified process
 *  not running properly. The process name is recorded in the
 *  'degraded_processes_list' for specified host.
 *  Clearing degrade against this process requires that host to
 *  send a clear event against that process or for that host to
 *  fully re-enable */
int nodeLinkClass::degrade_process_raise  ( string & hostname, 
                                            string & process )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host\n", hostname.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }
    else if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
    {
        if ( (node_ptr->degrade_mask & DEGRADE_MASK_PMON) == 0 )
        {
            node_ptr->degrade_mask |= DEGRADE_MASK_PMON ;
            wlog ("%s is degraded due to '%s' process failure\n", hostname.c_str(), process.c_str());
        }
    }
    return (PASS);
}

/* 
 * Name   : update_dport_states
 *
 * Purpose: Update data port states based on the event severity
 *
 * CLEAR    = enabled-available
 * MAJOR    = enabled-degraded
 * CRITICAL = disabled-failed
 *
 */
int update_dport_states_throttle = 0 ;
int nodeLinkClass::update_dport_states ( struct nodeLinkClass::node * node_ptr, int event )
{
    int rc = PASS ;

    /* if the host is locked then report the data ports as offline */
    if ( node_ptr->adminState == MTC_ADMIN_STATE__LOCKED )
    {
        event = MTC_EVENT_AVS_OFFLINE ;
    }

    switch (event)
    {
        case MTC_EVENT_AVS_OFFLINE:
        {
            if ( node_ptr->operState_dport != MTC_OPER_STATE__DISABLED )
            {
                ilog ("%s data port 'operState' change from '%s' -> 'disabled'",
                          node_ptr->hostname.c_str(),
                          operState_enum_to_str(node_ptr->operState_dport).c_str());

                node_ptr->operState_dport = MTC_OPER_STATE__DISABLED ;
            }

            if ( node_ptr->availStatus_dport != MTC_AVAIL_STATUS__OFFLINE )
            {
                ilog ("%s data port 'availStat' change from '%s' -> 'offline'",
                          node_ptr->hostname.c_str(),
                          availStatus_enum_to_str(node_ptr->availStatus_dport).c_str());

                node_ptr->availStatus_dport = MTC_AVAIL_STATUS__OFFLINE ;
            }
            break ;
        }
        case MTC_EVENT_AVS_CLEAR:
        {
            bool state_change = false ;
            if ( node_ptr->operState_dport != MTC_OPER_STATE__ENABLED )
            {
                ilog ("%s data port 'operState' change from '%s' -> 'enabled'",
                          node_ptr->hostname.c_str(),
                          operState_enum_to_str(node_ptr->operState_dport).c_str());

                node_ptr->operState_dport = MTC_OPER_STATE__ENABLED ;
                state_change = true ;
            }

            if ( node_ptr->availStatus_dport != MTC_AVAIL_STATUS__AVAILABLE )
            {
                ilog ("%s data port 'availStat' change from '%s' -> 'available'",
                          node_ptr->hostname.c_str(),
                          availStatus_enum_to_str(node_ptr->availStatus_dport).c_str());

                node_ptr->availStatus_dport = MTC_AVAIL_STATUS__AVAILABLE ;
                state_change = true ;
            }
            /** If there has been s state change as a result of a
              *  clear then send that to the VIM immediately
             **/
            if ( state_change == true )
            {
                /* Inform the VIM of the data port state change */
                mtcVimApi_state_change ( node_ptr, VIM_DPORT_CLEARED, 3 );
            }
            break ;
        }
        case MTC_EVENT_AVS_MAJOR:
        {
            if ( node_ptr->operState_dport != MTC_OPER_STATE__ENABLED )
            {
                ilog ("%s data port 'operState' change from '%s' -> 'enabled'",
                          node_ptr->hostname.c_str(),
                          operState_enum_to_str(node_ptr->operState_dport).c_str());

                node_ptr->operState_dport = MTC_OPER_STATE__ENABLED ;
            }

            if ( node_ptr->availStatus_dport != MTC_AVAIL_STATUS__DEGRADED )
            {
                wlog ("%s data port 'availStat' change from '%s' -> 'degraded'",
                          node_ptr->hostname.c_str(),
                          availStatus_enum_to_str(node_ptr->availStatus_dport).c_str());

                node_ptr->availStatus_dport = MTC_AVAIL_STATUS__DEGRADED ;
            }
            break ;
        }
        case MTC_EVENT_AVS_CRITICAL:
        {
            if ( node_ptr->operState_dport != MTC_OPER_STATE__DISABLED )
            {
                elog ("%s data port 'operState' change from '%s' -> 'disabled'",
                          node_ptr->hostname.c_str(),
                          operState_enum_to_str(node_ptr->operState_dport).c_str());

                node_ptr->operState_dport = MTC_OPER_STATE__DISABLED ;
            }

            if ( node_ptr->availStatus_dport != MTC_AVAIL_STATUS__FAILED )
            {
                elog ("%s data port 'availStat' change from '%s' -> 'failed'",
                          node_ptr->hostname.c_str(),
                          availStatus_enum_to_str(node_ptr->availStatus_dport).c_str());

                node_ptr->availStatus_dport = MTC_AVAIL_STATUS__FAILED ;
            }
            break ;
        }
        default:
        {
            wlog_throttled (update_dport_states_throttle, 10, "Invalid port state (%x)\n", event );
            rc = FAIL_BAD_CASE ;
        }
    }
    return (rc);
}

/** Resource Monitor 'Raise' Event handler.
 *
 *  The host will enter degrade state due to the specified resource
 *  threshold being surpased. The resource name is recorded in the
 *  'degraded_resources_list' for specified host.
 *  Clearing degrade against this resource requires that host to
 *  send a clear event against that resource or for that host to
 *  fully re-enable */
int nodeLinkClass::degrade_resource_raise  ( string & hostname,
                                            string & resource )
{
    /* lr - Log Prefix Rmond */
    string lr = hostname ;
    lr.append (" rmond:");

    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s Unknown Host\n", lr.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }
    else if ( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED )
    {
        if ( is_string_in_string_list ( node_ptr->degraded_resources_list, resource ) == false )
        {
            string degraded_resources = "";

            ilog ("%s '%s' Degraded\n", lr.c_str(), resource.c_str());
            node_ptr->degraded_resources_list.push_back (resource);
            node_ptr->degrade_mask |= DEGRADE_MASK_RESMON ;

            /* Cleanup the list */
            node_ptr->degraded_resources_list.sort ();
            node_ptr->degraded_resources_list.unique ();

            degraded_resources =
            get_strings_in_string_list ( node_ptr->degraded_resources_list );
            wlog ("%s Failing Resources: %s\n",
                      lr.c_str(), degraded_resources.c_str());
        }
        else
        {
            dlog ("%s '%s' Degraded (again)\n", lr.c_str(), resource.c_str());
        }
    }
    return (PASS);
}

/** Process Monitor 'Critical Process Failed' Event handler.
  *
  * This utility handles critical process failure event notifications.
  * Typically this interface will force a host re-enable through reset.
  *
  * For AIO Simplex this failure sets the auto recovery bool
  * so that the main enable FSM can handle it through a thresholded
  * self reboot.
  *
  * That as well as all other failure handling cases are deferred to
  * the enable handler's from failure case.
  *
  **/
int nodeLinkClass::critical_process_failed( string & hostname,
                                            string & process,
                                            unsigned int nodetype )
{
    UNUSED(nodetype);

    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s pmon: Unknown host\n", hostname.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }

    if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
        ( node_ptr->operState == MTC_OPER_STATE__ENABLED ))
    {
        elog ("%s has critical '%s' process failure\n", hostname.c_str(), process.c_str());

        node_ptr->degrade_mask |= DEGRADE_MASK_PMON ;

        /* Special critical process failure handling for AIO system */
        if ( THIS_HOST && ( is_inactive_controller_main_insv() == false ))
        {
            if ( node_ptr->ar_disabled == true )
            {
                dlog ("%s bypassing persistent critical process failure\n",
                          node_ptr->hostname.c_str());
                return (PASS);
            }

            dlog ("%s critical process failure (aio)\n",
                      node_ptr->hostname.c_str()); /* dlog */
        }

        /* Start fresh the next time we enter graceful recovery handler */
        node_ptr->graceful_recovery_counter = 0 ;

        /* Set node as unlocked-disabled-failed */
        allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED,
                                   MTC_OPER_STATE__DISABLED,
                                   MTC_AVAIL_STATUS__FAILED );

        enableStageChange ( node_ptr, MTC_ENABLE__FAILURE );
        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );

        dlog ("%s adminState:%s  EnableStage:%s\n",
                  node_ptr->hostname.c_str(),
                  adminAction_enum_to_str(node_ptr->adminAction).c_str(),
                  get_enableStages_str(node_ptr->enableStage).c_str());
    }
    return (PASS);
}

/** Resource Monitor 'Failed' Event handler.
  *
  *  The host will go out of service, be reset and 
  *  automatically re-enabled. */
int nodeLinkClass::critical_resource_failed( string & hostname, 
                                             string & resource )
{
    nodeLinkClass::node * node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        wlog ("%s rmond: Unknown host\n", hostname.c_str());
        return FAIL_UNKNOWN_HOSTNAME ;
    }

    if (( node_ptr->adminState == MTC_ADMIN_STATE__UNLOCKED ) &&
        ( node_ptr->operState == MTC_OPER_STATE__ENABLED ))
    {
        /* Start fresh the next time we enter graceful recovery handler */
        node_ptr->graceful_recovery_counter = 0 ;

        elog ("%s rmond: Critical Resource '%s' Failure\n", hostname.c_str(), resource.c_str());
        
        /* Set node as unlocked-enabled */
        allStateChange ( node_ptr, MTC_ADMIN_STATE__UNLOCKED, 
                                   MTC_OPER_STATE__DISABLED,
                                   MTC_AVAIL_STATUS__FAILED );
    }
        return (PASS);
}

bool nodeLinkClass::is_active_controller ( string hostname )
{
    if ( nodeLinkClass::my_hostname.compare(hostname) )
    {
        return (false) ;
    }
    return (true);
}

string nodeLinkClass::get_inactive_controller_hostname ( void )
{
    return (inactive_controller_hostname);
}

void nodeLinkClass::set_inactive_controller_hostname ( string hostname )
{
    inactive_controller_hostname = hostname ;
}

string nodeLinkClass::get_active_controller_hostname ( void )
{
    return (active_controller_hostname);
}

void nodeLinkClass::set_active_controller_hostname ( string hostname )
{
    active_controller_hostname = hostname ;
}

bool nodeLinkClass::inactive_controller_is_patched ( void )
{
    nodeLinkClass::node * node_ptr = getNode ( inactive_controller_hostname ) ;
    if ( node_ptr != NULL )
    {
        return ( node_ptr->patched );
    }
    return (false) ;
}

bool nodeLinkClass::inactive_controller_is_patching ( void )
{
    nodeLinkClass::node * node_ptr = getNode ( inactive_controller_hostname ) ;
    if ( node_ptr != NULL )
    {
        return ( node_ptr->patching );
    }
    return (false) ;
}

bool nodeLinkClass::is_inactive_controller_main_insv ( void )
{
    nodeLinkClass::node * node_ptr = getNode ( inactive_controller_hostname ) ;
    if ( node_ptr != NULL )
    {
        if ( node_ptr->operState == MTC_OPER_STATE__ENABLED )
        {
            return (true) ;
        }
    }
    return (false) ;
}

bool nodeLinkClass::is_inactive_controller_subf_insv ( void )
{
    nodeLinkClass::node * node_ptr = getNode ( inactive_controller_hostname ) ;
    if ( node_ptr != NULL )
    {
        if ( node_ptr->operState_subf   == MTC_OPER_STATE__ENABLED )
        {
            return (true) ;
        }
    }
    return (false) ;
}

int nodeLinkClass::set_subf_info ( string hostname,
                                   string functions,
                                   string operState_subf,
                                   string availState_subf )
{
    int rc = FAIL_HOSTNAME_LOOKUP ;
    if ( functions.empty() )
    {
        elog ("%s called with empty 'functions' string\n", hostname.c_str());
        return (FAIL_STRING_EMPTY);
    }

    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr != NULL )
    {
        node_ptr->functions        = functions ;
        node_ptr->operState_subf   = operState_str_to_enum(operState_subf.data());
        node_ptr->availStatus_subf = availStatus_str_to_enum(availState_subf.data());
        rc = update_host_functions ( hostname,  functions );
    }
    return (rc);
}



/********************************************************************************** 
 *
 * Name   : update_host_functions
 *
 * Purpose: Loads a nodeLinkClass with function information based on a comma
 *          delimited function string like.
 *
 *      controller
 *      compute
 *      storage
 *      controller,compute
 *      controller,storage
 *
 **********************************************************************************/
int nodeLinkClass::update_host_functions ( string hostname , string functions )
{
    int rc = FAIL ;

    if ( functions.empty() )
    {
        elog ("%s called with empty 'functions' string\n", hostname.c_str());
        return (rc);
    }

    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr != NULL )
    {
        node_ptr->functions = functions ;
        if ( set_host_functions ( functions, &node_ptr->nodetype, &node_ptr->function, &node_ptr->subfunction ) != PASS )
        {
            elog ("%s failed to extract nodetype\n", hostname.c_str());
            rc = FAIL_NODETYPE;
        }
        else
        {
            if ( node_ptr->function == CONTROLLER_TYPE )
                 node_ptr->function_str = "controller" ;
            else if ( node_ptr->function == COMPUTE_TYPE )
                 node_ptr->function_str = "compute" ;
            else if ( node_ptr->function == STORAGE_TYPE )
                 node_ptr->function_str = "storage" ;
            else
                 node_ptr->function_str = "" ;

            if ( node_ptr->subfunction == COMPUTE_TYPE )
            {
                node_ptr->subfunction_str = "compute" ;
            }
            else if ( node_ptr->subfunction == STORAGE_TYPE )
            {
                node_ptr->subfunction_str = "storage" ;
            }
            else
                 node_ptr->subfunction_str = "" ;
        }
        rc = PASS ;
    }
    return (rc);
}





/** Fetch the node type (compute or controller) by hostname */
int nodeLinkClass::get_nodetype ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr != NULL )
    {
        return ( node_ptr->nodetype );
    }
    return (false);
}

/** Check if a node is a controller */
bool nodeLinkClass::is_controller ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr != NULL )
    {
        if ( (node_ptr->function & CONTROLLER_TYPE ) == CONTROLLER_TYPE )
        {
            return (true);
        }
    }
    return (false);
}

/** Check if a node is a compute */
bool nodeLinkClass::is_compute_subfunction ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr != NULL )
    {
        if ( (node_ptr->subfunction & COMPUTE_TYPE ) == COMPUTE_TYPE )
        {
            return (true);
        }
    }
    return (false);
}

/** Check if a node is a compute */
bool nodeLinkClass::is_compute ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr != NULL )
    {
        if ( (node_ptr->function & COMPUTE_TYPE ) == COMPUTE_TYPE )
        {
            return (true);
        }
    }
    return (false);
}

/** Check if a node is a storage */
bool nodeLinkClass::is_storage ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr != NULL )
    {
        if ( (node_ptr->function & STORAGE_TYPE ) == STORAGE_TYPE )
        {
            return (true);
        }
    }
    return (false);
}

string nodeLinkClass::get_node_function_str ( string hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return node_ptr->function_str ;
    }
    return "unknown" ;
}

string nodeLinkClass::get_node_subfunction_str ( string hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname );
    if ( node_ptr != NULL )
    {
        return node_ptr->subfunction_str ;
    }
    return "unknown" ;
}

/** Check if a node is a controller */
bool nodeLinkClass::is_controller ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname );
    if ( node_ptr )
    {
        return is_controller(node_ptr);
    }
    return false ;
}

/** Check if a node is a compute */
bool nodeLinkClass::is_compute ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname );
    if ( node_ptr )
    {
        return is_compute(node_ptr);
    }
    return false ;
}

/** Check if a node is a compute */
bool nodeLinkClass::is_compute_subfunction ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname );
    if ( node_ptr )
    {
        return is_compute_subfunction(node_ptr);
    }
    return false ;
}

/** Check if a node is a storage */
bool nodeLinkClass::is_storage ( string & hostname )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname );
    if ( node_ptr )
    {
        return is_storage(node_ptr);
    }
    return false ;
}

/** Maintenance FSM Test Case Setup procedure */
int nodeLinkClass::set_enableStage ( string & hostname,
                                     mtc_enableStages_enum stage )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr != NULL )
    {
        node_ptr->enableStage = stage ;
        return (PASS);
    }
    return (FAIL);
} 

/* Set the reboot stage */
int nodeLinkClass::set_rebootStage ( string & hostname, mtc_resetProgStages_enum stage )
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr != NULL )
    {
        node_ptr->resetProgStage = stage ;
        return (PASS);
    }
    return (FAIL);
}

/** Maintenance FSM Test Case Setup procedure */
mtc_enableStages_enum nodeLinkClass::get_enableStage ( string & hostname)
{
    nodeLinkClass::node * node_ptr = getNode ( hostname ) ;
    if ( node_ptr != NULL )
    {
        return ( node_ptr->enableStage ) ;
    }
    return (MTC_ENABLE__STAGES);
}

int nodeLinkClass::allStateChange ( struct nodeLinkClass::node * node_ptr,
                                    mtc_nodeAdminState_enum adminState,
                                    mtc_nodeOperState_enum  operState,
                                    mtc_nodeAvailStatus_enum availStatus )
{
    int  rc     = FAIL  ;

    if (( adminState  < MTC_ADMIN_STATES ) &&
        ( operState   < MTC_OPER_STATES ) &&
        ( availStatus < MTC_AVAIL_STATUS ))
    {
        bool change = false ;
        if (( node_ptr->adminState  != adminState ) ||
            ( node_ptr->operState   != operState ) ||
            ( node_ptr->availStatus != availStatus ))
        {
            change = true ;
        }

        string admin = mtc_nodeAdminState_str [adminState ] ;
        string oper  = mtc_nodeOperState_str  [operState  ] ;
        string avail = mtc_nodeAvailStatus_str[availStatus] ;

        rc = mtcInvApi_force_states ( node_ptr, admin, oper, avail );

        admin_state_change  ( node_ptr->hostname, admin );

        if ((( operState   == MTC_OPER_STATE__DISABLED ) && ( node_ptr->operState   != MTC_OPER_STATE__DISABLED )) &&
            (( availStatus == MTC_AVAIL_STATUS__FAILED ) && ( node_ptr->availStatus != MTC_AVAIL_STATUS__FAILED )))
        {
           failed_state_change ( node_ptr );
        }
        else
        {
            oper_state_change   ( node_ptr->hostname, oper  );
            avail_status_change ( node_ptr->hostname, avail );
        }

        if ( change == true )
        {
            /* after */
            ilog ("%s %s-%s-%s (seq:%d)\n",
                      node_ptr->hostname.c_str(),
                      mtc_nodeAdminState_str [node_ptr->adminState ],
                      mtc_nodeOperState_str  [node_ptr->operState  ],
                      mtc_nodeAvailStatus_str[node_ptr->availStatus],
                      node_ptr->oper_sequence-1);
        }
    }
    else
    {
        slog ("Invalid State (%d:%d:%d)\n", adminState, operState, availStatus );
    }
    return (rc);
}

int nodeLinkClass::subfStateChange ( struct nodeLinkClass::node * node_ptr,
                                     mtc_nodeOperState_enum   operState_subf,
                                     mtc_nodeAvailStatus_enum availStatus_subf )
{
    int  rc     = FAIL  ;

    if (( operState_subf   < MTC_OPER_STATES ) &&
        ( availStatus_subf < MTC_AVAIL_STATUS ))
    {
        bool change = false ;
        if (( node_ptr->operState_subf   != operState_subf ) ||
            ( node_ptr->availStatus_subf != availStatus_subf ))
        {
            change = true ;
        }

        string oper  = mtc_nodeOperState_str  [operState_subf  ] ;
        string avail = mtc_nodeAvailStatus_str[availStatus_subf] ;

        rc = mtcInvApi_subf_states ( node_ptr, oper, avail );

        node_ptr->operState_subf   = operState_subf  ;
        node_ptr->availStatus_subf = availStatus_subf;

        if ( change == true )
        {
            /* after */
            ilog ("%s-%s %s-%s-%s (seq:%d)\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->subfunction_str.c_str(),
                      mtc_nodeAdminState_str [node_ptr->adminState ],
                      mtc_nodeOperState_str  [node_ptr->operState_subf  ],
                      mtc_nodeAvailStatus_str[node_ptr->availStatus_subf],
                      node_ptr->oper_sequence-1);
        }
    }
    else
    {
        slog ("Invalid State (%d:%d:%d)\n", node_ptr->adminState, availStatus_subf, availStatus_subf );
    }
    return (rc);
}





/**
 *  Set the required action and then let the FSP and handlers deal with it
 *  If we are in an action already then just add the action to the
 *  action todo list. When we chnage the action to none then query the
 *  todo list and pop it off and apply it
 **/
int nodeLinkClass::adminActionChange ( struct nodeLinkClass::node * node_ptr,
                                       mtc_nodeAdminAction_enum newActionState )
{
    int rc = FAIL ;


    if ((        newActionState < MTC_ADMIN_ACTIONS ) &&
        ( node_ptr->adminAction < MTC_ADMIN_ACTIONS ))
    {
        rc = PASS ;

        if ( node_ptr->adminAction == newActionState )
        {
            /* no action change */
            return (rc);
        }

        /**
         *  Any of these actions need to complete before any
         *  other action can take effect.
         *  If its not one of these action then just proceed with it
         **/
        if (( node_ptr->adminAction != MTC_ADMIN_ACTION__ADD ) &&
            ( node_ptr->adminAction != MTC_ADMIN_ACTION__FORCE_LOCK ))
        {
            clog ("%s Administrative Action '%s' -> '%s'\n",
                      node_ptr->hostname.c_str(),
                      mtc_nodeAdminAction_str [node_ptr->adminAction],
                      mtc_nodeAdminAction_str [newActionState]);
        }
        /* handle queue'd requests if here are any and
         * we are done with the curent action */
        else if (( newActionState == MTC_ADMIN_ACTION__NONE ) &&
            ( !node_ptr->adminAction_todo_list.empty()))
        {
            newActionState = *(node_ptr->adminAction_todo_list.begin());
            node_ptr->adminAction_todo_list.pop_front();

            clog ("%s Administrative Action '%s' -> '%s' from queue\n",
                      node_ptr->hostname.c_str(),
                      mtc_nodeAdminAction_str [node_ptr->adminAction],
                      mtc_nodeAdminAction_str [newActionState]);
        }
        /* queue the request if we are already acting on a current action
         * ... handle unsupported action queueing conditions */
        else if (( node_ptr->adminAction != MTC_ADMIN_ACTION__NONE ) &&
                 (        newActionState != MTC_ADMIN_ACTION__NONE ))
        {
            /* refuse to add duplicate action */
            if ( newActionState == node_ptr->adminAction )
            {
                wlog ("%s refusing to queue duplicate of current action (%s)\n",
                          node_ptr->hostname.c_str(),
                          mtc_nodeAdminAction_str [node_ptr->adminAction] );

                return (FAIL);
            }
            else if ( node_ptr->adminAction_todo_list.size() >= MTC_MAX_QUEUED_ACTIONS )
            {
                wlog ("%s rejecting action '%s' request ; max queued actions reached (%ld of %d)\n",
                          node_ptr->hostname.c_str(),
                          mtc_nodeAdminAction_str [newActionState],
                          node_ptr->adminAction_todo_list.size(),
                          MTC_MAX_QUEUED_ACTIONS );
                return (FAIL);
            }

            /* refuse to queue action that already exists in the queue */
            else
            {
                list<mtc_nodeAdminAction_enum>::iterator adminAction_todo_list_ptr ;
                for ( adminAction_todo_list_ptr = node_ptr->adminAction_todo_list.begin();
                      adminAction_todo_list_ptr != node_ptr->adminAction_todo_list.end();
                      adminAction_todo_list_ptr++ )
                {
                    if ( *adminAction_todo_list_ptr == newActionState )
                    {
                        wlog ("%s refusing to queue duplicate already queued action (%s)\n",
                                  node_ptr->hostname.c_str(),
                                  mtc_nodeAdminAction_str [*adminAction_todo_list_ptr]);

                        return (FAIL);
                    }
                }
            }
            /* Add the action to the action todo list */
            node_ptr->adminAction_todo_list.push_back( newActionState );

            ilog ("%s Administrative Action '%s' queued ; already handling '%s' action\n",
                  node_ptr->hostname.c_str(),
                  mtc_nodeAdminAction_str [newActionState],
                  mtc_nodeAdminAction_str [node_ptr->adminAction]);
            return (PASS);
        }
        /* otherwise just take the action change */
        else
        {
            clog ("%s Administrative Action '%s' -> '%s'\n",
                      node_ptr->hostname.c_str(),
                      mtc_nodeAdminAction_str [node_ptr->adminAction],
                      mtc_nodeAdminAction_str [newActionState]);
        }

        mtc_nodeAdminAction_enum oldActionState = node_ptr->adminAction ;
        log_adminAction ( node_ptr->hostname, oldActionState, newActionState );

        node_ptr->adminAction = newActionState ;
        node_ptr->action      = mtc_nodeAdminAction_str [node_ptr->adminAction] ;

        /* If we are starting a new ( not 'none' ) action ...
         * be sure we start at the beginning */
        if ( newActionState != MTC_ADMIN_ACTION__NONE )
        {
            if (( oldActionState == MTC_ADMIN_ACTION__POWERCYCLE ) &&
                (( newActionState != MTC_ADMIN_ACTION__POWERCYCLE ) &&
                 ( newActionState != MTC_ADMIN_ACTION__POWEROFF )))
            {
                blog ("%s (mon:%d:prov:%d)\n", node_ptr->hostname.c_str(), node_ptr->hwmond_monitor, node_ptr->bm_provisioned );

                if (( node_ptr->hwmond_monitor == false ) && ( node_ptr->bm_provisioned == true ))
                {
                    send_hwmon_command ( node_ptr->hostname, MTC_CMD_ADD_HOST   );
                    send_hwmon_command ( node_ptr->hostname, MTC_CMD_START_HOST );
                }
            }
            /* Lets ensure that the handlers start in the right stage
             * The enable_handler  -> MTC_ENABLE__START
             * The disable_handler -> MTC_DISABLE__START
             * The reset_handler   -> MTC_RESET__START
             * The reboot_handler  -> MTC_RESET__START
             *
             * This is a little detailed but exists for maintainability
             * All START stages are 0.
             */
            switch ( newActionState )
            {
                case MTC_ADMIN_ACTION__UNLOCK:
                {
                    if ( oldActionState != MTC_ADMIN_ACTION__UNLOCK )
                    {
                        node_ptr->node_unlocked_counter++ ;
                    }

                    ar_enable (node_ptr);

                    node_ptr->enableStage = MTC_ENABLE__START ;
                    break ;
                }
                case MTC_ADMIN_ACTION__LOCK:
                case MTC_ADMIN_ACTION__FORCE_LOCK:
                {
                    node_ptr->disableStage = MTC_DISABLE__START ;
                    break ;
                }
                case MTC_ADMIN_ACTION__RESET:
                {
                    node_ptr->resetStage = MTC_RESET__START ;
                    break ;
                }
                case MTC_ADMIN_ACTION__REBOOT:
                {
                    break ;
                }
                case MTC_ADMIN_ACTION__REINSTALL:
                {
                    node_ptr->reinstallStage = MTC_REINSTALL__START ;
                    break ;
                }
                case MTC_ADMIN_ACTION__POWERON:
                {
                    node_ptr->powerStage = MTC_POWERON__START ;
                    break ;
                }
                case MTC_ADMIN_ACTION__RECOVER:
                {
                    if ( node_ptr->mtcTimer.tid )
                    {
                        mtcTimer_stop ( node_ptr->mtcTimer ) ;
                    }
                    if ( node_ptr->mtcSwact_timer.tid )
                    {
                        mtcTimer_stop ( node_ptr->mtcSwact_timer ) ;
                    }
                    node_ptr->recoveryStage = MTC_RECOVERY__START ;
                    break ;
                }

                case MTC_ADMIN_ACTION__POWEROFF:
                {
                    node_ptr->powerStage = MTC_POWEROFF__START ;
                    break ;
                }
                case MTC_ADMIN_ACTION__DELETE:
                {
                    node_ptr->delStage = MTC_DEL__START ;
                    break ;
                }
                case MTC_ADMIN_ACTION__ENABLE:
                default:
                {
                    break ;
                }
            }
        }
    }
    return (rc);
}

int nodeLinkClass::adminStateChange ( struct nodeLinkClass::node * node_ptr,
                                     mtc_nodeAdminState_enum newAdminState )
{
    int rc = FAIL ;

    if ((        newAdminState < MTC_ADMIN_STATES ) &&
        ( node_ptr->adminState < MTC_ADMIN_STATES ))
    {
        rc = PASS ;

        /* See if we are actually changing the state */
        if ( node_ptr->adminState != newAdminState )
        {
            ilog ("%s %s-%s-%s' -> %s-%s-%s\n", node_ptr->hostname.c_str(),
                                   mtc_nodeAdminState_str [node_ptr->adminState],
                                   mtc_nodeOperState_str  [node_ptr->operState],
                                   mtc_nodeAvailStatus_str[node_ptr->availStatus],
                                   mtc_nodeAdminState_str [newAdminState],
                                   mtc_nodeOperState_str  [node_ptr->operState],
                                   mtc_nodeAvailStatus_str[node_ptr->availStatus]);
            node_ptr->adminState = newAdminState ;
        }
    }
    else
    {
        slog ("Invalid Host Operational State (now:%d new:%d)\n",
               node_ptr->adminState, newAdminState );
    }
    return (rc);
}


int nodeLinkClass::operStateChange ( struct nodeLinkClass::node * node_ptr,
                                     mtc_nodeOperState_enum newOperState )
{
    int rc = FAIL ;

    if ((        newOperState < MTC_OPER_STATES ) &&
        ( node_ptr->operState < MTC_OPER_STATES ))
    {
        rc = PASS ;

        /* See if we are actually changing the state */
        if ( node_ptr->operState != newOperState )
        {
            clog ("%s %s-%s-%s\n", node_ptr->hostname.c_str(),
                                   mtc_nodeAdminState_str [node_ptr->adminState],
                                   mtc_nodeOperState_str  [node_ptr->operState],
                                   mtc_nodeAvailStatus_str[node_ptr->availStatus]);

            /* Push it to the database */
            if ( node_ptr->uuid.length() == UUID_LEN )
            {
                string key   = MTC_JSON_INV_OPER ;
                string value = operState_enum_to_str(newOperState) ;
                rc = mtcInvApi_update_state ( node_ptr, key, value );
            }
            else
            {
                wlog ("%s has invalid uuid:%s so %s state not written to database\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->uuid.c_str(),
                      operState_enum_to_str(newOperState).c_str());
            }

            node_ptr->operState = newOperState ;

            clog ("%s %s-%s-%s\n", node_ptr->hostname.c_str(),
                                   mtc_nodeAdminState_str [node_ptr->adminState],
                                   mtc_nodeOperState_str  [node_ptr->operState],
                                   mtc_nodeAvailStatus_str[node_ptr->availStatus]);
        }
    }
    else
    {
        slog ("Invalid Host Operational State (now:%d new:%d)\n",
               node_ptr->operState, newOperState );
    }
    return (rc);
}

int nodeLinkClass::availStatusChange ( struct nodeLinkClass::node * node_ptr,
                                       mtc_nodeAvailStatus_enum newAvailStatus )
{
    int rc = FAIL ;

    if ((        newAvailStatus < MTC_AVAIL_STATUS ) &&
        ( node_ptr->availStatus < MTC_AVAIL_STATUS ))
    {
        rc = PASS ;

        /* See if we are actually changing the state */
        if ( node_ptr->availStatus != newAvailStatus )
        {
            clog ("%s %s-%s-%s\n", node_ptr->hostname.c_str(),
                                   mtc_nodeAdminState_str [node_ptr->adminState],
                                   mtc_nodeOperState_str  [node_ptr->operState],
                                   mtc_nodeAvailStatus_str[node_ptr->availStatus]);

            /* Push it to the database */
            if ( node_ptr->uuid.length() == UUID_LEN )
            {
                string key   = MTC_JSON_INV_AVAIL ;
                string value = availStatus_enum_to_str(newAvailStatus) ;
                rc = mtcInvApi_update_state ( node_ptr, key, value );
                if ( rc != PASS )
                {
                    wlog ("%s Failed to update availability '%s' to '%s'\n",
                              node_ptr->hostname.c_str(),
                              mtc_nodeAvailStatus_str[node_ptr->availStatus],
                              mtc_nodeAvailStatus_str[newAvailStatus]);
                }
            }
            else
            {
                wlog ("%s has invalid uuid:%s so %s state not written to database\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->uuid.c_str(),
                      availStatus_enum_to_str(newAvailStatus).c_str());
            }

            if (( node_ptr->operState == MTC_OPER_STATE__ENABLED ) &&
                (( node_ptr->availStatus == MTC_AVAIL_STATUS__AVAILABLE ) ||
                 ( node_ptr->availStatus == MTC_AVAIL_STATUS__DEGRADED )) &&
                 ( newAvailStatus == MTC_AVAIL_STATUS__FAILED ))
            {
                enableStageChange ( node_ptr, MTC_ENABLE__START );
            }

                        /* if we go to the failed state then clear all mtcAlive counts
             * so that the last ones don't look like we are online when we
             * might not be - we should relearn the on/off line state */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__FAILED ) &&
                (        newAvailStatus == MTC_AVAIL_STATUS__FAILED ))
            {
                node_ptr->mtcAlive_misses = 0 ;
                node_ptr->mtcAlive_hits   = 0 ;
                node_ptr->mtcAlive_gate   = false ;
            }

            /* check for need to generate power on log */
            if (( node_ptr->availStatus == MTC_AVAIL_STATUS__POWERED_OFF ) &&
                (       newAvailStatus  != MTC_AVAIL_STATUS__POWERED_OFF ))
            {
                if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWERON )
                {
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_MANUAL_POWER_ON );
                }
                else
                {
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_AUTO_POWER_ON );
                }
            }

            /* check for need to generate power off log */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__POWERED_OFF ) &&
                (        newAvailStatus == MTC_AVAIL_STATUS__POWERED_OFF ))
            {
                if ( node_ptr->adminAction == MTC_ADMIN_ACTION__POWEROFF )
                {
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_MANUAL_POWER_OFF );
                }
                else
                {
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_AUTO_POWER_OFF );
                }
            }

            /* check for need to generate online log */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__ONLINE ) &&
                (        newAvailStatus == MTC_AVAIL_STATUS__ONLINE ))
            {
                if ( node_ptr->offline_log_reported == true )
                {
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_ONLINE );
                    node_ptr->offline_log_reported = false ;
                    node_ptr->online_log_reported = true ;
                }
            }

            /* check for need to generate offline log */
            if (( node_ptr->availStatus != MTC_AVAIL_STATUS__OFFLINE ) &&
                (        newAvailStatus == MTC_AVAIL_STATUS__OFFLINE ))
            {
                if ( node_ptr->online_log_reported == true )
                {
                    mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__STATUSCHANGE_OFFLINE );
                    node_ptr->offline_log_reported = true  ;
                    node_ptr->online_log_reported  = false ;
                }
            }

            /* If the availability status is moving away from off or online then
             * be sure we cancel the mtcAlive timer */
            if ((( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE ) ||
                (  node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE )) &&
                ((        newAvailStatus != MTC_AVAIL_STATUS__OFFLINE ) &&
                 (        newAvailStatus != MTC_AVAIL_STATUS__ONLINE )))
            {
                /* Free the mtc timer if in use */
                if ( node_ptr->mtcAlive_timer.tid )
                {
                    tlog ("%s Stopping mtcAlive timer\n", node_ptr->hostname.c_str());
                    mtcTimer_stop ( node_ptr->mtcAlive_timer );
                    node_ptr->mtcAlive_timer.ring = false ;
                    node_ptr->mtcAlive_timer.tid  = NULL  ;
                }
                node_ptr->onlineStage = MTC_ONLINE__START ;
            }


            clog ("%s %s-%s-%s\n", node_ptr->hostname.c_str(),
                                   mtc_nodeAdminState_str [node_ptr->adminState],
                                   mtc_nodeOperState_str  [node_ptr->operState],
                                   mtc_nodeAvailStatus_str[node_ptr->availStatus]);

            node_ptr->availStatus = newAvailStatus ;
        }
    }
    else
    {
        slog ("Invalid Host Availability Status (now:%d new:%d)\n",
               node_ptr->availStatus, newAvailStatus );
    }
    return (rc);
}

/** Host Enable Handler Stage Change member function */
int nodeLinkClass::enableStageChange ( struct nodeLinkClass::node * node_ptr,
                                       mtc_enableStages_enum newHdlrStage )
{
    /* TODO: Consider converting stage to strings ... */
    if (( newHdlrStage >= MTC_ENABLE__STAGES ) ||
        ( node_ptr->enableStage >= MTC_ENABLE__STAGES ))
    {
       slog ("%s has invalid Enable stage (%d:%d)\n",
              node_ptr->hostname.c_str(),
              node_ptr->enableStage,
              newHdlrStage );

       node_ptr->enableStage = MTC_ENABLE__FAILURE ;

       /* TODO: cause failed or degraded state ? */
       return (FAIL);
    }
    else if ( node_ptr->enableStage != newHdlrStage )
    {
        clog ("%s %s -> %s\n",
               node_ptr->hostname.c_str(),
               get_enableStages_str(node_ptr->enableStage).c_str(),
               get_enableStages_str(newHdlrStage).c_str());

        node_ptr->enableStage = newHdlrStage  ;
        return (PASS);
    }
    else
    {
        /* No state change */
        dlog1 ("%s %s -> %s\n",
               node_ptr->hostname.c_str(),
               get_enableStages_str(node_ptr->enableStage).c_str(),
               get_enableStages_str(newHdlrStage).c_str());
        return (PASS);
    }
}

/** Host Disable Handler Stage Change member function */
int nodeLinkClass::disableStageChange ( struct nodeLinkClass::node * node_ptr, 
                                        mtc_disableStages_enum newHdlrStage )
{
    /* TODO: Consider converting stage to strings ... */
    if (( newHdlrStage >= MTC_DISABLE__STAGES ) ||
        ( node_ptr->disableStage >= MTC_DISABLE__STAGES ))
    {
       slog ("%s has invalid disable stage (%d:%d)\n",
              node_ptr->hostname.c_str(),
              node_ptr->disableStage,
              newHdlrStage );

       node_ptr->disableStage = MTC_DISABLE__DISABLED ;

       /* TODO: cause failed or degraded state ? */
       return (FAIL);
    }
    else 
    {
        clog ("%s %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_disableStages_str(node_ptr->disableStage).c_str(), 
               get_disableStages_str(newHdlrStage).c_str());

        node_ptr->disableStage = newHdlrStage  ;
        return (PASS);
    }
}

/** Validate and log Recovery stage changes */
int nodeLinkClass::recoveryStageChange  ( struct nodeLinkClass::node * node_ptr, 
                                          mtc_recoveryStages_enum newHdlrStage )
{
    int rc = PASS ;

    if (( newHdlrStage >= MTC_RECOVERY__STAGES ) ||
        ( node_ptr->recoveryStage >= MTC_RECOVERY__STAGES ))
    {
        slog ("%s Invalid recovery stage (%d:%d)\n", 
                  node_ptr->hostname.c_str(),
                  node_ptr->recoveryStage, 
                  newHdlrStage );

        if ( newHdlrStage < MTC_RECOVERY__STAGES )
        {
            clog ("%s ? -> %s\n", 
               node_ptr->hostname.c_str(),
               get_recoveryStages_str(newHdlrStage).c_str());

            node_ptr->recoveryStage = newHdlrStage ;
        }
        else
        {
            node_ptr->recoveryStage = MTC_RECOVERY__FAILURE ;
            rc = FAIL ;
        }
    }
    else 
    {
        clog ("%s %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_recoveryStages_str(node_ptr->recoveryStage).c_str(), 
               get_recoveryStages_str(newHdlrStage).c_str());

        node_ptr->recoveryStage = newHdlrStage  ;
    }
    return (rc) ;
}


/** Validate and log Recovery stage changes */
int nodeLinkClass::configStageChange  ( struct nodeLinkClass::node * node_ptr, 
                                          mtc_configStages_enum newHdlrStage )
{
    int rc = PASS ;

    if (( newHdlrStage >= MTC_CONFIG__STAGES ) ||
        ( node_ptr->configStage >= MTC_CONFIG__STAGES ))
    {
        slog ("%s Invalid config stage (%d:%d)\n", 
                  node_ptr->hostname.c_str(),
                  node_ptr->configStage, 
                  newHdlrStage );

        if ( newHdlrStage < MTC_CONFIG__STAGES )
        {
            clog ("%s ? -> %s\n", 
               node_ptr->hostname.c_str(),
               get_configStages_str(newHdlrStage).c_str());

            node_ptr->configStage = newHdlrStage ;
        }
        else
        {
            node_ptr->configStage = MTC_CONFIG__FAILURE ;
            rc = FAIL ;
        }
    }
    else 
    {
        clog ("%s %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_configStages_str(node_ptr->configStage).c_str(), 
               get_configStages_str(newHdlrStage).c_str());

        node_ptr->configStage = newHdlrStage  ;
    }
    return (rc) ;
}

/** Host Reset Handler Stage Change member function */
int nodeLinkClass::resetStageChange ( struct nodeLinkClass::node * node_ptr,
                                      mtc_resetStages_enum newHdlrStage )
{
    if ( newHdlrStage < MTC_RESET__STAGES )
    {
        clog ("%s stage %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_resetStages_str(node_ptr->resetStage).c_str(),  
               get_resetStages_str(newHdlrStage).c_str());

        node_ptr->resetStage = newHdlrStage ;
        return (PASS) ;
    }
    else
    {
        slog ("%s Invalid reset stage (%d)\n", node_ptr->hostname.c_str(), newHdlrStage );
        node_ptr->resetStage = MTC_RESET__DONE ;
        return (FAIL) ;
    }
}

/* Host Reset Handler Stage Change member function */
int nodeLinkClass::reinstallStageChange ( struct nodeLinkClass::node * node_ptr,
                                      mtc_reinstallStages_enum newHdlrStage )
{
    if ( newHdlrStage < MTC_REINSTALL__STAGES )
    {
        clog ("%s stage %s -> %s\n",
               node_ptr->hostname.c_str(),
               get_reinstallStages_str(node_ptr->reinstallStage).c_str(),
               get_reinstallStages_str(newHdlrStage).c_str());

        node_ptr->reinstallStage = newHdlrStage ;
        return (PASS) ;
    }
    else
    {
        slog ("%s Invalid reinstall stage (%d)\n", node_ptr->hostname.c_str(), newHdlrStage );
        node_ptr->reinstallStage = MTC_REINSTALL__DONE ;
        return (FAIL) ;
    }
}

/** Host Power control Handler Stage Change member function */
int nodeLinkClass::powerStageChange ( struct nodeLinkClass::node * node_ptr, 
                                      mtc_powerStages_enum newHdlrStage )
{
    if ( newHdlrStage < MTC_POWER__STAGES )
    {
        clog ("%s stage %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_powerStages_str(node_ptr->powerStage).c_str(),  
               get_powerStages_str(newHdlrStage).c_str());

        node_ptr->powerStage = newHdlrStage ;
        return (PASS) ;
    }
    else
    {
        slog ("%s Invalid power control stage (%d)\n",
                  node_ptr->hostname.c_str(), newHdlrStage );
        node_ptr->powerStage = MTC_POWER__DONE ;
        return (FAIL) ;
    }
}

/** Host Power Cycle control Handler Stage Change member function */
int nodeLinkClass::powercycleStageChange ( struct nodeLinkClass::node * node_ptr, 
                                           mtc_powercycleStages_enum newHdlrStage )
{
    if ( newHdlrStage < MTC_POWERCYCLE__STAGES )
    {
        clog ("%s stage %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_powercycleStages_str(node_ptr->powercycleStage).c_str(),  
               get_powercycleStages_str(newHdlrStage).c_str());

        node_ptr->powercycleStage = newHdlrStage ;
        return (PASS) ;
    }
    else
    {
        slog ("%s Invalid powercycle stage (%d)\n",
                  node_ptr->hostname.c_str(), newHdlrStage );
        node_ptr->powercycleStage = MTC_POWERCYCLE__DONE ;
        return (FAIL) ;
    }
}


/** Host Out-Of-Service Stage Change member function */
int nodeLinkClass::oosTestStageChange ( struct nodeLinkClass::node * node_ptr,
                                        mtc_oosTestStages_enum newHdlrStage )
{
    if ( newHdlrStage < MTC_OOS_TEST__STAGES )
    {
        clog ("%s stage %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_oosTestStages_str(node_ptr->oosTestStage).c_str(),  
               get_oosTestStages_str(newHdlrStage).c_str());

        node_ptr->oosTestStage = newHdlrStage ;
        return (PASS) ;
    }
    else
    {
        slog ("%s Invalid oos test stage (%d)\n", node_ptr->hostname.c_str(), newHdlrStage );
        node_ptr->oosTestStage = MTC_OOS_TEST__DONE ;
        return (FAIL) ;
    }
}

/** Host in-Service Stage Change member function */
int nodeLinkClass::insvTestStageChange ( struct nodeLinkClass::node * node_ptr,
                                         mtc_insvTestStages_enum newHdlrStage )
{
    if ( newHdlrStage < MTC_INSV_TEST__STAGES )
    {
        clog ("%s stage %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_insvTestStages_str(node_ptr->insvTestStage).c_str(),  
               get_insvTestStages_str(newHdlrStage).c_str());

        node_ptr->insvTestStage = newHdlrStage ;
        return (PASS) ;
    }
    else
    {
        slog ("%s Invalid insv test stage (%d)\n", node_ptr->hostname.c_str(), newHdlrStage );
        node_ptr->insvTestStage = MTC_INSV_TEST__START ;
        return (FAIL) ;
    }
}

/** SubStage Change member function */
int nodeLinkClass::subStageChange  ( struct nodeLinkClass::node * node_ptr, 
                                     mtc_subStages_enum   newHdlrStage )
{
    if ( newHdlrStage < MTC_SUBSTAGE__STAGES )
    {
        clog ("%s stage %s -> %s\n", 
               node_ptr->hostname.c_str(),
               get_subStages_str(node_ptr->subStage).c_str(),  
               get_subStages_str(newHdlrStage).c_str());

        node_ptr->subStage = newHdlrStage ;
        return (PASS) ;
    }
    else
    {
        slog ("%s Invalid 'subStage' stage (%d)\n", 
                  node_ptr->hostname.c_str(), newHdlrStage );
        node_ptr->subStage = MTC_SUBSTAGE__DONE ;
        return (FAIL) ;
    }
}

struct nodeLinkClass::node * nodeLinkClass::get_mtcTimer_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->mtcTimer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_mtcCmd_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->mtcCmd_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_host_services_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->host_services_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_http_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->http_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    } 
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_thread_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->ipmitool_thread_ctrl.timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_ping_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->bm_ping_info.timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_bm_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->bm_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_bmc_access_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->bmc_access_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}



struct nodeLinkClass::node * nodeLinkClass::get_mtcConfig_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->mtcConfig_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    } 
    return static_cast<struct node *>(NULL);
}   

struct nodeLinkClass::node * nodeLinkClass::get_powercycle_control_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->hwmon_powercycle.control_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_reset_control_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->hwmon_reset.control_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_powercycle_recovery_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->hwmon_powercycle.recovery_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_reset_recovery_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->hwmon_reset.recovery_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_mtcAlive_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->mtcAlive_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    } 
    return static_cast<struct node *>(NULL);
}   


struct nodeLinkClass::node * nodeLinkClass::get_offline_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->offline_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}


struct nodeLinkClass::node * nodeLinkClass::get_mtcSwact_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->mtcSwact_timer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    } 
    return static_cast<struct node *>(NULL);
}   

struct nodeLinkClass::node * nodeLinkClass::get_oosTestTimer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->oosTestTimer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

struct nodeLinkClass::node * nodeLinkClass::get_insvTestTimer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct node * ptr = head ;  ; ptr = ptr->next )
       {
           if ( ptr->insvTestTimer.tid == tid )
           {
               return ptr ;
           }
           if (( ptr->next == NULL ) || ( ptr == tail ))
               break ;
        }
    }
    return static_cast<struct node *>(NULL);
}

/*****************************************************************************
 *
 * Name       : ar_enable
 *
 * Description: Clears all auto recovery state for the specified host and
 *              removes the auto recovery count file if it exists.
 *
 * Auto recovery count is tracked/preserved in a host named auto recovery
 * counter file /etc/mtc/tmp/hostname_ar_count.
 *
 *****************************************************************************/

void nodeLinkClass::ar_enable ( struct nodeLinkClass::node * node_ptr )
{
    string ar_file = TMP_DIR_PATH + node_ptr->hostname + AUTO_RECOVERY_FILE_SUFFIX ;
    if ( daemon_is_file_present (ar_file.data()))
    {
        wlog ("%s clearing autorecovery file counter\n", node_ptr->hostname.c_str());
        daemon_remove_file (ar_file.data());
    }

    if (( node_ptr->ar_disabled ) ||
        ( node_ptr->ar_cause != MTC_AR_DISABLE_CAUSE__NONE ))
    {
        wlog ("%s re-enabling autorecovery\n", node_ptr->hostname.c_str());
    }

    node_ptr->ar_disabled = false ;
    node_ptr->ar_cause = MTC_AR_DISABLE_CAUSE__NONE ;
    memset (&node_ptr->ar_count, 0, sizeof(node_ptr->ar_count));

    node_ptr->ar_log_throttle = 0 ;
}

/*****************************************************************************
 *
 * Name       : ar_manage
 *
 * Purpose    : Manage Auto Recovery state.
 *
 * Description: the following checks and operations are performed ...
 *
 * Pre Checks:
 *
 * Validate auto recovery cause code
 * Return if already in ar_disabled state. Unlikely but safe guard.
 *
 * Manage Auto Recovery:
 *
 * Case 1: Failed active controller with no enabled inactive controller.
 *
 *    Requires persistent count file and self reboot until threshold
 *    is reached.
 *
 *    Issues an immediate lazy reboot if the autorecovery threshold
 *    is not reached. Otherwise it disables autorecovery and returns
 *    so we don't get a rolling boot loop.
 *
 *    Auto recovery count is tracked/preserved in a host named auto
 *    recovery counter file /etc/mtc/tmp/hostname_ar_count.
 *
 * Case 2: All other cases
 *
 * Case 2a: No auto recovery thresholding of active controller in non AIO SX
 *          where the user can't lock and unlock the active controller.
 *
 *    Maintain auto recovery count and set ar_disabled for the host when
 *    the threshold is reached.
 *
 * Parameters:
 *
 *    node_ptr nodeLinkClass ptr of failing host.
 *
 *    cause    autorecovery_disable_cause_enum failure cause code.
 *
 *    string   host status string to display when auto recovery
 *             threshold is reached and autorecovery is disabled.
 *
 * Returns:
 *
 *    FAIL tells the caller to break from its FSM at earliest opportunity
 *         because auto recovery threshold is reached and auto recovery
 *         is disabled.
 *
 *    PASS tells the caller that the threshold is not reached and to
 *         continue handling the failure.
 *
 ******************************************************************************/

int nodeLinkClass::ar_manage ( struct nodeLinkClass::node * node_ptr,
                               autorecovery_disable_cause_enum cause,
                               string ar_disable_banner )
{
    int rc = FAIL ;

    /* Auto recovery only applies for hosts that are unlocked
     * and not already in ar_disabled state */
    if (( node_ptr->adminState != MTC_ADMIN_STATE__UNLOCKED ) ||
        ( node_ptr->ar_disabled ))
    {
        return (rc);
    }

    /* check for invalid call case */
    if ( cause >= MTC_AR_DISABLE_CAUSE__LAST )
    {
        slog ("%s called with invalid auto recovery cause (%d)",
                  node_ptr->hostname.c_str(), cause );
        return (rc);
    }

    /* update cause code */
    if ( node_ptr->ar_cause != cause )
        node_ptr->ar_cause = cause ;


    /* Case 1 check */
    if ( ( THIS_HOST ) && ( is_inactive_controller_main_insv() == false ))
    {
        /* manage the auto recovery threshold count file */
        unsigned int value = 0 ;

        string ar_file = TMP_DIR_PATH +
                         node_ptr->hostname +
                         AUTO_RECOVERY_FILE_SUFFIX ;

        if ( daemon_is_file_present (ar_file.data()))
        {
            /* if the file is there then read the count and increment it */
            value = daemon_get_file_int ( ar_file.data() );
        }
        value++ ;

        /* Save the new value in the file */
        daemon_log_value ( ar_file.data(), value );

        value = daemon_get_file_int ( ar_file.data() );

        /* set rc to reflect what the caller should do */
        if ( value > this->ar_threshold[node_ptr->ar_cause] )
        {
            elog ("%s auto recovery threshold exceeded (%d)\n",
                      node_ptr->hostname.c_str(),
                      this->ar_threshold[node_ptr->ar_cause] );

            node_ptr->ar_disabled = true ;
            adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );

            allStateChange  ( node_ptr, node_ptr->adminState,
                                        MTC_OPER_STATE__ENABLED,
                                        MTC_AVAIL_STATUS__DEGRADED );

            mtcInvApi_update_task ( node_ptr, ar_disable_banner );

            return (rc);
        }

        wlog ("%s auto recovery (try %d of %d) (%d)",
                  node_ptr->hostname.c_str(),
                  value,
                  this->ar_threshold[node_ptr->ar_cause],
                  node_ptr->ar_cause);

        mtcInvApi_update_states_now ( node_ptr, "unlocked",
                                      "disabled", "failed",
                                      "disabled", "failed" );

        lazy_graceful_fs_reboot ( node_ptr );
    }
    else /* Case 2 */
    {
        send_hbs_command   ( node_ptr->hostname, MTC_CMD_STOP_HOST  );
        mtcInvApi_update_states ( node_ptr, "unlocked", "disabled", "failed" );

        if (( NOT_THIS_HOST ) &&
            ( this->system_type != SYSTEM_TYPE__CPE_MODE__SIMPLEX ))
        {
            if ( ++node_ptr->ar_count[node_ptr->ar_cause] >=
                  this->ar_threshold [node_ptr->ar_cause] )
            {
                elog ("%s auto recovery threshold exceeded (%d)\n",
                          node_ptr->hostname.c_str(),
                          this->ar_threshold[node_ptr->ar_cause] );
                node_ptr->ar_disabled = true ;
                adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE );
                mtcInvApi_update_task ( node_ptr, ar_disable_banner );
                rc = FAIL ;
            }
            else
            {
                wlog ("%s auto recovery (try %d of %d) (%d)",
                          node_ptr->hostname.c_str(),
                          node_ptr->ar_count[node_ptr->ar_cause],
                          this->ar_threshold[node_ptr->ar_cause],
                          node_ptr->ar_cause);
                rc = PASS ;
            }
        }
        else
        {
            wlog ("%s auto recovery\n", node_ptr->hostname.c_str());
            rc = PASS ;
        }
    }
    return (rc);
}

/****************************************************************************
 *
 * Name       : report_dor_recovery
 *
 * Description: Create a specifically formatted log for the the specified
 *              hosts DOR recovery state and timing.
 *
 * Parameters : The node and a caller prefix string that states if the node
 *              is ENABELD
 *              is FAILED
 *              is ENMABLED-degraded
 *              etc.
 *
 ***************************************************************************/
void nodeLinkClass::report_dor_recovery ( struct nodeLinkClass::node * node_ptr,
                                          string node_state_log_prefix )
{
    struct timespec ts ;
    clock_gettime (CLOCK_MONOTONIC, &ts );
    node_ptr->dor_recovery_time = ts.tv_sec ;
    plog ("%-12s %s ; DOR Recovery %2d:%02d mins (%4d secs) (uptime:%2d:%02d mins)\n",
                 node_ptr->hostname.c_str(),
                 node_state_log_prefix.c_str(),
                 node_ptr->dor_recovery_time/60,
                 node_ptr->dor_recovery_time%60,
                 node_ptr->dor_recovery_time,
                 node_ptr->uptime/60,
                 node_ptr->uptime%60 );

    node_ptr->dor_recovery_mode = false ;
    node_ptr->was_dor_recovery_mode = false ;
}

void nodeLinkClass::force_full_enable ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->ar_disabled == true )
        return ;

    if ( node_ptr->was_dor_recovery_mode )
    {
        report_dor_recovery ( node_ptr , "is FAILED " );
    }

    plog ("%s Forcing Full Enable Sequence\n", node_ptr->hostname.c_str());

    /* Raise Critical Enable Alarm */
    alarm_enabled_failure ( node_ptr, true );

    allStateChange      ( node_ptr, node_ptr->adminState, MTC_OPER_STATE__DISABLED, MTC_AVAIL_STATUS__FAILED );
    enableStageChange   ( node_ptr, MTC_ENABLE__FAILURE    );
    recoveryStageChange ( node_ptr, MTC_RECOVERY__START    ); /* reset the fsm */
    // don't override the add action or lock actions /
    if (( node_ptr->adminAction != MTC_ADMIN_ACTION__ADD ) &&
        ( node_ptr->adminAction != MTC_ADMIN_ACTION__LOCK ) &&
        ( node_ptr->adminAction != MTC_ADMIN_ACTION__FORCE_LOCK ))
    {
        adminActionChange ( node_ptr, MTC_ADMIN_ACTION__NONE ); // no action
    }
    else
    {
        wlog ("%s refusing to override '%s' action with 'none' action\n",
                  node_ptr->hostname.c_str(),
                  mtc_nodeAdminAction_str [node_ptr->adminAction]);
    }
}

/*****************************************************************************
 *
 * Name       : launch_host_services_cmd
 *
 * Description: This is a multi timeslice service that is executed
 *              by the command handler.
 *
 *              This interface just determines the host type and loads the
 *              command handler with the host type corresponding host
 *              services command based on the start bool. If 'subf' is
 *              specified then the start or stop command defaults to COMPUTE.
 *
 *              Supported Commands are defined in nodeBase.h
 *
 *              start = False (means stop)
 *
 *                 MTC_CMD_STOP_CONTROL_SVCS
 *                 MTC_CMD_STOP_COMPUTE_SVCS
 *                 MTC_CMD_STOP_STORAGE_SVCS
 *
 *              start = True
 *
 *                 MTC_CMD_START_CONTROL_SVCS
 *                 MTC_CMD_START_COMPUTE_SVCS
 *                 MTC_CMD_START_STORAGE_SVCS
 *
 * Returns    : PASS = launch success
 *             !PASS = launch failure
 *
 ****************************************************************************/

int nodeLinkClass::launch_host_services_cmd ( struct nodeLinkClass::node * node_ptr, bool start, bool subf )
{
    if ( !node_ptr )
        return (FAIL_NULL_POINTER);

    /* Initialize the host's command request control structure */
    mtcCmd_init ( node_ptr->host_services_req );

    /* Service subfunction override first, efficiency. */
    if ( subf == true )
    {
        /* only supported subfunction (right now) is COMPUTE */
        if ( start == true )
            node_ptr->host_services_req.cmd = MTC_CMD_START_COMPUTE_SVCS ;
        else
            node_ptr->host_services_req.cmd = MTC_CMD_STOP_COMPUTE_SVCS ;
    }
    else if ( start == true )
    {
        if ( is_controller (node_ptr) )
            node_ptr->host_services_req.cmd = MTC_CMD_START_CONTROL_SVCS ;
        else if ( is_compute (node_ptr) )
            node_ptr->host_services_req.cmd = MTC_CMD_START_COMPUTE_SVCS ;
        else if ( is_storage (node_ptr) )
            node_ptr->host_services_req.cmd = MTC_CMD_START_STORAGE_SVCS ;
        else
        {
            slog ("%s start host services is not supported for this host type\n",
                      node_ptr->hostname.c_str());
            return (FAIL_BAD_CASE) ;
        }
    }
    else
    {
        if ( is_controller (node_ptr) )
            node_ptr->host_services_req.cmd = MTC_CMD_STOP_CONTROL_SVCS ;
        else if ( is_compute (node_ptr) )
            node_ptr->host_services_req.cmd = MTC_CMD_STOP_COMPUTE_SVCS ;
        else if ( is_storage (node_ptr) )
            node_ptr->host_services_req.cmd = MTC_CMD_STOP_STORAGE_SVCS ;
        else
        {
            slog ("%s stop host services is not supported for this host type\n",
                      node_ptr->hostname.c_str());
            return (FAIL_BAD_CASE);
        }
    }

    /* Translate that command to its named string */
    node_ptr->host_services_req.name =
        get_mtcNodeCommand_str(node_ptr->host_services_req.cmd);

    /* Get the host services timeout and add MTC_AGENT_TIMEOUT_EXTENSION
     * seconds so that it is a bit longer than the mtcClient timeout */
    int timeout = daemon_get_cfg_ptr()->host_services_timeout ;
        timeout+= MTC_AGENT_TIMEOUT_EXTENSION ;

    ilog ("%s %s launch\n",
              node_ptr->hostname.c_str(),
              node_ptr->host_services_req.name.c_str());

    /* The launch part.
     * init the  */
    mtcCmd_init ( node_ptr->cmd );
    node_ptr->cmd.stage = MTC_CMD_STAGE__START ;
    node_ptr->cmd.cmd   = MTC_OPER__HOST_SERVICES_CMD ;

    node_ptr->mtcCmd_work_fifo.clear() ;
    node_ptr->mtcCmd_work_fifo.push_front(node_ptr->cmd);

    /* start an unbrella timer and start waiting for the result,
     * a little longer than the mtcClient version */
    mtcTimer_reset ( node_ptr->host_services_timer );
    mtcTimer_start ( node_ptr->host_services_timer, mtcTimer_handler, timeout ) ;


    return (PASS);
}

int send_event ( string & hostname, unsigned int cmd, iface_enum iface );

int nodeLinkClass::mon_host ( const string & hostname, bool true_false, bool send_clear )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr != NULL )
    {
        bool want_log = true ;
        for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
        {
            if ( iface == INFRA_IFACE )
            {
                if ( this->infra_network_provisioned == false )
                    continue ;

                if ( node_ptr->monitor[MGMNT_IFACE] == true_false )
                    want_log = false ;
            }

            if ( send_clear == true )
            {
                send_event ( node_ptr->hostname, MTC_EVENT_HEARTBEAT_MINOR_CLR, (iface_enum)iface ) ;
                send_event ( node_ptr->hostname, MTC_EVENT_HEARTBEAT_DEGRADE_CLR, (iface_enum)iface ) ;
            }

            if ( true_false == true )
            {
                if ( want_log )
                {
                    ilog ("%s starting heartbeat service \n",
                              hostname.c_str());
                }
                node_ptr->no_work_log_throttle = 0 ;
                node_ptr->b2b_misses_count[iface] = 0 ;
                node_ptr->hbs_misses_count[iface] = 0 ;
                node_ptr->b2b_pulses_count[iface] = 0 ;
                node_ptr->max_count[iface] = 0 ;
                node_ptr->hbs_failure[iface] = false ;
                node_ptr->hbs_minor[iface] = false ;
                node_ptr->hbs_degrade[iface] = false ;
            }
            else
            {
                if ( want_log )
                {
                    ilog ("%s stopping heartbeat service\n",
                              hostname.c_str());
                }
            }
            node_ptr->monitor[iface] = true_false ;
        }
        return PASS ;
    }
    return ( FAIL );
}

/* store the current hardware monitor monitoring state */
void nodeLinkClass::set_hwmond_monitor_state ( string & hostname, bool state )
{
    if ( hostname.length() )
    {  
        struct nodeLinkClass::node* node_ptr ;
        node_ptr = nodeLinkClass::getNode ( hostname );
        if ( node_ptr != NULL )
        {
            node_ptr->hwmond_monitor = state ;
        }
    }
}

/* get the current hardware monitor monitoring state */
bool nodeLinkClass::get_hwmond_monitor_state ( string & hostname )
{
    bool state = false ;
    if ( hostname.length() )
    {  
        struct nodeLinkClass::node* node_ptr ;
        node_ptr = nodeLinkClass::getNode ( hostname );
        if ( node_ptr != NULL )
        {
            state = node_ptr->hwmond_monitor ;
        }
    }
    return (state);
}

/* get the current heartbeat monitoring state */
bool nodeLinkClass::get_hbs_monitor_state ( string & hostname, int iface )
{
    bool state = false ;
    if ( hostname.length() )
    {
        struct nodeLinkClass::node* node_ptr ;
        node_ptr = nodeLinkClass::getNode ( hostname );
        if ( node_ptr != NULL )
        {
            int rri_max = this->hosts ;
            state = node_ptr->monitor[iface] ;
            if ( state == true )
            {
                wlog_throttled (node_ptr->no_rri_log_throttle, rri_max,
                                "%s Not Offering RRI (%d)\n",
                                hostname.c_str(), this->hosts );
            }
            else
            {
                node_ptr->no_rri_log_throttle = 0 ;
            }
        }
    }
    return (state);
}

/* Manage the heartbeat pulse flags by hostname */
void nodeLinkClass::manage_pulse_flags ( string & hostname, unsigned int flags )
{
    if ( hostname.length() )
    {
        struct nodeLinkClass::node* node_ptr ;
        node_ptr = nodeLinkClass::getNode ( hostname );
        if ( node_ptr != NULL )
        {
            manage_pulse_flags ( node_ptr, flags );
        }
    }
}


/* Manage the heartbeat pulse flags by pulse_ptr */
void nodeLinkClass::manage_pulse_flags ( struct nodeLinkClass::node * node_ptr, unsigned int flags )
{
    /* Do nothing with the flags for missing pulse 
     * responses (identified with flags=NULL_PULSE_FLAGS) */
    if ( flags == NULL_PULSE_FLAGS )
    {
        return ;
    }

    /* Code that manages enabling of Infrastructrure network moonitoring
     *
     * Algorithm: Only monitor a hosts infrastructure network while the
     *            management network of that same host is being monitored
     *            and while that host indicates support for the infrastructure
     *            network by setting the INFRA_FLAG in its management network
     *            pulse responses. */
    if ( node_ptr->monitor[MGMNT_IFACE] == false )
    {
        node_ptr->monitor[INFRA_IFACE] = false ;
    }
    else if ( flags & INFRA_FLAG )
    {
        /* TODO: Does this need to be debounced ??? */
        node_ptr->monitor[INFRA_IFACE] = true ;
    }
    
    /* A host indicates that its process monitor is running by setting the
     * PMOND_FLAG occasionally in its pulse response. 
     * The following if/else if clauses manage raising an alarm and degrading
     * a host has stopped sending the PMOND_FLAG. */
    if ( flags & PMOND_FLAG )
    {
        if ( node_ptr->pmon_degraded == true )
        {
            if ( node_ptr->alarms[HBS_ALARM_ID__PMOND] != FM_ALARM_SEVERITY_CLEAR )
            {
                alarm_clear ( node_ptr->hostname, PMOND_ALARM_ID, "pmond" );
            }
            if ( send_event ( node_ptr->hostname, MTC_EVENT_PMOND_CLEAR, MGMNT_IFACE ) == PASS )
            {
                node_ptr->alarms[HBS_ALARM_ID__PMOND] = FM_ALARM_SEVERITY_CLEAR ;
                node_ptr->pmon_degraded = false ;
            }
        }
        node_ptr->pmon_missing_count = 0 ;
        node_ptr->stall_monitor_log_throttle = 0 ;
        node_ptr->stall_recovery_log_throttle = 0 ;
    }
    else if ( ++node_ptr->pmon_missing_count > PMOND_MISSING_THRESHOLD )
    {
        if ( node_ptr->pmon_degraded == false )
        {
            wlog ("%s sending pmon degrade event to maintenance\n", node_ptr->hostname.c_str());
            if ( send_event ( node_ptr->hostname, MTC_EVENT_PMOND_RAISE, MGMNT_IFACE ) == PASS )
            {
                node_ptr->pmon_degraded = true ;
                node_ptr->alarms[HBS_ALARM_ID__PMOND] = FM_ALARM_SEVERITY_MAJOR ;
                alarm_major ( node_ptr->hostname, PMOND_ALARM_ID, "pmond" );
            }
        }
    }

    /* A host indicates that a process stall condition exists by setting the
     * STALL_REC_FLAG it its heartbeat pulse response messages */
    if ( flags & STALL_REC_FLAG )
    {
        wlog ("%s hbsClient stall recovery action (flags:%08x)\n", node_ptr->hostname.c_str(), flags);
        if ( node_ptr->stall_recovery_log_throttle++ == 0 )
        {
            send_event ( node_ptr->hostname, MTC_EVENT_HOST_STALLED , MGMNT_IFACE );
        }
    }
    else if ( flags & STALL_MON_FLAG )
    {
        if ( node_ptr->stall_monitor_log_throttle++ == 0 )
        {
            wlog ("%s hbsClient running stall monitor (flags:%08x)\n", node_ptr->hostname.c_str(), flags );
        }
        else if ( flags & STALL_ERROR_FLAGS )
        {
            wlog ("%s hbsClient running stall monitor (flags:%08x)\n", node_ptr->hostname.c_str(), flags );
        }
    }

    if ( node_ptr->stall_recovery_log_throttle > STALL_MSG_THLD )
    {  
        node_ptr->stall_recovery_log_throttle = 0 ; 
    }
    if ( node_ptr->stall_monitor_log_throttle > STALL_MSG_THLD )
    {
        node_ptr->stall_monitor_log_throttle = 0 ; 
    }
}

/* Create the monitored pulse list for the specified interface */
int nodeLinkClass::create_pulse_list ( iface_enum iface )
{
    struct node * ptr = head ;

    if ( iface >= MAX_IFACES )
    {
        dlog ("Invalid interface (%d)\n", iface );
        return 0;
    }

    pulses[iface] = 0;
    pulse_list[iface].last_ptr = NULL ;
    pulse_list[iface].head_ptr = NULL ;
    pulse_list[iface].tail_ptr = NULL ;

    /* No check-in list if there is no inventory */
    if (( head == NULL ) || ( hosts == 0 ))
    {
        return 0;
    }

    /* walk the node list looking for nodes that should be monitored */
    for ( ; ptr != NULL ; ptr = ptr->next )
    {
        if ( ptr->monitor[iface] == true )
        {
            /* current monitored node pointer */
            pulse_ptr = ptr ;
            
            /* if first pulse node */
            if ( pulse_list[iface].head_ptr == NULL )
            {
                /* need to keep track of the last node so we can deal with 
                 * skipped nodes when they are not in monitor mode */
                pulse_list[iface].last_ptr = pulse_ptr ;
                pulse_list[iface].head_ptr = pulse_ptr ;
                pulse_list[iface].tail_ptr = pulse_ptr ;
                pulse_ptr->pulse_link[iface].prev_ptr = NULL ;
            }
            else 
            {
                pulse_list[iface].last_ptr->pulse_link[iface].next_ptr = pulse_ptr ;
                pulse_ptr->pulse_link[iface].prev_ptr = pulse_list[iface].last_ptr ;
                pulse_list[iface].last_ptr = pulse_ptr ; /* save current to handle a skip */
                pulse_list[iface].tail_ptr = pulse_ptr ; /* migrate tail as list is built */
            }
            pulse_ptr->pulse_link[iface].next_ptr = NULL ;

            pulse_ptr->linknum[iface] = ++pulses[iface] ;

            mlog2 ("%s %s Pulse Info: %d:%d - %d:%p\n",
                      pulse_ptr->hostname.c_str(),
                      get_iface_name_str(iface),
                      pulse_ptr->linknum[iface], 
                      pulses[iface], 
                      pulse_ptr->rri, 
                      pulse_ptr);
        }
    }
    print_pulse_list(iface);
    return (pulses[iface]);
}

/** Clear heartbeat stats in support of failed heartbeat restart */
void nodeLinkClass::hbs_clear_all_stats ( void )
{
    ilog ("clearing all hearbeat stats\n");
    for (  struct node * ptr = head ; ptr != NULL ; ptr = ptr->next )
    {
        for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
        {
            ptr->max_count[iface] = 0 ;
            ptr->hbs_count[iface] = 0 ;
            ptr->hbs_misses_count[iface] = 0 ;
            ptr->b2b_pulses_count[iface] = 0 ;
            ptr->b2b_misses_count[iface] = 0 ;
            ptr->hbs_minor_count[iface] = 0 ;
            ptr->hbs_degrade_count[iface] = 0 ;
            ptr->hbs_failure_count[iface] = 0 ;
            ptr->hbs_minor[iface] = false ;
            ptr->hbs_degrade[iface] = false ;
            ptr->hbs_failure[iface] = false ;
            ptr->heartbeat_failed[iface] = false ;
        }
        if (( ptr->next == NULL ) || ( ptr == tail ))
            break ;
    }
}

/** Build the Reasource Reference Array */ 
void nodeLinkClass::build_rra ( void )
{
    struct node * ptr = NULL ;
    int x = 1 ;
    for ( ptr = head ; ptr != NULL ; ptr = ptr->next )
    {
        hbs_rra [x] = ptr ; 
        ptr->rri=x ; 
        x++ ;
        if (( ptr->next == NULL ) || ( ptr == tail ))
            break ;
    }

    if ( ptr != NULL )
    {
        dlog ("%s forced RRA build (%d)\n", ptr->hostname.c_str(), x-1);
    }

    /* fill the rest with NULL */
    for ( ; x < MAX_NODES ; x++ )
        hbs_rra[x] = NULL ;

    /* Reset the "Running RRI" */
    rrri = 0 ;
}

/** Gets the next hostname and resource reference identifier 
  * (the rra index) and updates the callers variables with them. 
  *
  *  This is a helper function in support of the fast resource lookup feature.
  *  During steady state operation the heartbeat agent cycles through
  *  all the resources , one per heartbeat request, sending new
  *  reference identifiers (name and index) to the monitored resources.
  *  Each time this is called it get the next set.
  *  */
void   nodeLinkClass::get_rris ( string & hostname, int & rri )
{
    if ( hosts )
    {
        hostname = "none" ;
        rrri++ ;
        if ( rrri > hosts )
        {
            rrri = 1 ;
        }
        hostname = hbs_rra[rrri]->hostname ;
        rri = rrri ;
    }
}

struct nodeLinkClass::node* nodeLinkClass::getPulseNode ( string & hostname , iface_enum iface )
{
    /* check for empty list condition */
    if ( pulse_list[iface].head_ptr == NULL )
        return NULL ;

    for ( pulse_ptr = pulse_list[iface].head_ptr ;  ; pulse_ptr = pulse_ptr->pulse_link[iface].next_ptr )
    {
        if ( !hostname.compare ( pulse_ptr->hostname ))
        {
            return pulse_ptr ;  
        }
        if (( pulse_ptr->pulse_link[iface].next_ptr == NULL ) || 
            ( pulse_ptr==pulse_list[iface].tail_ptr ))
        {
            break ;
        }
    } 
    return static_cast<struct node *>(NULL);
}

/* Find the node  in the list of nodes being heartbeated and splice it out */
int nodeLinkClass::remPulse_by_index ( string hostname, int index, iface_enum iface, bool clear_b2b_misses_count, unsigned int flags )
{
    int rc = FAIL ;
    if (( index > 0 ) && ( !(index > hosts)))
    {
        if ( hbs_rra[index] != NULL )
        {
            struct nodeLinkClass::node* node_ptr ;
            node_ptr = nodeLinkClass::getNode ( hostname );
            if ( node_ptr != NULL )
            {
                if (( hbs_rra[index] == node_ptr ) &&
                    ( ! node_ptr->hostname.compare(hostname)))
                {
                    node_ptr->lookup_mismatch_log_throttle = 0 ;
                    if ( node_ptr->monitor[iface] == true )
                    {
                        node_ptr->unexpected_pulse_log_throttle = 0 ;
                        return ( remPulse ( hbs_rra[index], iface, clear_b2b_misses_count, flags ));
                    }
                    else
                    {
                        wlog_throttled ( node_ptr->unexpected_pulse_log_throttle, 200, "%s is not being monitored\n", hostname.c_str());
                        rc = PASS;
                    }
                }
                else
                {
                    rc = remPulse_by_name ( hostname, iface, clear_b2b_misses_count, flags );
                    wlog_throttled ( node_ptr->lookup_mismatch_log_throttle, 200, "%s rri lookup mismatch (%s:%d) ; %s\n", hostname.c_str(), node_ptr->hostname.c_str(), index, rc ? "" : "removed by hostname" );
                    return (rc);
                }
            }
            else
            {
                dlog ("%s could not lookup by index or hostname (%d)\n", hostname.c_str(), index );
                rc = FAIL_HOSTNAME_LOOKUP ;
            }
        }
    }
    return (rc);
}

/* Find the node  in the list of nodes being heartbeated and splice it out */
int nodeLinkClass::remPulse_by_name ( string & hostname, iface_enum iface, bool clear_b2b_misses_count, unsigned int flags )
{
    return ( remPulse ( getPulseNode ( hostname, iface ), iface, clear_b2b_misses_count, flags ));
}

/** WANT_LINKLIST_FIT is not defined by default.
 *  Needs to be explicitely defined and the undef commented out for testing
 **/
#ifdef WANT_LINKLIST_FIT
#undef WANT_LINKLIST_FIT
#endif

#ifdef WANT_LINKLIST_FIT
static bool already_fit = false ;
#endif

/* Find the node  in the list of nodes being heartbeated and splice it out */
int nodeLinkClass::remPulse ( struct node * node_ptr, iface_enum iface, bool clear_b2b_misses_count, unsigned int flags )
{
    /* This default RC allows the caller to filter out unexpected pulse responses */
    int rc = ENXIO ;

    if ( head == NULL )
    {
        return -ENODEV ;
    }
    else if ( node_ptr == NULL )
    {
        return (rc) ;
    }

    struct node * ptr = node_ptr ;

    // dlog ("%s\n", node_ptr->hostname.c_str());

    /* Splice the node out of the pulse monitor list */

    /* Does the pulse monitor list exist and is the node in the list */
    /* Need to gracefully handle being called when there is no pulse */
    /* list and/or the specified host is not in the pulse list       */
    if (( pulse_list[iface].head_ptr != NULL ) && ( ptr != NULL ) && ( ptr->linknum[iface] != 0))
    {
        pulse_ptr = ptr ;

        manage_pulse_flags ( pulse_ptr , flags );

        /* clear_b2b_misses_count override check ; thresold recovery */
        if ( clear_b2b_misses_count == true )
        {
            ptr->hbs_count[iface]++ ;
            ptr->b2b_pulses_count[iface]++ ;

            if ( ptr->b2b_pulses_count[iface] == hbs_failure_threshold )
            {
                hbs_cluster_change( ptr->hostname + " " + get_iface_name_str(iface) + " heartbeat pass" );
            }
            else if ( ptr->b2b_pulses_count[iface] == 1 )
            {
                hbs_cluster_change( ptr->hostname + " " + get_iface_name_str(iface) + " heartbeat start" );
            }

            if ( ptr->hbs_failure[iface] == true )
            {
                /* threshold failure recovery */
                if ( ptr->b2b_pulses_count[iface] < HBS_PULSES_REQUIRED_FOR_RECOVERY )
                {
                    /* don't clear the alarm or send clear notifications to mtc
                     * if this interfaces failed and has not yet received the
                     * required number of back to back pulses needed for recovery */
                    clear_b2b_misses_count = false ;
                    dlog ("%s %s heartbeat failure recovery (%d of %d)\n",
                                 node_ptr->hostname.c_str(),
                                 get_iface_name_str(iface),
                                 ptr->b2b_pulses_count[iface],
                                 HBS_PULSES_REQUIRED_FOR_RECOVERY);
                }
                else
                {
                    ptr->hbs_failure[iface] = false ;
                    ilog ("%s %s heartbeat failure recovery (%d)\n",
                                 node_ptr->hostname.c_str(),
                                 get_iface_name_str(iface),
                                 ptr->b2b_pulses_count[iface]);
                }
            }
            else
            {
                ptr->b2b_misses_count[iface] = 0 ;
            }
        }
        else
        {
            if (( ptr->b2b_pulses_count[iface] != 0 ) && ( ptr->hbs_failure[iface] == true ))
            {
                ilog ("%s %s failed but %d\n", node_ptr->hostname.c_str(),
                                 get_iface_name_str(iface),
                                 ptr->b2b_pulses_count[iface]);
            }

        }

        if ( clear_b2b_misses_count == true )
        {
            manage_heartbeat_alarm ( pulse_ptr, FM_ALARM_SEVERITY_CLEAR, iface );
            if ( ptr->b2b_misses_count[iface] > hbs_degrade_threshold )
            {
                ilog ("%s %s Pulse Rxed (after %d misses)\n",
                             node_ptr->hostname.c_str(),
                             get_iface_name_str(iface),
                             node_ptr->b2b_misses_count[iface]);
            }

            ptr->b2b_misses_count[iface] = 0 ;
            if ( pulse_ptr->hbs_degrade[iface] == true )
            {
                /* Send a degrade clear event to maintenance */
                if ( send_event ( pulse_ptr->hostname, MTC_EVENT_HEARTBEAT_DEGRADE_CLR, iface ) == PASS )
                {
                    pulse_ptr->hbs_degrade[iface] = false ;
                }
            }
            if ( pulse_ptr->hbs_minor[iface] == true )
            {
                if ( send_event ( pulse_ptr->hostname, MTC_EVENT_HEARTBEAT_MINOR_CLR, iface ) == PASS )
                {
                    pulse_ptr->hbs_minor[iface] = false ;
                }
            }
        }
        rc = PASS ;
#ifdef WANT_LINKLIST_FIT
        if ( already_fit == false )
        {
            if ( daemon_is_file_present ( MTC_CMD_FIT__LINKLIST ) == true )
            {
                if ( pulse_list[iface].head_ptr->pulse_link[iface].next_ptr != NULL )
                {
                    slog ("FIT of next pointer\n");
                    pulse_list[iface].head_ptr->pulse_link[iface].next_ptr = NULL ;
                    already_fit = true ;
                }
            }
        }
#endif
        if ( pulse_list[iface].head_ptr == pulse_ptr )
        {
            if ( pulse_list[iface].head_ptr == pulse_list[iface].tail_ptr )
            {
                qlog2 ("%s Pulse: Single   Node -> Head Case  : %d of %d\n", node_ptr->hostname.c_str(), pulse_ptr->linknum[iface], pulses[iface] );
                pulse_list[iface].head_ptr = NULL ;
                pulse_list[iface].tail_ptr = NULL ;
            }
            else
            {
                qlog2 ("%s Pulse: Multiple Node -> Head Case  : %d of %d\n", node_ptr->hostname.c_str(), pulse_ptr->linknum[iface], pulses[iface] );
                if ( pulse_list[iface].head_ptr->pulse_link[iface].next_ptr == NULL )
                {
                    slog ("%s unexpected NULL next_ptr ; aborting this pulse window\n", node_ptr->hostname.c_str());
                    pulse_list[iface].head_ptr = NULL ;
                    pulse_list[iface].tail_ptr = NULL ;
                    pulse_ptr->linknum[iface] = 0 ;
                    pulses[iface] = 0 ;
                    return (FAIL_NULL_POINTER);
                }
                else
                {
                    pulse_list[iface].head_ptr = pulse_list[iface].head_ptr->pulse_link[iface].next_ptr ;
                    pulse_list[iface].head_ptr->pulse_link[iface].prev_ptr = NULL ;
                }
            }
        }
        else if ( pulse_list[iface].tail_ptr == pulse_ptr )
        {
            qlog2 ("%s Pulse: Multiple Node -> Tail Case  : %d of %d\n", node_ptr->hostname.c_str(), pulse_ptr->linknum[iface], pulses[iface] );
            if ( pulse_list[iface].tail_ptr->pulse_link[iface].prev_ptr == NULL )
            {
                slog ("%s unexpected NULL prev_ptr ; aborting this pulse window\n", node_ptr->hostname.c_str());
                pulse_list[iface].head_ptr = NULL ;
                pulse_list[iface].tail_ptr = NULL ;
                pulse_ptr->linknum[iface] = 0 ;
                pulses[iface] = 0 ;
                return (FAIL_NULL_POINTER);
            }
            else
            {
                pulse_list[iface].tail_ptr = pulse_list[iface].tail_ptr->pulse_link[iface].prev_ptr ;
                pulse_list[iface].tail_ptr->pulse_link[iface].next_ptr = NULL ;
            }
        }
        else
        {
            /* July 1 emacdona: Make failure path case more robust */
            if ( pulse_ptr                                  == NULL ) { slog ("Internal Err 1\n"); rc = FAIL; }
            else if ( pulse_ptr->pulse_link[iface].prev_ptr == NULL ) { slog ("Internal Err 2\n"); rc = FAIL; }
            else if ( pulse_ptr->pulse_link[iface].next_ptr == NULL ) { slog ("Internal Err 3\n"); rc = FAIL; }
            if ( rc == FAIL )
            {
                slog ("%s Null pointer error splicing %s out of pulse list with %d pulses remaining (Monitoring:%s)\n",
                          node_ptr->hostname.c_str(),
                          get_iface_name_str(iface),
                          pulses[iface],
                          node_ptr->monitor[iface] ? "Yes" : "No" );
            }
            else
            {
                pulse_ptr->pulse_link[iface].prev_ptr->pulse_link[iface].next_ptr = pulse_ptr->pulse_link[iface].next_ptr ;
                pulse_ptr->pulse_link[iface].next_ptr->pulse_link[iface].prev_ptr = pulse_ptr->pulse_link[iface].prev_ptr ;
            }
        }
        if ( rc == PASS )
        {
           pulse_ptr->linknum[iface]-- ;
        }
        pulses[iface]-- ;
    }
    else if ( node_ptr )
    {
        dlog ("%s unexpected pulse response ; %s",
                 node_ptr->hostname.c_str(),
                 get_iface_name_str(iface));
    }
    else
    {
        slog ("null pointer");
    }

    return rc ;
}

/** This utility will try and remove a pluse from the pulse
 *  linked list first by index and then by hostname.
 *
 *  By index does not require a lookup whereas hostname does */
int nodeLinkClass::remove_pulse ( string & hostname, iface_enum iface, int index, unsigned int flags )
{
    /* TODO: consider removing this check */
    if ( hostname == "localhost" )
    {
        /* localhost is not a supported hostname and indicates
         * an unconfigured host response ; return the ignore response */
        return(ENXIO);
    }
    if ( index )
    {
        int rc = remPulse_by_index ( hostname, index , iface, true , flags );
        switch (rc)
        {
            case PASS: return (rc) ;
            case ENXIO: return (rc);
            default: mlog ("%s RRI Miss (rri:%d) (rc:%d)\n", hostname.c_str(), index, rc );
        }
    }
    else
    {
    }
    return ( remPulse_by_name ( hostname , iface, true, flags ));
}

void nodeLinkClass::clear_pulse_list ( iface_enum iface )
{
   struct node * ptr = head ;
   for ( ; ptr != NULL ; ptr = ptr->next )
   {
      ptr->pulse_link[iface].prev_ptr = NULL ;
      ptr->pulse_link[iface].next_ptr = NULL ;
   }
   pulse_list[iface].head_ptr = NULL ;
   pulse_list[iface].tail_ptr = NULL ;

   if ( ptr != NULL )
   {
       ptr->linknum[iface] = 0    ;
       pulses[iface] = 0 ;
   }
}

/** Runs in the hbsAgent to set or clear heartbat alarms for all supported interfaces */
void nodeLinkClass::manage_heartbeat_alarm ( struct nodeLinkClass::node * node_ptr, EFmAlarmSeverityT sev, int iface )
{
    if ( this->heartbeat != true )
        return ;

    if ( this->hbs_failure_action == HBS_FAILURE_ACTION__NONE )
    {
        dlog ("%s dropping heartbeat alarm request (%s:%s) ; action none\n",
                  node_ptr->hostname.c_str(),
                  alarmUtil_getSev_str(sev).c_str(),
                  get_iface_name_str(iface) );
        return ;
    }

    bool make_alarm_call = false ;
    alarm_id_enum id ;
    EFmAlarmStateT state = FM_ALARM_STATE_SET ;
    const char * alarm_id_ptr = NULL ;
    const char *   entity_ptr = NULL ;
    if ( iface == MGMNT_IFACE )
    {
        entity_ptr = MGMNT_NAME ;
        id     = HBS_ALARM_ID__HB_MGMNT ;
        alarm_id_ptr = MGMNT_HB_ALARM_ID;
    }
    else
    {
        entity_ptr = INFRA_NAME ;
        id     = HBS_ALARM_ID__HB_INFRA ;
        alarm_id_ptr = INFRA_HB_ALARM_ID;
    }

    if ( sev == FM_ALARM_SEVERITY_CLEAR )
    {
        state = FM_ALARM_STATE_CLEAR ;
        if ( node_ptr->alarms[id] != FM_ALARM_SEVERITY_CLEAR )
        {
            make_alarm_call = true ;
            node_ptr->alarms[id] = sev ;
        }
    }
    else if ( sev == FM_ALARM_SEVERITY_MAJOR )
    {
        if ( node_ptr->alarms[id] == FM_ALARM_SEVERITY_CRITICAL )
        {
            ; /* we don't go from critical to degrade 
                 need a clear first */
        }
        else if ( node_ptr->alarms[id] != FM_ALARM_SEVERITY_MAJOR )
        {
            make_alarm_call = true ;
            node_ptr->alarms[id] = FM_ALARM_SEVERITY_MAJOR ;
        }
    }
    else if ( sev == FM_ALARM_SEVERITY_CRITICAL )
    {
        if ( node_ptr->alarms[id] != sev )
        {
            make_alarm_call = true ;
            node_ptr->alarms[id] = sev ;
        }
    }
    else if ( sev == FM_ALARM_SEVERITY_MINOR )
    {
        if ( node_ptr->alarms[id] != sev )
        {
            make_alarm_call = true ;
            node_ptr->alarms[id] = sev ;
        }
    }
    else
    {
        if ( node_ptr->alarms[id] != FM_ALARM_SEVERITY_WARNING )
        {
            make_alarm_call = true ;
            node_ptr->alarms[id] = FM_ALARM_SEVERITY_WARNING ;
        }
    }
    if ( make_alarm_call == true )
    {
        alarm_ ( node_ptr->hostname, alarm_id_ptr, state, sev, entity_ptr , "");
    }
}



#define HBS_LOSS_REPORT_THROTTLE (100)
int nodeLinkClass::lost_pulses ( iface_enum iface, bool & storage_0_responding )
{
    int lost = 0  ;

    /*
     * Assume storage-0 is responding until otherwise proven its not.
     * keep in mind that this interface counts nodes that have not responded ;
     * not those that have.
     */
    storage_0_responding = true ;

    /*
     * Loop over the pulse_list which now onoly contains a list of hosts
     * that have not responded in this heartbeat period.
     */
    for (  ; pulse_list[iface].head_ptr != NULL ; )
    {
        daemon_signal_hdlr ();
        pulse_ptr = pulse_list[iface].head_ptr ;
        lost++ ;
        if ( active )
        {
            string flat = "Flat Line:" ;
            pulse_ptr->b2b_misses_count[iface]++ ;
            pulse_ptr->hbs_misses_count[iface]++ ;
            pulse_ptr->b2b_pulses_count[iface] = 0 ;
            // pulse_ptr->max_count[iface]++ ;

            /*
             * Update storage_0_responding reference to false if storgate-0
             * is found in the pulse lots list.
             */
            if ( pulse_ptr->hostname == STORAGE_0 )
            {
                storage_0_responding = false ;
            }

            if ( pulse_ptr->b2b_misses_count[iface] > 1 )
            {
                if ( pulse_ptr->b2b_misses_count[iface] >= hbs_failure_threshold )
                {
                    if ( pulse_ptr->b2b_misses_count[iface] == hbs_failure_threshold )
                    {
                        ilog ("%s %s Pulse Miss (%d) (log throttled to every %d)\n",
                                                     pulse_ptr->hostname.c_str(),
                                                     get_iface_name_str(iface),
                                                     pulse_ptr->b2b_misses_count[iface],
                                                     0xfff);
                    }
                    /* Once the misses exceed 25 then throttle the logging to avoid flooding */
                    if ( (pulse_ptr->b2b_misses_count[iface] & 0xfff) == 0 )
                    {
                        ilog ("%s %s Pulse Miss (%d)\n", pulse_ptr->hostname.c_str(),
                                                     get_iface_name_str(iface),
                                                     pulse_ptr->b2b_misses_count[iface] );
                    }
                }
                else
                {
                    if ( pulse_ptr->b2b_misses_count[iface] > hbs_failure_threshold )
                    {
                        ilog ("%s %s Pulse Miss (%3d) (in failure)\n", pulse_ptr->hostname.c_str(),
                                                                    get_iface_name_str(iface),
                                                                    pulse_ptr->b2b_misses_count[iface] );
                    }
                    else if ( pulse_ptr->b2b_misses_count[iface] > hbs_degrade_threshold )
                    {
                        ilog ("%s %s Pulse Miss (%3d) (max:%3d) (in degrade)\n", pulse_ptr->hostname.c_str(),
                                                                    get_iface_name_str(iface),
                                                                    pulse_ptr->b2b_misses_count[iface],
                                                                    pulse_ptr->max_count[iface]);
                    }
                    else if ( pulse_ptr->b2b_misses_count[iface] > hbs_minor_threshold )
                    {
                        ilog ("%s %s Pulse Miss (%3d) (max:%3d) (in minor)\n",   pulse_ptr->hostname.c_str(),
                                                                    get_iface_name_str(iface),
                                                                    pulse_ptr->b2b_misses_count[iface] ,
                                                                    pulse_ptr->max_count[iface]);
                    }
                    else
                    {
                        ilog ("%s %s Pulse Miss (%3d) (max:%3d)\n", pulse_ptr->hostname.c_str(),
                                                         get_iface_name_str(iface),
                                                         pulse_ptr->b2b_misses_count[iface],
                                                         pulse_ptr->max_count[iface]);
                    }
                }
            }
            else
            {
                dlog ("%s %s Pulse Miss (%d)\n", pulse_ptr->hostname.c_str(),
                                                 get_iface_name_str(iface),
                                                 pulse_ptr->b2b_misses_count[iface] );
            }
#ifdef WANT_HBS_MEM_LOGS
            mem_log ( flat, pulse_ptr->b2b_misses_count[iface], pulse_ptr->hostname.c_str());
#endif
            if ( iface == MGMNT_IFACE )
            {
                if ( pulse_ptr->b2b_misses_count[iface] == hbs_minor_threshold )
                {
                    if ( this->active_controller )
                    {
                        send_event ( pulse_ptr->hostname, MTC_EVENT_HEARTBEAT_MINOR_SET, iface );
                    }
                    pulse_ptr->hbs_minor[iface] = true ;
                    pulse_ptr->hbs_minor_count[iface]++ ;
                    wlog ("%s %s -> MINOR\n", pulse_ptr->hostname.c_str(), get_iface_name_str(iface));
                }
            }
            if ( pulse_ptr->b2b_misses_count[iface] == hbs_degrade_threshold )
            {
                if ( this->active_controller )
                {
                    manage_heartbeat_alarm ( pulse_ptr, FM_ALARM_SEVERITY_MAJOR, iface );

                    /* report this host as failed */
                    if ( send_event ( pulse_ptr->hostname, MTC_EVENT_HEARTBEAT_DEGRADE_SET, iface ) == PASS )
                    {
                        pulse_ptr->hbs_degrade[iface] = true ;
                    }
                }
                else
                {
                    pulse_ptr->hbs_degrade[iface] = true ;
                }
                wlog ("%s %s -> DEGRADED\n", pulse_ptr->hostname.c_str(), get_iface_name_str(iface));
                pulse_ptr->hbs_degrade_count[iface]++ ;

            }
            /* Handle lost degrade event case */
            if (( pulse_ptr->b2b_misses_count[iface] > hbs_degrade_threshold ) &&
                ( pulse_ptr->hbs_degrade[iface] == false ))
            {
                wlog ("%s -> DEGRADED - Auto-Correction\n", pulse_ptr->hostname.c_str());
                if ( this->active_controller )
                {
                    manage_heartbeat_alarm ( pulse_ptr, FM_ALARM_SEVERITY_MAJOR, iface );

                    /* report this host as failed */
                    if ( send_event ( pulse_ptr->hostname, MTC_EVENT_HEARTBEAT_DEGRADE_SET, iface ) == PASS )
                    {
                        pulse_ptr->hbs_degrade[iface] = true ;
                    }
                }
                else
                {
                    pulse_ptr->hbs_degrade[iface] = true ;
                }
            }

            /* Turn the infra heartbeat loss into a degrade only
             * condition if the infra_degrade_only flag is set */
            if (( iface == INFRA_IFACE ) &&
                ( pulse_ptr->b2b_misses_count[iface] >= hbs_failure_threshold ) &&
                ( infra_degrade_only == true ))
            {
                /* Only print the log at the threshold boundary */
                if (( pulse_ptr->b2b_misses_count[iface]%HBS_LOSS_REPORT_THROTTLE) == hbs_failure_threshold )
                {
                    if ( this->active_controller )
                    {
                        manage_heartbeat_alarm ( pulse_ptr, FM_ALARM_SEVERITY_CRITICAL, iface );
                    }

                    wlog ( "%s %s *** Heartbeat Loss *** (degrade only)\n",
                               pulse_ptr->hostname.c_str(),
                               get_iface_name_str(iface) );
                    hbs_cluster_change ( pulse_ptr->hostname + " heartbeat loss" );
                }
            }

            /* Turn the infra heartbeat loss into a degrade only
             * condition for inactive controller on normal system. */
            else if (( iface == INFRA_IFACE ) &&
                     ( pulse_ptr->b2b_misses_count[iface] >= hbs_failure_threshold ) &&
                     ( this->system_type == SYSTEM_TYPE__NORMAL ) &&
                     (( pulse_ptr->nodetype & CONTROLLER_TYPE) == CONTROLLER_TYPE ))
            {
                /* Only print the log at the threshold boundary */
                if ( (pulse_ptr->b2b_misses_count[iface]%HBS_LOSS_REPORT_THROTTLE) == hbs_failure_threshold )
                {
                    if ( this->active_controller )
                    {
                        manage_heartbeat_alarm ( pulse_ptr, FM_ALARM_SEVERITY_CRITICAL, iface );
                    }
                    wlog ( "%s %s *** Heartbeat Loss *** (degrade only)\n",
                               pulse_ptr->hostname.c_str(),
                               get_iface_name_str(iface));
                    hbs_cluster_change ( pulse_ptr->hostname + " heartbeat loss" );
                }
            }

            else if ((pulse_ptr->b2b_misses_count[iface]%HBS_LOSS_REPORT_THROTTLE) == hbs_failure_threshold )
            {
                elog ("%s %s *** Heartbeat Loss ***\n", pulse_ptr->hostname.c_str(),
                                                        get_iface_name_str(iface) );

                if ( this->active_controller )
                {
                    manage_heartbeat_alarm ( pulse_ptr, FM_ALARM_SEVERITY_CRITICAL, iface );

                    /* report this host as failed */
                    if ( send_event ( pulse_ptr->hostname, MTC_EVENT_HEARTBEAT_LOSS , iface ) == PASS )
                    {
                        pulse_ptr->hbs_failure[iface] = true ;
                    }
                }
                else
                {
                    pulse_ptr->hbs_failure[iface] = true ;
                    hbs_cluster_change ( pulse_ptr->hostname + " heartbeat loss" );
                }
                pulse_ptr->hbs_failure_count[iface]++ ;
            }
            if ( pulse_ptr->b2b_misses_count[iface] > pulse_ptr->max_count[iface] )
                 pulse_ptr->max_count[iface] = pulse_ptr->b2b_misses_count[iface] ;
        }

        if ( remPulse_by_name ( pulse_ptr->hostname, iface, false, NULL_PULSE_FLAGS ))
        {
           elog ("%s %s not in pulse list\n", pulse_ptr->hostname.c_str(),
                                              get_iface_name_str(iface));
           clear_pulse_list ( iface );
           break ;
        }
        if ( pulse_list[iface].head_ptr == NULL )
        {
           // dlog ("Pulse list is Empty\n");
           break ;
        }
    }
    return (lost);
}

/* Return true if the specified interface is being monitored for this host */
bool nodeLinkClass::monitored_pulse ( string hostname , iface_enum iface )
{
    if ( hostname.length() )
    {
        struct nodeLinkClass::node* node_ptr ;
        node_ptr = nodeLinkClass::getNode ( hostname );
        if ( node_ptr != NULL )
        {
            return ( node_ptr->monitor[iface] ) ;
        }
   }
   return(false);
}

/* Reports pulse list empty status.
 * true if empty
 * false if not empty
 */
bool nodeLinkClass::pulse_list_empty ( iface_enum iface )
{
    if ( pulse_list[iface].head_ptr == NULL )
       return true ;
    return false ;
}

void nodeLinkClass::print_pulse_list ( iface_enum iface )
{
    string pulse_host_list = "- " ;

    if ( pulse_list[iface].head_ptr != NULL )
    {
        for ( pulse_ptr = pulse_list[iface].head_ptr ;
              pulse_ptr != NULL ;
              pulse_ptr = pulse_ptr->pulse_link[iface].next_ptr )
        {
            pulse_host_list.append(pulse_ptr->hostname.c_str());
            pulse_host_list.append(" ");
        }
        dlog ("Patients: %s\n", pulse_host_list.c_str());
    }

#ifdef WANT_HBS_MEM_LOGS
    if ( pulses[iface] && !pulse_host_list.empty() )
    {
        string temp = get_iface_name_str(iface) ;
        temp.append(" Patients :") ;
        mem_log ( temp, pulses[iface], pulse_host_list );
    }
#endif
}



/* Clear all degrade flags except for the HWMON one */
void clear_host_degrade_causes ( unsigned int & degrade_mask )
{
    if ( degrade_mask & DEGRADE_MASK_HWMON )
    {
        degrade_mask = DEGRADE_MASK_HWMON ;
    }
    else
    {
        degrade_mask = 0 ;
    }
}

/***************************************************************************/
/*******************       State Dump Utilities      ***********************/
/***************************************************************************/



void nodeLinkClass::mem_log_general ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s %s %s %s:%s %s:%s \n", 
                my_hostname.c_str(),
                my_local_ip.c_str(),
                my_float_ip.c_str(),
                daemon_get_cfg_ptr()->mgmnt_iface,
                mgmnt_link_up_and_running ? "Up" : "Down",
                daemon_get_cfg_ptr()->infra_iface,
                infra_link_up_and_running ? "Up" : "Down");
    mem_log (str);
}

void nodeLinkClass::mem_log_dor ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s  DOR - Active: %c  Was: %c  Time: %5d (00:%02d:%02d)\n",
                node_ptr->hostname.c_str(),
                node_ptr->dor_recovery_mode ? 'Y' : 'N',
                node_ptr->was_dor_recovery_mode ? 'Y' : 'N',
                node_ptr->dor_recovery_time,
                node_ptr->dor_recovery_time ? node_ptr->dor_recovery_time/60 : 0,
                node_ptr->dor_recovery_time ? node_ptr->dor_recovery_time%60 : 0);
    mem_log (str);
}



/* Multi-Node Failure Avoidance Data */
void nodeLinkClass::mem_log_mnfa ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s MNFA: State:%s Hosts:%d:%d Threshold:%d Occurances:%d\n",
                my_hostname.c_str(),
                mnfa_active ? "ACTIVE" : "inactive",
                mnfa_host_count[MGMNT_IFACE],
                mnfa_host_count[INFRA_IFACE],
                mnfa_threshold,
                mnfa_occurances);
    mem_log (str);
}

void nodeLinkClass::mem_log_general_mtce_hosts ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s EnableHosts -> Cont:%d Comp:%d Stor:%d StorType:%d\n",
                my_hostname.c_str(),
                num_controllers_enabled(),
                enabled_compute_nodes(),
                enabled_storage_nodes(),
                get_storage_backend());
    mem_log (str);
}

void nodeLinkClass::mem_log_bm ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tbm_ip:%s bm_un:%s bm_type:%s provisioned: %s\n",
                node_ptr->hostname.c_str(),
                node_ptr->bm_ip.c_str(),
                node_ptr->bm_un.c_str(),
                node_ptr->bm_type.c_str(),
                node_ptr->bm_provisioned ? "Yes" : "No" );
    mem_log (str);
}

void nodeLinkClass::mem_log_identity ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\t%s %s (%u)\n",
                node_ptr->hostname.c_str(),
                node_ptr->uuid.c_str(),
                node_ptr->type.c_str(),
                node_ptr->nodetype);
    mem_log (str);
}

void nodeLinkClass::mem_log_state1 ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    string ad = adminState_enum_to_str(node_ptr->adminState) ;
    string op = operState_enum_to_str(node_ptr->operState) ;
    string av = availStatus_enum_to_str(node_ptr->availStatus);

    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\t%s-%s-%s    degrade_mask:%08x\n",
                node_ptr->hostname.c_str(),
                ad.c_str(),
                op.c_str(),
                av.c_str(),
                node_ptr->degrade_mask);
    mem_log (str);
    op = operState_enum_to_str(node_ptr->operState_subf) ;
    av = availStatus_enum_to_str(node_ptr->availStatus_subf);
    if ( ! node_ptr->subfunction_str.empty() )
    {
        snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tSub-Functions: %s-%s %s-%s-%s\n",
                node_ptr->hostname.c_str(),
                node_ptr->function_str.c_str(),
                node_ptr->subfunction_str.c_str(),
                ad.c_str(),
                op.c_str(),
                av.c_str());
    }
    mem_log (str);
}

void nodeLinkClass::mem_log_state2 ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    string aa = adminAction_enum_to_str(node_ptr->adminAction) ;

    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tmtcAction:%s invAction:%s Task:%s\n",
                node_ptr->hostname.c_str(),
                aa.c_str(),
                node_ptr->action.c_str(),
                node_ptr->task.c_str());
    mem_log (str);
}

void nodeLinkClass::mem_log_mtcalive ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;

    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tmtcAlive: on:%c off:%c Cnt:%d State:%s Misses:%d\n", 
                node_ptr->hostname.c_str(), 
                node_ptr->mtcAlive_online ? 'Y' : 'N',
                node_ptr->mtcAlive_offline ? 'Y' : 'N',
                node_ptr->mtcAlive_count,
                node_ptr->mtcAlive_gate ? "gated" : "rxing",
                node_ptr->mtcAlive_misses); 
    mem_log (str);
}

void nodeLinkClass::mem_log_alarm1 ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tAlarm List:%s%s%s%s%s%s\n", 
               node_ptr->hostname.c_str(), 
               node_ptr->alarms[MTC_ALARM_ID__LOCK    ] ? " Locked"   : " .",
               node_ptr->alarms[MTC_ALARM_ID__CONFIG  ] ? " Config"   : " .",
               node_ptr->alarms[MTC_ALARM_ID__ENABLE  ] ? " Enable"   : " .",
               node_ptr->alarms[MTC_ALARM_ID__CH_CONT ] ? " Control"  : " .",
               node_ptr->alarms[MTC_ALARM_ID__CH_COMP ] ? " Compute"  : " .",
               node_ptr->alarms[MTC_ALARM_ID__BM      ] ? " Brd Mgmt" : " .");
    mem_log (str);
}

void nodeLinkClass::mem_log_stage ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tAdd:%d Offline:%d: Swact:%d Recovery:%d Enable:%d Disable:%d\n",
                node_ptr->hostname.c_str(),
                node_ptr->addStage,
                node_ptr->offlineStage,
                node_ptr->swactStage,
                node_ptr->recoveryStage,
                node_ptr->enableStage,
                node_ptr->disableStage);
    mem_log (str);
}

void nodeLinkClass::mem_log_power_info ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tStage:%s Attempts:%d: Holdoff:%d Retry:%d State:%x ctid:%p rtid:%p\n",
                node_ptr->hostname.c_str(),
                get_powercycleStages_str(node_ptr->powercycleStage).c_str(),
                node_ptr->hwmon_powercycle.attempts,
                node_ptr->hwmon_powercycle.holdoff,
                node_ptr->hwmon_powercycle.retries,
                node_ptr->hwmon_powercycle.state,
                node_ptr->hwmon_powercycle.control_timer.tid,
                node_ptr->hwmon_powercycle.recovery_timer.tid);
    mem_log (str);
}

void nodeLinkClass::mem_log_reset_info ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tStage:%s Attempts:%d: Holdoff:%d Retry:%d State:%x ctid:%p rtid:%p\n",
                node_ptr->hostname.c_str(),
                get_resetStages_str(node_ptr->resetStage).c_str(),
                node_ptr->hwmon_reset.attempts,
                node_ptr->hwmon_reset.holdoff,
                node_ptr->hwmon_reset.retries,
                node_ptr->hwmon_reset.state,
                node_ptr->hwmon_reset.control_timer.tid,
                node_ptr->hwmon_reset.recovery_timer.tid);
    mem_log (str);
}

void nodeLinkClass::mem_log_network ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\t%s %s infra_ip: %s Uptime: %u\n", 
                node_ptr->hostname.c_str(), 
                node_ptr->mac.c_str(), 
                node_ptr->ip.c_str(),
                node_ptr->infra_ip.c_str(),
                node_ptr->uptime );
    mem_log (str);
}

void nodeLinkClass::mem_log_heartbeat ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
    {
        snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\t%s Minor:%s Degrade:%s Failed:%s  Monitor:%s\n", 
                   node_ptr->hostname.c_str(), 
                   get_iface_name_str (iface),
                   node_ptr->hbs_minor[iface] ? "true " : "false", 
                   node_ptr->hbs_degrade[iface] ? "true " : "false", 
                   node_ptr->hbs_failure[iface] ? "true " : "false",
                   node_ptr->monitor[iface] ? "YES" : "no"  );
        mem_log (str);
    }
}

void nodeLinkClass::mem_log_hbs_cnts ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    for ( int iface = 0 ; iface < MAX_IFACES ; iface++ )
    {
        snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\t%s Counts  Minor:%d Degrade:%d Failed:%d Misses:%d MaxB2BMisses:%d Cur:%d Tot:%d\n",
                   node_ptr->hostname.c_str(),
                   get_iface_name_str(iface),
                   node_ptr->hbs_minor_count[iface],
                   node_ptr->hbs_degrade_count[iface],
                   node_ptr->hbs_failure_count[iface],
                   node_ptr->hbs_misses_count[iface],
                   node_ptr->max_count[iface],
                   node_ptr->b2b_pulses_count[iface],
                   node_ptr->hbs_count[iface]);
        mem_log (str);
    }
}

void nodeLinkClass::mem_log_test_info ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tOOS Stage:%s Runs:%d - INSV Stage:%s Runs:%d\n", 
                node_ptr->hostname.c_str(), 
                get_oosTestStages_str(node_ptr->oosTestStage).c_str(),
                node_ptr->oos_test_count,
                get_insvTestStages_str(node_ptr->insvTestStage).c_str(),
                node_ptr->insv_test_count);
    mem_log (str);
}

void nodeLinkClass::mem_log_thread_info ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tThread Stage:%d Runs:%d Progress:%d Ctrl Status:%d Thread Status:%d\n",
                node_ptr->hostname.c_str(),
                node_ptr->ipmitool_thread_ctrl.stage,
                node_ptr->ipmitool_thread_ctrl.runcount,
                node_ptr->ipmitool_thread_info.progress,
                node_ptr->ipmitool_thread_ctrl.status,
                node_ptr->ipmitool_thread_info.status);
    mem_log (str);
}


void nodeLinkClass::mem_log_type_info ( struct nodeLinkClass::node * node_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tSystem:%d NodeMask: %x Function: %s (%u)\n",
                node_ptr->hostname.c_str(),
                this->system_type,
                node_ptr->nodetype,
                node_ptr->function_str.c_str(),
                node_ptr->function);
    mem_log (str);

    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
    {
        snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tSub-Function: %s (%u) (SubFunc Enabled:%c)\n",
                node_ptr->hostname.c_str(),
                node_ptr->subfunction_str.c_str(), node_ptr->subfunction,
                node_ptr->subf_enabled ? 'Y' : 'n' );
        mem_log (str);
    }
}

void mem_log_delimit_host ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "-------------------------------------------------------------\n");
    mem_log (str);
}

void nodeLinkClass::memDumpNodeState ( string hostname )
{
    nodeLinkClass::node* node_ptr ;
    node_ptr = nodeLinkClass::getNode ( hostname );
    if ( node_ptr == NULL )
    {
        mem_log ( hostname, ": ", "Not Found\n" );
        return ;
    }
    else
    {
        if ( maintenance == true )
        {
            mem_log_dor        ( node_ptr );
            mem_log_identity   ( node_ptr );
            mem_log_type_info  ( node_ptr );
            mem_log_network    ( node_ptr );
            mem_log_state1     ( node_ptr );
            mem_log_state2     ( node_ptr );
            // mem_log_reset_info ( node_ptr );
            mem_log_power_info ( node_ptr );
            mem_log_alarm1     ( node_ptr );
            mem_log_mtcalive   ( node_ptr );
            mem_log_stage      ( node_ptr );
            mem_log_bm         ( node_ptr );
            mem_log_test_info  ( node_ptr );
            mem_log_thread_info( node_ptr );
            workQueue_dump     ( node_ptr );
        }
        if ( heartbeat == true )
        {
            mem_log_heartbeat( node_ptr );
            mem_log_hbs_cnts ( node_ptr );
        }
        mem_log_delimit_host ();
    }
}

void nodeLinkClass::memDumpAllState ( void )
{
    mem_log_delimit_host ();
    mem_log_general ();

    if ( nodeLinkClass::maintenance == true )
    {
        mem_log_general_mtce_hosts();
        mem_log_mnfa ();
    }

    mem_log_delimit_host ();

    /* walk the node list looking for nodes that should be monitored */
    for ( struct node * ptr = head ; ptr != NULL ; ptr = ptr->next )
    {
        memDumpNodeState ( ptr->hostname );
    }
}

 
/***************************************************************************
 *                                                                         *
 *                       Module Test Head                                  *
 *                                                                         *
 ***************************************************************************/

int nodeLinkClass::testhead ( int test )
{
    UNUSED(test);
    return (PASS) ;
}
