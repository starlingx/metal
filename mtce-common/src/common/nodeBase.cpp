/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform "Node Base" Utility
  */
  
#include <iostream>
#include <string.h>
#include <stdio.h>

using namespace std;

#include "nodeBase.h"

/** Maintenance command request to heartbeat servcie message header */
const char mtc_hbs_cmd_req_header [MSG_HEADER_SIZE] = {"cgts mtc hbs cmd:"};

/** Maintenance Request Message header content    */
const char mtc_cmd_req_msg_header [MSG_HEADER_SIZE] = {"cgts mtc cmd req:"};

/** Maintenance Response Message header content    */
const char mtc_cmd_rsp_msg_header [MSG_HEADER_SIZE] = {"cgts mtc cmd rsp:"};

/** Maintenance Reply Message header content    */
const char mtc_msg_rep_msg_header [MSG_HEADER_SIZE] = {"cgts mtc rep msg:"};

/** Maintenance Log message header */
const char mtc_log_msg_hdr    [MSG_HEADER_SIZE]     = {"cgts mtc log msg:"};

/** Maintenance Message header content */
const char mtc_worker_msg_header [MSG_HEADER_SIZE] = {"cgts mtc message:"};

const char mtc_event_hdr [MSG_HEADER_SIZE] =          {"mtce event msg  :"};

const char mtc_pmond_pulse_header[MSG_HEADER_SIZE]  = {"pmond alive pulse"};

const char mtc_heartbeat_event_hdr[MSG_HEADER_SIZE] = {"heart beat event:"};

const char mtc_heartbeat_loss_hdr [MSG_HEADER_SIZE] = {"heart beat loss :"};
const char mtc_heartbeat_ready_hdr[MSG_HEADER_SIZE] = {"heart beat ready:"};
const char mtc_loopback_hdr       [MSG_HEADER_SIZE] = {"mtc loopback msg:"};

const char * get_loopback_header       (void) { return mtc_loopback_hdr;}
const char * get_hbs_cmd_req_header    (void) { return mtc_hbs_cmd_req_header ;}
const char * get_cmd_req_msg_header    (void) { return mtc_cmd_req_msg_header ;}
const char * get_cmd_rsp_msg_header    (void) { return mtc_cmd_rsp_msg_header ;}
const char * get_worker_msg_header     (void) { return mtc_worker_msg_header ;}
const char * get_pmond_pulse_header    (void) { return mtc_pmond_pulse_header ;}
const char * get_mtc_log_msg_hdr       (void) { return mtc_log_msg_hdr        ;}
const char * get_mtce_event_header     (void) { return mtc_event_hdr          ;}
const char * get_heartbeat_ready_header(void) { return mtc_heartbeat_ready_hdr;}
const char * get_heartbeat_loss_header (void) { return mtc_heartbeat_loss_hdr ;}
const char * get_heartbeat_event_header(void) { return mtc_heartbeat_event_hdr;}
const char * get_msg_rep_msg_header    (void) { return mtc_msg_rep_msg_header ;}

int print_mtc_message ( mtc_message_type * msg_ptr )
{
    if ( msg_ptr->buf[0] )
    {
        mlog1 ("Message: %d.%d.%d %x:%x:%x.%x.%x.%x [%s][%s]\n",
                msg_ptr->ver,
                msg_ptr->rev,
                msg_ptr->res,
                msg_ptr->cmd,
                msg_ptr->num,
                msg_ptr->parm[0],
                msg_ptr->parm[1],
                msg_ptr->parm[2],
                msg_ptr->parm[3],
                msg_ptr->hdr,
               &msg_ptr->buf[0]);
    }
    else
    {
        mlog1 ("Message: %d.%d.%d %x:%x:%x.%x.%x.%x [%s]\n",
                msg_ptr->ver,
                msg_ptr->rev,
                msg_ptr->res,
                msg_ptr->cmd,
                msg_ptr->num,
                msg_ptr->parm[0],
                msg_ptr->parm[1],
                msg_ptr->parm[2],
                msg_ptr->parm[3],
                msg_ptr->hdr);
    }
    return (PASS);
}

int print_mtc_message ( mtc_message_type * msg_ptr , bool force )
{
    if ( force == true )
    {
        if ( msg_ptr->buf[0] )
        {
             ilog ("Message: %d.%d.%u %x:%x:%x.%x.%x.%x [%s][%s]\n",
                    msg_ptr->ver,
                    msg_ptr->rev,
                    msg_ptr->res,
                    msg_ptr->cmd,
                    msg_ptr->num,
                    msg_ptr->parm[0],
                    msg_ptr->parm[1],
                    msg_ptr->parm[2],
                    msg_ptr->parm[3],
                    msg_ptr->hdr,
                   &msg_ptr->buf[0]);
        }
        else
        {
             mlog1("Message: %d.%d.%d %x:%x:%x.%x.%x.%x [%s]\n",
                    msg_ptr->ver,
                    msg_ptr->rev,
                    msg_ptr->res,
                    msg_ptr->cmd,
                    msg_ptr->num,
                    msg_ptr->parm[0],
                    msg_ptr->parm[1],
                    msg_ptr->parm[2],
                    msg_ptr->parm[3],
                    msg_ptr->hdr);
        }
    }
    else
    {
        print_mtc_message ( msg_ptr );
    }
    return (PASS);
}

