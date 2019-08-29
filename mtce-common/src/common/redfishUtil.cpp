/*
 * Copyright (c) 2019 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Starling-X Common Redfish Utilities
 */
#include <stdio.h>
#include <iostream>
#include <string.h>

using namespace std;

#include "nodeBase.h"      /* for ... mtce node common definitions     */
#include "nodeUtil.h"      /* for ... tolowercase                      */
#include "hostUtil.h"      /* for ... mtce host common definitions     */
#include "jsonUtil.h"      /* for ...      */
#include "redfishUtil.h"   /* for ... this module header               */

/*************************************************************************
 *
 * Name       : redfishUtil_init
 *
 * Purpose    : Module init
 *
 * Description: Initialize redfish tool utility module.
 *
 * Returns    : Initialization result ; always PASS (for now)
 *
 *************************************************************************/

int redfishUtil_init ( void )
{
    daemon_make_dir(REDFISHTOOL_OUTPUT_DIR) ;
    return (PASS);
}

/*************************************************************************
 *
 * Name       : redfishUtil_is_supported
 *
 * Purpose    : Check for redfish supported response
 *
 * Description: A redfish root query response that indicates redfish
 *              protocol support includes the following key:value.
 *
 *              "RedfishVersion": "1.0.1",
 *
 * Assumptions: Must support redfish version 1.x.x or higher.
 *
 * Parameters : The root query response string
 *
 * Returns    : true if redfish is supported.
 *              false otherwise
 *
 *************************************************************************/

bool redfishUtil_is_supported (string & hostname, string & response)
{
    if ( response.length() > strlen(REDFISH_LABEL__REDFISH_VERSION ))
    {
        string redfish_version = "" ;

        /* look for error ; stderro is directed to the datafile */
        if ( response.find(REDFISHTOOL_RESPONSE_ERROR) != string::npos )
        {
            if ( response.find(REDFISHTOOL_ERROR_STATUS_CODE__NOT_FOUND) != string::npos )
            {
                ilog ("%s does not support Redfish platform management",
                          hostname.c_str());
            }
            else
            {
                wlog ("%s redfishtool %s: %s",
                          hostname.c_str(),
                          REDFISHTOOL_RESPONSE_ERROR,
                          response.c_str());
            }
            return (false) ;
        }

        /* if no error then look for the redfish version number */
        if ( jsonUtil_get_key_val ((char*)response.data(),
                                   REDFISH_LABEL__REDFISH_VERSION,
                                   redfish_version) == PASS )
        {
            if ( ! redfish_version.empty() )
            {
                int major = 0, minor = 0, revision = 0 ;
                int fields = sscanf ( redfish_version.data(),
                                      "%d.%d.%d",
                                      &major,
                                      &minor,
                                      &revision );

                if (( fields ) && ( major >= REDFISH_MIN_MAJOR_VERSION ))
                {
                    ilog ("%s bmc redfish version %s (%d.%d.%d)",
                              hostname.c_str(),
                              redfish_version.c_str(),
                              major, minor, revision );
                    return true ;
                }
                else
                {
                    ilog ("%s bmc has unsupported redfish version %s (%d:%d.%d.%d)",
                              hostname.c_str(),
                              redfish_version.c_str(),
                              fields, major, minor, revision );
                    blog ("%s response: %s", hostname.c_str(), response.c_str());
                }
            }
            else
            {
                wlog ("%s bmc failed to provide redfish version\n%s",
                          hostname.c_str(),
                          response.c_str());
            }
        }
        else
        {
            wlog ("%s bmc redfish root query response has no '%s' label\n%s",
                      hostname.c_str(),
                      REDFISH_LABEL__REDFISH_VERSION,
                      response.c_str());
        }
    }
    else
    {
        ilog ("%s bmc does not support redfish",
                  hostname.c_str());
    }
    return false ;
}


/*************************************************************************
 *
 * Name       : redfishUtil_create_request
 *
 * Purpose    : create redfishtool command request
 *
 * Description: A command request involves command options / arguements
 *
 *              -r ip          - the ip address to send the request to
 *              -c config_file - the bmc cfgFile (password) filename
 *                 cmd         - the redfish command to execute
 *               > out         - the filename to where the output is directed
 *
 * Returns    : full command request as a single string
 *
 *************************************************************************/

