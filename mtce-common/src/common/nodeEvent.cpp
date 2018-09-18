/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform common iNotify utilities
  */

#include "nodeEvent.h"
#include "nodeBase.h"

// The following are legal, implemented events that user-space can watch for
// -------------------------------------------------------------------------
// #define IN_ACCESS		0x00000001	/* File was accessed */
// #define IN_MODIFY		0x00000002	/* File was modified */
// #define IN_ATTRIB		0x00000004	/* Metadata changed */
// #define IN_CLOSE_WRITE	0x00000008	/* Writtable file was closed */
// #define IN_CLOSE_NOWRITE	0x00000010	/* Unwrittable file closed */
// #define IN_OPEN			0x00000020	/* File was opened */
// #define IN_MOVED_FROM	0x00000040	/* File was moved from X */
// #define IN_MOVED_TO		0x00000080	/* File was moved to Y */
// #define IN_CREATE		0x00000100	/* Subfile was created */
// #define IN_DELETE		0x00000200	/* Subfile was deleted */
// #define IN_DELETE_SELF	0x00000400	/* Self was deleted */

// The following are legal events.  they are sent as needed to any watch
// #define IN_UNMOUNT		0x00002000	/* Backing fs was unmounted */
// #define IN_Q_OVERFLOW	0x00004000	/* Event queued overflowed */
// #define IN_IGNORED		0x00008000	/* File was ignored */

// Helper events //
// #define IN_CLOSE		(IN_CLOSE_WRITE | IN_CLOSE_NOWRITE) /* close */
// #define IN_MOVE		(IN_MOVED_FROM | IN_MOVED_TO) /* moves */

// Special flags
// #define IN_ISDIR		0x40000000	/* event occurred against dir */
// #define IN_ONESHOT	0x80000000	/* only send event once */

 /* Display information from inotify_event structure */
 const char * get_inotify_event_str (int mask )
 {
     if (mask & IN_ACCESS)        return("IN_ACCESS ");
     if (mask & IN_ATTRIB)        return("IN_ATTRIB ");
     if (mask & IN_CLOSE_NOWRITE) return("IN_CLOSE_NOWRITE ");
     if (mask & IN_CLOSE_WRITE)   return("IN_CLOSE_WRITE ");
     if (mask & IN_CREATE)        return("IN_CREATE ");
     if (mask & IN_DELETE)        return("IN_DELETE ");
     if (mask & IN_DELETE_SELF)   return("IN_DELETE_SELF ");
     if (mask & IN_IGNORED)       return("IN_IGNORED ");
     if (mask & IN_ISDIR)         return("IN_ISDIR ");
     if (mask & IN_MODIFY)        return("IN_MODIFY ");
     if (mask & IN_MOVE_SELF)     return("IN_MOVE_SELF ");
     if (mask & IN_MOVED_FROM)    return("IN_MOVED_FROM ");
     if (mask & IN_MOVED_TO)      return("IN_MOVED_TO ");
     if (mask & IN_OPEN)          return("IN_OPEN ");
     if (mask & IN_Q_OVERFLOW)    return("IN_Q_OVERFLOW ");
     if (mask & IN_UNMOUNT)       return("IN_UNMOUNT ");
     return ("None");
 }

int  set_inotify_watch_file ( const char * file , int & fd , int & wd )
{
    int rc = PASS ;

    /* Close if already set */
    set_inotify_close ( fd , wd );

    fd = inotify_init();
    if ( fd < 0 )
    {
        elog ("iNotify init error (%d:%m)\n", errno );
        rc = FAIL;
    }
    else
    {
        wd = inotify_add_watch ( fd, file, IN_MODIFY | IN_CREATE | IN_DELETE );
        if ( wd < 0 )
        {
            elog ("failed adding watch on %s (%d:%m)\n", file, errno );
            rc = FAIL ;
        }
        else
        { 
            ilog ("watching %s\n", file );
        }
    }
    return (rc);
}


