/*
 * Copyright (c) 2013, 2016, 2023-2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/**
  * @file
  * Wind River CGTS Platform Controller Maintenance HTTP Utilities.
  *
  * Public Interfaces: tied to the nodeLinkClass
  *
  *    nodeLinkClass::workQueue_enqueue
  *    nodeLinkClass::doneQueue_dequeue
  *
  *
  * Private Helper Utilities:
  *
  *    _get_work_state_str
  *    _get_event_log_prefix_string
  *
  *
  */

using namespace std;

#include "nodeClass.h"      /* for ... maintenance class nodeLinkClass */
#include "mtcHttpUtil.h"    /*         this module header              */
#include "mtcNodeHdlrs.h"   /* for ... mtcTimer_handl                  */
#include "nodeUtil.h"       /* for ... common Node Utilities           */

#define QUEUE_OVERLOAD (40)

string _get_work_state_str ( httpStages_enum state )
{
    if ( state == HTTP__TRANSMIT ) return ("Tx  ");
    else if ( state == HTTP__RECEIVE  ) return ("  Rx");
    else if ( state == HTTP__FAILURE  ) return (" Er ");
    else if ( state == HTTP__RECEIVE_WAIT  ) return ("Wait");
    else
    {
        elog ("Invalid Http Work Queue State: %d\n", state );
        return ("----");
    }
}


void nodeLinkClass::workQueue_dump ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->libEvent_work_fifo.size() )
    {
        syslog ( LOG_INFO, "\n");
        syslog ( LOG_INFO, "+------+-------+--------------+---------+---------------+----------------------+\n");
        syslog ( LOG_INFO, "| Mode |  Seq  | Hostname     | Service |    Request    |  IP Address     Port | Payload ...\n");
        syslog ( LOG_INFO, "+------+-------+--------------+---------+---------------+----------------------+\n");
        for ( node_ptr->libEvent_work_fifo_ptr  = node_ptr->libEvent_work_fifo.begin();
              node_ptr->libEvent_work_fifo_ptr != node_ptr->libEvent_work_fifo.end();
              node_ptr->libEvent_work_fifo_ptr ++ )
        {
            syslog ( LOG_INFO, "| %-4s | %5d | %-12s | %-7s | %-13s | %15s:%d | %s\n",
                _get_work_state_str(node_ptr->libEvent_work_fifo_ptr->state).c_str(),
                node_ptr->libEvent_work_fifo_ptr->sequence,
                node_ptr->libEvent_work_fifo_ptr->hostname.c_str(),
                node_ptr->libEvent_work_fifo_ptr->service.c_str(),
                node_ptr->libEvent_work_fifo_ptr->operation.c_str(),
                node_ptr->libEvent_work_fifo_ptr->ip.c_str(),
                node_ptr->libEvent_work_fifo_ptr->port,
                node_ptr->libEvent_work_fifo_ptr->payload.c_str());
        }
        syslog ( LOG_INFO, "+------+-------+--------------+---------+--------------+----------------------+\n");
    }
    else
    {
        dlog ("%s work queue is empty\n", node_ptr->hostname.c_str());
    }
}

void nodeLinkClass::workQueue_dump_all ( void )
{
    struct node * ptr = static_cast<struct node *>(NULL) ;

    /* check for empty list condition */
    if ( head == NULL )
    {
        syslog ( LOG_INFO, "No inventory\n");
        return ;
    }
    /* Now search the node list */
    for ( ptr = head ; ptr != NULL ; ptr = ptr->next )
    {
        workQueue_dump ( ptr );
    }
}

void nodeLinkClass::doneQueue_dump ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->libEvent_done_fifo.size() )
    {
        for ( node_ptr->libEvent_done_fifo_ptr  = node_ptr->libEvent_done_fifo.begin();
              node_ptr->libEvent_done_fifo_ptr != node_ptr->libEvent_done_fifo.end();
              node_ptr->libEvent_done_fifo_ptr ++ )
        {
            syslog ( LOG_INFO, "%15s httpReq doneQueue:%5d - %s '%s' -> Status:%d\n",
                         node_ptr->libEvent_done_fifo_ptr->hostname.c_str(),
                         node_ptr->libEvent_done_fifo_ptr->sequence,
                         node_ptr->libEvent_done_fifo_ptr->service.c_str(),
                         node_ptr->libEvent_done_fifo_ptr->operation.c_str(),
                         node_ptr->libEvent_done_fifo_ptr->status );
        }
    }
}

