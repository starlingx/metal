#ifndef __INCLUDE_MTCTHREAD_HH__
#define __INCLUDE_MTCTHREAD_HH__

/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance "Thread Header"
 * Header and Maintenance API
 */

typedef struct
{
    string bm_ip ;
    string bm_un ;
    string bm_pw ;
    string bm_cmd ;
} thread_extra_info_type ;

#define MTCAGENT_STACK_SIZE (0x20000) // 128 kBytes

void * mtcThread_bmc ( void * );
void * mtcThread_bmc_test ( void * arg );

#endif // __INCLUDE_MTCTHREAD_HH__
