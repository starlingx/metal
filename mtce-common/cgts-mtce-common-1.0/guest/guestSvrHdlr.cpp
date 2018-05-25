/*
 * Copyright (c) 2015-2018 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
/****************************************************************************
 * @file
 * Wind River CGTS Platform Guest Services "Handlers" Implementation
 *
 * Description: This file contains the following FSM handlers, 
  
 * Interfaces:
 *
 *  guestInstClass::timer_handler
 *  guestInstClass::monitor_handler
 *  guestInstClass::connect_handler
 *
 ****************************************************************************/

#include <json-c/json.h>
#include "nodeBase.h"
#include "nodeUtil.h"        /* for ... clean_bm_response_files  */
#include "nodeTimers.h"      /* for ... mtcTimer_start/stop      */
#include "jsonUtil.h"        /* for ... jsonApi_array_value      */
#include "daemon_common.h"

#include "guestBase.h"       /* for ... */
#include "guestUtil.h"       /* for ... guestUtil_print_instance */
#include "guestSvrUtil.h"    /* for ... hb_get_message_type_name */
#include "guestVirtio.h"     /* for ... */
#include "guestSvrMsg.h"     /* for ... */
#include "guestInstClass.h"  /* for ... */

static int failure_reporting_count = 0 ;

void voteStateChange ( instInfo * instInfo_ptr , hb_state_t newState )
{
    if ( instInfo_ptr->vnState == newState )
        return ;

    clog ("%s '%s' -> '%s'\n", 
              log_prefix(instInfo_ptr).c_str(),
              hb_get_state_name(instInfo_ptr->vnState),
              hb_get_state_name(newState));

    instInfo_ptr->vnState = newState ;
}

void beatStateChange ( instInfo * instInfo_ptr , hb_state_t newState )
{
    if ( instInfo_ptr->hbState == newState )
        return ;

    if ((( instInfo_ptr->hbState == hbs_server_waiting_challenge ) && 
          ( newState == hbs_server_waiting_response )) ||
         (( instInfo_ptr->hbState == hbs_server_waiting_response ) && 
          ( newState == hbs_server_waiting_challenge )))
    {
         ; /* don't print heartbeat state changes */
    }
    else if (( newState == hbs_server_waiting_init ) && 
             ( instInfo_ptr->hbState != hbs_server_waiting_init ))
    {
         ilog ("%s waiting for init ... \n", log_prefix(instInfo_ptr).c_str());
    }
    else
    {
        clog ("%s '%s' -> '%s'\n", 
                  log_prefix(instInfo_ptr).c_str(),
                  hb_get_state_name(instInfo_ptr->hbState),
                  hb_get_state_name(newState));
    }
    instInfo_ptr->hbState = newState ;
}

void hbStatusChange ( instInfo * instInfo_ptr, bool status )
{
    if ( instInfo_ptr->heartbeating != status )
    {
        instInfo_ptr->heartbeating = status ;
        string payload = guestUtil_set_inst_info ( get_ctrl_ptr()->hostname , instInfo_ptr );

        if ( status == true )
        {
            ilog ("%s is now heartbeating\n", log_prefix(instInfo_ptr).c_str());
            send_to_guestAgent ( MTC_EVENT_HEARTBEAT_RUNNING, payload.data());
        }
        else
        {
            ilog ("%s is not heartbeating\n", log_prefix(instInfo_ptr).c_str());
            send_to_guestAgent ( MTC_EVENT_HEARTBEAT_STOPPED, payload.data());
        }

        jlog ("%s Heartbeating State Change: %s\n", log_prefix(instInfo_ptr).c_str(), payload.c_str());
    }
    else
    {
        clog ("%s heartbeating is still %s\n", 
                  log_prefix(instInfo_ptr).c_str(), status ? "enabled" : "disabled" );
    }
}


void manage_heartbeat_failure ( instInfo * instInfo_ptr )
{
    instInfo_ptr->heartbeat.failed = true ;

    dlog ("%s calling hbStatusChange false\n", log_prefix(instInfo_ptr).c_str());

    hbStatusChange ( instInfo_ptr, false) ; /* heartbeating is now false */

    beatStateChange ( instInfo_ptr, hbs_server_waiting_init ) ;
}

/* Looks up the timer ID and asserts the corresponding node's ringer */
void guestInstClass::timer_handler ( int sig, siginfo_t *si, void *uc)
{
    struct guestInstClass::inst * inst_ptr ;
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;
    
    ctrl_type * ctrl_ptr = get_ctrl_ptr();

    /* Avoid compiler errors/warnings for parms we must
     * have but currently do nothing with */
    sig=sig ; uc = uc ;

    if ( !(*tid_ptr) )
    {
        return ;
    }
    
    else if ( *tid_ptr == search_timer.tid )
    {
        mtcTimer_stop_int_safe ( search_timer );
        search_timer.ring = true ;
        return ;
    }
    else if ( *tid_ptr == ctrl_ptr->timer.tid )
    {
        mtcTimer_stop_int_safe ( ctrl_ptr->timer );
        ctrl_ptr->timer.ring = true ;
        return ;
    }

    for ( int timer_id = INST_TIMER_MONITOR ; timer_id < INST_TIMER_MAX ; timer_id++ )
    {
        if ( ( inst_ptr = guestInstClass::getInst_timer ( *tid_ptr , timer_id ) ) != NULL )
        {
            switch ( timer_id )
            {
                case INST_TIMER_MONITOR:
                {
                    if (( *tid_ptr == inst_ptr->monitor_timer.tid ) )
                    {
                        mtcTimer_stop_int_safe ( inst_ptr->monitor_timer );
                        inst_ptr->monitor_timer.ring = true ;
                        return ;
                    }
                    break ;
                }
                case INST_TIMER_CONNECT:
                {
                    if (( *tid_ptr == inst_ptr->connect_timer.tid ) )
                    {
                        mtcTimer_stop_int_safe ( inst_ptr->connect_timer );
                        inst_ptr->connect_timer.ring = true ;
                        return ;
                    }
                    break ;
                }
                case INST_TIMER_RECONNECT:
                {
                    if (( *tid_ptr == inst_ptr->reconnect_timer.tid ) )
                    {
                        mtcTimer_stop_int_safe  ( inst_ptr->reconnect_timer );
                        inst_ptr->reconnect_timer.ring = true ;

                        return ;
                    }
                    break ;
                }
                case INST_TIMER_INIT:
                {
                    if (( *tid_ptr == inst_ptr->init_timer.tid ) )
                    {
                        beatStateChange ( &inst_ptr->instance, hbs_server_waiting_init ) ;
                        mtcTimer_stop_int_safe ( inst_ptr->init_timer );
                        return ;
                    }
                    break ;
                }
                case INST_TIMER_VOTE:
                {
                    if (( *tid_ptr == inst_ptr->vote_timer.tid ) )
                    {
                        mtcTimer_stop_int_safe ( inst_ptr->vote_timer );
                        inst_ptr->vote_timer.ring = true ;
                        return ;
                    }
                    break ;
                }
                default:
                {
                    // slog ("unknown timer id (%d)\n", timer_id);
                }
            } /* end switch */
        } /* end if */
    } /* end for */
}

