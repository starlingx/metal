/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include "nodeBase.h"
#include "tokenUtil.h"
#include "secretUtil.h"
#include "hwmonClass.h"
#include "hwmonUtil.h"
#include "hwmonIpmi.h"
#include "hwmonHttp.h"
#include "hwmonAlarm.h"
#include "hwmonGroup.h"
#include "hwmonSensor.h"
#include "hwmonThreads.h"
#include "hwmon.h"

/**< constructor */
hwmonHostClass::hwmonHostClass()
{
    for ( int i = 0 ; i < MAX_HOSTS ; i++ )
        host_ptrs[i] = static_cast<struct hwmon_host *>(NULL) ;

    memory_allocs = 0 ;
    memory_used = 0 ;
    hwmon_head = NULL ;
    hwmon_tail = NULL ;
    hosts = 0 ;
    host_deleted = false ;
    config_reload = false ;

    return ;
}

hwmonHostClass::~hwmonHostClass() { return ; } /**< destructor  */

static std::string addStages_str        [HWMON_ADD__STAGES         +1] ;

void hwmon_stages_init ( void )
{
    addStages_str        [HWMON_ADD__START  ] = "Add-Start" ;
    addStages_str        [HWMON_ADD__STATES ] = "Add-States" ;
    addStages_str        [HWMON_ADD__WAIT   ] = "Add-Wait" ;
    addStages_str        [HWMON_ADD__DONE   ] = "Add-Done" ;
}

/** Host add handler Stage Change member function */
int hwmonHostClass::addStageChange ( struct hwmonHostClass::hwmon_host * ptr,
                                     hwmon_addStages_enum newStage )
{
    if ((      newStage < HWMON_ADD__STAGES ) &&
        ( ptr->addStage < HWMON_ADD__STAGES ))
    {
        clog ("%s %s -> %s (%d->%d)\n",
               &ptr->hostname[0],
               addStages_str[ptr->addStage].c_str(),
               addStages_str[newStage].c_str(),
               ptr->addStage, newStage);

        ptr->addStage = newStage ;

        return (PASS);
    }
    else
    {
        slog ("%s Invalid Stage (now:%d new:%d)\n",
                  ptr->hostname.c_str(),
                  ptr->addStage,
                  newStage );

        ptr->addStage = HWMON_ADD__DONE ;

        return (FAIL);
    }
}

/* Initialize bmc data for ipmi mode monitoring */
void hwmonHostClass::ipmi_bmc_data_init ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    host_ptr->ping_info.timer_handler = &hwmonTimer_handler ;
    host_ptr->accessible = false;
    host_ptr->degraded   = false ;

    hwmon_del_groups ( host_ptr );
    hwmon_del_sensors ( host_ptr );

    /* force the add handler to run */
    host_ptr->addStage   = HWMON_ADD__START;

    host_ptr->sensor_query_count = 0 ;
}

/*
 * Allocate new host and tack it on the end of the host_list
 */
struct hwmonHostClass::hwmon_host* hwmonHostClass::addHost( string hostname )
{
    /* verify host is not already provisioned */
    struct hwmon_host * ptr = hwmonHostClass::getHost ( hostname );
    if ( ptr )
    {
        if ( hwmonHostClass::remHost ( hostname ) )
        {
            /* Should never get here but if we do then */
            /* something is seriously wrong */
            elog ("Error: Unable to remove host during reprovision\n");
            return static_cast<struct hwmon_host *>(NULL);
        }
    }

    /* allocate memory for new host */
    ptr = hwmonHostClass::newHost ();
    if( ptr == NULL )
    {
        elog ( "Error: Failed to allocate memory for new host\n" );
		return static_cast<struct hwmon_host *>(NULL);
    }

    /* Init the new host */
    ptr->hostname = hostname ;
    ptr->host_delete = false ;
    ptr->poweron     = false ;
    ptr->retries     = 0     ;
    ptr->delStage = HWMON_DEL__START ;

    ptr->ping_info.timer_handler = &hwmonTimer_handler ;
    mtcTimer_init ( ptr->hostTimer,          ptr->hostname, "host timer" );
    mtcTimer_init ( ptr->addTimer,           ptr->hostname, "add timer"  );
    mtcTimer_init ( ptr->secretTimer,        ptr->hostname, "secret timer" );
    mtcTimer_init ( ptr->relearnTimer,       ptr->hostname, "relearn timer" );

    mtcTimer_init ( ptr->ping_info.timer,    ptr->hostname, "ping monitor timer" );
    mtcTimer_init ( ptr->monitor_ctrl.timer, ptr->hostname, "sensor monitor timer") ;

    ptr->groups           = 0 ;
    ptr->sensors          = 0 ;
    ptr->samples          = 0 ;

    /* http event pre-init
     * PATCHBACK - consider patchback to REL3 and earlier */
    ptr->event.base = NULL ;
    ptr->event.conn = NULL ;
    ptr->event.req = NULL ;
    ptr->event.buf = NULL ;

    ptr->secretEvent.base= NULL ;
    ptr->secretEvent.conn= NULL ;
    ptr->secretEvent.req = NULL ;
    ptr->secretEvent.buf = NULL ;

    /* If the host list is empty add it to the head */
    if( hwmon_head == NULL )
    {
        hwmon_head = ptr ;
        hwmon_tail = ptr ;
        ptr->prev = NULL ;
        ptr->next = NULL ;
    }
    else
    {
        /* link the new_host to the tail of the host_list
         * then mark the next field as the end of the host_list
         * adjust tail to point to the last host
         */
        hwmon_tail->next = ptr  ;
        ptr->prev  = hwmon_tail ;
        ptr->next  = NULL ;
        hwmon_tail = ptr ;
    }

    /* Default to not monitoring */
    ptr->monitor        = false ;
    ptr->bm_provisioned = false ;
    ptr->alarmed        = false ;
    ptr->alarmed_config = false ;
    ptr->degraded       = false ;

    hosts++ ;
    dlog2 ("Added hwmonHostClass host instance %d\n", hosts);
    return ptr ;
}

void hwmonHostClass::free_host_timers ( struct hwmon_host * ptr )
{
    mtcTimer_fini ( ptr->hostTimer );
    mtcTimer_fini ( ptr->addTimer );
    mtcTimer_fini ( ptr->secretTimer );
    mtcTimer_fini ( ptr->relearnTimer );
    mtcTimer_fini ( ptr->ping_info.timer );

    mtcTimer_fini ( ptr->monitor_ctrl.timer );
    mtcTimer_fini ( ptr->bmc_thread_ctrl.timer );
}

/* Remove a hist from the linked list of hosts - may require splice action */
int hwmonHostClass::remHost( string hostname )
{
    if ( hostname.c_str() == NULL )
        return -ENODEV ;

    if ( hwmon_head == NULL )
        return -ENXIO ;

    struct hwmon_host * ptr = hwmonHostClass::getHost ( hostname );

    if ( ptr == NULL )
        return -EFAULT ;

    free_host_timers ( ptr );

    /* If the host is the head host */
    if ( ptr == hwmon_head )
    {
        /* only one host in the list case */
        if ( hwmon_head == hwmon_tail )
        {
            dlog2 ("Single Host -> Head Case\n");
            hwmon_head = NULL ;
            hwmon_tail = NULL ;
        }
        else
        {
            dlog2 ("Multiple Hosts -> Head Case\n");
            hwmon_head = hwmon_head->next ;
            hwmon_head->prev = NULL ;
        }
    }
    /* if not head but tail then there must be more than one
     * host in the list so go ahead and chop the tail.
     */
    else if ( ptr == hwmon_tail )
    {
        dlog2 ("Multiple Host -> Tail Case\n");
        hwmon_tail = hwmon_tail->prev ;
        hwmon_tail->next = NULL ;
    }
    else
    {
        dlog2 ("Multiple Host -> Full Splice Out\n");
        ptr->prev->next = ptr->next ;
        ptr->next->prev = ptr->prev ;
    }

    hwmonHostClass::delHost ( ptr );
    hosts-- ;
    return (PASS) ;
}


struct hwmonHostClass::hwmon_host* hwmonHostClass::getHost ( string hostname )
{
   /* check for empty list condition */
   if ( hwmon_head == NULL )
      return NULL ;

   for ( struct hwmon_host * ptr = hwmon_head ;  ; ptr = ptr->next )
   {
       if ( !hostname.compare ( ptr->hostname ))
       {
           // dlog2 ("Fetched hwmonHostClass host instance %s\n", ptr->hostname.c_str());
           return ptr ;
       }

       if (( ptr->next == NULL ) || ( ptr == hwmon_tail ))
           break ;
    }
    return static_cast<struct hwmon_host *>(NULL);
}

/*
 * Allocates memory for a new host and stores its the address in host_ptrs
 *
 * @param void
 * @return pointer to the newly allocted host memory
 */
struct hwmonHostClass::hwmon_host * hwmonHostClass::newHost ( void )
{
   struct hwmonHostClass::hwmon_host * temp_host_ptr = NULL ;

   if ( memory_allocs == 0 )
   {
       memset ( host_ptrs, 0 , sizeof(struct hwmon_host *)*MAX_HOSTS);
   }

   // find an empty spot
   for ( int i = 0 ; i < MAX_HOSTS ; i++ )
   {
      if ( host_ptrs[i] == NULL )
      {
          host_ptrs[i] = temp_host_ptr = new hwmon_host ;
          memory_allocs++ ;
          memory_used += sizeof (struct hwmonHostClass::hwmon_host);

          return temp_host_ptr ;
      }
   }
   elog ( "Failed to save new host pointer address\n" );
   return temp_host_ptr ;
}

