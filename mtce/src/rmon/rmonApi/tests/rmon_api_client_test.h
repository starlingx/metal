/*
 * Copyright (c) 2014 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Resource Monitor API Test Client Header   
  */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

/** Maximum service fail count before action */
#define MAX_FAIL_COUNT (1)

#define RMON_HEADER_SIZE (15)
#define RMON_MAX_MSG (50)
#define FAIL_SOCKET_NOBLOCK   (36)
#define FAIL_SOCKET_OPTION   (38)

/* default process name if none is specified */
#define PROCESS_NAME     ((const char *)"testClient")



typedef struct
{
    /** Message buffer  */
    char           m [RMON_MAX_MSG]; 

    /** Sequence number */
    unsigned int   s ;

    /* Fast Lookup Clue Info */
    unsigned int   c ; 

    /* Status Flags */
    /* bit 0: Process Monitor Status: 1=running 
     * bit 1: tbd                                */
    unsigned int   f ;

    /* reserved for future use */
    unsigned int   r ;

} rmon_message_type ;

/** rmon notification api messaging socket control structure */
typedef struct
{

   /** Client (compute) socket address attributes
   *
   * Used by both controller and compute nodes
   * when messaging to and from the compute
   * node maintenance.
   */
   struct sockaddr_in  client_addr ;
   socklen_t           client_addr_len ;


    /** Unix domain socket used to transmit on-node event messages
     * to from other local services such as rmon - heartbeat service */
   int  send_event_socket ;
   struct sockaddr_un  agent_domain     ; 
   socklen_t           agent_domain_len ;

   /** rmon api Socket using UDP Inet over 'lo' interface */  
   int                rmon_api_sock  ; /**< receive rmon pulses socket    */
   int                rmon_api_port  ; /**< the port                      */
   struct sockaddr_in rmon_api_addr  ; /**< attributes                    */
   socklen_t          rmon_api_len   ; /**< length                        */

   int                rmon_socket     ; /**< Active monitor socket         */
   /** The addr and port are stored in the shared librmonapi.so library        */

   struct sockaddr_in  client_sockAddr ; /**< Client socket attributes       */
   socklen_t           agentLen        ; /**< Agent socket attr struct len   */
   socklen_t           clientLen       ; /**< Client socket attr struct len  */
   int                 tx_socket       ; /**< general transmit socket ID     */
   int                 rx_socket       ; /**< general receive socket ID      */
   rmon_message_type   tx_message      ; /**< transmit message               */
   rmon_message_type   rx_message      ; /**< receive message                */
   int                 rmon_client_port ;
   int                 fail_count      ; /**< Socket retry thresholding */

   /* For select dispatch */ 
   struct timeval waitd  ;
   fd_set readfds;
   msgSock_type       mtclogd ;

} rmon_socket_type ;


