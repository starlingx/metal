/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud 'Heartbeat Agent' Alarm Module
  */

#include <sys/types.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "alm"

#include "daemon_common.h"
#include "nodeBase.h"     /* for ... fail codes                   */
#include "jsonUtil.h"
#include "alarm.h"        /* for ... module header                */

static msgClassSock * user_sock_ptr = NULL ;

/* A call to this API is required in advance of sending an alarm request */
int alarm_register_user ( msgClassSock * sock_ptr )
{
    int rc = PASS ;
    if ( sock_ptr && sock_ptr->getFD() && sock_ptr->sock_ok() )
    {
        ilog ("Registered with maintenance alarm service\n");
        user_sock_ptr = sock_ptr ;
    }
    else
    {
        elog ("Failed to register with maintenance alarm service\n");
        rc = FAIL_SOCKET_BIND ;
    }
    return (rc);
}

void alarm_unregister_user ( void )
{
   user_sock_ptr = NULL ;
}

/* Construct an alarm request json string in the following form
   {\"mtcalarm\":[{\"alarmid\":\"200.009\",\"hostname\":\"compute-3\",\"operation\":\"set\",\"severity\":\"major\",\"entity\":\"cluster-host\",\"prefix\":\"service=heartbeat\"}, {\"alarmid\":\"200.005\",\"hostname\":\"compute-3\",\"operation\":\"set\",\"severity\":\"major\",\"entity\":\"Management\",\"prefix\":\"service=heartbeat\"}]}"

   or

   { \"mtcalarm\":
       [
          {
             \"alarmid\":\"200.009\",
             \"hostname\":\"compute-3\",
             \"operation\":\"set\",
             \"severity\":\"major\",
             \"entity\":\"cluster-host\",
             \"prefix\":\"service=heartbeat\"
          }
       ]
    }

*/
int alarm_ ( string hostname, const char * id, EFmAlarmStateT state, EFmAlarmSeverityT severity, const char * entity, string prefix )
{
    int rc = PASS ;
    char request [MAX_ALARM_REQ_MSG_SIZE] ;
    string msg_type ;
    string sev ;

    if ( user_sock_ptr == NULL )
    {
        slog ("alarm socket is NULL");
        return (FAIL_NULL_POINTER );
    }
    else if ( ! user_sock_ptr->sock_ok() )
    {
        elog ("alarm socket is not ok");
        return (FAIL_OPERATION);
    }

    if ( state == FM_ALARM_STATE_MSG )
        msg_type = "msg" ;
    else if ( state == FM_ALARM_STATE_SET )
        msg_type = "set" ;
    else
        msg_type = "clear" ;

    switch ( severity )
    {
        case FM_ALARM_SEVERITY_CLEAR:
            sev = "clear" ;
            break ;
        case FM_ALARM_SEVERITY_WARNING:
            sev = "warning";
            break ;
        case FM_ALARM_SEVERITY_MINOR:
            sev = "minor";
            break ;
        case FM_ALARM_SEVERITY_MAJOR:
            sev = "major";
            break ;
        case FM_ALARM_SEVERITY_CRITICAL:
            sev = "critical";
            break ;
        default :
            sev = "unknown";
            break ;
    }

    snprintf ( request, MAX_ALARM_REQ_MSG_SIZE, "{\"mtcalarm\":[{\"alarmid\":\"%s\",\"hostname\":\"%s\",\"operation\":\"%s\",\"severity\":\"%s\",\"entity\":\"%s\",\"prefix\":\"%s\"}]}",
               id,
               hostname.data(),
               msg_type.data(),
               sev.data(),
               entity,
               prefix.data());
    size_t len = strlen(request) ;

    /* Retrying up to 3 times if the send fails */
    for ( int i = 0 ; i < 3 ; i++ )
    {
        int bytes = user_sock_ptr->write((char*)&request[0], len );
        if ( bytes <= 0 )
        {
            elog("%s failed to send alarm request (%d:%m)\n", hostname.c_str(), errno );
            elog("... %s\n", request);
            rc = FAIL_SOCKET_SENDTO ;
        }
        else if ( ((int)len) != bytes )
        {
            elog ("%s failed to send complete alarm message (%d:%ld)\n", hostname.c_str(), bytes, len  );
        }
        else
        {
            ilog ("%s %s %s %s %s", hostname.c_str(), entity, msg_type.c_str(), sev.c_str(), id);
            mlog ("%s %s\n", hostname.c_str(), request);
            return ( PASS ) ;
        }
        daemon_signal_hdlr ();

        usleep (1000);
    }
    return (rc);
}


int alarm_clear ( string hostname, const char * alarm_id_ptr , string entity )
{
    string prefix = "" ;
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_CLEAR, FM_ALARM_SEVERITY_CLEAR, entity.data(), prefix.data() ));
}

int alarm_warning ( string hostname, const char * alarm_id_ptr , string entity )
{
    string prefix = "" ;
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_WARNING, entity.data(), prefix.data() ));
}

int alarm_minor ( string hostname, const char * alarm_id_ptr , string entity )
{
    string prefix = "" ;
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_MINOR, entity.data(), prefix.data() ));
}

int alarm_major ( string hostname, const char * alarm_id_ptr , string entity )
{
    string prefix = "" ;
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_MAJOR, entity.data(), prefix.data() ));
}

int alarm_critical ( string hostname, const char * alarm_id_ptr , string entity )
{
    string prefix = "" ;
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_SET, FM_ALARM_SEVERITY_CRITICAL, entity.data(), prefix.data() ));
}

int alarm_warning_log ( string hostname, const char * alarm_id_ptr , string entity , string prefix )
{
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_WARNING, entity.data(), prefix.data() ));
}

int alarm_minor_log ( string hostname, const char * alarm_id_ptr , string entity , string prefix )
{
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_MINOR, entity.data(), prefix.data() ));
}

int alarm_major_log ( string hostname, const char * alarm_id_ptr , string entity , string prefix )
{
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_MAJOR, entity.data(), prefix.data() ));
}

int alarm_critical_log ( string hostname, const char * alarm_id_ptr , string entity , string prefix )
{
    return (alarm_ ( hostname, alarm_id_ptr, FM_ALARM_STATE_MSG, FM_ALARM_SEVERITY_CRITICAL, entity.data(), prefix.data() ));
}
