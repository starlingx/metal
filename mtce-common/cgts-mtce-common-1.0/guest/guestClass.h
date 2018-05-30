#ifndef __INCLUDE_GUESTCLASS_H__
#define __INCLUDE_GUESTCLASS_H__

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include "guestBase.h"
#include "httpUtil.h"    /* for ... libEvent and httpUtil_...  */
#include "hostClass.h"

typedef enum
{
    STAGE__START,
    STAGES
} guest_stage_enum ;


class guestHostClass
{
    private:
    struct guest_host {

        /** Pointer to the previous / next host in the list */
        struct guest_host * prev;
        struct guest_host * next;

        string hostname ;
        string uuid     ;
        string ip       ;
        int    hosttype ;

        /**
         * Top level gate for the host.
         * If false then reporting for all instances are off.
         */
        bool   reporting;

        bool   query_flag   ;
        int    query_misses ;

        /** Instance level Audit timer */
        struct mtc_timer host_audit_timer;
    
        /** flag that indicates we were able to fetch host state from the VIM */ 
        bool got_host_state ; 
        
        /** flag that indicates we were able to fetch intances from the VIM */ 
        bool got_instances  ;

        /** Main FSM stage */
        guest_stage_enum stage ;

        /* List of instances for this host */
        list<instInfo>           instance_list ; 
        list<instInfo>::iterator instance_list_ptr;

        libEvent vimEvent ;
    };

   /** List of allocated host memory.
    *
    * An array of host pointers.
    */ 
    guest_host * host_ptrs[MAX_HOSTS] ;

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


    // struct hostBaseClass::host* getHost ( string hostname );
    
    struct guest_host * guest_head ; /**< Host Linked List Head pointer */
    struct guest_host * guest_tail ; /**< Host Linked List Tail pointer */

    struct guestHostClass::guest_host* newHost ( void );
    struct guestHostClass::guest_host* addHost ( string hostname );
    struct guestHostClass::guest_host* getHost ( string hostname );
    int                                remHost ( string hostname );
    int                                delHost ( struct guestHostClass::guest_host * guest_host_ptr );
    struct guestHostClass::guest_host* getHost_timer ( timer_t tid );
    
    libEvent & getEvent ( struct event_base * base_ptr, string & hostname );

    const 
    char * get_guestStage_str    ( struct guestHostClass::guest_host * guest_host_ptr );
    int        guestStage_change ( struct guestHostClass::guest_host * guest_host_ptr, guest_stage_enum newStage );

    void mem_log_info ( void );
    void mem_log_info_host ( struct guestHostClass::guest_host * guest_host_ptr );
    void mem_log_info_inst ( struct guestHostClass::guest_host * guest_host_ptr );

    public:

     guestHostClass(); /**< constructor */	
    ~guestHostClass(); /**< destructor  */

    hostBaseClass hostBase ;
 
    bool exit_fsm ;
    void run_fsm ( string hostname );

    bool audit_run ;
    
    /** Host level Audit timer */
    struct mtc_timer audit_timer;

    /** This is a list of host names. */ 
    std::list<string>           hostlist ;
    std::list<string>::iterator hostlist_iter_ptr ;

    // void guest_fsm ( void );

    int  hosts ;

    /* For select dispatch */ 
    struct timeval  waitd           ;

    fd_set          inotify_readfds ;
    fd_set          instance_readfds ;
    fd_set          message_readfds ;

    int  add_host ( string uuid, string address, string hostname, string nodetype );
    int  mod_host ( string uuid, string address, string hostname, string nodetype );
    int  del_host ( string hostname ); /* delete the host from the daemon - mtcAgent */
    int  rem_host ( string hostname );
    
    /** Delete all instances for this host */
    int   del_host_inst ( string host_uuid ); 

    int        add_inst ( string hostname, instInfo & instance );
    int        mod_inst ( string hostname, instInfo & instance );
    int        del_inst ( string instance );
    instInfo * get_inst ( string instance );
    
    /* The handler that lib event calls to handle the return response */
    void guestVimApi_handler ( struct evhttp_request *req, void *arg );

   /** 
    * Change all the instance service states to enabled or disable
    * for the specified host.
    **/
    int  host_inst( string hostname , mtc_cmd_enum command );

    /** 
     * Set and Get a bool that indicates whether we already
     * got the host reporting state from the VIM.
     *
     * The VIM might not be running at the time this daemon
     * is started so we need to retry until we get it 
     **/
    void  set_got_host_state  ( string hostname );
    bool  get_got_host_state  ( string hostname );
    void  set_got_instances   ( string hostname );
    bool  get_got_instances   ( string hostname );

    /** returns he number of instances on this host */
    int  num_instances        ( string hostname );

    string get_host_name      ( string host_uuid     );
    string get_host_uuid      ( string hostname      );
    string get_host_ip        ( string hostname      );
    string get_inst_host_name ( string instance_uuid );

    /* Send the instance reporting state to the guestServer on that host 
     * primarily used to preiodically refresh instance reporting state or
     * set it when the guestServer seems to have restarted */
    int    set_inst_state     ( string hostname      );
    
    libEvent & get_host_event ( string hostname      );

    void   inc_query_misses   ( string hostname );
    void   clr_query_misses   ( string hostname );
    int    get_query_misses   ( string hostname );
    void   set_query_flag     ( string hostname );
    void   clr_query_flag     ( string hostname );
    bool   get_query_flag     ( string hostname );

    bool   get_reporting_state( string hostname );
    int    set_reporting_state( string hostname, bool enabled );

    void memLogDelimit    ( void );              /**< Debug log delimiter    */
    void memDumpNodeState ( string hostname );
    void memDumpAllState  ( void );
    void print_node_info  ( void );              /**< Print node info banner */
};

guestHostClass * get_hostInv_ptr ( void );

#endif /* __INCLUDE_GUESTCLASS_H__ */
