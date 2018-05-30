/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Guest Services "Instances Base Class"
  */


#include <sys/types.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

using namespace std;

#include "nodeBase.h"       /* for ... common definitions                */
#include "nodeEvent.h"      /* for ... set_inotify_watch_file            */
#include "nodeTimers.h"     /* for ... mtcTimer                          */
#include "guestBase.h"      /* for ... instInfo                          */
#include "guestUtil.h"      /* for ... guestUtil_inst_init               */
#include "guestInstClass.h" /* for ... get_inst                          */
#include "guestSvrUtil.h"   /* for ... hb_get_state_name                 */

/**< constructor */
guestInstClass::guestInstClass()
{ 
    inst_head     = NULL ;
    inst_tail     = NULL ;

    memory_allocs = 0 ;
    memory_used   = 0 ;
    instances     = 0 ;

    for ( int i = 0 ; i < MAX_INSTANCES ; i++ )
    {
        inst_ptrs[i] = NULL ;
    }

    fsm_exit = false ;
    reporting = true ;
    return ; 
}

/**< destructor */
guestInstClass::~guestInstClass()
{ 
    inst * inst_ptr = inst_head ;
    inst * temp_ptr = inst_ptr  ;
    while ( inst_ptr != NULL )
    {
        temp_ptr = inst_ptr ;
        inst_ptr = inst_ptr->next ;
        delInst (temp_ptr);
    }
    if ( memory_used != 0 )
    {
       elog ( "Apparent Memory Leak - Allocs:%d and Bytes:%d\n", 
           memory_allocs, memory_used );
    }
    else
    {
        dlog ( "No Memory Leaks\n\n");
    }
    return ;
} 

void guestInstClass::guest_fsm_run ( void ) 
{
    fsm_run ();
}

/*
 * Allocate new instance and tack it on the end of the instance list
 */  
struct guestInstClass::inst* guestInstClass::addInst ( string uuid )
{
    if ( uuid.length() != UUID_LEN )
    {
        elog ("invalid instance uuid ; cannot add %s\n", uuid.c_str());
        return static_cast<struct inst *>(NULL);
    }

    /* verify instance is not already provisioned */
    struct inst * inst_ptr = guestInstClass::getInst ( uuid );
    if ( inst_ptr )
    {
        if ( guestInstClass::remInst ( uuid ) )
        {
            /* Should never get here but if we do then */
            /* something is seriously wrong */
            elog ("%s unable to remove instance during reprovision\n", 
                      log_prefix(&inst_ptr->instance).c_str()); 
            return static_cast<struct inst *>(NULL);
        }
    }
    
    /* allocate memory for new instance */
    inst_ptr = guestInstClass::newInst ();
    if( inst_ptr == NULL )
    {
        elog ( "failed to allocate memory for new instance\n" );
		return static_cast<struct inst *>(NULL);
    }
  
    guestUtil_inst_init ( &inst_ptr->instance );

    /* Init the new instance */
    inst_ptr->instance.uuid = uuid ;
    inst_ptr->query_flag    = false   ;
    inst_ptr->instance.connect_wait_in_secs = DEFAULT_CONNECT_WAIT ;

    /* Init instance's connect and monitor timers */
    /* Assign the timer the instance's name */
    mtcTimer_init ( inst_ptr->reconnect_timer, uuid );
    mtcTimer_init ( inst_ptr->connect_timer, uuid );
    mtcTimer_init ( inst_ptr->monitor_timer, uuid );
    mtcTimer_init ( inst_ptr->init_timer, uuid );
    mtcTimer_init ( inst_ptr->vote_timer, uuid );
            
    inst_ptr->action = FSM_ACTION__NONE ;

    inst_ptr->connectStage = INST_CONNECT__START   ;
    inst_ptr->monitorStage = INST_MONITOR__STEADY  ;
    inst_ptr->messageStage = INST_MESSAGE__RECEIVE ;

