#ifndef __INCLUDE_REGEXUTIL_H__
#define __INCLUDE_REGEXUTIL_H__

/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River - Titanium Cloud - Regex Utilty Module Header
  */

bool regexUtil_pattern_match ( std::string pattern , std::string rule, int type );
bool regexUtil_label_match   ( string hostname, string pattern, string rule );
bool regexUtil_string_startswith ( const char *original, const char *substring );
#endif