const char * get_mtcNodeCommand_str ( int cmd )
{
    switch ( cmd )
    {
        case MTC_CMD_NONE:        return ("none" );

        /* general action command */
        case MTC_CMD_LOOPBACK:    return ("loopback");
        case MTC_CMD_REBOOT:      return ("reboot");
        case MTC_CMD_WIPEDISK:    return ("wipedisk");
        case MTC_CMD_RESET:       return ("reset");
        case MTC_MSG_MTCALIVE:    return ("mtcAlive msg");
        case MTC_REQ_MTCALIVE:    return ("mtcAlive req");
        case MTC_MSG_LOCKED:      return ("locked msg");
        case MTC_CMD_LAZY_REBOOT: return ("lazy reboot");

        /* goenabled commands and messages */
        case MTC_MSG_MAIN_GOENABLED:         return ("goEnabled main msg");
        case MTC_MSG_SUBF_GOENABLED:         return ("goEnabled subf msg");
        case MTC_REQ_MAIN_GOENABLED:         return ("goEnabled main req");
        case MTC_REQ_SUBF_GOENABLED:         return ("goEnabled subf req");
        case MTC_MSG_MAIN_GOENABLED_FAILED:  return ("goEnabled main failed");
        case MTC_MSG_SUBF_GOENABLED_FAILED:  return ("goEnabled subf failed");

        /* start and stop services commands and messages */
        case MTC_CMD_STOP_CONTROL_SVCS:      return ("stop controller host services");
        case MTC_CMD_STOP_WORKER_SVCS:       return ("stop worker host services");
        case MTC_CMD_STOP_STORAGE_SVCS:      return ("stop storage host services");
        case MTC_CMD_START_CONTROL_SVCS:     return ("start controller host services");
        case MTC_CMD_START_WORKER_SVCS:      return ("start worker host services");
        case MTC_CMD_START_STORAGE_SVCS:     return ("start storage host services");
        case MTC_CMD_HOST_SVCS_RESULT:       return ("host services result");

        /* Heartbeat Control Commands */
        case MTC_RESTART_HBS:  return("heartbeat restart");
        case MTC_BACKOFF_HBS:  return("heartbeat backoff");
        case MTC_RECOVER_HBS:  return("heartbeat recover");

        /* heartbeat service messages */
        case MTC_EVENT_HEARTBEAT_READY:      return("heartbeat ready event");
        case MTC_EVENT_HEARTBEAT_LOSS:       return("heartbeat loss");
        case MTC_EVENT_HEARTBEAT_RUNNING:    return("heartbeat running");
        case MTC_EVENT_HEARTBEAT_ILLHEALTH:  return("heartbeat illhealth");
        case MTC_EVENT_HEARTBEAT_STOPPED:    return("heartbeat stopped");
        case MTC_EVENT_HEARTBEAT_DEGRADE_SET:return("heartbeat degrade set");
        case MTC_EVENT_HEARTBEAT_MINOR_CLR:  return("heartbeat minor clear");
        case MTC_EVENT_HEARTBEAT_DEGRADE_CLR:return("heartbeat degrade clear");
        case MTC_EVENT_HEARTBEAT_MINOR_SET:  return("heartbeat minor set");

        /* degrade events */
        case MTC_DEGRADE_RAISE:       return ("degrade raise");
        case MTC_DEGRADE_CLEAR:       return ("degrade clear");

        /* general events */
        case MTC_EVENT_LOOPBACK:      return ("loopback");
        case MTC_EVENT_MONITOR_READY: return ("monitor ready event");
        case MTC_EVENT_GOENABLE_FAIL: return ("goenable fail");
        case MTC_EVENT_HOST_STALLED: return("host stalled event");

        /* pmon events */
        case MTC_EVENT_PMON_CLEAR: return("pmon degrade clear");
        case MTC_EVENT_PMON_CRIT:  return("pmon critical event");
        case MTC_EVENT_PMON_MAJOR: return("pmon major event");
        case MTC_EVENT_PMON_MINOR: return("pmon minor event");
        case MTC_EVENT_PMON_LOG:   return("pmon log");
        case MTC_EVENT_PMOND_RAISE: return("pmon raise");

        /* data port events */
        case MTC_EVENT_AVS_CLEAR:    return("AVS clear");
        case MTC_EVENT_AVS_MAJOR:    return("AVS major");
        case MTC_EVENT_AVS_CRITICAL: return("AVS critical");
        case MTC_EVENT_AVS_OFFLINE:  return("AVS offline");

        /* hardware Monitor events */
        case MTC_EVENT_HWMON_CONFIG:    return("hwmon config event");   /* OBS */
        case MTC_EVENT_HWMON_CLEAR:     return("hwmon clear");
        case MTC_EVENT_HWMON_MINOR:     return("hwmon minor event");
        case MTC_EVENT_HWMON_MAJOR:     return("hwmon major event");
        case MTC_EVENT_HWMON_CRIT:      return("hwmon critical event");
        case MTC_EVENT_HWMON_RESET:     return("hwmon reset event");
        case MTC_EVENT_HWMON_LOG:       return("hwmon log");
        case MTC_EVENT_HWMON_POWERDOWN: return("hwmon powerdown event"); /* OBS */
        case MTC_EVENT_HWMON_POWERCYCLE:return("hwmon powercycle event");

        /* Host Commands */
        case MTC_CMD_ADD_HOST:      return("add host");
        case MTC_CMD_DEL_HOST:      return("del host");
        case MTC_CMD_MOD_HOST:      return("modify host");
        case MTC_CMD_QRY_HOST:      return("query host");
        case MTC_CMD_START_HOST:    return("start host service");
        case MTC_CMD_STOP_HOST:     return("stop host service");
        case MTC_CMD_ACTIVE_CTRL:   return("publish active controller");

        /* VM Instance Commands */
        case MTC_CMD_ADD_INST:      return("add instance");
        case MTC_CMD_DEL_INST:      return("delete instance");
        case MTC_CMD_MOD_INST:      return("modify instance");
        case MTC_CMD_QRY_INST:      return("query instance");
        case MTC_CMD_VOTE_INST:     return ("vote instance");
        case MTC_CMD_NOTIFY_INST:   return ("notify instance");
        case MTC_EVENT_VOTE_NOTIFY: return ("notify instance event");

        /* service events */
        case MTC_SERVICE_PMOND:     return ("pmond service");
        case MTC_SERVICE_HWMOND:    return ("hwmond service");
        case MTC_SERVICE_HEARTBEAT: return ("heartbeat service");
        default:
            break ;
    }
    return ( "unknown");
}


