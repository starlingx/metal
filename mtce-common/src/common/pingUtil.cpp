/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Maintenance Ping Utility Implementation
  */

#include "daemon_common.h"   /* for ... MEMSET_ZERO                         */
#include "nodeBase.h"
#include "nodeUtil.h"
#include "hostUtil.h"        /* for ... hostUtil_is_valid_ip_addr           */
#include "pingUtil.h"        /* for ... this module header                  */

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "acc"

typedef struct
{
    struct icmphdr hdr;
    char msg[PING_MESSAGE_LEN];
} ping4_tx_message_type ;

typedef struct
{
    struct iphdr ip_hdr ;
    struct icmphdr  hdr ;
    char msg[PING_MESSAGE_LEN];
} ping4_rx_message_type ;

typedef struct
{
//  struct ip6_hdr iphdr;
    struct icmp6_hdr icmphdr;
    char msg[PING_MESSAGE_LEN] ; // MSG_HEADER_SIZE];
} ping6_tx_message_type ;


typedef struct
{
    // struct ip6_hdr ip_hdr;
    struct icmp6_hdr  hdr;
    char msg[PING_MESSAGE_LEN];
} ping6_rx_message_type ;

/*******************************************************************************
 *
 * Name    : pingUtil_init
 *
 * Purpose : Setup a ping socket
 *
 * Assumptions: caller initializes and installs timer handler outside of init
 *              before the monitor is called.
 *
 * Returns : PASS      : non-blocking ping socket towards specified ip address setup ok
 *           FAIL__xxx : init failed
 *
 ******************************************************************************/
int pingUtil_init ( string hostname, ping_info_type & ping_info, const char * ip_address )
{
    int rc = PASS ;
    if ( hostUtil_is_valid_ip_addr ( ip_address ) == false )
    {
        wlog ("%s refusing to setup ping socket for invalid IP address\n", hostname.c_str());
        return (FAIL_NULL_POINTER);
    }

    string identity_string = program_invocation_short_name ;
    identity_string.append ("_");
    identity_string.append(ip_address) ;
    ping_info.identity =
    checksum ((void*)identity_string.data(), identity_string.length());

    dlog1 ("%s ping identity string: %s (0x%04x)\n",
              hostname.c_str(),
              identity_string.c_str(),
              ping_info.identity);

    /* init the ping_info struct */
    ping_info.hostname = hostname   ;
    ping_info.ip       = ip_address ;
    ping_info.sequence = getpid()   ;
    ping_info.recv_retries  = 0     ;
    ping_info.send_retries  = 0     ;
    ping_info.requested= false      ;
    ping_info.received = false      ;
    ping_info.recv_flush_highwater=2;
    /* added for ping monitor */
    ping_info.ok       = false      ;
    ping_info.monitoring = false    ;
    ping_info.stage = PINGUTIL_MONITOR_STAGE__OPEN ;

    ping_info.sock = new msgClassTx(ip_address, 0, IPPROTO_RAW, NULL );

    /* Validate the socket setup */
    if ( ping_info.sock == NULL )
    {
        rc = FAIL_SOCKET_CREATE ;
        elog ("%s failed to create ping socket ; null socket\n",
                  ping_info.hostname.c_str());
    }
    else
    {
        rc = ping_info.sock->return_status;
        if ( rc != PASS )
        {
            elog ("%s failed to create ping socket ; error status:%d\n",
                     ping_info.hostname.c_str(), rc );
            rc = FAIL_SOCKET_CREATE ;
            delete (ping_info.sock );
            return rc;
        }
        else
        {
            if ( ( rc = ping_info.sock->setSocketNonBlocking () ) == PASS )
            {
                MEMSET_ZERO(ping_info.message);
                switch ( ping_info.sock->get_dst_addr()->getIPVersion() )
                {
                    case AF_INET:
                    {
                        ping_info.ipv6_mode = false ;
                        snprintf (&ping_info.message[0], PING_MESSAGE_LEN,
                                  "%s ipv4 ping message from %s daemon",
                                      ping_info.hostname.data(),
                                      program_invocation_short_name);
                        break ;
                    }
                    case AF_INET6:
                    {
                        ping_info.ipv6_mode = true ;
                        snprintf (&ping_info.message[0], PING_MESSAGE_LEN,
                                  "%s ipv6 ping message from %s daemon",
                                      ping_info.hostname.data(),
                                      program_invocation_short_name);
                        break ;
                    }
                    default:
                    {
                        elog ("Unsupported IP protocol version\n");
                        return (FAIL);
                    }
                }
                dlog3 ("%s (fd:%d)\n",
                          ping_info.message,
                          ping_info.sock->getFD());
            }
            else
            {
                elog ("%s failed to set ping socket to non-blocking:%d\n",
                          ping_info.hostname.c_str(), rc );
            }
        }
    }

    return rc ;
}