int  set_inotify_watch ( const char * dir , int & fd , int & wd )
{
    int rc = PASS ;
    fd = inotify_init();
    if ( fd < 0 )
    {
        elog ("inotify init error (%d:%m)\n", errno);
        rc = FAIL;
    }
    else
    {
        ilog ("watching %s\n", dir );
        wd = inotify_add_watch ( fd, dir , IN_MODIFY | 
                                           IN_CREATE | 
                                           IN_DELETE | 
                                           IN_MOVE );
        if ( wd < 0 )
        {
            elog ("failed adding watch on %s (%d:%m)\n", dir, errno );
            rc = FAIL ;
        }
    }
    return (rc);
}

int  set_inotify_watch_events ( const char * dir , int & fd , int & wd, int events )
{
    int rc = PASS ;
    fd = inotify_init();
    if ( fd < 0 )
    {
        elog ("inotify init error (%d:%m)\n", errno );
        rc = FAIL;
    }
    else
    {
        ilog ("watching %s\n", dir );
        wd = inotify_add_watch ( fd, dir , events );
        if ( wd < 0 )
        {
            elog ("failed adding watch on %s (%d:%m)\n", dir, errno);
            rc = FAIL ;
        }
    }
    return (rc);
}

bool valid_file ( char * name )
{
    string temp = name ;
    std::size_t  found ; 
    
    found = temp.find(".swx",0) ;
    if ( found != std::string::npos )
    {
        dlog1 ("%s file is not valid\n", temp.c_str() );
        return (false);
    }

    found = temp.find(".swp",0) ;
    if ( found != std::string::npos )
    {
        dlog1 ("%s file is not valid\n", temp.c_str() );
        return (false);
    }
    
    dlog ("%s file is valid\n", temp.c_str() );
    return (true);
}

int get_inotify_events ( int fd, int event_mask )
{
    int l = 0 ;
    int i = 0 ;
    char buf [EVENT_BUF_LEN] ;
    int status = 0 ;
    memset (buf, 0 , EVENT_BUF_LEN );
    if ( ( l = read (fd, buf, EVENT_BUF_LEN ) > 0 ))
    {
        /*   Read returns the list of change events. 
         *   Deal with all the change events and then reload.
         */
        while ( i < l )
        {
            struct inotify_event *event_ptr = (struct inotify_event *)&buf[i];
            dlog ("iNotify Event Mask:%8x   Requested Mask:%8x\n", event_ptr->mask, event_mask );
            if ( event_ptr->mask & event_mask )
            {
                status |= (event_ptr->mask & event_mask) ;
            }
            if ( event_ptr->mask & IN_IGNORED )
            {
                wlog ("Watch file is now being ignored (%x) !!!\n", status );
                status |= IN_IGNORED ;
            }

            i += EVENT_SIZE + event_ptr->len;
        }
    }
    return(status);
}


bool get_inotify_events ( int fd )
{
    int l = 0 ;
    int i = 0 ;
    char buf [EVENT_BUF_LEN] ;
    bool status = false ;
    memset (buf, 0 , EVENT_BUF_LEN );
    if ( ( l = read (fd, buf, EVENT_BUF_LEN ) > 0 ))
    {
        /*   Read returns the list of change events. 
         *   Deal with all the change events and then reload.
         */
        while ( i < l )
        {
            struct inotify_event *event_ptr = (struct inotify_event *)&buf[i];
            dlog ("iNotify Event Mask:%08x\n", event_ptr->mask);
            if ( event_ptr->len )
            {
                if (( event_ptr->mask & IN_CREATE ) ||
                    ( event_ptr->mask & IN_MOVED_TO ))
                {
                    if ( valid_file ( event_ptr->name ) == true )
                    {
                        if ( !(event_ptr->mask & IN_ISDIR) ) 
                        {
                            dlog( "New file %s created or moved into\n", event_ptr->name );
                            status = true ;
                        }
                    }
                }
                else if (( event_ptr->mask & IN_DELETE ) || 
                         ( event_ptr->mask & IN_MOVED_FROM ))
                {
                    if ( valid_file ( event_ptr->name ) == true )
                    {
                        if ( !(event_ptr->mask & IN_ISDIR) )
                        {
                            dlog( "%s deleted or removed.\n", event_ptr->name );
                            status = true ;
                        }
                    }
                }
                else if ( event_ptr->mask & IN_MODIFY )
                {
                    if ( valid_file ( event_ptr->name ) == true )
                    {
                        if ( !(event_ptr->mask & IN_ISDIR) )
                        {
                            dlog( "%s modified.\n", event_ptr->name );
                            status = true ;
                        }
                    }
                }
                else
                {
                    dlog ("Unhandled iNotify Event Mask:%08x\n", event_ptr->mask);
                }
            }
            i += EVENT_SIZE + event_ptr->len;
        }
    }
    return(status);
}

