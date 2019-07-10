/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Maintenance Main Implementation
  */ 
 
#include <iostream>
#include <getopt.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>  
#include <sys/stat.h>  
#include <signal.h>
#include <syslog.h>
  
#define EXIT_SUCCESS 0  
#define EXIT_FAILURE 1  

using namespace std;

bool testing = false ;

/** Feature Utility Includes */
#include "daemon_option.h"
#include "daemon_common.h"
#include "nodeBase.h"
#include "nodeUtil.h"         /* for ... mem_log_list_init */

/** 
 * Cache a copy of the current hostname.  
 * Use these set/get interfaces to set and retrieve it
 */
static char this_hostname [MAX_CHARS_HOSTNAME+2];

char * _hn ( void ) 
{   
    return(&this_hostname[0]);
}

void set_hn ( char * hn )
{
    if ( hn )
        snprintf ( &this_hostname[0], MAX_CHARS_HOSTNAME+1, "%s", hn );
    else
        snprintf ( &this_hostname[0], MAX_CHARS_HOSTNAME+1, "%s", "localhost" );
}

static opts_type opts ; /**< The allocated memory for Daemon run options */

opts_type * daemon_get_opts_ptr ( void )
{
    return(&opts);
}

bool ltc ( void )
{
    return(opts.front);
}

void print_help ( void )
{
   printf ("\nUsage: <daemon> options ...\n");
   printf ("\t-h --help             -  Display this usage information\n");
   printf ("\t-a --active           -  Specify service as Active\n");
   printf ("\t-d --debug <0..15>    -  Enter specified debug level\n");
   printf ("\t-f --foreground       -  Run in foreground\n");
   printf ("\t-l --log              -  Log to file ; /var/log/<daemon>.log\n");
   printf ("\t-p --passive          -  Passive mode ; do not act on failures\n");
   printf ("\t-v --verbose          -  Show command line arguments\n");
   printf ("\t-V --Virtual          -  Running in virtual environment\n");
   printf ("\t-t --test             -  Run Test Head\n");
   printf ("\t-g --gap              -  Gap in seconds\n");
   printf ("\t-m --mode             -  Word string representing a run mode\n");
   printf ("\t-n --number           -  A number\n");
   printf ("\t-p --password <pw>    -  Password\n");
   printf ("\t-c --command <cmd>    -  Command\n");
   printf ("\t-u --username <un>    -  Username\n");
   printf ("\t-i --ipaddr  <ip>     -  Ipaddr\n");
   printf ("\n" );
   exit (0);
}

int daemon_get_run_option ( const char * option )
{
    if ( !strcmp ( option, "test" ) )
        return opts.test ; 
    else if ( !strcmp ( option, "active" ) )
        return opts.active ;
    else if ( !strcmp ( option, "log" ) )
    {
        /* no logging in foreground mode 
         * but always log otherwise */
        if ( opts.front )
        {
            return opts.log ;
        }
        return (1);
    }
    else if ( !strcmp ( option, "Virtual" ) )
        return opts.Virtual ;

    else if ( !strcmp ( option, "front" ) )
        return opts.front ;

    return 0 ;        
}

void opts_init ( void)
{
    opts.help    = false ;  
    opts.log     = false ; 
    opts.test    = false ;
    opts.info    = false ;
    opts.verbose = false ;
    opts.Virtual = false ;
    opts.active  = false ;
    opts.debug   = 0     ;
    opts.front   = false ;
    opts.command = ""    ;
    opts.username= ""    ;
    opts.password= ""    ;
    opts.ipaddr  = ""    ;
    opts.mode    = ""    ;
    opts.number  = 0     ;
    opts.delay   = 0     ;
}