/* guest services timer object wrapper 
 * - does a instance lookup and calls the timer handler */
void guestTimer_handler ( int sig, siginfo_t *si, void *uc)
{
    get_instInv_ptr()->timer_handler ( sig, si, uc );
}

void guestInstClass::start_monitor_timer ( struct guestInstClass::inst * inst_ptr )
{
    if ( inst_ptr->monitor_timer.tid )
        mtcTimer_stop ( inst_ptr->monitor_timer );

    mtcTimer_start_sec_msec ( &inst_ptr->monitor_timer,
                               guestTimer_handler,
                               (inst_ptr->instance.heartbeat_interval_ms/1000), 
                               (inst_ptr->instance.heartbeat_interval_ms%1000));
}

void _schedule_init_timer ( string event_type , struct mtc_timer & timer )
{
    if (( !event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_SUSPEND) ) ||
        ( !event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_LIVE_MIGRATE_BEGIN) ) ||
        ( !event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_COLD_MIGRATE_BEGIN) ) ||
        ( !event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_REBOOT)))
    {
        if ( timer.tid )
            mtcTimer_stop ( timer );
        mtcTimer_start ( timer, guestTimer_handler, WAIT_FOR_INIT_TIMEOUT );
        ilog ("scheduling waiting_init transition in %d seconds\n", WAIT_FOR_INIT_TIMEOUT );
    }
}

/* extend the reconnect time as the attempts pile up. max out at 1 minute. */
void manage_reconnect_timeout ( instInfo * instInfo_ptr )
{
   /* extend the reconnect time as the attempts pile up. max out at 1 minute. */
   if ( (instInfo_ptr->connect_wait_in_secs*2) > MTC_MINS_1 )
       instInfo_ptr->connect_wait_in_secs = MTC_MINS_1 ;
   else
       instInfo_ptr->connect_wait_in_secs *= 2 ;
}

int connect_count = 0 ;
int guestInstClass::connect_handler ( struct guestInstClass::inst * inst_ptr )
{
    int rc = PASS ;
    switch ( inst_ptr->connectStage )
    {
        case INST_CONNECT__START:
        {
            if ( inst_ptr->instance.connected == true )
            {
                inst_ptr->connectStage = INST_CONNECT__START ;
                inst_ptr->action       = FSM_ACTION__NONE ;

                if (inst_ptr->connect_timer.tid)
                    mtcTimer_stop ( inst_ptr->connect_timer );
            }
            else
            {
                ilog ("%s connect attempt in %d seconds\n", 
                          log_prefix(&inst_ptr->instance).c_str(), inst_ptr->instance.connect_wait_in_secs);
                inst_ptr->instance.connecting = true ;
                mtcTimer_start ( inst_ptr->connect_timer, guestTimer_handler, inst_ptr->instance.connect_wait_in_secs );
                inst_ptr->connectStage = INST_CONNECT__WAIT ;
            }
            break ;
        }
        case INST_CONNECT__WAIT:
        {
            if ( inst_ptr->instance.connecting != true )
            {
                slog ("%s bad connect wait state ; auto correcting\n", 
                          log_prefix(&inst_ptr->instance).c_str());

                inst_ptr->connectStage = INST_CONNECT__START ;
                inst_ptr->action = FSM_ACTION__NONE ;
            }
            
            else if ( inst_ptr->connect_timer.ring == true )
            {
                char buf[PATH_MAX];
                
                inst_ptr->connect_timer.ring = false ;
            
                /* if the socket is not there then don't try and connect to it */
                snprintf(buf, sizeof(buf), "%s/cgcs.heartbeat.%s.sock", QEMU_CHANNEL_DIR, inst_ptr->instance.uuid.data());
                if ( daemon_is_file_present ( buf ) )
                {
                    /* Try to connect with virtio_channel_connect ...
                     * If that succeeds then go DONE.
                     * if that fails with a ENOENT hen that means the socket fd is gone do close and delete instance
                     * otherwise retry the connect
                     */
                    
                    ilog ( "%s connect start\n", log_prefix(&inst_ptr->instance).c_str());
                    rc = virtio_channel_connect ( &inst_ptr->instance );
                    if ( rc == PASS )
                    {
                        inst_ptr->connectStage = INST_CONNECT__DONE ;
                        break ;
                    }
                    /* Abort connect if the instance channel is no longer there.
                     * -1 and errno=2 : No such file or directory) */
                    else if (( rc == -1 ) && ( errno == ENOENT ))
                    {
                        ilog ("%s channel gone\n", log_prefix(&inst_ptr->instance).c_str() );
                        del_inst ( inst_ptr->instance.uuid );
                        return (RETRY);
                    }
                    else
                    {
                        wlog ("%s channel connect failed\n", 
                                  log_prefix(&inst_ptr->instance).c_str() );
                        manage_reconnect_timeout ( &inst_ptr->instance );
                    }
                }
                else
                {
                    ilog ("%s does not exist\n", buf );
                    manage_reconnect_timeout ( &inst_ptr->instance );
                }
                inst_ptr->connectStage = INST_CONNECT__START ;
            }
            break ;
        }
        case INST_CONNECT__DONE:
        {

            inst_ptr->connectStage = INST_CONNECT__START ;
            inst_ptr->action = FSM_ACTION__NONE ;

            inst_ptr->instance.connecting = false ;
            inst_ptr->instance.connected = true ;

            failure_reporting_count = 0 ;

            /* no longer failed */
            inst_ptr->instance.heartbeat.failed = false ;
            inst_ptr->instance.heartbeat.b2b_misses = 0 ;

            /* waiting for init message */
            beatStateChange ( &inst_ptr->instance, hbs_server_waiting_init ) ;

            /* default back to the start 2 second reconnect time default */
            inst_ptr->instance.connect_wait_in_secs = DEFAULT_CONNECT_WAIT ;

            start_monitor_timer ( inst_ptr );

            if ( inst_ptr->reconnect_timer.tid )
                mtcTimer_stop  ( inst_ptr->reconnect_timer );
            mtcTimer_start ( inst_ptr->reconnect_timer, guestTimer_handler, HEARTBEAT_START_TIMEOUT );

            ilog ("%s connect done\n", log_prefix(&inst_ptr->instance).c_str());

            break ;
        }
        default:
        {
            slog ("Unsupported connect stage (%d) ... correcting\n", inst_ptr->connectStage );
            inst_ptr->connectStage = INST_CONNECT__START ;
        }
    }
    return(rc);
}

