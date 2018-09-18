#ifndef __INCLUDE_HOSTCLASS_H__
#define __INCLUDE_HOSTCLASS_H__
/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Host Maintenance "Host Manager"
 * class, support structs and enums.
 */ 

#include <sys/types.h>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <list>
#include <vector>

//using namespace std;

#include "nodeTimers.h"   /* for ... mtcTimer              */

/**
 * @addtogroup hostBaseClass
 * @{
 * This class is used to maintain a linked list of hosts for a given application.
 */

class hostBaseClass
{
    private:

    /** 
     *  A single host entity within the hostBaseClass.
     *  Used to build a linked list of added/provisioned hosts.
     */
    struct host {

        /** The name of the host */
        std::string hostname ;
        
        /** The name of the host */
        std::string uuid ; 

        /** The IP address of the host */    
        std::string ip ;

        /** The Mac address of the host node */    
        std::string mac ;

        /** A string indicating the host type as 'compute' , 'storage' or 'controller' */
        std::string type ;

        /** The Type ; host specific service refinement */
        int  nodetype ;  

        /** general retry counter */
        int  retries  ;

        /** Generic toggle switch */
        bool toggle ;

        /** Pointer to the previous host in the list */
        struct host * prev;
		
        /** Pointer to the next host in the list */
        struct host * next;
    } ;

    struct host * head ; /**< Host Linked List Head pointer */
    struct host * tail ; /**< Host Linked List Tail pointer */

    /** Allocate memory for a new host.
    *
    * Preserves the host address in the host_ptr list and increments
    * the memory_allocs counter used by the inservice test audit.
    *
    * @return 
    * a pointer to the memory of the newly allocated host */    
    struct hostBaseClass::host * newHost ( void );

   /** Start heartbeating a new host.
    * 
    * host is added to the end of the host linked list.
    *
    * @param host_info_ptr
    *  is a pointer containing pertinent info about the physical host
    * @return 
    *  a pointer to the newly added host
    */
    struct hostBaseClass::host* addHost ( string hostname );

   /** Get pointer to "hostname" host.
    * 
    * Host list lookup by pointer from hostname.
    *
    * @param host_info_ptr 
    *  is a pointer containing info required to find the host in the host list
    * @return 
    *  a pointer to the hostname's host
    */
    struct hostBaseClass::host* getHost ( string hostname );

   /** Free the memory of a previously allocated host.
    *
    * The memory to be removed is found in the host_ptr list, cleared and 
    * the memory_allocs counter is decremented.
    * If the memory cannot be found then an error is returned.
    *
    * @param host_ptr
    *  is a pointer to the host to be freed
    * @return
    *  a signed integer of PASS or -EINVAL
    */
    int delHost ( struct hostBaseClass::host * host_ptr );
    

   /** Remove a host from the linked list.
    *
    * Node is spliced out of the host linked list.
    *
    * @param node_info_ptr 
    *  is a pointer containing info required to find the host in the host list
    * @return 
    *  an integer of PASS or  -EINVAL  */
    int remHost ( string hostname );

  /** List of allocated host memory.
    *
    * An array of host pointers.
    */ 
    hostBaseClass::host * host_ptrs[MAX_HOSTS] ;
        
   /** A memory allocation counter.
    *
    * Should represent the number of hosts in the linked list.
    */
    int memory_allocs ;

    /** A memory used counter
    *  
    * A variable storing the accumulated host memory
    */  
    int memory_used ;

    void mem_log_host ( struct hostBaseClass::host * host_ptr );

/** Public Interfaces that allow hosts to be 
 *  added or removed from maintenance.
 */
public:
    
     hostBaseClass();	/**< constructor */	
    ~hostBaseClass();   /**< destructor  */

    /**< The service this list is associated with */
    int service ;

    int hosts ;

    string my_hostname ; /**< My hostname                     */
    string my_local_ip ; /**< Primary IP address              */
    string my_float_ip ; /**< Secondary (floating) IP address */


    bool bm_provisioned ;

    /** Add a host to the linked list using public API */
    int add_host ( node_inv_type & inv );
    
    /** Mod a host to the linked list using public API */
    int mod_host ( node_inv_type & inv );

    /** Remove a host from the linked list using public API */
    int rem_host ( string hostname );

    /** Free the memory of an already allocated host link using public API */
    int del_host ( string hostname );

    string get_ip        ( string hostname );
    string get_uuid      ( string hostname );
    string get_hostaddr  ( string hostname );
    string get_hostname  ( string uuid );



    void memLogDelimit    ( void );              /**< Debug log delimiter    */
    void memDumpNodeState ( string hostname );
    void memDumpAllState  ( void );
    void print_node_info  ( void );              /**< Print node info banner */

    /** This is a list of host names. */ 
    std::list<string>           hostlist ;
    std::list<string>::iterator hostlist_iter_ptr ;



} ;

/**
 * @addtogroup hostBaseClass_base
 * @{
 */

hostBaseClass * get_hostBaseClass_ptr ( void );

/* allocates hostBaseClass obj_ptr and host_ptr */
#define GET_HOST_PTR(hostname)                                       \
    hostBaseClass * obj_ptr = get_hostBaseClass_ptr () ;             \
    hostBaseClass::host * host_ptr = obj_ptr->getHost ( hostname ) ; \
    if ( host_ptr == NULL )                                          \
    {                                                                \
        elog ("%s hostname unknown\n", hostname.c_str());            \
        return (FAIL_HOSTNAME_LOOKUP);                               \
    }

#endif // __INCLUDE_HOSTCLASS_H__
