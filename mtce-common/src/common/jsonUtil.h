#ifndef __INCLUDE_JSONUTIL_H__
#define __INCLUDE_JSONUTIL_H__
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
#include <list>
#include "json-c/json.h"

using namespace std;

/** Inventory json GET request load struct.
  *
  * Used to hold/load the parsed contents of the response
  * json string returned from an inventory HTTP GET request. */
typedef struct
{
   int                   elements ; /**< converted elements */
   /** An array of inventory information, one for each host */
   node_inv_type host[MAX_JSON_INV_GET_HOST_NUM];
   string                next ; /**< pointer to the next inventory element */
} jsonUtil_info_type ;

#define MTC_JSON_AUTH_TOKEN     "token"
#define MTC_JSON_AUTH_SVCCAT    "catalog"
#define MTC_JSON_AUTH_TYPE      "type" /**< looking for "compute" */
#define MTC_JSON_AUTH_ENDPOINTS "endpoints"
#define MTC_JSON_AUTH_URL       "url"
#define MTC_JSON_AUTH_ISSUE     "issued_at"
#define MTC_JSON_AUTH_EXPIRE    "expires_at"
#define MTC_JSON_AUTH_ID        "X-Subject-Token"
#define MTC_JSON_AUTH_PLATFORM  "platform"
#define MTC_JSON_AUTH_INTERFACE "interface" /** looking for admin */
#define MTC_JSON_AUTH_ADMIN     "admin"


/** Authroization info loaded from the authorization server */
typedef struct
{
    bool   updated ; /**< true if struct has been updated.                */
    int    status  ; /**< PASS or error code. token is only valid if PASS.*/
    string tokenid ; /**< The long encrypted toke.n                       */
    string issued  ; /**< The "issued_at": "<date-time>".                 */
    string expiry  ; /**< The "expires": "<date-time>".                   */
    string adminURL; /**< path to the nova server.                        */
} jsonUtil_auth_type ;

/** Module initialization interface. 
  */
void jsonUtil_init  ( jsonUtil_info_type & info );

/** Print the authroization struct to stdio. 
  */
void jsonUtil_print ( jsonUtil_info_type & info , int index );
void jsonUtil_print_inv ( node_inv_type & info );

int jsonUtil_get_key_val ( char   * json_str_ptr,
                              string   key, 
                              string & value );

int jsonUtil_get_key_val_int ( char   * json_str_ptr,
                               string   key,
                               int    & value );

/** Submit a request to get an authorization token and nova URL */
int jsonApi_auth_request ( string & hostname, string & payload );

/** Parse through the authorization request's response json string 
  * and load the relavent information into the passed in structure */
 int jsonUtil_inv_load ( char * json_str_ptr,
                            jsonUtil_info_type & info );

int jsonUtil_load_host ( char * json_str_ptr, node_inv_type & info );
int jsonUtil_load_host_state ( char * json_str_ptr, node_inv_type & info );

int jsonUtil_hwmon_info ( char * json_str_ptr, node_inv_type & info );

/** Handle the patch request response and verify execution status */
int jsonUtil_patch_load ( char * json_str_ptr, node_inv_type & info );

/** Tokenizes the json string and loads 'info' with the received token
  *
  * @param json_str_ptr
  *  to a json string
  * @param info
  *  is the updated jsonUtil_auth_type bucket containing the token
  *
  * @return execution status (PASS or FAIL)
  *
  *-  PASS indicates tokenization ok and info is updated.
  *-  FAIL indicates bad or error reply in json string.
  *
  */
int jsonApi_auth_load    ( string & hostname, char * json_str_ptr, 
                              jsonUtil_auth_type & info );


/***************************************************************************
 * This utility searches for an 'array_label' and then loops over the array
 * looking at each element for the specified 'search_key' and 'search_value'
 * Once found it searches that same element for the specified 'element_key'
 * and loads its value content into 'element_value' - what we're looking for 
 ***************************************************************************/
int jsonApi_array_value (   char * json_str_ptr, 
                          string   array_label,
                          string   search_key,
                          string   search_value,
                          string   element_key,
                          string & element_value);

/***********************************************************************
 * This utility updates the reference key_list with all the
 * values for the specified label.
 ***********************************************************************/
int jsonUtil_get_list   (   char * json_str_ptr, 
                          string   label, list<string> & key_list );

/***********************************************************************
 * This utility updates the reference element with the number of array
 * elements for the specified label in the provided string 
 ***********************************************************************/
int jsonUtil_array_elements ( char * json_str_ptr, string label, int & elements );

/***********************************************************************
 * This utility updates the reference string 'element' with the
 * contents of the specified labeled array element index. 
 ***********************************************************************/
int jsonUtil_get_array_idx  ( char * json_str_ptr, string label, int idx, string & element );

/***********************************************************************
* Escape special characters in JSON string
************************************************************************/
string jsonUtil_escapeSpecialChar(const string& input);

int    jsonUtil_get_key_value_int    ( struct json_object * obj, const char * key );
bool   jsonUtil_get_key_value_bool   ( struct json_object * obj, const char * key );
string jsonUtil_get_key_value_string ( struct json_object * obj, const char * key );

/***********************************************************************
 * Get JSON Integer Value from Key
 * return 0 if success, -1 if fail.
 ***********************************************************************/
int jsonUtil_get_int( struct json_object *jobj,
                      const char *key, void *value );

/***********************************************************************
 * Get JSON String Value from Key
 * return 0 if success, -1 if fail.
 ***********************************************************************/
int jsonUtil_get_string( struct json_object* jobj,
                         const char* key, string * value );

#endif /* __INCLUDE_JSONUTIL_H__ */
