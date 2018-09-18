/*
 * Copyright (c) 2014, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGCS Platform Active Process Monitor Library Header
  */

#include <stdio.h>       /* for ... snprintf                   */
#include <unistd.h>      /* for ... unlink, close and usleep   */
#include <sys/socket.h>  /* for ... socket                     */
#include <sys/un.h>      /* for ... domain socket type         */
#include <netinet/in.h>  /* for ... inet socket type           */
#include <arpa/inet.h>   /* for ... inet_addr, inet_ntoa macro */
#include <syslog.h>      /* for ... syslog                     */
#include <errno.h>       /* for ... EINTR, errno, strerror     */
#include <stdbool.h>     /* for ... true and false             */
#include <sys/stat.h>    /* for ... file stat                  */


/**
 * @addtogroup active_monitor_library
 * @{
 *
 * This is a convenience module in support of actively monitoring process
 * health within a carrier grade processing environment.
 *
 * Packaged as a shared library that processes can link to.
 *
 * This module provides four simple interfaces to that provide the following general functions
 *
 * - open an abstract socket interface for active monitoring messaging
 * - return the socket file descriptor for event driven selection
 * - service events on socket
 * - close the socket when done
 *
 * *Interfaces including work flow are*
 *
 * Init:
 *
 *     active_monitor_initialize ( "hbsClient" , port );
 *
 * Setup event driven handling:
 *
 *     int active_monitor_socket = active_monitor_get_sel_obj();
 *     FD_SET( active_monitor_socket, &readfds);
 *
 * Main loop:
 *
 *     if ( FD_ISSET(active_monitor_socket, &readfds))
 *         active_monitor_dispatch ();
 *
 * Exit:
 *
 *     active_monitor_finalize ();
 *
 */

/** Initialize the library and open the messaging socket(s).
 *
 * Creates socket and binds to named endpoint.
 *
 * Prints status or errors to syslog.
 * 
 * @param process_name_ptr - char pointer to string containing monitored process name
 * @param port - integer specifying the port number this process is listening on
 *
 * @returns The socket file descriptor on success or negative version of 
 * standard Linux error numbers (errno) codes from socket(2) or bind(2)
 * 
 **/
int  active_monitor_initialize  ( const char * process_name_ptr, int port );

/** Supplies the messaging socket file descriptor.
 *
 * @returns The created socket file descriptor for event driven select 
 * or zero if initialize was not called of there was error creating
 * the socket.
 *
 **/
int  active_monitor_get_sel_obj ( void );

/** The work horse of this library.
 *
 * This interface services the receive, implements the sanity algorithm
 * on the receive message and sends back a sane response.
 *
 * @returns Zero on success or any standard connect(2), 
 * sendto(2) or recvfrom(2) error codes as well as
 *
 * - EPERM  : if called prior to initialize.
 * - EAGAIN : if no message to receive.
 *  
 * */
int  active_monitor_dispatch ( void );

/** Close the socket */
void active_monitor_finalize ( void );

/** Debug mode is enabled if the following file is found during initialize
  *
  * /var/run/<process>.debug
  *
  * Failt Insertion Mode is enabled if the first word of line one
  * of this file contains one of the following words
  * 
  * sequence - corrupt the sequence number returned
  * magic    - corrupt the magic number returned
  * process  - corrupt the process name returned
  *
  */

/** FAult Insertion Mode Strings */
#define FIT_MAGIC_STRING    "magic"
#define FIT_SEQUENCE_STRING "sequence"
#define FIT_PROCESS_STRING  "process"

/** Fault Insertion Codes */
#define FIT_NONE    0
#define FIT_MAGIC   1
#define FIT_SEQ     2
#define FIT_PROCESS 3

/**
 * @} active_monitor_library
 */
