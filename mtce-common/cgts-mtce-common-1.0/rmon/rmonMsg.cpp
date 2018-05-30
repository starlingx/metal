/*
 * Copyright (c) 2013-2014, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Resource Monitor Messaging API 
  * This class implements a server that accepts client processes
  * registering and deregistering for rmon notifications.  This class
  * also implements a send function to send alarm messages and clear
  * messages to the clients registered for a particular resource.  
  */

#include <dirent.h> /* for config dir reading          */
#include <list>     /* for the list of conf file names */
#include <vector>
#include <fstream>
#include <algorithm>
#include <sys/file.h>
#include "rmon.h"
#include "rmonApi/rmon_nodeMacro.h"


/**
 * Messaging Socket Control Struct - The allocated struct
 */

static rmon_socket_type rmon_sock;
rmon_socket_type * rmon_getSock_ptr ( void )
{
    return ( &rmon_sock );
}

msgSock_type * get_mtclogd_sockPtr ( void )
{
    return (&rmon_sock.mtclogd);
}

/****************************/
/* Initialization Utilities */
/****************************/

/* Init the messaging socket control structure 
 * The following messaging interfaces use this structure and 
 * are initialized separately
 * */

void rmon_msg_init ( void )
{
    memset(&rmon_sock, 0, sizeof(rmon_sock));
}

void rmon_msg_fini ( void )
{
    if ( rmon_sock.rmon_tx_sock ) {
        close (rmon_sock.rmon_tx_sock);
    } if ( rmon_sock.rmon_rx_sock ) {
        close (rmon_sock.rmon_rx_sock);
    } if ( rmon_sock.netlink_sock ) {
        close (rmon_sock.netlink_sock);
    } if ( rmon_sock.ioctl_sock ) {
        close (rmon_sock.ioctl_sock);
    }
}

 /*Initialize the default rmon tx socket from the socket provided in: 
 /etc/rmond.conf */
