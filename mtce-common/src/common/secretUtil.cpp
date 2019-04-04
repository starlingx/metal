/*
 * Copyright (c) 2019 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance
  * Access to Openstack Barbican via REST API Interface.
  *
  * This file implements the a set of secret utilities that maintenance
  * calls upon to get/read Barbican secrets from the Barbican Secret storage.
  *
  * The APIs exposed from this file are
  *
  *   secretUtil_get_secret   - gets the Barbican secret, filtered by name
  *   secretUtil_read_secret  - reads the payload for a specified secret uuid
  *
  *   Each utility is paired with a private handler.
  *
  *   secretUtil_handler  - handles response for Barbican requests
  *
  * Warning: These calls cannot be nested.
  *
  **/

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "pwd"

#include <map>
#include "nodeBase.h"       /* for ... Base Service Header             */
#include "nodeUtil.h"       /* for ... Utility Service Header          */
#include "hostUtil.h"       /* for ... Host Service Header             */
#include "jsonUtil.h"       /* for ... Json utilities                  */
#include "secretUtil.h"     /* this .. module header                   */

std::map<string, barbicanSecret_type> secretList;

barbicanSecret_type * secretUtil_find_secret ( string & host_uuid )
{
    std::map<string, barbicanSecret_type>::iterator it;
    it = secretList.find( host_uuid );
    if ( it != secretList.end() )
    {
        return &it->second;
    }
    return NULL;
}

barbicanSecret_type * secretUtil_manage_secret ( libEvent & event,
                                                 string & host_uuid,
                                                 struct mtc_timer & secret_timer,
                                                 void (*handler)(int, siginfo_t*, void*))
{
    int rc = PASS;
    std::map<string, barbicanSecret_type>::iterator it;
    it = secretList.find( host_uuid );
    if ( it == secretList.end() )
    {
         barbicanSecret_type secret;
         secret.stage = MTC_SECRET__START;
         it = secretList.insert( std::pair<string, barbicanSecret_type>( host_uuid, secret ) ).first;
    }

    if ( it->second.stage == MTC_SECRET__START )
    {
        it->second.reference.clear();
        it->second.payload.clear();
    }

    if ( it->second.stage == MTC_SECRET__START ||
         it->second.stage == MTC_SECRET__GET_REF_FAIL )
    {
        if ( secret_timer.ring == true )
        {
            rc = secretUtil_get_secret ( event, host_uuid );
            if (rc)
            {
                wlog ( "%s getting secret reference failed \n", host_uuid.c_str() );
                it->second.stage = MTC_SECRET__GET_REF_FAIL;
                mtcTimer_start( secret_timer, handler, SECRET_RETRY_DELAY );
            }
            else
            {
                mtcTimer_start( secret_timer, handler, SECRET_REPLY_DELAY );
            }
        }
        else if ( event.base )
        {
            httpUtil_free_conn ( event );
            httpUtil_free_base ( event );
        }
    }
    else if ( it->second.stage == MTC_SECRET__GET_REF_RECV ||
              it->second.stage == MTC_SECRET__GET_PWD_FAIL )
    {
        if ( secret_timer.ring == true )
        {
            rc = secretUtil_read_secret ( event, host_uuid );
            if (rc)
            {
                wlog ( "%s getting secret payload failed \n", host_uuid.c_str() );
                it->second.stage = MTC_SECRET__GET_PWD_FAIL;
                mtcTimer_start( secret_timer, handler, SECRET_RETRY_DELAY );
            }
            else
            {
                mtcTimer_start( secret_timer, handler, SECRET_REPLY_DELAY );
            }
        }
        else if ( event.base )
        {
            httpUtil_free_conn ( event );
            httpUtil_free_base ( event );
        }
    }
    else if ( it->second.stage == MTC_SECRET__GET_REF ||
              it->second.stage == MTC_SECRET__GET_PWD )
    {
        if ( event.active == true )
        {
            /* Look for the response */
            if ( event.base )
            {
                event_base_loop( event.base, EVLOOP_NONBLOCK );
            }
            else
            {
                /* should not get here. event active while base is null
                 *    try and recover from this error case. */
                event.active = false ;
            }
        }
        else if ( event.base )
        {
            if ( it->second.stage == MTC_SECRET__GET_REF )
            {
                wlog ( "%s getting secret reference timeout \n", host_uuid.c_str() );
                it->second.stage = MTC_SECRET__GET_REF_FAIL ;
                mtcTimer_reset( secret_timer );
                mtcTimer_start( secret_timer, handler, SECRET_RETRY_DELAY );

            }
            if ( it->second.stage == MTC_SECRET__GET_PWD )
            {
                wlog ( "%s getting secret payload timeout \n", host_uuid.c_str() );
                it->second.stage = MTC_SECRET__GET_PWD_FAIL ;
                mtcTimer_reset( secret_timer );
                mtcTimer_start( secret_timer, handler, SECRET_RETRY_DELAY );
            }

            httpUtil_free_conn ( event );
            httpUtil_free_base ( event );
        }
    }
    return & it->second ;
}

/***********************************************************************
 *
 * Name       : secretUtil_get_secret
 *
 * Purpose    : Issue an Barbican GET request for a specified secret name
 *              to manage secret's reference.
 *
 */

