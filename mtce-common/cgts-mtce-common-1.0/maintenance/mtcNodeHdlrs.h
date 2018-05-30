#ifndef __INCLUDE_MTCNODEHDLRS_H__
#define __INCLUDE_MTCNODEHDLRS_H__
/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance
  * 
  * JSON Utility Header
  */

#include <iostream>
#include <string>

using namespace std;

void mtcTimer_handler ( int sig, siginfo_t *si, void *uc);

#endif /* __INCLUDE_MTCNODEHDLRS_H__ */
