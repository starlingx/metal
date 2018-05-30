/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Guest Services "Base" Header
  */

#include "nodeBase.h"
#include "nodeTimers.h"
#include "guestClass.h"
#include "nodeUtil.h"


const char       guest_msg_hdr [MSG_HEADER_SIZE] = {"guest msg header:"};
const char * get_guest_msg_hdr (void) { return guest_msg_hdr ; }

/* used as a default return in procedures that return a reference to libEvent */
libEvent nullEvent ;

/**< constructor */
guestHostClass::guestHostClass()
{ 
    guest_head = guest_tail = NULL;
    memory_allocs = 0 ;
    memory_used   = 0 ;
    hosts         = 0 ;

    for ( int i = 0 ; i < MAX_HOSTS ; i++ )
    {
        host_ptrs[i] = NULL ;
    }
    /* Query Host state from the VIM bools */
    audit_run      = false ;

    exit_fsm = false ;

//    httpUtil_event_init ( &nullEvent, "null", "null" , "0.0.0.0", 0 );
    nullEvent.request = SERVICE_NONE ;
    return ; 
}

/**< destructor */
guestHostClass::~guestHostClass()
{ 
    guest_host * ptr = guest_head ;
    guest_host * temp_ptr = ptr ;
    while ( ptr != NULL )
    {
        temp_ptr = ptr ;
        ptr = ptr->next ;
        delHost (temp_ptr);
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

/*
 * Allocate new host and tack it on the end of the host_list
 */  
struct guestHostClass::guest_host* guestHostClass::addHost( string hostname )
{
    /* verify host is not already provisioned */
    struct guest_host * ptr = guestHostClass::getHost ( hostname );
    if ( ptr )
    {
        if ( guestHostClass::remHost ( hostname ) )
        {
            /* Should never get here but if we do then */
            /* something is seriously wrong */
            elog ("Error: Unable to remove host during reprovision\n"); 
            return static_cast<struct guest_host *>(NULL);
        }
    }
    
    /* allocate memory for new host */
    ptr = guestHostClass::newHost ();
    if( ptr == NULL )
    {
        elog ( "Error: Failed to allocate memory for new host\n" );
		return static_cast<struct guest_host *>(NULL);
    }
    
    /* Init the new host */
    ptr->hostname   = hostname ;
    ptr->reporting  = false    ;
    ptr->query_flag = false    ;

    ptr->got_host_state = false;
    ptr->got_instances  = false;

    ptr->stage      =  STAGE__START ;
    ptr->instance_list.clear();

    /* Init host's general mtc timer */
    mtcTimer_init ( ptr->host_audit_timer );

    /* Assign the timer the host's name */
    ptr->host_audit_timer.hostname = hostname ;
    
    /* If the host list is empty add it to the head */
    if( guest_head == NULL )
    {
        guest_head = ptr ;  
        guest_tail = ptr ;
        ptr->prev = NULL ;
        ptr->next = NULL ;
    }
    else
    {
        /* link the new_host to the tail of the host_list
         * then mark the next field as the end of the host_list
         * adjust tail to point to the last host
         */
        guest_tail->next = ptr  ;
        ptr->prev  = guest_tail ;
        ptr->next  = NULL ;
        guest_tail = ptr ; 
    }

    hosts++ ;
    dlog2 ("Added guestHostClass host instance %d\n", hosts);
    return ptr ;
}

/* Remove a hist from the linked list of hosts - may require splice action */
int guestHostClass::remHost( string hostname )
{
    if ( hostname.c_str() == NULL )
        return -ENODEV ;

    if ( guest_head == NULL )
        return -ENXIO ;
    
    struct guest_host * ptr = guestHostClass::getHost ( hostname );

    if ( ptr == NULL )
        return -EFAULT ;

    /* Free the mtc timer if in use */
    if ( ptr->host_audit_timer.tid )
    {
        tlog ("%s Stopping host timer\n", hostname.c_str());
        mtcTimer_stop ( ptr->host_audit_timer );
        ptr->host_audit_timer.ring = false ;
        ptr->host_audit_timer.tid  = NULL  ;
    }

    /* If the host is the head host */
    if ( ptr == guest_head )
    {
        /* only one host in the list case */
        if ( guest_head == guest_tail )
        {
            dlog ("Single Host -> Head Case\n");
            guest_head = NULL ;
            guest_tail = NULL ;
        }
        else
        {
            dlog ("Multiple Hosts -> Head Case\n");
            guest_head = guest_head->next ;
            guest_head->prev = NULL ; 
        }
    }
    /* if not head but tail then there must be more than one
     * host in the list so go ahead and chop the tail.
     */
    else if ( ptr == guest_tail )
    {
        dlog ("Multiple Host -> Tail Case\n");
        guest_tail = guest_tail->prev ;
        guest_tail->next = NULL ;
    }
    else
    {
        dlog ("Multiple Host -> Full Splice Out\n");
        ptr->prev->next = ptr->next ;
        ptr->next->prev = ptr->prev ;
    }
    guestHostClass::delHost ( ptr );
    hosts-- ;
    return (PASS) ;
}


struct guestHostClass::guest_host* guestHostClass::getHost ( string hostname_or_uuid )
{
   for ( struct guest_host * ptr = guest_head ; guest_head ; ptr = ptr->next )
   {
       if ( !hostname_or_uuid.compare ( ptr->hostname ))
       {
           return ptr ;
       }
       else if ( !hostname_or_uuid.compare ( ptr->uuid ))
       {
           return ptr ;
       }

       if (( ptr->next == NULL ) || ( ptr == guest_tail ))
           break ;
    }
    return static_cast<struct guest_host *>(NULL);
}

/*
 * Allocates memory for a new host and stores its the address in host_ptrs 
 *
 * @param void
 * @return pointer to the newly allocted host memory
 */ 
struct guestHostClass::guest_host * guestHostClass::newHost ( void )
{
   struct guestHostClass::guest_host * temp_host_ptr = NULL ;
      
   if ( memory_allocs == 0 )
   {
       memset ( host_ptrs, 0 , sizeof(struct guest_host *)*MAX_HOSTS);
   }

   // find an empty spot
   for ( int i = 0 ; i < MAX_HOSTS ; i++ )
   {
      if ( host_ptrs[i] == NULL )
      {
          host_ptrs[i] = temp_host_ptr = new guest_host ;
          memory_allocs++ ;
          memory_used += sizeof (struct guestHostClass::guest_host);

          return temp_host_ptr ;
      }
   }
   elog ( "Failed to save new host pointer address\n" );
   return temp_host_ptr ;
}

/* Frees the memory of a pre-allocated host and removes
 * it from the host_ptrs list 
 * @param host * pointer to the host memory address to be freed
 * @return int return code { PASS or -EINVAL }
 */
int guestHostClass::delHost ( struct guestHostClass::guest_host * host_ptr )
{
    if ( guestHostClass::memory_allocs > 0 )
    {
        for ( int i = 0 ; i < MAX_NODES ; i++ )
        {
            if ( guestHostClass::host_ptrs[i] == host_ptr )
            {
                delete host_ptr ;
                guestHostClass::host_ptrs[i] = NULL ;
                guestHostClass::memory_allocs-- ;
                guestHostClass::memory_used -= sizeof (struct guestHostClass::guest_host);
                return PASS ;
            }
        }
        elog ( "Error: Unable to validate memory address being freed\n" );
    }
    else
       elog ( "Error: Free memory called when there is no memory to free\n" );
    
    return -EINVAL ;
}


int guestHostClass::mod_host ( string uuid, string address, string hostname, string hosttype )
{
    struct guestHostClass::guest_host * host_ptr = static_cast<struct guest_host *>(NULL);

    if  (hostname.empty())
    {
        wlog ("Refusing to modify host with 'null' or 'invalid' hostname (uuid:%s)\n",
               uuid.c_str());
        return (FAIL_INVALID_HOSTNAME) ;
    }

    host_ptr = guestHostClass::getHost(hostname);
    if ( !host_ptr ) 
    {
        ilog ("%s not already provisioned\n", host_ptr->hostname.c_str());

        /* Send back a retry in case the add needs to be converted to a modify */
        return (FAIL_INVALID_OPERATION);
    }
    host_ptr->uuid     = uuid     ;
    host_ptr->ip       = address  ;
    host_ptr->hosttype = get_host_function_mask (hosttype) ;

    ilog ("%s modify %s %s %s\n", 
              hostname.c_str(),
              host_ptr->uuid.c_str(),
              host_ptr->ip.c_str(),
              hosttype.c_str());

    return (PASS);
}

int guestHostClass::add_host ( string uuid,
                               string address,
                               string hostname,
                               string hosttype)
{
    int rc = FAIL ;
    struct guestHostClass::guest_host * host_ptr = static_cast<struct guest_host *>(NULL);

    host_ptr = guestHostClass::getHost(hostname);
    if ( host_ptr ) 
    {
        ilog ("%s Already provisioned\n", host_ptr->hostname.c_str());

        /* Send back a retry in case the add needs to be converted to a modify */
        return (RETRY);
    }
    /* Otherwise add it as a new host */
    else
    {
        host_ptr = guestHostClass::addHost(hostname);
        if ( host_ptr )
        {
            host_ptr->uuid     = uuid ;
            host_ptr->ip       = address ;
            host_ptr->hosttype = get_host_function_mask(hosttype);

            mtcTimer_init ( host_ptr->host_audit_timer, hostname );

            host_ptr->stage = STAGE__START ;

            /* Add to the end of inventory */
            hostlist.push_back ( host_ptr->hostname );

            rc = PASS ;
            ilog ("%s added\n", hostname.c_str());
        }
        else
        {
            elog ("%s add failed\n", hostname.c_str());
            rc = FAIL_NULL_POINTER ;
        }
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : rem_host
 *
 * Purpose    : Remove this host from daemon all together
 * 
 *****************************************************************************/
int guestHostClass::rem_host ( string hostname )
{
    int rc = FAIL ;
    if ( ! hostname.empty() )
    {
        /* remove the service specific component */
        hostlist.remove ( hostname );
      
        exit_fsm = true ;

        /* free memory */
        rc = guestHostClass::remHost ( hostname );
    }
    return ( rc );
}

/*****************************************************************************
 *
 * Name       : del_host_inst
 *
 * Purpose    : Delete all instances for this host
 * 
 *****************************************************************************/
int guestHostClass::del_host_inst ( string host_uuid )
{
    int rc = FAIL_DEL_UNKNOWN ;
    guestHostClass::guest_host * guest_host_ptr = guestHostClass::getHost( host_uuid );
    if ( guest_host_ptr ) 
    {
        if ( guest_host_ptr->instance_list.size() != 0 )
        {
            for ( guest_host_ptr->instance_list_ptr = guest_host_ptr->instance_list.begin();
                  guest_host_ptr->instance_list_ptr != guest_host_ptr->instance_list.end();
                  guest_host_ptr->instance_list_ptr++ )
            {
                send_cmd_to_guestServer ( guest_host_ptr->hostname, 
                                          MTC_CMD_DEL_INST, 
                                          guest_host_ptr->instance_list_ptr->uuid,
                                          guest_host_ptr->instance_list_ptr->heartbeat.reporting );
            }

            /* If the instance list is empty then clear the query flag */
            if ( guest_host_ptr->instance_list.empty () )
            {
                clr_query_flag ( guest_host_ptr->hostname );
            }
        }
    }
    else
    {
        wlog ("Unknown host uuid: %s\n", host_uuid.c_str());
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : del_inst
 *
 * Purpose    : Add an instance to Delete all instances and then the host
 *
 *****************************************************************************/
int guestHostClass::del_host ( string uuid )
{
    int rc = FAIL_DEL_UNKNOWN ;
    guestHostClass::guest_host * guest_host_ptr = guestHostClass::getHost( uuid );
    if ( guest_host_ptr )
    {
        if ( guest_host_ptr->instance_list.size() != 0 )
        {
            for ( guest_host_ptr->instance_list_ptr = guest_host_ptr->instance_list.begin();
                  guest_host_ptr->instance_list_ptr != guest_host_ptr->instance_list.end();
                  guest_host_ptr->instance_list_ptr++ )
            {
                send_cmd_to_guestServer ( guest_host_ptr->hostname,
                                          MTC_CMD_DEL_INST,
                                          guest_host_ptr->instance_list_ptr->uuid,
                                          guest_host_ptr->instance_list_ptr->heartbeat.reporting );
            }
        }
        /* save the hostname so that the logs below refer to something valid */
        string hostname = guest_host_ptr->hostname ;
        rc = rem_host ( hostname );
        if ( rc == PASS )
        {
            ilog ("%s deleted\n", hostname.c_str());
            print_node_info();
        }
        else
        {
            elog ("%s delete host failed (rc:%d)\n", hostname.c_str(), rc );
        }
    }
    else
    {
        wlog ("Unknown uuid: %s\n", uuid.c_str());
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : add_inst
 *
 * Purpose    : Add an instance to the guestAgent guestHostClass database
 * 
 * Assumptions: This acts as a modify as well. See description below.
 *
 * Description: Search through all guestAgent guestHostClass database Looking
 *              for the specified instance uuid. 
 *
 *              If found against the specified hostname then just ensure the
 *              channel info is correct or updated, the services are disabled
 *              and the counters are reset.
 *
 *              If found against a different host the do the same 
 *              initialization but add a unique log staing this condition.
 *
 *              If not found then just go ahead and add it to the instance
 *              list for the specified host with the same default initialization.
 * 
 *              Send a add command to the guestServer so that it has the
 *              opportunity to try to open the channel to the guest and
 *              start heartbeating.
 *
 *****************************************************************************/

int guestHostClass::add_inst ( string hostname, instInfo & instance )
{
    int  rc    = FAIL_NOT_FOUND  ;

    if ( instance.uuid.empty() )
    {
        elog ("%s refusing to add null instance to host\n", hostname.c_str());
        return (FAIL_INVALID_UUID);
    }

    /**
     * Loop over all hosts looking for this instance. If it exhists for a different host
     * then remove it from that host and add it to the specified host.
     * This is done because the add is also acting as a modify operation 
     **/
    for ( struct guest_host * ptr = guest_head ; guest_head ; ptr = ptr->next )
    {
        if ( ptr->instance_list.size() != 0 )
        {
            for ( ptr->instance_list_ptr = ptr->instance_list.begin();
                  ptr->instance_list_ptr != ptr->instance_list.end();
                  ptr->instance_list_ptr++ )
            {
                if ( !ptr->instance_list_ptr->uuid.compare(instance.uuid))
                {
                    /* Verify that this instance is for the specified host.
                     * If not then delete this instance from this host and
                     * allow it to be added below for the specified host */
                    if ( ptr->hostname.compare(hostname) )
                    {
                        /* not this host so delete it from the list */
                        ilog ("%s %s move to %s\n", ptr->hostname.c_str(),
                                                    ptr->instance_list_ptr->uuid.c_str(), 
                                                    hostname.c_str() );
                        
                        /* remove the instance from this host's guestServer */
                        send_cmd_to_guestServer ( ptr->hostname,
                                                  MTC_CMD_DEL_INST, 
                                                  instance.uuid, 
                                                  instance.heartbeat.reporting );

                        /* remove it from this hosts list */
                        ptr->instance_list.erase(ptr->instance_list_ptr);
                    }
                    else
                    {
                        rc = PASS ;

                        /* TODO: OBSOLETE check
                         * Update the instance if it is different from what was passed in */
                        if ( ptr->instance_list_ptr->uuid.compare(instance.uuid) )
                        {
                            ptr->instance_list_ptr->hostname = hostname  ;
                            ptr->instance_list_ptr->uuid = instance.uuid ;
                            ptr->instance_list_ptr->heartbeat.reporting   = instance.heartbeat.reporting  ;
                            ptr->instance_list_ptr->heartbeat.provisioned = instance.heartbeat.provisioned;
                            ptr->instance_list_ptr->heartbeat.failures    =  0  ;
                            ptr->instance_list_ptr->heartbeat.b2b_misses  =  0  ;

                            ptr->instance_list_ptr->restart_to_str  = "0"  ;
                            ptr->instance_list_ptr->resume_to_str   = "0"  ;
                            ptr->instance_list_ptr->suspend_to_str  = "0"  ;
                            ptr->instance_list_ptr->shutdown_to_str = "0"  ;
                            ptr->instance_list_ptr->vote_to_str     = "0"  ;

                            ilog ("%s %s updated info\n", hostname.c_str(), instance.uuid.c_str());
                        
                            /* Setup the new channel */
                            send_cmd_to_guestServer ( hostname, 
                                                      MTC_CMD_ADD_INST, 
                                                      instance.uuid, 
                                                      instance.heartbeat.reporting );
                        }
                        else
                        {
                            ilog ("%s %s info unchanged\n", 
                                      hostname.c_str(), instance.uuid.c_str());
                        }
                    }
                    break ;
                }
            }
        }
        if (( ptr->next == NULL ) || ( ptr == guest_tail ))
           break ;
    }

    /* If the instance is not found then we need to add it to the specified host */
    if ( rc == FAIL_NOT_FOUND )
    {
        struct guestHostClass::guest_host * host_ptr = static_cast<struct guest_host *>(NULL);
        host_ptr = guestHostClass::getHost(hostname);
        if ( host_ptr ) 
        {
            instance.hostname = hostname    ;

            instance.restart_to_str  = "0"  ;
            instance.resume_to_str   = "0"  ;
            instance.suspend_to_str  = "0"  ;
            instance.shutdown_to_str = "0"  ;
            instance.vote_to_str     = "0"  ;

            instance.heartbeat.provisioned = true ;
            instance.heartbeat.failures    =  0   ;
            instance.heartbeat.b2b_misses  =  0   ;
            host_ptr->instance_list.push_back (instance);

            ilog ("%s %s add - Prov: %s Notify: %s\n", 
                      hostname.c_str(), 
                      instance.uuid.c_str(),
                      instance.heartbeat.provisioned ? "YES" : "no ",
                      instance.heartbeat.reporting ? "YES" : "no " );

            send_cmd_to_guestServer ( hostname, MTC_CMD_ADD_INST, instance.uuid, instance.heartbeat.reporting );
            rc = PASS ;
        }
        else
        {
            elog ("%s hostname is unknown (%s)\n", hostname.c_str(), instance.uuid.c_str() );
            rc = FAIL_INVALID_HOSTNAME ;
        }
    }
    return (rc);
}

/*****************************************************************************
 *
 * Name       : mod_inst
 *
 * Purpose    : Modify an instance's services' state(s)
 * 
 *****************************************************************************/

int guestHostClass::mod_inst ( string hostname, instInfo & instance )
{
    int  rc    = FAIL_NOT_FOUND  ;
    
    if ( instance.uuid.empty() )
    {
        elog ("%s empty instance uuid\n", hostname.c_str());
        return (FAIL_INVALID_UUID);
    }

   /**
    * First search for this instance.
    * If it is found against a different instance then we need to delete the
    * instance from that host and add it to he new host. 
    **/
    for ( struct guest_host * ptr = guest_head ; guest_head ; ptr = ptr->next )
    {
        if ( ptr->instance_list.size() != 0 )
        {
            for ( ptr->instance_list_ptr = ptr->instance_list.begin();
                  ptr->instance_list_ptr != ptr->instance_list.end();
                  ptr->instance_list_ptr++ )
            {
                if ( !ptr->instance_list_ptr->uuid.compare(instance.uuid))
                {
                    /* Verify that this instance is for the specified host.
                     * If not then delete this instance from this host and
                     * allow it to be added below for the specified host */
                    if ( !ptr->hostname.compare(hostname) )
                    {
                        /* This instance is provisioned for this host */

                        /* Manage its state */
                        if ( ptr->instance_list_ptr->heartbeat.reporting != instance.heartbeat.reporting )
                        {
                            ptr->instance_list_ptr->heartbeat.reporting = 
                                 instance.heartbeat.reporting ;

                            ilog ("%s %s instance reporting state changed to %s\n", 
                                      ptr->hostname.c_str(), ptr->instance_list_ptr->uuid.c_str(),
                                      ptr->instance_list_ptr->heartbeat.reporting ? "Enabled" : "Disabled");
                        }
                        else
                        {
                            ilog ("%s %s instance reporting state already %s\n", 
                                      ptr->hostname.c_str(), ptr->instance_list_ptr->uuid.c_str(),
                                      ptr->instance_list_ptr->heartbeat.reporting ? "Enabled" : "Disabled");
                        }
                        send_cmd_to_guestServer ( ptr->hostname, 
                                                  MTC_CMD_MOD_INST, 
                                                  ptr->instance_list_ptr->uuid, 
                                                  ptr->instance_list_ptr->heartbeat.reporting );
                        return (PASS) ;
                    }
                    else
                    {
                        ilog ("%s %s move to %s while %s\n", 
                                  ptr->hostname.c_str(),
                                  ptr->instance_list_ptr->uuid.c_str(), 
                                  hostname.c_str(),
                                  ptr->instance_list_ptr->heartbeat.reporting ? "enabled" : "disabled");
                        /**
                         * The instance must have moved to another host.
                         * Delete it here and then explicitely add 
                         * it below by keeping rc = FAIL_NOT_FOUND
                         **/
                        send_cmd_to_guestServer ( ptr->hostname, 
                                                  MTC_CMD_DEL_INST, 
                                                  instance.uuid, 
                                                  instance.heartbeat.reporting );
                        
                        ptr->instance_list.erase(ptr->instance_list_ptr);

                        /* Go through other hosts just to make it easy to exit
                         * - acts as a safety net */
                        break ;
                    }
                }
            }
        }
        if (( ptr->next == NULL ) || ( ptr == guest_tail ))
           break ;
    }

    /* If the instance is not found then we need to add it to the specified host */
    if ( rc == FAIL_NOT_FOUND )
    {
        struct guestHostClass::guest_host * ptr = guestHostClass::getHost(hostname);
        if ( ptr ) 
        {
            instance.hostname = hostname ;

            instance.heartbeat.provisioned = true ;
            
            /* Don't change the reportinfg state */
            instance.heartbeat.reporting   = instance.heartbeat.reporting ;
            
            instance.heartbeat.failures    =  0   ;
            instance.heartbeat.b2b_misses  =  0   ;
            
            instance.restart_to_str  = "0"  ;
            instance.resume_to_str   = "0"  ;
            instance.suspend_to_str  = "0"  ;
            instance.shutdown_to_str = "0"  ;
            instance.vote_to_str     = "0"  ;

            /* The mod might be straight to enabled state */
            ilog ("%s %s instance reporting state is %s\n",
                      ptr->hostname.c_str(),
                      instance.uuid.c_str(),
                      instance.heartbeat.reporting ? "Enabled" : "Disabled");

            ptr->instance_list.push_back (instance);
            send_cmd_to_guestServer ( hostname, 
                                      MTC_CMD_ADD_INST, 
                                      instance.uuid, 
                                      instance.heartbeat.reporting );
            rc = PASS ;
        }
        else
        {
            rc = FAIL_INVALID_HOSTNAME ;
        }
    }
    return (rc);
}


/*****************************************************************************
 *
 * Name       : del_inst
 *
 * Purpose    : Delete an instance from the guestAgent guestHostClass database
 *
 * Description: Search all the hosts for this instance and remove it
 *              from its instance tracking list.
 *
 *              Also send a delete command to the guestServer so that it
 *              has the opportunity to do any cleanup actions.
 *
 *****************************************************************************/
int guestHostClass::del_inst ( string instance_uuid )
{
    int rc = FAIL_NOT_FOUND ;

    if ( instance_uuid.empty() )
    {
        elog ("supplied instance uuid was null\n");
        return (FAIL_INVALID_UUID);
    }

    /** Loop over all hosts looking for this instance. */
    for ( struct guest_host * ptr = guest_head ; guest_head ; ptr = ptr->next )
    {
        if ( ptr->instance_list.size() != 0 )
        {
            for ( ptr->instance_list_ptr = ptr->instance_list.begin();
                  ptr->instance_list_ptr != ptr->instance_list.end();
                  ptr->instance_list_ptr++ )
            {
                if ( !ptr->instance_list_ptr->uuid.compare(instance_uuid))
                {
                    ilog ("%s removed instance %s\n", 
                              ptr->hostname.c_str(),
                              instance_uuid.c_str());

                    send_cmd_to_guestServer ( ptr->hostname, 
                                              MTC_CMD_DEL_INST, 
                                              instance_uuid, 
                                              ptr->instance_list_ptr->heartbeat.reporting );
 
                    ptr->instance_list.erase(ptr->instance_list_ptr);

                    return (PASS) ;
                }
                else
                {
                    jlog ("%s %s:%s (search)\n", 
                              ptr->hostname.c_str(), 
                              ptr->instance_list_ptr->uuid.c_str(), 
                              instance_uuid.c_str());
                }
            }
        }
        if (( ptr->next == NULL ) || ( ptr == guest_tail ))
           break ;
    }
    wlog ("instance was not found '%s'\n", instance_uuid.c_str());
    return (rc);
}

/** 
 * Change the host level fault repoorting state for the specified host.
 *
 * TODO: Consider sending a MOD_HOST command to the guestServer
 *
 **/
int guestHostClass::host_inst ( string hostname, mtc_cmd_enum command )
{
    int  rc = FAIL_NOT_FOUND  ;
    
    struct guestHostClass::guest_host * ptr = static_cast<struct guest_host *>(NULL);

    if ( hostname.empty() )
    {
        elog ("no hostname specified\n");
        return (FAIL_STRING_EMPTY);
    }
    ptr = guestHostClass::getHost(hostname);
    if ( ptr )
    {
        if ( command == MTC_CMD_ENABLE )
        {
            ptr->reporting = true ;
            ilog ("%s host level heartbeat reporting is Enabled\n", hostname.c_str());
            send_cmd_to_guestServer ( hostname, MTC_CMD_MOD_HOST, ptr->uuid, true );
        }
        else
        {
            ptr->reporting = false ;
            ilog ("%s host level heartbeat reporting is Disabled\n", hostname.c_str());
            send_cmd_to_guestServer ( hostname, MTC_CMD_MOD_HOST, ptr->uuid, false );
        }
        rc = PASS ;
    }
    return (rc);
}


instInfo * guestHostClass::get_inst ( string instance_uuid )
{
    if ( instance_uuid.empty() )
    {
        elog ("empty instance uuid\n");
        return (NULL);
    }

    /** Loop over all hosts looking for this instance. */
    for ( struct guest_host * ptr = guest_head ; guest_head ; ptr = ptr->next )
    {
        if ( ptr->instance_list.size() != 0 )
        {
            for ( ptr->instance_list_ptr = ptr->instance_list.begin();
                  ptr->instance_list_ptr != ptr->instance_list.end();
                  ptr->instance_list_ptr++ )
            {
                if ( !ptr->instance_list_ptr->uuid.compare(instance_uuid))
                {
                    dlog ("%s found instance %s\n",
                              ptr->hostname.c_str(),
                              ptr->instance_list_ptr->uuid.c_str());

                    return ( &(*ptr->instance_list_ptr) );
                }
            }
        }
        if (( ptr->next == NULL ) || ( ptr == guest_tail ))
            break ;
    }
    return (NULL);
}

/** returns he number of instances on this host */
int guestHostClass::num_instances ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr != NULL )
    {
        return ( guest_host_ptr->instance_list.size());
    }
    return ( 0);
}

/****************************************************************************/
/** Host Class Setter / Getters */
/****************************************************************************/

struct guestHostClass::guest_host * guestHostClass::getHost_timer ( timer_t tid )
{
   /* check for empty list condition */
   if ( tid != NULL )
   {
       for ( struct guest_host * host_ptr = guest_head ; guest_head ; host_ptr = host_ptr->next )
       {
           if ( host_ptr->host_audit_timer.tid == tid )
           {
               return host_ptr ;
           }
           if (( host_ptr->next == NULL ) || ( host_ptr == guest_tail ))
               break ;
       }
    } 
    return static_cast<struct guest_host *>(NULL);
}

static string null_str = "" ;
string guestHostClass::get_host_name ( string uuid )
{
    guest_host * guest_host_ptr = getHost ( uuid );
    if ( guest_host_ptr != NULL )
    {
        return ( guest_host_ptr->hostname );
    }
    return ( null_str );
}

string guestHostClass::get_host_uuid ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr != NULL )
    {
        return ( guest_host_ptr->uuid );
    }
    return ( null_str );
}





string guestHostClass::get_inst_host_name ( string instance_uuid )
{
    if ( instance_uuid.empty() )
    {
        elog ("empty instance uuid\n");
        return (null_str);
    }
        /** Loop over all hosts looking for this instance. */
        for ( struct guest_host * ptr = guest_head ; guest_head ; ptr = ptr->next )
        {
            if ( ptr->instance_list.size() != 0 )
            {
                for ( ptr->instance_list_ptr = ptr->instance_list.begin();
                      ptr->instance_list_ptr != ptr->instance_list.end();
                      ptr->instance_list_ptr++ )
                {
                    if ( !ptr->instance_list_ptr->uuid.compare(instance_uuid))
                    {
                        dlog ("%s found instance %s\n",
                                  ptr->hostname.c_str(),
                                  ptr->instance_list_ptr->uuid.c_str());
    
                        return ( ptr->hostname );
                    }
                }
            }
            if (( ptr->next == NULL ) || ( ptr == guest_tail ))
               break ;
        }
    return ( null_str );
}

/** 
 * Set and Get a bool that indicates whether we already
 * got the host reporting state from the VIM.
 *
 * The VIM might not be running at the time this daemon
 * is started so we need to retry until we get it 
 **/
void guestHostClass::set_got_host_state ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        guest_host_ptr->got_host_state = true ;
    }
    else
    {
        wlog ("%s not found\n", hostname.c_str());
    }
}

bool guestHostClass::get_got_host_state ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        return ( guest_host_ptr->got_host_state );
    }
    else
    {
        wlog ("%s not found\n", hostname.c_str());
    }
    return (false);
}

