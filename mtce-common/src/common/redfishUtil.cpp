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
    if ( response.length() > strlen(REDFISH_LABEL__FW_VERSION ))
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
                                   REDFISH_LABEL__FW_VERSION,
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
                    ilog ("%s response: %s", hostname.c_str(), response.c_str()); // ERIK: make blog ?
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
                      REDFISH_LABEL__FW_VERSION,
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