void print_mtc_message ( string hostname,
                         int direction,
                         mtc_message_type & msg,
                         const char * iface,
                         bool force )
{
    /* Handle raw json string messages differently.
     * Those messages just have a json string that starts at the header */
    if ( msg.hdr[0] == '{' )
    {
        if ( force )
        {
            ilog ("%s %s (%s network) - %s\n",
                      hostname.c_str(),
                      direction ? "rx <-" : "tx ->" ,
                      iface,
                      msg.hdr);
        }
        else if (( daemon_get_cfg_ptr()->debug_alive&1) && ( msg.cmd  == MTC_MSG_MTCALIVE ))
        {
            alog  ("%s %s (%s network) - %s\n",
                       hostname.c_str(),
                       direction ? "rx <-" : "tx ->" ,
                       iface,
                       msg.hdr);
        }
        else
        {
            mlog1 ("%s %s (%s network) - %s\n",
                       hostname.c_str(),
                       direction ? "rx <-" : "tx ->" ,
                       iface,
                       msg.hdr);
        }
        return ;
    }

    string str = "" ;
    if ( msg.buf[0] )
        str = msg.buf ;
    if ( force )
    {
        ilog ("%s %s %s (%s network) %d.%d %x:%x:%x.%x.%x.%x [%s] %s\n",
                hostname.c_str(),
                direction ? "rx <-" : "tx ->" ,
                get_mtcNodeCommand_str (msg.cmd),
                iface,
                msg.ver,
                msg.rev,
                msg.cmd,
                msg.num,
                msg.parm[0],
                msg.parm[1],
                msg.parm[2],
                msg.parm[3],
                msg.hdr,
                str.c_str());
    }
    else
    {
        mlog1 ("%s %s %s (%s network) %d.%d %x:%x:%x.%x.%x.%x [%s] %s\n",
                hostname.c_str(),
                direction ? "rx <-" : "tx ->" ,
                get_mtcNodeCommand_str (msg.cmd),
                iface,
                msg.ver,
                msg.rev,
                msg.cmd,
                msg.num,
                msg.parm[0],
                msg.parm[1],
                msg.parm[2],
                msg.parm[3],
                msg.hdr,
                str.c_str());
    }
}

/* Graceful recovery stages strings and string get'er */
static std::string   recoveryStages_str [MTC_RECOVERY__STAGES   +1] ;
static std::string    disableStages_str [MTC_DISABLE__STAGES    +1] ;
static std::string     enableStages_str [MTC_ENABLE__STAGES     +1] ;
static std::string     sensorStages_str [MTC_SENSOR__STAGES     +1] ;
static std::string      powerStages_str [MTC_POWER__STAGES      +1] ;
static std::string powercycleStages_str [MTC_POWERCYCLE__STAGES +1] ;
static std::string      resetStages_str [MTC_RESET__STAGES      +1] ;
static std::string  reinstallStages_str [MTC_REINSTALL__STAGES  +1] ;
static std::string    oosTestStages_str [MTC_OOS_TEST__STAGES   +1] ;
static std::string   insvTestStages_str [MTC_INSV_TEST__STAGES  +1] ;
static std::string     configStages_str [MTC_CONFIG__STAGES     +1] ;
static std::string        addStages_str [MTC_ADD__STAGES        +1] ;
static std::string        delStages_str [MTC_DEL__STAGES        +1] ;
static std::string        subStages_str [MTC_SUBSTAGE__STAGES   +1] ;

