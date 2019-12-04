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
bool hostUtil_is_valid_username ( string un );
bool hostUtil_is_valid_bm_type ( string bm_type );

int  hostUtil_mktmpfile ( string hostname, string basename, string & filename, string data );

#endif