    /* If the instance list is empty add it to the head */
    if( inst_head == NULL )
    {
        inst_head = inst_ptr  ;  
        inst_tail = inst_ptr  ;
        inst_ptr->prev = NULL ;
        inst_ptr->next = NULL ;
    }
    else
    {
        /* link the new_instance to the tail of the inst_list
         * then mark the next field as the end of the inst_list
         * adjust tail to point to the last instance
         */
        inst_tail->next = inst_ptr  ;
        inst_ptr->prev  = inst_tail ;
        inst_ptr->next  = NULL      ;
        inst_tail       = inst_ptr  ; 
    }

    instances++ ;
    ilog ("%s added as instance %d\n", log_prefix(&inst_ptr->instance).c_str(), instances);
    return inst_ptr ;
}

/* Remove an instance from the linked list of instances - may require splice action */
int guestInstClass::remInst( string uuid )
{
    if ( uuid.empty() )
        return -ENODEV ;

    if ( inst_head == NULL )
        return -ENXIO ;
    
    struct inst * inst_ptr = getInst ( uuid );

    if ( inst_ptr == NULL )
        return -EFAULT ;

    stop_instance_timers ( inst_ptr );

    /* Close the channel if it is open */
    guestUtil_close_channel ( &inst_ptr->instance );

    /* If the instance is the head instance */
    if ( inst_ptr == inst_head )
    {
        /* only one instance in the list case */
        if ( inst_head == inst_tail )
        {
            dlog2 ("Single Inst -> Head Case\n");
            inst_head = NULL ;
            inst_tail = NULL ;
        }
        else
        {
            dlog2 ("Multiple Insts -> Head Case\n");
            inst_head = inst_head->next ;
            inst_head->prev = NULL ; 
        }
    }
    /* if not head but tail then there must be more than one
     * instance in the list so go ahead and chop the tail.
     */
    else if ( inst_ptr == inst_tail )
    {
        dlog2 ("Multiple Inst -> Tail Case\n");
        inst_tail = inst_tail->prev ;
        inst_tail->next = NULL ;
    }
    else
    {
        dlog2 ("Multiple Inst -> Full Splice Out\n");
        inst_ptr->prev->next = inst_ptr->next ;
        inst_ptr->next->prev = inst_ptr->prev ;
    }
    delInst ( inst_ptr );
    instances-- ;

    if ( instances == 0 )
        ilog ("no instances to monitor\n");

    return (PASS) ;
}

/* Perform a linked list search for the instance matching the instance name */
struct guestInstClass::inst* guestInstClass::getInst ( string chan_or_uuid )
{
    struct inst * inst_ptr = static_cast<struct inst *>(NULL) ;

    /* check for empty list condition */
    if ( inst_head )
    {
        for ( inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
        {
            if ( !inst_ptr->instance.uuid.compare (chan_or_uuid) )
            {
                return inst_ptr ;
            }
            if ( !inst_ptr->instance.chan.compare (chan_or_uuid) )
            {
                return inst_ptr ;
            }

            if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                break ;
        }
    }
    return static_cast<struct inst *>(NULL);
}

/*
 * Allocates memory for a new instance and stores its address in inst_ptrs 
 *
 * @param void
 * @return pointer to the newly allocted instance memory
 */ 
struct guestInstClass::inst * guestInstClass::newInst ( void )
{
   struct guestInstClass::inst * temp_inst_ptr = NULL ;
      
   if ( memory_allocs == 0 )
   {
       memset ( inst_ptrs, 0 , sizeof(struct inst *)*MAX_INSTANCES);
   }

