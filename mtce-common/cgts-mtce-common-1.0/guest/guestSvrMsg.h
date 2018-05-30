#ifndef __INCLUDE_GUESTSVRMSG_H__
#define __INCLUDE_GUESTSVRMSG_H__

/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Guest Services "Messaging" Header
  */

#include "guestBase.h"
#include "guestInstClass.h"  /* for ... */

/* Send a command and buffer to the guestAgent */
int send_to_guestAgent ( unsigned int cmd, 
                         const char * buf_ptr );

int recv_from_guestAgent ( unsigned int cmd, char * buf_ptr );

string guestSvrMsg_hdr_init (string channel, string  msg_type);

#endif /* __INCLUDE_GUESTSVRMSG_H__ */
