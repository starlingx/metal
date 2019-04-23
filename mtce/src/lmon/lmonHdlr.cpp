/*
 * Copyright (c) 2019 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Starling-X Maintenance Link Monitor Service Header
  */

#include "lmon.h"
#include <linux/rtnetlink.h> /* for ... RTMGRP_LINK                         */
#include "nodeMacro.h"       /* for ... CREATE_REUSABLE_INET_UDP_TX_SOCKET  */

#define HTTP_SERVER_NAME ((const char *)"link status query")


static lmon_ctrl_type lmon_ctrl ;

static interface_ctrl_type interfaces[INTERFACES_MAX];

static const char * iface_list[INTERFACES_MAX] = { MGMT_INTERFACE_NAME,
                                                   CLUSTER_HOST_INTERFACE_NAME,
                                                   OAM_INTERFACE_NAME };

/* httpUtil needs a mtclog socket pointer */
msgSock_type * get_mtclogd_sockPtr ( void )
{
    return (&lmon_ctrl.mtclogd);
}

/* dump state before exiting */
void daemon_exit ( void )
{
    daemon_files_fini ();
    daemon_dump_info  ();
    exit (0);
}

/* daemon timer handler */
void lmonTimer_handler ( int sig, siginfo_t *si, void *uc)
{
    UNUSED(sig); UNUSED(uc);
    timer_t * tid_ptr = (void**)si->si_value.sival_ptr ;
    if ( !(*tid_ptr) )
        return ;
    else if (( *tid_ptr == lmon_ctrl.audit_timer.tid ) )
        lmon_ctrl.audit_timer.ring = true ;
    else
        mtcTimer_stop_tid_int_safe ( tid_ptr );
}

/*****************************************************************************
 *
 * Name       : lmonHdlr_http_handler
 *
 * Description: Handle HTTP Link Status Query requests.
 *
 * Method     : GET
 *
 ******************************************************************************/

