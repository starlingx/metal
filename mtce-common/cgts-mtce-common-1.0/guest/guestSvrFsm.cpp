/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/***************************************************************************
 *
 * @file
 * Wind River CGTS Platform "Guest Services - Finite State Machine"
 *
 *
 * This FSM handles the following actions
 *
 * FSM_ACTION__CONNECT
 *
 */

#include <iostream>
#include <string.h>

using namespace std;

#include "nodeBase.h"
#include "nodeTimers.h"
#include "guestBase.h"
#include "guestInstClass.h"
#include "guestSvrUtil.h"

void guestInstClass::fsm_run ( void )
{
    int rc = PASS ;
    struct inst * inst_ptr = static_cast<struct inst *>(NULL) ;

    if (( instances > 0 ) )
    {
        /* get new messages */
        readInst();

        for ( inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
        {
            if ( inst_ptr->message_list.size() )
            {
                guestInstClass::message_handler ( inst_ptr );
            }

            if ( inst_ptr->action == FSM_ACTION__NONE )
            {
                guestInstClass::monitor_handler ( inst_ptr );
            }

            else if ( inst_ptr->action == FSM_ACTION__CONNECT )
            {
                rc = guestInstClass::connect_handler ( inst_ptr );
                if ( rc == RETRY )
                    return ;
            }
            else
            {
                slog ("unknown action (%d) for instance %s\n", 
                       inst_ptr->action, inst_ptr->instance.uuid.c_str());
            }

#ifdef WANT_LOSS_FIT
            if ( inst_ptr->heartbeat_count > 10 )
            {
                mtcTimer_stop ( inst_ptr->monitor_timer );
                mtcWait_secs (1);
                start_monitor_timer ( inst_ptr );
                inst_ptr->heartbeat_count = 0 ;
            }
#endif

            /* exit if this happens to be the last one in the list */
            if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                break ;
        }
    }
    else if ( inst_head != NULL )
    {
        slog ("head pointer is not NULL while there are no instances (%p)\n", inst_head );
    }

    if ( search_timer.ring == true )
    {
        guestUtil_channel_search ();
        mtcTimer_start ( search_timer, guestTimer_handler, SEARCH_AUDIT_TIME );
    }
    
    /* Make this part of the connect FSM */
    manage_comm_loss ( );
}
