#ifndef __INCLUDE_HWMONJSON_H__
#define __INCLUDE_HWMONJSON_H__
/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Hardware Monitor Json Utilities Imp
  * 
  * JSON Utility Header
  */

#include <iostream>

using namespace std;

int hwmonJson_load_inv ( char * json_str_ptr, node_inv_type & info );

#endif /* __INCLUDE_HWMONJSON_H__ */