int guestInstClass::monitor_handler ( struct guestInstClass::inst * inst_ptr )
{
    int rc = PASS ;

#ifdef WANT_THIS
    clog ("%s in '%s:%s' state - stage %d - R:%c F:%c H:%c\n", 
                                           log_prefix(&inst_ptr->instance).c_str(),
                         hb_get_state_name(inst_ptr->instance.hbState),
                         hb_get_state_name(inst_ptr->instance.vnState),
                                           inst_ptr->monitorStage,
                                           inst_ptr->instance.heartbeat.reporting ? 'Y' : 'n',
                                           inst_ptr->instance.heartbeat.failed ? 'Y' : 'n',
                                           inst_ptr->instance.heartbeating ? 'Y' : 'n');
                                           // inst_ptr->instance.heartbeat.waiting ? 'Y' : 'n');
#endif

    switch ( inst_ptr->monitorStage )
    {
        case INST_MONITOR__STEADY:
        {
            /* Manage Reconnect Timer */
            if ( inst_ptr->reconnect_timer.ring == true ) 
            {
                inst_ptr->reconnect_timer.ring = false ;
                if (( inst_ptr->instance.heartbeating == false ) &&
                    ( inst_ptr->instance.connecting   == false ))
                {
                    /* If this timer rings and heartbeating is not started
                     * then we need to close the connection and repoen it 
                     * Since the re-open is automatic all we need to do is
                     * close it here */
                    wlog ("%s issuing auto-reconnect ; no heartbeating\n",
                              log_prefix(&inst_ptr->instance).c_str() );

                    reconnect_start ( inst_ptr->instance.uuid.data() );
                }
                mtcTimer_start ( inst_ptr->reconnect_timer, guestTimer_handler, HEARTBEAT_START_TIMEOUT );
            }

            /* Manage Monitor Timer - expires in 3 cases
             * 1. heartbeat miss - hbs_server_waiting_response
             * 2. heartbeat done - hbs_server_waiting_challenge - interval is done and ready for the next one 
             * 3. heartbeat none - not heartbeating ; waiting for init
             * 4. heratbeat fail - in wrong state
             **/
            if ( inst_ptr->monitor_timer.ring == true )
            {
                inst_ptr->monitor_timer.ring = false ;

                /* Case 1: heartbeat miss while waiting for heartbeat response */
                if ( inst_ptr->instance.hbState == hbs_server_waiting_response )
                {
                    int threshold = daemon_get_cfg_ptr()->hbs_failure_threshold ;
                    if (( inst_ptr->instance.heartbeat.failed == true ) || 
                        ( inst_ptr->instance.heartbeat.reporting == false ))
                    {
                        hbStatusChange  ( &inst_ptr->instance, false );
                        beatStateChange ( &inst_ptr->instance, hbs_server_waiting_init) ;
                    }
                    else if ( ++inst_ptr->instance.heartbeat.b2b_misses > threshold ) 
                    {
                        inst_ptr->instance.message_count = 0 ;
                        inst_ptr->instance.heartbeat.b2b_misses = 0 ;

                        elog ("%s *** Heartbeat Loss *** (Timeout=%d msec)\n", 
                                  log_prefix(&inst_ptr->instance).c_str(), 
                                  inst_ptr->instance.heartbeat_interval_ms );

                        manage_heartbeat_failure ( &inst_ptr->instance );
                        inst_ptr->monitorStage = INST_MONITOR__FAILURE ;
                    }
                    else
                    {
                        wlog ("%s *** Heartbeat Miss *** %d of %d (Timeout=%d msec)\n", 
                                  log_prefix(&inst_ptr->instance).c_str(), 
                                  inst_ptr->instance.heartbeat.b2b_misses,
                                  threshold,
                                  inst_ptr->instance.heartbeat_interval_ms );
                        /* Send another challenge */
                        send_challenge ( inst_ptr ) ;
                   }
                }

                /* Case 2: Heartbeat done and the interval is expired.
                 *         Just start another challenge request
                 */
                else if (( inst_ptr->instance.hbState != hbs_server_waiting_init ) && 
                         ( inst_ptr->instance.hbState != hbs_server_waiting_response) && 
                         ( inst_ptr->instance.heartbeat.waiting == false ))
                {
                    // printf ("*");
                    /* Send another challenge */
                    inst_ptr->instance.heartbeat.b2b_misses = 0 ;
                    send_challenge ( inst_ptr ) ;
                }

                /* Case 3: The monitor timer still runs while we are in the 
                 *         waiting for init state so just make sure we are
                 *         handling init stuff 
                 */
                else if ( inst_ptr->instance.hbState == hbs_server_waiting_init )
                {
                    clog ("%s is %s\n", log_prefix(&inst_ptr->instance).c_str(), 
                                  hb_get_state_name(inst_ptr->instance.hbState));
                    inst_ptr->messageStage = INST_MESSAGE__RECEIVE ;
                    inst_ptr->instance.message_count = 0 ;
                    inst_ptr->instance.heartbeat.b2b_misses = 0 ;
                }

                /* Case 4: Heratbeat has failed while we are in the wrong state */
                else
                {
                    int threshold = daemon_get_cfg_ptr()->hbs_failure_threshold ;
                    if ( inst_ptr->instance.heartbeat.failed == true ) 
                    {   
                        ; /* nothing to do while failed */
                    }
                    else if ( inst_ptr->instance.heartbeat.reporting == false )
                    {
                        /* Send a challenge to keep the heartbeat going */
                        send_challenge ( inst_ptr ) ;
                    }
                    else if ( ++inst_ptr->instance.heartbeat.b2b_misses > threshold ) 
                    {
                        inst_ptr->instance.message_count = 0 ;
                        inst_ptr->instance.heartbeat.b2b_misses = 0 ;
                
                        elog ("%s *** Heartbeat Loss *** (state:%s)\n", 
                                  log_prefix(&inst_ptr->instance).c_str(),
                                  hb_get_state_name(inst_ptr->instance.hbState));

                        manage_heartbeat_failure ( &inst_ptr->instance );
                        inst_ptr->monitorStage = INST_MONITOR__FAILURE ;
                    }
                    else
                    {
                        wlog ("%s *** Heartbeat Miss *** (state:%s)\n", 
                                  log_prefix(&inst_ptr->instance).c_str(),
                                  hb_get_state_name(inst_ptr->instance.hbState));
                        /* Send another challenge */
                        send_challenge ( inst_ptr ) ;
                    }
                }
            }

            if ( inst_ptr->vote_timer.ring == true )
            {
                if ( inst_ptr->instance.vnState == hbs_client_waiting_shutdown_response )
                {
                    // handle time out as silent agreement to accept
                    if ( !inst_ptr->instance.msg_type.compare(GUEST_HEARTBEAT_MSG_ACTION_NOTIFY) ||
                         !inst_ptr->instance.msg_type.compare(GUEST_HEARTBEAT_MSG_ACTION_RESPONSE)  )
                    {
                        ilog ("%s response time out on '%s' message ; proceeding with action\n",
                                log_prefix(&inst_ptr->instance).c_str(), 
                                inst_ptr->instance.msg_type.c_str());

                        string reject_reason = "";
                        string vote_result = GUEST_HEARTBEAT_MSG_VOTE_RESULT_UNKNOWN;
                        if (!inst_ptr->instance.notification_type.compare(GUEST_HEARTBEAT_MSG_NOTIFY_REVOCABLE))
                        {
                            vote_result = GUEST_HEARTBEAT_MSG_VOTE_RESULT_ACCEPT;
                        }
                        else if (!inst_ptr->instance.notification_type.compare(GUEST_HEARTBEAT_MSG_NOTIFY_IRREVOCABLE))
                        {
                            vote_result = GUEST_HEARTBEAT_MSG_VOTE_RESULT_COMPLETE;
                        }
                        else
                        {
                            wlog ("%s Unexpected '%s' notify timeout ; proceeding with action\n",
                                      log_prefix(&inst_ptr->instance).c_str(), 
                                      inst_ptr->instance.notification_type.c_str());
                        }
                        send_vote_notify_resp (get_ctrl_ptr()->hostname,
                                               inst_ptr->instance.uuid,
                                               inst_ptr->instance.notification_type,
                                               inst_ptr->instance.event_type,
                                               vote_result, reject_reason);
                    }

                    _schedule_init_timer ( inst_ptr->instance.event_type ,
                                           inst_ptr->init_timer) ;

                    voteStateChange ( &inst_ptr->instance, hbs_server_waiting_init );
                }
                inst_ptr->vote_timer.ring = false ;
            }
            break ;
        }
        case INST_MONITOR__DELAY:
        {
            if ( inst_ptr->monitor_timer.ring == true )
            {
                inst_ptr->monitorStage = INST_MONITOR__FAILURE ;
            }
            break ;
        }
        case INST_MONITOR__FAILURE:
        {
            if ( get_instInv_ptr()->reporting == false )
            {
                wlog_throttled (failure_reporting_count, 100, "host level reporting is disabled\n");
            }
            else if (  inst_ptr->instance.heartbeat.reporting == false )
            {
                wlog_throttled (failure_reporting_count, 100, "%s instance level reporting is disabled\n", 
                        log_prefix(&inst_ptr->instance).c_str());
            }
            else
            {
                 inst_ptr->instance.heartbeat.failures++ ;
                
                 wlog ("%s sending failure notification to guestAgent (failures:%d)\n", 
                           log_prefix(&inst_ptr->instance).c_str(),
                           inst_ptr->instance.heartbeat.failures);

                 string payload = "" ;
                 payload.append ("{\"hostname\":\"");
                 payload.append (get_ctrl_ptr()->hostname);
                 payload.append ("\",\"uuid\":\"");
                 payload.append (inst_ptr->instance.uuid);
                 payload.append ("\"}");

                 jlog1 ("%s Failure Event Payload: %s\n", 
                            log_prefix(&inst_ptr->instance).c_str(), payload.c_str());

                 send_to_guestAgent ( MTC_EVENT_HEARTBEAT_LOSS , payload.data());
                 failure_reporting_count = 0 ;
            }
            // inst_ptr->instance.heartbeat.failed = false ;
            inst_ptr->monitorStage = INST_MONITOR__STEADY ;

            break ;
        }
        default:
        {
           inst_ptr->monitorStage = INST_MONITOR__STEADY ;
           break ;
        }
    }
    
    /* This will try to reconnect failed channels */
    if (( !inst_ptr->instance.connected ) || 
        (( inst_ptr->instance.chan_fd > 0 ) && ( inst_ptr->instance.chan_ok != true )))
    {
        if ( inst_ptr->action == FSM_ACTION__NONE )
        {
            ilog ("%s enabling connect FSM\n", log_prefix(&inst_ptr->instance).c_str());

            hbStatusChange ( &inst_ptr->instance, false) ;
            
            inst_ptr->connectStage = INST_CONNECT__START ;
            inst_ptr->action = FSM_ACTION__CONNECT ;
        }
        else if (  inst_ptr->action != FSM_ACTION__CONNECT )
        {
            wlog ("%s bypassing reconnect due to existing action (%d)\n", 
                      log_prefix(&inst_ptr->instance).c_str(), 
                      inst_ptr->action);
        }
    }

    return (rc);
}


