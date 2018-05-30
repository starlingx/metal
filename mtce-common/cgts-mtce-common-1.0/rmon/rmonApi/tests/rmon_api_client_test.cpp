/*
 * Copyright (c) 2014-2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Resource Monitor API Test Client  
  */
/*
 *This simulates a test client process to test out the rmon client notification
 *api.  To run: ./rmond_api_test <port> <processname> 
 *If left blank it runs with the default port: 2302 and default process name.  When testing
 *with more than one client test process, these values must be entered. For help:
 *./rmond_api_test --help 
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>   
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h> 
#include <list>

using namespace std;

#include "../rmon_nodeMacro.h"     /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET   */
#include "rmon_api_client_test.h"

extern "C"
{
    #include "../rmon_api.h"     
}
#define MAX_HOST_NAME_SIZE (100)
#define FAIL_SOCKET_INIT -1 
#define FAIL_SOCKET_CREATE -2
#define PASS 0
#define FAIL 1
#define LOOPBACK_IP "127.0.0.1"
#define RX_PORT 2302
static char   my_hostname [MAX_HOST_NAME_SIZE+1];


/**
 * Messaging Socket Control Struct - The allocated struct
 * @see rmon_api_client_test.h for rmon_socket_type struct format.
 */
static rmon_socket_type rmon_sock   ;
static rmon_socket_type * sock_ptr ;

/** Client Config mask */
#define CONFIG_CLIENT_MASK      (CONFIG_AGENT_PORT        |\
                                 CONFIG_CLIENT_API_PORT |\
                                 CONFIG_CLIENT_PORT)




/****************************/
/* Initialization Utilities */
/****************************/

/* Initialize the unicast api response message  */
/* One time thing ; tx same message all the time. */
int rmon_message_init ( void ) 
{
    /* Build the transmit api response message */
    memset ( &sock_ptr->tx_message, 0, sizeof (rmon_message_type));
    memcpy ( &sock_ptr->tx_message.m[RMON_HEADER_SIZE], my_hostname, strlen(my_hostname));
    return (PASS);
}

int rmon_socket_init ( int port, const char * process_name )
{

    int    on = 1 ;
    int    rc = PASS ;

    
    /***********************************************************/
    /* Setup the RMON API Message Receive Socket           */
    /***********************************************************/

    CREATE_NONBLOCK_INET_UDP_RX_SOCKET ( LOOPBACK_IP, 
                                         port, 
                                         rmon_sock.rmon_api_sock,
                                         rmon_sock.rmon_api_addr,
                                         rmon_sock.rmon_api_port,
                                         rmon_sock.rmon_api_len,
                                         "rmon api socket receive", 
                                         rc );
    if ( rc ) return (rc) ;

    /* Open the active monitoring socket */
    rmon_sock.rmon_socket = resource_monitor_initialize ( process_name, port, ALL_USAGE );
    printf("Resource Monitor API Socket %d\n", rmon_sock.rmon_socket );
    if ( 0 > rmon_sock.rmon_socket )
        rmon_sock.rmon_socket = 0 ;

    /* Make the resource monitor api socket non-blocking */
    rc = ioctl(rmon_sock.rmon_socket, FIONBIO, (char *)&on);
    if ( 0 > rc )
    {
        printf("Failed to set rmon socket non-blocking (%d:%m)\n", errno );
        return (FAIL_SOCKET_NOBLOCK);
    }
    

    return (PASS);
}

int daemon_init (int port, const char * process_name )
{
    int rc = PASS ;

    /* Initialize socket construct and pointer to it */   
    memset ( &rmon_sock,   0, sizeof(rmon_sock));
    sock_ptr = &rmon_sock ;  
    
    /* Setup the resmon api rx messaging sockets */
    if ( (rc = rmon_socket_init  (port, process_name)) != PASS )
    {
        printf ("socket initialization failed (rc:%d)\n", rc );
        rc = FAIL_SOCKET_INIT;
    }
    return (rc);
}

#define RMON_MAX_LEN (100)
int client_service_inbox ( const char * process_name)
{
    #define MAX_T 100
    int bytes = 0 ;
    char buf[RMON_MAX_LEN] ;
    socklen_t len = sizeof(struct sockaddr_in) ;
	char str[RMON_MAX_LEN];
    int sequence = 0;
    int rc = FAIL;

    do
    {
        memset ( buf,0,RMON_MAX_LEN);
        memset ( str,0,RMON_MAX_LEN);

        bytes = recvfrom( rmon_sock.rmon_socket, buf, RMON_MAX_LEN, 0, (struct sockaddr *)&rmon_sock.client_sockAddr, &len);
        if ( bytes > 0 )
        {

            sscanf ( buf, "%s %d", str, &sequence );
                if ( str[0] != '\0' )
                {
				   printf("%s \n",str);

				   if (strstr(str, "cleared_alarms_for_resource:") != NULL) {
				      /* Sleep for 10 secs */
                      sleep (10);
				      rc = resource_monitor_deregister( process_name, rmon_sock.rmon_socket ); 
				      if ( rc == PASS ) {
				        printf("deregistered test client\n");
					    break;
				      } 
					}
                 }
                else
                {
                   printf("Null string !\n");
                }
            
        }
        else if (( 0 > bytes ) && ( errno != EINTR ) && ( errno != EAGAIN ))
        {
           printf("problem with test client recv \n");
		}
    } while ( bytes > 0 ) ;

	return rc;
}

#define MAX_LEN 300
int main ( int argc, char *argv[] )
{
    int rc     = 0 ;
    int port = RX_PORT;
    const char * process_name = PROCESS_NAME;

    if ((argc > 1) && (strcmp(argv[1],"--help") == 0)) {
	  printf("usage: ./rmond_api_test <port> <process name> \n");
	  return 0;
    }
	else if (argc > 1) {
      port = atoi(argv[1]); 
	}
	if (argc > 2) {
      process_name = argv[2];
	}


	daemon_init(port, process_name);
	rc = rmon_message_init();
	if (rc == PASS) {

       printf("socket initialized \n");
	}

    rmon_sock.rmon_socket = resource_monitor_get_sel_obj ();
	std::list<int> socks;
    socks.clear();   
    socks.push_front ( rmon_sock.rmon_socket );    
    socks.sort();
    
    /* Run test loop forever or until stop condition */ 
    for ( ;  ; )
    {
    
        /* Initialize the timeval struct  */
        rmon_sock.waitd.tv_sec = 20;
        rmon_sock.waitd.tv_usec = 0;

        /* Initialize the master fd_set */
        FD_ZERO(&rmon_sock.readfds);
        FD_SET(rmon_sock.rmon_socket, &rmon_sock.readfds);

        rc = select( socks.back()+1, 
                     &rmon_sock.readfds, NULL, NULL, 
                     &rmon_sock.waitd);

        /* If the select time out expired then  */
        if (( rc < 0 ) || ( rc == 0 ))
        {
            /* Check to see if the select call failed. */
            /* ... but filter Interrupt signal         */
            if (( rc < 0 ) && ( errno != EINTR ))
            {
                printf ("Socket Select Failed (rc:%d) %s \n", errno, strerror(errno));
            }
        }

      
        if ( FD_ISSET(rmon_sock.rmon_socket, &rmon_sock.readfds))
        {
            printf("Resource Monitor API Select Fired got message from rmon:\n");
			rc = client_service_inbox(process_name);

			if (rc == PASS) {
               break; 
			}
         }
       }
    return 0;  
}

