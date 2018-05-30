/*
* Copyright (c) 2013-2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/


#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <resolv.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> 
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

using namespace std;

#include "nodeBase.h"
#include "nodeEvent.h"
#include "guestBase.h"
#include "guestUtil.h"
#include "guestVirtio.h"
#include "guestInstClass.h" /* for ... get_inst */

/*****************************************************************************
 * Name   : virtio_check_filename
 * 
 * Purpose: Return valid virtio instance heartbeat messaging socket filenames
 * 
 * Description: 
 *
 * Check a filename, already striped of an directory component,
 * against the expected pattern for a cgcs heartbeat vio socket file.  
 *
 * If satisfied, returns an allocated buffer containing the qemu instance name. 
 * The buffer must be free'd.
 *
 * Returns NULL on failure.
 *
 *****************************************************************************/

const char* host_virtio_dir = "/var/lib/libvirt/qemu";

// Use instance id to substitute the first %s below
const char*     host_virtio_file_format_print = "cgcs.heartbeat.%s.sock";
const char* alt_host_virtio_file_format_print = "wrs.heartbeat.agent.0.%s.sock";

// Must return '2' when scaned, first buffer recieves instance id, second should get a k, and third is unused
const char*          virtio_file_format_scan = "%m[cgcs].%m[heartbeat].%m[^.].soc%m[k]%ms";
const char*     host_virtio_file_format_scan = "cgcs.heartbeat.%m[^.].soc%m[k]%ms";
const char* alt_host_virtio_file_format_scan = "wrs.heartbeat.agent.0.%m[^.].soc%m[k]%ms";

string virtio_instance_name ( char * fn )
{
    string name = "" ;
    char *s1 = NULL;
    char *s2= NULL;
    char *instance_name = NULL;

    int rc = sscanf(fn, host_virtio_file_format_scan, &instance_name, &s1, &s2);
    if (rc != 2)
    {
        dlog3 ("'%s' does not satisfy scan pattern %s\n", fn, host_virtio_file_format_scan);
        if (s1)
        {
            free(s1);
            s1 = NULL;
        }

        if (s2)
        {
            free(s2);
            s2 = NULL;
        }

        if (instance_name)
        {
            free(instance_name);
            instance_name = NULL;
        }

        rc = sscanf(fn, alt_host_virtio_file_format_scan, &instance_name, &s1, &s2);
        if (rc != 2)
        {
            dlog3 ("'%s' does not satisfy scan pattern %s\n", fn, alt_host_virtio_file_format_scan);
            if (instance_name)
            {
                free(instance_name);
                instance_name = NULL;
            }
        }
        else
        {
            /* Valid instance filename found */
            name = instance_name ;
        }
    }
    else
    {
        /* Valid instance filename found */
        name = instance_name ;
    }

    if (s1) free(s1);
    if (s2) free(s2);

    if (instance_name)
    {
        free(instance_name);
    }

    return (name);
}


bool virtio_check_filename ( char * fn )
{
    string instance_name = virtio_instance_name ( fn ) ;
    if ( instance_name.size () == UUID_LEN )
        return true ;
    else
        return false ;
}

/* Add the auto detected channel to the instance list 
 * WARNING: This is where the cgcs.heartbeat.*.sock part is
 *          removed from the channel and put into the instInfo
 *          struct as a uuid value */
int virtio_channel_add ( char * channel )
{
    instInfo * instInfo_ptr ;
    int rc = FAIL_NOT_FOUND ;
    char * prefix1   = NULL ;
    char * prefix2   = NULL ;
    char * suffix   = NULL ;
    char * uuid_ptr = NULL ;
    char * s1       = NULL ;
    string uuid = "";
    instInfo instance ;
    guestUtil_inst_init ( &instance );
    
    rc = sscanf(channel, virtio_file_format_scan, &prefix1, &prefix2, &uuid_ptr, &suffix, &s1 );
    if ( rc != 4 )
    {
        elog ("failed to extract uuid from channel %s (num:%d)\n", channel, rc);
        rc = FAIL_INVALID_DATA ;
        goto virtio_channel_add_cleanup ;
    }
   
    uuid = uuid_ptr ;
    if ( uuid.length() != UUID_LEN )
    {
        elog ("failed to get UUID from channel %s (uuid:%ld)\n", uuid.c_str(), uuid.length());
        rc = FAIL_INVALID_UUID ;
        goto virtio_channel_add_cleanup ;
    }
    

    instInfo_ptr = get_instInv_ptr()->get_inst ( uuid );
    if ( instInfo_ptr )
    {
        /* detected channel found */
        ilog ("%s add ; already provisioned\n", log_prefix(instInfo_ptr).c_str());
        rc = PASS ;
    }
    else if ( ( rc = get_instInv_ptr()->add_inst ( uuid, instance ) ) == PASS )
    {
        dlog ("%s add ; auto provisioned\n", instance.uuid.c_str());
        rc = PASS ;
    }
    else
    {
        elog ("%s add failed\n", uuid.c_str());
        rc = FAIL_INVALID_UUID ;
    }

    if ( rc == PASS )
    {

        /* get the recently added instance */
        instInfo_ptr = get_instInv_ptr()->get_inst ( uuid );
        if ( instInfo_ptr )
        {
            instInfo_ptr->uuid = uuid     ;
            instInfo_ptr->chan = channel  ;
            instInfo_ptr->fd_namespace = QEMU_CHANNEL_DIR ;
            instInfo_ptr->fd_namespace.append ("/") ;
            instInfo_ptr->fd_namespace.append (channel) ;

            instInfo_ptr->connect_wait_in_secs = DEFAULT_CONNECT_WAIT ;

            get_instInv_ptr()->reconnect_start ( (const char *)uuid_ptr ) ;
        }
    }

virtio_channel_add_cleanup:

    if (prefix1)  free(prefix1);
    if (prefix2)  free(prefix2);
    if (suffix)   free(suffix);
    if (uuid_ptr) free(uuid_ptr);
    if (s1)       free (s1);

    return(rc);
}