int pingUtil_recv_flush ( ping_info_type & ping_info, bool loud );

/*******************************************************************************
 *
 * Name    : pingUtil_send
 *
 * Purpose : Send an ICMP ECHO ping request to the specified socket
 *
 * Returns : PASS : send was ok
 *           FAIL : send failed
 *
 ******************************************************************************/
int pingUtil_send ( ping_info_type & ping_info )
{
    ping4_tx_message_type ping4_tx;
    ping6_tx_message_type ping6_tx;
    int bytes = 0 ;


    pingUtil_recv_flush ( ping_info, false );

    if (( ping_info.sock == NULL ) || ( ping_info.sock->return_status != PASS ))
    {
        wlog ("%s refusing to send ping on %s socket\n",
                  ping_info.hostname.c_str(),
                  ping_info.sock ? "faulty" : "null" );

        return (FAIL_NULL_POINTER);
    }

    ping_info.sequence++ ;
    ping_info.recv_retries = 0;

    if ( ping_info.ipv6_mode == false )
    {
        MEMSET_ZERO (ping4_tx);

        ping4_tx.hdr.type = ICMP_ECHO;

        ping4_tx.hdr.un.echo.id       = htons(ping_info.identity) ;
        ping4_tx.hdr.un.echo.sequence = htons(ping_info.sequence) ;

        snprintf ( &ping4_tx.msg[0], PING_MESSAGE_LEN, ping_info.message );

        /* checksum should not be converted to htons
         * - will get (wrong icmp cksum ) */
        ping4_tx.hdr.checksum = checksum(&ping4_tx, sizeof(ping4_tx));

        dlog3 ("%s ping4 checksum: %04x\n",
                  ping_info.hostname.c_str(),
                  ping4_tx.hdr.checksum );

        bytes = ping_info.sock->write((const char*)&ping4_tx, sizeof(ping4_tx));
    }
    else
    {
        MEMSET_ZERO (ping6_tx);

        ping6_tx.icmphdr.icmp6_type = ICMP6_ECHO_REQUEST;
        ping6_tx.icmphdr.icmp6_code = 0;

        ping6_tx.icmphdr.icmp6_id = htons(ping_info.identity) ;
        ping6_tx.icmphdr.icmp6_seq = htons(ping_info.sequence) ;

        snprintf ( &ping6_tx.msg[0], PING_MESSAGE_LEN, ping_info.message );

        ping6_tx.icmphdr.icmp6_cksum = htons(checksum(&ping6_tx, sizeof(ping6_tx)));

        dlog3 ("%s ping6 checksum: %04x\n",
                  ping_info.hostname.c_str(),
                  ping6_tx.icmphdr.icmp6_cksum );

        bytes = ping_info.sock->write( (const char*)&ping6_tx, sizeof(ping6_tx_message_type));
    }

    ping_info.recv_retries = 0;

    if ( bytes <= 0 )
    {
        wlog ("%s ping %s send failed (rc:%d) (%d:%m)\n", ping_info.hostname.c_str(), ping_info.ip.c_str(), bytes, errno );
        return FAIL ;
    }
    if ( ping_info.monitoring == false )
    {
        ilog ("%s ping send %s ok ; identity:%04x sequence:%04x (try %d)\n",
                  ping_info.hostname.c_str(),
                  ping_info.ip.c_str(),
                  ping_info.identity,
                  ping_info.sequence,
                  ping_info.send_retries);
    }
    else
    {
        mlog ("%s ping send %s ok ; identity:%04x sequence:%04x (try %d)\n",
                  ping_info.hostname.c_str(),
                  ping_info.ip.c_str(),
                  ping_info.identity,
                  ping_info.sequence,
                  ping_info.send_retries);
    }

    ping_info.received = false ;
    ping_info.requested = true ;

    return PASS ;
}

/*******************************************************************************
 *
 * Name    : pingUtil_recv_flush
 *
 * Purpose : Empty the ping receiver in preparation for a fresh ping request.
 *
 * Returns : PASS : empty
 *           RETRY: not empty
 *
 ******************************************************************************/