void hwmonHostClass::degrade_state_audit ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    bool found         ;
    string sensorname  ;
    int         s      ;

    /* manage degrade state */
    for ( s = 0 , sensorname.clear() , found = false ; s < host_ptr->sensors ; s++ )
    {
        if ( host_ptr->sensor[s].degraded == true )
        {
            sensorname = host_ptr->sensor[s].sensorname ;

            /* do some auto correction of degrade */
            if ( is_alarmed ( &host_ptr->sensor[s] ) == false )
            {
                slog ("%s %s is degraded but not alarmed ; correcting by removing degrade\n",
                          host_ptr->hostname.c_str(),
                          host_ptr->sensor[s].sensorname.c_str());
                host_ptr->sensor[s].degraded = false ;
            }
            else
            {
                found = true ;
                break ;
            }
        }
    }

    if ( found == true )
    {
        hwmon_send_event ( host_ptr->hostname, MTC_DEGRADE_RAISE , sensorname.data() );
        wlog_throttled (host_ptr->degrade_audit_log_throttle, 20, "%s degraded ... due to '%s' sensor\n", host_ptr->hostname.c_str(), sensorname.c_str());
    }
    else if ( host_ptr->degraded == true )
    {
        hwmon_send_event ( host_ptr->hostname, MTC_DEGRADE_RAISE , sensorname.data());
        wlog_throttled (host_ptr->degrade_audit_log_throttle, 20, "%s degraded ... due to 'hwmon' config error\n", host_ptr->hostname.c_str());
    }
    else
    {
        dlog ("%s available\n", host_ptr->hostname.c_str());
        hwmon_send_event ( host_ptr->hostname, MTC_DEGRADE_CLEAR, "sensors" );
        host_ptr->degrade_audit_log_throttle = 0 ;
    }

#ifdef WANT_FIT_TESTING

    if (daemon_want_fit(FIT_CODE__HWMON__CORRUPT_TOKEN))
    {
        tokenUtil_fail_token ();
        if ( host_ptr->event.active == false )
        {
            hwmonHttp_load_sensors ( host_ptr->hostname, host_ptr->event );
        }
        else
        {
            slog ("%s FIT skipping hwmonHttp_load_sensors failure trigger due to in-progress event\n",
                      host_ptr->hostname.c_str());
            daemon_hits_fit (1);
        }
    }

    if ( host_ptr->bm_provisioned == true )
    {
        /* FIT Support for creating orphan sensor or group alarm */
        if ( daemon_want_fit ( FIT_CODE__HWMON__CREATE_ORPHAN_GROUP_ALARM, host_ptr->hostname ))
        {
            string orphan = "orphan_group_" + itos((rand()%1000)) ;
            hwmonAlarm_major ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, orphan, REASON_DEGRADED );
        }
        if ( daemon_want_fit ( FIT_CODE__HWMON__CREATE_ORPHAN_SENSOR_ALARM, host_ptr->hostname ))
        {
            string orphan = "orphan_sensor_" + itos((rand()%1000)) ;
            hwmonAlarm_major ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, orphan, REASON_DEGRADED );
        }

        /* FIT Support for forcing raise or clear of any Group or Sensor Alarm in FM */
        /* FIT Support for forcing state or status of any Group or Sensor Alarm in the database */
        for ( int g = 0 ; g < host_ptr->groups ; g++ )
        {
            string sev ;
            if ( daemon_want_fit ( FIT_CODE__HWMON__RAISE_GROUP_ALARM, host_ptr->hostname, host_ptr->group[g].group_name, sev ))
            {
                hwmon_alarm_util ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, FM_ALARM_STATE_SET, alarmUtil_getSev_enum(sev), host_ptr->group[g].group_name, REASON_DEGRADED );
                break ;
            }
            if ( daemon_want_fit ( FIT_CODE__HWMON__CLEAR_GROUP_ALARM, host_ptr->hostname, host_ptr->group[g].group_name ))
            {
                hwmon_alarm_util ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, FM_ALARM_STATE_CLEAR, FM_ALARM_SEVERITY_CLEAR, host_ptr->group[g].group_name, REASON_OK );
                break ;
            }
            if ( daemon_want_fit ( FIT_CODE__HWMON__SET_DB_GROUP_STATE, host_ptr->hostname, host_ptr->group[g].group_name, sev ))
            {
                hwmonHttp_mod_group ( host_ptr->hostname, host_ptr->event , host_ptr->group[g].group_uuid, "state", sev );
                break ;
            }
            if ( daemon_want_fit ( FIT_CODE__HWMON__SET_DB_GROUP_STATUS, host_ptr->hostname, host_ptr->group[g].group_name, sev ))
            {
                hwmonHttp_mod_group ( host_ptr->hostname, host_ptr->event , host_ptr->group[g].group_uuid, "status", sev );
                break ;
            }
        }

        for ( int s = 0 ; s < host_ptr->sensors ; s++ )
        {
            string sev ;
            if ( daemon_want_fit ( FIT_CODE__HWMON__RAISE_SENSOR_ALARM, host_ptr->hostname, host_ptr->sensor[s].sensorname, sev ))
            {
                hwmon_alarm_util ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, FM_ALARM_STATE_SET, alarmUtil_getSev_enum(sev), host_ptr->sensor[s].sensorname, REASON_DEGRADED );
                break ;
            }
            if ( daemon_want_fit ( FIT_CODE__HWMON__CLEAR_SENSOR_ALARM, host_ptr->hostname, host_ptr->sensor[s].sensorname ))
            {
                hwmon_alarm_util ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, FM_ALARM_STATE_CLEAR, FM_ALARM_SEVERITY_CLEAR, host_ptr->sensor[s].sensorname, REASON_OK );
                break ;
            }
            if ( daemon_want_fit ( FIT_CODE__HWMON__SET_DB_SENSOR_STATE, host_ptr->hostname, host_ptr->sensor[s].sensorname, sev ))
            {
                hwmonHttp_mod_sensor ( host_ptr->hostname, host_ptr->event , host_ptr->sensor[s].uuid, "state", sev );
                break ;
            }
            if ( daemon_want_fit ( FIT_CODE__HWMON__SET_DB_SENSOR_STATUS, host_ptr->hostname, host_ptr->sensor[s].sensorname, sev ))
            {
                hwmonHttp_mod_sensor ( host_ptr->hostname, host_ptr->event , host_ptr->sensor[s].uuid, "status", sev );
                break ;
            }
        }
    }

#endif

}

/* Frees the memory of a pre-allocated host and removes
 * it from the host_ptrs list
 * @param host * pointer to the host memory address to be freed
 * @return int return code { PASS or -EINVAL }
 */
int hwmonHostClass::delHost ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    if ( hwmonHostClass::memory_allocs > 0 )
    {
        for ( int i = 0 ; i < MAX_NODES ; i++ )
        {
            if ( hwmonHostClass::host_ptrs[i] == host_ptr )
            {
                delete host_ptr ;
                hwmonHostClass::host_ptrs[i] = NULL ;
                hwmonHostClass::memory_allocs-- ;
                hwmonHostClass::memory_used -= sizeof (struct hwmonHostClass::hwmon_host);
                return PASS ;
            }
        }
        elog ( "Error: Unable to validate memory address being freed\n" );
    }
    else
       elog ( "Error: Free memory called when there is no memory to free\n" );

    return -EINVAL ;
}

void hwmonHostClass::clear_bm_assertions ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    /* Loop over all sensors and groups
     *  - clear any outstanding alarms
     *  - clear degrade of host
     *  ... while we deprovision the BMC */
    for ( int i = 0 ; i < host_ptr->sensors ; i++ )
    {
        if ( host_ptr->sensor[i].alarmed == true )
        {
            hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSOR, host_ptr->sensor[i].sensorname, REASON_DEPROVISIONED );
            host_ptr->sensor[i].alarmed  = false ;
            host_ptr->sensor[i].degraded = false ;
        }
    }

    for ( int g = 0 ; g < host_ptr->groups ; ++g )
    {
        hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORGROUP, host_ptr->group[g].group_name, REASON_DEPROVISIONED );
    }

    /* send the degrade anyway , just to be safe */
    hwmon_send_event ( host_ptr->hostname, MTC_DEGRADE_CLEAR , "sensors" );

    /* Bug Fix: This was outside the if bm_provisioned clause causing it
     *          to be called even if the bmc was not already provisioned
     */
    hwmonAlarm_clear ( host_ptr->hostname, HWMON_ALARM_ID__SENSORCFG, "sensors", REASON_DEPROVISIONED );
}

int hwmonHostClass::set_bm_prov ( struct hwmonHostClass::hwmon_host * host_ptr, bool state )
{
    int rc = FAIL_HOSTNAME_LOOKUP ;
    if ( host_ptr )
    {
        rc = PASS ;

        /* Clear the alarm if we are starting fresh from an unprovisioned state */
        if (( host_ptr->bm_provisioned == false ) && ( state == true ))
        {
            ilog ("%s board management controller is being provisioned\n", host_ptr->hostname.c_str());
            ilog ("%s setting up ping socket\n", host_ptr->hostname.c_str() );

            /* ---------------------------------------
             * Init bmc data based on monitoring mode
             * ---------------------------------------*/

            mtcTimer_reset ( host_ptr->ping_info.timer ) ;
            host_ptr->ping_info.stage    = PINGUTIL_MONITOR_STAGE__OPEN ;
            host_ptr->ping_info.ip       = host_ptr->bm_ip ;
            host_ptr->ping_info.hostname = host_ptr->hostname ;
            ipmi_bmc_data_init ( host_ptr );

            string host_uuid = hostBase.get_uuid( host_ptr->hostname );
            barbicanSecret_type * secret = secretUtil_find_secret( host_uuid );
            if ( secret )
            {
                secret->reference.clear() ;
                secret->payload.clear() ;
                secret->stage = MTC_SECRET__START ;
            }
            mtcTimer_start( host_ptr->secretTimer, hwmonTimer_handler, SECRET_START_DELAY );

            host_ptr->thread_extra_info.bm_pw.clear() ;
            host_ptr->thread_extra_info.bm_ip = host_ptr->bm_ip ;
            host_ptr->thread_extra_info.bm_un = host_ptr->bm_un ;
        }
        /* handle the case going from provisioned to not provisioned */
        if (( host_ptr->bm_provisioned == true ) && ( state == false ))
        {
            ilog ("%s board management controller is being deprovisioned\n", host_ptr->hostname.c_str());
            clear_bm_assertions ( host_ptr );
            pingUtil_fini  ( host_ptr->ping_info );
            ipmi_bmc_data_init ( host_ptr );
        }
        host_ptr->bm_provisioned = state ;
    }
    return (rc);
}