int  rmon_port_init ( int tx_port )
{
    int val = 1    ;
    int rc  = FAIL ;
    if ( tx_port )
    {
        rmon_sock.rmon_tx_port = tx_port ;

        rmon_sock.rmon_tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if ( 0 >= rmon_sock.rmon_tx_sock )
            return (-errno);

        if ( setsockopt ( rmon_sock.rmon_tx_sock , SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1 )
        {
            wlog ( "rmon: failed to set rmon api tx socket as re-useable (%d:%m)\n", errno );
        }

        /* Set socket to be non-blocking.  */
        rc = ioctl(rmon_sock.rmon_tx_sock, FIONBIO, (char *)&val);
        if ( 0 > rc )
        {
            elog ("Failed to set rmon tx socket non-blocking\n");
        }

        /* Setup with localhost ip */
        memset(&rmon_sock.rmon_tx_addr, 0, sizeof(struct sockaddr_in));
        rmon_sock.rmon_tx_addr.sin_family = AF_INET ;
        // rmon_sock.rmon_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        rmon_sock.rmon_tx_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        rmon_sock.rmon_tx_addr.sin_port = htons(rmon_sock.rmon_tx_port) ;

        /* bind socket to the receive addr */
        if ( bind ( rmon_sock.rmon_tx_sock, (const struct sockaddr *)&rmon_sock.rmon_tx_addr, sizeof(struct sockaddr_in)) == -1 )
        {
            elog ( "failed to bind to 'tx' socket with port %d (%d:%m)\n", tx_port, errno );
            close (rmon_sock.rmon_tx_sock);
            rmon_sock.rmon_tx_sock = 0 ;
            return (-errno);
        }
    } 
    else
    {
        elog ("No tx port specified\n");
    }

    return (rc) ;    
}

/* Open a socket for a new client process */ 
int  open_resource_socket ( char str[RMON_MAX_LEN], char registered_not[RMON_MAX_LEN], int port )
{
    int rc = FAIL ;
    int on = 1;
    registered_clients clt;

    memset((char*)&clt, 0, sizeof(clt));
    strcpy(clt.registered_not, registered_not); 

    clt.rx_sock.rmon_rx_port = port - 1 ;
    clt.rx_sock.rmon_rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if ( 0 >= clt.rx_sock.rmon_rx_sock )
    {
        elog ("failed to open 'rx' socket (%d:%m)", errno );
        return (-errno);
    }

    if ( setsockopt ( clt.rx_sock.rmon_rx_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1 )
    {
        wlog ( "rmon: failed to set rmon api rx socket as re-useable (%d:%m)\n", errno);
    }

    /* Set socket to be non-blocking.  */
    rc = ioctl(clt.rx_sock.rmon_rx_sock, FIONBIO, (char *)&on);
    if ( 0 > rc )
    {
        elog ("Failed to set rmon rx socket non-blocking\n");
    }
   
    /* Setup with localhost ip */
    memset(&clt.rx_sock.rmon_rx_addr, 0, sizeof(struct sockaddr_in));
    clt.rx_sock.rmon_rx_addr.sin_family = AF_INET ;
    // rmon_sock.rmon_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    clt.rx_sock.rmon_rx_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    clt.rx_sock.rmon_rx_addr.sin_port = htons(clt.rx_sock.rmon_rx_port) ;
    clt.port = port;
    strcpy(clt.client_name, str);

    /* Prop the port numnber into the message struct */
    if ( clt.port ) {
        clt.msg.tx_port = clt.port ;
    }

    if ( clt.msg.tx_port )
    {
        /* if the sock is already open then close it first */
        if ( clt.msg.tx_sock )
        {
            wlog ("%s open on already open socket %d, closing first\n", 
                      clt.client_name, clt.msg.tx_sock );
            close (clt.msg.tx_sock);
        }
        clt.msg.tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if ( 0 >= clt.msg.tx_sock )
        {
            elog ("failed to open 'tx' socket (%d:%m)", errno );
            return (-errno);
        }

        /* Setup with localhost ip */
        memset(&clt.msg.tx_addr, 0, sizeof(struct sockaddr_in));
        clt.msg.tx_addr.sin_family = AF_INET ;
        clt.msg.tx_addr.sin_addr.s_addr = inet_addr(LOOPBACK_IP);
        clt.msg.tx_addr.sin_port = htons(clt.msg.tx_port) ;
        
        /* Make the resource monitor client api socket non-blocking */
        rc = ioctl(clt.msg.tx_sock , FIONBIO, (char *)&on);
        if ( 0 > rc )
        {
            elog("Failed to set rmon socket non-blocking\n");
        }
 
        add_registered_client(clt); 

        rc = PASS ;
    }
    else
    {
        elog ("%s has no port specified\n", clt.client_name );
    }
    return (rc) ;    
}

/* close the client process socket */
void close_resource_socket ( registered_clients * ptr )
{
    if ( ptr->msg.tx_sock ) {
        close ( ptr->msg.tx_sock );
    }
}

/* remove a client from the list of registered clients */
int delete_client ( int clients, int index )
{
    /* close the client socket first */ 
    close_resource_socket( get_registered_clients_ptr(index));
    if (index == (clients -1 )) {
        registered_clients *tmp_ptr = get_registered_clients_ptr(index);
        memset(tmp_ptr, 0 , sizeof(*tmp_ptr));
    } 
    else {

        for (int j = index; j < (clients - 1); j++) 
        {
            registered_clients * clt = get_registered_clients_ptr(j);
            registered_clients * cltTwo = get_registered_clients_ptr(j+1);
            *clt = *cltTwo;
            cltTwo = clt;
        }
    }
    clients--;
    ilog("deleted registered client, %d clients left \n",clients); 
    return clients;
}

void send_response( char message[RMON_MAX_LEN], registered_clients * clt ) 
{
    int rc; 
    /* send a message to the rmon api to tell it the client is registered or deregistered */ 
    rc = sendto ( clt->rx_sock.rmon_rx_sock, 
                  &message ,
                  strlen ( message ), 0,
                  (struct sockaddr *) &clt->rx_sock.rmon_rx_addr, 
                  sizeof(struct sockaddr_in));

    if ( 0 >= rc )
    {
        elog ("sendto error (%d:%s)\n", errno , strerror(errno));     
    }

}

/* Send the outstanding registration and deregistration messages to rmon */
void rmon_alive_notification (int & clients) 
{
    FILE * pFile;
    char * line = NULL;
    size_t len = RMON_MAX_LEN;
    ssize_t read;
    vector<string> active_clients;
    vector<string> dereg_clients;
    char buf[RMON_MAX_LEN];
  
    active_clients.clear();
    dereg_clients.clear();

    /* service deregister requests in queue */
    pFile = fopen (RMON_API_DEREG_DIR , "r");
    if (pFile != NULL) {
        // take out a reader lock on this file incase another
        // entity is exclusively writing to it at this time
        flock(fileno(pFile), LOCK_SH);
        while ((read = getline(&line, &len, pFile)) != -1) {
            clients = rmon_service_file_inbox(clients, line, false );
            string str(line, find(line, line + len, '\0'));
            /* add the deregistered clients to the list to avoid relaunching them */
            dereg_clients.push_back(str);
        }
        // release shared lock
        flock(fileno(pFile), LOCK_UN);
        fclose(pFile);
     }

     /* In the case that rmon restarts or rmon_alive_notifaction()
      * is called periodically, and the clients have not re-registered,
      * then attempt registration from active.txt, ONLY for clients
      * that are not already in the registered_client framework
      */
     pFile = fopen (RMON_API_ACTIVE_DIR , "r");
     if (pFile != NULL) {
         // take out a reader lock on this file incase another
         // entity is exclusively writing to it at this time
         flock(fileno(pFile), LOCK_SH);
         ifstream fin( RMON_API_ACTIVE_DIR );
         string readLine;

         while (getline(fin, readLine)) {
             if ((dereg_clients.empty()) || 
                (find(dereg_clients.begin(), dereg_clients.end(), readLine) == dereg_clients.end())) {
                 /* only add a previously active client if it has not de-registered */ 
                 active_clients.push_back(readLine);
             }
         }
         // release shared lock
         flock(fileno(pFile), LOCK_UN);
         fclose(pFile);
     }
     // remove(RMON_API_ACTIVE_DIR);
     for (unsigned int i=0; i<active_clients.size(); i++)
     {
         /* reconnect the previously connected clients */ 
         strcpy(buf, (active_clients.at(i)).c_str());
         clients = rmon_service_file_inbox(clients, buf, false );
     }

    /* service register requests in queue */
    pFile = fopen (RMON_API_REG_DIR , "r");
    if (pFile != NULL) {
        // take out a reader lock on this file incase another
        // entity is exclusively writing to it at this time
        flock(fileno(pFile), LOCK_SH);
        while ((read = getline(&line, &len, pFile)) != -1)
        {
            clients = rmon_service_file_inbox(clients, line );
        }
        flock(fileno(pFile), LOCK_UN);

        fclose(pFile);
    }
    if (line) {
        free(line);
    }
    remove(RMON_API_REG_DIR);
    remove(RMON_API_DEREG_DIR);
}

/* Remove a client from the active client list if it is being de-registered */ 
void remove_active_client( char buf[NOT_SIZE] ) 
{
    FILE * pFile;
    vector<string> active_clients;
    vector<string> new_active_clients;
    char lineBuf[NOT_SIZE];
    
    active_clients.clear();
    new_active_clients.clear();

    pFile = fopen (RMON_API_ACTIVE_DIR , "r");

    if (pFile != NULL) {
        // take out a reader lock which will block
        // if a writer has exclusive access to this
        // file.
        flock(fileno(pFile), LOCK_SH);
        ifstream fin( RMON_API_ACTIVE_DIR );
        string readLine;

        while (getline(fin, readLine)) {
            active_clients.push_back(readLine);
        }
        // release shared lock
        flock(fileno(pFile), LOCK_UN);
        fclose(pFile);
     }

    for (unsigned int i=0; i<active_clients.size(); i++)
    {
       strcpy(lineBuf, (active_clients.at(i)).c_str());
       if (strcmp(lineBuf, buf) != 0) 
       {
           /* put back all the clients except the one de-registering */
           new_active_clients.push_back(active_clients.at(i));
       }
    }

    /* Create a new active clients file and add all the remaining clients */
    pFile = fopen (RMON_API_ACTIVE_DIR , "w");
    if (pFile)
    {
        // take out a writer lock on this file to
        // ensure that no other entity is writing to it
        // at this time
        int lock = flock(fileno(pFile), LOCK_EX);
        if (lock < 0)
        {
            elog("Failed to get an exclusive on '%s' (%d:%m)", RMON_API_ACTIVE_DIR, errno);
        }
        else
        {
            for (unsigned int i=0; i<new_active_clients.size(); i++)
            {
                strcpy(lineBuf, (new_active_clients.at(i)).c_str());
                fprintf(pFile, "%s\n", lineBuf);
            }
            // release lock
            flock(fileno(pFile), LOCK_UN);
        }
        fclose(pFile);
    } else {
        elog("Cannot open %s to deregister: %s\n",
             RMON_API_ACTIVE_DIR, buf);
    }
}
 
/* Service client registratgion and deregistration requests from file
 *
 * Added new 'add' bool that defaults to true but if called with false
 * suggests not to add this new client reg string to active.txt cause
 * it is already there */
int rmon_service_file_inbox ( int clients, char buf[RMON_MAX_LEN], bool add )
{
    int rc; 
    bool found = false;
    char active_buf[RMON_MAX_LEN] ;
    int total_clients = clients; 
    char str[RMON_MAX_LEN] ;
    char registered_not[RMON_MAX_LEN];
    unsigned int port = 0;
    FILE * pFile;

     memset ( str,0,RMON_MAX_LEN);

     int len = strlen(buf);
     if( buf[len-1] == '\n' )
     {
         /* ensure that the buffer does not have a carriage return */
         buf[len-1] = 0;
     }
     // the format for each registered client is as follows:
     // clientName resourceName port
     sscanf ( buf, "%99s %99s %u", str, registered_not, &port ); //RMON_MAX_LEN is defined as 100
     strcpy( active_buf, buf );
     for (int j=0; j<clients; j++) {
        registered_clients * clt = get_registered_clients_ptr(j);

        if( strcmp(clt->client_name, str) == 0 )
        {
            found = true;
            if (strcmp(CLR_CLIENT, registered_not) == 0) {
                /* the client process wants to deregister, delete it and close it's socket */ 
                remove_active_client(clt->client_name);
                total_clients = delete_client(clients, j);
                break;       
             }
             break; 
        }
     }

    /* only add a client process if it is not already added */  
    if (!found) {

        ilog("registering client \n");
        if ( str[0] != '\0' )
        {  
            rc = open_resource_socket(str, registered_not, port);     
            if (rc == FAIL) {
                wlog("resource client port open failed \n");
            } else if (rc==PASS) {

                total_clients++;
                if ( add == true )
                {
                    /* Add the client to the active clients file */
                    pFile = fopen (RMON_API_ACTIVE_DIR , "a+");
                    if (pFile)
                    {
                        // take out a writer lock on this file to
                        // ensure that no other entity is writing to it
                        // at this time
                        int lock = flock(fileno(pFile), LOCK_EX);
                        if (lock < 0)
                        {
                            elog("Failed to get an exclusive on"
                                 " '%s' (errno: %d)", RMON_API_ACTIVE_DIR, errno);
                        }
                        else
                        {
                            ilog ("adding %s to %s\n", active_buf, RMON_API_ACTIVE_DIR );
                            fprintf(pFile, "%s\n", active_buf);

                            // release the lock
                            flock(fileno(pFile), LOCK_UN);
                        }
                        fclose(pFile);
                    }
                    else
                    {
                        elog("Failed to open file %s", RMON_API_ACTIVE_DIR);
                    }
                }
                else
                {
                    dlog ("avoid adding duplicate entry\n");
                }
            }
        }
        else
        {
            wlog ("Null string !\n");
        }
    }

    return total_clients;
}

/* Service client registration and deregistration requests from select */
int rmon_service_inbox ( int clients )
{
    #define MAX_T  (3)
    int count = 0 ;
    int bytes = 0 ;
    char buf[RMON_MAX_LEN] ;
    char active_buf[RMON_MAX_LEN] ;
    socklen_t len = sizeof(struct sockaddr_in) ;
    int rc; 
    unsigned int port = 0 ;
    bool found = false;
    int total_clients = clients; 
    char str[RMON_MAX_LEN] ;
    char registered_not[RMON_MAX_LEN];
    FILE * pFile;

    memset ( buf,0,RMON_MAX_LEN);
    memset ( str,0,RMON_MAX_LEN);
    bytes = recvfrom( rmon_sock.rmon_tx_sock, buf, RMON_MAX_LEN, 0, (struct sockaddr *)&rmon_sock.rmon_tx_addr, &len);
    if ( bytes > 0 )
    {
        sscanf ( buf, "%99s %99s %u", str, registered_not, &port ); //RMON_MAX_LEN is defined as 100
        strcpy( active_buf, buf );

        if ( strcmp(str, RMON_RESOURCE_NOT) != 0 )
        {
            for (int j=0; j<clients; j++) {
                registered_clients * clt = get_registered_clients_ptr(j);

                if( strcmp(clt->client_name, str) == 0 ) { 

                    found = true;
                    memset ( buf,0,RMON_MAX_LEN );
                    strcpy( buf, "client_already_registered");
                    send_response(buf, clt);

                    if (strcmp(CLR_CLIENT, registered_not) == 0) {
                        /* the client process wants to deregister, delete it and close it's socket */
                        total_clients = delete_client(clients, j);
                        memset ( buf,0,RMON_MAX_LEN);
                        strcpy( buf, "deregistered_client");
                        send_response(buf, clt);
                        break;       
                     }
                 break; 
               }  
            }

            /* only add a client process if it is not already added */  
            if (!found) {

                ilog("registering client \n");
                if ( str[0] != '\0' )
                {  
                    rc = open_resource_socket(str, registered_not, port);     

                    if (rc == FAIL) {
                        dlog("resource client port open failed \n");
                    } else if (rc==PASS) {

                        memset ( buf,0,RMON_MAX_LEN );
                        strcpy( buf, "registered_client");
                        registered_clients * clt = get_registered_clients_ptr(clients);
                        send_response(buf, clt);
                        total_clients++;
                        /* Add the client to the active clients file */
                        pFile = fopen (RMON_API_ACTIVE_DIR , "a+");
                        if (pFile)
                        {
                            // take out a writer lock on this file to
                            // ensure that no other entity is writing to it
                            // at this time
                            int lock = flock(fileno(pFile), LOCK_EX);
                            if (lock < 0)
                            {
                                elog("Failed to get an exclusive on"
                                     " '%s' (errno: %d)", RMON_API_ACTIVE_DIR, errno);
                            }
                            else
                            {
                                fprintf(pFile, "%s\n", active_buf);
                                // release the lock
                                flock(fileno(pFile), LOCK_UN);
                            }
                            fclose(pFile);
                        }
                        else
                        {
                            elog("Failed to open file %s", RMON_API_ACTIVE_DIR);
                        }
                    }
                }
                else
                {
                    wlog ("Null string !\n");
                }
            } 
      } 
      else if ( strcmp(str, RMON_RESOURCE_NOT) == 0 ) {
          /* read the dynamic file systems file and send a response back */
          process_dynamic_fs_file();
      }
    }
    else if (( 0 > bytes ) && ( errno != EINTR ) && ( errno != EAGAIN ))
    {
        wlog_throttled ( count , MAX_T, "receive error (%d:%s)\n", errno, strerror(errno));
    }  

    return total_clients;
}

/* send resource response */
int rmon_resource_response ( int clients )
{
    int rc = FAIL ;
    
    for (int j=0; j<clients; j++) {

        registered_clients * clt = get_registered_clients_ptr(j);
        clt->waiting = true; 
        if(( strcmp(clt->registered_not, RMON_RESOURCE_NOT) == 0)) { 
            /* only send to clients that are registered for the rmon api updates */
            clt->rx_sequence = 0 ;
            memset  ( clt->msg.tx_buf, 0, RMON_MAX_LEN );
            strcpy( clt->msg.tx_buf, "done_reading_dynamic_file_systems") ;
            dlog("sending: %s on socket: %d bytes: %lu \n", clt->msg.tx_buf, clt->msg.tx_sock, strlen(clt->msg.tx_buf));  
            rc = sendto (clt->msg.tx_sock, 
                         clt->msg.tx_buf ,
                         strlen ( clt->msg.tx_buf), 0,
                (struct sockaddr *) &clt->msg.tx_addr, 
                 sizeof(struct sockaddr_in)); 
             if ( rc < 0 )
             {
                 elog ("%s sendto error (%d:%s) (%s) (%s)\n", 
                      clt->client_name,
                      errno , strerror(errno), 
                      clt->msg.tx_buf, 
                      inet_ntoa(clt->msg.tx_addr.sin_addr));
                 clt->send_err_cnt++ ;
             }
             else
             {           
                 mlog ("%s\n", &clt->msg.tx_buf[0] );
                 clt->waiting = false;
                 clt->send_err_cnt = 0;
                 clt->send_msg_count++ ;
                 rc = PASS ;
             }
            /*
             * In certain rare instances, the UDP response packet
             * sent back to the rmon client (over localhost), may
             * be lost, resulting in the rmon client waiting indefinately
             * (or until timeout). As a fail-safe, we will also set an
             * the acknowledgement flag file that the client can
             * look at on timeout
             */
            daemon_log(RESPONSE_RMON_RESOURCE_NOT, "");
        } 
    }
    return (rc);
}

/* send rmon resource set and clear alarm messages to registered client processes */
int rmon_send_request ( resource_config_type * ptr, int clients)
{
    dlog("%s, number of clients: %d\n", ptr->resource, clients);
    int rc = FAIL ;
    int total_clients = clients;

    for (int j=0; j<clients; j++) {

        registered_clients * clt = get_registered_clients_ptr(j);
        clt->waiting = true;

        dlog("registered notification client: %s\n", clt->registered_not);
        if(( strcmp(clt->registered_not, ptr->resource) == 0) || ( strcmp(clt->registered_not, ALL_USAGE) == 0)) {
            /* only send to clients that are registered for the resource type in question */
            clt->rx_sequence = 0 ;
            memset  ( clt->msg.tx_buf, 0, RMON_MAX_LEN );
            sprintf ( clt->msg.tx_buf, "%s %u", ptr->errorMsg, ++clt->tx_sequence ) ;
            mlog( "%s sending: %s on socket: %d bytes: %lu\n",
                      ptr->resource,
                      clt->msg.tx_buf,
                      clt->msg.tx_sock,
                      strlen(clt->msg.tx_buf));
            rc = sendto (clt->msg.tx_sock,
                         clt->msg.tx_buf ,
                         strlen ( clt->msg.tx_buf), 0,
                (struct sockaddr *) &clt->msg.tx_addr,
                 sizeof(struct sockaddr_in));
             if ( rc < 0 )
             {
                 elog ("%s %s sendto error (%d:%s) rc: (%d) (%s) (%s)\n",
                      ptr->resource,
                      clt->client_name,
                      errno , strerror(errno),
                      rc,
                      clt->msg.tx_buf,
                      inet_ntoa(clt->msg.tx_addr.sin_addr));
                 clt->send_err_cnt++ ;
                 if (clt->send_err_cnt >= MAX_ERR_CNT) {
                     /* assume the client process is killed, deregister the client */
                     ilog("%s client process: %s is not responding, deregistering it \n", ptr->resource, clt->client_name);
                     total_clients = delete_client(clients, j);
                     update_total_clients(total_clients);
                 }
             }
             else
             {
                 mlog ("%s %s\n", ptr->resource, &clt->msg.tx_buf[0] );
                 clt->waiting = false;
                 clt->send_err_cnt = 0;
                 clt->send_msg_count++ ;
                 rc = PASS ;
             }
        }
    }
    return (rc);
}

/* send rmon interface resource set and clear alarm messages to registered client processes */
int send_interface_msg ( interface_resource_config_type * ptr, int clients)
{
    int rc = FAIL ;
    int total_clients = clients; 

    for (int j=0; j<clients; j++) {

        registered_clients * clt = get_registered_clients_ptr(j);
        clt->waiting = true;
        if(( strcmp(clt->registered_not, ptr->resource) == 0) || ( strcmp(clt->registered_not, ALL_USAGE) == 0)) {
            /* only send to clients that are registered for the resource type in question */
            clt->rx_sequence = 0 ;
            memset  ( clt->msg.tx_buf, 0, RMON_MAX_LEN );
            sprintf ( clt->msg.tx_buf, "%s %u", ptr->errorMsg, ++clt->tx_sequence ) ;
            mlog("sending: %s on socket: %d bytes: %lu\n",
                  clt->msg.tx_buf,
                  clt->msg.tx_sock,
                  strlen(clt->msg.tx_buf));
            rc = sendto (clt->msg.tx_sock,
                         clt->msg.tx_buf ,
                         strlen ( clt->msg.tx_buf), 0,
                (struct sockaddr *) &clt->msg.tx_addr,
                 sizeof(struct sockaddr_in));
             if ( 0 >= rc )
             {
                 elog ("%s sendto error (%d:%s) (%s) (%s)\n",
                      clt->client_name,
                      errno , strerror(errno),
                      clt->msg.tx_buf,
                      inet_ntoa(clt->msg.tx_addr.sin_addr));
                 clt->send_err_cnt++ ;
                 if (clt->send_err_cnt >= MAX_ERR_CNT) {
                     /* assume the client process is killed, deregister the client */
                     ilog("client process: %s is not responding, deregistering it \n", clt->client_name);
                     total_clients = delete_client(clients, j);
                     update_total_clients(total_clients);
                 }
             }
             else
             {           
                 mlog ("%s\n", &clt->msg.tx_buf[0] );
                 clt->waiting = false;
                 clt->send_err_cnt = 0;
                 clt->send_msg_count++ ;
                 rc = PASS ;
             }
        } 
    }
    return (rc);
}