/*****************************************************************************
 *
 * Name       : message_handler
 *
 * Purpose    : Receive messages from the guest and trigger actions
 *              based on message content and type.
 *
 * Description: Only stage presently supported is INST_MESSAGE__RECEIVE
 *              for each connected socket. This FSM handler is not called
 *              unless there is a valid receive message to be handled. If
 *              for some reason there are no enqued messages then the FSM
 *              just returns having done thinting ; should not happen 
 *              through.
 *
 * Currently supported message types are.
 * 
 * GUEST_HEARTBEAT_MSG_INIT - vm heartbeat init message.
 *              > Action is to send an init_ack message to start heartbeating
 *
 * GUEST_HEARTBEAT_MSG_CHALLENGE_RESPONSE - a challenge response message
 *              > Action is to change state to 'hbs_server_waiting_challenge'
 *                and allow the heartbeat interval timer to expire in the
 *                monitor_handler which will then send another challenge
 *                request setting state back to 'hbs_server_waiting_response'
 *
 * Note: Unsupported messages are popped off the queue and discarded with
 *       an error log containing the message type.
 *
 *****************************************************************************/
int guestInstClass::message_handler ( struct guestInstClass::inst * inst_ptr )
{
    int rc = PASS ;

    switch ( inst_ptr->messageStage )
    {
        case INST_MESSAGE__RECEIVE:
        {
            /* Only process if there are messages */
            if ( inst_ptr->message_list.size() )
            {
                struct json_object *jobj_msg = inst_ptr->message_list.front();
                inst_ptr->message_list.pop_front();

                if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_VERSION, &inst_ptr->instance.version) != PASS)
                {
                    handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_VERSION, jobj_msg);
                    return FAIL;
                }
                if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_REVISION, &inst_ptr->instance.revision) != PASS)
                {
                    handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_REVISION, jobj_msg);
                    return FAIL;
                }
                if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_MSG_TYPE, &inst_ptr->instance.msg_type) != PASS)
                {
                    handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_MSG_TYPE, jobj_msg);
                    return FAIL;
                }
                if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_SEQUENCE, &inst_ptr->instance.sequence) != PASS)
                {
                    handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_SEQUENCE, jobj_msg);
                    return FAIL;
                }

                mlog1 ("%s:%s message - Seq:%x Ver:%d.%d Fd:%d\n",
                         inst_ptr->instance.uuid.c_str(),
                         inst_ptr->instance.msg_type.c_str(),
                         inst_ptr->instance.sequence ,
                         inst_ptr->instance.version, inst_ptr->instance.revision,
                         inst_ptr->instance.chan_fd);

                if ( !inst_ptr->instance.msg_type.compare(GUEST_HEARTBEAT_MSG_CHALLENGE_RESPONSE) )
                {
                    if ( inst_ptr->instance.hbState == hbs_server_waiting_response )
                    {
                        uint32_t heartbeat_response;
                        string heartbeat_health;
                        string corrective_action;
                        string log_msg;

                        inst_ptr->instance.heartbeat.waiting = false ;

                        if ( daemon_get_cfg_ptr()->debug_work )
                            printf ("-");

                        inst_ptr->heartbeat_count++ ;
                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_HEARTBEAT_RESPONSE, &heartbeat_response) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_HEARTBEAT_RESPONSE, jobj_msg);
                            return FAIL;
                        }
                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_HEARTBEAT_HEALTH, &heartbeat_health) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_HEARTBEAT_HEALTH, jobj_msg);
                            return FAIL;
                        }
                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_CORRECTIVE_ACTION, &corrective_action) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_CORRECTIVE_ACTION, jobj_msg);
                            return FAIL;
                        }
                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_LOG_MSG, &log_msg) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_LOG_MSG, jobj_msg);
                            return FAIL;
                        }

                        if ( heartbeat_response != inst_ptr->instance.heartbeat_challenge)
                        {
                            inst_ptr->instance.health_count = 0 ;
                            wlog_throttled (inst_ptr->mismatch_count, 100, "%s challenge secret mismatch (%d:%d) (throttle:100)\n",
                                      log_prefix(&inst_ptr->instance).c_str(), 
                                      inst_ptr->instance.heartbeat_challenge,
                                      heartbeat_response);
                        }
                        else if (!heartbeat_health.compare(GUEST_HEARTBEAT_MSG_HEALTHY))
                        {
                            inst_ptr->mismatch_count = 0 ;
                            inst_ptr->instance.health_count = 0 ;
                            inst_ptr->instance.corrective_action_count = 0 ;

                            mlog ("%s recv '%s'  (seq:%x) (health:%s)\n",
                                      log_prefix(&inst_ptr->instance).c_str(),
                                      inst_ptr->instance.msg_type.c_str(), inst_ptr->instance.sequence, heartbeat_health.c_str());
                        
                            /* lets wait for the period timer to expire before
                             * sending another in the monitor_handler */
                            beatStateChange ( &inst_ptr->instance, hbs_server_waiting_challenge ) ;

                            if ( inst_ptr->instance.heartbeating != true )
                            {
                                hbStatusChange ( &inst_ptr->instance, true );
                            }
                            
                            if (inst_ptr->instance.heartbeat.failed != false )
                            {
                                inst_ptr->instance.heartbeat.failed = false ;
                            }

                            ilog_throttled ( inst_ptr->instance.message_count, 1000, "%s is heartbeating ...(seq:%08x)\n",
                                             log_prefix(&inst_ptr->instance).c_str(), 
                                             inst_ptr->instance.sequence );
                        }
                        else
                        {
                            const char *msg = json_object_to_json_string_ext(jobj_msg, JSON_C_TO_STRING_PLAIN);
                            ilog ("%s received unhealthy response message: %s\n",
                                      log_prefix(&inst_ptr->instance).c_str(), msg );

                            inst_ptr->mismatch_count = 0 ;
                                                        
                            /* lets wait for the period timer to expire before
                             * sending another in the monitor_handler */
                            beatStateChange ( &inst_ptr->instance, hbs_server_waiting_challenge ) ;

                            if ( inst_ptr->instance.health_count == 0 )
                            {
                                if ( heartbeat_health.compare(GUEST_HEARTBEAT_MSG_UNHEALTHY) != 0 )
                                {
                                    wlog ("%s Invalid health reported (%s)\n",
                                              log_prefix(&inst_ptr->instance).c_str(),
                                              heartbeat_health.c_str() );
                                }

                                wlog_throttled ( inst_ptr->instance.health_count, 500, 
                                                 "%s VM Unhealthy Message:\n", 
                                                 log_prefix(&inst_ptr->instance).c_str());

                                wlog ("%s ... %s\n", log_prefix(&inst_ptr->instance).c_str(), 
                                                     log_msg.c_str() );
                            }

                            inst_ptr->instance.unhealthy_corrective_action = corrective_action;

                            if (!inst_ptr->instance.unhealthy_corrective_action.compare(GUEST_HEARTBEAT_MSG_ACTION_NONE) ||
                                !inst_ptr->instance.unhealthy_corrective_action.compare(GUEST_HEARTBEAT_MSG_ACTION_UNKNOWN))
                            {
                                wlog_throttled ( inst_ptr->instance.corrective_action_count, 500, 
                                        "%s corrective action is %s ; not reporting\n",
                                        log_prefix(&inst_ptr->instance).c_str(),
                                        inst_ptr->instance.unhealthy_corrective_action.c_str());
                            } else {
                                inst_ptr->instance.unhealthy_failure = true ;
                                string payload = guestUtil_set_inst_info ( get_ctrl_ptr()->hostname , &inst_ptr->instance );
                                inst_ptr->instance.unhealthy_failure =  false ;

                                ilog ("%s ill health notification\n", log_prefix(&inst_ptr->instance).c_str());
                                send_to_guestAgent ( MTC_EVENT_HEARTBEAT_ILLHEALTH, payload.data());
                                inst_ptr->instance.corrective_action_count = 0 ;
                            }
                        }
                    }
                    else if ( inst_ptr->instance.hbState == hbs_server_waiting_challenge )
                    {
                        wlog ("%s received late '%s' response (seq:%x)\n",
                               log_prefix(&inst_ptr->instance).c_str(),
                               inst_ptr->instance.msg_type.c_str(),
                               inst_ptr->instance.sequence);
                    }
                    else
                    {
                        dlog ("%s recv '%s' while in '%s' state (seq:%x)\n",
                               log_prefix(&inst_ptr->instance).c_str(),
                               inst_ptr->instance.msg_type.c_str(),
                               hb_get_state_name(inst_ptr->instance.hbState), 
                               inst_ptr->instance.sequence);
                    }
                }

                else if ( !inst_ptr->instance.msg_type.compare(GUEST_HEARTBEAT_MSG_INIT) )
                {
                    const char *msg = json_object_to_json_string_ext(jobj_msg, JSON_C_TO_STRING_PLAIN);
                    ilog ("%s received init message: %s\n",
                              log_prefix(&inst_ptr->instance).c_str(), msg );

                    if (inst_ptr->instance.hbState != hbs_server_waiting_init)
                    {
                        wlog("%s unexpected 'init' message ; currState: '%s' (%d)\n", 
                                log_prefix(&inst_ptr->instance).c_str(),
                                hb_get_state_name(inst_ptr->instance.hbState), 
                                inst_ptr->instance.hbState );

                        /* Allow the heartbeat challenge response message log */
                        inst_ptr->instance.message_count = 0 ;
                        beatStateChange ( &inst_ptr->instance, hbs_server_waiting_init ) ;
                    }
                    else
                    {
                        string instance_name;
                        string response;

                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_INVOCATION_ID, &inst_ptr->instance.invocation_id) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_INVOCATION_ID, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_NAME, &instance_name) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_NAME, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_CORRECTIVE_ACTION, &inst_ptr->instance.corrective_action) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_CORRECTIVE_ACTION, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_HEARTBEAT_INTERVAL_MS, &inst_ptr->instance.heartbeat_interval_ms) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_HEARTBEAT_INTERVAL_MS, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_VOTE_SECS, &inst_ptr->instance.vote_secs) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_VOTE_SECS, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_SHUTDOWN_NOTICE_SECS, &inst_ptr->instance.shutdown_notice_secs) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_SHUTDOWN_NOTICE_SECS, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_SUSPEND_NOTICE_SECS, &inst_ptr->instance.suspend_notice_secs) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_SUSPEND_NOTICE_SECS, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_RESUME_NOTICE_SECS, &inst_ptr->instance.resume_notice_secs) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_RESUME_NOTICE_SECS, jobj_msg);
                            return FAIL;
                        }

                        if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_RESTART_SECS, &inst_ptr->instance.restart_secs) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_RESTART_SECS, jobj_msg);
                            return FAIL;
                        }

                        inst_ptr->instance.name = instance_name;
                        
                        /* Override the unused 'inst' name with an abbreviated version of the instance uuid 
                         * cgcs.heartbeat.1f0bc3e3-efbe-48b8-9688-4821fc0ff83c.sock
                         *
                         * */
                        if (  inst_ptr->instance.uuid.length() >= (24+12) )
                            inst_ptr->instance.inst = inst_ptr->instance.uuid.substr(24,12);
                       
                        string name = log_prefix(&inst_ptr->instance).c_str() ;

                        ilog ("%s 'init' message ; sending 'init_ack' (ver:%d.%d)\n", 
                                  log_prefix(&inst_ptr->instance).c_str(),
                                  inst_ptr->instance.version,
                                  inst_ptr->instance.revision );

                        inst_ptr->instance.heartbeat_challenge = rand();


                        /* Set the unhealthy corrective action to unknown by default */
                        inst_ptr->instance.unhealthy_corrective_action = GUEST_HEARTBEAT_MSG_ACTION_UNKNOWN ;

                        ilog ("%s corrective_action = %s\n",
                                  log_prefix(&inst_ptr->instance).c_str(), 
                                  inst_ptr->instance.corrective_action.c_str() );

                        ilog ("%s Interval : %4d msec\n",name.c_str(), inst_ptr->instance.heartbeat_interval_ms);

                        /* auto correct an interval that is too small */
                        if ( inst_ptr->instance.heartbeat_interval_ms < (uint32_t)daemon_get_cfg_ptr()->hbs_pulse_period )
                        {
                            wlog ("%s cannot have an interval of zero seconds\n", 
                                      log_prefix(&inst_ptr->instance).c_str());

                            wlog ("%s ... auto correcting to %d msecs\n",
                                      log_prefix(&inst_ptr->instance).c_str(),
                                      daemon_get_cfg_ptr()->hbs_pulse_period);

                            inst_ptr->instance.heartbeat_interval_ms = daemon_get_cfg_ptr()->hbs_pulse_period ;
                        }

                        ilog ("%s Vote TO  : %4d secs\n",name.c_str(), inst_ptr->instance.vote_secs);
                        inst_ptr->instance.vote_to_str = time_in_secs_to_str(inst_ptr->instance.vote_secs) ;

                        ilog ("%s Shutdown : %4d secs\n", name.c_str(), inst_ptr->instance.shutdown_notice_secs);
                        inst_ptr->instance.shutdown_to_str = time_in_secs_to_str (inst_ptr->instance.shutdown_notice_secs);

                        ilog ("%s Suspend  : %4d secs\n", name.c_str(), inst_ptr->instance.suspend_notice_secs);
                        inst_ptr->instance.suspend_to_str = time_in_secs_to_str (inst_ptr->instance.suspend_notice_secs);

                        ilog ("%s Resume   : %4d secs\n", name.c_str(), inst_ptr->instance.resume_notice_secs);
                        inst_ptr->instance.resume_to_str = time_in_secs_to_str (inst_ptr->instance.resume_notice_secs);

                        ilog ("%s Restart  : %4d secs\n", name.c_str(), inst_ptr->instance.restart_secs);
                        inst_ptr->instance.restart_to_str = time_in_secs_to_str(inst_ptr->instance.restart_secs);

                        /* cancel the init timer since we already got the init */
                        if ( inst_ptr->init_timer.tid )
                            mtcTimer_stop ( inst_ptr->init_timer ) ;
 
                        /*************************************************************
                         *
                         * Send INIT ACK right away followed by the first Challenge.
                         *
                         * Cannot allow the FSM to run or we might see a 
                         * race condition with another INIT messages that come after.
                         *
                         *************************************************************/
                        response = guestSvrMsg_hdr_init(inst_ptr->instance.uuid , GUEST_HEARTBEAT_MSG_INIT_ACK);
                        response.append ("\"");
                        response.append (GUEST_HEARTBEAT_MSG_INVOCATION_ID);
                        response.append ("\":");
                        response.append (int_to_string(inst_ptr->instance.invocation_id));
                        response.append ("}\n");

                        inst_ptr->instance.message_count = 0 ;

                        /* Send message to the vm through the libvirt channel */
                        ilog("%s sending 'init_ack' invocation_id:%d, msg: %s\n", name.c_str(),
                                 inst_ptr->instance.invocation_id, response.c_str());

                        get_instInv_ptr()->write_inst (&inst_ptr->instance, response.c_str(), response.length());

                        /* Send a challenge right away */
                        beatStateChange ( &inst_ptr->instance, hbs_server_waiting_response ) ;
                        inst_ptr->instance.heartbeat.b2b_misses = 0 ;
                        inst_ptr->instance.heartbeat.failed = false ;
                        send_challenge ( inst_ptr ) ;
                        inst_ptr->messageStage = INST_MESSAGE__RECEIVE ;
                    }
                }
                else if ( !inst_ptr->instance.msg_type.compare(GUEST_HEARTBEAT_MSG_ACTION_RESPONSE) )
                {
                    uint32_t invocation_id;
                    const char *msg = json_object_to_json_string_ext(jobj_msg, JSON_C_TO_STRING_PLAIN);
                    ilog ("%s received action response message: %s\n",
                              log_prefix(&inst_ptr->instance).c_str(), msg );

                    if (jsonUtil_get_int(jobj_msg, GUEST_HEARTBEAT_MSG_INVOCATION_ID, &invocation_id) != PASS)
                    {
                        handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_INVOCATION_ID, jobj_msg);
                        return FAIL;
                    }

                    if ( invocation_id != inst_ptr->instance.invocation_id )
                    {
                        wlog ("%s invocation id mismatch (%x:%x) - dropping response\n",
                                  log_prefix(&inst_ptr->instance).c_str(),
                                  invocation_id,
                                  inst_ptr->instance.invocation_id );
                        string log_err = "Invocation id mismatch. Received: ";
                        log_err.append(int_to_string(invocation_id));
                        log_err.append(" expect: ");
                        log_err.append(int_to_string(inst_ptr->instance.invocation_id));
                        send_client_msg_nack(&inst_ptr->instance, log_err);
                    }
                    else
                    {
                        string event_type;
                        string notification_type;
                        string vote_result;
                        string reject_reason;

                        if(jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_EVENT_TYPE, &event_type) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_EVENT_TYPE, jobj_msg);
                            return FAIL;
                        }
                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_NOTIFICATION_TYPE, &notification_type) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_NOTIFICATION_TYPE, jobj_msg);
                            return FAIL;
                        }
                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_VOTE_RESULT, &vote_result) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_VOTE_RESULT, jobj_msg);
                            return FAIL;
                        }
                        if (jsonUtil_get_string(jobj_msg, GUEST_HEARTBEAT_MSG_LOG_MSG, &reject_reason) != PASS)
                        {
                            handle_parse_failure(inst_ptr, GUEST_HEARTBEAT_MSG_LOG_MSG, jobj_msg);
                            return FAIL;
                        }

                        send_vote_notify_resp (get_ctrl_ptr()->hostname,
                                               inst_ptr->instance.uuid,
                                               notification_type,
                                               event_type,
                                               vote_result,
                                               reject_reason);

                        inst_ptr->monitorStage = INST_MONITOR__STEADY ;

                        _schedule_init_timer ( event_type , inst_ptr->init_timer );
                    
                        // if pause-accept or pause-complete)
                        if (!event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_PAUSE) &&
                           (!vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_ACCEPT) ||
                            !vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_COMPLETE)) )
                        {
                            beatStateChange ( &inst_ptr->instance, hbs_server_waiting_init ) ;
                        }
                        voteStateChange ( &inst_ptr->instance, hbs_server_waiting_init ) ;

                        // cancel the vote timer
                        if ( inst_ptr->vote_timer.tid )
                            mtcTimer_stop ( inst_ptr->vote_timer );
                        inst_ptr->vote_timer.ring = false ;
                    }
                }
                else if ( !inst_ptr->instance.msg_type.compare(GUEST_HEARTBEAT_MSG_EXIT) )
                {
                    const char *msg = json_object_to_json_string_ext(jobj_msg, JSON_C_TO_STRING_PLAIN);
                    ilog ("%s received client exit request: %s\n",
                              log_prefix(&inst_ptr->instance).c_str(), msg );

                    /* Prevent a heartbeat loss in the case of a graceful exit 
                     * by moving into the waiting_init state */
                    beatStateChange ( &inst_ptr->instance, hbs_server_waiting_init ) ;

                    hbStatusChange ( &inst_ptr->instance, false );
                }
                else
                {
                    elog ("%s unsupported message type: %s.\n",
                              log_prefix(&inst_ptr->instance).c_str(), 
                              inst_ptr->instance.msg_type.c_str());

                    string log_err = "unsupported message type: ";
                    log_err.append(inst_ptr->instance.msg_type);
                    send_client_msg_nack(&inst_ptr->instance, log_err);
                }
                json_object_put(jobj_msg);
            }
            break ;
        }

        default:
        {
            elog ("Unsupported stage (%d)\n", inst_ptr->messageStage );
        }
    }
    return (rc) ;
}