int hwmonHostClass::mod_host ( node_inv_type & inv )
{
    int rc = FAIL ;
    struct hwmonHostClass::hwmon_host * host_ptr = static_cast<struct hwmon_host *>(NULL);

    if  (( inv.name.empty())           ||
         ( !inv.name.compare (NONE)) ||
         ( !inv.name.compare ("None")))
    {
        wlog ("Refusing to add host with 'null' or 'invalid' hostname (%s)\n",
               inv.uuid.c_str());
        return (FAIL_INVALID_HOSTNAME) ;
    }

    host_ptr = hwmonHostClass::getHost(inv.name);
    if ( host_ptr )
    {
        rc = PASS ;
        bool modify_bm = false ;

        if ( host_ptr->bm_ip.compare( inv.bm_ip ) )
        {
            ilog ("%s modify board management 'ip' from '%s' to '%s'\n",
                      inv.name.c_str(),
                      host_ptr->bm_ip.c_str(),
                      inv.bm_ip.c_str());

            host_ptr->bm_ip = inv.bm_ip   ;

            modify_bm = true ;
        }

        if ( host_ptr->bm_un.compare( inv.bm_un ) )
        {
            ilog ("%s modify board management 'username' from '%s' to '%s'\n",
                      inv.name.c_str(),
                      host_ptr->bm_un.c_str(),
                      inv.bm_un.c_str());

            host_ptr->bm_un = inv.bm_un   ;

            modify_bm = true ;
        }

        if ( host_ptr->bm_type.compare( inv.bm_type ) )
        {
            ilog ("%s modify board management 'type' from '%s' to '%s'\n",
                      inv.name.c_str(),
                      host_ptr->bm_type.c_str(),
                      inv.bm_type.c_str());

            host_ptr->bm_type = inv.bm_type ;

            modify_bm = true ;
        }

        if ( modify_bm == true )
        {
            ilog ("%s modify summary %s %s@%s ... provisioned = %s\n",
                  inv.name.c_str(),
                  host_ptr->bm_type.c_str(),
                  host_ptr->bm_un.c_str(),
                  host_ptr->bm_ip.c_str(),
                  host_ptr->bm_provisioned ? "Yes" : "No" );
            if ( host_ptr->bm_provisioned == true )
            {
                /* if we have a credentials only change then disable the sensor
                 * model only to get re-enabled if sensor monitoring is
                 * successful with the new credentils */
                if (( hostUtil_is_valid_bm_type (host_ptr->bm_type) == true ) &&
                    ( host_ptr->bm_un.compare(NONE)))
                {
                    ipmi_set_group_state ( host_ptr, "disabled" );
                    ipmi_disable_sensors ( host_ptr );
                }
                rc = set_bm_prov ( host_ptr, false );
            }

            if (( hostUtil_is_valid_bm_type (host_ptr->bm_type) == true ) &&
                ( hostUtil_is_valid_ip_addr (host_ptr->bm_ip) == true ) &&
                  !host_ptr->bm_un.empty())
            {
                rc = set_bm_prov ( host_ptr, true );
            }
        }
        else
        {
            /* Only reprovision if the provisioning data has changed */
            dlog ("%s bmc provisioning unchanged\n", host_ptr->hostname.c_str());
        }
    }
    else
    {
        elog ("%s board management info modify failed\n", inv.name.c_str());
        rc = FAIL_NULL_POINTER ;
    }
    return (rc);
}

void hwmonHostClass::set_degrade_audit ( void )
{
    struct hwmon_host * ptr = hwmon_head ;
    for ( int i = 0 ; i < hosts ; i++ )
    {
        ptr->want_degrade_audit = true ;
        ptr = ptr->next ;
        if ( ptr == NULL )
            break ;
    }
}


int hwmonHostClass::add_host ( node_inv_type & inv )
{
    int rc = FAIL ;
    struct hwmonHostClass::hwmon_host * host_ptr = static_cast<struct hwmon_host *>(NULL);

    if  (( inv.name.empty())         ||
         ( !inv.name.compare (NONE)) ||
         ( !inv.name.compare ("None")))
    {
        wlog ("Refusing to add host with 'null' or 'invalid' hostname (%s)\n",
               inv.uuid.c_str());
        return (FAIL_INVALID_HOSTNAME) ;
    }

    rc = hostBase.add_host ( inv );
    if ( rc > RETRY )
    {
        elog ("Error\n");
    }

    host_ptr = hwmonHostClass::getHost(inv.name);
    if ( host_ptr )
    {
        if ( host_ptr->host_delete == true )
        {
            ilog ("%s cannot be added while previous delete is still in progress\n", host_ptr->hostname.c_str());
            return (FAIL_OPERATION);
        }
        dlog ("%s already provisioned\n", host_ptr->hostname.c_str());

        /* Send back a retry in case the add needs to be converted to a modify */
        return (RETRY);
    }
    /* Otherwise add it as a new host */
    else
    {
        host_ptr = hwmonHostClass::addHost(inv.name);
        if ( host_ptr )
        {
            /* Add board management stuff */
            host_ptr->bm_ip       = inv.bm_ip   ;
            host_ptr->bm_un       = inv.bm_un   ;
            host_ptr->bm_type     = inv.bm_type ;

            /* default the socket number to closed */
            host_ptr->ping_info.sock = 0 ;

            host_ptr->quanta_server= false ;

            ipmi_bmc_data_init ( host_ptr );

            /* Default audit interval to zero - disable sensor monitoring by default */
            host_ptr->interval = 0 ;
            host_ptr->interval_old = 0 ;
            host_ptr->interval_changed = false ;
            host_ptr->accounting_ok = false ;
            host_ptr->accounting_bad_count = 0 ;

            /* Additions for sensor monitoring using IPMI protocol */
            host_ptr->want_degrade_audit = false ;
            host_ptr->degrade_audit_log_throttle = 0 ;
            host_ptr->json_ipmi_sensors.clear();

            /* Sensor Monitoring Control Structure */
            host_ptr->monitor_ctrl.stage            = HWMON_SENSOR_MONITOR__START  ;
            host_ptr->monitor_ctrl.last_sample_time = 0 ;
            host_ptr->monitor_ctrl.this_sample_time = 0 ;
            host_ptr->sensor_query_count            = 0 ;

            /* Sensor Monitoring Thread 'Extra Request Information' */
            host_ptr->empty_secret_log_throttle = 0 ;
            host_ptr->thread_extra_info.bm_ip = host_ptr->bm_ip ;
            host_ptr->thread_extra_info.bm_un = host_ptr->bm_un ;
            host_ptr->thread_extra_info.bm_pw.clear() ;
            host_ptr->thread_extra_info.sensor_query_request = IPMITOOL_PATH_AND_FILENAME ;

            /* Sensor Monitoring Thread Initialization */
            thread_init ( host_ptr->bmc_thread_ctrl,
                          host_ptr->bmc_thread_info,
                         &host_ptr->thread_extra_info,
                          hwmonThread_ipmitool,
                          DEFAULT_THREAD_TIMEOUT_SECS,
                          host_ptr->hostname,
                          THREAD_NAME__BMC);

            /* TODO: create a is_bm_info_valid */
            if ( ( hostUtil_is_valid_ip_addr (host_ptr->bm_ip) == true ) &&
                 ( hostUtil_is_valid_bm_type (host_ptr->bm_type) == true ) &&
                 ( !host_ptr->bm_un.empty() ) &&
                 (  host_ptr->bm_un.compare(NONE)) )
            {
                set_bm_prov ( host_ptr, true );
            }
            else
            {
                set_bm_prov ( host_ptr, false );
            }
            ilog ("%s BMC is %sprovisioned\n", host_ptr->hostname.c_str(), host_ptr->bm_provisioned ? "" : "not " );

            host_ptr->bmc_fw_version.clear();

            host_ptr->group_index  = 0 ;

            /* Init sensor model relearn controls, state and status */
            host_ptr->relearn = false ;
            host_ptr->relearn_request = false ;
            host_ptr->relearn_retry_counter = 0 ;
            host_ptr->relearn_done_date.clear();
            init_model_attributes ( host_ptr->model_attributes_preserved );

            /* Add to the end of inventory */
            hostlist.push_back ( host_ptr->hostname );

            rc = PASS ;
            dlog ("%s running add FSM\n", inv.name.c_str());
        }
        else
        {
            elog ("%s host service add failed\n", inv.name.c_str());
            rc = FAIL_NULL_POINTER ;
        }
    }
    return (rc);
}

int hwmonHostClass::rem_host ( string hostname )
{
    int rc = FAIL ;
    if ( ! hostname.empty() )
    {
        /* Remove the hostBase */
        rc = hostBase.rem_host ( hostname );
        if ( rc == PASS )
        {
            rc = hwmonHostClass::remHost ( hostname );
        }
        else
        {
            hwmonHostClass::remHost ( hostname );
            slog ("potential memory leak !\n");
        }

        /* Now remove the service specific component */
        hostlist.remove ( hostname );
    }
    return ( rc );
}

int hwmonHostClass::request_del_host ( string hostname )
{
    int rc = FAIL_DEL_UNKNOWN ;
    hwmonHostClass::hwmon_host * host_ptr = hwmonHostClass::getHost( hostname );
    if ( host_ptr )
    {
        if ( host_ptr->host_delete == true )
        {
            ilog ("%s delete already in progress\n", hostname.c_str());
        }
        else
        {
            host_ptr->delStage = HWMON_DEL__START ;
            host_ptr->host_delete = true ;
        }
        rc = PASS ;
    }
    else
    {
        wlog ("Unknown hostname: %s\n", hostname.c_str());
    }
    return (rc);
}

int hwmonHostClass::del_host ( string hostname )
{
    int rc = FAIL_DEL_UNKNOWN ;
    hwmonHostClass::hwmon_host * hwmon_host_ptr = hwmonHostClass::getHost( hostname );
    if ( hwmon_host_ptr )
    {
        rc = rem_host ( hostname );
        if ( rc == PASS )
        {
            ilog ("%s deleted\n", hostname.c_str());
            print_node_info();
        }
        else
        {
            elog ("%s delete host failed (rc:%d)\n", hostname.c_str(), rc );
        }
    }
    else
    {
        wlog ("Unknown hostname: %s\n", hostname.c_str());
    }
    return (rc);
}

