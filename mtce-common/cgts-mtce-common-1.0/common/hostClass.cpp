/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Host Base Class Member Implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <errno.h>  /* for ENODEV, EFAULT and ENXIO */
#include <unistd.h> /* for close and usleep */

using namespace std;

#ifdef  __AREA__
#undef  __AREA__
#define __AREA__ "~~~"
#endif

#include "nodeBase.h"
#include "hostClass.h"
#include "nodeUtil.h"


hostBaseClass::hostBaseClass() /* constructor */
{
    for(unsigned int i = 0; i < MAX_HOSTS; ++i)
    {
        host_ptrs[i] = NULL;
    }
    bm_provisioned= false;
    head = tail   = NULL;
    memory_allocs = 0   ;
    memory_used   = 0   ;
    hosts         = 0   ;
    service       = 0   ;
   
    /* Start with null identity */
    my_hostname.clear() ;
    my_local_ip.clear() ;
    my_float_ip.clear() ;
}

hostBaseClass::~hostBaseClass() /* destructor */
{
    host *      ptr = head ;
    host * temp_ptr = ptr  ;
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
}

/*
 * Allocates memory for a new host and stores its the address in host_ptrs 
 *
 * @param void
 * @return pointer to the newly allocted host memory
 */ 
struct hostBaseClass::host * hostBaseClass::newHost ( void )
{
   struct hostBaseClass::host * host_ptr = NULL ;
      
   if ( memory_allocs == 0 )
   {
       memset ( host_ptrs, 0 , sizeof(struct host *)*MAX_HOSTS);
   }

   // find an empty spot
   for ( int i = 0 ; i < MAX_HOSTS ; i++ )
   {
      if ( host_ptrs[i] == NULL )
      {
          host_ptrs[i] = host_ptr = new host ;
          memory_allocs++ ;
          memory_used += sizeof (struct hostBaseClass::host);
          // ilog ("%p:%p - mem after new: allocs:%d used:%d\n", host_ptr , host_ptrs[i], memory_allocs, memory_used);
          return host_ptr ;
      }
   }
   elog ( "Failed to save new host pointer address\n" );
   return host_ptr ;
}


/* Frees the memory of a pre-allocated host and removes
 * it from the host_ptrs list 
 * @param host * pointer to the host memory address to be freed
 * @return int return code { PASS or -EINVAL }
 */
int hostBaseClass::delHost ( struct hostBaseClass::host * host_ptr )
{
    if ( memory_allocs > 0 )
    {
        for ( int i = 0 ; i < MAX_NODES ; i++ )
        {
            if ( host_ptrs[i] == host_ptr )
            {
                // ilog ("%p:%p - mem before del: allocs:%d used:%d\n", host_ptr , host_ptrs[i], memory_allocs, memory_used);
                delete host_ptr ;
                host_ptrs[i] = NULL ;
                memory_allocs-- ;
                memory_used -= sizeof (struct hostBaseClass::host);
                return PASS ;
            }
        }
        elog ( "Error: Unable to validate memory address being freed\n" );
    }
    else
       elog ( "Error: Free memory called when there is no memory to free\n" );
    
    return -EINVAL ;
}

 /*
  * Allocate new host and tack it on the end of the host_list
  */  
struct 
hostBaseClass::host* hostBaseClass::addHost( string hostname )
{
    /* verify host is not already provisioned */
    struct host * ptr = getHost ( hostname );
    if ( ptr )
    {
        if ( remHost ( hostname ) )
        {
            /* Should never get here but if we do then */
            /* something is seriously wrong */
            elog ("Error: Unable to remove host during reprovision\n"); 
            return static_cast<struct host *>(NULL);
        }
    }
    
    /* allocate memory for new host */
    ptr = newHost ();
    if( ptr == NULL )
    {
        elog ( "Error: Failed to allocate memory for new host\n" );
		return static_cast<struct host *>(NULL);
    }
    
    /* Init the new host */
    ptr->hostname = hostname ;

    /* If the host list is empty add it to the head */
    if( head == NULL )
    {
        head = ptr ;  
        tail = ptr ;
        ptr->prev = NULL ;
        ptr->next = NULL ;
    }
    else
    {
        /* link the new_host to the tail of the host_list
         * then mark the next field as the end of the host_list
         * adjust tail to point to the last host
         */
        tail->next = ptr  ;
        ptr->prev  = tail ;
        ptr->next  = NULL ;
        tail = ptr ; 
    }

    hosts++ ;

    return ptr ;
}