/*************************************************************************************
 *
 * Name       : send_challenge
 *
 * Description: Transmit a heartbeat challenge to he specified VM 
 *              and start the timoeut timer.
 *
 **************************************************************************************/
int guestInstClass::send_challenge ( struct guestInstClass::inst * inst_ptr )
{
    size_t bytes_sent ;

    string message = guestSvrMsg_hdr_init(inst_ptr->instance.uuid , GUEST_HEARTBEAT_MSG_CHALLENGE);

    beatStateChange ( &inst_ptr->instance, hbs_server_waiting_response );

    inst_ptr->instance.heartbeat_challenge = rand();

    message.append ("\"");
    message.append (GUEST_HEARTBEAT_MSG_HEARTBEAT_CHALLENGE);
    message.append ("\":");
    message.append (int_to_string(inst_ptr->instance.heartbeat_challenge));
    message.append ("}\n");

    /* Send message to the vm through the libvirt channel */
    bytes_sent = write_inst (&inst_ptr->instance, message.c_str(), message.length());

    /* The write_inst will report an error log. 
     * This one is only to report a partial message send.
     */
    if (( bytes_sent > 0) && ( bytes_sent != message.length()))
    {
        wlog ("%s only sent %ld of %ld bytes\n", 
                  log_prefix(&inst_ptr->instance).c_str(), 
                  bytes_sent, message.length() );
    }
    
    /* Waiting on a response now */
    inst_ptr->instance.heartbeat.waiting = true ;

    start_monitor_timer ( inst_ptr ) ;
    
    if ( daemon_get_cfg_ptr()->debug_work )
        printf ("_");

    return (PASS);
}