int hwmonHostClass::mon_host ( string hostname, bool monitor )
{
    int rc = FAIL_UNKNOWN_HOSTNAME ;
    hwmonHostClass::hwmon_host * hwmon_host_ptr = hwmonHostClass::getHost( hostname );
    if ( hwmon_host_ptr )
    {
        bool change = false ;
        string want_state = "" ;

        if ( monitor == true )
            want_state = "enabled" ;
        else
            want_state = "disabled" ;

        /* if not provisioned then just return */
        if ( hwmon_host_ptr->bm_provisioned == false )
        {
            dlog ("%s ignoring monitor '%s' request for unprovisioned bmc\n",
                      hostname.c_str(), want_state.c_str());
            return (PASS);
        }

        else if ( hwmon_host_ptr->host_delete == true )
        {
            dlog ("%s ignoring monitor '%s' request while delete is pending\n",
                      hostname.c_str(), want_state.c_str() );
            return (PASS);
        }

        if (( monitor == false ) &&
            ( hwmon_host_ptr->monitor != monitor  ) &&
            ( hwmon_host_ptr->bm_provisioned == true ))
        {
            clear_bm_assertions ( hwmon_host_ptr );
        }

        if ( hwmon_host_ptr->monitor == monitor )
        {
            dlog ("%s sensor monitoring already %s\n", hwmon_host_ptr->hostname.c_str(), monitor ? "enabled" : "disabled" );

            /* if any group is not in the correct enabled state then set change bool */
            for ( int g = 0 ; g < hwmon_host_ptr->groups ; ++g )
            {
                if ( hwmon_host_ptr->group[g].group_state.compare(want_state) )
                {
                    change = true ;
                }
            }
        }
        else
        {
            ilog ("%s sensor monitoring set to %s\n", hwmon_host_ptr->hostname.c_str(), monitor ? "enabled" : "disabled" );
            change = true ;
            hwmon_host_ptr->monitor = monitor ;
        }

        if ( change == true )
        {
            if ( monitor == false )
            {
                /* sets all groups state to disable if monitor is false ; handle state change failure alarming internally */
                rc = ipmi_set_group_state ( hwmon_host_ptr, "disabled" );
            }
            else if ( hwmon_host_ptr->group[0].group_state.compare("disabled") == 0 )
            {
                 /* or to enabled if presently disabled - don't change from failed to enabled over a monitor start */
                 rc = ipmi_set_group_state ( hwmon_host_ptr, "enabled" );
            }
        }
    }
    else
    {
        dlog ("Unknown hostname: %s\n", hostname.c_str());
    }
    return (rc);
}

/****************************************************************************/
/** Host Class Setter / Getters */
/****************************************************************************/

bool hwmonHostClass::is_bm_provisioned ( string hostname )
{
    hwmonHostClass::hwmon_host * hwmon_host_ptr ;
    hwmon_host_ptr = hwmonHostClass::getHost ( hostname );
    if ( hwmon_host_ptr != NULL )
    {
        return (hwmon_host_ptr->bm_provisioned);
    }
    elog ("%s lookup failed\n", hostname.c_str() );
    return (false);
}

/** Get this hosts board management IP address */
string hwmonHostClass::get_bm_ip ( string hostname )
{
    hwmonHostClass::hwmon_host * hwmon_host_ptr ;
    hwmon_host_ptr = hwmonHostClass::getHost ( hostname );
    if ( hwmon_host_ptr != NULL )
    {
        if ( hostUtil_is_valid_ip_addr (hwmon_host_ptr->bm_ip) == false )
        {
            return (NONE);
        }
        else
        {
            return (hwmon_host_ptr->bm_ip);
        }
    }
    elog ("%s bm ip lookup failed\n", hostname.c_str() );
    return ("");
}

/** Get this hosts board management TYPE ilo3/ilo4/quanta/etc */
string hwmonHostClass::get_bm_type ( string hostname )
{
    hwmonHostClass::hwmon_host * hwmon_host_ptr ;
    hwmon_host_ptr = hwmonHostClass::getHost ( hostname );
    if ( hwmon_host_ptr != NULL )
    {
         return (hwmon_host_ptr->bm_type);
    }
    elog ("%s bm type lookup failed\n", hostname.c_str() );
    return ("");
}

/** Get this hosts board management user name */
string hwmonHostClass::get_bm_un ( string hostname )
{
    hwmonHostClass::hwmon_host * hwmon_host_ptr ;
    hwmon_host_ptr = hwmonHostClass::getHost ( hostname );
    if ( hwmon_host_ptr != NULL )
    {
        if ( hwmon_host_ptr->bm_un.empty() )
        {
            return (NONE);
        }
        else
        {
            return (hwmon_host_ptr->bm_un);
        }
    }
    elog ("%s bm username lookup failed\n", hostname.c_str() );
    return ("");
}



string hwmonHostClass::get_relearn_done_date ( string hostname )
{
    hwmonHostClass::hwmon_host * hwmon_host_ptr ;
    hwmon_host_ptr = hwmonHostClass::getHost ( hostname );
    if ( hwmon_host_ptr != NULL )
    {
        if ( !hwmon_host_ptr->relearn_done_date.empty())
        {
            return (hwmon_host_ptr->relearn_done_date);
        }
    }
    elog ("%s relearn done date empty or hostname lookup failed\n", hostname.c_str());
    return (pt());
}


struct hwmonHostClass::hwmon_host * hwmonHostClass::getHost_timer ( timer_t tid )
{
   /* check for empty list condition */
   if (( hwmon_head ) && ( tid ))
   {
       for ( struct hwmon_host * host_ptr = hwmon_head ;  ; host_ptr = host_ptr->next )
       {
           if ( host_ptr->bmc_thread_ctrl.timer.tid == tid )
           {
               return host_ptr ;
           }
           if ( host_ptr->hostTimer.tid == tid )
           {
               return host_ptr ;
           }
           if ( host_ptr->secretTimer.tid == tid )
           {
               return host_ptr ;
           }
           if ( host_ptr->ping_info.timer.tid == tid )
           {
               return host_ptr ;
           }
           if ( host_ptr->monitor_ctrl.timer.tid == tid )
           {
               return host_ptr ;
           }
           if ( host_ptr->addTimer.tid == tid )
           {
               return host_ptr ;
           }
           if ( host_ptr->relearnTimer.tid == tid )
           {
               return host_ptr ;
           }

           if (( host_ptr->next == NULL ) || ( host_ptr == hwmon_tail ))
               break ;
        }
    }
    return static_cast<struct hwmon_host *>(NULL);
}

/**********************************************************************************
 *
 * Name        : get_sensor
 *
 * Description : Update the supplied pointer with the host sensor
 *               that matches the supplied sensor name.
 *
 * Updates     : sensor_ptr is set if found, otherwise a NULL is returned
 *
 **********************************************************************************/
sensor_type * hwmonHostClass::get_sensor ( string hostname, string entity_path )
{
    int rc = FAIL_NOT_FOUND ;

    if ( entity_path.empty() )
        rc = FAIL_STRING_EMPTY ;
    else
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            for ( int i = 0 ; i < host_ptr->sensors ; i++ )
            {
                if ( !entity_path.compare(host_ptr->sensor[i].sensorname))
                {
                    blog ("%s '%s' sensor found\n",
                              hostname.c_str(),
                              host_ptr->sensor[i].sensorname.c_str());

                    return (&host_ptr->sensor[i]) ;
                }
            }
        }
    }
    if ( rc == FAIL_NOT_FOUND )
    {
        wlog ("%s '%s' entity path not found\n", hostname.c_str() , entity_path.c_str());
    }
    else if ( rc )
    {
        elog ("%s sensor entity path query failed\n", hostname.c_str() );
    }
    return (static_cast<sensor_type*>(NULL));
}

int hwmonHostClass::add_sensor ( string hostname, sensor_type & sensor )
{
    int rc = PASS ;

    if ( sensor.sensorname.empty() )
        return (FAIL_STRING_EMPTY);
    else
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            int i ;
            bool found = false ;
            for ( i = 0 ; i < host_ptr->sensors ; i++ )
            {
                if ( !sensor.entity_path.compare(host_ptr->sensor[i].sensorname))
                {
                    found = true ;
                    break ;
                }
            }
            if ( i >= MAX_HOST_SENSORS )
            {
                rc = FAIL ;
            }
            else
            {
                /* PATCHBACK - to REL3 and earlier
                 * This init should have been initialized here all along */
                hwmonSensor_init ( hostname, &host_ptr->sensor[i] );

                host_ptr->sensor[i].sensorname     = sensor.sensorname ; /* for fresh add case */
                host_ptr->sensor[i].sensortype     = sensor.sensortype ;
                host_ptr->sensor[i].script         = sensor.script ;
                host_ptr->sensor[i].uuid           = sensor.uuid ;
                host_ptr->sensor[i].datatype       = sensor.datatype ;
                host_ptr->sensor[i].group_uuid     = sensor.group_uuid;
                host_ptr->sensor[i].host_uuid      = sensor.host_uuid ;
                host_ptr->sensor[i].algorithm      = sensor.algorithm ;
                host_ptr->sensor[i].group_uuid     = sensor.group_uuid;
                host_ptr->sensor[i].status         = sensor.status    ;
                host_ptr->sensor[i].state          = sensor.state     ;
                host_ptr->sensor[i].prot           = sensor.prot ;
                host_ptr->sensor[i].kind           = sensor.kind ;
                host_ptr->sensor[i].unit           = sensor.unit ;
                host_ptr->sensor[i].suppress       = sensor.suppress ;
                host_ptr->sensor[i].path           = sensor.path ;

                if ( sensor.path.empty() )
                {
                    host_ptr->sensor[i].entity_path = sensor.sensorname ;
                }
                else
                {
                    host_ptr->sensor[i].entity_path = sensor.path ;
                    host_ptr->sensor[i].entity_path.append(ENTITY_DELIMITER);
                    host_ptr->sensor[i].entity_path.append(sensor.sensorname);
                }

                host_ptr->sensor[i].unit_base      = sensor.unit_base ;
                host_ptr->sensor[i].unit_rate      = sensor.unit_rate ;
                host_ptr->sensor[i].unit_modifier  = sensor.unit_modifier ;

                host_ptr->sensor[i].actions_minor  = sensor.actions_minor ;
                host_ptr->sensor[i].actions_major  = sensor.actions_major ;
                host_ptr->sensor[i].actions_critl  = sensor.actions_critl ;

                host_ptr->sensor[i].t_critical_lower = sensor.t_critical_lower ;
                host_ptr->sensor[i].t_major_lower    = sensor.t_major_lower ;
                host_ptr->sensor[i].t_minor_lower    = sensor.t_minor_lower ;
                host_ptr->sensor[i].t_minor_upper    = sensor.t_minor_upper  ;
                host_ptr->sensor[i].t_major_upper    = sensor.t_major_upper  ;
                host_ptr->sensor[i].t_critical_upper = sensor.t_critical_upper  ;

                if ( found == false )
                    host_ptr->sensors++ ;
            }
        }
    }

    if ( rc )
    {
        elog ("%s '%s' sensor add failed\n", hostname.c_str(),
                                             sensor.sensorname.c_str());
    }
    return (rc);
}

/****************************************************************************
 *
 * Name:        hwmon_get_sensorgroup
 *
 * Description: Returns a pointer to the sensor group that matches the supplied
 *              entity path.
 *
 ****************************************************************************/
