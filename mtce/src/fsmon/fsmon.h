/*
 * Copyright (c) 2014-2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
/*
 * This implements the CGCS process Monitor ; /usr/local/bin/pmond
 *
 * Call trace is as follows:
 *    daemon_init
 *        daemon_files_init
 *        daemon_signal_init
 *        daemon_configure
 *            ini_parse
 *            get_debug_options
 *        
 *    daemon_service_run
 *          _forever
 */

 /**
  * @file
  * Wind River CGCS Platform File System Monitor Service Header
  */

#include <iostream>
#include <string.h>
#include <stdio.h>
#include <signal.h>        /* for .. signaling                */
#include <unistd.h>        /* for .. close and usleep         */
#include <stdlib.h>        /* for .. system                   */
#include <dirent.h>        /* for config dir reading          */
#include <list>            /* for the list of conf file names */
#include <syslog.h>        /* for ... syslog                  */
#include <sys/wait.h>      /* for ... waitpid                 */
#include <time.h>          /* for ... time                    */
#include <sys/prctl.h>     /* for program control header      */
#include <sys/types.h>     /*                                 */
#include <sys/socket.h>    /* for ... socket                  */
#include <sys/un.h>        /* for ... domain socket type      */
#include <netinet/in.h>    /* for ... UDP socket type         */
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>         /* for hostent                      */
#include <errno.h>
#include <sys/stat.h>

using namespace std;

#include "nodeBase.h"
#include "daemon_ini.h"    /* Ini Parser Header                        */
#include "daemon_common.h" /* Common definitions and types for daemons */
#include "daemon_option.h" /* Common options  for daemons              */
#include "nodeTimers.h"    /* maintenance timer utilities start/stop   */
#include "nodeUtil.h"      /* common utilities */

/**
 * @addtogroup fsmon_base
 * @{
 */

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "fsm"

#define CONFIG_AUDIT_PERIOD 1

#define CONFIG_MASK CONFIG_AUDIT_PERIOD


void fsmon_service ( unsigned int nodetype );

/**
 * @} fsmon_base
 */
