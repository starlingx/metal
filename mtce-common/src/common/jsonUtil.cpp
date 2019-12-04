/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance JSON Utilities
  */

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <list>
#include <json-c/json.h>      /* for ... json-c json string parsing */
#include <sstream>

using namespace std;

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "jsn"

#include "nodeUtil.h"
#include "jsonUtil.h"    /* JSON Utilities      */

/* Internal Private Interfaces */
static struct json_object * _json_verify_object        ( struct json_object * obj,
                                                         const char * label);
static struct json_object * _json_get_host_next        ( struct json_object * obj );
static struct json_object * _json_object_array_get_idx ( struct json_object * obj,
                                                         int index );
static                int   _json_get_object_number    ( struct json_object * obj );
static             string   _json_get_key_value_string ( struct json_object * obj,
                                                         const char * key );

/* init one element of the struct */
void jsonUtil_init (  jsonUtil_info_type & info , int index )
{
    node_inv_init ( info.host[index] ) ;
}

/* init the entire struct */
void jsonUtil_init ( jsonUtil_info_type & info )
{
    info.elements = 0 ;
    for ( int i = 0 ; i < MAX_JSON_INV_GET_HOST_NUM ; i++ )
    {
       jsonUtil_init ( info , i ) ;
    }
    info.next = "" ;
}

/* Validate the supplied label is in the specified object */
static struct json_object * _json_verify_object ( struct json_object * obj,
                                                  const char * label )
{
    struct json_object * req_obj = (struct json_object *)(NULL);

    json_bool status = json_object_object_get_ex (obj, label, &req_obj);
    if (( status == TRUE ) && ( req_obj ))
    {
        return (req_obj);
    }
    wlog ("Specified label '%s' not found in response\n", label );
    status = json_object_object_get_ex (obj, "error", &req_obj );
    if (( status == TRUE ) && ( req_obj ))
    {
        jlog ("Found 'error' label instead\n");
    }
    else
    {
        jlog ("Neither specified nor error label found in object\n");
    }
    return ((struct json_object *)(NULL)) ;
}

static struct
json_object * _json_get_host_next ( struct json_object * obj )
{
    /* Get the next host entity path */
    struct json_object * next_obj =  (struct json_object *)(NULL);
    json_bool status = json_object_object_get_ex(obj, MTC_JSON_INV_NEXT, &next_obj );
    if (( status == TRUE ) && ( next_obj ))
    {
        return (next_obj);
    }
    else
    {
        return ((struct json_object *)(NULL)) ;
    }
}


/* Get the json host info object */
static struct
json_object * _json_object_array_get_idx ( struct json_object * obj, int index )
{
    /* Get the json host array list ; there should be one since
     * we read inventory one host at a time */
    struct array_list *array_list_obj = json_object_get_array(obj);
    if ( array_list_obj )
    {
        int len = array_list_length (array_list_obj);
        if ( len == 0 )
        {
           ilog ( "No provisioned hosts\n");
        }
        else if ( index < len )
        {
            struct json_object *node_obj ;
            node_obj = json_object_array_get_idx (obj, index );
            if ( node_obj )
            {
                 return ( node_obj );
            }
            else
            {
                 elog ("No json host info object\n");
            }
        }
        else
        {
            elog ("No json host for requested index\n");
        }
    }
    else
    {
        elog ("No json host array list\n");
    }

    return ((struct json_object *)(NULL)) ;
}

/* return the number of array objects in the specified object */
static int _json_get_object_number ( struct json_object * obj )
{
    /* Get number of elements in the json host array list */
    struct array_list *array_list_obj = json_object_get_array(obj);
    if ( array_list_obj )
    {
        return ( array_list_length (array_list_obj));
    }
    return (0);
}

string _json_get_key_value_string ( struct json_object * obj, const char * key )
{
    std::string value = "" ;

    /* Get the node uuid */
    struct json_object * key_obj = (struct json_object *)(NULL);
    json_bool status = json_object_object_get_ex(obj, key, &key_obj );
    if ( ( status == TRUE ) && ( key_obj ))
    {
        value.append(json_object_get_string(key_obj));
    }
    else
    {
        value.append("none");
    }
    return ( value );
}

string jsonUtil_get_key_value_string ( struct json_object * obj, const char * key )
{
    return (_json_get_key_value_string ( obj, key ));
}

int jsonUtil_get_key_value_int ( struct json_object * obj, const char * key )
{
    int value = 0 ;

    /* Get the node uuid */
    struct json_object * key_obj = (struct json_object *)(NULL);
    json_bool status = json_object_object_get_ex(obj, key, &key_obj);
    if ( (status == TRUE ) && ( key_obj ))
    {
        value = json_object_get_int(key_obj);
    }
    return ( value );
}

bool jsonUtil_get_key_value_bool ( struct json_object * obj, const char * key )
{
    bool value = false ;

    /* Get the node uuid */
    struct json_object * key_obj = (struct json_object *)(NULL);
    json_bool status = json_object_object_get_ex(obj, key, &key_obj );
    if (( status == TRUE ) && ( key_obj ))
    {
        value = json_object_get_boolean(key_obj);
    }
    else
    {
        wlog ("failed to get key object\n");
    }
    return ( value );
}