int pingUtil_recv_flush ( ping_info_type & ping_info, bool loud )
{
    int empty_count = 0 ;
    int flush_count = 0 ;
    bool exit_pass  = false ;

    if ( ping_info.sock == NULL )
        return (FAIL_NULL_POINTER);

    for ( int i = 0 , bytes = 0 ; i < PING_MAX_FLUSH_RETRIES ; i++ )
    {
        if (  ping_info.ipv6_mode == true )
        {
            ping6_rx_message_type ping6_rx ;
            MEMSET_ZERO(ping6_rx);
            bytes = ping_info.sock->readReply( (char *)&ping6_rx, sizeof(ping6_rx_message_type));
            if ( bytes > 0 )
            {
                unsigned short  id = htons(ping6_rx.hdr.icmp6_id)  ;
                unsigned short seq = htons(ping6_rx.hdr.icmp6_seq) ;
                flush_count++   ;
                empty_count = 0 ;

                if ( id == ping_info.identity )
                {
                    wlog ("%s flushed out-of-sequence ping response for my identity:%04x ; sequence:%04x (%d)\n",
                              ping_info.hostname.c_str(), ping_info.identity, seq, flush_count );
                }
                else if ( loud == true )
                {
                    wlog ("%s flushed %d byte message identity:%04x sequence:%04x\n",
                              ping_info.hostname.c_str(), bytes, id , seq );
                }
            }
        }
        else
        {
            ping4_rx_message_type  ping4_rx ;
            MEMSET_ZERO(ping4_rx);
            bytes = ping_info.sock->readReply( (char *)&ping4_rx, sizeof(ping4_rx_message_type));
            if (( bytes > 0 ) && ( ping4_rx.hdr.un.echo.id != 0 ))
            {
                flush_count++   ;
                empty_count = 0 ;
                unsigned short  id = htons(ping4_rx.hdr.un.echo.id);
                unsigned short seq = htons(ping4_rx.hdr.un.echo.sequence);
                if ( id == ping_info.identity )
                {
                    wlog ("%s flushed out-of-sequence ping response for my identity:%04x ; sequence:%04x (%d)\n",
                              ping_info.hostname.c_str(),
                              ping_info.identity,
                              seq,
                              flush_count );
                }
                else if ( loud == true )
                {
                    wlog ("%s flushed %d byte message identity:%04x sequence:%04x\n",
                              ping_info.hostname.c_str(), bytes, id, seq );
                }
            }
        }

        if ( bytes <= 0 )
        {
            if ( empty_count++ == 3 )
            {
                exit_pass = true ;
                break ;
            }
        }
    }

    if ( flush_count > ping_info.recv_flush_highwater )
    {
        ping_info.recv_flush_highwater = flush_count ;
        dlog ("%s ping flush peak at %d\n",
                  ping_info.hostname.c_str(),
                  ping_info.recv_flush_highwater );
    }
    else if ( flush_count )
    {
        dlog2 ("%s ping flushed %d messages\n",
                   ping_info.hostname.c_str(), flush_count );
    }

    if ( exit_pass == true )
        return (PASS);

    return (RETRY);
}

/*******************************************************************************
 *
 * Name    : pingUtil_recv
 *
 * Purpose : Receive an ICMP ping response and compare the suggested sequence
 *           and identifier numbers.
 *
 * Returns : PASS : got the response with the correct id and seq codes
 *           RETRY: got response but with one or mode bad codes
 *           FAIL : got no ping reply
 *
 ******************************************************************************/

/* handle a reasonable ping flood without failing local pings */
#define MAX_PING_FLUSH (512)