struct hostBaseClass::host* hostBaseClass::getHost ( string hostname )
{
   /* check for empty list condition */
   if ( head == NULL )
      return NULL ;

   for ( struct host * ptr = head ;  ; ptr = ptr->next )
   {
       if ( !hostname.compare ( ptr->hostname ))
       {
           // ilog ("%s %p\n", hostname.c_str(), ptr );
           return ptr ;  
       }
       else if ( !hostname.compare ( ptr->uuid ))
       {
           // ilog ("%s %p\n", hostname.c_str(), ptr );
           return ptr ;  
       }
       if (( ptr->next == NULL ) || ( ptr == tail ))
           break ;
    }
    return static_cast<struct host *>(NULL);
}

/* Remove a hist from the linked list of hosts - may require splice action */
int hostBaseClass::remHost( string hostname )
{
    if ( hostname.c_str() == NULL )
        return -ENODEV ;

    if ( head == NULL )
        return -ENXIO ;
    
    struct host * ptr = getHost ( hostname );

    if ( ptr == NULL )
        return -EFAULT ;

    /* If the host is the head host */
    if ( ptr == head )
    {
        /* only one host in the list case */
        if ( head == tail )
        {
            dlog ("Single Host -> Head Case\n");
            head = NULL ;
            tail = NULL ;
        }
        else
        {
            dlog ("Multiple Hosts -> Head Case\n");
            head = head->next ;
            head->prev = NULL ; 
        }
    }
    /* if not head but tail then there must be more than one
     * host in the list so go ahead and chop the tail.
     */
    else if ( ptr == tail )
    {
        dlog ("Multiple Host -> Tail Case\n");
        tail = tail->prev ;
        tail->next = NULL ;
    }
    else
    {
        dlog ("Multiple Host -> Full Splice Out\n");
        ptr->prev->next = ptr->next ;
        ptr->next->prev = ptr->prev ;
    }
    delHost ( ptr );
    hosts-- ;
    return (PASS) ;
}



/******************************************************************************************
 ******************************************************************************************
 *****************************************************************************************/ 



int hostBaseClass::add_host ( node_inv_type & inv )
{
    int rc = FAIL ;
    struct hostBaseClass::host * host_ptr = static_cast<struct host *>(NULL);

    if  (( inv.name.empty())           || 
         ( !inv.name.compare ("none")) ||
         ( !inv.name.compare ("None")))
    {
        wlog ("Refusing to add host with 'null' or 'invalid' hostname (%s)\n", 
               inv.uuid.c_str());
        return (FAIL_INVALID_HOSTNAME) ;
    }

    host_ptr = hostBaseClass::getHost(inv.name);
    if ( host_ptr ) 
    {
        dlog ("%s Already provisioned\n", host_ptr->hostname.c_str());

        /* Send back a retry in case the add needs to be converted to a modify */
        return (RETRY);
    }
    /* Otherwise add it as a new host */
    else
    {
        if ( daemon_get_cfg_ptr()->debug_level > 1 )
            print_inv ( inv );

        host_ptr = hostBaseClass::addHost(inv.name);
        if ( host_ptr )
        {
            host_ptr->ip   = inv.ip   ;
            host_ptr->mac  = inv.mac  ;
            host_ptr->uuid = inv.uuid ;

            host_ptr->type = inv.type ;
            host_ptr->nodetype = CGTS_NODE_NULL ;
            
            host_ptr->retries = 0 ;
            host_ptr->toggle = false ;

            /* Add to the end of inventory */
            hostlist.push_back ( host_ptr->hostname );
            dlog ("%s Added Host Base\n", inv.name.c_str());
            rc = PASS ;
        }
        else
        {
            elog ("%s Host Base Add Failed\n", inv.name.c_str());
            rc = FAIL_NULL_POINTER ;
        }
    }
    return (rc);
}