int jsonUtil_get_key_val ( char   * json_str_ptr,
                           string   key,
                           string & value )
{
    value = "" ;

    /* init to null to avoid trap on early cleanup call with
     * bad non-null default pointer value */
    struct json_object *raw_obj  = (struct json_object *)(NULL);

    if ((json_str_ptr == NULL) || ( *json_str_ptr == '\0' ) || ( ! strncmp ( json_str_ptr, "(null)" , 6 )))
    {
        elog ("Cannot tokenize a null json string\n");
        elog ("... json string: %s\n", json_str_ptr );
        return (FAIL);
    }

    size_t len_before = strlen (json_str_ptr);

    jlog2 ("String: %s\n", json_str_ptr );

    raw_obj = json_tokener_parse( json_str_ptr );
    if ( raw_obj )
    {
        value = _json_get_key_value_string ( raw_obj, key.data() ) ;
        jlog1 ("%s:%s\n", key.c_str(), value.c_str());
    }
    else
    {
        size_t len_after = strlen (json_str_ptr);

        elog ("Unable to tokenize string (before:%ld after:%ld);\n", len_before, len_after);
        elog ("... json string: %s\n", json_str_ptr );
    }

    if (raw_obj)
        json_object_put(raw_obj);

/* Sometimes gettibng an empty key is acceptable */
//    if (  value.empty() || !value.compare("none") )
//    {
//        return (FAIL);
//    }

    return (PASS);
}

int jsonUtil_get_key_val_int ( char   * json_str_ptr,
                               string   key,
                               int    & value )
{
    /* init to null to avoid trap on early cleanup call with
     * bad non-null default pointer value */
    struct json_object *raw_obj  = (struct json_object *)(NULL);

    if ((json_str_ptr == NULL) || ( *json_str_ptr == '\0' ) || ( ! strncmp ( json_str_ptr, "(null)" , 6 )))
    {
        elog ("Cannot tokenize a null json string\n");
        elog ("... json string: %s\n", json_str_ptr );
        return (FAIL);
    }

    size_t len_before = strlen (json_str_ptr);

    jlog2 ("String: %s\n", json_str_ptr );

    raw_obj = json_tokener_parse( json_str_ptr );
    if ( raw_obj )
    {
        value = jsonUtil_get_key_value_int ( raw_obj, key.data() ) ;
        jlog1 ("%s:%d\n", key.c_str(), value);
    }
    else
    {
        size_t len_after = strlen (json_str_ptr);

        elog ("Unable to tokenize string (before:%ld after:%ld);\n", len_before, len_after);
        elog ("... json string: %s\n", json_str_ptr );
    }

    if (raw_obj)
        json_object_put(raw_obj);

    return (PASS);
}

/** This utility freads the passed in inventory GET request
  * response json character string and performes the following
  * operations with failure detection for each step.
  *
  *   1. tokenizes the passed in string
  *   2. confirms string as valid inventory GET response
  *   3.
  *   4.
  *
  *  @returns
  *           PASS if no inventory,
  *           RETRY if there is a next element
  *           FAIL if there was a error
  *
  * Parse and load the data from the json inv request */
int jsonUtil_inv_load ( char * json_str_ptr,
                           jsonUtil_info_type & info )
{
    int rc = PASS ;

    /* init to null to avoid trap on early cleanup call with
     * bad non-null default pointer value */
    struct json_object *req_obj  = (struct json_object *)(NULL);
    struct json_object *raw_obj  = (struct json_object *)(NULL);
    struct json_object *node_obj = (struct json_object *)(NULL);
    struct json_object *next_obj = (struct json_object *)(NULL);

    if (( json_str_ptr == NULL ) || ( *json_str_ptr == '\0' ) ||
        ( ! strncmp ( json_str_ptr, "(null)" , 6 )))
    {
        elog ("Cannot tokenize a null json string\n");
        return (FAIL);
    }
    raw_obj = json_tokener_parse( json_str_ptr );
    if ( !raw_obj )
    {
        elog ("No or invalid inventory GET response\n");
        rc = FAIL ;
        goto cleanup ;
    }
    else
    {
       jlog1 ("%s\n", json_object_get_string(raw_obj));
    }

    /* Check response sanity */
    req_obj = _json_verify_object ( raw_obj, MTC_JSON_INV_LABEL );
    if ( !req_obj )
    {
        elog ("Missing or Invalid JSON Inventory Object\n");
        rc = FAIL ;
        goto cleanup ;
    }

    /* Get the label used to request the next inventory element */
    next_obj = _json_get_host_next ( raw_obj );
    if ( !next_obj )
    {
        info.next.clear();
    }
    else
    {
        info.next.clear();
        info.next.append(json_object_get_string(next_obj));
    }

    /* Limit the amount of batched inventory that can be read at once */
    info.elements = _json_get_object_number ( req_obj ) ;
    if ( info.elements > MAX_JSON_INV_GET_HOST_NUM )
        info.elements = MAX_JSON_INV_GET_HOST_NUM ;

    for ( int i = 0 ; i < info.elements ; i++ )
    {
        node_obj = _json_object_array_get_idx ( req_obj, i );
        if ( !node_obj )
        {
            wlog ("Host object index %d is not present.\n", i );
            rc = RETRY ;
            goto cleanup ;
        }

        /* Get all required fields */
        info.host[i].uuid = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_UUID    );
        info.host[i].name = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_NAME    );
        info.host[i].avail = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_AVAIL   );
        info.host[i].admin = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_ADMIN   );
        info.host[i].oper = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_OPER    );
        info.host[i].mac  = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_HOSTMAC );
        info.host[i].ip   = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_HOSTIP  );
        info.host[i].type = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_TYPE    );
        info.host[i].func = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_FUNC    );
        info.host[i].task = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_TASK    );
        info.host[i].bm_ip = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_BMIP    );
        info.host[i].bm_un = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_BMUN    );
        info.host[i].bm_type = _json_get_key_value_string( node_obj, MTC_JSON_INV_BMTYPE  );
        info.host[i].action = _json_get_key_value_string ( node_obj, MTC_JSON_INV_ACTION  );
        info.host[i].uptime = _json_get_key_value_string ( node_obj, MTC_JSON_INV_UPTIME  );
        info.host[i].oper_subf  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_OPER_SUBF );
        info.host[i].avail_subf = _json_get_key_value_string ( node_obj, MTC_JSON_INV_AVAIL_SUBF);
        info.host[i].clstr_ip = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_CLSTRIP );
        info.host[i].mtce_info = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_MTCE_INFO );

        if ( info.host[i].uuid.length() != UUID_LEN )
        {
            elog ("Failed to get json host uuid string\n");
            rc = FAIL;
        }
        if ( info.host[i].name.length() == 0 )
        {
            elog ("Failed to get json host name string\n");
            rc = FAIL ;
        }
        // jsonUtil_print ( info, i );
    }

