/*
* Copyright (c) 2013-2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/


#define CREATE_REUSABLE_INET_UDP_TX_SOCKET(ip, port, s, a, p, l, n, rc) \
{                                                  \
    int on = 1 ;                                   \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  \
    if ( 0 >= s )                                  \
    {                                              \
        elog ("failed to create '%s' socket (%d:%s)\n", n, errno, strerror(errno)); \
        rc = FAIL_SOCKET_CREATE ;                  \
    }                                              \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                              \
        elog ("failed to make '%s' socket re-useable (%d:%s)\n", n, errno, strerror(errno)); \
        close(s);                                  \
        s = 0 ;                                    \
        rc = FAIL_SOCKET_OPTION ;                  \
    }                                              \
    else                                           \
    {                                              \
        memset(&a, 0, sizeof(struct sockaddr_in)); \
        l = sizeof(a);                             \
        p = port ;                                 \
        a.sin_family = AF_INET ;                   \
        a.sin_addr.s_addr = inet_addr(ip);         \
        a.sin_port = htons(p) ;                    \
        ilog ("Transmitting: '%s' socket %s:%d\n", n, inet_ntoa(a.sin_addr), p); \
    }                                              \
}

#define CREATE_NONBLOCK_INET_UDP_RX_SOCKET(ip, port, s, a, p, l, n, rc) \
{                                                  \
    int on = 1 ;                                   \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  \
    if ( 0 >= s )                                  \
    {                                              \
        elog ("failed to create '%s' socket (%d:%s)\n", n, errno, strerror(errno)); \
        rc = FAIL_SOCKET_CREATE ;                  \
    }                                              \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                              \
        elog ("failed to make '%s' socket re-useable (%d:%s)\n", n, errno, strerror(errno)); \
        close(s);                                  \
        s = 0 ;                                    \
        rc = FAIL_SOCKET_OPTION ;                  \
    }                                              \
    else if ( 0 > ioctl(s, FIONBIO, (char *)&on))  \
    {                                              \
       elog ("failed to set '%s' socket non-blocking (%d:%s)\n", n, errno, strerror(errno)); \
       close(s);                                   \
       s = 0 ;                                     \
       rc = FAIL_SOCKET_NOBLOCK ;                  \
    }                                              \
    else                                           \
    {                                              \
        memset(&a, 0, sizeof(a));                  \
        l = sizeof(a);                             \
        p = port ;                                 \
        a.sin_family = AF_INET ;                   \
        a.sin_addr.s_addr = inet_addr(ip);         \
        a.sin_port = htons(p) ;                    \
        if ( bind ( s, (const struct sockaddr *)&a, sizeof(struct sockaddr_in)) == -1 ) \
        {                                          \
            elog ( "failed to bind '%s' socket to port %d (%d:%s)\n", n, p, errno, strerror(errno) ); \
            close  (s);                            \
            s = 0 ;                                \
            rc = FAIL_SOCKET_BIND ;                \
        }                                          \
        rc = PASS ;                                \
        ilog ("Listening On: '%s' socket %s:%d\n", n, inet_ntoa(a.sin_addr), p); \
    }                                              \
}

/* Non-Blocking Receive From ANY IP on 'port' */ 
#define CREATE_NONBLOCK_INET_UDP_CMD_RX_SOCKET(port, s, a, n, rc) \
{                                                  \
    int on = 1 ;                                   \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  \
    if ( 0 >= s )                                  \
    {                                              \
        elog ("failed to create '%s' socket (%d:%m)\n", n, errno); \
        rc = FAIL_SOCKET_CREATE ;                  \
    }                                              \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                              \
        elog ("failed to make '%s' socket re-useable (%d:%m)\n", n, errno); \
        close(s);                                  \
        s = 0 ;                                    \
        rc = FAIL_SOCKET_OPTION ;                  \
    }                                              \
    else if ( 0 > ioctl(s, FIONBIO, (char *)&on))  \
    {                                              \
       elog ("failed to set '%s' socket non-blocking (%d:%m)\n", n, errno); \
       close(s);                                   \
       s = 0 ;                                     \
       rc = FAIL_SOCKET_NOBLOCK ;                  \
    }                                              \
    else                                           \
    {                                              \
        memset(&a, 0, sizeof(a));                  \
        a.sin_family = AF_INET ;                   \
        a.sin_addr.s_addr = htonl(INADDR_ANY);     \
        a.sin_port = htons(port) ;                 \
        if ( bind ( s, (const struct sockaddr *)&a, sizeof(a)) == -1 ) \
        {                                          \
            elog ( "failed to bind '%s' socket to port %d (%d:%m)\n", n, port, errno); \
            close (s);                             \
            s = 0 ;                                \
            rc = FAIL_SOCKET_BIND ;                \
        }                                          \
        ilog ("Listening for  '%s' messages on %s:%d\n", n, inet_ntoa(a.sin_addr), port); \
    }                                              \
}