static string null_str = "" ;

/* Returns the number of events found */
int get_inotify_events ( int fd, inotify_event_queue_type & event_queue )
{
    int l = 0 ;
    int i = 0 ;
    char buf [EVENT_BUF_LEN] ;
    memset (buf, 0 , EVENT_BUF_LEN );

    event_queue.num = 0 ; /* default to no events */

    if ( ( l = read (fd, buf, EVENT_BUF_LEN ) > 0 ))
    {
        /*   Read returns the list of change events. 
         *   Deal with all the change events and then reload.
         */
        while ( i < l )
        {
            struct inotify_event *event_ptr = (struct inotify_event *)&buf[i];
            dlog ("iNotify Event Mask:%08x\n", event_ptr->mask);
            if ( event_ptr->len )
            {
                if (( event_ptr->mask & IN_CREATE ) ||
                    ( event_ptr->mask & IN_MOVED_TO ))
                {
                    if ( valid_file ( event_ptr->name ) == true )
                    {
                        if ( !(event_ptr->mask & IN_ISDIR) ) 
                        {
                            dlog( "%s created\n", event_ptr->name );
                            event_queue.item[event_queue.num].event = IN_CREATE ;
                            snprintf ( &event_queue.item[event_queue.num].name[0], EVENT_BUF_LEN, "%s", event_ptr->name );
                            event_queue.num++ ;
                        }
                    }
                }
                else if (( event_ptr->mask & IN_DELETE ) || 
                         ( event_ptr->mask & IN_MOVED_FROM ))
                {
                    if ( valid_file ( event_ptr->name ) == true )
                    {
                        if ( !(event_ptr->mask & IN_ISDIR) )
                        {
                            dlog( "%s deleted\n", event_ptr->name );
                            event_queue.item[event_queue.num].event = IN_DELETE ;
                            snprintf ( &event_queue.item[event_queue.num].name[0], EVENT_BUF_LEN, "%s", event_ptr->name );
                            event_queue.num++ ;
                        }
                    }
                }
                else if ( event_ptr->mask & IN_MODIFY )
                {
                    if ( valid_file ( event_ptr->name ) == true )
                    {
                        if ( !(event_ptr->mask & IN_ISDIR) )
                        {
                            dlog( "%s modified\n", event_ptr->name );
                            event_queue.item[event_queue.num].event = IN_MODIFY ;
                            snprintf ( &event_queue.item[event_queue.num].name[0], EVENT_BUF_LEN, "%s", event_ptr->name );
                            event_queue.num++ ;
                        }
                    }
                }
                else
                {
                    dlog ("Unhandled iNotify Event Mask:%08x\n", event_ptr->mask);
                }
            }
            i += EVENT_SIZE + event_ptr->len;
        }
    }
    return (event_queue.num) ;
}

void set_inotify_close ( int & fd, int & wd )
{
    /* cleanup */
    if ( fd )
    {
        inotify_rm_watch( fd, wd );
        close (fd);
        fd = 0 ;
        wd = 0 ;
    }
}