cleanup:

    if (raw_obj)  json_object_put(raw_obj);

    return (rc);
}

/* This handler does nothing but verify the
   tokenization of the response */
int jsonUtil_patch_load ( char * json_str_ptr,
                             node_inv_type & info )
{
    /* init to null to avoid trap on early
     * cleanup call with bad non null
     * default pointer value */
    struct json_object *node_obj  = (struct json_object *)(NULL);

    node_obj = json_tokener_parse( json_str_ptr );
    if ( !node_obj )
    {
        elog ("No or invalid inventory PATCH response\n");
        return (FAIL);
    }

    /* Get all required fields */
    info.uuid  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_UUID    );
    info.name  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_NAME    );
    info.avail = _json_get_key_value_string ( node_obj, MTC_JSON_INV_AVAIL   );
    info.admin = _json_get_key_value_string ( node_obj, MTC_JSON_INV_ADMIN   );
    info.oper  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_OPER    );
    info.mac   = _json_get_key_value_string ( node_obj, MTC_JSON_INV_HOSTMAC );
    info.ip    = _json_get_key_value_string ( node_obj, MTC_JSON_INV_HOSTIP  );
    info.type  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_TYPE    );
    info.func  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_FUNC    );
    info.task  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_TASK    );
    info.bm_ip = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_BMIP   );
    info.bm_un = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_BMUN   );
    info.bm_type = _json_get_key_value_string( node_obj, MTC_JSON_INV_BMTYPE );
    info.action= _json_get_key_value_string ( node_obj, MTC_JSON_INV_ACTION  );
    info.uptime= _json_get_key_value_string ( node_obj, MTC_JSON_INV_UPTIME  );
    info.oper_subf  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_OPER_SUBF );
    info.avail_subf = _json_get_key_value_string ( node_obj, MTC_JSON_INV_AVAIL_SUBF);
    info.clstr_ip    = _json_get_key_value_string ( node_obj, MTC_JSON_INV_CLSTRIP );
    info.mtce_info = _json_get_key_value_string   ( node_obj, MTC_JSON_INV_MTCE_INFO );
    if (node_obj) json_object_put(node_obj);

    return (PASS);
}



/* Load up json_info with the contents of the json_str */
int jsonUtil_load_host ( char * json_str_ptr, node_inv_type & info )
{
    int rc = PASS ;
    string error = "" ;

    /* init to null to avoid trap on early cleanup call with
     * bad non-null default pointer value */
    struct json_object *node_obj = (struct json_object *)(NULL);
    struct json_object *err_obj = (struct json_object *)(NULL);

    if (( json_str_ptr == NULL ) || ( *json_str_ptr == '\0' ) ||
        ( ! strncmp ( json_str_ptr, "(null)" , 6 )))
    {
        elog ("Cannot tokenize a null json string\n");
        return (FAIL);
    }
    node_obj = json_tokener_parse( json_str_ptr );
    if ( !node_obj )
    {
        elog ("No or invalid inventory response\n");
        rc = FAIL ;
        goto load_host_cleanup ;
    }

    /* Check for error */
    error = _json_get_key_value_string   ( node_obj, "error_message" );
    if ( error == "none" )
    {
        node_inv_init ( info );

        /* Get all required fields */
        info.mac   = _json_get_key_value_string ( node_obj, MTC_JSON_INV_HOSTMAC);
        info.ip    = _json_get_key_value_string ( node_obj, MTC_JSON_INV_HOSTIP );
        info.uuid  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_UUID   );
        info.name  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_NAME   );
        info.avail = _json_get_key_value_string ( node_obj, MTC_JSON_INV_AVAIL  );
        info.admin = _json_get_key_value_string ( node_obj, MTC_JSON_INV_ADMIN  );
        info.oper  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_OPER   );
        info.type  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_TYPE   );
        info.func  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_FUNC   );
        info.task  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_TASK   );
        info.bm_ip = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_BMIP  );
        info.bm_un = _json_get_key_value_string  ( node_obj, MTC_JSON_INV_BMUN  );
        info.bm_type = _json_get_key_value_string( node_obj, MTC_JSON_INV_BMTYPE);
        info.action= _json_get_key_value_string ( node_obj, MTC_JSON_INV_ACTION );
        info.uptime= _json_get_key_value_string ( node_obj, MTC_JSON_INV_UPTIME );
        info.id    = _json_get_key_value_string ( node_obj, "id" );
        info.oper_subf  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_OPER_SUBF );
        info.avail_subf = _json_get_key_value_string ( node_obj, MTC_JSON_INV_AVAIL_SUBF);
        info.clstr_ip   = _json_get_key_value_string ( node_obj, MTC_JSON_INV_CLSTRIP );
        info.mtce_info  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_MTCE_INFO );

        if ( info.uuid.length() != UUID_LEN )
        {
            elog ("Failed to get json host uuid string\n");
            rc = FAIL;
        }
        if ( info.name.length() == 0 )
        {
            elog ("Failed to get json host name string\n");
            rc = FAIL ;
        }

    }
    else
    {
        std::size_t found ;
        /* { "error_message":
              "{ "debuginfo": null,
                 "faultcode": "Server",
                 "faultstring": "Server controller-0 could not be found."
               }"
            }
         */
        /* get the error */
        found=error.find ( "could not be found" );
        if ( found!=std::string::npos)
        {
            elog ("%s\n", error.c_str());
            elog ("Requested host not found\n");
            rc = PASS ;
            goto load_host_cleanup ;
        }
        else
        {
            elog ("Unknown error (%s)\n", error.c_str());
            rc = PASS ;
        }
    }

