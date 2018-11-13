#if !defined(_MTC_DAEMON_H__)
#define      _MTC_DAEMON_H__
/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
/**
 * @file
 * Wind River CGTS Platform Maintenance Main Implementation
 */ 

/**
 * @addtogroup daemon_main
 * @{
 */


#include <iostream>
#include <string>

using namespace std ;

/**
 * Daemon development run options structure
 */
typedef struct
{
   int help    ; /**< Display daemon options help          */
   int log     ; /**< Request log to file                  */
   int test    ; /**< Enable test mode                     */
   int info    ; /**< Dump data module info                */
   int verbose ; /**< Dump command line options            */
   int Virtual ; /**< Set to non-zero when in virtual env  */
   int active  ; /**< Set daemon active                    */
   int debug   ; /**< Set tracing debug mode "debug,"test","info","trace" */
   int front   ; /**< run in the foreground ; do not daemonize */
   int    number   ; /**< a number option - loops */
   int    delay  ; /**< a number option - loops */
   string mode     ; /**< specify a mode as a string ' i.e. shell mode*/
   string ipaddr   ;
   string username ;
   string command  ;
   string password ;
}  opts_type   ;

opts_type * daemon_get_opts_ptr ( void );

/** Returns the value of a specified run option
 *
 * @param option
 *  pointer to a run option string ; debug, test, info, trace
 * @return
 *  the run option value
 */
int daemon_get_run_option ( const char * option );


/**
 * @} daemon_main
 */

#endif