int pingUtil_recv ( ping_info_type & ping_info,
                              bool   loud )  /* print log if no data received */
{
    int rc = FAIL ;
    int bytes = 0 ;

    if (( ping_info.requested == true ) && ( ping_info.received == true ))
    {
        ping_info.requested = false ;
        return (PASS);
    }

    if ( ping_info.sock == NULL )
        return (FAIL_NULL_POINTER);

    for ( int i = 0 ; i < MAX_PING_FLUSH ; i++ )
    {
        if (  ping_info.ipv6_mode == true )
        {
            ping6_rx_message_type  ping6_rx ;
            MEMSET_ZERO(ping6_rx);
            bytes = ping_info.sock->readReply( (char *)&ping6_rx, sizeof(ping6_rx_message_type));
            if ( bytes > 0 )
            {
                unsigned short  id = htons(ping6_rx.hdr.icmp6_id);
                unsigned short seq = htons(ping6_rx.hdr.icmp6_seq);

                if ( loud == true )
                {
                    ilog ("%s %s search ; bytes:%d ; identity:%04x (got %04x) sequence:%04x (got %04x)\n",
                              ping_info.hostname.c_str(),
                              ping_info.ip.c_str(), bytes,
                              ping_info.identity, ping6_rx.hdr.icmp6_id,
                              ping_info.sequence, ping6_rx.hdr.icmp6_seq );
                }

                if (( ping6_rx.hdr.icmp6_type == ICMP6_ECHO_REPLY ) &&
                    ( id  == ping_info.identity ) &&
                    ( seq == ping_info.sequence ))
                {
                    /* Don't print this log once we have established ping and
                     * are in monitoring mode. */
                    if ( ping_info.monitoring == false )
                    {
                        /* ... only want the log when we ar first connecting */
                        ilog ("%s ping recv %s ok ; identity:%04x sequence:%04x (try %d) (%d)\n",
                                  ping_info.hostname.c_str(),
                                  ping_info.ip.c_str(),
                                  ping_info.identity,
                                  ping_info.sequence,
                                  ping_info.recv_retries+1,
                                  i);
                    }
                    else
                    {
                        /* ... only want the log when we ar first connecting */
                        mlog ("%s ping recv %s ok ; identity:%04x sequence:%04x (try %d) (%d)\n",
                                  ping_info.hostname.c_str(),
                                  ping_info.ip.c_str(),
                                  ping_info.identity,
                                  ping_info.sequence,
                                  ping_info.recv_retries+1,
                                  i);
                    }

                    ping_info.requested = false ;
                    ping_info.received  = true  ;
                    rc = PASS ;
                    break ;
                }

                else if ( ping6_rx.hdr.icmp6_id == ping_info.identity )
                {
 	 	            ilog ("%s received-out-of-sequence ping response for this identity:%04x ; sequence:%04x\n",
                              ping_info.hostname.c_str(), id, seq);
                     rc = RETRY ;
                }
                else
                {
                   ; /* identity is 0 or does not match this host */
                }
            }
            else
            {
                /* no data */
                rc = RETRY ;
                break ;
            }
        }
        else
        {
            ping4_rx_message_type  ping4_rx ;
            MEMSET_ZERO(ping4_rx);
            bytes = ping_info.sock->readReply ( (char*)&ping4_rx, sizeof(ping4_rx)) ;
            if ( bytes > 0 )
            {
                unsigned short  id = htons(ping4_rx.hdr.un.echo.id);
                unsigned short seq = htons(ping4_rx.hdr.un.echo.sequence);

                // dump_memory ( &ping4_rx, 16, sizeof(ping4_rx_message_type));
                if ( loud == true )
                {
                    ilog ("%s %s search ; bytes:%d ; identity:%04x (got %04x) sequence:%04x (got %04x)\n",
                              ping_info.hostname.c_str(),
                              ping_info.ip.c_str(), bytes,
                              ping_info.identity, id,
                              ping_info.sequence, seq );
                }

                if (( ping4_rx.hdr.type == ICMP_ECHOREPLY ) &&
                    ( id  == ping_info.identity ) &&
                    ( seq == ping_info.sequence ))
                {
                    /* Don't print this log once we have established ping and
                     * are in monitoring mode. */
                    if ( ping_info.monitoring == false )
                    {
                        /* ... only want the log when we ar first connecting */
                        ilog ("%s ping recv %s ok ; identity:%04x sequence:%04x (try %d) (%d)\n",
                                  ping_info.hostname.c_str(),
                                  ping_info.ip.c_str(),
                                  ping_info.identity,
                                  ping_info.sequence,
                                  ping_info.recv_retries+1,
                                  i);
                    }
                    else
                    {
                        /* ... only want the log when we ar first connecting */
                        mlog ("%s ping recv %s ok ; identity:%04x sequence:%04x (try %d) (%d)\n",
                                  ping_info.hostname.c_str(),
                                  ping_info.ip.c_str(),
                                  ping_info.identity,
                                  ping_info.sequence,
                                  ping_info.recv_retries+1,
                                  i);
                    }

                    ping_info.requested = false ;
                    ping_info.received  = true  ;

                    rc = PASS ;
                    break ;
                }

                else if ( id == ping_info.identity )
                {
 	 	            ilog ("%s received-out-of-sequence ping response for this identity:%04x ; sequence:%04x\n",
                              ping_info.hostname.c_str(), id, seq );
                    rc = RETRY ;
                }
                else
                {
                   ; /* identity is 0 or does not match this host */
                }
            }
            else
            {
                /* no data */
                rc = RETRY ;
                break ;
            }
        }
    }
    return rc ;
}