void nodeLinkClass::doneQueue_dump_all ( void )
{
    struct node * ptr = static_cast<struct node *>(NULL) ;

    /* check for empty list condition */
    if ( head == NULL )
    {
        syslog ( LOG_INFO, "\nNo inventory\n");
        return ;
    }
    /* Now search the node list */
    for ( ptr = head ; ptr != NULL ; ptr = ptr->next )
    {
        doneQueue_dump  ( ptr );
        doneQueue_purge ( ptr );
    }
}


/* ***********************************************************************
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
 * ************************************************************************/

int nodeLinkClass::workQueue_enqueue ( libEvent & event )
{
    char seq_str[64] ;
    memset ( &seq_str[0], 0 , 64 );

    GET_NODE_PTR(event.hostname) ;

    event.sequence = node_ptr->oper_sequence++ ;
    sprintf ( &seq_str[0], "%d", event.sequence );

    event.log_prefix = event.hostname ;
    event.log_prefix.append (" ");
    event.log_prefix.append (event.service) ;
    //event.log_prefix.append (" '");
    //event.log_prefix.append (event.operation) ;
    //event.log_prefix.append ("' seq:");
    event.log_prefix.append (" seq:");
    event.log_prefix.append (seq_str) ;

    node_ptr->libEvent_work_fifo.push_back(event);

    qlog ("%s Enqueued\n", event.log_prefix.c_str());

    return (PASS) ;
}

/* ***********************************************************************
 *
 * Name       : nodeLinkClass::doneQueue_dequeue
 *
 * Description: Searches the done queue for the event matching the supplied
 *              event reference , specifically the sequence number. If found
 *              it pulls the execution status information and then proceeds
 *              to remove it from the done queue.
 *
 * If the event is found then the event status is returned.
 * if not found then a RETRY is returned.
 * If the done event status is RETRY then a FAIL is returned since
 * it should not be on the done queue with a retry status.
 *
 * @param event is a reference to the callers libEvent.
 * @return an integer with values of PASS, FAIL, RETRY
 *
 * ************************************************************************/

int nodeLinkClass::doneQueue_dequeue ( libEvent & event )
{
    int rc = FAIL ;
    bool found = false ;
    GET_NODE_PTR(event.hostname) ;
    for ( node_ptr->libEvent_done_fifo_ptr  = node_ptr->libEvent_done_fifo.begin();
          node_ptr->libEvent_done_fifo_ptr != node_ptr->libEvent_done_fifo.end();
          node_ptr->libEvent_done_fifo_ptr++ )
    {
         if ( node_ptr->libEvent_done_fifo_ptr->sequence == event.sequence )
         {
             ilog ("%s fetched from done queue\n",
                       node_ptr->libEvent_done_fifo_ptr->log_prefix.c_str());

             /* get on the response data */
             event.http_status = node_ptr->libEvent_done_fifo_ptr->http_status ;
             event.status      = node_ptr->libEvent_done_fifo_ptr->status ;
             event.active      = node_ptr->libEvent_done_fifo_ptr->active ;
             event.value       = node_ptr->libEvent_done_fifo_ptr->value  ;
             event.result      = node_ptr->libEvent_done_fifo_ptr->result ;
             event.response    = node_ptr->libEvent_done_fifo_ptr->response ;
             event.response_len= node_ptr->libEvent_done_fifo_ptr->response_len ;

             node_ptr->libEvent_done_fifo.erase(node_ptr->libEvent_done_fifo_ptr);
             found = true ;
             if ( event.status == RETRY )
             {
                 slog ("%s over riding rety to fail\n",
                           node_ptr->libEvent_done_fifo_ptr->log_prefix.c_str() );
                 event.status = FAIL ;
             }
             rc = event.status ;
             break ;
         }
    }
    if ( found == false )
    {
         qlog ("%s not found in done queue\n", event.log_prefix.c_str());
         rc = RETRY ;
    }
    return (rc);
}