struct sensor_group_type * hwmonHostClass::hwmon_get_sensorgroup ( string hostname, string entity_path )
{
    int rc = FAIL_NOT_FOUND ;

    if ( ( !entity_path.empty() ) && ( !hostname.empty()) )
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            for ( int g = 0 ; g < host_ptr->groups ; g++ )
            {
                /* look for the sensor in the group */
                for ( int s = 0 ; s < host_ptr->group[g].sensors ; s++ )
                {
                    if ( !host_ptr->group[g].sensor_ptr[s]->sensorname.compare(entity_path) )
                    {
                        blog ("%s '%s' sensor found in '%s' group\n",
                              hostname.c_str(),
                              host_ptr->group[g].sensor_ptr[s]->sensorname.c_str(),
                              host_ptr->group[g].group_name.c_str());

                        return (&host_ptr->group[g]);
                    }
                }
            }
        }
        else
        {
            rc = FAIL_HOSTNAME_LOOKUP ;
            elog ("%s hostname lookup failed\n", hostname.c_str() );
        }
    }
    else
    {
        rc = FAIL_STRING_EMPTY ;
        slog ("%s empty hostname or entity path '%s' string\n", hostname.c_str(), entity_path.c_str() );
    }
    if ( rc == FAIL_NOT_FOUND )
    {
        slog ("%s '%s' entity path not found in any group\n", hostname.c_str() , entity_path.c_str());
    }
    return (static_cast<struct sensor_group_type*>(NULL));
}

/**********************************************************************************
 *
 * Name        : hwmon_get_group
 *
 * Description : Returns a pointer to the sensor group that matches the supplied
 *               group name.
 *
 **********************************************************************************/
struct sensor_group_type * hwmonHostClass::hwmon_get_group ( string hostname, string group_name )
{
    int rc = FAIL_NOT_FOUND ;

    if ( ( !group_name.empty() ) && ( !hostname.empty()) )
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            for ( int i = 0 ; i < host_ptr->groups ; i++ )
            {
                if ( !group_name.compare(host_ptr->group[i].group_name))
                {
                    blog ("%s '%s' sensor group found\n",
                              hostname.c_str(),
                              host_ptr->group[i].group_name.c_str());

                    return (&host_ptr->group[i]) ;
                }
            }
        }
    }
    if ( rc == FAIL_NOT_FOUND )
    {
        wlog ("%s '%s' sensor group not found\n", hostname.c_str() , group_name.c_str());
    }
    else if ( rc )
    {
        elog ("%s sensor group query failed\n", hostname.c_str() );
    }
    return (static_cast<struct sensor_group_type*>(NULL));
}


/* Add a sensor group to a host */
int hwmonHostClass::hwmon_add_group ( string hostname, struct sensor_group_type & group )
{
    int rc = PASS ;

    if ( group.group_name.empty() )
        return (FAIL_STRING_EMPTY);
    else
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            int i ;
            bool found = false ;
            for ( i = 0 ; i < host_ptr->groups ; i++ )
            {
                if ( !group.group_name.compare(host_ptr->group[i].group_name))
                {
                    found = true ;
                    break ;
                }
            }
            if ( i >= MAX_HOST_GROUPS )
            {
                rc = FAIL ;
            }
            else
            {
                host_ptr->group[i].failed = false ;

                host_ptr->group[i].host_uuid      = group.host_uuid ;

                host_ptr->group[i].group_name     = group.group_name ; /* for fresh add case */
                host_ptr->group[i].group_uuid     = group.group_uuid ;

                host_ptr->group[i].hostname = hostname ;
                host_ptr->interval_changed = true ;

                host_ptr->group[i].group_interval = group.group_interval ;

                host_ptr->group[i].sensortype     = group.sensortype  ;
                host_ptr->group[i].datatype       = group.datatype    ;
                host_ptr->group[i].algorithm      = group.algorithm   ;
                host_ptr->group[i].group_state    = group.group_state ;
                host_ptr->group[i].suppress       = group.suppress    ;
                host_ptr->group[i].path           = group.path        ;

                host_ptr->group[i].unit_base_group      = group.unit_base_group ;
                host_ptr->group[i].unit_rate_group      = group.unit_rate_group ;
                host_ptr->group[i].unit_modifier_group  = group.unit_modifier_group ;

                host_ptr->group[i].actions_minor_choices = group.actions_minor_choices ;
                host_ptr->group[i].actions_major_choices = group.actions_major_choices ;
                host_ptr->group[i].actions_critical_choices = group.actions_critical_choices ;

                host_ptr->group[i].actions_minor_group  = group.actions_minor_group ;
                host_ptr->group[i].actions_major_group  = group.actions_major_group ;
                host_ptr->group[i].actions_critl_group  = group.actions_critl_group ;

                host_ptr->group[i].t_critical_lower_group = group.t_critical_lower_group ;
                host_ptr->group[i].t_critical_upper_group = group.t_critical_upper_group  ;
                host_ptr->group[i].t_major_lower_group    = group.t_major_lower_group ;
                host_ptr->group[i].t_major_upper_group    = group.t_major_upper_group  ;
                host_ptr->group[i].t_minor_lower_group    = group.t_minor_lower_group ;
                host_ptr->group[i].t_minor_upper_group    = group.t_minor_upper_group  ;

                /* Default the read index to the first sensor in this group.
                 * This member is only used when we are reading group sensors individually */
                host_ptr->group[i].sensor_read_index = 0 ;

                blog ("%s '%s' sensor group added\n", host_ptr->hostname.c_str(), host_ptr->group[i].group_name.c_str() );

                if ( found == false )
                    host_ptr->groups++ ;
            }
        }
    }

    if ( rc )
    {
        elog ("%s '%s' sensor group add failed\n", hostname.c_str(),
                                                   group.group_name.c_str());
    }
    return (rc);
}

/****************************************************************************
 *
 * Name:        add_group_uuid
 *
 * Description: Adds the sysinv supplied group uuid to hwmon for
 *              the specified group/host.
 *
 ****************************************************************************/
int  hwmonHostClass::add_group_uuid ( string & hostname, string & group_name, string & uuid )
{
    int rc = FAIL_NOT_FOUND ;

    if ( ( !group_name.empty() ) && ( !hostname.empty()) )
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            for ( int i = 0 ; i < host_ptr->groups ; i++ )
            {
                if ( !group_name.compare(host_ptr->group[i].group_name))
                {
                    blog1 ("%s '%s' sensor group found\n",
                              hostname.c_str(),
                              host_ptr->group[i].group_name.c_str());

                    host_ptr->group[i].group_uuid = uuid ;
                    rc = PASS ;
                    break ;
                }
            }
        }
    }
    if ( rc == FAIL_NOT_FOUND )
    {
        wlog ("%s '%s' sensor group not found\n", hostname.c_str() , group_name.c_str());
    }
    return (rc);
}


/****************************************************************************
 *
 * Name:        add_sensor_uuid
 *
 * Description: Adds the sysinv supplied sensor uuid to hwmon for
 *              the specified sensor/host.
 *
 ****************************************************************************/
int  hwmonHostClass::add_sensor_uuid ( string & hostname, string & sensorname, string & uuid )
{
    int rc = FAIL_NOT_FOUND ;

    if ( ( !sensorname.empty() ) && ( !hostname.empty()) )
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            for ( int i = 0 ; i < host_ptr->sensors ; i++ )
            {
                if ( !sensorname.compare(host_ptr->sensor[i].sensorname))
                {
                    blog1 ("%s '%s' sensor found\n",
                              hostname.c_str(),
                              host_ptr->sensor[i].sensorname.c_str());

                    host_ptr->sensor[i].uuid = uuid ;
                    rc = PASS ;
                    break ;
                }
            }
        }
    }
    if ( rc == FAIL_NOT_FOUND )
    {
        wlog ("%s '%s' sensor not found\n", hostname.c_str() , sensorname.c_str());
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : hwmon_del_groups
 *
 * Description: Delete all the groups from the specified host in hwmon
 *
 * Purpose    : In support of group reprovisioning
 *
 *****************************************************************************/

int hwmonHostClass::hwmon_del_groups ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;

    for ( int g = 0 ; g < host_ptr->groups ; g++ )
    {
        hwmonGroup_init ( host_ptr->hostname , &host_ptr->group[g] );
    }

    host_ptr->groups = 0 ;
    return (rc);
}

/*****************************************************************************
 *
 * Name       : hwmon_del_sensors
 *
 * Description: Delete all the sensors from the specified host in hwmon
 *
 * Purpose    : In support of sensor reprovisioning
 *
 *****************************************************************************/

int hwmonHostClass::hwmon_del_sensors ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    int rc = PASS ;

    host_ptr->quanta_server = false ;

    for ( int s = 0 ; s < host_ptr->sensors ; s++ )
    {
        hwmonSensor_init ( host_ptr->hostname, &host_ptr->sensor[s] );
    }

    /* these are the sample data transient lists */
    for ( int i = 0 ; i < (MAX_HOST_SENSORS-1) ; i++ )
    {
        sensor_data_init ( host_ptr->sample[i] );
    }

    host_ptr->sensors =
    host_ptr->samples =
    host_ptr->profile_sensor_checksum =
    host_ptr->sample_sensor_checksum =
    host_ptr->last_sample_sensor_checksum = 0 ;
    return (rc);
}



/* look up a host name from a host uuid */
string hwmonHostClass::get_hostname ( string uuid )
{
    if ( !uuid.empty() )
    {
        string hostname = hostBase.get_hostname ( uuid ) ;
        if ( !hostname.empty() )
        {
            dlog ("%s is hostname for uuid:%s\n", hostname.c_str(), uuid.c_str());
            return (hostname);
        }
    }
    wlog ("hostname not found (uuid:%s)\n", uuid.c_str());
    return ("");
}

/*************************************************************************
 *
 * Sensor Model Attributes Saving and Restoring Support Utilities
 *
 *************************************************************************/
void init_model_attributes ( model_attr_type & attr )
{
    attr.interval = HWMON_DEFAULT_AUDIT_INTERVAL ;
    for ( int i = 0 ; i < MAX_HOST_GROUPS ; i++ )
    {
        attr.group_actions[i].name  = HWMON_GROUP_NAME__NULL ;
        attr.group_actions[i].minor = HWMON_ACTION_IGNORE ;
        attr.group_actions[i].major = HWMON_ACTION_LOG    ;
        attr.group_actions[i].critl = HWMON_ACTION_ALARM  ;
    }
    attr.groups = 0 ;
}