/*******************************************************************************
 *
 * Name    : pingUtil_fini
 *
 * Purpose : Close an ping socket
 *
 *******************************************************************************/
void pingUtil_fini ( ping_info_type & ping_info )
{
    if ( ping_info.sock )
    {
        dlog1 ("%s ping socket close ok (fd:%d)\n",
                  ping_info.hostname.c_str(),
                  ping_info.sock->getFD());

        delete ( ping_info.sock );
        ping_info.sock = NULL ;
    }

    ping_info.recv_retries = 0;
    ping_info.send_retries = 0;
    ping_info.sequence = 0;
    ping_info.identity = 0;

    /* Support for ping monitor */
    mtcTimer_reset ( ping_info.timer );
    ping_info.stage = PINGUTIL_MONITOR_STAGE__IDLE ;
}

/********************************************************************************
 *
 * Name    : pingUtil_acc_monitor
 *
 * Purpose : FSM used to monitor ping access to specific ip address
 *
 *******************************************************************************/

int pingUtil_acc_monitor ( ping_info_type & ping_info )
{
    switch ( ping_info.stage )
    {
        /* do nothing stage */
        case PINGUTIL_MONITOR_STAGE__IDLE:
        {
            break ;
        }
        case PINGUTIL_MONITOR_STAGE__WAIT:
        {
            if ( mtcTimer_expired ( ping_info.timer ) )
            {
                ping_info.stage = PINGUTIL_MONITOR_STAGE__SEND ;
            }
            /* Don't let the buffer fill up with pings ;
             * keep the socket empty till we want to ping */
            pingUtil_recv_flush ( ping_info , false );

            break ;
        }
        case PINGUTIL_MONITOR_STAGE__OPEN:
        {
            if ( pingUtil_init ( ping_info.hostname,
                                 ping_info,
                                 ping_info.ip.data()) != PASS )
            {
                ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
            }
            else
            {
                ping_info.stage = PINGUTIL_MONITOR_STAGE__SEND ;
            }
            break ;
        }
        case PINGUTIL_MONITOR_STAGE__SEND:
        {
            if ( ping_info.sock == NULL )
            {
                if (( ping_info.ip.empty()) || !ping_info.ip.compare(NONE))
                {
                    elog ("%s no address to ping\n", ping_info.hostname.c_str());
                    ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
                    break ;
                }

                int rc = pingUtil_init ( ping_info.hostname,
                                         ping_info,
                                         ping_info.ip.data());
                if ( rc )
                {
                    elog ("%s failed to setup bmc ping socket to '%s'\n",
                              ping_info.hostname.c_str(),
                              ping_info.ip.c_str());

                    ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
                    break ;
                }
            }


            if ( ++ping_info.send_retries > PING_MAX_SEND_RETRIES )
            {
                elog ("%s ping to %s failed\n",
                          ping_info.hostname.c_str(),
                          ping_info.ip.c_str());

                ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
            }
            else if ( pingUtil_send ( ping_info ) )
            {
                elog ("%s failed to send bmc ping\n", ping_info.hostname.c_str());
                ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
            }
            else
            {
                if ( ping_info.timer_handler == NULL )
                {
                    elog ("%s no timer handler installed\n", ping_info.hostname.c_str());
                    ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
                }
                else
                {
                    if ( ping_info.timer.tid )
                    {
                        ilog ("%s unexpected active timer\n", ping_info.hostname.c_str());
                        mtcTimer_reset ( ping_info.timer );
                    }
                    mtcTimer_start_msec ( ping_info.timer, ping_info.timer_handler, PING_WAIT_TIMER_MSEC );
                    ping_info.stage = PINGUTIL_MONITOR_STAGE__RECV ;
                }
            }
            break ;
        }
        case PINGUTIL_MONITOR_STAGE__RECV:
        {
            if ( mtcTimer_expired ( ping_info.timer ))
            {
                bool loud = false ;
                if ( daemon_get_cfg_ptr()->debug_bmgmt )
                    loud = true ;

                if ( pingUtil_recv ( ping_info , loud ) )
                {
                    if ( ++ping_info.recv_retries > (PING_MAX_RECV_RETRIES) )
                    {
                        /* only print this log once on the resend attempt */
                        if ( ping_info.send_retries >= PING_MAX_SEND_RETRIES )
                        {
                            mlog ("%s ping recv from %s missed ; identity:%04x sequence:%04x (try %d of %d)\n",
                                      ping_info.hostname.c_str(),
                                      ping_info.ip.c_str(),
                                      ping_info.identity,
                                      ping_info.sequence,
                                      ping_info.recv_retries-1,
                                      PING_MAX_RECV_RETRIES);
                        }
                        ping_info.stage = PINGUTIL_MONITOR_STAGE__SEND ;
                        break ;
                    }
                    else
                    {
                       blog1 ("%s retrying ping\n", ping_info.hostname.c_str());
                    }
                    mtcTimer_start_msec ( ping_info.timer, ping_info.timer_handler, PING_RETRY_DELAY_MSECS );
                }
                else
                {
                    int interval = PING_MONITOR_INTERVAL ;
                    ping_info.ok = true ;
                    ping_info.monitoring = true ;

                    dlog ("%s ping %s ok (send:%d:recv:%d) (%d)\n",
                              ping_info.hostname.c_str(),
                              ping_info.ip.c_str(),
                              ping_info.send_retries,
                              ping_info.recv_retries+1,
                              ping_info.ok );

                    ping_info.send_retries = 0 ;
                    ping_info.recv_retries = 0 ;

#ifdef WANT_FIT_TESTING
                    if ( daemon_want_fit ( FIT_CODE__FAST_PING_AUDIT_HOST, ping_info.hostname   ) == true )
                        interval = 3 ;
                    if ( daemon_want_fit ( FIT_CODE__FAST_PING_AUDIT_ALL ) == true )
                        interval = 3 ;
#endif
                    mtcTimer_start ( ping_info.timer, ping_info.timer_handler, interval );
                    ping_info.stage = PINGUTIL_MONITOR_STAGE__WAIT ;
                }
            }
            break ;
        }
        case PINGUTIL_MONITOR_STAGE__CLOSE:
        {
            pingUtil_fini (ping_info);
            break ;
        }
        case PINGUTIL_MONITOR_STAGE__FAIL:
        {
            ping_info.ok = false ;
            ping_info.send_retries = 0 ;
            ping_info.monitoring = false ;
            pingUtil_fini (ping_info);
            pingUtil_init (ping_info.hostname, ping_info, ping_info.ip.data());

            mtcTimer_reset ( ping_info.timer );
            mtcTimer_start ( ping_info.timer, ping_info.timer_handler, PING_MONITOR_INTERVAL );
            ping_info.stage = PINGUTIL_MONITOR_STAGE__WAIT;
            break ;
        }
        default:
        {
            slog ("%s default case (%d)\n", ping_info.hostname.c_str(), ping_info.stage );

            /* Default to check the connection.
             * Failure case is handled there */
            mtcTimer_reset ( ping_info.timer );

            ping_info.stage = PINGUTIL_MONITOR_STAGE__FAIL ;
        }
    }
    return(PASS);
}