void guestHostClass::set_got_instances ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        guest_host_ptr->got_instances = true ;
    }
    else
    {
        wlog ("%s not found\n", hostname.c_str());
    }
}

bool guestHostClass::get_got_instances ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        return ( guest_host_ptr->got_instances );
    }
    else
    {
        wlog ("%s not found\n", hostname.c_str());
    }
    return (false);
}

bool guestHostClass::get_reporting_state ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        return ( guest_host_ptr->reporting );
    }
    else
    {
        wlog ("%s not found\n", hostname.c_str());
    }
    return ( false );
}

int  guestHostClass::set_reporting_state( string hostname, bool reporting )
{
    int rc = PASS ;
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        guest_host_ptr->reporting = reporting ;
    }
    else
    {
        wlog ("%s not found\n", hostname.c_str());
        rc = FAIL_NOT_FOUND ;
    }
    return (rc);
}


string guestHostClass::get_host_ip ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        return ( guest_host_ptr->ip );
    }
    return ( null_str );
}

void guestHostClass::set_query_flag ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        guest_host_ptr->query_flag = true ;
    }
}

void guestHostClass::clr_query_flag ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        guest_host_ptr->query_flag = false ;
    }
}

bool guestHostClass::get_query_flag ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr  )
    {
        return ( guest_host_ptr->query_flag );
    }
    return ( false );
}