   // find an empty spot
   for ( int i = 0 ; i < MAX_INSTANCES ; i++ )
   {
      if ( inst_ptrs[i] == NULL )
      {
          inst_ptrs[i] = temp_inst_ptr = new inst ;
          memory_allocs++ ;
          memory_used += sizeof (struct guestInstClass::inst);

          return temp_inst_ptr ;
      }
   }
   elog ( "failed to store new instance pointer address\n" );
   return temp_inst_ptr ;
}

/* Frees the memory of a pre-allocated instance and removes
 * it from the inst_ptrs list.
 *
 * @param instance * pointer to the instance memory address to be freed
 * @return int return code { PASS or -EINVAL }
 */
int guestInstClass::delInst ( struct guestInstClass::inst * inst_ptr )
{
    if ( memory_allocs > 0 )
    {
        for ( int i = 0 ; i < MAX_INSTANCES ; i++ )
        {
            if ( inst_ptrs[i] == inst_ptr )
            {
                delete inst_ptr ;
                inst_ptrs[i] = NULL ;
                memory_allocs-- ;
                memory_used -= sizeof (struct guestInstClass::inst);
                return PASS ;
            }
        }
        elog ( "unable to validate memory address being freed\n" );
    }
    else
       elog ( "free memory called when there is no memory to free\n" );
    
    return -EINVAL ;
}

/***************************************************************************************
 *                      P U B L I C     I N T E R F A C E S
 **************************************************************************************/

/* Add an instance based on its uuid.
 * If the instance already exists then  update its info */
int guestInstClass::add_inst ( string uuid , instInfo & instance )
{
    int rc = FAIL ;

    struct guestInstClass::inst * inst_ptr = getInst(uuid);
    if ( inst_ptr ) 
    {
        ilog ("********************************************************\n");
        ilog ("%s Already provisioned - TODO: Create copy constructor  \n", uuid.c_str());
        ilog ("********************************************************\n");

        /* Send back a retry in case the add needs to be converted to a modify */
        rc = PASS ;
    }
    /* Otherwise add it as a new instance */
    else
    {
        if ( uuid.length() != UUID_LEN )
        {
            elog ("invalid uuid %s\n", uuid.c_str());
            return (FAIL_INVALID_UUID);
        }

        inst_ptr = guestInstClass::addInst(uuid);
        if ( inst_ptr )
        {
            rc = PASS ;
        }
        else
        {
            elog ("failed to add instance '%s'\n", uuid.c_str());
            rc = FAIL_NULL_POINTER ;
        }
    }

    if ( rc == PASS )
    {
        inst_ptr->heartbeat_count = 0 ;

        inst_ptr->mismatch_count = 0 ;

        /* TODO: This needs to be a complete copy - Need copy constructor */
        inst_ptr->instance.heartbeat.failures    = 0    ;
        inst_ptr->instance.heartbeat.failed      = false                          ;
        inst_ptr->instance.heartbeat.reporting   = instance.heartbeat.reporting   ;
        inst_ptr->instance.heartbeat.provisioned = instance.heartbeat.provisioned ;
        inst_ptr->instance.heartbeat.state       = instance.heartbeat.state       ;
        inst_ptr->instance.hbState               = hbs_server_waiting_init        ;
        inst_ptr->instance.vnState               = hbs_server_waiting_init        ;
        
        inst_ptr->instance.name_log_prefix = "" ;
        inst_ptr->instance.uuid_log_prefix = "" ;

        inst_ptr->instance.name                  = instance.name        ;
        inst_ptr->instance.inst                  = instance.inst        ;
        inst_ptr->instance.connected             = instance.connected ;
        inst_ptr->instance.heartbeating          = instance.heartbeating ;
        inst_ptr->instance.chan_fd               = instance.chan_fd   ;
        inst_ptr->instance.chan_ok               = instance.chan_ok   ;
        
        inst_ptr->instance.corrective_action     = instance.corrective_action     ;
        inst_ptr->instance.heartbeat_interval_ms = instance.heartbeat_interval_ms ;

        inst_ptr->instance.vote_secs               = instance.vote_secs ;
        inst_ptr->instance.shutdown_notice_secs    = instance.shutdown_notice_secs ;
        inst_ptr->instance.suspend_notice_secs     = instance.suspend_notice_secs ;
        inst_ptr->instance.resume_notice_secs      = instance.resume_notice_secs ;
        inst_ptr->instance.restart_secs            = instance.restart_secs ;

        /* Update the channel */
        if ( instance.chan.length() > UUID_LEN )
            inst_ptr->instance.chan = instance.chan ;
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : del_inst
 *
 * Purpose    : Delete an instance from the linked list
 * 
 *****************************************************************************/
int guestInstClass::del_inst ( string uuid )
{
    int rc = FAIL ;
    if ( ! uuid.empty() )
    {
        /* free memory */
        rc = remInst ( uuid );
    }
    return ( rc );
}

/*****************************************************************************
 *
 * Name       : qry_inst
 *
 * Purpose    : Send instance info to the guestAgent
 * 
 *****************************************************************************/
int guestInstClass::qry_inst (  )
{
    return ( guestAgent_qry_handler ());
}

void guestInstClass::stop_instance_timers ( struct guestInstClass::inst * inst_ptr )
{
    /* Free the mtc timer if in use */
    if ( inst_ptr->reconnect_timer.tid )
    {
        mtcTimer_stop ( inst_ptr->reconnect_timer );
        inst_ptr->reconnect_timer.ring = false ;
        inst_ptr->reconnect_timer.tid  = NULL  ;
    }
    /* Free the connect timer if in use */
    if ( inst_ptr->connect_timer.tid )
    {
        mtcTimer_stop ( inst_ptr->connect_timer );
        inst_ptr->connect_timer.ring = false ;
        inst_ptr->connect_timer.tid  = NULL  ;
    }
    /* Free the monitor timer if in use */
    if ( inst_ptr->monitor_timer.tid )
    {
        mtcTimer_stop ( inst_ptr->monitor_timer );
        inst_ptr->monitor_timer.ring = false ;
        inst_ptr->monitor_timer.tid  = NULL  ;
    }
    /* Free the init timer if in use */
    if ( inst_ptr->init_timer.tid )
    {
        mtcTimer_stop ( inst_ptr->init_timer );
        inst_ptr->init_timer.ring = false ;
        inst_ptr->init_timer.tid  = NULL  ;
    }
    /* Free the vote timer if in use */
    if ( inst_ptr->vote_timer.tid )
    {
        mtcTimer_stop ( inst_ptr->vote_timer );
        inst_ptr->vote_timer.ring = false ;
        inst_ptr->vote_timer.tid  = NULL  ;
    }
}


void guestInstClass::free_instance_resources ( void )
{
    /* check for empty list condition */
    if ( inst_head )
    {
        for ( struct inst * inst_ptr = inst_head ;  ; inst_ptr = inst_ptr->next )
        {
            if ( inst_ptr->instance.chan_fd )
            {
                ilog ("%s closing fd %d for uuid %s\n", 
                          log_prefix(&inst_ptr->instance).c_str(), 
                                      inst_ptr->instance.chan_fd, 
                                      inst_ptr->instance.uuid.c_str());

                close ( inst_ptr->instance.chan_fd );
            }
            stop_instance_timers ( inst_ptr );

            if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                break ;
        }
    }
}


/****************************************************************************/
/** FSM Control Utilities                                                    */
/****************************************************************************/

void guestInstClass::reconnect_start ( const char * uuid_ptr )
{
    string uuid = uuid_ptr ;
    if ( uuid.length() != UUID_LEN )
    {
        elog ("invalid uuid %s (uuid:%ld)\n", uuid.c_str(), uuid.length());
        return ;
    }

    struct guestInstClass::inst * inst_ptr = guestInstClass::getInst(uuid);
    if ( inst_ptr ) 
    {
        guestUtil_close_channel ( &inst_ptr->instance );
    }
    else
    {
        inst_ptr = guestInstClass::addInst(uuid);
    }

    if ( inst_ptr ) 
    {
        instInfo * instInfo_ptr = &inst_ptr->instance ;
        if ( instInfo_ptr->fd_namespace.size() )
        {
            /* Setup inotify to watch for new instance serial IO channel creations */
            if ( set_inotify_watch_file ( instInfo_ptr->fd_namespace.data(), 
                                          instInfo_ptr->inotify_file_fd, 
                                          instInfo_ptr->inotify_file_wd))
            {
                elog ("%s failed to setup 'inotify' on %s\n", 
                          log_prefix(instInfo_ptr).c_str(),
                          instInfo_ptr->fd_namespace.c_str());
            }
        }
        ilog ("%s reconnecting ... %s\n", log_prefix(instInfo_ptr).c_str(), 
                  instInfo_ptr->connected ? " CONNECTED" : "" );

        if ( inst_ptr->connect_timer.tid )
            mtcTimer_stop ( inst_ptr->connect_timer );

        inst_ptr->action = FSM_ACTION__CONNECT ;
        inst_ptr->connectStage = INST_CONNECT__START ;

        // mtcTimer_start ( inst_ptr->connect_timer, guestTimer_handler, inst_ptr->instance.connect_wait_in_secs );

        //ilog ("%s connect attempt in %d seconds\n", 
        //          log_prefix(&inst_ptr->instance).c_str(), inst_ptr->instance.connect_wait_in_secs);
        instInfo_ptr->connecting = true ;
    }
    else
    {
        elog ("%s failed to find or add instance\n", uuid.c_str() );
    }
}



/****************************************************************************/
/** Inst Class Setter / Getters */
/****************************************************************************/

/*****************************************************************************
 *
 * Name       : get_inst
 *
 * Purpose    : Return a pointer to the instance for a specified uuid
 * 
 *****************************************************************************/
instInfo * guestInstClass::get_inst ( string uuid )
{
    struct guestInstClass::inst * inst_ptr = guestInstClass::getInst(uuid);
    if ( inst_ptr ) 
    {
        return (&inst_ptr->instance );
    }
    return static_cast<instInfo *>(NULL);
}

/*****************************************************************************
 *
 * Name       : getInst_timer
 *
 * Purpose    : Return a pointer to the instance that contains the timer for
 *              the specified timer ID.
 * 
 *****************************************************************************/
struct guestInstClass::inst * guestInstClass::getInst_timer ( timer_t tid, int timer_id )
{
   if ( tid != NULL )
   {
       if ( inst_head )
       {
           struct inst * inst_ptr ;
           for ( inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
           {
               if (( timer_id == INST_TIMER_MONITOR ) && (inst_ptr->monitor_timer.tid == tid ))
               {
                   return inst_ptr ;
               }
               else if (( timer_id == INST_TIMER_CONNECT ) && (inst_ptr->connect_timer.tid == tid ))
               {
                   return inst_ptr ;
               }
               else if (( timer_id == INST_TIMER_VOTE ) && ( inst_ptr->vote_timer.tid == tid ))
               {
                   return inst_ptr ;
               }
               else if (( timer_id == INST_TIMER_INIT ) && ( inst_ptr->init_timer.tid == tid ))
               {
                   return inst_ptr ;
               }
               else if (( timer_id == INST_TIMER_RECONNECT ) && ( inst_ptr->reconnect_timer.tid == tid ))
               {
                   return inst_ptr ;
               }

               if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                   break ;
            }
        }
    } 
    return static_cast<struct inst *>(NULL);
}

/* Get an instance's heartbeat fault reporting state */
bool guestInstClass::get_reporting_state ( string uuid )
{
    guestInstClass::inst * inst_ptr = guestInstClass::getInst ( uuid );
    if ( inst_ptr )
    {
        return ( inst_ptr->instance.heartbeat.reporting );
    }
    else
    {
        wlog ("uuid not found '%s'\n", uuid.c_str());
    }
    return ( false );
}

/* Set an instances heartbeat fault reporting state */ 
int guestInstClass::set_reporting_state( string uuid, bool reporting )
{
    guestInstClass::inst * inst_ptr = guestInstClass::getInst ( uuid );
    if ( inst_ptr )
    {
        inst_ptr->instance.heartbeat.reporting = reporting ;
    }
    else
    {
        wlog ("uuid not found '%s'\n", uuid.c_str());
        return (FAIL_NOT_FOUND) ;
    }
    return (PASS);
}


/*****************************************************************************
 *
 * Name   : print_all_instances
 *
 * Purpose: Print a summary of the instances that are currently provisioned
 *
 *****************************************************************************/
void guestInstClass::print_all_instances ( void )
{
    bool found = false;
    int i = 0 ;
    if ( inst_head )
    {
        struct inst * inst_ptr ;
        for ( inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
        {
            ilog ("%2d %s Heartbeat: Notify:%c Failures:%d\n", i,
                       log_prefix(&inst_ptr->instance).c_str(),
                       inst_ptr->instance.heartbeat.reporting   ? 'Y':'n',
                       inst_ptr->instance.heartbeat.failures);
            found = true ;
            i++ ;
            if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
                break ;
        }
    }

    if ( found == false )
    {
        ilog ("no instances provisioned\n");
    }
}

/*****************************************************************************
 *
 * Name   : print_instances (private)
 *
 *****************************************************************************/
void guestInstClass::print_instances ( void )
{
    print_all_instances();
}

/*****************************************************************************
 *                              Memory Dump Stuff                            *
 *****************************************************************************/
void guestInstClass::print_node_info ( void )
{
    fflush (stdout);
    fflush (stderr);
}

void guestInstClass::mem_log_info ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "Instances:%d  Allocs:%d  Memory:%d\n", instances, memory_allocs, memory_used );
    mem_log (str);
}

void mem_log_delimit_host ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "-------------------------------------------------------------\n");
    mem_log (str);
}

void guestInstClass::mem_log_inst_info ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    
    struct inst * inst_ptr = static_cast<struct inst *>(NULL) ;