void mtc_stages_init ( void )
{
   enableStages_str  [MTC_ENABLE__START                ] = "Handler-Start";
   enableStages_str  [MTC_ENABLE__RESERVED_1           ] = "reserved 1";
   enableStages_str  [MTC_ENABLE__HEARTBEAT_CHECK      ] = "Heartbeat-Check";
   enableStages_str  [MTC_ENABLE__HEARTBEAT_STOP_CMD   ] = "Heartbeat-Stop";
   enableStages_str  [MTC_ENABLE__RECOVERY_TIMER       ] = "Recovery-Start";
   enableStages_str  [MTC_ENABLE__RECOVERY_WAIT        ] = "Recovery-Wait";
   enableStages_str  [MTC_ENABLE__RESET_PROGRESSION    ] = "Reset-Prog";
   enableStages_str  [MTC_ENABLE__RESET_WAIT           ] = "Reset-Prog-Wait";
   enableStages_str  [MTC_ENABLE__INTEST_START         ] = "Intest-Start";
   enableStages_str  [MTC_ENABLE__MTCALIVE_PURGE       ] = "MtcAlive-Purge";
   enableStages_str  [MTC_ENABLE__MTCALIVE_WAIT        ] = "MtcAlive-Wait";
   enableStages_str  [MTC_ENABLE__CONFIG_COMPLETE_WAIT ] = "Config-Complete-Wait";
   enableStages_str  [MTC_ENABLE__GOENABLED_TIMER      ] = "GoEnable-Start";
   enableStages_str  [MTC_ENABLE__GOENABLED_WAIT       ] = "GoEnable-Wait";
   enableStages_str  [MTC_ENABLE__PMOND_READY_WAIT     ] = "PmondReady-Wait";
   enableStages_str  [MTC_ENABLE__HOST_SERVICES_START  ] = "HostServices-Start";
   enableStages_str  [MTC_ENABLE__HOST_SERVICES_WAIT   ] = "HostServices-Wait";
   enableStages_str  [MTC_ENABLE__SERVICES_START_WAIT  ] = "Services-Start";
   enableStages_str  [MTC_ENABLE__HEARTBEAT_WAIT       ] = "Heartbeat-Wait";
   enableStages_str  [MTC_ENABLE__HEARTBEAT_SOAK       ] = "Heartbeat-Soak";
   enableStages_str  [MTC_ENABLE__STATE_CHANGE         ] = "State-Change";
   enableStages_str  [MTC_ENABLE__WORKQUEUE_WAIT       ] = "WorkQueue-Wait";
   enableStages_str  [MTC_ENABLE__WAIT                 ] = "Enable-Wait";
   enableStages_str  [MTC_ENABLE__DONE                 ] = "Enable-Done";
   enableStages_str  [MTC_ENABLE__ENABLED              ] = "Host-Enabled";
   enableStages_str  [MTC_ENABLE__SUBF_FAILED          ] = "Host-Degraded-Subf-Failed";
   enableStages_str  [MTC_ENABLE__DEGRADED             ] = "Host-Degraded";
   enableStages_str  [MTC_ENABLE__FAILURE              ] = "Failure";
   enableStages_str  [MTC_ENABLE__FAILURE_WAIT         ] = "Failure-Wait";
   enableStages_str  [MTC_ENABLE__FAILURE_SWACT_WAIT   ] = "Failure-Swact-Wait";
   enableStages_str  [MTC_ENABLE__STAGES               ] = "unknown" ;

   recoveryStages_str[MTC_RECOVERY__START              ] = "Handler-Start";
   recoveryStages_str[MTC_RECOVERY__RETRY_WAIT         ] = "Req-Retry-Wait";
   recoveryStages_str[MTC_RECOVERY__REQ_MTCALIVE       ] = "Req-MtcAlive";
   recoveryStages_str[MTC_RECOVERY__REQ_MTCALIVE_WAIT  ] = "Req-MtcAlive-Wait";
   recoveryStages_str[MTC_RECOVERY__RESET_RECV_WAIT    ] = "Reset-Recv-Wait";
   recoveryStages_str[MTC_RECOVERY__MTCALIVE_TIMER     ] = "MtcAlive-Timer";
   recoveryStages_str[MTC_RECOVERY__MTCALIVE_WAIT      ] = "MtcAlive-Wait";
   recoveryStages_str[MTC_RECOVERY__GOENABLED_TIMER    ] = "GoEnable-Timer";
   recoveryStages_str[MTC_RECOVERY__GOENABLED_WAIT     ] = "GoEnable-Wait";
   recoveryStages_str[MTC_RECOVERY__HOST_SERVICES_START] = "HostServices-Start";
   recoveryStages_str[MTC_RECOVERY__HOST_SERVICES_WAIT ] = "HostServices-Wait";
   recoveryStages_str[MTC_RECOVERY__CONFIG_COMPLETE_WAIT]= "Compute-Config-Wait";
   recoveryStages_str[MTC_RECOVERY__SUBF_GOENABLED_TIMER]= "Subf-GoEnable-Timer";
   recoveryStages_str[MTC_RECOVERY__SUBF_GOENABLED_WAIT] = "Subf-GoEnable-Wait";
   recoveryStages_str[MTC_RECOVERY__SUBF_SERVICES_START] = "Subf-Services-Start";
   recoveryStages_str[MTC_RECOVERY__SUBF_SERVICES_WAIT ] = "Subf-Services-Wait";
   recoveryStages_str[MTC_RECOVERY__HEARTBEAT_START    ] = "Heartbeat-Start";
   recoveryStages_str[MTC_RECOVERY__HEARTBEAT_SOAK     ] = "Heartbeat-Soak";
   recoveryStages_str[MTC_RECOVERY__STATE_CHANGE       ] = "State Change";
   recoveryStages_str[MTC_RECOVERY__ENABLE_START       ] = "Enable-Start";
   recoveryStages_str[MTC_RECOVERY__FAILURE            ] = "Failure";
   recoveryStages_str[MTC_RECOVERY__WORKQUEUE_WAIT     ] = "WorkQ-Wait";
   recoveryStages_str[MTC_RECOVERY__ENABLE_WAIT        ] = "Enable-Wait";
   recoveryStages_str[MTC_RECOVERY__STAGES             ] = "unknown";

   disableStages_str [MTC_DISABLE__START               ] = "Disable-Start";
   disableStages_str [MTC_DISABLE__HANDLE_POWERON_SEND ] = "Disable-PowerOn-Send";
   disableStages_str [MTC_DISABLE__HANDLE_POWERON_RECV ] = "Disable-PowerOn-Recv";
   disableStages_str [MTC_DISABLE__HANDLE_FORCE_LOCK   ] = "Disable-Force-Lock";
   disableStages_str [MTC_DISABLE__RESET_HOST_WAIT     ] = "Disable-Reset-Wait";
   disableStages_str [MTC_DISABLE__DISABLE_SERVICES    ] = "Disable-Services-Start";
   disableStages_str [MTC_DISABLE__DIS_SERVICES_WAIT   ] = "Disable-Services-Wait";
   disableStages_str [MTC_DISABLE__HANDLE_CEPH_LOCK    ] = "Disable-Ceph-Lock-Wait";
   disableStages_str [MTC_DISABLE__RESERVED            ] = "Disable-reserved";
   disableStages_str [MTC_DISABLE__TASK_STATE_UPDATE   ] = "Disable-States-Update";
   disableStages_str [MTC_DISABLE__WORKQUEUE_WAIT      ] = "Disable-WorkQ-Wait";
   disableStages_str [MTC_DISABLE__DISABLED            ] = "Host-Disabled";
   disableStages_str [MTC_DISABLE__STAGES              ] = "Unknown";

   powerStages_str   [MTC_POWERON__START               ] = "Power-On-Start";
   powerStages_str   [MTC_POWERON__POWER_STATUS_WAIT   ] = "Power-On-Status";
   powerStages_str   [MTC_POWERON__POWER_STATUS_WAIT   ] = "Power-On-Status-Wait";
   powerStages_str   [MTC_POWERON__REQ_SEND            ] = "Power-On-Req-Send";
   powerStages_str   [MTC_POWERON__RESP_WAIT           ] = "Power-On-Resp-Wait";
   powerStages_str   [MTC_POWERON__DONE                ] = "Power-On-Done";
   powerStages_str   [MTC_POWERON__FAIL                ] = "Power-On-Fail";
   powerStages_str   [MTC_POWERON__FAIL_WAIT           ] = "Power-On-Fail-Wait";
   powerStages_str   [MTC_POWEROFF__START              ] = "Power-Off-Start";
   powerStages_str   [MTC_POWEROFF__REQ_SEND           ] = "Power-Off-Req-Send";
   powerStages_str   [MTC_POWEROFF__RESP_WAIT          ] = "Power-Off-Resp-Wait";
   powerStages_str   [MTC_POWEROFF__OFFLINE_WAIT       ] = "Power-Off-Offline-Wait";
   powerStages_str   [MTC_POWEROFF__POWERQRY           ] = "Power-Off-Power-Query";
   powerStages_str   [MTC_POWEROFF__POWERQRY_WAIT      ] = "Power-Off-Power-Query-Wait";
   powerStages_str   [MTC_POWEROFF__QUEUE              ] = "Power-Off-Queue";
   powerStages_str   [MTC_POWEROFF__DONE               ] = "Power-Off-Done";
   powerStages_str   [MTC_POWEROFF__FAIL               ] = "Power-Off-Fail";
   powerStages_str   [MTC_POWEROFF__FAIL_WAIT          ] = "Power-Off-Fail-Wait";
   powerStages_str   [MTC_POWER__DONE                  ] = "Power-Done";
   powerStages_str   [MTC_POWER__STAGES                ] = "Power-Unknown";


   powercycleStages_str [MTC_POWERCYCLE__START          ] = "Power-Cycle-Start";
   powercycleStages_str [MTC_POWERCYCLE__POWEROFF       ] = "Power-Cycle-Off";
   powercycleStages_str [MTC_POWERCYCLE__POWEROFF_WAIT  ] = "Power-Cycle-Off-Wait";
   powercycleStages_str [MTC_POWERCYCLE__POWERON        ] = "Power-Cycle-On";
   powercycleStages_str [MTC_POWERCYCLE__POWERON_REQWAIT] = "Power-Cycle-On-Req-Wait";
   powercycleStages_str [MTC_POWERCYCLE__POWERON_VERIFY]  = "Power-Cycle-On-Verify";
   powercycleStages_str [MTC_POWERCYCLE__POWERON_WAIT   ] = "Power-Cycle-On-Wait";
   powercycleStages_str [MTC_POWERCYCLE__DONE           ] = "Power-Cycle-Done";
   powercycleStages_str [MTC_POWERCYCLE__FAIL           ] = "Power-Cycle-Fail";
   powercycleStages_str [MTC_POWERCYCLE__HOLDOFF        ] = "Power-Cycle-Hold-Off";
   powercycleStages_str [MTC_POWERCYCLE__COOLOFF        ] = "Power-Cycle-Cool-Off";

   powercycleStages_str [MTC_POWERCYCLE__POWEROFF_CMND_WAIT] = "Power-Cycle-Off-Cmnd-Wait";
   powercycleStages_str [MTC_POWERCYCLE__POWERON_CMND_WAIT]  = "Power-Cycle-On-Cmnd-Wait";
   powercycleStages_str [MTC_POWERCYCLE__POWERON_VERIFY_WAIT]= "Power-Cycle-On-Verify-Wait";


   resetStages_str   [MTC_RESET__START                 ] = "Reset-Start";
   resetStages_str   [MTC_RESET__REQ_SEND              ] = "Reset-Req-Send";
   resetStages_str   [MTC_RESET__RESP_WAIT             ] = "Reset-Resp-Wait";
   resetStages_str   [MTC_RESET__QUEUE                 ] = "Reset-Queue";
   resetStages_str   [MTC_RESET__OFFLINE_WAIT          ] = "Reset-Offline-Wait";
   resetStages_str   [MTC_RESET__DONE                  ] = "Reset-Done";
   resetStages_str   [MTC_RESET__FAIL                  ] = "Reset-Fail";
   resetStages_str   [MTC_RESET__FAIL_WAIT             ] = "Reset-Fail-Wait";
   resetStages_str   [MTC_RESET__STAGES                ] = "Reset-Unknown";

   reinstallStages_str   [MTC_REINSTALL__START         ] = "Reinstall-Start";
   reinstallStages_str   [MTC_REINSTALL__START_WAIT    ] = "Reinstall-Start-Wait";
   reinstallStages_str   [MTC_REINSTALL__POWERQRY      ] = "Reinstall-Power-State-Query";
   reinstallStages_str   [MTC_REINSTALL__POWERQRY_WAIT ] = "Reinstall-Power-State-Query-Wait";
   reinstallStages_str   [MTC_REINSTALL__RESTART       ] = "Reinstall-ReStart";
   reinstallStages_str   [MTC_REINSTALL__RESTART_WAIT  ] = "Reinstall-ReStart-Wait";
   reinstallStages_str   [MTC_REINSTALL__POWEROFF      ] = "Reinstall-PowerOff";
   reinstallStages_str   [MTC_REINSTALL__POWEROFF_WAIT ] = "Reinstall-PowerOff-Wait";
   reinstallStages_str   [MTC_REINSTALL__NETBOOT       ] = "Reinstall-Netboot";
   reinstallStages_str   [MTC_REINSTALL__NETBOOT_WAIT  ] = "Reinstall-Netboot-Wait";
   reinstallStages_str   [MTC_REINSTALL__POWERON       ] = "Reinstall-PowerOn";
   reinstallStages_str   [MTC_REINSTALL__POWERON_WAIT  ] = "Reinstall-PowerOn-Wait";
   reinstallStages_str   [MTC_REINSTALL__WIPEDISK      ] = "Reinstall-Wipedisk";
   reinstallStages_str   [MTC_REINSTALL__WIPEDISK_WAIT ] = "Reinstall-Wipedisk-Wait";
   reinstallStages_str   [MTC_REINSTALL__OFFLINE_WAIT  ] = "Reinstall-Offline-Wait";
   reinstallStages_str   [MTC_REINSTALL__ONLINE_WAIT   ] = "Reinstall-Online-Wait";
   reinstallStages_str   [MTC_REINSTALL__FAIL          ] = "Reinstall-Failure";
   reinstallStages_str   [MTC_REINSTALL__MSG_DISPLAY   ] = "Reinstall-Message-Display";
   reinstallStages_str   [MTC_REINSTALL__DONE          ] = "Reinstall-Done";
   reinstallStages_str   [MTC_REINSTALL__STAGES        ] = "Reinstall-Unknown";

   oosTestStages_str [MTC_OOS_TEST__LOAD_NEXT_TEST     ] = "Test-Load-Next";
   oosTestStages_str [MTC_OOS_TEST__BMC_ACCESS_TEST    ] = "Test-BMC-Access-Test";
   oosTestStages_str [MTC_OOS_TEST__BMC_ACCESS_RESULT  ] = "Test-BMC-Access-Result";
   oosTestStages_str [MTC_OOS_TEST__START_WAIT         ] = "Test-Start-Wait";
   oosTestStages_str [MTC_OOS_TEST__WAIT               ] = "Test-Wait";
   oosTestStages_str [MTC_OOS_TEST__DONE               ] = "Test-Done";
   oosTestStages_str [MTC_OOS_TEST__STAGES             ] = "Test-Unknown";

   insvTestStages_str[MTC_INSV_TEST__START             ] = "Test-Start";
   insvTestStages_str[MTC_INSV_TEST__WAIT              ] = "Test-Wait";
   insvTestStages_str[MTC_INSV_TEST__RUN               ] = "Test-Run";
   insvTestStages_str[MTC_INSV_TEST__STAGES            ] = "Test-Unknown";

   sensorStages_str  [MTC_SENSOR__START                ] = "Sensor-Read-Start";
   sensorStages_str  [MTC_SENSOR__READ_FAN             ] = "Sensor-Read-Fans";
   sensorStages_str  [MTC_SENSOR__READ_TEMP            ] = "Sensor-Read-Temp";
   sensorStages_str  [MTC_SENSOR__STAGES               ] = "Sensor-Unknown";

   configStages_str  [MTC_CONFIG__START                ] = "Config-Start";
   configStages_str  [MTC_CONFIG__SHOW                 ] = "Config-Show";
   configStages_str  [MTC_CONFIG__MODIFY               ] = "Config-Modify";
   configStages_str  [MTC_CONFIG__VERIFY               ] = "Config-Verify";
   configStages_str  [MTC_CONFIG__FAILURE              ] = "Config-Fail";
   configStages_str  [MTC_CONFIG__TIMEOUT              ] = "Config-Timeout";
   configStages_str  [MTC_CONFIG__DONE                 ] = "Config-Done";
   configStages_str  [MTC_CONFIG__STAGES               ] = "Config-Unknown";

   addStages_str     [MTC_ADD__START                   ] = "Add-Start";
   addStages_str     [MTC_ADD__START_DELAY             ] = "Add-Start-Delay";
   addStages_str     [MTC_ADD__START_SERVICES          ] = "Add-Start-Services";
   addStages_str     [MTC_ADD__START_SERVICES_WAIT     ] = "Add-Start-Services-Wait";
// addStages_str     [MTC_ADD__CLEAR_ALARMS            ] = "Add-Clear-Alarms";
   addStages_str     [MTC_ADD__MTC_SERVICES            ] = "Add-Mtc-Services";
   addStages_str     [MTC_ADD__CLEAR_TASK              ] = "Add-Clear-Task";
   addStages_str     [MTC_ADD__WORKQUEUE_WAIT          ] = "Add-WorkQ-Wait";
   addStages_str     [MTC_ADD__DONE                    ] = "Add-Done";
   addStages_str     [MTC_ADD__STAGES                  ] = "Add-Unknown";

   delStages_str     [MTC_DEL__START                   ] = "Del-Start";
   delStages_str     [MTC_DEL__WAIT                    ] = "Del-Wait";
   delStages_str     [MTC_DEL__DONE                    ] = "Del-Done";

   subStages_str     [MTC_SUBSTAGE__START              ] = "subStage-Start";
   subStages_str     [MTC_SUBSTAGE__SEND               ] = "subStage-Send";
   subStages_str     [MTC_SUBSTAGE__RECV               ] = "subStage-Recv";
   subStages_str     [MTC_SUBSTAGE__WAIT               ] = "subStage-Wait";
   subStages_str     [MTC_SUBSTAGE__DONE               ] = "subStage-Done";
   subStages_str     [MTC_SUBSTAGE__FAIL               ] = "subStage-Fail";
}

