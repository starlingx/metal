/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River Titanium Cloud Hardware Monitor Json Utilities Header
  */

#include <stdio.h>
#include <stdlib.h>
#include <json-c/json.h>      /* for ... json-c json string parsing */

using namespace std;

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "hwm"

#include "nodeBase.h"
#include "nodeUtil.h"
#include "hwmonJson.h"    /* JSON Utilities      */
#include "hwmon.h"
#include "hwmonHttp.h"

string _get_key_value_string ( struct json_object * obj, const char * key )
{
    std::string value = "" ;
    struct json_object * key_obj = (struct json_object *)(NULL) ;

    /* use the new API ;
     * json_object_object_get is depricated and yields compile warning */
    json_bool status = json_object_object_get_ex(obj, key, &key_obj);
    if ( ( status == TRUE ) && key_obj )
    {
        value.append(json_object_get_string(key_obj));
    }
    else
    {
        value.append("none");
    }
    return ( value );
}


int hwmonJson_load_inv ( char * json_str_ptr, node_inv_type & info )
{
    int rc = PASS ;
    string error = "" ;
    string infra_ip = "" ;

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
        goto hwmon_info_cleanup ;
    }

    node_inv_init ( info );

    /* Get all required fields */
    //info.mac   = _get_key_value_string ( node_obj, MTC_JSON_INV_HOSTMAC);
    //info.ip    = _get_key_value_string ( node_obj, MTC_JSON_INV_HOSTIP );
    info.name    = _get_key_value_string ( node_obj, MTC_JSON_INV_NAME  );

    infra_ip = _get_key_value_string ( node_obj, MTC_JSON_INV_INFRAIP );
    if ( infra_ip.length() )
    {
        dlog ("%s inventory has infra_ip=%s\n", info.name.c_str(), infra_ip.c_str());
        info.infra_ip = infra_ip;
    }
    info.type    = _get_key_value_string ( node_obj, MTC_JSON_INV_TYPE  );
    info.uuid    = _get_key_value_string ( node_obj, MTC_JSON_INV_UUID  );
    info.bm_ip   = _get_key_value_string ( node_obj, MTC_JSON_INV_BMIP  );
    info.bm_un   = _get_key_value_string ( node_obj, MTC_JSON_INV_BMUN  );
    info.bm_type = _get_key_value_string ( node_obj, MTC_JSON_INV_BMTYPE);

    /* print the parsed info if debug level is 3 - mlog2 */
    if ( daemon_get_cfg_ptr()->debug_msg == DEBUG_LEVEL3 )
    {
        print_inv ( info );
    }

hwmon_info_cleanup:

    if (node_obj) json_object_put(node_obj);
    if (err_obj) json_object_put(err_obj);

    return (rc);
}
