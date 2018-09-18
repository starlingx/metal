/*
* Copyright (c) 2013-2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>   /*                                 */
#include <sys/inotify.h> /* for inotify service             */
#include <list>          /* for the list of conf file names */
#include <unistd.h>

using namespace std;

#define EVENT_SIZE     ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN  ( PATH_MAX * ( EVENT_SIZE + 16 ) )

#define MAX_EVENTS (50)

typedef struct
{
    int  event ;
    char name [EVENT_BUF_LEN] ;
} inotify_event_type ;


typedef struct
{
    int  num ;
    inotify_event_type item[MAX_EVENTS] ;
} inotify_event_queue_type ;

int  set_inotify_watch      ( const char *  dir, int & fd, int & wd );
int  set_inotify_watch      ( const char *  dir, int & fd, int & wd , int events );
int  set_inotify_watch_file ( const char * file, int & fd, int & wd );

bool get_inotify_events( int fd );
int  get_inotify_events( int fd, int   event_mask );
int  get_inotify_events( int fd, inotify_event_queue_type & event_queue );
void set_inotify_close ( int & fd, int & wd );

const char * get_inotify_event_str (int i);