string get_delStages_str ( mtc_delStages_enum stage )
{
    if ( stage >= MTC_DEL__STAGES )
    {
        return (delStages_str[MTC_DEL__STAGES]);
    }
    return (delStages_str[stage]);
}

/* Get the specified 'enable' stage string */
string get_enableStages_str ( mtc_enableStages_enum stage )
{
    if ( stage >= MTC_ENABLE__STAGES )
    {
        return (enableStages_str[MTC_ENABLE__STAGES]);
    }
    return (enableStages_str[stage]);
}

/* Get the specified 'recovery' stage string */
string get_recoveryStages_str ( mtc_recoveryStages_enum stage )
{
    if ( stage >= MTC_RECOVERY__STAGES )
    {
        return (recoveryStages_str[MTC_RECOVERY__STAGES]);
    }
    return (recoveryStages_str[stage]);
}

/* Get the specified 'config' stage string */
string get_configStages_str ( mtc_configStages_enum stage )
{
    if ( stage >= MTC_CONFIG__STAGES )
    {
        return (configStages_str[MTC_CONFIG__STAGES]);
    }
    return (configStages_str[stage]);
}

/* Get the specified 'disable' stage string */
string get_disableStages_str ( mtc_disableStages_enum stage )
{
    if ( stage >= MTC_DISABLE__STAGES )
    {
        return (disableStages_str[MTC_DISABLE__STAGES]);
    }
    return (disableStages_str[stage]);
}