load_host_cleanup:

    if (node_obj) json_object_put(node_obj);
    if (err_obj) json_object_put(err_obj);

    return (rc);
}


/* Load up json_info with the contents of the json_str */
int jsonUtil_load_host_state ( char * json_str_ptr, node_inv_type & info )
{
    int rc = PASS ;
    string error = "" ;

    /* init to null to avoid trap on early cleanup call with
     * bad non-null default pointer value */
    struct json_object *node_obj = (struct json_object *)(NULL);
    struct json_object *err_obj = (struct json_object *)(NULL);

    if (( json_str_ptr == NULL ) || ( *json_str_ptr == '\0' ) ||
        ( ! strncmp ( json_str_ptr, "(null)" , 6 )))
    {
        elog ("Cannot tokenize a null json string\n");
        return (FAIL);
    }
    node_obj = json_tokener_parse( json_str_ptr );
    if ( !node_obj )
    {
        elog ("No or invalid inventory response\n");
        rc = FAIL ;
        goto load_host_cleanup ;
    }

    /* Check for error */
    error = _json_get_key_value_string   ( node_obj, "error_message" );
    if ( error == "none" )
    {
        node_inv_init ( info );

        /* Get all required fields */
        info.uuid  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_UUID   );
        info.name  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_NAME   );
        info.avail = _json_get_key_value_string ( node_obj, MTC_JSON_INV_AVAIL  );
        info.admin = _json_get_key_value_string ( node_obj, MTC_JSON_INV_ADMIN  );
        info.oper  = _json_get_key_value_string ( node_obj, MTC_JSON_INV_OPER   );
    }
    else
    {
        std::size_t found ;

        /* get the error */
        found=error.find ( "could not be found" );
        if ( found!=std::string::npos)
        {
            elog ("%s\n", error.c_str());
            elog ("Requested host not found\n");
            rc = PASS ;
            goto load_host_cleanup ;
        }
        else
        {
            elog ("Unknown error (%s)\n", error.c_str());
            rc = PASS ;
        }
    }

load_host_cleanup:

    if (node_obj) json_object_put(node_obj);
    if (err_obj) json_object_put(err_obj);

    return (rc);
}

int jsonUtil_secret_load ( string & name,
                           char * json_str_ptr,
                           jsonUtil_secret_type & info )
{
    int rc = PASS ;
    json_bool status ;

    /* init to null to avoid trap on early cleanup call with
     * bad non-null default pointer value */
    struct array_list * array_list_obj = (struct array_list *)(NULL);
    struct json_object *raw_obj = (struct json_object *)(NULL);
    struct json_object *secret_obj = (struct json_object *)(NULL);
    struct json_object *ref_obj = (struct json_object *)(NULL);

    if (( json_str_ptr == NULL ) || ( *json_str_ptr == '\0' ) ||
        ( ! strncmp ( json_str_ptr, "(null)" , 6 )))
    {
        elog ("Cannot tokenize a null json string\n");
        return (FAIL);
    }
    raw_obj = json_tokener_parse( json_str_ptr );
    if ( !raw_obj )
    {
        elog ("No or invalid json string (%s)\n", json_str_ptr );
        rc = FAIL ;
        goto secret_load_cleanup ;
    }

    status = json_object_object_get_ex(raw_obj, MTC_JSON_SECRET_LIST, &secret_obj );
    if ( ( status == TRUE ) && ( secret_obj ))
    {
        array_list_obj = json_object_get_array(secret_obj );
        if ( array_list_obj )
        {
            int len = array_list_length (array_list_obj );
            if ( len == 0 )
            {
                wlog ( "No %s elements in array\n", MTC_JSON_SECRET_LIST );
                goto secret_load_cleanup;
            }
            for ( int i = 0 ; i < len ; i++ )
            {
                ref_obj = _json_object_array_get_idx (secret_obj, i );
                if ( ref_obj )
                {
                    string secret_name = _json_get_key_value_string ( ref_obj, MTC_JSON_SECRET_NAME );
                    if ( ( secret_name.length() > 0) && !secret_name.compare(name) )
                    {
                        info.secret_ref = _json_get_key_value_string ( ref_obj, MTC_JSON_SECRET_REFERENCE );
                        jlog ( "Found secret_ref %s\n", info.secret_ref.c_str() );
                        break ;
                    }
                }
            }
        }
        else
        {
            elog ("%s Failed to find %s object array\n", name.c_str(), MTC_JSON_SECRET_LIST );
        }
    }
    else
    {
        elog ("%s Failed to find %s object\n", name.c_str(), MTC_JSON_SECRET_LIST );
    }

secret_load_cleanup:

    if (raw_obj) json_object_put(raw_obj );
    if (secret_obj) json_object_put(secret_obj );
    if (ref_obj) json_object_put(ref_obj );

    return (rc);
}