/* ***********************************************************************
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
 * ************************************************************************/

int nodeLinkClass::workQueue_process ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    /* handle purging the done queue on the last */
    if ( node_ptr->libEvent_done_fifo.size() > 0 )
    {
        /* Manage the done queue so that it does not grow forever
         * if command producers do not come back and dequeue their
         * responses */
         if ( node_ptr->libEvent_done_fifo.size() > 10 )
         {
             qlog ("%s Done Queue has %ld elements\n",
                       node_ptr->hostname.c_str(),
                       node_ptr->libEvent_done_fifo.size());

             /* TODO: look at the status of the commands and print a log of those that failed */

             /* Remove the first 8 - its a fifo the first ones at the front are the oldest */
             for ( int i=0 ; i < 8 ; i++ )
             {
                 node_ptr->libEvent_done_fifo.pop_front();
             }
             qlog ("%s Done Queue has %ld elements remaining\n",
                       node_ptr->hostname.c_str(),
                       node_ptr->libEvent_done_fifo.size());
         }
    }

    if ( node_ptr->libEvent_work_fifo.empty() )
    {
        // qlog_throttled ( node_ptr->no_work_log_throttle, 300,
        //                  "%s Idle ... \n",
        //                  node_ptr->hostname.c_str());
        node_ptr->no_work_log_throttle = 0 ;
        return (PASS);
    }

    if ( daemon_get_cfg_ptr()->debug_work & 8 )
    {
        // workQueue_print ( node_ptr ) ;
        syslog ( LOG_INFO, "\n");
        syslog ( LOG_INFO, "+------+-------+--------------+---------+---------------+-----+----------------------+\n");
        syslog ( LOG_INFO, "| Mode |  Seq  | Hostname     | Service |    Request    | Tmo |  IP Address     Port | payload ...\n");
        syslog ( LOG_INFO, "+------+-------+--------------+---------+---------------+-----+----------------------+\n");
        for ( node_ptr->libEvent_work_fifo_ptr  = node_ptr->libEvent_work_fifo.begin();
              node_ptr->libEvent_work_fifo_ptr != node_ptr->libEvent_work_fifo.end();
              node_ptr->libEvent_work_fifo_ptr ++ )
        {
            syslog ( LOG_INFO, "| %-4s | %5d | %-12s | %-7s | %-13s | %3d | %15s:%d | %s\n",
                _get_work_state_str(node_ptr->libEvent_work_fifo_ptr->state).c_str(),
                node_ptr->libEvent_work_fifo_ptr->sequence,
                node_ptr->libEvent_work_fifo_ptr->hostname.c_str(),
                node_ptr->libEvent_work_fifo_ptr->service.c_str(),
                node_ptr->libEvent_work_fifo_ptr->operation.c_str(),
                node_ptr->libEvent_work_fifo_ptr->timeout,
                node_ptr->libEvent_work_fifo_ptr->ip.c_str(),
                node_ptr->libEvent_work_fifo_ptr->port,
                node_ptr->libEvent_work_fifo_ptr->payload.c_str());
        }
        syslog ( LOG_INFO, "+------+-------+--------------+---------+--------------+-----+----------------------+\n");
    }

    int size = node_ptr->libEvent_work_fifo.size() ;
    if ( size > QUEUE_OVERLOAD )
    {
        elog ( "%s work queue overload ; clearing %d entries\n", node_ptr->hostname.c_str(), size );
        workQueue_purge ( node_ptr );
        return (FAIL);
    }

    if ( node_ptr->libEvent_work_fifo.empty() )
    {
        slog ("%s unexpected empty 'libEvent_work_fifo_ptr' (should have %d elements)\n",
                  node_ptr->hostname.c_str(), size );
        workQueue_purge ( node_ptr );
        return (FAIL_NULL_POINTER);
    }

    node_ptr->libEvent_work_fifo_ptr = node_ptr->libEvent_work_fifo.begin();
    switch ( node_ptr->libEvent_work_fifo_ptr->state )
    {
        case HTTP__TRANSMIT:
        {
            node_ptr->thisReq = node_ptr->libEvent_work_fifo.front();

            qlog ("%s Transmitted\n", node_ptr->thisReq.log_prefix.c_str() );

            rc = mtcHttpUtil_api_request ( node_ptr->thisReq ) ;
            if ( rc )
            {
                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__FAILURE ;
            }
            else
            {
                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__RECEIVE_WAIT ;

                if ( node_ptr->http_timer.tid )
                    mtcTimer_stop ( node_ptr->http_timer );
                rc = mtcTimer_start_msec ( node_ptr->http_timer, mtcTimer_handler, HTTP_RECEIVE_WAIT_MSEC );
                if ( rc != PASS )
                {
                    elog ("%s failed to start http command timer ; failing command\n", node_ptr->thisReq.log_prefix.c_str());
                    node_ptr->libEvent_work_fifo_ptr->state =
                    node_ptr->thisReq.state = HTTP__FAILURE ;
                }
            }
            break ;
        }
        case HTTP__RECEIVE_WAIT:
        {
            if ( node_ptr->http_timer.ring == true )
            {
                if ( node_ptr->http_timer.error == true )
                {
                    slog ("%s timer handler ran while still in start utility ; handled ...\n", node_ptr->thisReq.log_prefix.c_str());
                    node_ptr->http_timer.error = false ;
                }

                if (( node_ptr->http_timer._guard != 0x12345678 ) || ( node_ptr->http_timer.guard_ != 0x77654321 ))
                {
                    slog ("%s timer struct guard barrier detected corruption\n", node_ptr->thisReq.log_prefix.c_str());
                }
                node_ptr->http_timer.ring = false ;
                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__RECEIVE ;
            }
            break ;
        }
        case HTTP__RECEIVE:
        {
            /* Try and receive the response */
            if ( node_ptr->thisReq.base == NULL )
            {
                slog ("%s has unexpected null HTTP request base pointer\n",
                          node_ptr->thisReq.log_prefix.c_str());

                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__FAILURE ;
                break ;
            }

            int msec_timeout = (node_ptr->thisReq.timeout*1000);
            int wait_time = (++node_ptr->thisReq.rx_retry_cnt)*HTTP_RECEIVE_WAIT_MSEC ;

            rc = mtcHttpUtil_receive ( node_ptr->thisReq );
            if ( rc == RETRY )
            {
                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__RECEIVE_WAIT ;
                mtcTimer_start_msec ( node_ptr->http_timer, mtcTimer_handler, HTTP_RECEIVE_WAIT_MSEC );

                if ((wait_time > (msec_timeout/4)) && ( node_ptr->thisReq.low_wm == false ) )
                {
                    qlog1 ("%s reached lower (1/4) timeout watermark (%d msec)\n",
                              node_ptr->thisReq.log_prefix.c_str(), wait_time );
                    node_ptr->libEvent_work_fifo_ptr->low_wm = node_ptr->thisReq.low_wm = true ;
                    break ;
                }
                else if ((wait_time > (msec_timeout/2)) && ( node_ptr->thisReq.med_wm == false ))
                {
                    qlog ("%s reached mid (1/2) timeout watermark (%d msec)\n",
                              node_ptr->thisReq.log_prefix.c_str(), wait_time);
                    node_ptr->libEvent_work_fifo_ptr->med_wm = node_ptr->thisReq.med_wm = true ;
                    break ;
                }
                else if (( wait_time > ((msec_timeout/4)*3)) && ( node_ptr->thisReq.high_wm == false ))
                {
                    wlog ("%s reached high (3/4) timeout watermark (%d msec)\n",
                              node_ptr->thisReq.log_prefix.c_str(), wait_time );
                    node_ptr->libEvent_work_fifo_ptr->high_wm = node_ptr->thisReq.high_wm = true ;
                    break ;
                }
                else
                {
                    /* Only print every 16 starting with 2 */
                    if ( (node_ptr->thisReq.rx_retry_cnt & 0xF) == 2 )
                    {
                        qlog ("%s rx_retry_cnt:%d\n",
                                  node_ptr->thisReq.log_prefix.c_str(),
                                  node_ptr->thisReq.rx_retry_cnt );
                    }
                    break ;
                }
            }
            #ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__HTTP_WORKQUEUE_OPERATION_FAILED, node_ptr->hostname, "" ))
            {
               ilog("%s FIT Operation Failed: %s", node_ptr->hostname.c_str(), node_ptr->httpReq.payload.c_str());
               node_ptr->thisReq.status = FAIL_AUTHENTICATION ;
               rc = FAIL_OPERATION ;
            }
            else if ( daemon_want_fit ( FIT_CODE__HTTP_WORKQUEUE_REQUEST_TIMEOUT, node_ptr->hostname, "" ))
            {
               ilog("%s FIT Request Timeout Failed: %s", node_ptr->hostname.c_str(), node_ptr->httpReq.payload.c_str());
               rc = FAIL_TIMEOUT ;
            }
            else if ( daemon_want_fit ( FIT_CODE__HTTP_WORKQUEUE_CONNECTION_LOSS, node_ptr->hostname, "" ))
            {
               ilog("%s FIT Connection Loss: %s", node_ptr->hostname.c_str(), node_ptr->httpReq.payload.c_str());
               node_ptr->thisReq.status = rc = FAIL_HTTP_ZERO_STATUS ;
            }
            #endif
            if ( rc != PASS )
            {
                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__FAILURE ;
            }
            else
            {
                if ( node_ptr->thisReq.cur_retries )
                {
                    ilog ("%s Completed (after %d retries) (took %d of %d msecs)\n",
                              node_ptr->thisReq.log_prefix.c_str(),
                              node_ptr->thisReq.cur_retries, wait_time,
                              node_ptr->thisReq.timeout*1000);
                }
                else
                {
                    qlog ("%s Completed (took %d of %d msecs)\n",
                              node_ptr->thisReq.log_prefix.c_str(),
                              wait_time,
                              node_ptr->thisReq.timeout*1000);
                }
                node_ptr->thisReq.exec_time_msec = wait_time ;

                node_ptr->thisReq.rx_retry_cnt = 0 ;

                mtcHttpUtil_free_conn ( node_ptr->thisReq );
                mtcHttpUtil_free_base ( node_ptr->thisReq );

                /* Don't add success responses to non-critical commands like
                 * "update uptime" and "update task" to the done queue */
                if ( !node_ptr->thisReq.noncritical )
                {
                    /* Copy done event to the done queue */
                    node_ptr->libEvent_done_fifo.push_back(node_ptr->thisReq);

                }
                /* Pop that done event off the work queue */
                node_ptr->libEvent_work_fifo.pop_front();
            }
            break ;
        }
        case HTTP__FAILURE:
        {
            bool want_retry = false ;

            mtcHttpUtil_free_conn ( node_ptr->thisReq );
            mtcHttpUtil_free_base ( node_ptr->thisReq );

            node_ptr->http_retries_cur++ ;
            node_ptr->thisReq.cur_retries++ ;

            if ( node_ptr->thisReq.noncritical == true )
            {
                if ( node_ptr->thisReq.cur_retries > node_ptr->thisReq.max_retries )
                {
                    node_ptr->oper_failures++ ;

                    wlog ("%s retry conjestion abort of non-critical command (%d:%d)\n",
                              node_ptr->thisReq.log_prefix.c_str(),
                              node_ptr->thisReq.cur_retries,
                              node_ptr->thisReq.max_retries );

                    /* Pop this aborted event off the work queue */
                    node_ptr->libEvent_work_fifo.pop_front();
                }
                else
                {
                    want_retry = true ;
                }
            }
            /* other wise its critical and we are going for the retries */
            else if ( node_ptr->thisReq.cur_retries >= node_ptr->thisReq.max_retries )
            {
                node_ptr->oper_failures++ ;
                elog ("%s Failed (rc:%d) - (%d of %d) (work->%s) (Critical:%s) (Total Fails:%d)\n",
                          node_ptr->thisReq.log_prefix.c_str(),
                          node_ptr->thisReq.status,
                          node_ptr->thisReq.cur_retries,
                          node_ptr->thisReq.max_retries,
                          node_ptr->thisReq.noncritical ? "drop" : "done",
                          node_ptr->thisReq.noncritical ? "No" : "Yes",
                          node_ptr->oper_failures );

                if ( node_ptr->thisReq.noncritical == false )
                {
                    /* Copy done event to the done queue */
                    node_ptr->libEvent_done_fifo.push_back(node_ptr->thisReq);
                }
                /* Pop that done event off the work queue */
                node_ptr->libEvent_work_fifo.pop_front();
            }
            else
            {
                want_retry = true ;
            }

            if ( want_retry )
            {
                wlog ("%s Failed (rc:%d) - (%d of %d) (Timeout=%d) (Critical:%s)\n",
                          node_ptr->thisReq.log_prefix.c_str(),
                          node_ptr->thisReq.status,
                          node_ptr->thisReq.cur_retries,
                          node_ptr->thisReq.max_retries,
                          node_ptr->thisReq.timeout,
                          node_ptr->thisReq.noncritical ? "No" : "Yes" );

                node_ptr->thisReq.response.clear();

                node_ptr->thisReq.status      = PASS  ;
                node_ptr->thisReq.http_status = 0     ;
                node_ptr->thisReq.active      = false ;
                node_ptr->thisReq.response_len= 0     ;

                /*
                 * If this is an inventory request ...
                 *
                 * 1. Init the inv struct
                 * 2. increase the timeout if is a critical command
                 *
                 * */
                if ( node_ptr->thisReq.service.find("mtcInvApi") != std::string::npos )
                {
                    node_inv_init ( node_ptr->thisReq.inv_info ) ;
                    if ( node_ptr->thisReq.noncritical == false )
                    {
                        int temp = node_ptr->libEvent_work_fifo_ptr->timeout ;

                        /*
                         * Increase and update the timeout value for critical commands
                         * in hope that it will succeed on he next go around.
                         */
                        node_ptr->libEvent_work_fifo_ptr->timeout += get_mtcInv_ptr()->sysinv_timeout ;
                        dlog ("%s timeout extended from %d to %d secs\n",
                                  node_ptr->thisReq.log_prefix.c_str(), temp,
                                  node_ptr->libEvent_work_fifo_ptr->timeout );
                    }
                }

                /* Save the retry count */
                node_ptr->libEvent_work_fifo_ptr->cur_retries =
                node_ptr->thisReq.cur_retries ;

                mtcTimer_start ( node_ptr->http_timer, mtcTimer_handler, HTTP_RETRY_WAIT_SECS );
                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__RETRY_WAIT ;
                dlog ("%s %d sec retry wait started", node_ptr->thisReq.log_prefix.c_str(), HTTP_RETRY_WAIT_SECS);
            }
            break ;
        }
        case HTTP__RETRY_WAIT:
        {
            if ( node_ptr->http_timer.ring == true )
            {
                dlog ("%s %d sec retry wait expired", node_ptr->thisReq.log_prefix.c_str(), HTTP_RETRY_WAIT_SECS);
                node_ptr->libEvent_work_fifo_ptr->state =
                node_ptr->thisReq.state = HTTP__TRANSMIT ;
            }
            break ;
        }
        default:
        {
            slog ("%s Bad libEvent work state (%d) ; clearing work/done queue\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->libEvent_work_fifo_ptr->state );
            node_ptr->libEvent_work_fifo.clear();
            node_ptr->libEvent_done_fifo.clear();
            rc = FAIL_BAD_CASE ;
        }
    }
    return (rc) ;
}