/*****************************************************************************
 *
 * Name       : save_model_attributes
 *
 * Description: Save key sensor group settings.
 *
 *            - severity level group_actions
 *            - audit interval
 *
 *****************************************************************************/

void hwmonHostClass::save_model_attributes ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    init_model_attributes ( host_ptr->model_attributes_preserved );
    if ( host_ptr->groups )
    {
        for ( int g = 0 ; g < host_ptr->groups ; g++ )
        {
            host_ptr->model_attributes_preserved.group_actions[g].name  = host_ptr->group[g].group_name ;
            host_ptr->model_attributes_preserved.group_actions[g].minor = host_ptr->group[g].actions_minor_group ;
            host_ptr->model_attributes_preserved.group_actions[g].major = host_ptr->group[g].actions_major_group ;
            host_ptr->model_attributes_preserved.group_actions[g].critl = host_ptr->group[g].actions_critl_group ;
        }
        host_ptr->model_attributes_preserved.interval = host_ptr->interval ;
        host_ptr->model_attributes_preserved.groups = host_ptr->groups ;
    }
}

/******************************************************************************
 *
 * Name       : restore_group_actions
 *
 * Description: Copy saved severity level group action into the matching
 *              sensor group (name).
 *
 *****************************************************************************/

void hwmonHostClass::restore_group_actions ( struct hwmonHostClass::hwmon_host * host_ptr,
                                                      struct sensor_group_type * group_ptr  )
{
    if ( ( host_ptr ) && ( group_ptr ) && ( host_ptr->model_attributes_preserved.groups ) )
    {
        for ( int i = 0 ; i < host_ptr->model_attributes_preserved.groups ; i++ )
        {
            /* look for a matching group name and restore the settings for that group */
            if ( group_ptr->group_name == host_ptr->model_attributes_preserved.group_actions[i].name )
            {
                ilog ("%s %s group match\n", host_ptr->hostname.c_str(), group_ptr->group_name.c_str());
                if ( group_ptr->actions_minor_group != host_ptr->model_attributes_preserved.group_actions[i].minor )
                {
                    group_ptr->actions_minor_group = host_ptr->model_attributes_preserved.group_actions[i].minor ;
                    ilog ("%s %s group 'minor' action restored to '%s'\n",
                              host_ptr->hostname.c_str(),
                              group_ptr->group_name.c_str(),
                              group_ptr->actions_minor_group.c_str());
                }
                if ( group_ptr->actions_major_group != host_ptr->model_attributes_preserved.group_actions[i].major )
                {
                    group_ptr->actions_major_group = host_ptr->model_attributes_preserved.group_actions[i].major ;
                    ilog ("%s %s group 'major' action restored to '%s'\n",
                              host_ptr->hostname.c_str(),
                              group_ptr->group_name.c_str(),
                              group_ptr->actions_major_group.c_str());
                }
                if ( group_ptr->actions_critl_group != host_ptr->model_attributes_preserved.group_actions[i].critl )
                {
                    group_ptr->actions_critl_group = host_ptr->model_attributes_preserved.group_actions[i].critl ;
                    ilog ("%s %s group 'critical' action restored to '%s'\n",
                              host_ptr->hostname.c_str(),
                              group_ptr->group_name.c_str(),
                              group_ptr->actions_critl_group.c_str());
                }

                /* don't need to look anymore */
                return ;
            }
        }
    }
}

/*****************************************************************************
 *
 * Name       : ipmi_sensor_model_learn
 *
 * Description: Setup hwmon for a sesor model relearn.
 *              Relearn is a background operation.
 *              Generates warning log if requested while already in progress.
 *
 *****************************************************************************/

int hwmonHostClass::ipmi_learn_sensor_model ( string uuid )
{
   /* check for empty list condition */
   if ( hwmon_head == NULL )
   {
      elog ("no provisioned hosts\n");
      return FAIL_HOSTNAME_LOOKUP ;
   }

   else if ( hostUtil_is_valid_uuid ( uuid ) == false )
   {
      elog ("invalid host uuid:%s\n",
                uuid.empty() ? "empty" : uuid.c_str());

      return FAIL_INVALID_UUID ;
   }

   for ( struct hwmon_host * ptr = hwmon_head ;  ; ptr = ptr->next )
   {
       string hostname = hostBase.get_hostname ( uuid ) ;
       if ( hostname == ptr->hostname )
       {
           int rc ;

           if ( ptr->relearn == true )
           {
               wlog ("%s sensor model relearn already in progress\n",
                         ptr->hostname.c_str());

               wlog ("%s ... projected completion time: %s\n",
                         ptr->hostname.c_str(),
                         ptr->relearn_done_date.c_str());

               rc = RETRY ;
           }
           else
           {
               ilog ("%s sensor model relearn request accepted\n",
                         ptr->hostname.c_str());

               ptr->bmc_fw_version.clear();
               ptr->relearn_request = true ;
               ptr->relearn_retry_counter = 0 ;
               rc = PASS ;
           }
           return rc ;
       }

       if (( ptr->next == NULL ) || ( ptr == hwmon_tail ))
           break ;
    }

    elog ("hostname lookup failed for uuid:%s\n", uuid.c_str());
    return FAIL_HOSTNAME_LOOKUP ;
}

/*********************************************************************************
 *
 * Name       : manage_sensor_state
 *
 * Purpose    : manage sensor that change events
 *
 * Description: Manages sensor failures in the following way
 *
 *  1. if the sensor is suppressed then check to see if it is already alarmed
 *     and if so clear that alarm. Send degrade clear message to mtce if this is
 *     the only sensor that is degraded.
 *
 *  2. if the sensor is already failed then
 *     - see if its severity level has changed
 *        - if the new level is to not alarm then clear the alarm.
 *        - if the new level is alarm then raise the correct alarm level
 *
 *  3. if the severity action is to alarm then raise the alarm
 *
 * Assumptions: sensor status in the database is managed by the caller
 *
 * Parameters:
 *
 *     hostname - the host that is affected.
 *       sensor - the sensor that is affected
 *     severity - any of sensor_severity_enum types
 *
 **********************************************************************************/
