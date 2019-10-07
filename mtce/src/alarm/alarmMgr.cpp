/*
 * Copyright (c) 2016-2017,2019 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Starling-X Maintenance Alarm Manager Daemon Manager
  */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

#define __MODULE_PRIVATE__

#include "daemon_common.h" /* for ... gettime_monotonic_nsec */
#include "alarm.h"         /* module header                  */

/* Accomodate for MNFA heartbeat alarms.
 * Up to 2 (Mgmnt and Cluster) for each node of up to 1000 nodes = 2000 */
#define MAX_QUEUED_ALARMS (2000)

/* the alarm queue */
static list<queue_entry_type> alarm_queue ;

/* FM retry throttle */
static unsigned long long _holdoff_timestamp = 0 ;

/*************************************************************************
 *
 * Name       : _pop_front, _pop_back
 *
 * Scope      : local
 *
 * Purpose    : Remove the entry at the head/tail of the queue.
 *
 *              Also reset the log throttle counter.
 *
 ************************************************************************/

void _pop_front( void )
{
    if ( alarm_queue.size() )
    {
        alarm_queue.pop_front();
    }
    _holdoff_timestamp = 0 ;
}

void _pop_back( void )
{
    if ( alarm_queue.size() )
    {
        alarm_queue.pop_back();
    }
    _holdoff_timestamp = 0 ;
}

/*************************************************************************
 *
 * Name       : alarmMgr_queue_clear
 *
 * Purpose    : Clear the alarm queue ; called from init.
 *
 ************************************************************************/
void alarmMgr_queue_clear ( void )
{
    alarm_queue.clear();
}

/*************************************************************************
 *
 * Name       : alarmMgr_queue_alarm
 *
 * Purpose    : Add an incoming alarm request to the tail of the queue.
 *
 ************************************************************************/
void alarmMgr_queue_alarm  ( queue_entry_type entry )
{
    alog ("%s adding %s to alarm queue [size=%ld]\n",
              entry.hostname.c_str(),
              entry.alarmid.c_str(),
              alarm_queue.size() );

    alarm_queue.push_back(entry);
}

/*************************************************************************
 *
 * Name       : alarmMgr_service_queue
 *
 * Purpose    : Service the alarm queue from the head.
 *
 * Description: Load the first/oldest element of the queue and submit it
 *              to FM.
 *
 *              If it fails for a reason that is likely to resolve itself
 *              with a retry, then it is not popped of the head. Instead
 *              it is left there to be retried after the hold off period.
 *
 *              If it fails for a reason that is NOT likely to succeed
 *              by retries then an error log is produced and this faulty
 *              entry is dropped. It is done this way to avoid a bad
 *              entry from stalling/blocking the queue.
 *
 ************************************************************************/

/* 5 second holdoff time before FM retry */
#define RETRY_HOLDOFF_TIME_NSECS ((unsigned long long)(5000000000))

