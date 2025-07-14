#ifndef __INCLUDE_NODETIMERS_HH__
#define __INCLUDE_NODETIMERS_HH__

/*
 * Copyright (c) 2013-2023 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance "Timer Facility"
 * Header and Maintenance API
 */

/**
 * @detail
 * Detailed description ...
 *
 * Common timer struct
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>

#define MAX_TIMER_DURATION (30000)

#define MTC_SECS_1    (1)
#define MTC_SECS_2    (2)
#define MTC_SECS_5    (5)
#define MTC_SECS_10 ( 10)
#define MTC_SECS_15 ( 15)
#define MTC_SECS_20 ( 20)
#define MTC_SECS_30 ( 30)

#define MTC_MINS_1  ( 60)
#define MTC_MINS_2  (120)
#define MTC_MINS_3  (180)
#define MTC_MINS_4  (240)
#define MTC_MINS_5  (300)
#define MTC_MINS_8  (480)
#define MTC_MINS_10 (600)
#define MTC_MINS_14 (840)
#define MTC_MINS_15 (900)
#define MTC_MINS_20 (1200)
#define MTC_MINS_30 (1800)
#define MTC_MINS_40 (2400)
#define MTC_HRS_1   (3600)
#define MTC_HRS_4   (14400)
#define MTC_HRS_8   (28800) /* old token refresh rate */

#define HOST_MTCALIVE_TIMEOUT         (MTC_MINS_20)
#define HOST_GOENABLED_TIMEOUT        (MTC_MINS_2)
#define MTC_CMD_RSP_TIMEOUT           (10)
#define MTC_FORCE_LOCK_RESET_WAIT     (30)
#define MTC_RECOVERY_TIMEOUT          (16)
#define MTC_PMOND_READY_TIMEOUT       (10)
#define MTC_UPTIME_REFRESH_TIMER      (MTC_MINS_1) /* If this interval changes review impact
                                                      to garbage collecton in mtctimer_handler */
#define MTC_MNFA_RECOVERY_TIMER        (3)
#define MTC_ALIVE_TIMER               (5)
#define MTC_POWEROFF_DELAY            (5)
#define MTC_SWACT_POLL_TIMER         (10)
#define MTC_TASK_UPDATE_DELAY        (30)
#define MTC_BM_PING_TIMEOUT          (30)
#define MTC_BM_POWEROFF_TIMEOUT      (60)
#define MTC_BM_POWERON_TIMEOUT       (30)
#define MTC_RESET_PROG_TIMEOUT       (20)
#define MTC_WORKQUEUE_TIMEOUT        (60)
#define MTC_WORKER_CONFIG_TIMEOUT    (MTC_MINS_14)
#define MTC_RESET_PROG_OFFLINE_TIMEOUT   (20)
#define MTC_RESET_TO_OFFLINE_TIMEOUT    (150)
#define MTC_POWEROFF_TO_OFFLINE_TIMEOUT (200)
#define MTC_POWERON_TO_ONLINE_TIMEOUT   (900)
#define MTC_POWERCYCLE_COOLDOWN_DELAY  (MTC_MINS_5)
#define MTC_POWERCYCLE_BACK2BACK_DELAY (MTC_MINS_5)
#define MTC_HEARTBEAT_SOAK_BEFORE_ENABLE (11)
#define MTC_HEARTBEAT_SOAK_DURING_ADD    (10)
#define MTC_REINSTALL_TIMEOUT_DEFAULT  (MTC_MINS_40)
#define MTC_REINSTALL_TIMEOUT_BMC_ACC  (MTC_MINS_10)
#define MTC_REINSTALL_TIMEOUT_MIN      (MTC_MINS_1)
#define MTC_REINSTALL_TIMEOUT_MAX      (MTC_HRS_4)
#define MTC_REINSTALL_WAIT_TIMER       (10)
#define MTC_BMC_REQUEST_DELAY     (10) /* consider making this shorter */
#define LAZY_REBOOT_RETRY_DELAY_SECS   (60)
#define SM_NOTIFY_UNHEALTHY_DELAY_SECS  (5)
#define MTC_MIN_ONLINE_PERIOD_SECS      (7)
#define MTC_RETRY_WAIT                  (5)
#define MTC_FIRST_WAIT                  (3)
#define MTC_AGENT_TIMEOUT_EXTENSION     (5)
#define MTC_LOCK_CEPH_DELAY             (90)