void jsonUtil_print ( jsonUtil_info_type & info, int index )
{
    if ( info.elements == 0 )
       return ;

    if ( info.elements < index )
       return ;

    print_inv ( info.host[index] ) ;
}

// {"auth": {"tenantName": "admin", "passwordCredentials": {"username": "admin", "password": "password"}}}


/* TODO: remove "assumed" case in favor of failing utility */
int jsonApi_auth_request ( string & hostname, string & payload )
{
    int rc = PASS ;
    char * getenv_ptr = NULL ;
    string projectname = ""   ;
    string username   = ""   ;
    string password   = ""   ;
    string userdomain    = "" ;
    string projectdomain = "" ;

    /* Get local username an password credentials */
    getenv_ptr = daemon_get_cfg_ptr()->keystone_auth_project;
    if ( getenv_ptr == NULL )
    {
        wlog ("%s Null Project Name\n", hostname.c_str());
        return ( FAIL_AUTHENTICATION );
    }
    projectname = getenv_ptr ;

    getenv_ptr = daemon_get_cfg_ptr()->keystone_user_domain;
    if ( getenv_ptr == NULL )
    {
        wlog ("%s Null User Domain Name\n", hostname.c_str());
        return ( FAIL_AUTHENTICATION );
    }
    userdomain = getenv_ptr ;

    getenv_ptr = daemon_get_cfg_ptr()->keystone_project_domain;
    if ( getenv_ptr == NULL )
    {
        wlog ("%s Null Project Domain Name\n", hostname.c_str());
        return ( FAIL_AUTHENTICATION );
    }
    projectdomain = getenv_ptr ;

    getenv_ptr = daemon_get_cfg_ptr()->keystone_auth_username;
    if ( getenv_ptr == NULL )
    {
        wlog ("%s Null Username\n", hostname.c_str());
        return ( FAIL_AUTHENTICATION );
    }
    username = getenv_ptr ;

    getenv_ptr = daemon_get_cfg_ptr()->keystone_auth_pw;
    if ( getenv_ptr == NULL )
    {
        wlog ("%s Null Password for '%s'\n", hostname.c_str(), username.c_str());
        return ( FAIL_AUTHENTICATION );
    }
    password = getenv_ptr ;

    /*
     * {
     *     "auth":
     *     {
     *         "identity":
     *         {
     *           "methods": ["password"],
     *           "password":
     *           {
     *             "user":
     *             {
     *               "name": "user name",
     *               "domain": { "name": "user domain name" },
     *               "password": "password"
     *             }
     *           }
     *         },
     *         "scope":
     *         {
     *           "project":
     *           {
     *             "name": "project name",
     *             "domain": { "name": "project domain name" }
     *           }
     *         }
     *     }
     * }
     *
     */

    /***** Create the payload *****/
    payload.append ("{\"auth\": ");
    payload.append ("{\"identity\": ");
    payload.append ("{\"methods\": [\"password\"],");
    payload.append ("\"password\": ");
    payload.append ("{\"user\": {\"name\": \"");
    payload.append (username.data());
    payload.append ("\",\"domain\": {\"name\": \"");
    payload.append (userdomain.data());
    payload.append ("\"},\"password\": \"");
    payload.append (password.data());
    payload.append ("\"}}},");
    payload.append ("\"scope\": ");
    payload.append ("{\"project\": {\"name\": \"");
    payload.append (projectname.data());
    payload.append ("\", \"domain\": { \"name\": \"");
    payload.append (projectdomain.data());
    payload.append ("\"}}}}}");

    return (rc);
}