int hostBaseClass::rem_host ( string hostname )
{
    int rc = FAIL ;
    if ( ! hostname.empty() )
    {  
        hostlist.remove ( hostname );
        rc = hostBaseClass::remHost ( hostname );
    }
    return ( rc );
}

int hostBaseClass::del_host ( string hostname )
{
    int rc = FAIL_DEL_UNKNOWN ;
    hostBaseClass::host * host_ptr = hostBaseClass::getHost( hostname );
    if ( host_ptr ) 
    {
        rc = rem_host ( host_ptr->hostname );
        if ( rc == PASS )
        {
            dlog ("%s Deleted\n", host_ptr->hostname.c_str());
            print_node_info();
        }
        else
        {
            elog ("%s Delete Failed (rc:%d)\n", hostname.c_str(), rc );
        }
    }
    else
    {
        wlog ("Unknown hostname: %s\n", hostname.c_str());
    }
    return (rc);
}

/** Get this hosts uuid address */
string hostBaseClass::get_uuid ( string hostname )
{
    hostBaseClass::host * host_ptr ;
    host_ptr = hostBaseClass::getHost ( hostname );
    if ( host_ptr != NULL )
    {
         return (host_ptr->uuid );
    }
    elog ("%s uuid lookup failed\n", hostname.c_str() );
    return ("");
}

/** Get this hosts uuid address */
string hostBaseClass::get_hostname ( string uuid )
{
    hostBaseClass::host * host_ptr ;
    host_ptr = hostBaseClass::getHost ( uuid );
    if ( host_ptr != NULL )
    {
         return (host_ptr->hostname );
    }
    elog ("%s hostname lookup failed\n", uuid.c_str() );
    return ("");
}

/** Get this hosts ip address */
string hostBaseClass::get_ip ( string hostname )
{
    hostBaseClass::host * host_ptr ;
    host_ptr = hostBaseClass::getHost ( hostname );
    if ( host_ptr != NULL )
    {
         return (host_ptr->ip );
    }
    elog ("%s ip lookup failed\n", hostname.c_str() );
    return ("");
}

static string null_str = "" ;
string hostBaseClass::get_hostaddr ( string hostname )
{
    hostBaseClass::host* host_ptr ;
    host_ptr = hostBaseClass::getHost ( hostname );
    if ( host_ptr != NULL )
    {
        return ( host_ptr->ip );
    }
    return ( null_str );
}



void hostBaseClass::print_node_info ( void )
{
    fflush (stdout);
    fflush (stderr);
}

void hostBaseClass::memLogDelimit ( void )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "-------------------------------------------------------------\n");
    mem_log (str);
}

void hostBaseClass::mem_log_host ( struct hostBaseClass::host * host_ptr )
{
    char str[MAX_MEM_LOG_DATA] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s\t%s - %s - %s - %s\n", 
               host_ptr->hostname.c_str(), 
               host_ptr->ip.c_str(),
               host_ptr->mac.c_str(),
               host_ptr->uuid.c_str(), 
               host_ptr->type.c_str());
    mem_log (str);
}

void hostBaseClass::memDumpNodeState ( string hostname )
{
    hostBaseClass::host* host_ptr ;
    host_ptr = hostBaseClass::getHost ( hostname );
    if ( host_ptr == NULL )
    {
        mem_log ( hostname, ": ", "Not Found hostBaseClass\n" );
        return ;
    }
    else
    {
        mem_log_host ( host_ptr );
        // memLogDelimit ();
    }
}

