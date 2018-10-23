/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Resource Monitor Client Notification API Library
  * See rmon_api.h for API header.
  *
  **/

#include "rmon_api.h"
#include <sys/file.h>

/* Pass code */
#ifndef PASS
#define PASS  (0)
#endif

/* Fail Code */
#ifndef FAIL
#define FAIL  (1)
#endif

/* Retry Code */
#ifndef RETRY
#define RETRY (2)
#endif

/* maximum string and socket endpoint path length */
#define RMON_MAX_LEN (100)

/* initialization signature to gate functional
 * api calls made prior to initialization */
#define INIT_SIG (0xffffdead)

/* rmon default messaging port */
#define RMONTXPORT 2300

/** Control Structure */
typedef struct
{
    unsigned int             init ; /**< Init signature                      */

    int                   client_rx_sock ; /**< inet pulse request rx socket        */
    int                   client_rx_port ; /**< inet pulse request rx port number   */
    struct sockaddr_in    client_rx_addr ; /**< inet pulse request rx attributes    */
    char     client_rx_buf[RMON_MAX_LEN] ;

    int                   rmon_tx_sock ; /**< inet pulse response tx socket       */
    int                   rmon_tx_port ; /**< inet pulse response tx port number  */
    struct sockaddr_in    rmon_tx_addr ; /**< inet pulse response tx attributes   */
    char     rmon_tx_buf[RMON_MAX_LEN] ;

    int                   rmon_rx_sock ; /**< inet pulse response rx socket       */
    int                   rmon_rx_port ; /**< inet pulse response rx port number  */
    struct sockaddr_in    rmon_rx_addr ; /**< inet pulse response rx attributes   */
    char     rmon_rx_buf[RMON_MAX_LEN] ;

    char       name[RMON_MAX_LEN] ; /**< name of process using this instance */

    bool               debug_mode ; /**< debug mode if true                  */
    int                fit_code   ; /**< fit code MAGIC, SEQ, PROCESS        */
} resource_mon_socket_type ;

/* Instance Control Structure - Per Process Private Data */
static resource_mon_socket_type rmon ;
/* Mutex For sending client process information to rmon */
pthread_mutex_t client_mutex;

int remove_rmon_client( const char * process_name_ptr, int socket );

int add_rmon_client ( const char * process_name_ptr, int port , const char * registration, int rx_port);

int resource_monitor_initialize ( const char * process_name_ptr, int port , const char * registration);

int resource_monitor_deregister( const char * process_name_ptr, int socket );

int  resource_monitor_get_sel_obj ( void );

int remove_rmon_client( const char * process_name_ptr, int socket );

void resource_monitor_finalize ();

int create_tx_socket();

/* Create and Setup Inet Transmit Socket 
 * return PASS (0) on success 
 * -# on kernel call error
 * non-zero on internal error
 *
 **/