/* Transmit UDP messages to a specified for on specified interface */
#define CREATE_NTWK_UDP_TX_SOCKET(iface, ip, s, a, p, n, rc)     \
{                                                                \
    struct ifreq ifr ;                                           \
    int on = 1 ;                                                 \
    rc = PASS ;                                                  \
    memset(&ifr, 0, sizeof(ifr));                                \
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface );  \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);                \
    if ( 0 >= s )                                                \
    {                                                            \
        elog ("failed to create '%s' socket (%d:%m)\n",n,errno); \
        rc = FAIL_SOCKET_CREATE ;                                \
    }                                                            \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                                            \
        elog ("failed to make '%s' socket re-useable (%d:%m)\n", n, errno ); \
        close(s);                                                \
        s = 0 ;                                                  \
        rc = FAIL_SOCKET_OPTION ;                                \
    }                                                            \
    else                                                         \
    {                                                            \
        ioctl(s, SIOCGIFADDR, &ifr);                             \
        dlog("Interface   : %s %s\n", iface,                     \
            inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr)); \
                                                                 \
        rc=setsockopt(s,SOL_SOCKET,SO_BINDTODEVICE,(void*)&ifr,sizeof(ifr)) ; \
        if (rc < 0) \
        {                                                        \
            elog ("setsockopt failed for SO_BINDTODEVICE (%d:%m)\n", errno);  \
            wlog ("Check permission level, must be root\n");     \
            close(s);                                            \
            rc = FAIL_SOCKET_OPTION ;                            \
        }                                                        \
        else                                                     \
        {                                                        \
            memset(&a, 0, sizeof(struct sockaddr_in));           \
            a.sin_family = AF_INET ;                             \
            a.sin_addr.s_addr = inet_addr(ip);                   \
            a.sin_port = htons(p) ;                              \
            ilog ("Transmitting: '%s' messages to %s:%s:%d\n",n,iface,ip,p);\
        }                                                        \
    }                                                            \
}

/* Non-Blocking Receive From specified IP and port */ 
#define CREATE_NTWK_UDP_RX_SOCKET(ip, hn, p, s, a, l, n, rc)     \
{                                                                \
    int on = 1 ;                                                 \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);                \
    if ( 0 >= s )                                                \
    {                                                            \
        elog ("failed to create '%s' socket (%d:%m)\n",n,errno); \
        rc = FAIL_SOCKET_CREATE ;                                \
    }                                                            \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                                            \
        elog ("failed to make '%s' socket re-useable (%d:%m)\n", n, errno); \
        close(s); s = 0 ;                                        \
        rc = FAIL_SOCKET_OPTION ;                                \
    }                                                            \
    else if ( 0 > ioctl(s, FIONBIO, (char *)&on))                \
    {                                                            \
       elog ("failed to set '%s' socket non-blocking (%d:%m)\n", n, errno); \
       close(s); s = 0 ;                                         \
       rc = FAIL_SOCKET_NOBLOCK ;                                \
    }                                                            \
    else                                                         \
    {                                                            \
        rc = inet_pton(AF_INET, ip.c_str(), &(a));               \
        if ( rc != 1 )                                           \
        {                                                        \
            elog("%s Failed to convert the '%s' to network address (rc:%d)\n",  hn, ip.c_str(), rc); \
            close (s); s = 0 ;                                   \
            rc = FAIL_HOSTADDR_LOOKUP;                           \
        }                                                        \
        else                                                     \
        {                                                        \
            rc = PASS ;                                          \
            memset(&a, 0, sizeof(a));                            \
            a.sin_family = AF_INET ;                             \
            a.sin_addr.s_addr = inet_addr(ip.data()) ;           \
            a.sin_port = htons(p) ;                              \
            l = sizeof(a);                                       \
            if ( bind ( s, (const struct sockaddr *)&a, sizeof(a)) == -1 ) \
            {                                                    \
                elog ( "'%s' socket bind failed (%d) (%d:%m)\n",n,p,errno); \
                close (s);                                       \
                s = 0 ;                                          \
                rc = FAIL_SOCKET_BIND ;                          \
            }                                                    \
            ilog ("Listening for '%s' messages on %s:%d\n", n, inet_ntoa(a.sin_addr), p); \
        }                                                        \
    }                                                            \
}

