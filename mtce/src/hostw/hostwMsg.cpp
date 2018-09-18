/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Host Watchdog Service Messaging
  */

#include "hostw.h"
#include "nodeMacro.h"

/**
 * Messaging Socket Control Struct - The allocated struct
 */
static hostw_socket_type hostw_sock;

hostw_socket_type * hostw_getSock_ptr ( void )
{
    return ( &hostw_sock );
}

/**
 * Create the socket interface to the host watchdog daemon
 * We use Unix sockets rather than UDP, which are identified by a pathname
 * (essentially, a FIFO pipe) rather than a portn number
 */
int hostw_socket_init ( )
{
    hostw_socket_type * hostw_socket = hostw_getSock_ptr();

    hostw_socket->status_sock = socket (AF_UNIX, SOCK_DGRAM, 0);
    if (hostw_socket->status_sock <= 0) return FAIL;

    memset(&hostw_socket->status_addr, 0, sizeof(hostw_socket->status_addr));
    hostw_socket->status_addr.sun_family = AF_UNIX;

    snprintf( &(hostw_socket->status_addr.sun_path[1]), UNIX_PATH_MAX-1,
         "%s",
         HOSTW_UNIX_SOCKNAME);

    if (bind(hostw_socket->status_sock, (struct sockaddr *)&hostw_socket->status_addr, sizeof(hostw_socket->status_addr)) == -1)
    {
        elog ("failed to bind\n");
        close (hostw_socket->status_sock);
        return FAIL;
    }

    ilog ("hostwd listening for status updates\n");

    return PASS;
}