#ifdef WANT_MAIN
/*--------------------------------------------------------------------*/
/*--- main - look up host and start ping processes.                ---*/
/*--------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    int rc ;
    int ping_socket   = 0;
    int sequence = 1 ;
    struct sockaddr_in addr_ping ;

    if ( argc > 1 )
    {
        int identity = getpid() ;
        printf ( "\npinging %s\n", argv[1]);
        if ( ( rc = pingUtil_init ( argv[1], ping_socket , addr_ping )) == 0 )
        {
            pingUtil_recv ( ping_socket, identity, sequence, false );
            if ( ( rc = pingUtil_send ( ping_socket, &addr_ping, identity, sequence )) == 0 )
            {
                for ( int loop=0;loop < 10; loop++)
                {
                    usleep(300000);
                    if ( ( rc = pingUtil_recv ( ping_socket, identity, sequence, true ) ) == 0 )
                    {
                        printf("Ping OK.\n");
                        return 0;
                    }
                    else
                    {
                        printf ("receive failed (%d)\n", rc );
                    }
                }
                printf("Ping FAILED !!\n");
            }
            else
            {
                printf ("ping send Failed (%d)\n", rc );
            }
        }
        else
        {
            printf ("ping init failed (%d)\n", rc );
        }
    }
    pingUtil_close ( ping_socket );
    return 0;
}

#endif