int create_tx_socket( int rx_port )
{
    int val = 1 ;
    int ok  = 1 ;

    rmon.rmon_tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( 0 >= rmon.rmon_tx_sock )
    {
        syslog ( LOG_ERR, "create_tx_socket failed to create 'tx' socket (%d:%m)", errno );
        return (-errno);
    }

    if ( setsockopt ( rmon.rmon_tx_sock , SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1 )
    {
        syslog ( LOG_WARNING, "create_tx_socket failed to set 'tx' socket as reusable (%d:%m)", errno );
    }

    /* Setup with localhost ip */
    memset(&rmon.rmon_tx_addr, 0, sizeof(struct sockaddr_in));
    rmon.rmon_tx_addr.sin_family = AF_INET ;
    rmon.rmon_tx_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    rmon.rmon_tx_addr.sin_port = htons(RMONTXPORT) ;
    rmon.rmon_tx_port = RMONTXPORT ;

    /* Set socket to be non-blocking.  */
    int rc = ioctl(rmon.rmon_tx_sock, FIONBIO, (char *)&ok);
    if ( 0 > rc )
    {
        syslog ( LOG_WARNING, "create_tx_socket failed to set 'tx' socket as non-blocking (%d:%m)\n", errno );
    }

    /* if the sock is already open then close it first */
    if ( rmon.rmon_rx_sock )
    {
         close (rmon.rmon_rx_sock);
    }

    rmon.rmon_rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( 0 >= rmon.rmon_rx_sock )
    {
        syslog ( LOG_WARNING, "create_rx_socket failed (%d:%m)\n", errno );
        return (-errno);
    }
    if ( setsockopt ( rmon.rmon_rx_sock , SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1 )
    {
        syslog ( LOG_WARNING, "create_tx_socket failed to set 'rx' socket as reusable (%d:%m)", errno );
    }

    /* Setup with localhost ip */
    memset(&rmon.rmon_rx_addr, 0, sizeof(struct sockaddr_in));
    rmon.rmon_rx_addr.sin_family = AF_INET ;
    rmon.rmon_rx_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    rmon.rmon_rx_addr.sin_port = htons(rx_port) ;
    rmon.rmon_rx_port = rx_port ;

    /* Set socket to be non-blocking.  */
    rc = ioctl(rmon.rmon_rx_sock, FIONBIO, (char *)&ok);
    if ( 0 > rc )
    {
         syslog ( LOG_ERR, "create_tx_socket failed to set 'rx' socket as non-blocking (%d:%m)\n", errno );
         return -errno;
    }
    /* bind socket to the receive addr */
    if ( bind ( rmon.rmon_rx_sock, (const struct sockaddr *)&rmon.rmon_rx_addr, sizeof(struct sockaddr_in)) == -1 )
    {
        syslog ( LOG_ERR, "failed to bind rmon 'rx' socket with port %d (%d:%m)\n", rx_port, errno );
        close  (rmon.rmon_rx_sock);
        rmon.rmon_rx_sock = 0 ;
        return -errno;
    }
    return PASS;
}

/* open lo socket */
int add_rmon_client ( const char * process_name_ptr, int port , const char * registration, int rx_port)
{
    struct stat p ;
    int val = 1 ;
    memset ( &rmon, 0, sizeof(rmon));
    memset ( &p, 0 , sizeof(struct stat));

    if ( registration == NULL )
    {
        syslog ( LOG_INFO, "resource_monitor_initialize called with null registration info");
        return (0);
    }

    syslog ( LOG_INFO , "Add Client '%s' to rmon (port:%d)\n", registration, port );

    sprintf ( rmon.name, "/var/run/%s.rmon", process_name_ptr );

    stat ( rmon.name, &p ) ;
    if ((p.st_ino != 0 ) && (p.st_dev != 0))
    {
        rmon.debug_mode = true ;
        syslog ( LOG_INFO, "Enabling resource Monitor Debug Mode\n");
        if ( p.st_size )
        {
            FILE * filename = fopen ( rmon.name, "rb" ) ;
            if ( filename != NULL )
            {
                memset ( &rmon.name, 0, RMON_MAX_LEN);
                if ( fgets  ( rmon.name, 20, filename ) != NULL )
                {
                    if ( !strncmp ( rmon.name, FIT_MAGIC_STRING, strlen (FIT_MAGIC_STRING)))
                    {
                        rmon.fit_code = FIT_MAGIC ;
                        syslog ( LOG_INFO, "Enabling FIT on 'magic calculation'\n");
                    }
                    else if ( !strncmp ( rmon.name, FIT_SEQUENCE_STRING, strlen(FIT_SEQUENCE_STRING)))
                    {
                        rmon.fit_code = FIT_SEQ ;
                        syslog ( LOG_INFO, "Enabling FIT on 'sequence number'\n");
                    }
                    else if ( !strncmp ( rmon.name, FIT_PROCESS_STRING, strlen(FIT_PROCESS_STRING)))
                    {
                        rmon.fit_code = FIT_PROCESS ;
                        syslog ( LOG_INFO, "Enabling FIT on 'process name'\n");
                    }
                    else
                    {
                        syslog ( LOG_INFO, "Unsupported FIT string (%s)\n", rmon.name );
                    }
                }
                fclose (filename);
            }
            else
            {
                syslog ( LOG_INFO, "Failed to open %s\n", rmon.name);
            }
        }
    }
    /* Init the control struct - includes all members */
    memset (  rmon.name, 0, RMON_MAX_LEN);

    if ( process_name_ptr )
    {
        memcpy ( rmon.name, process_name_ptr, strlen (process_name_ptr)) ;
    }
    else
    {
        syslog ( LOG_INFO, "resource_monitor_initialize called with null process name");
        return (0);
    }

    /*******************************************************/
    /*     Create and Setup Inet Receive Socket            */
    /*******************************************************/
    rmon.client_rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( 0 >= rmon.client_rx_sock )
    {
        syslog ( LOG_INFO, "add_rmon_client error:1");
        return (0);
    }

    if ( setsockopt ( rmon.client_rx_sock , SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1 )
    {
        syslog ( LOG_INFO, "%s failed to set socket as re-useable (%d:%s)\n",
                  process_name_ptr, errno, strerror(errno));
    }

    /* Setup with localhost ip */
    memset(&rmon.client_rx_addr, 0, sizeof(struct sockaddr_in));
    rmon.client_rx_addr.sin_family = AF_INET ;
    rmon.client_rx_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    rmon.client_rx_addr.sin_port = htons(port) ;
    rmon.client_rx_port = port ;

    /* bind socket to the receive addr */
    if ( bind ( rmon.client_rx_sock, (const struct sockaddr *)&rmon.client_rx_addr, sizeof(struct sockaddr_in)) == -1 )
    {
        syslog ( LOG_ERR, "failed to bind to rx socket with port %d\n", port );
        close  (rmon.client_rx_sock);
        rmon.client_rx_sock = 0 ;
        return (0);

    }

    int rc = create_tx_socket ( rx_port );

    if (rc != PASS )
    {
        syslog ( LOG_ERR, "add_rmon_client failed to create_tx_socket (rc:%d)", rc );
        return (0);
    }
	if ((registration != NULL) && (rc == PASS))
	{
        int bytes = 0;
#ifdef WANT_CLIENT_REGISTER_SOCKET_SEND
        socklen_t len = sizeof(struct sockaddr_in) ;

        /* client registering, send rmon the resources registered for */
		memset(rmon.rmon_tx_buf, 0, sizeof(rmon.rmon_tx_buf));
		snprintf(rmon.rmon_tx_buf, sizeof(rmon.rmon_tx_buf), "%s %s %d", process_name_ptr, registration, port);
        bytes = sendto ( rmon.rmon_tx_sock, &rmon.rmon_tx_buf, strlen(rmon.rmon_tx_buf), 0,
                       (struct sockaddr *) &rmon.rmon_tx_addr, sizeof(struct sockaddr_in));
        fd_set readfds;
        struct timeval waitd;
        bytes = 0;

        FD_ZERO(&readfds);
        FD_SET(rmon.rmon_rx_sock, &readfds);

        waitd.tv_sec  = WAIT_DELAY;
        waitd.tv_usec = 0;
        /* This is used as a delay up to select_timeout */
        select(FD_SETSIZE, &readfds, NULL, NULL, &waitd);

        if (FD_ISSET(rmon.rmon_rx_sock, &readfds))
        {
            /* wait for the response from rmon to verify that the client is registered */
            memset(rmon.rmon_rx_buf, 0, sizeof(rmon.rmon_rx_buf));
            rmon.rmon_rx_buf[0] = 0;
            bytes = recvfrom( rmon.rmon_rx_sock, rmon.rmon_rx_buf, RMON_MAX_LEN, 0, (struct sockaddr *)&rmon.rmon_rx_addr, &len );
        }
#endif
        if (bytes <= 0) {
            /* no respone, write the client name and notification to a file for later use */
            FILE * pFile;
            memset(rmon.rmon_rx_buf, 0, sizeof(rmon.rmon_rx_buf));
        	snprintf(rmon.rmon_rx_buf, sizeof(rmon.rmon_rx_buf), "%s %s %d", process_name_ptr, registration, port);
            pFile = fopen (RMON_API_REG_DIR , "a+");
            if ( pFile )
            {
                // take out a writer lock on this file to
                // ensure that no other entity is writing to it
                // at this time
                int lock = flock(fileno(pFile), LOCK_EX);
                if (lock < 0) {
                    syslog (LOG_ERR, "Failed to get exclusive lock on"
                            " '%s' (errno: %d)", RMON_API_REG_DIR, errno);
                } else {
                    fprintf(pFile, "%s\n", rmon.rmon_rx_buf);
                    // release write lock
                    flock(fileno(pFile), LOCK_UN);
                }
                fclose(pFile);
            }
            else
            {
                syslog ( LOG_ERR, "Failed to open '%s'\n", RMON_API_REG_DIR );
            }
        }
        else
        {
            syslog ( LOG_ERR, "add_rmon_client send message succeeded");
        }

        /* Set init sig */
        rmon.init = INIT_SIG ;

        /* Return the socket descriptor */
        return (rmon.client_rx_sock);
	}
    else
    {
        syslog ( LOG_ERR, "Failed register due to previous failure\n");
    }
    return (0);
}

int rmon_notification ( const char * notification_name )
{
	int port = RMONTXPORT;
    int rc;

    /* send the message to check the dynamic file */
	memset(rmon.rmon_tx_buf, 0, sizeof(rmon.rmon_tx_buf));
	snprintf(rmon.rmon_tx_buf, sizeof(rmon.rmon_tx_buf), "%s %s %d", notification_name, RESOURCE_NOT, port);
	rc = sendto ( rmon.rmon_tx_sock, &rmon.rmon_tx_buf, strlen(rmon.rmon_tx_buf), 0,
            (struct sockaddr *) &rmon.rmon_tx_addr, sizeof(struct sockaddr_in));
    return rc;
}

int resource_monitor_initialize ( const char * process_name_ptr, int port , const char * registration)
{
   /* use a mutex to prevent multiple clients from registering at once */
   int clt_rx_sock;
   int rx_port = port - 1;

   pthread_mutex_lock(&client_mutex);
   clt_rx_sock = add_rmon_client(process_name_ptr, port , registration, rx_port );
   pthread_mutex_unlock(&client_mutex);

   return clt_rx_sock;

}

int resource_monitor_deregister( const char * process_name_ptr, int socket )
{
   /* use a mutex to prevent multiple clients from de-registering at once */
   int rc;
   pthread_mutex_lock(&client_mutex);
   rc = remove_rmon_client(process_name_ptr, socket);
   pthread_mutex_unlock(&client_mutex);

   return rc;
}


/* */
int  resource_monitor_get_sel_obj ( void )
{
    if (( rmon.init != INIT_SIG ) || ( rmon.client_rx_sock <= 0 ))
    {
        syslog (LOG_WARNING , "'%s' called with invalid init (sock:%d)\n",
                              __FUNCTION__, rmon.client_rx_sock);
    }

    return (rmon.client_rx_sock);
}

int remove_rmon_client( const char * process_name_ptr, int socket )
{
	int rc;
	int port = RMONTXPORT;
    int bytes;
    socklen_t len = sizeof(struct sockaddr_in);

    /* client deregistering, send rmon the client process name */
	memset(rmon.rmon_tx_buf, 0, sizeof(rmon.rmon_tx_buf));
	snprintf(rmon.rmon_tx_buf, sizeof(rmon.rmon_tx_buf), "%s %s %d", process_name_ptr, CLR_CLIENT, port);
	rc = sendto ( rmon.rmon_tx_sock, &rmon.rmon_tx_buf, strlen(rmon.rmon_tx_buf), 0,
                (struct sockaddr *) &rmon.rmon_tx_addr, sizeof(struct sockaddr_in));
    sleep(WAIT_DELAY);
    /* wait for the response from rmon to verify that the client is de-registered */
    memset(rmon.rmon_rx_buf, 0, sizeof(rmon.rmon_rx_buf));
    rmon.rmon_rx_buf[0] = 0;
    bytes = recvfrom( rmon.rmon_rx_sock, rmon.rmon_rx_buf, RMON_MAX_LEN, 0, (struct sockaddr *)&rmon.rmon_rx_addr, &len);

    if ((bytes <= 0) || (rmon.rmon_rx_buf[0] == 0))  {

        FILE * pFile;
        memset(rmon.rmon_rx_buf, 0, sizeof(rmon.rmon_rx_buf));
    	snprintf(rmon.rmon_tx_buf, sizeof(rmon.rmon_tx_buf), "%s %s %d",
                 process_name_ptr, CLR_CLIENT, port);
        pFile = fopen (RMON_API_DEREG_DIR , "a+");
        if (pFile) {
            // take out a writer lock on this file to
            // ensure that no other entity is writing to it
            // at this time
            int lock = flock(fileno(pFile), LOCK_EX);
            if (lock < 0) {
                syslog (LOG_ERR, "Failed to get exclusive lock on"
                        " '%s' (errno: %d)", RMON_API_DEREG_DIR, errno);
            } else{
                fprintf(pFile, "%s\n", rmon.rmon_rx_buf);
                // release the lock
                flock(fileno(pFile), LOCK_UN);
            }
            fclose(pFile);
        } else {
            syslog (LOG_ERR, "Failed to open '%s'\n",
                    RMON_API_DEREG_DIR );
        }
    }

	if ( socket )
    {
		/* close the client receive port */
        close (socket);
    }
    rc = PASS ;

	return rc;
}

/* close the rmon ports */
void resource_monitor_finalize ()
{
    if ( rmon.rmon_tx_sock )
    {
        close (rmon.rmon_tx_sock);
    }
    if ( rmon.rmon_rx_sock )
    {
        close (rmon.rmon_rx_sock);
    }

}
