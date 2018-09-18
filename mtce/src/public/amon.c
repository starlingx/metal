/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Active (Process) Monitoring Implementation. 
  * See amon.h for API.
  *
  **/

#include "amon.h"
#include <stdarg.h>

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
#define AMON_MAX_LEN (100)

/* initialization signature to gate functional
 * api calls made prior to initialization */
#define INIT_SIG (0xdeafdead)

#define TXPORT 2200

/** Control Structure */
typedef struct
{
    unsigned int             init ; /**< Init signature                      */

    int                   rx_sock ; /**< inet pulse request rx socket        */
    int                   rx_port ; /**< inet pulse request rx port number   */
    struct sockaddr_in    rx_addr ; /**< inet pulse request rx attributes    */
    char     rx_buf[AMON_MAX_LEN] ; /**< pulse request message               */

    int                   tx_sock ; /**< inet pulse response tx socket       */
    int                   tx_port ; /**< inet pulse response tx port number  */
    struct sockaddr_in    tx_addr ; /**< inet pulse response tx attributes   */
    char     tx_buf[AMON_MAX_LEN] ; /**< pulse response message              */

    char       name[AMON_MAX_LEN] ; /**< name of process using this instance */

    bool               debug_mode ; /**< debug mode if true                  */
    int                fit_code   ; /**< fit code MAGIC, SEQ, PROCESS        */
} active_mon_socket_type ;

/* Instance Control Structure - Per Process Private Data */
static active_mon_socket_type amon ;