string redfishUtil_create_request ( string   cmd,
                                    string & ip,
                                    string & config_file,
                                    string & out )
{
    /* build the command ; starting with the redfishtool binary */
    string command_request = REDFISHTOOL_PATH_AND_FILENAME ;

    /* allow the BMC to redirect http to https */
    command_request.append(" -S Always");

    /* specify the bmc ip address */
    command_request.append(" -r ");
    command_request.append(ip);

    /* add the config file option and config filename */
    command_request.append(" -c ");
    command_request.append(config_file);

    /* add the command */
    command_request.append(" ");
    command_request.append(cmd);

    /* output filename */
    command_request.append (" > ");
    command_request.append (out);

    /* direct stderr to stdio */
    command_request.append (" 2>&1");

    return (command_request);
}

/*************************************************************************
 *
 * Name      : redfishUtil_get_bmc_info
 *
 * Purpose   :
 *
 * Description:
 *
 * Returns    : PASS if succesful
 *              FAIL_OPERATION if unsuccessful
 *
 ************************************************************************/

int redfishUtil_get_bmc_info ( string & hostname,
                               string & bmc_info_filename,
                               bmc_info_type & bmc_info )
{
    if ( bmc_info_filename.empty() )
    {
        wlog ("%s bmc info filename empty", hostname.c_str());
        return (FAIL_NO_DATA);
    }

    string json_bmc_info = daemon_read_file (bmc_info_filename.data());
    if ( json_bmc_info.empty() )
    {
        wlog ("%s bmc info file empty", hostname.c_str());
        return (FAIL_STRING_EMPTY) ;
    }

    struct json_object *json_obj = json_tokener_parse((char*)json_bmc_info.data());
    if ( !json_obj )
    {
        wlog ("%s bmc info file empty", hostname.c_str());
        return (FAIL_JSON_PARSE) ;

    }

    bmc_info.manufacturer  = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__MANUFACTURER );
    bmc_info.sn            = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__SERIAL_NUMBER);
    bmc_info.mn            = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__MODEL_NUMBER );
    bmc_info.pn            = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__PART_NUMBER  );
    bmc_info.bmc_ver       = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__BMC_VERSION  );
    bmc_info.bios_ver      = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__BIOS_VERSION );

    ilog ("%s manufacturer is %s", hostname.c_str(), bmc_info.manufacturer.c_str());
    ilog ("%s model number:%s  part number:%s  serial number:%s",
              hostname.c_str(),
              bmc_info.mn.c_str(),
              bmc_info.pn.c_str(),
              bmc_info.sn.c_str());

    ilog ("%s BIOS firmware version is %s",
              hostname.c_str(),
              bmc_info.bios_ver != NONE ? bmc_info.bios_ver.c_str() : "unavailable" );

    ilog ("%s BMC  firmware version is %s",
              hostname.c_str(),
              bmc_info.bmc_ver != NONE ? bmc_info.bmc_ver.c_str() : "unavailable" );

    /* load the power state */
    string power_state = tolowercase(jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__POWER_STATE));
    if ( power_state == "on" )
        bmc_info.power_on = true ;
    else
        bmc_info.power_on = false ;
    ilog ("%s power is %s", hostname.c_str(), power_state.c_str());


    /* get number of processors */
    string processors = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__PROCESSOR );
    if ( ! processors.empty() )
    {
        struct json_object *proc_obj = json_tokener_parse((char*)processors.data());
        if ( proc_obj )
        {
            bmc_info.processors = jsonUtil_get_key_value_int ( proc_obj, REDFISH_LABEL__COUNT );
            ilog ("%s has %d processors", hostname.c_str(), bmc_info.processors);
            json_object_put(proc_obj );
        }
        else
        {
            slog ("%s processor obj: %s", hostname.c_str(), processors.c_str());
        }
    }
    else
    {
         slog ("%s processor count unavailable", hostname.c_str());
    }

    /* get amount of memory */
    string memory = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__MEMORY );
    if ( ! memory.empty() )
    {
        struct json_object *mem_obj = json_tokener_parse((char*)memory.data());
        if ( mem_obj )
        {
            bmc_info.memory_in_gigs = jsonUtil_get_key_value_int ( mem_obj, REDFISH_LABEL__MEMORY_TOTAL );
            ilog ("%s has %d gigs of memory", hostname.c_str(), bmc_info.memory_in_gigs );
            json_object_put(mem_obj );
        }
        else
        {
            slog ("%s memory obj: %s", hostname.c_str(), memory.c_str() );
        }
    }
    else
    {
        slog ("%s memory size unavailable", hostname.c_str());
    }

    json_object_put(json_obj );

    return PASS ;
}