void alarmMgr_service_queue ( void )
{
    alog1 ("Elements: %ld\n", alarm_queue.size());
    if ( alarm_queue.empty() )
        return ;

    /* throttle access to FM if in retry mode */
    if ( _holdoff_timestamp )
    {
        unsigned long long _now_time = gettime_monotonic_nsec ();

        /* retry only retry every RETRY_HOLDOFF_TIME_NSECS while in holdoff */
        if (( _now_time-_holdoff_timestamp ) < RETRY_HOLDOFF_TIME_NSECS)
            return ;
        else
            _holdoff_timestamp = 0 ;
    }

    queue_entry_type entry = alarm_queue.front() ;

    int rc = PASS ;
    string action = entry.operation ;
    action.append (" alarm");

    alog ("%s %s operation:%s severity:%s entity:%s prefix:%s\n",
           entry.hostname.c_str(),
           entry.alarmid.c_str(),
           entry.operation.c_str(),
           entry.severity.c_str(),
           entry.entity.c_str(),
           entry.prefix.c_str());

    EFmAlarmSeverityT sev = alarmUtil_getSev_enum ( entry.severity );

    /* customer logs */
    if ( entry.operation == "msg" )
    {
        if ( sev == FM_ALARM_SEVERITY_WARNING )
        {
           rc = alarmUtil_warning_log ( entry.hostname, entry.alarmid, entry.entity, entry.prefix, entry.timestamp );
        }
        else if ( sev == FM_ALARM_SEVERITY_MINOR )
        {
            rc = alarmUtil_minor_log ( entry.hostname, entry.alarmid, entry.entity, entry.timestamp );
        }
        else if ( sev == FM_ALARM_SEVERITY_MAJOR)
        {
            rc = alarmUtil_major_log ( entry.hostname, entry.alarmid, entry.entity, entry.timestamp );
        }
        else if ( sev == FM_ALARM_SEVERITY_CRITICAL )
        {
            rc = alarmUtil_critical_log ( entry.hostname, entry.alarmid, entry.entity, entry.timestamp );
        }
        else
        {
            rc = FM_ERR_INVALID_REQ ;
            wlog ("Unsupported log severity '%d:%s'\n", sev, entry.severity.c_str());
        }
        action="create log" ;
    }

    /* alarm clear request */
    else if ( entry.operation == "clear" )
    {
        rc = alarmUtil_clear ( entry.hostname, entry.alarmid, entry.entity );
    }

    /* alarm set request */
    else if ( entry.operation == "set" )
    {
        if ( sev == FM_ALARM_SEVERITY_WARNING )
            rc = alarmUtil_warning ( entry.hostname, entry.alarmid, entry.entity, entry.timestamp );
        else if ( sev == FM_ALARM_SEVERITY_MINOR )
            rc = alarmUtil_minor ( entry.hostname, entry.alarmid, entry.entity, entry.timestamp );
        else if ( sev == FM_ALARM_SEVERITY_MAJOR )
            rc = alarmUtil_major ( entry.hostname, entry.alarmid, entry.entity, entry.timestamp );
        else if ( sev == FM_ALARM_SEVERITY_CRITICAL )
            rc = alarmUtil_critical ( entry.hostname, entry.alarmid, entry.entity, entry.timestamp );
        else
        {
            rc = FM_ERR_INVALID_REQ ;
        }
    }
    else
    {
        rc = FM_ERR_INVALID_PARAMETER ;
    }

    /* Handle behavior based on return code */
    if ( rc == FM_ERR_OK )
    {
        /* alarm call succeeded, pop off the list. */
        _pop_front();
    }

    else if ( rc == FM_ERR_ENTITY_NOT_FOUND )
    {
        ilog ("%s %s '%s:%s' ; not found",
                  entry.hostname.c_str(),
                  action.c_str(),
                  entry.alarmid.c_str(),
                  entry.entity.c_str());
        _pop_front();
    }

    /*******************************************************************
     * Now these are non-success cases.
     *******************************************************************/

    /* Most typical failure case first - FM not running */
    else if (( rc == FM_ERR_NOCONNECT       ) ||
             ( rc == FM_ERR_REQUEST_PENDING ) ||
             ( rc == FM_ERR_COMMUNICATIONS  ))
    {
        if ( _holdoff_timestamp == 0 )
             _holdoff_timestamp = gettime_monotonic_nsec();

        string type = "" ;
        if ( rc == FM_ERR_NOCONNECT ) type = "not connected" ;
        else if ( rc == FM_ERR_COMMUNICATIONS ) type = "communication error" ;
        else if ( rc == FM_ERR_REQUEST_PENDING ) type = "pending request" ;

        wlog ("%s %s '%s:%s' failure ; %s ; retrying [q=%ld]",
                  entry.hostname.c_str(),
                  action.c_str(),
                  entry.alarmid.c_str(),
                  entry.entity.c_str(),
                  type.c_str(),
                  alarm_queue.size());
    }

    /* Look for cases where we don't want to retry.
     *
     * These would be cases that are unlikely to resolve with retry.
     */

    /* pop off if alarm already asserted */
    else if ( rc == FM_ERR_ALARM_EXISTS )
    {
        wlog ("%s %s '%s:%s' ; already exists",
              entry.hostname.c_str(),
              action.c_str(),
              entry.alarmid.c_str(),
              entry.entity.c_str());
        _pop_front();
    }

    /* never retry on any of these error cases */
    else if (( rc == FM_ERR_INVALID_REQ          ) ||
             ( rc == FM_ERR_INVALID_ATTRIBUTE    ) ||
             ( rc == FM_ERR_INVALID_PARAMETER    ) ||
             ( rc == FM_ERR_DB_OPERATION_FAILURE ) ||
             ( rc == FM_ERR_RESOURCE_UNAVAILABLE ))
    {
        wlog ("%s failed to %s '%s:%s' ; dropped ; bad request [rc=%d]",
              entry.hostname.c_str(),
              action.c_str(),
              entry.alarmid.c_str(),
              entry.entity.c_str(), rc);
        _pop_front();
    }

    /* never retry due to resource error on assert cases */
    else if (( rc == FM_ERR_NOMEM            ) ||
             ( rc == FM_ERR_SERVER_NO_MEM    ) ||
             ( rc == FM_ERR_NOT_ENOUGH_SPACE ))
    {
        wlog ("%s failed to %s '%s:%s' ; dropped ; resource error [rc=%d]",
              entry.hostname.c_str(),
              action.c_str(),
              entry.alarmid.c_str(),
              entry.entity.c_str(),rc );
        _pop_front();
    }
    else
    {
        wlog ("%s failed to %s '%s:%s' ; dropped ; unexpected [rc=%d]",
              entry.hostname.c_str(),
              action.c_str(),
              entry.alarmid.c_str(),
              entry.entity.c_str(),rc );
        _pop_front();
    }

    /* pop from back if the queue is loaded to the max */
    if ( alarm_queue.size() > MAX_QUEUED_ALARMS )
    {
        wlog ("%s %s '%s:%s' dropped ; most recent ; queue full",
                  entry.hostname.c_str(),
                  action.c_str(),
                  entry.alarmid.c_str(),
                  entry.entity.c_str() );
        _pop_back();
    }
    else
    {
        ilog ("%ld queue entries to service", alarm_queue.size());
    }
}