/* open unix domain socket */
int active_monitor_initialize ( const char * process_name_ptr, int port )
{
    struct stat p ;
    int val = 1 ;
    memset ( &amon, 0, sizeof(amon));
    memset ( &p, 0 , sizeof(struct stat));
    
    syslog ( LOG_INFO , "%s is actively Monitored over lo:%d:%x\n", process_name_ptr, port, port );

    sprintf ( amon.name, "/var/run/%s.debug", process_name_ptr );
    
    stat ( amon.name, &p ) ;
    if ((p.st_ino != 0 ) && (p.st_dev != 0))
    {
        amon.debug_mode = true ;
        syslog ( LOG_INFO, "Enabling Active Monitor Debug Mode\n");
        if ( p.st_size )
        {
            FILE * filename = fopen ( amon.name, "rb" ) ;
            if ( filename != NULL )
            {
                memset ( &amon.name, 0, AMON_MAX_LEN);
                if ( fgets  ( amon.name, 20, filename ) != NULL )
                {
                    if ( !strncmp ( amon.name, FIT_MAGIC_STRING, strlen (FIT_MAGIC_STRING)))
                    {
                        amon.fit_code = FIT_MAGIC ;
                        syslog ( LOG_INFO, "Enabling FIT on 'magic calculation'\n");
                    }
                    else if ( !strncmp ( amon.name, FIT_SEQUENCE_STRING, strlen(FIT_SEQUENCE_STRING)))
                    {
                        amon.fit_code = FIT_SEQ ;
                        syslog ( LOG_INFO, "Enabling FIT on 'sequence number'\n");
                    }
                    else if ( !strncmp ( amon.name, FIT_PROCESS_STRING, strlen(FIT_PROCESS_STRING)))
                    {
                        amon.fit_code = FIT_PROCESS ;
                        syslog ( LOG_INFO, "Enabling FIT on 'process name'\n");
                    }
                    else
                    {
                        syslog ( LOG_INFO, "Unsupported FIT string (%s)\n", amon.name );
                    }
                }
                fclose (filename);
            }
            else
            {
                syslog ( LOG_INFO, "Failed to open %s\n", amon.name);
            }
        }
    }
    /* Init the control struct - includes all members */
    memset (  amon.name, 0, AMON_MAX_LEN);
    
    if ( process_name_ptr )
    {
        memcpy ( amon.name, process_name_ptr, strlen (process_name_ptr)) ;
    }
    else
    {
        syslog ( LOG_INFO, "active_monitor_initialize called with null process name");
        return (-ESRCH);
    }

    /*******************************************************/
    /*     Create and Setup Inet Receive Socket            */
    /*******************************************************/
    amon.rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( 0 >= amon.rx_sock )
        return (-errno);

    if ( setsockopt ( amon.rx_sock , SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1 )
    {
        syslog ( LOG_INFO, "%s failed to set socket as re-useable (%d:%s)\n", 
                  process_name_ptr, errno, strerror(errno));
    }
   
    /* Setup with localhost ip */
    memset(&amon.rx_addr, 0, sizeof(struct sockaddr_in));
    amon.rx_addr.sin_family = AF_INET ;
    amon.rx_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    amon.rx_addr.sin_port = htons(port) ;
    amon.rx_port = port ;
    
    /* bind socket to the receive addr */
    if ( bind ( amon.rx_sock, (const struct sockaddr *)&amon.rx_addr, sizeof(struct sockaddr_in)) == -1 )
    {
        syslog ( LOG_ERR, "failed to bind to rx socket with port %d\n", port );
        close  (amon.rx_sock);
        amon.rx_sock = 0 ;
        return (-errno);
    }

    /*******************************************************/
    /*     Create and Setup Inet Transmit Socket           */
    /*******************************************************/
    amon.tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( 0 >= amon.tx_sock )
        return (-errno);
    
    /* Setup with localhost ip */
    memset(&amon.tx_addr, 0, sizeof(struct sockaddr_in));
    amon.tx_addr.sin_family = AF_INET ;
    amon.tx_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    amon.tx_addr.sin_port = htons(TXPORT) ;
    amon.tx_port = TXPORT ;

    /*******************************************************************/

    /* Set init sig */
    amon.init = INIT_SIG ;

    /* Return the socket descriptor */
    return (amon.rx_sock);
}

/* */
int  active_monitor_get_sel_obj ( void )
{
    if (( amon.init != INIT_SIG ) || ( amon.rx_sock <= 0 ))
    {
        syslog (LOG_WARNING , "'%s' called with invalid init (sock:%d)\n", 
                              __FUNCTION__, amon.rx_sock);
    }

    return (amon.rx_sock);
}

/* Receive pulse request */ 
int  active_monitor_dispatch ( void )
{
    int msg_cnt = 0 ;
    int rc = RETRY ;
    socklen_t len = sizeof(struct sockaddr_un);

    if ( amon.init != INIT_SIG )
    {
        syslog (LOG_WARNING , "'%s' called with invalid init\n", __FUNCTION__ );
        return (-EPERM);
    }

    do
    {
        memset ( amon.rx_buf, 0 , AMON_MAX_LEN );
        rc = recvfrom ( amon.rx_sock, amon.rx_buf, AMON_MAX_LEN, 0,
                        (struct sockaddr *)&amon.rx_addr, &len);
        if ( rc == -1 )
        {
            if (( errno != EINTR ) && ( errno != EAGAIN ))
            {
                syslog ( LOG_WARNING, "amon:%s 'recvfrom' %s.%d failed (%d:%s)\n", 
                         __FUNCTION__, inet_ntoa(amon.tx_addr.sin_addr), amon.rx_port,
                         errno, strerror(errno));
                rc = -errno ;
            }
            else
            {
                rc = -EAGAIN ;
            }
        }

        /* Otherwise we got a message */
        else
        {
            /* Small Song and Dance 
             *
             * Invert magic number and maintain the sequence number 
             *
             **/
            char str[AMON_MAX_LEN] ;
            unsigned int magic = 0 ;
            int seq ;

            memset (str, 0, AMON_MAX_LEN );
            sscanf ( amon.rx_buf, "%s %8x %d", str, &magic, &seq );
        
            /* Fault Insertion Controls */
            if ( amon.fit_code == FIT_PROCESS )    
            {
                str[0] = 'x' ;
                str[1] = 'x' ;
            }
            if ( amon.fit_code == FIT_SEQ )    
            {
                seq-- ;
            }
            if ( amon.fit_code != FIT_MAGIC )    
            {
                magic = magic ^ -1 ;
            }

            memset ( amon.tx_buf, 0 , AMON_MAX_LEN );
            sprintf( amon.tx_buf, "%s %8x %d%c", str, magic, seq, '\0' );
            
            if ( strcmp ( str, amon.name ) )
            {
                 syslog ( LOG_ERR, "recv message for wrong process: %s:%s (%d)\n", str, amon.name, seq );
            }
            else
            {
                if ( amon.debug_mode )
                {
                    syslog ( LOG_INFO, "recv: %s (%d)\n", amon.rx_buf, seq );
                    syslog ( LOG_INFO, "send: %s (%d)\n", amon.tx_buf, seq );
                }
                rc = sendto ( amon.tx_sock, &amon.tx_buf[0], strlen(amon.tx_buf), 0,
                     (struct sockaddr *) &amon.tx_addr, sizeof(struct sockaddr_in)); 
                if ( rc == -1 )
                {
                    syslog ( LOG_WARNING, "amon:%s 'sendto' %s.%d failed (%d:%s)\n", 
                             __FUNCTION__, inet_ntoa(amon.tx_addr.sin_addr), TXPORT, errno, strerror(errno));
                    syslog ( LOG_WARNING, "amon:%s:%s -> %s\n",
                             __FUNCTION__, amon.rx_buf, amon.tx_buf );
                    rc  = -errno ;
                }
                else
                {
                    rc = PASS ;
                }
            }
        } 
    } while (( rc == RETRY ) && ( msg_cnt < 5 )) ;
    return (rc);
}

void active_monitor_finalize ( void )
{
    if ( amon.tx_sock )
    {
        close (amon.tx_sock);
    }
    if ( amon.rx_sock )
    {
        close (amon.rx_sock);
    }
}
