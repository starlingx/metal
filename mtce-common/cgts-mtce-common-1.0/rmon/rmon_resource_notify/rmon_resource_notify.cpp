/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Resource Monitor Resource Notify  
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
#include <vector>
#include <sstream>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>

using namespace std;

#include "../rmonApi/rmon_nodeMacro.h"     /* for ... CREATE_NONBLOCK_INET_UDP_RX_SOCKET   */
#include "rmon_resource_notify.h"

extern "C"
{
#include "../rmonApi/rmon_api.h"     
}
#define LOOPBACK_IP "127.0.0.1"
#define RX_PORT (2304)

static char   my_hostname [MAX_HOST_NAME_SIZE+1];
static rmon_socket_type rmon_sock   ;
static rmon_socket_type * sock_ptr ;

/** Client Config mask */
#define CONFIG_CLIENT_MASK  (CONFIG_AGENT_PORT        |\
                             CONFIG_CLIENT_API_PORT   |\
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

    CREATE_NONBLOCK_INET_UDP_RX_SOCKET ( LOOPBACK_IP, 
            port, 
            rmon_sock.rmon_api_sock,
            rmon_sock.rmon_api_addr,
            rmon_sock.rmon_api_port,
            rmon_sock.rmon_api_len,
            "rmon api socket receive", 
            rc );
    if ( rc ) return (rc) ;

    /* Open the monitoring socket */
    rmon_sock.rmon_socket = resource_monitor_initialize ( process_name, port, RMON_RESOURCE_NOT );
    //ilog("Resource Monitor API Socket %d\n", rmon_sock.rmon_socket);
    if ( 0 > rmon_sock.rmon_socket )
    {
        close_syslog();
        return (FAIL); 
    }

    /* Make the socket non-blocking */
    rc = ioctl(rmon_sock.rmon_socket, FIONBIO, (char *)&on);
    if ( 0 > rc )
    {
        //elog("Failed to set rmon socket non-blocking (%d:%m)\n", errno );
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
        // elog("socket initialization failed (rc:%d)\n", rc);
        rc = FAIL_SOCKET_INIT;
    }
    return (rc);
}

#define RMON_MAX_LEN (100)
int client_service_inbox ()
{
#define MAX_T 100
    int bytes = 0 ;
    char buf[RMON_MAX_LEN] ;
    socklen_t len = sizeof(struct sockaddr_in) ;
    char str[RMON_MAX_LEN];
    int rc = FAIL;

    do
    {
        memset ( buf,0,RMON_MAX_LEN);
        memset ( str,0,RMON_MAX_LEN);

        bytes = recvfrom( rmon_sock.rmon_socket, buf, RMON_MAX_LEN, 0, (struct sockaddr *)&rmon_sock.client_sockAddr, &len);
        if ( bytes > 0 )
        {
            sscanf ( buf, "%99s", str);
            if ( str[0] != '\0' )
            {
                if ( strcmp(str, RMON_DONE) == 0)
                {
                    return (PASS);
                }
            }
            return (FAIL);
        }
        else if (( 0 > bytes ) && ( errno != EINTR ) && ( errno != EAGAIN ))
        {
            //ilog("problem with test client recv \n");
        }
    } while ( bytes > 0 ) ;

    return rc;
}

/* Maximum length of the dynamic resource list */
#define DYNAMIC_RESOURCE_MAX (1024)

