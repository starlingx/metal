/*
 * Copyright (c) 2014, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGCS Platform Resource Monitor Client Notification API Library Header
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
#include <pthread.h>     /* for ... mutual exclusion           */
#include <stdlib.h>
#include <sys/ioctl.h>
/**

 * This is a convenience module in support of resource monitoring notificiations to 
 * client processes 
 *
 * Packaged as a shared library that processes can link to.
 *
 * This module provides four simple interfaces to that provide the following general functions
 *
 * - open an abstract socket interface for resource monitoring messaging
 * - return the socket file descriptor for event driven selection
 * - service events on socket
 * - close the socket when done
 *
 * *Interfaces including work flow are*
 *
 * Init:
 *
 *     resource_monitor_initialize ( "testClient" , 2302, CPU_USAGE );
 *
 * Setup event driven handling:
 *
 *     int resource_monitor_socket = resource_monitor_get_sel_obj();
 *     FD_SET( resource_monitor_socket, &readfds);
 *
 * Main loop:
 *
 *     if ( FD_ISSET(resource_monitor_socket, &readfds))
 *         resource_monitor_dispatch ();
 *
 * Exit:
 *
 *     resource_monitor_deregister("testClient", 2302);
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


/* Notification resource types */
#define CPU_USAGE     ((const char *)"cpuUsage")
#define MEMORY_USAGE  ((const char *)"memoryUsage")
#define FS_USAGE      ((const char *)"fsUsage")
#define ALL_USAGE     ((const char *)"allUsage")
#define CLR_CLIENT    ((const char *)"clearClient")
#define RESOURCE_NOT ((const char *)"resourceNotification")
#define NOT_SIZE      (100)
#define ERR_SIZE      (100)
#define MAX_ERR_CNT   (5)
/** Supplies the messaging socket file descriptor.
 *
 * @returns The created socket file descriptor for event driven select 
 * or zero if initialize was not called of there was error creating
 * the socket. A notification message is sent to rmon to tell it that a new client 
 * is registering for a notification of type resource.  From then on, rmon will send 
 * alarm set and clear messages for that resource to the process until it deregisters. 
 **/
int  resource_monitor_initialize  ( const char * process_name_ptr, int port, const char * resource );


int rmon_notification ( const char * notification_name );

/* returns the client socket fd */ 
int  resource_monitor_get_sel_obj ( void );

/** Close the rmon tx socket */
void resource_monitor_finalize ( void );

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


/* Deregister a client process from rmon notifications */
int resource_monitor_deregister( const char * process_name_ptr, int socket );

/** FAult Insertion Mode Strings */
#define FIT_MAGIC_STRING    "magic"
#define FIT_SEQUENCE_STRING "sequence"
#define FIT_PROCESS_STRING  "process"

/** Fault Insertion Codes */
#define FIT_NONE    0
#define FIT_MAGIC   1
#define FIT_SEQ     2
#define FIT_PROCESS 3
#define WAIT_DELAY (3)
#define PASS (0)
#define FAIL (1)  

/* location of file for registering clients */ 
#define RMON_API_REG_DIR   ((const char *)"/etc/rmonapi.d/register.txt")
/* location of file for deregistering clients */ 
#define RMON_API_DEREG_DIR   ((const char *)"/etc/rmonapi.d/deregister.txt")
/* location of file for the current registered clients */ 
#define RMON_API_ACTIVE_DIR   ((const char *)"/etc/rmonapi.d/active.txt")
