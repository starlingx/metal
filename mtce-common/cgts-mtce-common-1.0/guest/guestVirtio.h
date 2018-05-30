#ifndef __GUESTVIRTIO_H__
#define __GUESTVIRTIO_H__

/*
* Copyright (c) 2013-2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/

#include <string.h>
#include <stdbool.h>

using namespace std;

#include "guestBase.h"

bool   virtio_check_filename  ( char * fn );
int    virtio_channel_connect ( string channel );
int    virtio_channel_connect ( instInfo * inst_ptr );
int    virtio_channel_add     ( char * chan_ptr );
string virtio_instance_name   ( char * fn );

#endif /* __GUESTVIRTIO_H__ */