int hwmonHostClass::manage_sensor_state ( string & hostname, sensor_type * sensor_ptr, sensor_severity_enum severity )
{
    int rc  = FAIL_UNKNOWN_HOSTNAME ;

    hwmonHostClass::hwmon_host * host_ptr = hwmonHostClass::getHost ( hostname );
    if ( host_ptr )
    {
        string reason = REASON_OOT ;

        bool ignore_action       = false ;
        bool log_action          = false ;
        bool clear_alarm         = false ;
        bool clear_degrade       = false ;
        bool clear_log           = false ;

        bool assert_alarm        = false ;
        bool assert_degrade      = false ;
        bool assert_log_minor    = false ;
        bool assert_log_major    = false ;
        bool assert_log_critical = false ;

        int  current_severity = HWMON_SEVERITY_GOOD ;

        /* load up the severity level */
        if ( !sensor_ptr->status.compare("ok") )
            current_severity = HWMON_SEVERITY_GOOD ;
        else if ( !sensor_ptr->status.compare("critical") )
            current_severity = HWMON_SEVERITY_CRITICAL ;
        else if ( !sensor_ptr->status.compare("major") )
            current_severity = HWMON_SEVERITY_MAJOR ;
        else if ( !sensor_ptr->status.compare("minor") )
            current_severity = HWMON_SEVERITY_MINOR ;
        else if ( !sensor_ptr->status.compare("offline") )
        {
            current_severity = HWMON_SEVERITY_GOOD ;
            return (PASS);
        }
        else
        {
            slog ("%s unsupported sensor status '%s'\n", hostname.c_str(), sensor_ptr->status.c_str());
            return (FAIL_BAD_STATE);
        }

        /* Check suppression */
        if ( sensor_ptr->suppress == true )
        {
            reason = REASON_SUPPRESSED ;
            blog ("%s '%s' sensor %s\n", hostname.c_str(), sensor_ptr->sensorname.c_str(), reason.c_str());

            if ( sensor_ptr->critl.logged || sensor_ptr->major.logged || sensor_ptr->minor.logged )
            {
                clear_log = true ;
            }

            if ( sensor_ptr->alarmed == true )
                clear_alarm = true ;

            if ( sensor_ptr->degraded == true )
                clear_degrade = true ;

            clear_ignored_state (sensor_ptr);
            clear_logged_state  (sensor_ptr);
        }

        /* ignore these cases if suppress is true (else if) */
        else if ( severity == HWMON_SEVERITY_GOOD )
        {
            reason = REASON_OK ;
            if ( sensor_ptr->critl.logged || sensor_ptr->major.logged || sensor_ptr->minor.logged )
            {
                clear_log = true ;
            }

            if ( sensor_ptr->alarmed == true )
            {
                clear_alarm = true ;
            }

            if ( sensor_ptr->degraded == true )
            {
                clear_degrade = true ;
            }
            clear_ignored_state (sensor_ptr);
            clear_logged_state  (sensor_ptr);
        }
        else if ( severity == HWMON_SEVERITY_MINOR )
        {
            if ( sensor_ptr->degraded == true )
                clear_degrade = true ;

            if ( sensor_ptr->minor.ignored == true )
            {
                reason = REASON_IGNORED ;
                if ( is_alarmed ( sensor_ptr ) == true )
                {
                    clear_alarm = true ;
                }
                ignore_action = true ;
            }
            else if ( ( log_action = is_log_action ( sensor_ptr->actions_minor )) == true )
            {
                if ( sensor_ptr->minor.logged == false)
                {
                    clear_logged_state ( sensor_ptr );
                    assert_log_minor = true ;
                }

                if ( sensor_ptr->alarmed == true )
                {
                    clear_alarm = true ;
                }
                clear_ignored_state ( sensor_ptr );
            }
            else if ( sensor_ptr->alarmed == true )
            {
                if (( ignore_action == true ) || ( log_action == true ))
                {
                    clear_alarm = true ;
                }
                else if ( current_severity != HWMON_SEVERITY_MINOR )
                {
                    assert_alarm = true ;
                }
            }
            else
            {
                assert_alarm = true ;
            }

            /* Minor assertions should not degrade */
            if ( sensor_ptr->degraded == true )
            {
                clear_degraded_state ( sensor_ptr ) ;
            }
        }
        else if ( severity == HWMON_SEVERITY_MAJOR )
        {
            if ( sensor_ptr->major.ignored == true )
            {
                reason = REASON_IGNORED ;
                if ( is_alarmed ( sensor_ptr ) == true )
                {
                    clear_alarm = true ;
                }
                ignore_action = true ;

                if ( sensor_ptr->degraded == true )
                    clear_degrade = true ;
            }

            else if (( log_action = is_log_action ( sensor_ptr->actions_major )) == true )
            {
                if ( sensor_ptr->major.logged == false)
                {
                    clear_logged_state ( sensor_ptr );
                    assert_log_major = true ;
                }

                if ( sensor_ptr->alarmed == true )
                {
                    clear_alarm = true ;
                }
                clear_ignored_state ( sensor_ptr );
            }

            else if ( sensor_ptr->alarmed == true )
            {
                if (( ignore_action == true ) || ( log_action == true ))
                {
                    clear_alarm = true ;
                }
                else if ( current_severity != HWMON_SEVERITY_MAJOR )
                {
                    assert_alarm = true ;
                }
            }
            else
            {
                assert_alarm = true ;
            }

            if ( sensor_ptr->degraded == false )
            {
                if (( ignore_action == true ) || ( log_action == true ))
                {
                    ; // clear_degrade = true ;
                }
                else
                {
                    assert_degrade = true ;
                }
            }
        }
        else if ( severity == HWMON_SEVERITY_CRITICAL )
        {
            if ( sensor_ptr->critl.ignored == true )
            {
                reason = REASON_IGNORED ;
                if ( is_alarmed ( sensor_ptr ) == true )
                {
                    clear_alarm = true ;
                }
                ignore_action = true ;

                if ( sensor_ptr->degraded == true )
                    clear_degrade = true ;
            }

            else if ( ( log_action = is_log_action ( sensor_ptr->actions_critl )) == true )
            {
                if ( sensor_ptr->critl.logged == false )
                {
                    clear_logged_state ( sensor_ptr );
                    assert_log_critical = true ;
                }

                if ( sensor_ptr->alarmed == true )
                {
                    clear_alarm = true ;
                }
                clear_ignored_state ( sensor_ptr );
            }

            else if ( sensor_ptr->alarmed == true )
            {
                if (( ignore_action == true ) || ( log_action == true ))
                {
                    clear_alarm = true ;
                }
                else if ( current_severity != HWMON_SEVERITY_CRITICAL )
                {
                    assert_alarm = true ;
                }
            }
            else
            {
                assert_alarm = true ;
            }

            if ( sensor_ptr->degraded == false )
            {
                if (( ignore_action == true ) || ( log_action == true ))
                {
                    ; // clear_degrade = true ;
                }
                else
                {
                    assert_degrade = true ;
                }
            }
        }

        if ( assert_degrade || clear_degrade || clear_alarm || assert_alarm )
        {
            ilog ("%s %-20s assert_degrade = %d  severity = %x  %s\n",         hostname.c_str(), sensor_ptr->sensorname.c_str(), assert_degrade, severity,                            sensor_ptr->suppress ? "suppressed" : "   action " );
            ilog ("%s %-20s  clear_degrade = %d  status   = %3s  minor = %s\n", hostname.c_str(), sensor_ptr->sensorname.c_str(), clear_degrade , sensor_ptr->status.c_str(),          sensor_ptr->actions_minor.c_str());
            ilog ("%s %-20s    clear_alarm = %d  degraded = %3s  major = %s\n", hostname.c_str(), sensor_ptr->sensorname.c_str(), clear_alarm   , sensor_ptr->degraded ? "Yes" : "No ", sensor_ptr->actions_major.c_str());
            ilog ("%s %-20s   assert_alarm = %d  alarmed  = %3s  critl = %s\n", hostname.c_str(), sensor_ptr->sensorname.c_str(), assert_alarm  , sensor_ptr->alarmed ? "Yes" : "No ",  sensor_ptr->actions_critl.c_str());
        }

        if ( assert_log_critical || assert_log_major || assert_log_minor || clear_log )
        {
            ilog ("%s %s assert log [%s%s%s] %s %s\n",
                      hostname.c_str(),
                      sensor_ptr->sensorname.c_str(),
                      assert_log_critical ? "crit"  : "",
                      assert_log_major    ? "major" : "",
                      assert_log_minor    ? "minor" : "",
                      clear_log           ? "clear log" : "",
                      ignore_action       ? "ignore" : "" );
        }

        /* logic error check */
        if ((( assert_degrade == true ) && ( clear_degrade == true )) ||
             ((  assert_alarm == true ) && (   clear_alarm == true )))
        {
            slog ("%s conflicting degrade state or alarming calculation - favoring clear\n", hostname.c_str() );
            if ( clear_alarm == true )
            {
                assert_alarm = false ;
            }
            if ( clear_degrade == true )
            {
               assert_degrade = false ;
            }
        }

        /***************************************************************************
         *
         *                         TAKE THE ACTIONS NOW
         *
         **************************************************************************/

        if ( clear_log == true )
        {
            hwmonLog_clear ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
            clear_logged_state ( sensor_ptr );
        }

        if ( assert_log_critical )
        {
            clear_logged_state (sensor_ptr);
            sensor_ptr->critl.logged = true ;
            hwmonLog_critical ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
        }
        if ( assert_log_major )
        {
            clear_logged_state (sensor_ptr);
            sensor_ptr->major.logged = true ;
            hwmonLog_major ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
        }
        if ( assert_log_minor )
        {
            clear_logged_state (sensor_ptr);
            sensor_ptr->minor.logged = true ;
            hwmonLog_minor ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
        }

        /* handle clearing the specified alarm */
        if ( clear_alarm == true )
        {
             hwmonAlarm_clear ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
             clear_degraded_state ( sensor_ptr );
             clear_alarmed_state  ( sensor_ptr );
        }
        /* handle asserting the specified alarm */
        else if ( assert_alarm == true )
        {
            clear_alarmed_state ( sensor_ptr);
            if ( severity == HWMON_SEVERITY_CRITICAL )
            {
                hwmonAlarm_critical ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
                set_alarmed_severity ( sensor_ptr, FM_ALARM_SEVERITY_CRITICAL );
                if ( assert_degrade != true )
                    assert_degrade = true ;
            }
            else if ( severity == HWMON_SEVERITY_MAJOR )
            {
                hwmonAlarm_major ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
                set_alarmed_severity ( sensor_ptr, FM_ALARM_SEVERITY_MAJOR );
                if ( assert_degrade != true )
                    assert_degrade = true ;
            }
            else if ( severity == HWMON_SEVERITY_MINOR )
            {
                hwmonAlarm_minor ( hostname, HWMON_ALARM_ID__SENSOR, sensor_ptr->sensorname, reason );
                set_alarmed_severity ( sensor_ptr, FM_ALARM_SEVERITY_MINOR );
            }
            /* NEW */
            clear_logged_state  ( sensor_ptr );
            clear_ignored_state ( sensor_ptr );
        }

        /* handle sending a degrade clear request to mtcAgent */
        if ( clear_degrade == true )
        {
            clear_degraded_state ( sensor_ptr );
        }

        /* handle sending a degrade request to mtcAgent */
        else if ( assert_degrade == true )
        {
            set_degraded_state ( sensor_ptr );
        }
    }
    else
    {
        wlog ("%s Unknown Host\n", hostname.c_str());
    }

    sensorState_print ( hostname, sensor_ptr );
    return (rc);
}

/*****************************************************************************
 *
 * Name       : audit_interval_change
 *
 * Description: Set a host specific flag indicating that the sensor monitoring
 *              audit interval for this host has changed.
 *
 *              The actual interval change is handled in the add handler.
 *
 *              This API is used during group load from the database when the
 *              default host_ptr->interval is zero or groups have differing
 *              values.
 *
 *****************************************************************************/

void hwmonHostClass::audit_interval_change ( string hostname )
{
    if ( !hostname.empty())
    {
        hwmon_host * host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            /* handle refreshing sysinv at base level to avoid deadlock */
            host_ptr->interval_changed = true ;
        }
    }
}

/*****************************************************************************
 *
 * Name       : modify_audit_interval
 *
 * Description: Changes the host_ptr->interval to the specified value and
 *              sets the 'interval_changed' flag indicating that the sensor
 *              monitoring audit interval for this host has changed.
 *
 *              The actual interval change is handled in the DELAY stage of the
 *              ipmi_sensor_monitor.
 *
 *              This API is called by http group modify handler to trigger
 *              change of the sensor audit interval to a specific value.
 *
 *****************************************************************************/

void hwmonHostClass::modify_audit_interval ( string hostname , int interval )
{
    if ( !hostname.empty())
    {
        hwmonHostClass::hwmon_host * host_ptr ;
        host_ptr = hwmonHostClass::getHost ( hostname );
        if ( host_ptr != NULL )
        {
            if ( host_ptr->interval != interval )
            {
                host_ptr->interval_old = host_ptr->interval ;
                host_ptr->interval = interval ;

                /* handle popping this new value to hwmon groups
                 * and sysinv database at base level to avoid deadlock */

                host_ptr->interval_changed = true ;
            }
        }
    }
}


/* log sensor data to a tmp file to assis debug of sensor read issues */
void hwmonHostClass::log_sensor_data ( struct hwmonHostClass::hwmon_host * host_ptr, string & sensorname, string from, string to )
{
    string sensor_datafile = IPMITOOL_OUTPUT_DIR ;
    sensor_datafile.append(host_ptr->hostname);
    sensor_datafile.append(IPMITOOL_SENSOR_OUTPUT_FILE_SUFFIX);

    string debugfile = "/tmp/" ;
    debugfile.append(host_ptr->hostname);
    debugfile.append(IPMITOOL_SENSOR_OUTPUT_FILE_SUFFIX);
    debugfile.append("_debug");

    string source = pt() ;
    source.append (" - ");
    source.append (sensorname);
    source.append (" from '");
    source.append (from );
    source.append ("' to '");
    source.append (to );
    source.append ("'\n");
    daemon_log ( debugfile.data(), source.data());
    daemon_log ( debugfile.data(), host_ptr->bmc_thread_info.data.data());
    daemon_log ( debugfile.data(), daemon_read_file ( sensor_datafile.data()).data());
    daemon_log ( debugfile.data(), "---------------------------------------------------------------------\n");
}


void hwmonHostClass::print_node_info ( void )
{
    fflush (stdout);
    fflush (stderr);
}