/*************************************************************************************
 *
 * Name       : send_vote_notify
 *
 * Description: Send a voting or notification message to GuestClient  on VM
 *              and start the timeout timer.
 *
 **************************************************************************************/
int guestInstClass::send_vote_notify ( string uuid )
{
    struct guestInstClass::inst * inst_ptr = getInst(uuid);
    size_t bytes_sent ;
    uint32_t timeout_ms;

    string message = guestSvrMsg_hdr_init(inst_ptr->instance.uuid , GUEST_HEARTBEAT_MSG_ACTION_NOTIFY);

    voteStateChange ( &inst_ptr->instance, hbs_client_waiting_shutdown_response );


    if ( !inst_ptr->instance.notification_type.compare(GUEST_HEARTBEAT_MSG_NOTIFY_REVOCABLE) )
    {
        timeout_ms = inst_ptr->instance.vote_secs * 1000;
    }
    else
    {
        timeout_ms = inst_ptr->instance.vote_secs ;
        if (!inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_STOP) ||
            !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_REBOOT))
        {
            timeout_ms = inst_ptr->instance.shutdown_notice_secs * 1000 ;

        } else if (!inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_SUSPEND) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_PAUSE) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_RESIZE_BEGIN) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_LIVE_MIGRATE_BEGIN) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_COLD_MIGRATE_BEGIN) ) {
            timeout_ms = inst_ptr->instance.suspend_notice_secs * 1000 ;

        } else if (!inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_UNPAUSE) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_RESUME) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_RESIZE_END) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_LIVE_MIGRATE_END) ||
                   !inst_ptr->instance.event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_COLD_MIGRATE_END) ) {
            timeout_ms = inst_ptr->instance.resume_notice_secs * 1000 ;
        } else {
            wlog ("%s unsupported event type (%s) defaulting to 'vote' timeout of %d secs\n",
                      log_prefix(&inst_ptr->instance).c_str(),
                      inst_ptr->instance.event_type.c_str(),
                      inst_ptr->instance.vote_secs);
        }
    }

    dlog ("%s event_type:%s notification_type:%s invocation_id:%d timeout_ms:%d\n",
              log_prefix(&inst_ptr->instance).c_str(),
              inst_ptr->instance.event_type.c_str(),
              inst_ptr->instance.notification_type.c_str(),
              inst_ptr->instance.invocation_id,
              timeout_ms);

    message.append ("\"");
    message.append (GUEST_HEARTBEAT_MSG_INVOCATION_ID);
    message.append ("\":");
    message.append (int_to_string(inst_ptr->instance.invocation_id));
    message.append (",\"");
    message.append (GUEST_HEARTBEAT_MSG_EVENT_TYPE);
    message.append ("\":\"");
    message.append (inst_ptr->instance.event_type);
    message.append ("\",\"");
    message.append (GUEST_HEARTBEAT_MSG_NOTIFICATION_TYPE);
    message.append ("\":\"");
    message.append (inst_ptr->instance.notification_type);
    message.append ("\",\"");
    message.append (GUEST_HEARTBEAT_MSG_TIMEOUT_MS);
    message.append ("\":");
    message.append (int_to_string(timeout_ms));
    message.append ("}\n");

    ilog("%s send_vote_notify message=%s\n",
             log_prefix(&inst_ptr->instance).c_str(), message.c_str());

    /* Send message to the vm through the libvirt channel */
    bytes_sent = write_inst (&inst_ptr->instance, message.c_str(), message.length());
    if ( bytes_sent != message.length() )
    {
        wlog ("%s only sent %ld of %ld bytes\n", inst_ptr->instance.inst.c_str(),
                                                 bytes_sent, message.length() );
    }

    if ( inst_ptr->vote_timer.tid )
        mtcTimer_stop ( inst_ptr->vote_timer );
    mtcTimer_start ( inst_ptr->vote_timer, guestTimer_handler, inst_ptr->instance.vote_secs );
   
    dlog("%s timer started for %d seconds\n", 
             log_prefix(&inst_ptr->instance).c_str(), 
             inst_ptr->instance.vote_secs);
   
    return (PASS);
}

