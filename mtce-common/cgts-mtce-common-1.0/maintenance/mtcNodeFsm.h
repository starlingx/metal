#ifndef __INCLUDE_MTCNODEFSM_HH__
#define __INCLUDE_MTCNODEFSM_HH__
/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGTS Platform Node Maintenance "Finite State Machine"
 * Interface, Types and Definitions.
 */

#include <iostream>
#include <string.h>

using namespace std;

#include "nodeClass.h"

/** Maintenance FSM Testing Support
 *
 * The test head sets test mode, provisions a test node and
 * then proceeds to setup the node's x.731 states and calls
 * the fsm to run against that node. The FSM and handlers 
 * are is coded with test case clauses that transition the
 * fsmtest word
 * test
 */

/** Maintenance FSM test head interface */
int mtcNodeFsm_testhead ( nodeLinkClass * obj_ptr );

int mtc_fsm_run ( nodeLinkClass * obj_ptr );

#endif /* __INCLUDE_MTCNODEFSM_HH__ */
