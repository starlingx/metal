#ifndef __INCLUDE_HOSTUTIL_H__
#define __INCLUDE_HOSTUTIL_H__

/*
* Copyright (c) 2013-2014, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/

#include <string.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <list>
#include <evhttp.h>          /* for ... HTTP_ status definitions */

using namespace std;

#include "nodeBase.h"

/* Supported Server Names */
//#define SERVER__UNKNOWN                ((const char*)"Undetermined Server")
//#define SERVER__NOKIA_QUANTA_1234_GEN1 ((const char*)"Quanta Computer")
//#define SERVER__HP_PROLIANT_DL380_GEN9 ((const char*)"ProLiant DL380 Gen9")
//#define SERVER__HP_PROLIANT_DL360_GEN9 ((const char*)"ProLiant DL360 Gen9")

/* Supported Board Management Controller Names */
//#define SERVER_BMC__UNKNOWN            ((const char*)"Unknown BMC")
//#define SERVER_BMC__STANDARD_ILO_V3    ((const char*)"iLO 3 Standard")
//#define SERVER_BMC__STANDARD_ILO_V4    ((const char*)"iLO 4 Standard")


/* A list of supported servers */
//typedef enum
//{
//    SERVER_IS_UNKNOWN = 0,
//    SERVER_IS_NOKIA__QUANTA_1234____GEN1__ILO_V4 = 1,
//    SERVER_IS_HP_____PROLIANT_DL380_GEN9__ILO_V4 = 2,
//    SERVER_IS_HP_____PROLIANT_DL360_GEN9__ILO_V4 = 3,
//    SERVER_IS_LAST                               = 4
//} server_enum ;

/* Server Table Entry Type */
//typedef struct
//{
//    server_enum     server_code ;
//    protocol_enum   protocol    ;
//    const char    * server_name ;
//    const char    * server_bmc  ;
//    const char    * profile     ;
//
//} server_table_entry_type ;
//server_table_entry_type * hostUtil_get_server_info ( server_enum server_code );

typedef enum
{
    CLIENT_NONE         = 0,
    CLIENT_SYSINV       = 1,
    CLIENT_VIM_HOSTS    = 2,
    CLIENT_VIM_SYSTEMS  = 3,
    CLIENT_SENSORS      = 4,
    CLIENT_SENSORGROUPS = 5,
    CLIENT_SM           = 6,
} mtc_client_enum ;

typedef enum
{
    SERVICE_SYSINV = 0,
    SERVICE_TOKEN  = 1,
    SERVICE_SMGR   = 2,
    SERVICE_VIM    = 3,
    SERVICE_SECRET = 4,
} mtc_service_enum ;

string hostUtil_getServiceIp   ( mtc_service_enum service );
int    hostUtil_getServicePort ( mtc_service_enum service );
string hostUtil_getPrefixPath  ( void );

bool hostUtil_is_valid_uuid    ( string uuid );
bool hostUtil_is_valid_ip_addr ( string ip );
bool hostUtil_is_valid_bm_type ( string bm_type );

int  hostUtil_mktmpfile ( string hostname, string basename, string & filename, string data );

#endif
