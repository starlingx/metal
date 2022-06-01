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

/**************************************************************************
 *
 * Name       : secretUtil_manage_secret
 *
 * Description: This FSM serves to fetch a secret from barbican with error
 *              handling.
 *
 * The FSM uses a key value pair map list of type barbicanSecret_type
 * defined in httpUtil.h
 *
 * typedef struct
 * {
 *     mtc_secretStages_enum stage    ;
 *     string                reference;
 *     string                payload  ;
 * } barbicanSecret_type;
 *
 * The fsm declares a c++ map of this type which serves as
 * a key:value pair list keyed by host uuid string using the
 * secretUtil_find_secret utility. */

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

/* The FSM uses mtc_secretStages_enum also defined in httpUtil.h
 *
 *   MTC_SECRET__START ....... issue request to barbican for the list of
 *                             secrets for this host_uuid.
 *
 *   MTC_SECRET__GET_REF ..... wait for barbican's response with the
 *                             reference uuid
 *
 *   The secret_handler will store the secret reference uuid and change
 *   state to MTC_SECRET__GET_PWD or MTC_SECRET__GET_REF_FAIL if the
 *   request fails or there are no secrets available.
 *
 *   MTC_SECRET__GET_PWD ..... issue a secret read request using the
 *                             secret reference uuid
 *
 *   MTC_SECRET__GET_PWD_REC . wait for barbican's response with the
 *                             secret string and extract it once recv'ed
 *
 *   The secret_handler will store the secret payload and change state
 *   to MTC_SECRET__GET_PWD_RECV or MTC_SECRET__GET_PWD_FAIL if the
 *   request to provide a secret payload failed or was empty.
 *
 * The secretUtil_get_secret and secretUtil_read_secret http requests
 * are non-blocking.
 *
 * Parameters:
 *
 * event  .......... reference to an http libevent event
 * hostname ........ the host's name
 * host_uuid ....... the host's uuid
 * secret_timer .... a maintenance timer for request timeout detection
 * secret_handler .. pointer
 *
 * Updates   : secretList host_uuid key value is updated with reference
 *             and payload ; the secret.
 *
 * Returns   : execution status
 *
 ***************************************************************************/

barbicanSecret_type * secretUtil_manage_secret ( libEvent & event,
                                                 string & hostname,
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
         secret.payload.clear();
         secret.reference.clear();
         it = secretList.insert( std::pair<string, barbicanSecret_type>( host_uuid, secret ) ).first;
    }

    if ( event.active == true )
    {
        if (( it->second.stage != MTC_SECRET__GET_REF ) &&
            ( it->second.stage != MTC_SECRET__GET_PWD ))
        {
            slog ("%s event is active while in the wrong stage (%d)", hostname.c_str(), it->second.stage );
            event.active = false ; /* correct the error */
            return & it->second ;
        }
    }

    switch ( it->second.stage )
    {
        case MTC_SECRET__START:
        {
            if ( mtcTimer_expired ( secret_timer ) )
            {
                ilog ("%s query bmc password", hostname.c_str());

                it->second.reference.clear();
                it->second.payload.clear();

                rc = secretUtil_get_secret ( event, hostname, host_uuid );
                if (rc)
                {
                    elog ("%s get secret request failed (%d)", hostname.c_str(), rc );
                    it->second.stage = MTC_SECRET__GET_REF_FAIL;
                }
                else
                {
                    it->second.stage = MTC_SECRET__GET_REF;
                    mtcTimer_start ( secret_timer, handler, SECRET_REPLY_DELAY );
                }
            }
            break ;
        }
        case MTC_SECRET__GET_REF:
        {
            if ( mtcTimer_expired ( secret_timer ) == false )
            {
                if ( event.active == true )
                {
                    /* Look for the response */
                    if ( event.base )
                    {
                        dlog ( "%s calling event_base_loop \n", hostname.c_str() );
                        event_base_loop ( event.base, EVLOOP_NONBLOCK );
                    }
                    else
                    {
                        /* should not get here. event active while base is null
                         *    try and recover from this error case. */
                        slog ("%s active with null base", hostname.c_str());
                        event.active = false ;
                    }
                }
            }
            else
            {
                elog ( "%s timeout waiting for secret reference \n", hostname.c_str() );
                it->second.stage = MTC_SECRET__GET_REF_FAIL ;
            }
            break ;
        }
        case MTC_SECRET__GET_PWD:
        {
            if ( mtcTimer_expired ( secret_timer ) == false )
            {
                if ( event.active == true )
                {
                    /* Look for the response */
                    if ( event.base )
                    {
                        dlog ( "%s calling event_base_loop \n", hostname.c_str() );
                        event_base_loop( event.base, EVLOOP_NONBLOCK );
                    }
                    else
                    {
                        /* should not get here. event active while base is null
                         *    try and recover from this error case. */
                        slog ("%s active with null base", hostname.c_str() );
                        event.active = false ;
                    }
                }
            }
            else
            {
                elog ( "%s timeout waiting for secret payload \n", hostname.c_str() );
                it->second.stage = MTC_SECRET__GET_PWD_FAIL ;
            }
            break ;
        }

        case MTC_SECRET__GET_REF_RECV:
        {
            mtcTimer_reset( secret_timer );
            httpUtil_free_conn ( event );
            httpUtil_free_base ( event );

            rc = secretUtil_read_secret ( event, hostname, host_uuid );
            if (rc)
            {
                wlog ( "%s call to secretUtil_read_secret failed \n", hostname.c_str() );
                it->second.stage = MTC_SECRET__GET_PWD_FAIL;
            }
            else
            {
                dlog ("%s waiting on secret password ; timeout in %d secs\n", hostname.c_str(), SECRET_REPLY_DELAY);
                it->second.stage = MTC_SECRET__GET_PWD ;
                mtcTimer_start ( secret_timer, handler, SECRET_REPLY_DELAY );
            }
            break ;
        }

        case MTC_SECRET__GET_REF_FAIL:
        case MTC_SECRET__GET_PWD_FAIL:
        {
            if ( it->second.stage == MTC_SECRET__GET_REF_FAIL )
            {
                wlog ( "%s failed to get secret reference \n", hostname.c_str() );
            }
            else
            {
                wlog ( "%s failed to get secret payload \n", hostname.c_str() );
            }
            it->second.stage = MTC_SECRET__START ;
            mtcTimer_reset ( secret_timer );
            mtcTimer_start ( secret_timer, handler, SECRET_RETRY_DELAY );
            httpUtil_free_conn ( event );
            httpUtil_free_base ( event );
            break ;
        }

        default:
        {
            it->second.stage = MTC_SECRET__START ;
        }
    } // switch

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