#define DOS_LOG_THROTTLE_THLD (10000)
void lmonHdlr_http_handler (struct evhttp_request *req, void *arg)
{
    int http_status_code = HTTP_NOTFOUND ;

    UNUSED(arg);

    if ( ! req )
        return;

    /* Get sender must be localhost */
    const char * host_ptr = evhttp_request_get_host (req);
    if (( host_ptr == NULL ) ||
        (( strncmp ( host_ptr , "localhost" , 10 ) != 0 ) &&
         ( strncmp ( host_ptr , LOOPBACK_IP , 10 ) != 0 )))
    {
        wlog_throttled ( lmon_ctrl.dos_log_throttle,
                         DOS_LOG_THROTTLE_THLD,
                         "Message received from unknown host (%s)\n",
                         host_ptr?host_ptr:"(null)" );
        return ;
    }

    const char * uri_ptr = evhttp_request_get_uri (req);
    if ( uri_ptr == NULL )
    {
        wlog_throttled ( lmon_ctrl.dos_log_throttle,
                         DOS_LOG_THROTTLE_THLD,
                         "null uri");
        return ;
    }

    string uri_path = daemon_get_cfg_ptr()->uri_path ;
    if (strncmp(uri_ptr, uri_path.data(), uri_path.length()))
    {
        wlog_throttled ( lmon_ctrl.dos_log_throttle,
                         DOS_LOG_THROTTLE_THLD,
                         "http request not for this service: %s",
                         uri_ptr);
        return ;
    }

    /* Extract the operation */
    evhttp_cmd_type http_cmd = evhttp_request_get_command (req);
    jlog ("'%s' %s\n", uri_ptr, getHttpCmdType_str(http_cmd));
    switch ( http_cmd )
    {
        case EVHTTP_REQ_GET:
        {
            http_status_code = HTTP_OK ;
            break ;
        }
        default:
        {
            ilog_throttled ( lmon_ctrl.dos_log_throttle,
                             DOS_LOG_THROTTLE_THLD,
                             "unsupported %s request (%d)",
                             getHttpCmdType_str(http_cmd),
                             http_cmd);

            http_status_code = MTC_HTTP_METHOD_NOT_ALLOWED ;
        }
    }

    /*
     * Link status query response format - success path
     *
     * Lagged case has an array of 2 links
     *

{ "status" : "pass",
   "link_info": [
      { "network":"mgmt",
        "type":"vlan",
        "links": [
              { "name":"enp0s8.1", "state":"Up/Down", "time":5674323454567 },
              { "name":"enp0s8.2", "state":"Up/Down", "time":5674323454567 }]
      },
      { "network":"cluster-host",
        "type":"bond",
        "bond":"bond0",
        "links": [
              { "name":"enp0s9f1", "state":"Up/Down", "time":5674323454567 },
              { "name":"enp0s9f0", "state":"Up/Down", "time":5674323454567 }]
      },
      { "network":"oam",
        "type":"ethernet",
        "links": [
              { "name":"enp0s3", "state":"Up/Down", "time":5674323454567 }]
      }]
}
     *
     *lmonHdlr.cpp
     *
     */

// #define WANT_TYPE_STR
// #define WANT_BOND_STR

    if (( http_status_code == HTTP_OK ) || ( http_status_code == MTC_HTTP_ACCEPTED ))
    {
        /* build response string */
        string response = "{ \"status\":\"pass\",\"link_info\":[" ;

        /* loop over the interfaces and build a response string for each
         * of those that are used */
        for ( int i = 0 ; i < INTERFACES_MAX ; i++ )
        {
            if ((interfaces[i].used == true) && (interfaces[i].name[0] != '\0'))
            {
                if ( i > 0 )
                    response.append (",");

                response.append ("{\"network\":\"");
                response.append (interfaces[i].name);
                response.append ("\"");

#ifdef WANT_TYPE_STR
                string type_str = "ethernet" ;
                if ( interfaces[i].type_enum == bond )
                    type_str = "bond" ;
                else if ( interfaces[i].type_enum == vlan )
                    type_str = "vlan" ;
                response.append (",\"type\":\"" + type_str + "\"");
#endif

#ifdef WANT_BOND_STR
                if ( interfaces[i].type_enum == bond )
                {
                    response.append (",\"bond\":\"");
                    response.append (interfaces[i].bond);
                    response.append ("\"");
                }
#endif

                response.append (",\"links\":[");
                {
                    response.append ("{\"name\":\"");
                    response.append (interfaces[i].interface_one);
                    response.append ("\",\"state\":\"");
                    response.append (interfaces[i].interface_one_link_up?"Up":"Down");
                    response.append ("\",\"time\":\"" + lltos(interfaces[i].interface_one_event_time) + "\"}");
                }
                if (( interfaces[i].lagged ) &&
                    ( interfaces[i].interface_two[0] != '\0'))
                {
                    response.append (",{\"name\":\"");
                    response.append (interfaces[i].interface_two);
                    response.append ("\",\"state\":\"");
                    response.append (interfaces[i].interface_two_link_up?"Up":"Down");
                    response.append ("\",\"time\":\"" + lltos(interfaces[i].interface_two_event_time) + "\"}");
                }
                response.append ("]}");
            }
        }
        response.append ("]}");

        struct evbuffer * resp_buf = evbuffer_new();
        jlog ("Resp: %s\n", response.c_str());
        evbuffer_add_printf (resp_buf, "%s\n", response.data());
        evhttp_send_reply (req, http_status_code, "OK", resp_buf );
        evbuffer_free ( resp_buf );
    }
    else if ( http_status_code == MTC_HTTP_METHOD_NOT_ALLOWED )
    {
        /* build response string */
        string response = "{" ;
        response.append (" \"status\" : \"fail ; method not allowed\"");
        response.append ("}");

        struct evbuffer * resp_buf = evbuffer_new();
        jlog ("Event Response: %s\n", response.c_str());
        evbuffer_add_printf (resp_buf, "%s\n", response.data());

        /* Add the 'Allow' header */
        int rc = evhttp_add_header( req->output_headers, "Allow", "GET" );
        if ( rc ) { ilog ("failed to add 'Allow' header (%d %d:%m", rc, errno);}
        evhttp_send_reply (req, http_status_code, "NOT ALLOWED", resp_buf );
        evbuffer_free ( resp_buf );
    }
    else
    {
        /* build response string */
        string response = "{" ;
        response.append (" \"status\" : \"fail ; bad request\"");
        response.append ("}");
        elog ("HTTP Event error:%d ; cmd:%s uri:%s response:%s\n",
               http_status_code,
               getHttpCmdType_str(http_cmd),
               uri_ptr,
               response.c_str());
        evhttp_send_error (req, http_status_code, response.data() );
    }
}