/*****************************************************************************
 *
 * Name    : virtio_channel_connect
 *
 * Purpose : Connect to the channel specified by the instance pointer
 *
 *****************************************************************************/
int virtio_channel_connect ( instInfo * instInfo_ptr )
{
    int rc = PASS ;
    char buf[PATH_MAX];

    if ( ! instInfo_ptr )
    {
        slog ("called with NULL instance pointer\n");
        return (FAIL_NULL_POINTER);
    }

    snprintf(buf, sizeof(buf), "%s/cgcs.heartbeat.%s.sock", QEMU_CHANNEL_DIR, instInfo_ptr->uuid.data());

    dlog ("... trying connect: %s\n", buf );

    if (( instInfo_ptr->chan_fd > 0 ) && ( instInfo_ptr->chan_ok == true ))
    {
        if ( instInfo_ptr->connected )
        {
            ilog ("%s already connected\n", log_prefix(instInfo_ptr).c_str());
            return (PASS);
        }
        else
        {
             ilog ("%s socket and chan ok but not connected\n", log_prefix(instInfo_ptr).c_str());
        }
    }

    instInfo_ptr->chan_ok   = false ;
    instInfo_ptr->connected = false ;

    if ( instInfo_ptr->chan_fd )
        close (instInfo_ptr->chan_fd);

    /* found channel */
    instInfo_ptr->chan_fd = socket ( AF_UNIX, CHAN_FLAGS, 0 );
    if ( instInfo_ptr->chan_fd <= 0 )
    {
        ilog("%s socket create failed for %s, (%d:%m)\n", log_prefix(instInfo_ptr).c_str(), buf, errno ) ;
        rc = FAIL_SOCKET_CREATE ; 
    }
    else
    {
        int flags ;
        struct linger so_linger ;

        /* get socket flags */
        flags = fcntl(instInfo_ptr->chan_fd, F_GETFL);
        if (flags < 0)
        {
            elog ("%s failed to get socket %d flags (%d:%m)\n", 
                   log_prefix(instInfo_ptr).c_str(),
                   instInfo_ptr->chan_fd , errno);
            rc = FAIL_SOCKET_OPTION ;
        }

        /* set socket as nonblocking */
        if ( flags & O_NONBLOCK )
        {
            dlog ("%s Socket already set as non-blocking\n", 
                      log_prefix(instInfo_ptr).c_str());
        }
        else
        {
            flags = (flags | O_NONBLOCK);
            if (fcntl(instInfo_ptr->chan_fd, F_SETFL, flags) < 0)
            {
                elog ("%s failed to set socket %d nonblocking (%d:%m)\n", 
                       instInfo_ptr->uuid.data(),
                       instInfo_ptr->chan_fd , errno);
                rc = FAIL_SOCKET_NOBLOCK ;
            }
        }
        so_linger.l_onoff  = 1 ; /* true */
        so_linger.l_linger = 0 ; /* linger time is 0 ; no TIME_WAIT */

        rc = setsockopt ( instInfo_ptr->chan_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
        if ( rc )
        {
            elog ("%s failed to set linger=0 option (%d:%m)\n", log_prefix(instInfo_ptr).c_str(), errno );
        }
    }

    if ( rc == PASS )
    {
        int len ;
        struct sockaddr_un un;
        un.sun_family = AF_UNIX;

        strcpy(un.sun_path, buf);
        len = offsetof(struct sockaddr_un, sun_path) + strlen(buf);
        rc = connect(instInfo_ptr->chan_fd, (struct sockaddr *)&un, len);
        if (rc < 0)
        {
            elog ( "%s connect failed %s (%d:%d:%m)\n", 
                       log_prefix(instInfo_ptr).c_str(), buf, rc, errno);
        }
        else
        {
            ilog ("%s connect accepted\n", log_prefix(instInfo_ptr).c_str() );
            instInfo_ptr->chan_ok   = true ;
            instInfo_ptr->connected = true ;
            rc = PASS ;
        }
    }
    /* Handle errors */
    if ( rc != PASS )
    {
        /* TODO: cleanup */
        if (instInfo_ptr->chan_fd )
        {
            ilog ("%s closing socket %d\n", 
                      log_prefix(instInfo_ptr).c_str(), 
                      instInfo_ptr->chan_fd);

            close (instInfo_ptr->chan_fd) ;
            instInfo_ptr->chan_fd = 0     ;
            instInfo_ptr->chan_ok = false ;
            instInfo_ptr->connected = false ;
        }
        /* TODO: consider removing this entry from the list */
    }
    return (rc);
}


int virtio_channel_connect ( string channel )
{
    instInfo * instInfo_ptr = get_instInv_ptr()->get_inst ( channel ) ;
    if ( instInfo_ptr )
    {
        return ( virtio_channel_connect ( instInfo_ptr ));
    }
    elog ("%s instance lookup failed\n", channel.c_str() );
    return (FAIL_NULL_POINTER);
}