/* ***********************************************************************
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
 */
int nodeLinkClass::workQueue_del_cmd ( struct nodeLinkClass::node * node_ptr, int sequence )
{
    bool found = false ;
    for ( node_ptr->libEvent_work_fifo_ptr = node_ptr->libEvent_work_fifo.begin();
          node_ptr->libEvent_work_fifo_ptr != node_ptr->libEvent_work_fifo.end();
          node_ptr->libEvent_work_fifo_ptr++ )
    {
         if ( node_ptr->libEvent_work_fifo_ptr->sequence == sequence )
         {
             wlog ("%s force removed from work queue\n",
                       node_ptr->libEvent_work_fifo_ptr->log_prefix.c_str());
             node_ptr->libEvent_work_fifo.erase(node_ptr->libEvent_work_fifo_ptr);
             found = true ;
             break ;
         }
    }
    if ( found == false )
    {
         wlog ("%s command Seq:%d not found in work queue\n",
                   node_ptr->hostname.c_str(), sequence );
    }
    return(PASS);
}

/* ***********************************************************************
 *
 * Name       : nodeLinkClass::doneQueue_purge
 *
 * Description: Removes all items from the done queue.
 *
 * Returns a failure, the sequence number of the first command
 * in the done queue that did not PASS.
 *
 */
