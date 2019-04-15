/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform netlink listener event support for maintenance
  */

#include <asm/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <list>

using namespace std;

#include "nodeBase.h"
#include "nodeUtil.h"

int get_netlink_events_throttle = 0 ;
int get_netlink_events ( int nl_socket , std::list<string> & links_gone_down, 
                                         std::list<string> & links_gone_up )
{
    char buf[4096];
    char name[IF_NAMESIZE];

    int len ;
    int ret = 0;
  
    struct sockaddr_nl sa;
    struct iovec iov = { buf, sizeof (buf) };
    struct msghdr msg = { &sa, sizeof (sa), &iov, 1, NULL, 0, 0 };
    struct nlmsghdr *h;
    struct ifinfomsg *ifi;
    /* struct ifaddrmsg * ifa ; used for addr change events */

    links_gone_up.clear();
    links_gone_down.clear();
    
    len = recvmsg (nl_socket, &msg, 0);
    if (len < 0)
    {
        /* Socket non-blocking so bail out once we have read everything */
        if ( (errno == EWOULDBLOCK) || (errno == EAGAIN))
        {
            return ret ;
        }
      
        /* Anything else is an error */
        elog ("failed netlink recvmsg (%d:%d) (%d:%s)\n", nl_socket, len, errno, strerror(errno));
        return len;
    }

    if (len == 0)
    {
        wlog ("No netlink data read_netlink: EOF\n");
    }

    /* Handle all the messages from 'recvmsg' */
    h = (struct nlmsghdr *) &buf[0] ;
    for ( ; NLMSG_OK (h,(unsigned int)len); h=NLMSG_NEXT (h,len))
    {
        /* ignore address change events */
        if (h->nlmsg_type == RTM_NEWADDR )
        {

#ifdef RTM_NEWADDR_SUPPORTED
            ifa = (struct ifaddrmsg *) NLMSG_DATA (nlh);

            struct rtattr * rth = IFA_RTA (ifa);
            int rtl = IFA_PAYLOAD (nlh);
            for (;rtl && RTA_OK (rth, rtl); rth = RTA_NEXT (rth,rtl))
            {
                char name[IFNAMSIZ];
                uint32_t ipaddr;

                if (rth->rta_type != IFA_LOCAL) continue;

                ipaddr = * ((uint32_t *)RTA_DATA(rth));
                ipaddr = htonl(ipaddr);

                fprintf (stdout,"%s is now %X\n",if_indextoname(ifa->ifa_index,name),ipaddr);
            }
#else
            dlog ("unsupported netlink event: RTM_NEWADDR\n"); 
            continue ;
#endif
        }

        /* Finish reading */
        if (h->nlmsg_type == NLMSG_NOOP )
        {
            ilog ("netlink message: Nothing to read\n");
            return ret;
        }

        /* Finish reading */
        if (h->nlmsg_type == NLMSG_DONE)
        {
            ilog ("netlink message: No more messages\n");
            return ret;
        }
      
        /* Message is some kind of error */
        if (h->nlmsg_type == NLMSG_ERROR)
        {
            wlog ("netlink message: indicates error\n");
            return -1;
        }

        ifi = (ifinfomsg*) NLMSG_DATA (h);
        memset ( name, 0 , IF_NAMESIZE );
        if ( ifi->ifi_index )
        {
            if_indextoname(ifi->ifi_index, name);
            if (ifi->ifi_flags & IFF_RUNNING)
            {
                /* if 'up' then remove interface from 'down' list and add it to the 'up' list */
                links_gone_down.remove(name);
                links_gone_up.push_front(name);
                dlog ( "%s is up and running \n", name );
            }
            else
            {
                if ( ifi->ifi_flags & IFF_UP )
                {
                    dlog ("%s is admin:up but oper:down\n", name );
                }
                else
                {
                    dlog ("%s is admin:down and oper:down\n", name );
                }

                /* if 'down' then remove interface from 'up' list and add it to the 'down' list */
                links_gone_up.remove(name);
                links_gone_down.push_front(name);
            }
            get_netlink_events_throttle = 0 ;
        }
        else
        {
            wlog_throttled (get_netlink_events_throttle, 100, "got netlink event for unknown interface index\n");
        }
        ret++ ;
    }
    links_gone_up.unique();
    links_gone_down.unique();

    return ret;
}


