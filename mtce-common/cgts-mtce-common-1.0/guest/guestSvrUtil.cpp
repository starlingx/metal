/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <errno.h>

using namespace std;

#include "nodeBase.h"       /* for ...                                      */
#include "nodeEvent.h"      /* for ... inotify_event_queue_type and utils   */
#include "nodeTimers.h"     /* maintenance timer utilities start/stop       */

#include "guestInstClass.h" 
#include "guestUtil.h"      /* for ... guestUtil_inst_init                  */
#include "guestSvrUtil.h"   /* for ... this module header                   */
#include "guestVirtio.h"    /* for ... virtio_check_filename,
                                       virtio_channel_add                   */

/*****************************************************************************
 * 
 * Name   : guestUtil_close_channel
 *
 * Purpose: Close the specified channel's virtio channel file descriptor. 
 *
 ******************************************************************************/
int guestUtil_close_channel ( instInfo * instInfo_ptr )
{
    int rc = FAIL_NOT_FOUND ;
    if ( instInfo_ptr )
    {
        /* Free up the inotify watch */
        if ( instInfo_ptr->inotify_file_fd )
        {
            dlog ("%s freeing inotify resource\n", log_prefix(instInfo_ptr).c_str() );
            set_inotify_close (instInfo_ptr->inotify_file_fd , 
                               instInfo_ptr->inotify_file_wd );
        }

        if ( instInfo_ptr->chan_fd )
        {
            dlog ("%s closing socket %d\n", 
                      log_prefix(instInfo_ptr).c_str(),
                      instInfo_ptr->chan_fd );

            close ( instInfo_ptr->chan_fd );
            instInfo_ptr->chan_fd  = 0     ;
        }
        instInfo_ptr->chan_ok      = false ;
        instInfo_ptr->heartbeating = false ;
        instInfo_ptr->connected    = false ;
        rc = PASS ;
    }
    return (rc);
}

/*****************************************************************************
 * 
 * Name   : guestUtil_load_channels
 *
 * Purpose: Scan the Virtio Qemu directory looking for heartbeat channels
 *          into guests. 
 *
 * Load those that are found into the control structure 
 * and setup messaging to them.
 *
 ******************************************************************************/
void guestUtil_load_channels ( void ) 
{
    DIR *dirp;
    struct dirent entry;
    struct dirent *result;

    dirp = opendir(QEMU_CHANNEL_DIR);
    if (!dirp)
    {
        elog("failed to open %s directory (%d:%m)\n", QEMU_CHANNEL_DIR, errno);
    }
    else
    {
        dlog ("Searching %s directory\n", QEMU_CHANNEL_DIR);
        while(0 == readdir_r(dirp, &entry, &result))
        {
            if (!result)
                break;

            if ( virtio_check_filename (result->d_name) )
            {
                string channel = result->d_name ;
                ilog ("%s found\n", channel.c_str() );
                if ( virtio_channel_add  ( result->d_name ) == PASS )
                {
                    if ( virtio_channel_connect ( channel ) != PASS )
                    {
                        string uuid = virtio_instance_name ( result->d_name ) ;
                        get_instInv_ptr()->reconnect_start ( uuid.data() );
                    }
                }
            }
            else
            {
                dlog3 ("ignoring file %s\n", result->d_name);
            }
        }
        closedir(dirp);
    }
}

/*****************************************************************************
 * 
 * Name   : guestUtil_channel_search
 *
 * Purpose: Scan the Virtio Qemu directory looking for heartbeat channels
 *          into guests that are not currently provisioned.
 *
 ******************************************************************************/
void guestUtil_channel_search ( void ) 
{
    DIR *dirp;
    struct dirent entry;
    struct dirent *result;

    dirp = opendir(QEMU_CHANNEL_DIR);
    if (!dirp)
    {
        elog("failed to open %s directory (%d:%m)\n", QEMU_CHANNEL_DIR, errno);
    }
    else
    {
        dlog ("Searching %s directory\n", QEMU_CHANNEL_DIR);
        while(0 == readdir_r(dirp, &entry, &result))
        {
            if (!result)
                break;

            if ( virtio_check_filename (result->d_name) )
            {
                if ( get_instInv_ptr()->get_inst ( virtio_instance_name (result->d_name).data()) == NULL )
                { 
                    string channel = result->d_name ;
                    ilog ("found %s\n", channel.c_str() );
                    virtio_channel_add  ( result->d_name );
                    virtio_channel_connect ( channel );
                }
            }
        }
        closedir(dirp);
    }
}

/*****************************************************************************
 *
 * Name   : guestUtil_inotify_events
 *
 * Purpose: Handle inotify events for the specified file descriptor.
 *
 *****************************************************************************/
int guestUtil_inotify_events ( int fd )
{
    string channel = "" ;
    inotify_event_queue_type event_queue ;
    int num = get_inotify_events ( fd , event_queue ) ;

    dlog3 ("inotify events queued: %d\n", num );

    for ( int i = 0 ; i < num ; i++ )
    {
        dlog2 ( "Event:%s for file:%s\n", get_inotify_event_str(event_queue.item[i].event), event_queue.item[i].name );

        if ( event_queue.item[i].event == IN_CREATE )
        {
            dlog1 ("%s CREATE event on %s\n", event_queue.item[i].name, QEMU_CHANNEL_DIR );
            if ( virtio_check_filename (&event_queue.item[i].name[0]) )
            {
                dlog ("%s CREATE accepted\n", event_queue.item[i].name );
                channel = event_queue.item[i].name ;
                if ( virtio_channel_add ( event_queue.item[i].name ) != PASS )
                {
                    elog ("%s failed to add detected channel\n", event_queue.item[i].name );
                }
            }
        }
        else if ( event_queue.item[i].event == IN_DELETE )
        {
            dlog1 ("%s DELETE event on %s\n", event_queue.item[i].name, QEMU_CHANNEL_DIR );
            if ( virtio_check_filename (&event_queue.item[i].name[0]) )
            {
                dlog ("%s DELETE accepted\n", event_queue.item[i].name );
                channel = event_queue.item[i].name ;
                get_instInv_ptr()->del_inst ( channel );
            }
            else
            {
                dlog ("%s DELETE rejected\n", event_queue.item[i].name );
            }
        }
        else if ( event_queue.item[i].event == IN_MODIFY )
        {
            dlog1 ("%s MODIFY event on %s\n", event_queue.item[i].name, QEMU_CHANNEL_DIR );
            if ( virtio_check_filename (&event_queue.item[i].name[0]) )
            {
                dlog ("%s MODIFY accepted\n", event_queue.item[i].name );
                channel = event_queue.item[i].name ;

                /* if the channel was modified then we need 
                 *
                 * 1. to close the channel, 
                 * 2. delete it, 
                 * 3. re-add it and 
                 * 4. then repoen it.
                 * */
                get_instInv_ptr()->del_inst ( channel );

                if ( virtio_channel_add ( event_queue.item[i].name ) != PASS )
                {
                    elog ("%s failed to re-add modified channel\n", channel.c_str());
                }
            }
        }
        else
        {
            wlog ("%s UNKNOWN event on %s\n", event_queue.item[i].name, QEMU_CHANNEL_DIR );
        }
    }
    return (PASS);
}