/*****************************************************************************
 *
 * Name    : lmon_socket_init
 *
 * Purpose : Initialize all the sockets for this process.
 *
 *           Sockets include ...
 *
 *           1. local kernel ioctl socket ; link attribute query
 *
 *****************************************************************************/

int lmon_socket_init ( lmon_ctrl_type * ctrl_ptr )
{
    int rc = PASS ;
    if ( ctrl_ptr )
    {
        httpUtil_event_init ( &lmon_ctrl.http_event,
                               &lmon_ctrl.my_hostname[0],
                               HTTP_SERVER_NAME,
                               lmon_ctrl.my_address,
                               daemon_get_cfg_ptr()->lmon_query_port );

        if (( ctrl_ptr->ioctl_socket = open_ioctl_socket()) <= 0 )
        {
            /* errno/strerror logged by open utility if failure is detected */
            elog ("failed to create ioctl socket\n");
            rc = FAIL_SOCKET_CREATE ;
        }

        /* Note that address changes should not generate netlink events.
         * Therefore these options are not set
         *     RTMGRP_IPV4_IFADDR
         *     RTMGRP_IPV6_IFADDR
         */
        else if (( ctrl_ptr->netlink_socket = open_netlink_socket ( RTMGRP_LINK )) <= 0 )
        {
            /* errno/strerr logged by open utility if failure is detected */
            elog ("failed to create netlink listener socket\n");
            rc = FAIL_SOCKET_CREATE ;
        }
        else if ( httpUtil_setup ( ctrl_ptr->http_event,
                                    HTTP_SUPPORTED_METHODS,
                                   &lmonHdlr_http_handler ) != PASS )
        {
            /* errno/strerr logged by open utility if failure is detected */
            elog ("failed to setup http server\n");
            rc = FAIL_SOCKET_CREATE ;
        }
        else
        {
            ctrl_ptr->mtclogd.port = daemon_get_cfg_ptr()->daemon_log_port ;
            CREATE_REUSABLE_INET_UDP_TX_SOCKET ( LOOPBACK_IP,
                                             ctrl_ptr->mtclogd.port,
                                             ctrl_ptr->mtclogd.sock,
                                             ctrl_ptr->mtclogd.addr,
                                             ctrl_ptr->mtclogd.port,
                                             ctrl_ptr->mtclogd.len,
                                             "mtc logger message",
                                             rc );
            if ( rc )
            {
                elog ("failed to setup mtce logger port %d\n", ctrl_ptr->mtclogd.port );
                rc = PASS ;
            }
        }
    }
    else
    {
        rc = FAIL_NULL_POINTER ;
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name    : lmon_learn_interfaces
 *
 * Purpose : realize the interfaces to monitor in terms of
 *
 *           - interface type ; ethernet, bonded or vlan
 *           - initial up/down state
 *
 *****************************************************************************/

void lmon_learn_interfaces ( int ioctl_socket )
{
    /* initialize interface monitoring */
    for ( int iface = 0 ; iface < INTERFACES_MAX ; iface++ )
    {
        interfaces[iface].name = iface_list[iface];
        lmon_interfaces_init ( &interfaces[iface] );

        if ( interfaces[iface].used == false )
            continue ;

        /* set the link state for all the primary physical interfaces */
        if ( lmon_get_link_state ( ioctl_socket,
                                   interfaces[iface].interface_one,
                                   interfaces[iface].interface_one_link_up ) )
        {
            interfaces[iface].interface_one_event_time = lmon_fm_timestamp();
            interfaces[iface].interface_one_link_up = false ;
            wlog ("%s interface state query failed ; defaulting to Down\n",
                      interfaces[iface].interface_one) ;
        }
        else
        {
            interfaces[iface].interface_one_event_time = lmon_fm_timestamp();
            ilog ("%s is %s\n",
                      interfaces[iface].interface_one,
                      interfaces[iface].interface_one_link_up ?
                      "Up" : "Down" );

            if ( interfaces[iface].lagged == true )
            {
                /* set the link state for all the lagged physical interfaces */
                if ( lmon_get_link_state ( ioctl_socket,
                                           interfaces[iface].interface_two,
                                           interfaces[iface].interface_two_link_up ) )
                {
                    interfaces[iface].interface_two_event_time = lmon_fm_timestamp();
                    interfaces[iface].interface_two_link_up = false ;
                    wlog ("%s lag interface state query failed ; defaulting to Down\n",
                              interfaces[iface].interface_two) ;
                }
                else
                {
                    interfaces[iface].interface_two_event_time = lmon_fm_timestamp();
                    ilog ("%s is %s (lag)\n",
                              interfaces[iface].interface_two,
                              interfaces[iface].interface_two_link_up ?
                              "Up" : "Down" );
                }
            }
        }
    }
}

/*****************************************************************************
 *
 * Name       : service_interface_events
 *
 * Purpose    : Service state changes for monitored link
 *
 * Description: netlink event driven state change handler.
 *
 *****************************************************************************/
int service_interface_events ( void )
{
    list<string> links_gone_down ;
    list<string> links_gone_up   ;
    list<string>::iterator iter_ptr ;

    links_gone_down.clear();
    links_gone_up.clear();

    int events = get_netlink_events ( lmon_ctrl.netlink_socket,
                                  links_gone_down,
                                  links_gone_up );
    if ( events <= 0 )
    {
        dlog1 ("called but get_netlink_events reported no events");
        return RETRY ;
    }

    for ( int i = 0 ; i < INTERFACES_MAX ; i++ )
    {
        if ( interfaces[i].used == true )
        {
            bool running = false ;

            /* handle links that went down */
            if ( ! links_gone_down.empty() )
            {
                bool found = false ;
                dlog ("netlink Down events: %ld", links_gone_down.size());
                /* Look at the down list */
                for ( iter_ptr  = links_gone_down.begin();
                      iter_ptr != links_gone_down.end() ;
                      iter_ptr++ )
                {
                    if ( strcmp ( interfaces[i].interface_one, iter_ptr->c_str()) == 0 )
                    {
                        found = true ;
                        interfaces[i].interface_one_event_time = lmon_fm_timestamp();

                        dlog ("%s is Down ; netlink event\n",
                                  interfaces[i].interface_one );

                        if ( get_link_state ( lmon_ctrl.ioctl_socket,
                                              iter_ptr->c_str(),
                                              &running ) == PASS )
                        {
                            if ( interfaces[i].interface_one_link_up == true )
                            {
                                wlog ("%s is Down ; (%s)\n",
                                        iter_ptr->c_str(),
                                        running ? "Up" : "Down" );
                            }
                            else
                            {
                                dlog ("%s is Down ; (%s)\n",
                                        iter_ptr->c_str(),
                                        running ? "Up" : "Down" );
                            }
                            interfaces[i].interface_one_link_up = running ? true:false;
                        }
                        else
                        {
                            wlog ("%s is Down ; oper query failed\n",
                                      iter_ptr->c_str());
                            interfaces[i].interface_one_link_up = false ;
                        }
                    }

                    else if (interfaces[i].lagged == true)
                    {
                        if ( strcmp ( interfaces[i].interface_two, iter_ptr->c_str()) == 0 )
                        {
                            found = true ;
                            interfaces[i].interface_two_event_time = lmon_fm_timestamp();

                            dlog ("%s is Down\n", interfaces[i].interface_two);

                            if ( get_link_state ( lmon_ctrl.ioctl_socket,
                                                  iter_ptr->c_str(),
                                                  &running ) == PASS )
                            {
                                if ( interfaces[i].interface_two_link_up == true )
                                {
                                    wlog ("%s is Down (%s)\n",
                                            iter_ptr->c_str(),
                                            running ? "Up" : "Down" );
                                }
                                else
                                {
                                    dlog ("%s is Down (%s)\n",
                                            iter_ptr->c_str(),
                                            running ? "Up" : "Down" );
                                }
                                interfaces[i].interface_two_link_up = running ? true:false;
                            }
                            else
                            {
                                wlog ("%s is Down ; oper query failed\n",
                                          iter_ptr->c_str() );
                                interfaces[i].interface_two_link_up = false ;
                            }
                        }
                        if ( strcmp ( interfaces[i].bond, iter_ptr->c_str()) == 0 )
                        {
                            found = true ;
                            wlog ("%s is Down (bond)\n", interfaces[i].bond);
                        }
                    }
                }
                if ( ! found )
                {
                    dlog ("netlink Down event on unmonitored link:%s", iter_ptr->c_str());
                }
            }
            /* handle links that came up */
            if ( !links_gone_up.empty() )
            {
                bool found = false ;
                dlog ("netlink Up events: %ld", links_gone_up.size());
                /* Look at the down list */
                for ( iter_ptr  = links_gone_up.begin();
                      iter_ptr != links_gone_up.end() ;
                      iter_ptr++ )
                {
                    if ( strcmp ( interfaces[i].interface_one, iter_ptr->c_str()) == 0 )
                    {
                        found = true ;
                        interfaces[i].interface_one_event_time = lmon_fm_timestamp();

                        dlog ("%s is Up\n", interfaces[i].interface_one );

                        if ( get_link_state ( lmon_ctrl.ioctl_socket,
                                              iter_ptr->c_str(),
                                              &running ) == PASS )
                        {
                            if ( interfaces[i].interface_one_link_up == false )
                            {
                                ilog ("%s is Up   (%s)\n",
                                          iter_ptr->c_str(),
                                          running ? "Up" : "Down" );
                            }
                            else
                            {
                                dlog ("%s is Up   (%s)\n",
                                          iter_ptr->c_str(),
                                          running ? "Up" : "Down" );
                            }
                            interfaces[i].interface_one_link_up = running ? true:false;
                        }
                        else
                        {
                            wlog ("%s is Down ; oper query failed\n", iter_ptr->c_str() );
                            interfaces[i].interface_one_link_up = false ;
                        }
                    }
                    else if (interfaces[i].lagged == true)
                    {
                        if ( strcmp ( interfaces[i].interface_two, iter_ptr->c_str()) == 0 )
                        {
                            found = true ;
                            interfaces[i].interface_two_event_time = lmon_fm_timestamp();
                            dlog ("%s is Up\n", interfaces[i].interface_two );

                            if ( get_link_state ( lmon_ctrl.ioctl_socket,
                                                  iter_ptr->c_str(),
                                                  &running ) == PASS )
                            {
                                if ( interfaces[i].interface_two_link_up == false )
                                {
                                    ilog ("%s is Up   (%s)\n",
                                              iter_ptr->c_str(),
                                              running ? "Up" : "Down" );
                                }
                                else
                                {
                                    dlog ("%s is Up   (%s)\n",
                                              iter_ptr->c_str(),
                                              running ? "Up" : "Down" );
                                }
                                interfaces[i].interface_two_link_up = running ? true:false;
                            }
                            else
                            {
                                wlog ("%s is Down ; oper query failed\n", iter_ptr->c_str() );
                                interfaces[i].interface_two_link_up = false ;
                            }
                        }
                        if ( strcmp ( interfaces[i].bond, iter_ptr->c_str()) == 0 )
                        {
                            found = true ;
                            wlog ("%s is Up   (bond)\n", interfaces[i].bond);
                        }
                    }
                }
                if ( ! found )
                {
                    dlog ("netlink Up event on unmonitored link:%s", iter_ptr->c_str());
                }
            }
        }
    }
    return (PASS);
}


/**************************************************************************
 *
 * Name     : lmon_query_all_links
 *
 * Purpose  : self correct for netlink event misses by running this
 *            as a periodic audit at a 1 minute cadence.
 *
 **************************************************************************/
void lmon_query_all_links( void )
{
    dlog1 ("audit timer fired");

    for ( int i = 0 ; i < INTERFACES_MAX ; i++ )
    {
        if ( interfaces[i].used )
        {
            bool link_up = false ;
            string log_msg = "link state mismatch detected by audit";
            if ( lmon_get_link_state ( lmon_ctrl.ioctl_socket,
                                       interfaces[i].interface_one,
                                       link_up) == PASS )
            {
                if ( link_up != interfaces[i].interface_one_link_up )
                {
                    wlog ("%s %s ; is:%s was:%s ; corrected",
                              interfaces[i].interface_one,
                              log_msg.c_str(),
                              link_up?"Up":"Down",
                              interfaces[i].interface_one_link_up?"Up":"Down" );

                    interfaces[i].interface_one_event_time = lmon_fm_timestamp();
                    interfaces[i].interface_one_link_up = link_up ;
                }
            }
            if ( interfaces[i].lagged )
            {
                if ( lmon_get_link_state ( lmon_ctrl.ioctl_socket,
                                           interfaces[i].interface_two,
                                           link_up) == PASS )
                {
                    if ( link_up != interfaces[i].interface_two_link_up )
                    {
                        wlog ("%s %s ; is:%s was:%s ; corrected",
                                  interfaces[i].interface_two,
                                  log_msg.c_str(),
                                  link_up?"Up":"Down",
                                  interfaces[i].interface_two_link_up?"Up":"Down" );

                        interfaces[i].interface_two_event_time = lmon_fm_timestamp();
                        interfaces[i].interface_two_link_up = link_up ;
                    }
                }
            }
        }
    }
}

/*****************************************************************************
 *
 * Name    : daemon_service_run
 *
 * Purpose : track interface profile link status.
 *
 * Assumptions: Event driven with self-correcting audit.
 *
 * General Behavior:
 *
 *   Init:
 *
 *   1. learn interface/port model
 *   2. setup http server
 *
 *   Select:
 *
 *   3. load initial link status for learned links
 *   4. listen for link status change events
 *   5. provide link status info to http GET Query requests.
 *
 *   Audit:
 *
 *   6. run 1 minute periodic self correcting audit.
 *
 */

void daemon_service_run ( void )
{
    fd_set readfds;
    struct timeval waitd;
    std::list<int> socks;

    lmon_ctrl.ioctl_socket = 0 ;
    lmon_ctrl.netlink_socket = 0 ;
    memset (&lmon_ctrl.mtclogd, 0, sizeof(lmon_ctrl.mtclogd));
    memset (&interfaces, 0, sizeof(interface_ctrl_type));

    get_hostname (&lmon_ctrl.my_hostname[0], MAX_HOST_NAME_SIZE );
    mtcTimer_init ( lmon_ctrl.audit_timer, lmon_ctrl.my_hostname, "audit");

    string my_address = lmon_ctrl.my_address ;
    get_iface_address ( daemon_mgmnt_iface().data(), my_address, true );

    /* Setup the messaging sockets */
    if (( lmon_socket_init ( &lmon_ctrl )) != PASS )
    {
        elog ("socket initialization failed ; exiting ...\n");
        daemon_exit ();
    }
    else if ( 0 >= lmon_ctrl.netlink_socket )
    {
        elog ("failed to get ioctl socket descriptor (%d) ; exiting ...\n",
               lmon_ctrl.netlink_socket );
        daemon_exit ();
    }

    lmon_learn_interfaces ( lmon_ctrl.ioctl_socket );

    int audit_secs = daemon_get_cfg_ptr()->audit_period ;
    ilog ("started %d second link state self correcting audit", audit_secs );
    mtcTimer_start ( lmon_ctrl.audit_timer, lmonTimer_handler, audit_secs );

    socks.clear();
    socks.push_front (lmon_ctrl.netlink_socket);
    socks.sort();

    ilog ("waiting on netlink events ...");

    for (;;)
    {
        /* Accomodate for hup reconfig */
        FD_ZERO(&readfds);
        FD_SET(lmon_ctrl.netlink_socket, &readfds);
        waitd.tv_sec  = 0;
        waitd.tv_usec = SOCKET_WAIT ;

        /* This is used as a delay up to select timeout ; SOCKET_WAIT */
        select( socks.back()+1, &readfds, NULL, NULL, &waitd);
        if (FD_ISSET(lmon_ctrl.netlink_socket, &readfds))
        {
            dlog ("netlink socket fired\n");
            service_interface_events ();
        }

        if ( lmon_ctrl.audit_timer.ring == true )
        {
            lmon_ctrl.audit_timer.ring = false ;
            lmon_query_all_links();
        }

        httpUtil_look ( lmon_ctrl.http_event );
        daemon_signal_hdlr();
    }
    daemon_exit();
}