int daemon_set_cfg_option ( const char * option , int value )
{
    int rc = PASS ;
    
    daemon_config_type * cfg_ptr = daemon_get_cfg_ptr ();
    
    if ( !strcmp ( option, "active" ) )
        cfg_ptr->active = value ;
    else
        rc = FAIL ;
    
    return (rc);
}
 
 
int parseArg ( int argc, char * argv[], opts_type * opts_ptr )
{
    int arg_count     = 0 ;
    int next_option   = 0 ;
    int cmd_arg_count = 1 ; /* command args start at 1 */

   /* A string listing of valid short options letters. */
   const char* const short_options = "u:c:p:g:i:m:n:d:hlfpvVta";

   /* An array listing of valid long options. */
   const struct option long_options[] =
   {
         { "debug"     , 1, NULL, 'd' },
         { "gap"       , 1, NULL, 'g' },
         { "mode"      , 1, NULL, 'm' },
         { "number"    , 1, NULL, 'n' },
         { "ipaddr"    , 1, NULL, 'i' },
         { "command"   , 1, NULL, 'c' },
         { "password"  , 1, NULL, 'p' },
         { "username"  , 1, NULL, 'u' },
         { "help"      , 0, NULL, 'h' },
         { "active"    , 0, NULL, 'a' },
         { "foreground", 0, NULL, 'f' },
         { "log"       , 0, NULL, 'l' },
         { "verbose"   , 0, NULL, 'v' },
         { "Virtual"   , 0, NULL, 'V' },
         { "test"      , 0, NULL, 't' },
         {  NULL       , 0, NULL,  0  } /* Required at end of array. */
   };

   do
   {
      next_option = getopt_long (argc, argv, short_options, long_options, NULL);
      arg_count++ ;
      switch (next_option)
      {
         case -1: /* Done with options */
         {
            break ;
         }
         case 'f': /* -f or --foreground */
         {
             opts_ptr->front = true ;
             cmd_arg_count++ ;
             break ;
         }
         case 'g': /* -g or --gap */
         {
             opts_ptr->delay = atoi(optarg) ;
             cmd_arg_count++ ;
             break;
         }

         case 'l': /* -l or --log */
         {
             opts_ptr->log = true ;
             cmd_arg_count++ ;
             break ;
         }
         case 'h': /* -h or --help */
         {
             opts_ptr->help = true ;
             cmd_arg_count++ ;
             return ( PASS ) ;
         }
         case 'd': /* -p or --debug */
         {
             opts_ptr->debug = atoi(optarg) ;
             cmd_arg_count++ ;
             break;
         }
         case 'm': /* -u or --mode */
         {
             opts_ptr->mode = optarg ;
             cmd_arg_count++ ;
             break;
         }
         case 'n': /* -p or --number */
         {
             opts_ptr->number = atoi(optarg) ;
             cmd_arg_count++ ;
             break;
         }
         case 'p': /* -p or --password */
         {
             opts_ptr->password = optarg ;
             cmd_arg_count++ ;
             break;
         }
         case 'u': /* -u or --username */
         {
             opts_ptr->username = optarg ;
             cmd_arg_count++ ;
             break;
         }
         case 'c': /* -c or --command */
         {
             opts_ptr->command = optarg ;
             cmd_arg_count++ ;
             break;
         }
         case 'i': /* -i or --ipaddr */
         {
             opts_ptr->ipaddr = optarg;
             cmd_arg_count++ ;
             break;
         }
         case 't': /* -t or --test */
         {
             opts_ptr->test = true ;
             cmd_arg_count++ ;
             break;
         }
         case 'v': /* -v or --verbose */
         {
             opts_ptr->verbose = true ;
             cmd_arg_count++ ;
             break;
         }
         case 'V': /* -V or --Virtual */
         {
             opts_ptr->Virtual = true ;
             cmd_arg_count++ ;
             break;
         }
         case 'a': /* -a or --active */
         {
             opts_ptr->active = true ;
             cmd_arg_count++ ;
             break;
         }
         case '?':
         default: /* Something else: unexpected */
         {
             printf ("Unsupported option (%c)\n", next_option ); 
             opts_ptr->help = true ;
             return ( cmd_arg_count );
         }
      }
   } while (next_option != -1);

   if (opts_ptr->verbose)
   {
      int i ;
      
      for ( i = 0 ; i < argc; ++i)
         printf ("Arg [%d]: %s\n", i, argv[i]);
      
      printf ("\n");
   }
   return ( cmd_arg_count ) ;
}


static void daemonize(void)  
{  
    pid_t pid, sid;
    int fd;
  
    /* already a daemon */  
    if ( getppid() == 1 )
        return;
  
    /* Fork off the parent process */  
    pid = fork();
    if (pid < 0)
    { 
        exit(EXIT_FAILURE);  
    } 
  
    if (pid > 0)
    {     
        exit(EXIT_SUCCESS); /*Killing the Parent Process*/  
    }     
  
    /* At this point we are executing as the child process */  
  
    /* Create a new SID for the child process */  
    sid = setsid();
    if (sid < 0)
    {
        exit(EXIT_FAILURE); 
    }
  
    /* Change the current working directory. */  
    if ((chdir("/")) < 0)
    {
        exit(EXIT_FAILURE);
    }
  
    fd = open("/dev/null",O_RDWR, 0);  
  
    if (fd != -1)  
    {  
        dup2 (fd, STDIN_FILENO);  
        dup2 (fd, STDOUT_FILENO);  
        dup2 (fd, STDERR_FILENO);  
  
        if (fd > 2)  
        {  
            close (fd);  
        }  
    }  
  
    /* File Creation Mask */
    umask(022);
}

int main(int argc, char *argv[])
{
   int rc = FAIL ;

   set_hn (NULL);

   /* Manually Zero the main service structs */
   opts_init ();

   /* Parse the argument list */
   parseArg ( argc, argv, &opts );
 
   if ( opts.help )
   {
       print_help ( );
       exit (0) ;
   }

   /* lets turn this process into an independent daemon */
   if ( opts.front != true )
   {
       daemonize ();
   }

   daemon_health_test ();
   daemon_create_pidfile ();
   if ( !opts.front )
   {
       open_syslog();
   }

   mem_log_list_init ( );

   /* Init the daemon config structure */
   daemon_config_default ( daemon_get_cfg_ptr() );

   /* get the management interface */
   string iface = daemon_mgmnt_iface ();

   /* get the node type */
   string nodet = daemon_nodetype ();

   rc = daemon_init ( iface, nodet );
   if ( rc != PASS )
   {
       elog ("Initialization failed (rc=%d)\n", rc);
       rc = FAIL_DAEMON_INIT ;
   }
   else if ( rc == PASS )
   {
       if ( opts.username.size() )
       {
           ilog ("Username    : %s\n", opts.username.c_str());
       }
       if ( opts.ipaddr.size())
       {
           ilog ("IP Addr     : %s\n", opts.ipaddr.c_str());
       }
       if ( opts.password.size())
       {
           ilog ("Password    : %s\n", opts.password.c_str());
       }
       if ( opts.command.size())
       {
           ilog ("Command     : %s\n", opts.command.c_str());
       }

       if ( opts.test )
       {
           printf ("Enabling Test Mode\n");
           testing = true ;
       }
       ilog ("Build Date  : %s\n", BUILDINFO);
       ilog ("------------------------------------------------------\n");

       /* Call the test head if test mode is selected.
        * Otherwise call the main service. */
        if ( opts.test )
            daemon_run_testhead ( );
        else
        {
            daemon_load_fit ();
            daemon_service_run ( );
        }
   }
   exit (rc) ;
}