/* Tokenizes the json string and loads info with
 * the received token and adminURL.
 * The search algorithm below
 *
 *  An authentication response returns the token ID in X-Subject-Token header
 *  rather than in the response body.
 *
 *
    "token": {                                                  <--- 1
        "audit_ids": [
            "LdAVJLRPQwiVN2OkJnuewg"
        ],
        "expires_at": "2016-06-08T21:16:04.408053Z",            <--- 2
        "extras": {},
        "issued_at": "2016-06-08T20:16:04.408082Z",             <--- 3
         "methods": [
            "password"
         ],
        "catalog": [                                            <--- 4
        {
                "endpoints": [
                    {
                        "id": "24177b65a50548519ae5182a5fd44533",
                        "interface": "public",
                        "region": "RegionOne",
                        "region_id": "RegionOne",
                        "url": "http://10.10.10.2:9696"
                    },
                    {
                        "id": "52cd1c1b83d9449babfe85b69797c952",
                        "interface": "internal",
                        "region": "RegionOne",
                        "region_id": "RegionOne",
                        "url": "http://192.168.204.2:9696"
                    },
                    {
                        "id": "ea7b94d6a9fb4b86acd29985babb61b3",
                        "interface": "admin",                  <--- 5
                        "region": "RegionOne",
                        "region_id": "RegionOne",
                        "url": "http://192.168.204.2:9696"     <--- 6
                    }
                ],
                "id": "367ab50ca63e468f8abbf8fe348efe12",
                "name": "neutron",
                "type": "network"
            },
            { ... }]}
*/
int jsonApi_auth_load    ( string & hostname,
                              char * json_str_ptr,
                              jsonUtil_auth_type & info )
{
    json_bool status ;
    int rc = FAIL ;

    info.tokenid = "" ;
    info.issued  = "" ;
    info.expiry  = "" ;
    info.adminURL= "" ;

    /* init to null to avoid trap on early cleanup call */
    struct array_list * array_list_obj = (struct array_list *)(NULL) ;
    struct json_object *raw_obj  = (struct json_object *)(NULL);
    struct json_object *token_obj = (struct json_object *)(NULL);
    struct json_object *svccat_obj = (struct json_object *)(NULL);
    struct json_object *tuple_obj = (struct json_object *)(NULL);
    struct json_object *url_obj = (struct json_object *)(NULL);
    struct json_object *end_obj = (struct json_object *)(NULL);

    bool found_type = false ;

    raw_obj = json_tokener_parse( json_str_ptr );
    if ( !raw_obj )
    {
        elog ("%s No or invalid inventory GET response\n", hostname.c_str());
        goto auth_load_cleanup ;
    }

    /* Get the token object */
    status = json_object_object_get_ex(raw_obj, MTC_JSON_AUTH_TOKEN, &token_obj);
    if (( status == TRUE ) && ( token_obj ))
    {
        info.issued.append (_json_get_key_value_string(token_obj, MTC_JSON_AUTH_ISSUE ));
        info.expiry.append (_json_get_key_value_string(token_obj, MTC_JSON_AUTH_EXPIRE));
    }
    else
    {
        elog ("%s Failed to read %s object label\n", hostname.c_str(), MTC_JSON_AUTH_TOKEN );
        goto auth_load_cleanup ;
    }
    /* Now look for the compute admin URL */
    /* Get the token object */
    status = json_object_object_get_ex(token_obj, MTC_JSON_AUTH_SVCCAT, &svccat_obj );
    if (( status == TRUE ) && ( svccat_obj ))
    {
        array_list_obj = json_object_get_array(svccat_obj);
        if ( array_list_obj )
        {
            string entity ;
            int len = array_list_length (array_list_obj);
            if ( len == 0 )
            {
                ilog ( "%s No reply %s elements\n", hostname.c_str(), MTC_JSON_AUTH_SVCCAT );
                goto auth_load_cleanup;
            }
            for ( int i = 0 ; i < len ; i++ )
            {
                tuple_obj = _json_object_array_get_idx (svccat_obj, i);
                entity = _json_get_key_value_string ( tuple_obj, MTC_JSON_AUTH_TYPE );
                if ( entity == MTC_JSON_AUTH_PLATFORM)
                {
                    found_type = true ;
                    break ;
                }
            }
        }
    }

    if ( found_type == true )
    {
        json_bool status = json_object_object_get_ex(tuple_obj, MTC_JSON_AUTH_ENDPOINTS, &end_obj);
        if ( ( status == TRUE ) && ( end_obj ))
        {
            array_list_obj = json_object_get_array(end_obj);
            if ( array_list_obj )
            {


                int len = array_list_length (array_list_obj);
                if ( len == 0 )
                {
                    ilog ( "No reply %s elements in array\n", MTC_JSON_AUTH_ENDPOINTS);
                    goto auth_load_cleanup;
                }
                for ( int i = 0 ; i < len ; i++ )
                {
                    url_obj = _json_object_array_get_idx (end_obj, i);
                    if ( url_obj )
                    {
                        string inf = _json_get_key_value_string ( url_obj, MTC_JSON_AUTH_INTERFACE );
                        if (( inf.length() > 0 ) && (inf == MTC_JSON_AUTH_ADMIN))
                        {
                            info.adminURL = _json_get_key_value_string ( url_obj, MTC_JSON_AUTH_URL);
                            jlog ( "Found adminURL %s\n", info.adminURL.c_str());
                            rc = PASS ;
                            break ;
                        }
                    }
                }
            }
            else
            {
                elog ("%s Failed to find %s object array\n",
                          hostname.c_str(), MTC_JSON_AUTH_ENDPOINTS );
            }
        }
        else
        {
            elog ("%s Failed to find %s object\n",
                      hostname.c_str(), MTC_JSON_AUTH_ENDPOINTS );
        }
    }
    else
    {
        elog ("%s Failed to find %s object label\n",
                  hostname.c_str(), MTC_JSON_AUTH_SVCCAT);
    }

auth_load_cleanup:

    if (raw_obj)
    {
        if ( rc )
        {
            wlog ("%s JSON String:%s\n", hostname.c_str(), json_object_get_string(raw_obj));
        }
        json_object_put(raw_obj);
    }

    return (rc);
}

/***********************************************************************
 * This utility updates the reference key_list with all the
 * values for the specified label.
 ***********************************************************************/