void hwmonHostClass::mem_log_info ( struct hwmonHostClass::hwmon_host * hwmon_host_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s has %d sensor(s) across %d sensor group(s)\n",
               hwmon_host_ptr->hostname.c_str(),
               hwmon_host_ptr->sensors,
               hwmon_host_ptr->groups );
     mem_log (str);
}

void hwmonHostClass::mem_log_options ( struct hwmonHostClass::hwmon_host * hwmon_host_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tMonitoring: %s  Provisioned: %s  Connected: %s  Count: %d\n",
               hwmon_host_ptr->hostname.c_str(),
               hwmon_host_ptr->monitor ? "YES" : "no" ,
               hwmon_host_ptr->bm_provisioned ? "YES" : "no",
               hwmon_host_ptr->connected ? "YES" : "no",
               hwmon_host_ptr->sensor_query_count);

     mem_log (str);
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tMon Gates : GroupIndex:%d Groups:%d Sensors:%d\n",
               hwmon_host_ptr->hostname.c_str(),
               hwmon_host_ptr->group_index,
               hwmon_host_ptr->groups,
               hwmon_host_ptr->sensors );
     mem_log (str);
}

void hwmonHostClass::mem_log_bm ( struct hwmonHostClass::hwmon_host * hwmon_host_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tbm_ip:%s bm_un:%s bm_type:%s\n",
                hwmon_host_ptr->hostname.c_str(),
                hwmon_host_ptr->bm_ip.c_str(),
                hwmon_host_ptr->bm_un.c_str(),
                hwmon_host_ptr->bm_type.c_str());
    mem_log (str);
}

void hwmonHostClass::mem_log_threads (  struct hwmonHostClass::hwmon_host * hwmon_host_ptr)
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\tThread Stage:%d Runs:%d Progress:%d Ctrl Status:%d Thread Status:%d\n",
                hwmon_host_ptr->hostname.c_str(),
                hwmon_host_ptr->bmc_thread_ctrl.stage,
                hwmon_host_ptr->bmc_thread_ctrl.runcount,
                hwmon_host_ptr->bmc_thread_info.progress,
                hwmon_host_ptr->bmc_thread_ctrl.status,
                hwmon_host_ptr->bmc_thread_info.status);
    mem_log (str);
}

void hwmonHostClass::check_accounting ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    int count = 0 ;

    for ( int g = 0 ; g < host_ptr->groups ; ++g )
    {
        for ( int s = 0 ; s < host_ptr->group[g].sensors ; ++s )
        {
            count++ ;
        }
    }

    if ( count == host_ptr->sensors )
        host_ptr->accounting_ok = true ;
    else
        host_ptr->accounting_ok = false ;

    snprintf ( &str[0], MAX_MEM_LOG_DATA, "SENSOR: Accounting is %s (%d:%d)", host_ptr->accounting_ok ? "GOOD" : "BAD", host_ptr->sensors, count );
    mem_log (str);
}

void hwmonHostClass::mem_log_groups ( struct hwmonHostClass::hwmon_host * host_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;

    for ( int i = 0 ; i < host_ptr->groups ; i++ )
    {
        /* Don't dump sensor group info if there are no sensors in it */
        if ( !host_ptr->group[i].sensors )
            continue ;

        snprintf (&str[0], MAX_MEM_LOG_DATA, " ");
        mem_log (str);

        snprintf (&str[0], MAX_MEM_LOG_DATA, "GROUP : %03d secs %s %s %s uuid:%s\n",
                   host_ptr->group[i].group_interval,
                   host_ptr->group[i].group_name.c_str(),
                   host_ptr->group[i].group_state.c_str(),
                   host_ptr->group[i].suppress ? "suppressed" : "",
                   host_ptr->group[i].group_uuid.c_str());
        mem_log (str);

        snprintf (&str[0], MAX_MEM_LOG_DATA, "        Actions: [minor:%s][%s] [major:%s][%s] [crit:%s][%s]\n\n",
                   host_ptr->group[i].actions_minor_group.c_str(),
                   host_ptr->group[i].actions_minor_choices.c_str(),
                   host_ptr->group[i].actions_major_group.c_str(),
                   host_ptr->group[i].actions_major_choices.c_str(),
                   host_ptr->group[i].actions_critl_group.c_str(),
                   host_ptr->group[i].actions_critical_choices.c_str());
        mem_log (str);

#ifdef WANT_UNIT_MEMLOG_INFO /* not used presently */
        snprintf (&str[0], MAX_MEM_LOG_DATA, "   > Info      : algorithm: %s - unit [base:%s] [rate:%s] [modifier:%s]\n",
                   host_ptr->group[i].algorithm.c_str(),
                   host_ptr->group[i].unit_base_group.c_str(),
                   host_ptr->group[i].unit_rate_group.c_str(),
                   host_ptr->group[i].unit_modifier_group.c_str());
        mem_log (str);
#endif

#ifdef WANT_THRESHOLD_MEMLOG_INFO /* not used presently */
        snprintf (&str[0], MAX_MEM_LOG_DATA, "   > Threshold: Lcrit - Lmajor - Lminor | Uminor - Umajor - Ucrit\n");
        mem_log (str);

        snprintf (&str[0], MAX_MEM_LOG_DATA, "   >          %5.3f - %6.3f - %6.3f | %6.3f - %6.3f - %6.3f\n",
                   host_ptr->group[i].t_critical_lower_group, host_ptr->group[i].t_major_lower_group ,
                   host_ptr->group[i].t_minor_lower_group, host_ptr->group[i].t_minor_upper_group ,
                   host_ptr->group[i].t_major_upper_group, host_ptr->group[i].t_critical_upper_group);
        mem_log (str);
#endif

        if ( host_ptr->accounting_ok == true )
        {
            for ( int s = 0 ; s < host_ptr->group[i].sensors ; s++ )
            {
                sensor_type * sensor_ptr = host_ptr->group[i].sensor_ptr[s] ;

                snprintf ( &str[0], MAX_MEM_LOG_DATA, "SENSOR: %-20s %-20s %8s-%-8s sev:%-8s [minor:%-6s major:%-6s crit:%-6s] [alarmed:%c%c%c] [ignored:%c%c%c] [logged:%c%c%c] %s:%s %s%s%s\n",
                host_ptr->group[i].group_name.c_str(),
                sensor_ptr->sensorname.c_str(),
                sensor_ptr->state.c_str(),
                sensor_ptr->status.c_str(),
   get_severity(sensor_ptr->severity).c_str(),
                sensor_ptr->actions_minor.c_str(),
                sensor_ptr->actions_major.c_str(),
                sensor_ptr->actions_critl.c_str(),
                sensor_ptr->minor.alarmed ? 'Y' : '.',
                sensor_ptr->major.alarmed ? 'Y' : '.',
                sensor_ptr->critl.alarmed ? 'Y' : '.',
                sensor_ptr->minor.ignored ? 'Y' : '.',
                sensor_ptr->major.ignored ? 'Y' : '.',
                sensor_ptr->critl.ignored ? 'Y' : '.',
                sensor_ptr->minor.logged  ? 'Y' : '.',
                sensor_ptr->major.logged  ? 'Y' : '.',
                sensor_ptr->critl.logged  ? 'Y' : '.',
                sensor_ptr->uuid.c_str(),
                sensor_ptr->group_uuid.substr(0,8).c_str(),
                sensor_ptr->degraded ? "degraded "   : "",
                sensor_ptr->alarmed  ? "alarmed "    : "",
                sensor_ptr->suppress ? "suppressed " : "");
                mem_log (str);
            }
        }
        else
        {
            string sensor_list = "" ;
            bool first = true ;
            bool done = false ;
            for ( int x = 0 ; x < host_ptr->group[i].sensors ; x++ )
            {
                sensor_type * sensor_ptr = host_ptr->group[i].sensor_ptr[x] ;
                sensor_list.append(sensor_ptr->sensorname);
                if ( x < host_ptr->group[i].sensors - 1 )
                    sensor_list.append(", ");

                if ( x == host_ptr->group[i].sensors - 1 )
                {
                    done = true ;
                }
                if ((( x % 8 == 0 ) & ( x != 0 )) || ( done == true ))
                {
                    if ( first == true )
                    {
                        snprintf (&str[0], MAX_MEM_LOG_DATA, "   SENSORS:%02d: %s\n", host_ptr->group[i].sensors, sensor_list.c_str() );
                        mem_log (str);
                        first = false ;
                    }
                    else
                    {
                        snprintf (&str[0], MAX_MEM_LOG_DATA, "              %s\n", sensor_list.c_str() );
                        mem_log (str);
                    }
                    sensor_list = " " ;
                }
                if ( done == true ) break ;
            }
        }
    }
}

void hwmonHostClass::memDumpNodeState ( string hostname )
{
    hwmonHostClass::hwmon_host* hwmon_host_ptr ;
    hwmon_host_ptr = hwmonHostClass::getHost ( hostname );
    if ( hwmon_host_ptr == NULL )
    {
        mem_log ( hostname, ": ", "Not Found in hwmonHostClass\n" );
        return ;
    }
    else
    {
        mem_log_options ( hwmon_host_ptr );
        hwmonHostClass::hostBase.memDumpNodeState ( hostname );
        mem_log_info    ( hwmon_host_ptr );
        mem_log_bm      ( hwmon_host_ptr );
        mem_log_threads ( hwmon_host_ptr );
        check_accounting( hwmon_host_ptr );
        mem_log_groups  ( hwmon_host_ptr );
    }
}

void hwmonHostClass::memDumpAllState ( void )
{
    struct hwmon_host * ptr = hwmon_head ;

    if ( hwmon_head == NULL ) return ;

    hwmonHostClass::hostBase.memLogDelimit ();

    /* walk the node list looking for nodes that should be monitored */
    for ( int i = 0 ; i < hosts ; i++ )
    {
        memDumpNodeState ( ptr->hostname );
        hwmonHostClass::hostBase.memLogDelimit ();
        ptr = ptr->next ;
        if ( ptr == NULL )
            break ;
    }
}

void hwmonHostClass::sensorState_print_debug ( struct hwmonHostClass::hwmon_host * host_ptr, string sensorname, string proc, int line )
{
    /* loop over all the sensors handling their current severity */
    for ( int i = 0 ; i < host_ptr->sensors ; i++ )
    {
        sensor_type * ptr = &host_ptr->sensor[i] ;

        if ( ptr->sensorname.compare(sensorname) == 0 )
        {
            plog ("Location: %s %d\n", proc.c_str(), line );
            sensorState_print ( host_ptr->hostname, ptr );
            break ;
        }
    }
}