int guestHostClass::set_inst_state ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        for ( guest_host_ptr->instance_list_ptr = guest_host_ptr->instance_list.begin();
              guest_host_ptr->instance_list_ptr != guest_host_ptr->instance_list.end();
              guest_host_ptr->instance_list_ptr++ )
        {
            send_cmd_to_guestServer ( hostname, MTC_CMD_MOD_INST, 
                    guest_host_ptr->instance_list_ptr->uuid,
                    guest_host_ptr->instance_list_ptr->heartbeat.reporting );
        }
    }
    return (PASS);
}

void guestHostClass::inc_query_misses ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        guest_host_ptr->query_misses++ ;
    }
    else
    {
        /* TODO: turn into a wlog_throttled ... */
        dlog ("%s not found\n", hostname.c_str());
    }
}

void guestHostClass::clr_query_misses ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        guest_host_ptr->query_misses = 0 ;
    }
    else
    {
        /* TODO: turn into a wlog_throttled ... */
        dlog ("%s not found\n", hostname.c_str());
    }
}

int  guestHostClass::get_query_misses ( string hostname )
{
    guest_host * guest_host_ptr = getHost ( hostname );
    if ( guest_host_ptr )
    {
        return ( guest_host_ptr->query_misses ) ;
    }
    else
    {
        /* TODO: turn into a wlog_throttled ... */
        dlog ("%s not found\n", hostname.c_str());
    }
    return (-1);
}

