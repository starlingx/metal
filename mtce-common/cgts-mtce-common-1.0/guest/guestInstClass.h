#ifndef __INCLUDE_INSTBASECLASS_H__
#define __INCLUDE_INSTBASECLASS_H__

/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file 
  * Wind River CGTS Platform Guest Services "Instances Base Class Header"
  */

#include "guestBase.h"          /* for ... instInfo                    */

typedef enum
{
    FSM_ACTION__NONE,
    FSM_ACTION__CONNECT,
    FSM_ACTION__LAST

} guest_fsmActions_enum ;

typedef enum
{
    INST_CONNECT__START  = 1,
    INST_CONNECT__WAIT   = 2,
    INST_CONNECT__RETRY  = 3,
    INST_CONNECT__DONE   = 4,
    INST_CONNECT__STAGES = 5
} guest_connectStages_enum ;

typedef enum
{
    INST_MONITOR__STEADY  = 0,
    INST_MONITOR__DELAY   = 2,
    INST_MONITOR__FAILURE = 1,
} guest_monitorStages_enum ;


typedef enum
{
    INST_MESSAGE__RECEIVE       = 0,
    INST_MESSAGE__SEND_INIT_ACK = 1,
    INST_MESSAGE__RESP_WAIT     = 2, /* Waiting for heartbeat challenge response          */
    INST_MESSAGE__SEND_WAIT     = 3, /* Waiting for period to expire for challenge resend */
    INST_MESSAGE__TRANSMIT      = 4,
    INST_MESSAGE__STALL         = 5
} guest_messageStages_enum ;

class guestInstClass
{
    private:
    struct inst {

        /** Pointer to the previous / next host in the list */
        struct inst * prev;
        struct inst * next;

        /* Instance info */
        instInfo  instance ; 

        /**
         * Top level gate for the host.
         * If false then reporting for all instances are off.
         */
        // bool   reporting;

        bool   query_flag ;

        #define INST_TIMER_MONITOR   (0)
        #define INST_TIMER_CONNECT   (1)
        #define INST_TIMER_RECONNECT (2)
        #define INST_TIMER_INIT      (3)
        #define INST_TIMER_VOTE      (4)
        #define INST_TIMER_MAX       (5)

        /** General Purpose instance timer */
        // struct mtc_timer timer;
        struct mtc_timer      vote_timer;
        struct mtc_timer      init_timer;
        struct mtc_timer   monitor_timer;
        struct mtc_timer   connect_timer;
        struct mtc_timer reconnect_timer;


        guest_connectStages_enum connectStage ;
        guest_messageStages_enum messageStage ;
        guest_monitorStages_enum monitorStage ;

        guest_fsmActions_enum action ;

        int monitor_handler_count ;
        int message_handler_count ;
        int connect_handler_count ;
        int mismatch_count ;
        int heartbeat_count ;

        /* Message list for this instance*/
        list<struct json_object *> message_list ;
    };

    struct inst * inst_head ; /**< Inst Linked List Head pointer */
    struct inst * inst_tail ; /**< Inst Linked List Tail pointer */

   /** List of allocated host memory.
    *
    * An array of host pointers.
    */ 
    inst * inst_ptrs[MAX_HOSTS] ;

   /** A memory allocation counter.
    *
    * Should represent the number of hosts in the linked list.
    */
    int memory_allocs ;

    /** A memory used counter
    *  
    * A variable storing the accumulated instance memory
    */  
    int memory_used ;

    bool   fsm_exit ;
    void   fsm_run ( void );

    struct guestInstClass::inst* newInst ( void );
    struct guestInstClass::inst* addInst ( string uuid );
    struct guestInstClass::inst* getInst ( string uuid );
    int                          remInst ( string uuid );
    int                          delInst ( struct guestInstClass::inst * inst_ptr );
    void                        readInst ( void );

    void print_all_instances ( void );

    void mem_log_inst_info ( void );

    struct guestInstClass::inst* getInst_timer     ( timer_t tid, int timer_id );
   
    int message_handler ( struct guestInstClass::inst * inst_ptr );
    int connect_handler ( struct guestInstClass::inst * inst_ptr );
    int monitor_handler ( struct guestInstClass::inst * inst_ptr );

    void start_monitor_timer ( struct guestInstClass::inst * inst_ptr );

    /** Thus member function loops over all the insances and sends
     *  a json string instances: [uuid:state],[uuid:state]... 
     *  back to the guestAgent. */
    int guestAgent_qry_handler ( void );

    int send_challenge  ( struct guestInstClass::inst * inst_ptr );

    void manage_comm_loss ( void );

    void mem_log_info ( void );

    public:

     guestInstClass(); /**< constructor */	
    ~guestInstClass(); /**< destructor  */

    bool reporting ;
    void print_instances ( void );

    /** handle an expired timer */
    void timer_handler ( int sig, siginfo_t *si, void *uc);
        
    struct mtc_timer search_timer;

    int  instances ;
    void guest_fsm_run ( void );

    int        qry_inst ( void );
    int        add_inst ( string uuid, instInfo & instance );
    int        mod_inst ( string uuid, instInfo & instance );
    int        del_inst ( string uuid );
    instInfo * get_inst ( string uuid );
    
    ssize_t  write_inst ( instInfo * ptr, const char *message, size_t size);

    void   reconnect_start    ( const char * uuid_ptr ) ; // string uuid );

    void   set_query_flag     ( string uuid );
    bool   get_query_flag     ( string uuid );
    bool   get_reporting_state( string uuid );
    int    set_reporting_state( string uuid, bool enabled );

    int send_vote_notify ( string uuid );
    int send_vote_notify_resp ( char * hostname, string  uuid,
                                string notification_type,
                                string event_type,
                                string vote_result,
                                string reject_reason);

    void send_client_msg_nack ( instInfo * instInfo_ptr,
                                string log_err);
    void handle_parse_failure ( struct guestInstClass::inst * inst_ptr,
                                const char *key);

    /* Called on controlle daemon exit */
    void free_instance_resources ( void );
    void stop_instance_timers    ( struct guestInstClass::inst * inst_ptr );

    /* For select dispatch */ 
    struct timeval  waitd           ;

    fd_set          inotify_readfds ;
    fd_set         instance_readfds ;
    fd_set          message_readfds ;
    
    void memLogDelimit    ( void );              /**< Debug log delimiter    */
    void memDumpNodeState ( string uuid );
    void memDumpAllState  ( void );
    void print_node_info  ( void );              /**< Print node info banner */
};

guestInstClass * get_instInv_ptr ( void );

#endif /* __INCLUDE_INSTBASECLASS_H__ */