int nodeLinkClass::doneQueue_purge ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;
    int size = node_ptr->libEvent_done_fifo.size() ;
    if ( size )
    {
        int index = 0 ;
        for ( node_ptr->libEvent_done_fifo_ptr = node_ptr->libEvent_done_fifo.begin();
              node_ptr->libEvent_done_fifo_ptr != node_ptr->libEvent_done_fifo.end();
              node_ptr->libEvent_done_fifo_ptr++ )
        {
            index++ ;
            if ( node_ptr->libEvent_done_fifo_ptr->status )
            {
               /* Don't report noncritical command failure status.
                * Such commands might be "update uptime" and "update task"
                * and we don't want them to fail operations */
                if ( !node_ptr->libEvent_done_fifo_ptr->noncritical )
                {
                    elog ("%s critical operation failed (rc:%d)\n",
                              node_ptr->libEvent_done_fifo_ptr->log_prefix.c_str(),
                              node_ptr->libEvent_done_fifo_ptr->status);

                    if ( ! node_ptr->libEvent_done_fifo_ptr->payload.empty() )
                    {
                        elog ("%s ... %s\n", node_ptr->hostname.c_str(),
                              node_ptr->libEvent_done_fifo_ptr->payload.c_str());
                    }

                    /* Save sequence of first failed priority command */
                    if ( rc == PASS )
                    {
                        rc = node_ptr->libEvent_done_fifo_ptr->sequence ;
                    }
                }
                else
                {
                    wlog ("%s noncritical operation failed (rc:%d)\n",
                              node_ptr->libEvent_done_fifo_ptr->log_prefix.c_str(),
                              node_ptr->libEvent_done_fifo_ptr->status);

                    if ( ! node_ptr->libEvent_done_fifo_ptr->payload.empty() )
                    {
                        wlog ("%s ... %s\n", node_ptr->hostname.c_str(),
                              node_ptr->libEvent_done_fifo_ptr->payload.c_str());
                    }
                }
            }
        }
        if ( rc == PASS )
        {
            qlog ("%s all (%d) priority queued operations passed (qlog)\n", node_ptr->hostname.c_str(), size );
        }

        qlog ("%s purging %d items from doneQueue\n", node_ptr->hostname.c_str(), size );
        node_ptr->libEvent_done_fifo.clear();
    }
    return (rc);
}