/*************************************************************************************
 *
 * Name       : send_vote_notify_resp
 *
 * Description: Send response for voting or notification to GuestAgent
 *
 **************************************************************************************/
int guestInstClass::send_vote_notify_resp ( char * hostname,  string uuid,
                                            string notification_type,
                                            string event_type,
                                            string vote_result,
                                            string reject_reason)
{
    instInfo * instInfo_ptr = get_inst ( uuid );

    if ( !instInfo_ptr )
    {
        elog ("%s is unknown\n", uuid.c_str());
        return FAIL;
    }

    if (!vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_ACCEPT) ||
        !vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_COMPLETE))
    {
        // accept
        ilog ("%s '%s' '%s' '%s'\n",
                  log_prefix(instInfo_ptr).c_str(),
                  notification_type.c_str(),
                  event_type.c_str(),
                  vote_result.c_str());

        if (!vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_COMPLETE) &&
            !event_type.compare(GUEST_HEARTBEAT_MSG_EVENT_SUSPEND))
        {
            instInfo_ptr->connected = false ;
            hbStatusChange ( instInfo_ptr , false );
        }
    } else if (!vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_REJECT)) {
        ilog ("%s '%s' '%s' '%s' reason: %s\n",
                  log_prefix(instInfo_ptr).c_str(),
                  notification_type.c_str(),
                  event_type.c_str(),
                  vote_result.c_str(),
                  reject_reason.c_str());
    } else if (!vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_TIMEOUT)) {
        ilog ("%s '%s' '%s' '%s'\n",
                  log_prefix(instInfo_ptr).c_str(),
                  notification_type.c_str(),
                  event_type.c_str(),
                  vote_result.c_str());
    } else if (!vote_result.compare(GUEST_HEARTBEAT_MSG_VOTE_RESULT_ERROR)) {
        elog ("%s vote to '%s' returned error: %s\n",
                  log_prefix(instInfo_ptr).c_str(),
                  event_type.c_str(),
                  vote_result.c_str());
    } else {
        elog ("%s vote to '%s' unknown vote response %s\n",
                  log_prefix(instInfo_ptr).c_str(),
                  event_type.c_str(),
                  vote_result.c_str());
    }

    string payload = "" ;
    payload.append ("{\"hostname\":\"");
    payload.append (hostname);
    payload.append ("\", \"uuid\": \"");
    payload.append (uuid.c_str());
    payload.append ("\", \"notification_type\": \"");
    payload.append (notification_type);
    payload.append ("\", \"event-type\": \"");
    payload.append (event_type);
    payload.append ("\", \"vote\": \"");
    payload.append (vote_result);
    payload.append ("\", \"reason\": \"");
    payload.append (reject_reason);
    payload.append ("\"}");

    jlog ("%s Notification Event Payload: %s\n", log_prefix(instInfo_ptr).c_str(), payload.c_str());

    send_to_guestAgent ( MTC_EVENT_VOTE_NOTIFY , payload.data());

    return (PASS);
}