int jsonUtil_get_list ( char * json_str_ptr, string label, list<string> & key_list )
{
    int rc = PASS ;
    struct array_list  * list_obj  = (struct array_list  *)(NULL);
    struct json_object * label_obj = (struct json_object *)(NULL);
    struct json_object * raw_obj   = (struct json_object *)(NULL);
    struct json_object * item_obj  = (struct json_object *)(NULL);

    raw_obj = json_tokener_parse( json_str_ptr );
    if ( !raw_obj )
    {
        elog ("unable to parse raw object (%s)\n", json_str_ptr);
        rc = FAIL_JSON_OBJECT ;
        goto get_list_cleanup ;
    }

    label_obj = _json_verify_object (raw_obj, label.data());
    if ( !label_obj )
    {
        jlog ("unable to find label '%s'\n", label.c_str());
        rc = FAIL_JSON_OBJECT ;
        goto get_list_cleanup ;
    }
    list_obj = json_object_get_array(label_obj);
    if ( list_obj )
    {
        int len = array_list_length (list_obj);
        jlog ( "'%s' array has %d elements\n", label.c_str(), len );
        for ( int i = 0 ; i < len ; i++ )
        {
            item_obj = _json_object_array_get_idx (label_obj, i);
            jlog1 ("%s %d:%s\n", label.c_str(), i, json_object_get_string(item_obj));
            key_list.push_back (json_object_get_string(item_obj));
        }
    }

get_list_cleanup:

    if (raw_obj)   json_object_put(raw_obj);

    return (rc);
}

/***************************************************************************
 * This utility searches for an 'array_label' and then loops over the array
 * looking at each element for the specified 'search_key' and 'search_value'
 * Once found it searches that same element for the specified 'element_key'
 * and loads its value content into 'element_value' - what we're looking for
 ***************************************************************************/
int jsonApi_array_value ( char * json_str_ptr,
                             string array_label,
                             string search_key,
                             string search_value,
                             string element_key,
                             string & element_value)
{
    json_bool status ;
    int rc = FAIL ;

    /* init to null to avoid trap on early cleanup call */
    struct array_list * array_list_obj = (struct array_list *)(NULL) ;
    struct json_object *raw_obj  = (struct json_object *)(NULL);
    struct json_object *array_obj = (struct json_object *)(NULL);
    struct json_object *tuple_obj = (struct json_object *)(NULL);
    struct json_object *type_obj = (struct json_object *)(NULL);

    if ( strlen(json_str_ptr) < 3 )
    {
        elog ("Null json string\n" );
        rc = FAIL_NULL_POINTER;
        goto array_value_cleanup ;
    }
    raw_obj = json_tokener_parse( json_str_ptr );
    if ( !raw_obj )
    {
        elog ("No or invalid json string (%s)\n", json_str_ptr );
        rc = FAIL_JSON_PARSE ;
        goto array_value_cleanup ;
    }

    /* Now look in each array element for the 'search_key' */
    status = json_object_object_get_ex(raw_obj, array_label.data(), &array_obj );
    if (( status == TRUE ) && ( array_obj ))
    {
        /* Leaking array_list_obj ???? */
        array_list_obj = json_object_get_array(array_obj);
        if ( array_list_obj )
        {
            int len = array_list_length (array_list_obj);
            if ( len == 0 )
            {
                wlog ( "%s has zero array elements\n", array_label.c_str() );
                goto array_value_cleanup;
            }
            for ( int i = 0 ; i < len ; i++ )
            {
                tuple_obj = _json_object_array_get_idx (array_obj, i);
                if ( tuple_obj )
                {
                    string element = _json_get_key_value_string ( tuple_obj, search_key.data() );
                    if ( !search_value.compare(element))
                    {
                        /* ok we found "secname : cUSERS" now get the uuid */
                        element_value = _json_get_key_value_string ( tuple_obj, element_key.data() );
                        if ( !element_value.empty() )
                        {
                            jlog1 ("%s with %s element has %s:%s\n", array_label.c_str(),
                                                                search_value.c_str(),
                                                                element_key.c_str(),
                                                                element_value.c_str());
                            rc = PASS ;
                        }
                    }
                }
            }
            if ( rc )
            {
                elog ("Failed to find %s : %s\n", search_key.c_str(), element_key.c_str());
                rc = FAIL_JSON_OBJECT ;
            }
        }
    }
    else
    {
        elog ("Failed to locate array label (%s)\n", array_label.c_str());
        rc = FAIL_JSON_OBJECT ;
    }

array_value_cleanup:

    if (raw_obj)   json_object_put(raw_obj);
    if (type_obj)  json_object_put(type_obj);

    return (rc);
}

/***********************************************************************
 * This utility updates the reference string 'element' with the
 * contents of the specified labeled array element index.
 ***********************************************************************/
