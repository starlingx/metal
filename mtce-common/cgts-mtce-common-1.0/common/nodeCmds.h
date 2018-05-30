#ifndef __INCLUDE_MTCCMDHDLR_HH__
#define __INCLUDE_MTCCMDHDLR_HH__
/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River CGTS Platform Maintenance Command Handler Header
  */
  
#include <iostream>
#include <string.h>

using namespace std;


#define MTC_OPER__NONE               0
#define MTC_OPER__MODIFY_HOSTNAME    1
#define MTC_OPER__RUN_IPMI_COMMAND   2
#define MTC_OPER__RESET_PROGRESSION  3
#define MTC_OPER__HOST_SERVICES_CMD  4

/* A set of command groupings that create sub-FSMs for 
 * specialized maintenance command operations */
typedef enum
{
    /* Common command dispatch stage */
    MTC_CMD_STAGE__START   = 0,

    /* Modify Hostname FSM Stages
     *
     * FSM that runs Nova and Neutron Delete then Create Operations
     * in support tof changing a hostname */
    MTC_CMD_STAGE__MODIFY_HOSTNAME_START,
    MTC_CMD_STAGE__MODIFY_HOSTNAME_DELETE_WAIT,
    MTC_CMD_STAGE__MODIFY_HOSTNAME_CREATE_WAIT,

    /* Reset Progression FSM Stages
     *
     * FSM that tries all possible avenues to reset/reboot a host */
    MTC_CMD_STAGE__RESET_PROGRESSION_START,
    MTC_CMD_STAGE__RESET,
    MTC_CMD_STAGE__RESET_ACK,
    MTC_CMD_STAGE__REBOOT,
    MTC_CMD_STAGE__REBOOT_ACK,
    MTC_CMD_STAGE__OFFLINE_CHECK,
    MTC_CMD_STAGE__IPMI_COMMAND_SEND,
    MTC_CMD_STAGE__IPMI_COMMAND_RECV,
    MTC_CMD_STAGE__RESET_PROGRESSION_RETRY,

    /* Manage Running a Host Services Start or Stop Command for host type */
    MTC_CMD_STAGE__HOST_SERVICES_SEND_CMD,
    MTC_CMD_STAGE__HOST_SERVICES_RECV_ACK,
    MTC_CMD_STAGE__HOST_SERVICES_WAIT_FOR_RESULT,

    /* Common command done stage */
    MTC_CMD_STAGE__DONE,
    MTC_CMD_STAGE__STAGES
} mtc_cmdStages_enum ;

typedef struct
{
    string             name   ;
    mtc_cmdStages_enum stage  ;
    unsigned int       seq    ;

    /* command and response info */
    unsigned int       cmd    ;
    unsigned int       rsp    ;
    unsigned int       ack    ;

    /* variable parms */
    unsigned int       parm1  ;
    unsigned int       parm2  ;

    /* controls */
    bool               task   ; /* send task updates */
    unsigned int       retry  ;

    /* execution status */
    int                status ;
    string             status_string ;
} mtcCmd ;

void mtcCmd_init ( mtcCmd & cmd );

#endif