/* Get the specified 'power' stage string */
string get_powerStages_str ( mtc_powerStages_enum stage )
{
    if ( stage >= MTC_POWER__STAGES )
    {
        return (powerStages_str[MTC_POWER__STAGES]);
    }
    return (powerStages_str[stage]);
}

/* Get the specified 'powercycle' stage string */
string get_powercycleStages_str ( mtc_powercycleStages_enum stage )
{
    if ( stage >= MTC_POWERCYCLE__STAGES )
    {
        return (powercycleStages_str[MTC_POWERCYCLE__STAGES]);
    }
    return (powercycleStages_str[stage]);
}

/* Get the specified 'reset' stage string */
string get_resetStages_str ( mtc_resetStages_enum stage )
{
    if ( stage >= MTC_RESET__STAGES )
    {
        return (resetStages_str[MTC_RESET__STAGES]);
    }
    return (resetStages_str[stage]);
}

/* Get the specified 'reinstall' stage string */
string get_reinstallStages_str ( mtc_reinstallStages_enum stage )
{
    if ( stage >= MTC_REINSTALL__STAGES )
    {
        return (reinstallStages_str[MTC_REINSTALL__STAGES]);
    }
    return (reinstallStages_str[stage]);
}

/* Get the specified 'out-of-service test' stage string */
string get_oosTestStages_str ( mtc_oosTestStages_enum stage )
{
    if ( stage >= MTC_OOS_TEST__STAGES )
    {
        return (oosTestStages_str[MTC_OOS_TEST__STAGES]);
    }
    return (oosTestStages_str[stage]);
}