/* ***********************************************************************
 *
 * Name       : nodeLinkClass::workQueue_purge
 *
 * Description: Removes all items from the work queue.
 *
 */
int nodeLinkClass::workQueue_purge ( struct nodeLinkClass::node * node_ptr )
{
    int size = node_ptr->libEvent_work_fifo.size() ;
    if ( size )
    {
        /* TODO: find out how to force close a connection.
         * Don't free the connection if it is in the receiving state or
         * we might get a segfault
         * There is only ever one connection open at a time for a specific host
         * so its only 'thisReq' we need to worry about. */
        if ( node_ptr->libEvent_work_fifo_ptr->state != HTTP__RECEIVE )
        {
            mtcHttpUtil_free_conn ( node_ptr->thisReq );
            mtcHttpUtil_free_base ( node_ptr->thisReq );
        }

        wlog ("%s purging %d items from workQueue\n", node_ptr->hostname.c_str(), size );
        for ( node_ptr->libEvent_work_fifo_ptr = node_ptr->libEvent_work_fifo.begin();
              node_ptr->libEvent_work_fifo_ptr != node_ptr->libEvent_work_fifo.end();
              node_ptr->libEvent_work_fifo_ptr++ )
        {
            if ( node_ptr->libEvent_work_fifo_ptr->state == HTTP__TRANSMIT )
            {
                wlog ("%s ... was not executed\n",
                           node_ptr->libEvent_work_fifo_ptr->log_prefix.c_str());
            }
            else
            {
                wlog ("%s ... did not complete (%s)\n",
                           node_ptr->libEvent_work_fifo_ptr->log_prefix.c_str(),
                           _get_work_state_str(node_ptr->libEvent_work_fifo_ptr->state).c_str());
            }
        }

        node_ptr->libEvent_work_fifo.clear();
    }
    else
    {
        qlog ("%s all work done\n", node_ptr->hostname.c_str());
    }

    // node_ptr->libEvent_work_fifo_ptr->state = HTTP__TRANSMIT ;
    return (PASS);
}