int jsonUtil_get_array_idx  (   char * json_str_ptr,
                              string   label,
                                 int   idx,
                              string & element )
{
    json_bool status ;
    int rc = PASS ;

    /* init to null to avoid trap on early cleanup call */
    struct array_list * array_list_obj = (struct array_list *)(NULL) ;
    struct json_object *raw_obj  = (struct json_object *)(NULL);
    struct json_object *array_obj = (struct json_object *)(NULL);
    struct json_object *tuple_obj = (struct json_object *)(NULL);

    if ( strlen(json_str_ptr) < 3 )
    {
        elog ("Null json string\n" );
        rc = FAIL_NULL_POINTER;
        goto get_array_idx_cleanup ;
    }
    raw_obj = json_tokener_parse( json_str_ptr );
    if ( !raw_obj )
    {
        elog ("No or invalid json string (%s)\n", json_str_ptr );
        rc = FAIL_JSON_PARSE ;
        goto get_array_idx_cleanup ;
    }

    /* Now look in each array element for the 'search_key' */
    status = json_object_object_get_ex(raw_obj, label.data(), &array_obj );
    if (( status == TRUE ) && ( array_obj ))
    {
        element.clear();

        array_list_obj = json_object_get_array(array_obj);
        if ( array_list_obj )
        {
            int len = array_list_length (array_list_obj);
            if ( idx < len )
            {
                tuple_obj = _json_object_array_get_idx (array_obj, idx);
                if ( tuple_obj )
                {
                    jlog1 ("%s %d:%s\n", label.c_str(), idx, json_object_get_string(tuple_obj));
                    element = json_object_get_string(tuple_obj);
                }
                if ( rc )
                {
                    elog ("Failed to get '%s' array index %d\n", label.c_str(), idx);
                    rc = FAIL_JSON_OBJECT ;
                }
            }
            else if ( len == 0 )
            {
                dlog ( "%s array has zero elements\n", label.c_str() );
            }
            else
            {
                dlog ( "%s array has fewer elements than the specified index (%d)\n", label.c_str(), idx );
            }
        }
    }
    else
    {
        elog ("Failed to locate array label (%s)\n", label.c_str());
        rc = FAIL_JSON_OBJECT ; 
    }

get_array_idx_cleanup:

    if (raw_obj)   json_object_put(raw_obj);
    return (rc);
}


/***********************************************************************
 * This utility updates the reference element with the number of array
 * elements for the specified label in the provided string
 ***********************************************************************/
int jsonUtil_array_elements ( char * json_str_ptr, string label, int & elements )
{
    json_bool status ;
    int rc = FAIL ;

    /* init to null to avoid trap on early cleanup call */
    struct array_list * array_list_obj = (struct array_list *)(NULL) ;
    struct json_object *raw_obj  = (struct json_object *)(NULL);
    struct json_object *array_obj = (struct json_object *)(NULL);

    if ( strlen(json_str_ptr) < 3 )
    {
        elog ("Null json string\n" );
        rc = FAIL_NULL_POINTER;
        goto array_elements_cleanup ;
    }
    raw_obj = json_tokener_parse( json_str_ptr );
    if ( !raw_obj )
    {
        elog ("No or invalid json string (%s)\n", json_str_ptr );
        rc = FAIL_JSON_PARSE ;
        goto array_elements_cleanup ;
    }

    /* Now look in each array element for the 'search_key' */
    status = json_object_object_get_ex(raw_obj, label.data(), &array_obj );
    if (( status == TRUE ) && ( array_obj ))
    {
        array_list_obj = json_object_get_array(array_obj);
        if ( array_list_obj )
        {
            elements = array_list_length (array_list_obj);
            rc = PASS ;
        }
    }
    else
    {
        elog ("Failed to locate array label (%s)\n", label.c_str());
        rc = FAIL_JSON_OBJECT ;
    }

array_elements_cleanup:

    if (raw_obj)   json_object_put(raw_obj);

    return (rc);
}

/*************************************************************************************
 *
 * Name       : escapeJsonString
 *
 * Description: escape special characters in JSON string
 *
 **************************************************************************************/
string jsonUtil_escapeSpecialChar(const string& input)
{
    ostringstream ss;
    for (string::const_iterator iter = input.begin(); iter != input.end(); iter++)
    {
        switch (*iter)
        {
            case '\\': ss << "\\\\"; break;
            case '"': ss << "\\\""; break;
            case '/': ss << "\\/"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << *iter; break;
        }
    }
    return ss.str();
}

/***********************************************************************
 * Get JSON Integer Value from Key
 * return PASS if success, FAIL if fail.
 ***********************************************************************/
int jsonUtil_get_int( struct json_object *jobj,
                      const char *key, void *value )
{
    struct json_object *jobj_value;
    if (!json_object_object_get_ex(jobj, key, &jobj_value))
    {
        return FAIL;
    }
    enum json_type type = json_object_get_type(jobj_value);
    switch(type)
    {
        case json_type_boolean:
            *(unsigned int *)value = json_object_get_boolean(jobj_value);
            break;
        case json_type_int:
            *(unsigned int *)value = json_object_get_int(jobj_value);
            break;
        case json_type_double:
            *(double *)value = json_object_get_double(jobj_value);
            break;
        default:
            elog("type %d is not supported\n", type);
            return FAIL;
            break;
    }
    return PASS;
}

/***********************************************************************
 * Get JSON String Value from Key
 * return PASS if success, FAIL if fail.
 ***********************************************************************/
int jsonUtil_get_string( struct json_object* jobj,
                         const char* key, string * value )
{
    struct json_object *jobj_value;
    if (!json_object_object_get_ex(jobj, key, &jobj_value))
    {
        return FAIL;
    }
    enum json_type type = json_object_get_type(jobj_value);
    const char *str;
    switch(type)
    {
        case json_type_string:
            str = json_object_get_string(jobj_value);
            break;
        default:
            elog("type %d is not supported\n", type);
            return FAIL;
            break;
    }
    *value = str;
    return PASS;
}