/* Get the specified 'in-service test' stage string */
string get_insvTestStages_str ( mtc_insvTestStages_enum stage )
{
    if ( stage >= MTC_INSV_TEST__STAGES )
    {
        return (insvTestStages_str[MTC_INSV_TEST__STAGES]);
    }
    return (insvTestStages_str[stage]);
}

string get_sensorStages_str ( mtc_sensorStages_enum stage )
{
    if ( stage >= MTC_SENSOR__STAGES )
    {
        return (sensorStages_str[MTC_SENSOR__STAGES]);
    }
    return (sensorStages_str[stage]);
}

/** Return the string representing the specified 'sub' stage */
string get_subStages_str ( mtc_subStages_enum stage )
{
    if ( stage >= MTC_SUBSTAGE__STAGES )
    {
        return (subStages_str[MTC_SUBSTAGE__STAGES]);
    }
    return (subStages_str[stage]);
}

void log_adminAction ( string hostname, 
                       mtc_nodeAdminAction_enum currAction, 
                       mtc_nodeAdminAction_enum  newAction )
{
    if (( currAction != MTC_ADMIN_ACTION__LOCK ) &&
        (  newAction == MTC_ADMIN_ACTION__LOCK ))
    {
        ilog ("%s Lock Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__FORCE_LOCK ) &&
             (  newAction == MTC_ADMIN_ACTION__FORCE_LOCK ))
    {
        ilog ("%s Lock Action (Force)\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__UNLOCK ) &&
             (  newAction == MTC_ADMIN_ACTION__UNLOCK ))
    {
        ilog ("%s Unlock Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__SWACT ) &&
             (  newAction == MTC_ADMIN_ACTION__SWACT ))
    {
        ilog ("%s Swact Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__FORCE_SWACT ) &&
             (  newAction == MTC_ADMIN_ACTION__FORCE_SWACT ))
    {
        ilog ("%s Swact Action (Force)\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__ADD ) &&
             (  newAction == MTC_ADMIN_ACTION__ADD ))
    {
        ilog ("%s Add Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__RESET ) &&
             (  newAction == MTC_ADMIN_ACTION__RESET ))
    {
        ilog ("%s Reset Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__REBOOT ) &&
             (  newAction == MTC_ADMIN_ACTION__REBOOT ))
    {
        ilog ("%s Reboot Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__REINSTALL ) &&
             (  newAction == MTC_ADMIN_ACTION__REINSTALL ))
    {
        ilog ("%s Reinstall Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__POWEROFF ) &&
             (  newAction == MTC_ADMIN_ACTION__POWEROFF ))
    {
        ilog ("%s Power-Off Action\n", hostname.c_str());
    }
    else if (( currAction != MTC_ADMIN_ACTION__POWERON ) &&
             (  newAction == MTC_ADMIN_ACTION__POWERON ))
    {
        ilog ("%s Power-On Action\n", hostname.c_str());
    }
}

/* Init recovery control structure */
void recovery_ctrl_init ( recovery_ctrl_type & recovery_ctrl )
{
    recovery_ctrl.state = RECOVERY_STATE__INIT ;
    recovery_ctrl.attempts = 0 ;
    recovery_ctrl.holdoff  = 0 ;
    recovery_ctrl.queries  = 0 ;
    recovery_ctrl.retries  = 0 ;
}

/* returns 'true' if the specified command is a host services command */
bool is_host_services_cmd ( unsigned int cmd )
{
    if (( cmd == MTC_CMD_START_CONTROL_SVCS ) ||
        ( cmd == MTC_CMD_START_WORKER_SVCS ) ||
        ( cmd == MTC_CMD_START_STORAGE_SVCS ) ||
        ( cmd == MTC_CMD_STOP_CONTROL_SVCS  ) ||
        ( cmd == MTC_CMD_STOP_WORKER_SVCS  ) ||
        ( cmd == MTC_CMD_STOP_STORAGE_SVCS  ) ||
        ( cmd == MTC_CMD_HOST_SVCS_RESULT ))
    {
        return (true);
    }
    return (false);
}

/* Used to fill the mtce message buffer starting after supplied 'bytes' count */
void zero_unused_msg_buf ( mtc_message_type & msg, int bytes)
{
    if ( bytes < (int)sizeof(msg) )
    {
        char * ptr = (char *)&msg ;
        ptr += (bytes) ;
        memset ( ptr, 0, sizeof(msg)-bytes);
    }
}