void log_link_events ( int netlink_sock,
                       int ioctl_sock, 
                       const char * mgmnt_iface_ptr, 
                       const char * clstr_iface_ptr,
                       bool & mgmnt_link_up_and_running,
                       bool & clstr_link_up_and_running)
{
    std::list<string> links_gone_down ;
    std::list<string> links_gone_up   ;
    std::list<string>::iterator iter_curr_ptr ;
    dlog3 ("logging for interfaces %s and %s\n", mgmnt_iface_ptr, clstr_iface_ptr);
    if ( get_netlink_events ( netlink_sock, links_gone_down, links_gone_up )) 
    {
        bool running = false ;
        if ( !links_gone_down.empty() )
        {
            dlog3 ("%ld links have dropped\n", links_gone_down.size() );
           
            /* Look at the down list */
            for ( iter_curr_ptr  = links_gone_down.begin();
                  iter_curr_ptr != links_gone_down.end() ;
                  iter_curr_ptr++ )
            {
                dlog3 ( "downed link: %s (running:%d:%d)\n", 
                        iter_curr_ptr->c_str(), 
                        mgmnt_link_up_and_running, 
                        clstr_link_up_and_running );

                if ( !strcmp (mgmnt_iface_ptr, iter_curr_ptr->data()))
                {
                    if ( mgmnt_link_up_and_running == true )
                    {
                        mgmnt_link_up_and_running = false ; 
                        wlog ("Mgmnt link %s is down\n", mgmnt_iface_ptr );
                    }
                }
                if ( !strcmp (clstr_iface_ptr, iter_curr_ptr->data()))
                {
                    if ( clstr_link_up_and_running == true )
                    {
                        clstr_link_up_and_running = false ;
                        wlog ("Cluster-host link %s is down\n", clstr_iface_ptr );
                    }
                }
     
                if ( get_link_state ( ioctl_sock, iter_curr_ptr->data(), &running ) == PASS )
                {
                   dlog ("%s is down (oper:%s)\n", iter_curr_ptr->c_str(), running ? "up" : "down" );
                }
                else
                {
                    wlog ("%s is down (driver query failed)\n", iter_curr_ptr->c_str() );
                }
            }
        }
        if ( !links_gone_up.empty() )
        {
            dlog3 ("%ld links have recovered\n", links_gone_up.size());

            /* Look at the up list */
            for ( iter_curr_ptr  = links_gone_up.begin();
                  iter_curr_ptr != links_gone_up.end() ;
                  iter_curr_ptr++ )
            {
                dlog3 ( "recovered link: %s (running:%d:%d)\n", 
                        iter_curr_ptr->c_str(), 
                        mgmnt_link_up_and_running, 
                        clstr_link_up_and_running );

                if ( !strcmp (mgmnt_iface_ptr, iter_curr_ptr->data()))
                {
                    mgmnt_link_up_and_running = true ; 
                    wlog ("Mgmnt link %s is up\n", mgmnt_iface_ptr );
                }
                if ( !strcmp (clstr_iface_ptr, iter_curr_ptr->data()))
                {
                    clstr_link_up_and_running = true ;
                    wlog ("Cluster-host link %s is up\n", clstr_iface_ptr );
                }

                if ( get_link_state ( ioctl_sock, iter_curr_ptr->data(), &running ) == PASS )
                {
                    dlog ("%s is up (oper:%s)\n", 
                            iter_curr_ptr->c_str(), 
                            running ? "up" : "down" );
                }
                else
                {
                    wlog ("%s is up (driver query failed)\n", iter_curr_ptr->c_str() );
                }
            }
        }
    }
}




/* Open a netlink listener socket and return that socket id.
 * Return 0 on create or bind failure */
int open_netlink_socket ( int groups )
{
    struct sockaddr_nl addr;
    int on = 1 ;
    ilog ( "NLMon Groups: %d\n", groups ) ;

    int nl_socket = socket (AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nl_socket < 0)
    {
        elog ("Failed to open netlink socket (%d:%s)\n", errno, strerror(errno));
        return (0);
    }

    else if ( 0 > ioctl( nl_socket, FIONBIO, (char *)&on))
    {
       elog ("failed to set 'netlink monitor' socket non-blocking (%d:%m)\n", errno );
       close (nl_socket);
       nl_socket = 0 ;
    }
    else
    {
        memset ((void *) &addr, 0, sizeof (addr));

        addr.nl_family = AF_NETLINK;
        addr.nl_pid = getpid ();
        /* addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR; */
        addr.nl_groups = groups ; /* allow the caller to specify the groups */
    
        if (bind (nl_socket, (struct sockaddr *) &addr, sizeof (addr)) < 0)
        {  
            elog ( "Failed to bind netlink socket (%d:%s)\n", errno, strerror(errno));
            close (nl_socket);
            nl_socket = 0 ;
        }
    }
    return (nl_socket);
}

void close_netlink_socket ( int socket )
{
    if ( socket )
    {
        close (socket);
    }
}
