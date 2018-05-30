/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Alarm Manager Daemon Handler
  */

#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>       /* for close and usleep */
#include <json-c/json.h>  /* for ... json-c json string parsing */

using namespace std;

#define __MODULE_PRIVATE__

#include "alarm.h"         /* module header                                */
#include "jsonUtil.h"      /* for ... jsonUtil_ utiltiies                  */
#include "nodeTimers.h"    /* for ... maintenance timers                   */
#include "daemon_common.h" /* for UNUSED()                                 */

void daemon_sigchld_hdlr ( void ) { ; }


/** Daemon timer handler */
void _timer_handler ( int sig, siginfo_t *si, void *uc)
{
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;
    UNUSED(sig);
    UNUSED(uc);
    if ( !(*tid_ptr) )
    {
        return ;
    }
    else
    {
        mtcTimer_stop_tid_int_safe (tid_ptr);
    }
}

int alarmHdlr_request_handler ( char * msg_ptr )
{
    int rc = FAIL_JSON_PARSE ;
    struct json_object *raw_obj = json_tokener_parse( msg_ptr );
    jlog ("Alarm Request: %s\n", msg_ptr );
    if ( raw_obj )
    {
        int elements ;

        /* Check response sanity */
        rc = jsonUtil_array_elements ( msg_ptr, MTCALARM_REQ_LABEL, elements );
        if ( elements )
        {
            #define PARSE_FAILURE ((const char *)"failed to parse value for key")
            string alarmid = "" ;
            string hostname = "" ;
            string operation = "" ;
            string severity = "" ;
            string entity = "" ;
            string prefix = "" ;
            string alarm_req = "" ;

            for ( int i = 0 ; i < elements ; i++ )
            {
                if ( ( rc = jsonUtil_get_array_idx ( msg_ptr, MTCALARM_REQ_LABEL, i, alarm_req ) ) == PASS )
                {
                    if (( rc = jsonUtil_get_key_val ( (char*)alarm_req.data(), MTCALARM_REQ_KEY__ALARMID, alarmid )) != PASS )
                    {
                     elog ("%s '%s'\n", PARSE_FAILURE, MTCALARM_REQ_KEY__ALARMID);
                    }
                    else if (( rc = jsonUtil_get_key_val ( (char*)alarm_req.data(), MTCALARM_REQ_KEY__HOSTNAME, hostname )) != PASS )
                    {
                       elog ("%s '%s'\n", PARSE_FAILURE, MTCALARM_REQ_KEY__HOSTNAME);
                    }
                    else if (( rc = jsonUtil_get_key_val ( (char*)alarm_req.data(), MTCALARM_REQ_KEY__OPERATION, operation )) != PASS )
                    {
                       elog ("%s '%s'\n", PARSE_FAILURE, MTCALARM_REQ_KEY__OPERATION);
                    }
                    else if (( rc = jsonUtil_get_key_val ( (char*)alarm_req.data(), MTCALARM_REQ_KEY__SEVERITY, severity)) != PASS )
                    {
                       elog ("%s '%s'\n", PARSE_FAILURE, MTCALARM_REQ_KEY__SEVERITY);
                    }
                    else if (( rc = jsonUtil_get_key_val ( (char*)alarm_req.data(), MTCALARM_REQ_KEY__ENTITY, entity )) != PASS )
                    {
                       elog ("%s '%s'\n", PARSE_FAILURE, MTCALARM_REQ_KEY__ENTITY);
                    }
                    else if (( rc = jsonUtil_get_key_val ( (char*)alarm_req.data(), MTCALARM_REQ_KEY__PREFIX, prefix)) != PASS )
                    {
                       elog ("%s '%s'\n", PARSE_FAILURE, MTCALARM_REQ_KEY__PREFIX);
                    }
                    else
                    {
                        jlog ("Alarm Message has %d requests\n", elements );
                        rc = alarmMgr_manage_alarm ( alarmid,
                                                     hostname,
                                                     tolowercase(operation),
                                                     tolowercase(severity),
                                                     entity,
                                                     prefix);
                    }
                    if ( rc ) break ;
                }
                else
                {
                    wlog ("failed to get index '%d of %d' from alarm request", i, elements);
                }
            } /* for loop */
        }
        else
        {
            elog ("failed to find '%s' label in json object\n", MTCALARM_REQ_LABEL );
            elog (" ... %s\n", msg_ptr );
            rc = FAIL_JSON_OBJECT ;
        }
    }
    else
    {
       elog ("failed to parse json request\n");
       elog (" ... %s\n", msg_ptr );
       rc = FAIL_JSON_OBJECT ;
    }
    if (raw_obj) json_object_put(raw_obj);
    return (rc);
}