    for ( inst_ptr = inst_head ; inst_ptr != NULL ; inst_ptr = inst_ptr->next )
    {
        snprintf (&str[0], MAX_MEM_LOG_DATA, "Name  : %s  %s (%s)\n",
                           inst_ptr->instance.name.data(), 
                           inst_ptr->instance.uuid.data(),
                           inst_ptr->instance.inst.data());
        mem_log (str);

        snprintf (&str[0], MAX_MEM_LOG_DATA, "Action: %8d Connect:%2d  Message:%2d  Delay:%d secs\n",
                           inst_ptr->action, 
                           inst_ptr->connectStage,
                           inst_ptr->messageStage,
                           inst_ptr->instance.connect_wait_in_secs);
        mem_log (str);

        snprintf (&str[0], MAX_MEM_LOG_DATA, "State : Reporting: %c   Failures: %d   Failed: %c\n",
                           inst_ptr->instance.heartbeat.reporting ? 'Y' : 'n', 
                           inst_ptr->instance.heartbeat.failures,
                           inst_ptr->instance.heartbeat.failed ? 'Y' : 'n' ); 
        mem_log (str);

        snprintf (&str[0], MAX_MEM_LOG_DATA, "Setup : Select   :%2d  Channel OK: %c  hbState:%s vnState:%s\n",
                           inst_ptr->instance.chan_fd, 
                           inst_ptr->instance.chan_ok ? 'Y' : 'n' ,
                           hb_get_state_name(inst_ptr->instance.hbState),
                           hb_get_state_name(inst_ptr->instance.vnState));
        mem_log (str);

        snprintf (&str[0], MAX_MEM_LOG_DATA, "Oper  : Connected: %c   Heartbeating: %c\n",
                           inst_ptr->instance.connected  ? 'Y' : 'n',
                           inst_ptr->instance.heartbeating ? 'Y' : 'n');
        mem_log (str);

        mem_log_delimit_host();

        /* exit if this happens to be the last one in the list */
        if (( inst_ptr->next == NULL ) || ( inst_ptr == inst_tail ))
            break ;
    }
    if ( inst_head == NULL )
    {
        snprintf (&str[0], MAX_MEM_LOG_DATA, "no instances\n");
        mem_log (str);
    }
}

void guestInstClass::memDumpAllState ( void )
{
    mem_log_info ( );
    mem_log_delimit_host ();
    mem_log_inst_info ();
}