/**************************************************************************
 *
 * Name    : getEvent
 *
 * Purpose : Return a reference to a host or instance level libEvent.
 *
 **************************************************************************/
libEvent & guestHostClass::getEvent ( struct event_base * base_ptr, string & hostname )
{
     struct guest_host * guest_ptr = static_cast<struct guest_host *>(NULL) ;

    /* check for empty list condition */
    if ( guest_head == NULL )
        return (nullEvent) ;

    if ( base_ptr == NULL )
        return (nullEvent) ;
    
    /** Loop over all hosts looking for this instance. */
    for ( guest_ptr = guest_head ; guest_ptr != NULL ; guest_ptr = guest_ptr->next )
    {
        if ( guest_ptr->vimEvent.base == base_ptr )
        {
            dlog2 ("%s Found Event Base Pointer (host) (%p)\n", 
                       guest_ptr->vimEvent.uuid.c_str(), 
                       guest_ptr->vimEvent.base);

            /* Update the reference variable */
            hostname = guest_ptr->hostname ;

            return (guest_ptr->vimEvent) ;
        }
        else if ( guest_ptr->instance_list.size() )
        {
            for ( guest_ptr->instance_list_ptr  = guest_ptr->instance_list.begin();
                  guest_ptr->instance_list_ptr != guest_ptr->instance_list.end();
                  guest_ptr->instance_list_ptr++ )
            {
                if ( guest_ptr->instance_list_ptr->vimEvent.base == base_ptr )
                {
                    dlog2 ("%s Found Event Base Pointer (instance) (%p)\n", 
                               guest_ptr->instance_list_ptr->uuid.c_str(), 
                               guest_ptr->instance_list_ptr->vimEvent.base);

                    /* Update the reference variable */
                    hostname = guest_ptr->hostname ;

                    return (guest_ptr->instance_list_ptr->vimEvent) ;
                }
            }
        }
        if (( guest_ptr->next == NULL ) || ( guest_ptr == guest_tail ))
            break ;
    }
    return (nullEvent) ;
}

