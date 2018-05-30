/*
* Copyright (c) 2013-2014 Wind River Systems, Inc.
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
        printf ("failed to create '%s' socket (%d:%s)\n", n, errno, strerror(errno)); \
        rc = FAIL_SOCKET_CREATE ;                  \
    }                                              \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                              \
        printf ("failed to make '%s' socket re-useable (%d:%s)\n", n, errno, strerror(errno)); \
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
    }                                              \
}

#define CREATE_NONBLOCK_INET_UDP_RX_SOCKET(ip, port, s, a, p, l, n, rc) \
{                                                  \
    int on = 1 ;                                   \
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  \
    if ( 0 >= s )                                  \
    {                                              \
        printf ("failed to create '%s' socket (%d:%s)\n", n, errno, strerror(errno)); \
        rc = FAIL_SOCKET_CREATE ;                  \
    }                                              \
    else if ( setsockopt ( s , SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 ) \
    {                                              \
        printf ("failed to make '%s' socket re-useable (%d:%s)\n", n, errno, strerror(errno)); \
        close(s);                                  \
        s = 0 ;                                    \
        rc = FAIL_SOCKET_OPTION ;                  \
    }                                              \
    else if ( 0 > ioctl(s, FIONBIO, (char *)&on))  \
    {                                              \
       printf ("failed to set '%s' socket non-blocking (%d:%s)\n", n, errno, strerror(errno)); \
       close(s);                                   \
       s = 0 ;                                     \
       rc = FAIL_SOCKET_NOBLOCK ;                  \
    }                                              \
    else                                           \
    {                                              \
        memset(&a, 0, sizeof(struct sockaddr_in)); \
        l = sizeof(a);                             \
        p = port ;                                 \
        a.sin_family = AF_INET ;                   \
        a.sin_addr.s_addr = inet_addr(ip);         \
        a.sin_port = htons(p) ;                    \
        if ( bind ( s, (const struct sockaddr *)&a, sizeof(struct sockaddr_in)) == -1 ) \
        {                                          \
            printf ( "failed to bind '%s' socket with port %d\n", n, p ); \
            close  (s);                            \
            s = 0 ;                                \
            rc = -errno;                           \
        }                                          \
        printf ("Listening on '%s' socket %s port %d\n", n, inet_ntoa(a.sin_addr), p); \
    }                                              \
}