int secretUtil_get_secret ( libEvent & event,
                            string & hostname,
                            string & host_uuid )
{
    dlog ("%s get secret", hostname.c_str());
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
        elog ("%s failed to find secret record (%s)\n",
                  hostname.c_str(),
                  host_uuid.c_str());
        return FAIL;
    }

    event.hostname    = hostname ;
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

    hlog ("%s secretUtil_get_secret %s\n",
              hostname.c_str(), event.token.url.c_str() );

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

int secretUtil_read_secret ( libEvent & event,
                             string & hostname,
                             string & host_uuid )
{
    dlog ("%s read secret", hostname.c_str());
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

    event.hostname    = hostname ;
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

    hlog ("%s secretUtil_read_secret %s",
              hostname.c_str(), event.token.url.c_str() );

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

    /* handler called */
    event.active = false ;

    std::map<string, barbicanSecret_type>::iterator it;
    it = secretList.find( event.uuid );
    if ( it == secretList.end() )
    {
        return 0 ;
    }

    if ( event.request == BARBICAN_GET_SECRET )
    {
        if ( event.status )
        {
            it->second.stage = MTC_SECRET__GET_REF_FAIL;
            elog ("%s failed to get secret ; %d\n", hn.c_str(), event.status) ;
            return 0 ;
        }
        int rc = jsonUtil_secret_load ( event.uuid,
                             (char*)event.response.data(),
                                    json_info );
        if ( rc != PASS )
        {
            it->second.stage = MTC_SECRET__GET_REF_FAIL;
            elog ("%s failed to parse secret response : %s (%d:%d)\n",
                      hn.c_str(),
                      event.response.c_str(),
                      rc, event.status );
        }
        else
        {
            size_t pos = json_info.secret_ref.find_last_of( '/' );
            it->second.reference = json_info.secret_ref.substr( pos+1 );

            if ( it->second.reference.empty() )
            {
                it->second.stage = MTC_SECRET__GET_REF_FAIL;
                elog ("%s no barbican secret reference found : %s (%d)\n",
                          hn.c_str(),
                          event.response.c_str(),
                          FAIL_OPERATION ) ;
            }
            else
            {
                it->second.stage = MTC_SECRET__GET_REF_RECV;
                dlog ("%s barbican secret reference found\n", hn.c_str() ) ;
            }
        }
    }
    else if ( event.request == BARBICAN_READ_SECRET )
    {
        if ( event.status == HTTP_NOTFOUND )
        {
            it->second.stage = MTC_SECRET__GET_PWD_FAIL;
            elog ("%s no barbican secret payload found : %s (rc:%d:%d)\n",
                      hn.c_str(),
                      event.response.c_str(),
                      event.status, FAIL_NOT_FOUND );
        }
        else if ( event.status != PASS )
        {
            it->second.stage = MTC_SECRET__GET_PWD_FAIL;
            elog ("%s failed to read secret : %s (rc:%d)\n",
                      hn.c_str(),
                      event.response.c_str(),
                      event.status );
        }
        else if ( event.response.empty() )
        {
            it->second.stage = MTC_SECRET__GET_PWD_FAIL;
            wlog ("%s secret payload is empty (rc:%d)\n",
                      hn.c_str(),
                      FAIL_OPERATION ) ;
        }
        else
        {
            it->second.stage = MTC_SECRET__GET_PWD_RECV;
            it->second.payload = event.response;
            dlog ("%s secret payload : %s\n", hn.c_str(), event.response.c_str() );
        }
    }
    else
    {
        slog ("%s unsupported secret request (rc:%d)\n", hn.c_str(), FAIL_BAD_CASE );
    }

    return ( 0 ) ;
}