int main ( int argc, char *argv[] )
{
    int rc;
    int port = RX_PORT;
    const char * process_name = PROCESS_NAME;
    char res_name[30];
    char state[20];
    char mount[50]; 
    char type[20];
    char device[50];
    char volume_group[50]; 
    string delimiter = ",";
    unsigned long long  timeout = DEFAULT_RESPONSE_TIMEOUT;
    char dynamic_res[DYNAMIC_RESOURCE_MAX];
    char resource_name [50];
    struct stat fileInfo;
    struct timespec start, stop;
    struct flock fl;
    int fd;
    bool toNotify = false;
    vector<string> dynamic_resources;
    size_t pos;
    string token;

    open_syslog();

    memset ((char *)&fileInfo, 0 , sizeof(fileInfo));
    memset(&res_name[0], 0, sizeof(res_name));
    memset(&state[0], 0, sizeof(state));
    memset(&mount[0], 0, sizeof(mount));
    memset(&type[0], 0, sizeof(type));
    memset(&device[0], 0, sizeof(device));
    memset(&volume_group[0], 0, sizeof(volume_group));

    fl.l_whence = SEEK_SET; 
    fl.l_start  = 0;        
    fl.l_len    = 0;        
    fl.l_pid    = getpid(); 

    if ((argc > 1) && (strcmp(argv[1],"--help") == 0)) {
        printf("usage: rmon_resource_notify "
               "--resource-name <entity_name> "
               "--resource-state <enabled|disabled> "
               "--resource-type <filesystem> "
               "--device <device_name> "
               "--mount-point <mount_path> "
               "--volume_group <volume_name> "
               "--timeout <ms> \n");
        close_syslog();
        return FAIL;
    }

    for (int i=0; i<argc; ++i)
    {
        /* parse the dynamic resource monitor request list/table */
        if (strcmp(argv[i], "--resource-name") == 0) {
            sscanf(argv[i+1], "%29s", res_name);
        }
        else if (strcmp(argv[i], "--resource-state") == 0) {
            sscanf(argv[i+1], "%19s", state);
            if (strcmp(state, "disabled") == 0)
            {
                /* because enabled and disable have the same number of characters */ 
                strcpy(state, "disable");
            }
        }
        else if (strcmp(argv[i], "--resource-type") == 0) {
            sscanf(argv[i+1], "%19s", type);
        }
        else if (strcmp(argv[i], "--device") == 0) {
            sscanf(argv[i+1], "%49s", device);
        }
        else if (strcmp(argv[i], "--mount-point") == 0) {
            sscanf(argv[i+1], "%49s", mount);
        }
        else if (strcmp(argv[i], "--timeout") == 0) {
            sscanf(argv[i+1], "%llu", &timeout);
        }
        else if (strcmp(argv[i], "--volume-group") == 0) {
            sscanf(argv[i+1], "%49s", volume_group);
        }
    }

    if (res_name[0] != '\0') {
        strcpy( dynamic_res, res_name);
        strcat( dynamic_res, " " );
    }
    if (state[0] != '\0') {
        strcat( dynamic_res, state );
        strcat( dynamic_res, " " );
    }
    if (type[0] != '\0') {
        strcat( dynamic_res, type );
        strcat( dynamic_res, " " );
    }
    if (device[0] != '\0') {
        strcat( dynamic_res, device );
    }
    else if (volume_group[0] != '\0')
    {
        strcat( dynamic_res, volume_group );
    }
    if (mount[0] != '\0') {
        strcat( dynamic_res, " " );
        strcat( dynamic_res, mount );
    }
    strcat( dynamic_res, delimiter.c_str() );

    // syslog ( LOG_DEBUG, "dynamic_res: %s\n", dynamic_res);

    if ( stat(DYNAMIC_FS_FILE, &fileInfo) != -1 )
    {
        /*******************************************************************************************
         * Dynamic Resource Monitor Request - Example
         *
         * cat /etc/rmonfiles.d/dynamic.conf
         * nova-local          disable lvg                   nova-local,
         * /etc/nova/instances enabled mount /dev/mapper/nova--local-instances_lv /etc/nova/instances,
         * platform-storage    disable mount /dev/drbd2                           /opt/platform,
         * cloud-storage       disable mount /dev/drbd3                           /opt/cgcs,
         * volume-storage      disable lvg                  cinder-volumes,
         * database-storage    disable mount /dev/drbd0                           /var/lib/postgresql,
         * messaging-storage   disable mount /dev/drbd1                           /var/lib/rabbitmq,
         * extension-storage   disable mount /dev/drbd5                           /opt/extension,
         *
         *********************************************************************************************/
        /* file is yet to be read and written  */
        fd = open(DYNAMIC_FS_FILE, O_RDWR, (mode_t)0600);
        if (fd == -1)
        {
            // elog("Error opening file for read/write");
            close_syslog();
            return(FAIL);
        }
        /* lock the file for read and write  */
        fl.l_type   = F_WRLCK;
        fcntl(fd, F_SETLKW, &fl);

        if (fd == -1)
        {
            // elog("Error opening file for reading");
            close_syslog();
            return (FAIL);
        }

        if (fstat(fd, &fileInfo) == -1)
        {
            // elog("Error getting the file size");
            close_syslog();
            return (FAIL);
        }

        //ilog("File size is %ji\n", (intmax_t)fileInfo.st_size);

        char *map = static_cast<char*>(mmap(0, fileInfo.st_size, PROT_READ, MAP_SHARED, fd, 0));
        if (map == MAP_FAILED)
        {
            close(fd);
            // elog("Error mmapping the file");
            close_syslog();
            return (FAIL);
        }
        string oldFile(map);
        /* extract the resource name */ 
        sscanf(dynamic_res, "%49s", resource_name);
        string newResource(resource_name);
        string updatedResource(dynamic_res);
        dynamic_resources.clear();

        if ( oldFile.find(updatedResource) == string::npos )
        {
            if ( oldFile.find(newResource) != string::npos )
            {
                /* the resource exists, update it in the file */ 
                while ((pos = oldFile.find(delimiter)) != string::npos)
                {
                    /* separate the resources from the file */ 
                    token = oldFile.substr(0, pos);    
                    if (token.find(newResource) == string::npos)
                    {
                        dynamic_resources.push_back(token);
                    }
                    oldFile.erase(0, pos + delimiter.length());
                }
                oldFile = "";
                for (unsigned int i=0; i<dynamic_resources.size(); i++)
                {
                    oldFile.append(dynamic_resources.at(i));
                    oldFile.append(delimiter);
                }
                oldFile.append(updatedResource);
            }
            else 
            {
                /* the resource does not exist, add it to the file */
                //ilog("updated_resource: %s", oldFile.c_str());
                oldFile.append(updatedResource);      
            }
            snprintf ( dynamic_res, DYNAMIC_RESOURCE_MAX-1, "%s", oldFile.data());
            // syslog ( LOG_DEBUG, "%s\n", dynamic_res);
        }
        else 
        {
            /* resource already exists no need to add it */
            memset(dynamic_res, 0, DYNAMIC_RESOURCE_MAX ) ; // sizeof(dynamic_res));
        }

        /* free the mmapped memory */
        if (munmap(map, fileInfo.st_size) == -1)
        {
            /* unlock the file */
            fl.l_type = F_UNLCK;  
            fcntl(fd, F_SETLK, &fl); 
            // elog("Error un-mmapping the file");
            close_syslog();
            return (FAIL);
        }
    }

    /* add or modify the dynamic resource */ 
    else 
    {
        /* file is yet to be written to create a new one */
        fd = open(DYNAMIC_FS_FILE, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
        if (fd == -1)
        {
            // elog("Error opening file for writing");
            close_syslog();
            return(FAIL);
        }  
        /* lock the file for writing */ 
        fl.l_type   = F_WRLCK;  
        fcntl(fd, F_SETLKW, &fl);
    }

    if (dynamic_res[0] != '\0')
    {
        toNotify = true; 
        /* stretch the file size to the size of the (mmapped) array of char */
        string text (dynamic_res);
        //ilog("dynamicres: %s\n", dynamic_res);
        size_t textsize = strlen(text.c_str());  
        
        if (lseek(fd, textsize, SEEK_SET) == -1)
        {
            close(fd);
            // elog("Error calling lseek() to 'stretch' the file");
            close_syslog();
            return (FAIL);
        }
        
        if (write(fd, "", 1) == -1)
        {
            close(fd);
            // elog("Error writing last byte of the file");
            close_syslog();
            return (FAIL);
        }

        /* memory map the file */
        char *map = static_cast<char*>(mmap(0, textsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        if (map == MAP_FAILED)
        {
            close(fd);
            // elog("Error mmapping the file");
            close_syslog();
            return (FAIL);
        }

        /* write the resource into memory */
        memcpy(map, text.c_str(), textsize); 

        /* write the updated list to the disk */
        if (msync(map, textsize, MS_SYNC) == -1)
        {
           ; // elog("Could not sync the file to disk");
        }


        /* free the mmapped memory */
        if (munmap(map, textsize) == -1)
        {
            /* unlock the file */
            fl.l_type = F_UNLCK;  
            fcntl(fd, F_SETLK, &fl); 
            // elog("Error un-mmapping the file");
            close_syslog();
            return (FAIL);
        }
    }
    close(fd);
    /* unlock the file */
    fl.l_type = F_UNLCK;  
    fcntl(fd, F_SETLK, &fl);
    if (!toNotify)
    {
        close_syslog();
        return (PASS);
    }

    /* Check to see if rmond is running */
    rc = system("pidof rmond");
    if (WEXITSTATUS(rc) != 0)
    {
        return (PASS);
    }

    rc = daemon_init(port, process_name);
    if (rc == PASS) {

        if( clock_gettime( CLOCK_MONOTONIC, &start) == -1 ) {
            // elog("clock gettime \n" );
            close_syslog();
            return (FAIL);
        }

        rmon_message_init();
        rmon_sock.rmon_socket = resource_monitor_get_sel_obj ();
        std::list<int> socks;
        socks.clear();   
        socks.push_front ( rmon_sock.rmon_socket );    
        socks.sort();
        // remove the rmon resource notify flag file
        // as this will be reset by rmon
        remove (RESPONSE_RMON_RESOURCE_NOT);
        /* signal to rmon that the dynamic file has been written */ 
        rc = rmon_notification ( RMON_RESOURCE_NOT );

        for ( ;  ; )
        { 
            /* Initialize the timeval struct  */
            rmon_sock.waitd.tv_sec = 0;
            rmon_sock.waitd.tv_usec = SELECT_TIMEOUT * 100;

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
                    //ilog("Socket Select Failed (rc:%d) %s \n", errno, strerror(errno));
                }
            }

            if ( FD_ISSET(rmon_sock.rmon_socket, &rmon_sock.readfds))
            {
                rc = client_service_inbox();

                if (rc == PASS) {
                    close_syslog();
                    return PASS;
                }
            }

            if ( clock_gettime( CLOCK_MONOTONIC, &stop) == -1 ) {
                // elog("clock gettime\n");
                return (FAIL);
            }
            unsigned long delta = (stop.tv_sec - start.tv_sec) * 1000 + (stop.tv_nsec - start.tv_nsec) / 1000000;
            if (delta > timeout) 
            { 
                /* we exceeded the timeout.
                 * It may have happened that rmon
                 * sent its acknowledgment but that response
                 * got lost. In that case check for the flag file
                 * as a last ditch effort
                 */
                if (access(RESPONSE_RMON_RESOURCE_NOT, F_OK) != -1) {
                    close_syslog()
                    return (PASS);
                }
                close_syslog();
                return (FAIL);
            }
        }
    }
    close_syslog();
    return FAIL;  
}