int nodeLinkClass::workQueue_done ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    /* have we timed out waiting the work queue to deplete */
    if ( node_ptr->mtcTimer.ring == true )
    {
        qlog ( "%s Ring handler\n" , node_ptr->hostname.c_str());

        node_ptr->mtcTimer.ring = false ;
        /* Search through work queue and don't fail if the
         * only requests remaining is an uptime */
        for ( node_ptr->libEvent_work_fifo_ptr = node_ptr->libEvent_work_fifo.begin();
              node_ptr->libEvent_work_fifo_ptr != node_ptr->libEvent_work_fifo.end();
              node_ptr->libEvent_work_fifo_ptr++ )
        {
            /* Don't report work queue timeout if there are only noncritical
             * commands left in the work queue. Such commands might be
             * "update uptime" and "update task" */
            if ( !node_ptr->libEvent_work_fifo_ptr->noncritical )
            {
                rc = FAIL_WORKQ_TIMEOUT ;
            }
        }
        if ( rc != PASS )
        {
            elog ("%s timeout on work queue complete\n", node_ptr->hostname.c_str());
        }
    }

    /* We still doing enable work ? */
    else if ( node_ptr->libEvent_work_fifo.size () == 0 )
    {
        qlog ( "%s Empty Work Queue\n" , node_ptr->hostname.c_str());

        /* O.K. the work queue is done - cancel the timer */
        if ( node_ptr->mtcTimer.tid )
             mtcTimer_stop ( node_ptr->mtcTimer );

        /* Error logs are generated inside */
        rc = doneQueue_purge ( node_ptr );
    }
    else
    {
        qlog ( "%s Retry\n" , node_ptr->hostname.c_str());

        rc = RETRY ;
    }

