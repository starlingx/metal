#ifndef __INCLUDE_MTCNODEMSG_HH__
#define __INCLUDE_MTCNODEMSG_HH__
/*
 * Copyright (c) 2013, 2016, 2024 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance "Messaging"
 *
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

using namespace std;

#include "nodeBase.h"
#include "nodeUtil.h"      /* for ... msgSock_type                       */
#include "msgClass.h"


/*************************************************************************
 * Common Service Messaging Stuff
 *************************************************************************

         +----------*                      +-----------*
         *          * --- agent_addr --->  *           *
         * mtcAgent *                      * mtcClient *
         *          *                      *           *
         *          * <-- client_addr ---> *           *
         *          *                      *           *
    +--> *          * ---+                 *           *
    |    +----------*    |                 +-----------*
    |                    |
  event               config
    |                    |
    |    +----------*    |                 +-----------*
    +--- *          * <--+                 *           *
         * hbsAgent *                      * hbsClient *
         *          * -- multicast req --> *           *
         *          *                      *           *
         *          * <-- hb pulse resp -- *           *
         *          *                      *           *
         +----------*                      +-----------*        */

#define SA struct sockaddr*


#define MTC_AGENT_RX_BUFF_SIZE (MAX_NODES*MAX_MSG)

#define MAX_RX_MSG_BATCH (50)

/** Maintenance messaging socket control structure */
typedef struct
{
    /** These sockets define the maintenance system msging.      */

    /** UDP sockets used by the mtcAgent to transmit and receive
     *  maintenance commands to the client (compute) node and
     *  receive the compute node reply in the receive direction   */
    msgClassSock*  mtc_agent_mgmt_tx_socket   ; /**< tx to   mtc client mgmnt */
    msgClassSock*  mtc_agent_mgmt_rx_socket   ; /**< rx from mtc client mgmnt */
    msgClassSock*  mtc_agent_clstr_tx_socket  ; /**< tx to   mtc client clstr */
    msgClassSock*  mtc_agent_clstr_rx_socket  ; /**< rx from mtc client clstr */
    int  mtc_agent_port                       ; /**< the agent rx port number */
    int  mtc_rx_mgmnt_port                    ; /**< the agent rx port number */

    struct sockaddr_in  agent_addr; /**< socket attributes struct */
    int  mtc_agent_mgmt_rx_socket_size ;
    int  mtc_agent_clstr_rx_socket_size ;

    /** UDP sockets used by the mtcClient to receive maintenance
      * commands from and transmit replies to the mtcAgent                           */
    msgClassSock*  mtc_client_mgmt_rx_socket     ; /**< rx from controller   mgmt    */
    msgClassSock*  mtc_client_mgmt_tx_socket     ; /**< tx to   controller   mgmnt   */
    msgClassSock*  mtc_client_clstr_tx_socket_c0 ; /**< tx to   controller-0 clstr   */
    msgClassSock*  mtc_client_clstr_tx_socket_c1 ; /**< tx to   controller-1 clstr   */
    msgClassSock*  mtc_client_clstr_rx_socket    ; /**< rx from controller   clstr   */
    int            mtc_mgmnt_cmd_port            ; /**< mtc command port     mgmnt   */
    int            mtc_clstr_cmd_port            ; /**< mtc command port     clstr   */
    struct sockaddr_in mtc_cmd_addr              ; /**< socket attributes mgmnt      */

   /***************************************************************/

    /** Event Receive Interface - (UDP over 'lo')                        */
    int                mtc_event_rx_port  ; /**< mtc event receive port  */
    msgClassSock*      mtc_event_rx_sock  ; /**< ... socket              */

    /** UDP Mtc to Hbs command port                                      */
    int                 mtc_to_hbs_port   ; /**< hbs command port        */
    msgClassSock*       mtc_to_hbs_sock   ; /**< ... socket              */


    /** UDP Hardware Monitor Command Port                                */
    int                 hwmon_cmd_port    ; /**< ava event port          */
    msgClassSock*       hwmon_cmd_sock    ; /**< ... socket              */

    /** UDP Logger Port                                                  */
    msgSock_type        mtclogd           ; /**< messaging into mtclogd  */

    /* For select dispatch */
    struct timeval waitd  ;
            fd_set readfds;

    /** IPV4 Pxeboot transmit and receive sockets and ports */
    int  pxeboot_tx_socket    ;
    int  mtc_tx_pxeboot_port  ;
    int  pxeboot_rx_socket    ;
    int  mtc_rx_pxeboot_port  ;

    /** Active Monitor Socket */
    int  amon_socket ;

    bool main_go_enabled_reply_ack ;
    bool subf_go_enabled_reply_ack ;

    int netlink_sock ; /* netlink socket */
    int   ioctl_sock ; /* general ioctl socket */

    float msg_rate ;
} mtc_socket_type ;


mtc_socket_type * get_sockPtr ( void );
int send_mtc_msg ( mtc_socket_type * sock_ptr, int cmd, string who_i_am );
int send_mtcAlive_msg ( mtc_socket_type * sock_ptr, string identity, int interface );

int recv_mtc_reply_noblock ( void );

int send_mtc_cmd ( string & hostname, int cmd, int interface , string json_dict="" );
int mtc_service_command ( mtc_socket_type * sock_ptr , int interface );
int mtc_set_availStatus ( string & hostname, mtc_nodeAvailStatus_enum status );
int mtce_send_event    ( mtc_socket_type * sock_ptr, unsigned int cmd , const char * mtce_name_ptr );
int mtc_clstr_init     ( mtc_socket_type * sock_ptr , char * iface );
string get_who_i_am ( void );

int send_mtcClient_cmd ( mtc_socket_type * sock_ptr, int cmd, string hostname, string address, int port);

#endif
