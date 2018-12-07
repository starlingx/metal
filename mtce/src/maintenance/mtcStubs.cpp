/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Maintenance Agent Stubs
  */

using namespace std;

#include "nodeClass.h"     /* The main link class                        */

void hbs_cluster_log ( string & hostname, string prefix, bool force=false )
{
    UNUSED(hostname);
    UNUSED(prefix);
    UNUSED(force);
}

void hbs_cluster_change ( string reason )
{
    UNUSED(reason);
}