libEvent & guestHostClass::get_host_event ( string hostname )
{
    guestHostClass::guest_host * guest_host_ptr ;
    guest_host_ptr = guestHostClass::getHost ( hostname );
    if ( guest_host_ptr )
    {
        return ( guest_host_ptr->vimEvent );
    }
    else
    {
        wlog ("%s not found\n", hostname.c_str());
    }
    return ( nullEvent );
}

/*****************************************************************************
 *                              Memory Dump Stuff                            *
 *****************************************************************************/
void guestHostClass::print_node_info ( void )
{
    fflush (stdout);
    fflush (stderr);
}

void guestHostClass::mem_log_info ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "Hosts:%d  Allocs:%d  Memory:%d\n", hosts, memory_allocs, memory_used );
    mem_log (str);
}

void guestHostClass::mem_log_info_host ( struct guestHostClass::guest_host * guest_host_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s:%s\n", guest_host_ptr->hostname.c_str(), guest_host_ptr->ip.c_str());
    mem_log (str);
}

void mem_log_delimit_host ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "-------------------------------------------------------------\n");
    mem_log (str);
}

void guestHostClass::mem_log_info_inst ( struct guestHostClass::guest_host * ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    if ( ptr->instance_list.size() )
    {
        for ( ptr->instance_list_ptr = ptr->instance_list.begin();
              ptr->instance_list_ptr != ptr->instance_list.end();
              ptr->instance_list_ptr++ )
        {
            snprintf (&str[0], MAX_MEM_LOG_DATA, 
                               "   %s  %s  Faults:%d  %s  %s\n",
                       ptr->instance_list_ptr->uuid.data(), 
                       ptr->instance_list_ptr->hostname.data(), 
                       ptr->instance_list_ptr->heartbeat.failures,
                       ptr->instance_list_ptr->heartbeat.provisioned ? "provisioned" : "",
                       ptr->instance_list_ptr->heartbeat.reporting ? "reporting" : "");
            mem_log (str);

            snprintf (&str[0], MAX_MEM_LOG_DATA, 
                               "      Timeouts:  Restart:%s  Resume:%s  Suspend:%s  Shutdown:%s  Vote:%s\n",
                       ptr->instance_list_ptr->restart_to_str.data(), 
                       ptr->instance_list_ptr->resume_to_str.data(),
                       ptr->instance_list_ptr->suspend_to_str.data(),
                       ptr->instance_list_ptr->shutdown_to_str.data(),
                       ptr->instance_list_ptr->vote_to_str.data());
            mem_log (str);

            // mem_log_delimit_host ();
        }
    }
    else
    {
        snprintf (&str[0], MAX_MEM_LOG_DATA, "no instances\n");
        mem_log (str);
    }
}


void guestHostClass::memDumpNodeState ( string hostname )
{
    guestHostClass::guest_host* guest_host_ptr ;
    guest_host_ptr = guestHostClass::getHost ( hostname );
    if ( guest_host_ptr == NULL )
    {
        mem_log ( hostname, ": ", "Not Found in guestHostClass\n" );
        return ;
    }
    else
    {
        mem_log_info_host ( guest_host_ptr );
    }
}

void guestHostClass::memDumpAllState ( void )
{
    guestHostClass::hostBase.memLogDelimit ();
    
    mem_log_info ( );

    /* walk the node list looking for nodes that should be monitored */
    for ( struct guest_host * ptr = guest_head ; ptr != NULL ; ptr = ptr->next )
    {
        memDumpNodeState  ( ptr->hostname );
        if ( (ptr->hosttype & COMPUTE_TYPE) == COMPUTE_TYPE) 
        {
            mem_log_info_inst ( ptr );
        }
        guestHostClass::hostBase.memLogDelimit ();
    }
}