#define MTC_RECV_RETRY_WAIT (MTC_RETRY_WAIT)
#define MTC_RECV_WAIT       (MTC_RETRY_WAIT)

/** Host must stay enabled for this long for the
 *  failed_recovery_counter to get cleared */
#define MTC_ENABLED_TIMER              (5)

/** Should be same or lower but not less than half of ALIVE_TIMER */
#define MTC_OFFLINE_TIMER              (7)

#define TIMER_INIT_SIGNATURE (0x86752413)

struct mtc_timer
{
                                /** linux timer structs                  */
   struct sigevent   sev      ; /**< set by util - time event specifier  */
   struct itimerspec value    ; /**< set by util - time values           */
   struct sigaction  sa       ; /**< set by util and create parm handler */

                                /** local service members                */
   unsigned int      init     ; /** timer initialized signatur           */
          timer_t    tid      ; /**< the timer address pointer           */
          bool       active   ; /**< indicates that the timer is active  */
          bool       mutex    ;
          bool       error    ;
          int       _guard    ;
          bool       ring     ; /**< set to true if the timer fires      */
          int        guard_   ;
          int        secs     ; /**< set by create parm - sub second not supported */
          int        msec     ; /**< set by create parm - sub second not supported */
          string     hostname ; /**< name of the host using the timer    */
          string     service  ; /**< name of the service using the timer */
} ;

void mtcTimer_mem_log ( void );

void mtcTimer_init ( struct mtc_timer & mtcTimer );
void mtcTimer_init ( struct mtc_timer & mtcTimer, string hostname );
void mtcTimer_init ( struct mtc_timer & mtcTimer, string hostname, string service );
void mtcTimer_init ( struct mtc_timer * mtcTimer_ptr );
void mtcTimer_init ( struct mtc_timer * mtcTimer_ptr, string hostname, string service );

int mtcTimer_start ( struct mtc_timer & mtcTimer,
                     void (*handler)(int, siginfo_t*, void*),
                     int seconds );

int mtcTimer_start ( struct mtc_timer * mtcTimer_ptr,
                     void (*handler)(int, siginfo_t*, void*),
                     int seconds );

int mtcTimer_start_msec ( struct mtc_timer & mtcTimer,
                          void (*handler)(int, siginfo_t*, void*),
                          int msec );

int mtcTimer_start_msec ( struct mtc_timer * mtcTimer_ptr,
                          void (*handler)(int, siginfo_t*, void*),
                          int msec );

int mtcTimer_start_sec_msec ( struct mtc_timer * mtcTimer_ptr,
                              void (*handler)(int, siginfo_t*, void*),
                              int secs , int msec );

int mtcTimer_stop  ( struct mtc_timer & mtc_timer );
int mtcTimer_stop  ( struct mtc_timer * mtcTimer_ptr );
int mtcTimer_stop_int_safe ( struct mtc_timer & mtcTimer );
int mtcTimer_stop_int_safe ( struct mtc_timer * mtcTimer_ptr );

void mtcTimer_dump_data ( void );

/** Cleanup interface - stop and delete an unknown timer */
int mtcTimer_stop_tid          ( timer_t * tid_ptr );
int mtcTimer_stop_tid_int_safe ( timer_t * tid_ptr );

/* returns true if the timer is not active or ring is true */
bool mtcTimer_expired ( struct mtc_timer & mtcTimer );
bool mtcTimer_expired ( struct mtc_timer * mtcTimer_ptr );

/* stops timer if tid is active running */
void mtcTimer_reset ( struct mtc_timer & mtcTimer );
void mtcTimer_reset ( struct mtc_timer * mtcTimer_ptr );

/* de-init a user timer */
void mtcTimer_fini ( struct mtc_timer & mtcTimer );
void mtcTimer_fini ( struct mtc_timer * mtcTimer_ptr );

void mtcWait_msecs ( int millisecs );
void mtcWait_secs  ( int secs );

int mtcTimer_testhead ( void );

#endif