int secretUtil_get_secret ( libEvent & event, string & host_uuid )
{
    httpUtil_event_init ( &event,
                          host_uuid,
                          "secretUtil_get_secret",
                          hostUtil_getServiceIp  (SERVICE_SECRET),
                          hostUtil_getServicePort(SERVICE_SECRET));

    std::map<string, barbicanSecret_type>::iterator it;
    it = secretList.find( host_uuid );
    if ( it != secretList.end() )
    {
        it->second.stage = MTC_SECRET__GET_REF;
    }
    else
    {
        elog ("%s failed to find secret record\n", host_uuid.c_str());
        return FAIL;
    }

    event.hostname    = _hn();
    event.uuid        = host_uuid;

    event.token.url = MTC_SECRET_LABEL;
    event.token.url.append(MTC_SECRET_NAME);
    event.token.url.append(host_uuid);
    event.token.url.append(MTC_SECRET_BATCH);
    event.token.url.append(MTC_SECRET_BATCH_MAX);
    event.address   = event.token.url;

    event.blocking    = false;
    event.request     = BARBICAN_GET_SECRET;
    event.operation   = "get secret reference";
    event.type        = EVHTTP_REQ_GET ;
    event.timeout     = HTTP_SECRET_TIMEOUT ;
    event.handler     = &secretUtil_handler ;

    dlog ("Path:%s\n", event.token.url.c_str() );

    return ( httpUtil_api_request ( event ) ) ;
}

/* ******************************************************************
 *
 * Name:       secretUtil_read_secret
 *
 * Purpose:    Issue an Barbican GET request for a specified secret uuid
 *             to read secret's payload, ie password itself.
 *
 *********************************************************************/

int secretUtil_read_secret ( libEvent & event, string & host_uuid )
{
    httpUtil_event_init ( &event,
                          host_uuid,
                          "secretUtil_read_secret",
                          hostUtil_getServiceIp  (SERVICE_SECRET),
                          hostUtil_getServicePort(SERVICE_SECRET));

    string bm_pw_reference;
    std::map<string, barbicanSecret_type>::iterator it;
    it = secretList.find( host_uuid );
    if ( it != secretList.end() )
    {
        bm_pw_reference = it->second.reference;
        it->second.stage = MTC_SECRET__GET_PWD;
    }
    else
    {
        elog ("%s failed to find secret record\n", host_uuid.c_str());
        return FAIL;
    }

    event.hostname    = _hn();
    event.uuid        = host_uuid;

    event.token.url = MTC_SECRET_LABEL;
    event.token.url.append("/");
    event.token.url.append(bm_pw_reference);
    event.token.url.append("/");
    event.token.url.append(MTC_SECRET_PAYLOAD);
    event.address   = event.token.url;

    event.blocking    = false;
    event.request     = BARBICAN_READ_SECRET ;
    event.operation   = "get secret payload";
    event.type        = EVHTTP_REQ_GET ;
    event.timeout     = HTTP_SECRET_TIMEOUT ;
    event.handler     = &secretUtil_handler ;

    dlog ("Path:%s\n", event.token.url.c_str() );

    return ( httpUtil_api_request ( event ) ) ;
}


/*******************************************************************
 *
 * Name       : secretUtil_handler
 *
 * Description: The handles the barbican get request
 *              responses for the following messages
 *
 *     BARBICAN_GET_SECRET,
 *     BARBICAN_READ_SECRET
 *
 *******************************************************************/

int secretUtil_handler ( libEvent & event )
{
    /* Declare and clean the json info object string containers */
    jsonUtil_secret_type json_info ;

    string hn = event.hostname ;
    int    rc = event.status   ;

    std::map<string, barbicanSecret_type>::iterator it;
    it = secretList.find( event.uuid );
    if ( it == secretList.end() )
    {
        elog ("%s failed to find secret record\n", hn.c_str());
        return ( rc ) ;
    }

    if ( event.request == BARBICAN_GET_SECRET )
    {
        if ( event.status )
        {
            elog ("%s failed to get secret - error code (%d) \n", hn.c_str(), event.status );
            it->second.stage = MTC_SECRET__GET_REF_FAIL;
            return ( rc ) ;
        }
        rc = jsonUtil_secret_load ( event.uuid,
                                    (char*)event.response.data(),
                                    json_info );
        if ( rc != PASS )
        {
            elog ( "%s failed to parse secret response (%s)\n",
                   event.hostname.c_str(),
                   event.response.c_str() );
            event.status = FAIL_JSON_PARSE ;
            it->second.stage = MTC_SECRET__GET_REF_FAIL;
        }
        else
        {
            size_t pos = json_info.secret_ref.find_last_of( '/' );
            it->second.reference = json_info.secret_ref.substr( pos+1 );
            if ( it->second.reference.empty() )
            {
                ilog ("%s no barbican secret reference found \n", hn.c_str() );
                it->second.stage = MTC_SECRET__GET_PWD_RECV;
            }
            else
            {
                ilog ("%s barbican secret reference found \n", hn.c_str() );
                it->second.stage = MTC_SECRET__GET_REF_RECV;
            }
        }
    }
    else if ( event.request == BARBICAN_READ_SECRET )
    {
        if ( event.status == HTTP_NOTFOUND )
        {
            ilog ("%s no barbican secret payload found \n", hn.c_str() );
        }
        else if ( event.status != PASS )
        {
            elog ("%s failed to read secret - error code (%d) \n", hn.c_str(), event.status );
            it->second.stage = MTC_SECRET__GET_REF_FAIL;
            return ( rc ) ;
        }

        ilog ("%s barbican secret payload found \n", hn.c_str() );
        it->second.payload = event.response;
        it->second.stage = MTC_SECRET__GET_PWD_RECV;
    }
    else
    {
        elog ("%s unsupported secret request (%d)\n", hn.c_str(), event.request );
    }
    return ( rc ) ;
}
