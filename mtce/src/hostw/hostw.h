/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/*
 * This implements the CGCS Host Watchdog ; /usr/local/bin/hostwd
 *
 * Call trace is as follows:
 *    daemon_init
 *        daemon_files_init
 *        daemon_signal_init
 *        daemon_configure
 *            ini_parse
 *                hostw_process_config
 *        socket_init
 *            hostw_socket_init
 *
 *    daemon_service_run
 *        hostw_service
 *            _forever
 *                hostw_service_command
 *                kernel_watchdog_pet
 *
 *
 * This daemon waits for a "goenabled" signal and then
 *   - starts the kernel watchdog (to insure against death of this process)
 *   - regularly pets the watchdog
 *   - expects regular updates from PMON indicating "sane" status
 *       - PMON is configured via pmond.config to send updates at certain
 *         intervals
 *       - host watchdog reads PMON config file to determine the expected
 *         interval, but allows for some flexability (HOSTW_UPDATE_TOLERANCE)
 *         to allow for process scheduling, etc
 *   - will log and reboot if PMON dies or if PMON reports system is not right
 *
 */
 /**
  * @file
  * Wind River CGCS Host Watchdog Service Header
  */

#include <linux/un.h>
#include <sys/ioctl.h>
#include <errno.h>

using namespace std;

#include "nodeBase.h"
#include "daemon_ini.h"    /* Ini Parser Header                        */
#include "daemon_common.h" /* Common definitions and types for daemons */
#include "daemon_option.h" /* Common options  for daemons              */
#include "nodeTimers.h"    /* maintenance timer utilities start/stop   */
#include "nodeUtil.h"      /* common utilities */
#include "hostwMsg.h"      /* message format */

/* Configuration Files */
#define HOSTWD_CONFIG_FILE   ((const char *)"/etc/mtc/hostwd.conf")
#define PMOND_CONFIG_FILE    ((const char *)"/etc/mtc/pmond.conf")

#define HOSTW_MIN_KERN_UPDATE_PERIOD  60 /* user can set how long until kernel
                                          * watchdog panics, down to this
                                          * minimum (seconds) */

/* Daemon Config Bit Masks */
#define CONFIG_HOSTWD_FAILURE_THRESHOLD 0x01
#define CONFIG_HOSTWD_REBOOT            0x02
#define CONFIG_HOSTWD_USE_KERN_WD       0x04
#define CONFIG_HOSTWD_CONSOLE_PATH      0x10
#define CONFIG_HOSTWD_UPDATE_PERIOD     0x40
#define CONFIG_KERNWD_UPDATE_PERIOD     0x80


/** Daemon Config Mask */
#define CONFIG_MASK   (CONFIG_HOSTWD_FAILURE_THRESHOLD |\
                       CONFIG_HOSTWD_REBOOT            |\
                       CONFIG_HOSTWD_USE_KERN_WD       |\
                       CONFIG_HOSTWD_CONSOLE_PATH      |\
                       CONFIG_HOSTWD_UPDATE_PERIOD     |\
                       CONFIG_KERNWD_UPDATE_PERIOD)

#define PIPE_COMMAND_RESPON_LEN      100 /* max pipe command rsponse length */

#define GRACEFUL_REBOOT_DELAY         60 /* how many seconds to wait for logger
                                          * to finish before we start reboot */

#define FORCE_REBOOT_DELAY           300 /* how many seconds to wait for logger
                                          * and graceful reboot to finish before
                                          * we give up and force reboot */

/* Context control structure */
typedef struct
{
    /* Watchdog interface                                                    */
    /* ------------------                                                    */
    int watchdog           ; /** The opened /dev/watchdog file               */

    /* Loop counters                                                         */
    /* ------------------                                                    */
    int pmon_grace_loops   ; /** Messages we allow pmon to miss before panic */
    int process_grace_loops; /** Number of consecutive "something is wrong"  */
                             /*     messages we allow before panic           */
    struct sigaction info  ; /**< This daemon signal action struct  */
    struct sigaction prev  ; /**< Action handler that was replaced  */
                             /**< This is put back on the exit      */

} hostw_ctrl_type ;

/** Daemon Service messaging socket control structure */
typedef struct
{
    /** Unix socket used to listen for status updates */
    int                 status_sock; /**< Tx Event Socket            */
    struct sockaddr_un  status_addr; /**< Address to use for unix socket */
    fd_set readfds;
} hostw_socket_type ;

/* functions called between files */

hostw_ctrl_type   * get_ctrl_ptr(void);
hostw_socket_type * hostw_getSock_ptr(void);
void                hostw_service(void);
void                hostw_log_and_reboot(void);
int                 hostw_socket_init(void);
void                kernel_watchdog_pet(void) ;