/*************************************************************************************
 *
 * Name       : send_client_msg_nack
 *
 * Description: Send failure response to GuestClient when fail to process the client message
 *
 **************************************************************************************/
void guestInstClass::send_client_msg_nack ( instInfo * instInfo_ptr,
                                            string log_err)
{
    size_t bytes_sent ;

    string message = guestSvrMsg_hdr_init(instInfo_ptr->uuid , GUEST_HEARTBEAT_MSG_NACK);

    message.append ("\"");
    message.append (GUEST_HEARTBEAT_MSG_INVOCATION_ID);
    message.append ("\":");
    message.append (int_to_string(instInfo_ptr->invocation_id));
    message.append (",\"");
    message.append (GUEST_HEARTBEAT_MSG_LOG_MSG);
    message.append ("\":\"");
    message.append (log_err.c_str());
    message.append ("\"}\n");

    ilog("%s send_client_msg_nack message=%s\n",
             log_prefix(instInfo_ptr).c_str(), message.c_str());

    /* Send message to the vm through the libvirt channel */
    bytes_sent = write_inst (instInfo_ptr, message.c_str(), message.length());
    if ( bytes_sent != message.length() )
    {
        wlog ("%s only sent %ld of %ld bytes\n", instInfo_ptr->inst.c_str(),
                                                 bytes_sent, message.length() );
    }
}

/*************************************************************************************
 *
 * Name       : handle_parse_failure
 *
 * Description: Handle JSON parse failure
 *
 **************************************************************************************/
void guestInstClass::handle_parse_failure ( struct guestInstClass::inst * inst_ptr,
                                            const char *key,
                                            struct json_object *jobj_msg)
{
    string log_err = "failed to parse ";
    log_err.append(key);
    elog("%s %s\n", log_prefix(&inst_ptr->instance).c_str(), log_err.c_str());
    send_client_msg_nack(&inst_ptr->instance, log_err);
    /* pop_front() only deletes the internal copy of jobj_msg in the message_list.
       The original object still needs to be released here */
    json_object_put(jobj_msg);
}