/**************************************************
 *
 * Name: CREATE_INET_UDP_TX_SOCKET
 *
 * Create reusable INET Transmit socket over
 * the 'lo' interface to the specified 'port' 
 * 
 * Parameters:
 *
 *  s - socket  (int)
 *  a - address (struct sockaddr_in)
 *  p - port    (int)
 *  l - length  (socklen_t)
 *  n - name    (char *)
 * rc - status  (int)
 *
 **************************************************/ 
#define CREATE_INET_UDP_TX_SOCKET(s,a,p,l,n,rc)    \
{                                                  \
    int on = 1 ;                                   \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  \
    if ( 0 >= s )                                  \
    {                                              \
        elog ("failed to create '%s' socket (%d:%m)\n", n, errno ); \
        rc = FAIL_SOCKET_CREATE ;                  \
    }                                              \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                              \
        elog ("failed to make '%s' socket re-useable (%d:%m)\n", n, errno ); \
        close(s);                                  \
        s = 0 ;                                    \
        rc = FAIL_SOCKET_OPTION ;                  \
    }                                              \
    else                                           \
    {                                              \
        memset(&a, 0, sizeof(struct sockaddr_in)); \
        l = sizeof(a);                             \
        a.sin_family = AF_INET ;                   \
        a.sin_addr.s_addr = inet_addr(LOOPBACK_IP);\
        a.sin_port = htons(p) ;                    \
        ilog ("Transmitting: '%s' messages to %s:%d\n", n, inet_ntoa(a.sin_addr), p); \
    }                                              \
}

/**************************************************
 *
 * Name: CREATE_INET_UDP_RX_SOCKET
 *
 * Create Non-Blocking, reusable INET Receive
 * socket over the 'lo' interface on 
 * specified 'port' 
 * 
 * Parameters:
 *
 *  s - socket  (int)
 *  a - address (struct sockaddr_in)
 *  p - port    (int)
 *  l - length  (socklen_t)
 *  n - name    (char *)
 * rc - status  (int)
 *
 **************************************************/ 
#define CREATE_INET_UDP_RX_SOCKET(s,a,p,l,n,rc)    \
{                                                  \
    int on = 1 ;                                   \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  \
    if ( 0 >= s )                                  \
    {                                              \
        elog ("failed to create '%s' socket (%d:%m)\n", n, errno); \
        rc = FAIL_SOCKET_CREATE ;                  \
    }                                              \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                              \
        elog ("failed to make '%s' socket re-useable (%d:%m)\n", n, errno); \
        close(s);                                  \
        s = 0 ;                                    \
        rc = FAIL_SOCKET_OPTION ;                  \
    }                                              \
    else if ( 0 > ioctl(s, FIONBIO, (char *)&on))  \
    {                                              \
       elog ("failed to set '%s' socket non-blocking (%d:%m)\n", n, errno); \
       close(s);                                   \
       s = 0 ;                                     \
       rc = FAIL_SOCKET_NOBLOCK ;                  \
    }                                              \
    else                                           \
    {                                              \
        memset(&a, 0, sizeof(a));                  \
        a.sin_family = AF_INET ;                   \
        a.sin_addr.s_addr = inet_addr(LOOPBACK_IP);    \
        a.sin_port = htons(p) ;                    \
        if ( bind ( s, (const struct sockaddr *)&a, sizeof(a)) == -1 ) \
        {                                          \
            elog ( "failed to bind '%s' socket to port %d (%d:%m)\n", n, p, errno); \
            close (s);                             \
            s = 0 ;                                \
            rc = FAIL_SOCKET_BIND ;                \
        }                                          \
        ilog ("Listening for '%s' messages on %s:%d\n", n, inet_ntoa(a.sin_addr), p); \
    }                                              \
}