#ifdef WANT_FIT_TESTING
    if (( rc == PASS ) && ( daemon_want_fit ( FIT_CODE__WORK_QUEUE, node_ptr->hostname )))
        rc = FAIL_FIT ;
#endif

    return (rc);
}


/* ***********************************************************************
 *
 * Name       : nodeLinkClass::workQueue_present
 *
 * Description: Checks to see if this libEvent is in the work queue.
 *
 * @return true if present otherwise false
 *
 */
bool nodeLinkClass::workQueue_present ( libEvent & event )
{
    nodeLinkClass::node * node_ptr = this->getNode (event.hostname);
    if ( node_ptr != NULL )
    {
        if ( node_ptr->libEvent_work_fifo.size() )
        {
            for ( node_ptr->libEvent_work_fifo_ptr = node_ptr->libEvent_work_fifo.begin();
                  node_ptr->libEvent_work_fifo_ptr != node_ptr->libEvent_work_fifo.end();
                  node_ptr->libEvent_work_fifo_ptr++ )
            {
                if ( node_ptr->libEvent_work_fifo_ptr->sequence == event.sequence )
                {
                    qlog ("%s ... found in work queue\n", node_ptr->libEvent_work_fifo_ptr->log_prefix.c_str());
                    return (true);
                }
            }
        }
    }
    wlog ("%s ... not found in work queue\n", event.log_prefix.c_str());
    return (false);
}
