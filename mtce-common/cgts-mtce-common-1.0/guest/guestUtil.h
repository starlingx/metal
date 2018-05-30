#ifndef __INCLUDE_GUESTUTIL_H__
#define __INCLUDE_GUESTUTIL_H__

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include "guestBase.h"          /* for ... instInfo                    */

void   guestUtil_inst_init       (  instInfo * instance_ptr );
void   guestUtil_print_instances ( ctrl_type * ctrl_ptr );
void   guestUtil_print_instance  ( instInfo * instInfo_ptr );

/* called in guestAgent  */
int    guestUtil_get_inst_info   ( string hostname, instInfo * instInfo_ptr, char * buf_ptr );

/* called in guestServer */
string guestUtil_set_inst_info   ( string hostname, instInfo * instInfo_ptr );



string log_prefix              ( instInfo * instInfo_ptr );
string time_in_secs_to_str     ( time_t secs );

const char* hb_get_corrective_action_name( uint32_t a) ; // heartbeat_corrective_action_t a);
const char* hb_get_state_name       (hb_state_t s);
// Convert integer to string
string int_to_string(int number);


#endif /* __INCLUDE_GUESTUTIL_H__ */
