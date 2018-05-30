#ifndef __INCLUDE_GUESTSVRUTIL_H__
#define __INCLUDE_GUESTSVRUTIL_H__

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include "guestBase.h"          /* for ... instInfo                    */

int  guestUtil_close_channel   ( instInfo * instInfo_ptr );
void guestUtil_load_channels   ( void );
int  guestUtil_inotify_events  ( int fd );
void guestUtil_channel_search ( void ) ;

#endif /* __INCLUDE_GUESTSVRUTIL_H__ */
