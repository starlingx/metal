/*
 * Copyright (c) 2019 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Starling-X Common Bmc Utilities
 */
#include <stdio.h>
#include <iostream>
#include <string.h>

using namespace std;

#include "nodeBase.h"    /* for ... mtce-common node definitions     */
#include "hostUtil.h"    /* for ... mtce-common host definitions     */
#include "bmcUtil.h"     /* for ... mtce-common bmc utility header   */
#include "nodeUtil.h"    /* for ... tolowercase                      */
#include "jsonUtil.h"    /* for ... jsonUtil_get_key_value_string    */

/**********************************************************************
 *
 * Name       : bmcUtil_getCmd_str
 *
 * Purpose    : logging ; bmc request
 *
 * Description: return string representing command
 *
 * Construct  : static array of bmc request strings
 *
 *              bmcUtil_request_str_array
 *
 * Assumptions: initialized in module init
 *
 **********************************************************************/

static std::string bmcUtil_request_str_array [BMC_THREAD_CMD__LAST+1] ;
string bmcUtil_getCmd_str ( int command )
{
    if ( command >= BMC_THREAD_CMD__LAST )
    {
        slog ("Invalid thread command (%d)\n", command );
        return (bmcUtil_request_str_array[BMC_THREAD_CMD__LAST]);
    }
    return (bmcUtil_request_str_array[command]);
}

/**********************************************************************
 *
 * Name       : bmcUtil_getAction_str
 *
 * Purpose    : logging ; bmc action
 *
 * Description: return string representing action
 *
 * Construct  : static array of bmc action strings
 *
 *              bmcUtil_action_str_array
 *
 * Assumptions: initialized in module init
 *
 **********************************************************************/

static std::string bmcUtil_action_str_array  [BMC_THREAD_CMD__LAST+1] ;
string bmcUtil_getAction_str ( int action )
{
    if ( action >= BMC_THREAD_CMD__LAST )
    {
        slog ("Invalid thread action (%d)\n", action );
        return (bmcUtil_action_str_array[BMC_THREAD_CMD__LAST]);
    }
    return (bmcUtil_action_str_array[action]);
}

/**********************************************************************
 *
 * Name       : bmcUtil_getProtocol_str
 *
 * Purpose    : logging ; bmc protocol name
 *
 * Description: return string representing bmc protocol name
 *
 **********************************************************************/

string bmcUtil_getProtocol_str ( bmc_protocol_enum protocol )
{
    switch (protocol)
    {
        case BMC_PROTOCOL__REDFISHTOOL: return(BMC_PROTOCOL__REDFISHTOOL_STR);
        case BMC_PROTOCOL__IPMITOOL:    return(BMC_PROTOCOL__IPMITOOL_STR);
        case BMC_PROTOCOL__DYNAMIC:     return(BMC_PROTOCOL__DYNAMIC_STR);
        default:                        return(NONE);
    }
}

/**********************************************************************
 *
 * Name       : bmcUtil_chop_system_req
 *
 * Purpose    : logging ; reduce the length of the system call
 *              request for logging purposes.
 *
 * Warning    : Do not use the chopped string to make the system
 *              call request. Only use it for logging.
 *
 * Description: return the chopped string.
 *
 **********************************************************************/

string bmcUtil_chop_system_req ( string request )
{
    string chopped_request = "" ;
    size_t found = request.find(" > ") ;
    if ( found != string::npos )
        chopped_request = request.substr(9,found-9);
    else
        chopped_request = request.substr(9);
    return (chopped_request);
}

/*************************************************************************
 *
 * Name       : bmcUtil_init
 *
 * Purpose    : Initialize various common Board Management support
 *              service functions and aspects.
 *
 * Description: Init support for IPMI and Redfish
 *
 * Returns    : Initialization result ; always PASS (for now)
 *
 *************************************************************************/

int bmcUtil_init ( void )
{
    if ( daemon_is_file_present ( BMC_OUTPUT_DIR ) == false )
        daemon_make_dir(BMC_OUTPUT_DIR) ;
    ipmiUtil_init ();
    redfishUtil_init ();

#ifdef WANT_FIT_TESTING
    daemon_make_dir(FIT__INFO_FILEPATH);
#endif

    /* init static strings */
    bmcUtil_request_str_array[BMC_THREAD_CMD__POWER_RESET]   = "Reset";
    bmcUtil_request_str_array[BMC_THREAD_CMD__POWER_ON]      = "Power-On";
    bmcUtil_request_str_array[BMC_THREAD_CMD__POWER_OFF]     = "Power-Off";
    bmcUtil_request_str_array[BMC_THREAD_CMD__POWER_CYCLE]   = "Power-Cycle";
    bmcUtil_request_str_array[BMC_THREAD_CMD__BMC_QUERY]     = "Query BMC Root";
    bmcUtil_request_str_array[BMC_THREAD_CMD__BMC_INFO]      = "Query BMC Info";
    bmcUtil_request_str_array[BMC_THREAD_CMD__POWER_STATUS]  = "Query Power Status";
    bmcUtil_request_str_array[BMC_THREAD_CMD__RESTART_CAUSE] = "Query Reset Reason";
    bmcUtil_request_str_array[BMC_THREAD_CMD__BOOTDEV_PXE]   = "Netboot";
    bmcUtil_request_str_array[BMC_THREAD_CMD__READ_SENSORS]  = "Read Sensors";
    bmcUtil_request_str_array[BMC_THREAD_CMD__LAST]          = "unknown";

    bmcUtil_action_str_array[BMC_THREAD_CMD__POWER_RESET]    = "resetting";
    bmcUtil_action_str_array[BMC_THREAD_CMD__POWER_ON]       = "powering on";
    bmcUtil_action_str_array[BMC_THREAD_CMD__POWER_OFF]      = "powering off";
    bmcUtil_action_str_array[BMC_THREAD_CMD__POWER_CYCLE]    = "power cycling";
    bmcUtil_action_str_array[BMC_THREAD_CMD__BMC_QUERY]      = "querying bmc root";
    bmcUtil_action_str_array[BMC_THREAD_CMD__BMC_INFO]       = "querying bmc info";
    bmcUtil_action_str_array[BMC_THREAD_CMD__POWER_STATUS]   = "querying power status";
    bmcUtil_action_str_array[BMC_THREAD_CMD__RESTART_CAUSE]  = "querying reset cause";
    bmcUtil_action_str_array[BMC_THREAD_CMD__BOOTDEV_PXE]    = "setting next boot dev";
    bmcUtil_action_str_array[BMC_THREAD_CMD__READ_SENSORS]   = "reading sensors";
    bmcUtil_action_str_array[BMC_THREAD_CMD__LAST]           = "unknown";

    return (PASS);
}


/*************************************************************************
 *
 * Name       : bmcUtil_info_init
 *
 * Purpose    : Initialize the BMC information struct.
 *
 * Returns    : nothing
 *
 *************************************************************************/

void bmcUtil_info_init ( bmc_info_type & bmc_info )
{
    bmc_info.manufacturer.clear();
    bmc_info.manufacturer_id.clear();
    bmc_info.product_name.clear();
    bmc_info.product_id.clear();
    bmc_info.device_id.clear();

    bmc_info.fw_version.clear();
    bmc_info.hw_version.clear();

    bmc_info.power_on = false ;
    bmc_info.restart_cause.clear() ;

    bmc_info.power_ctrl.reset.graceful     = "" ;
    bmc_info.power_ctrl.reset.immediate    = "" ;
    bmc_info.power_ctrl.poweron.graceful   = "" ;
    bmc_info.power_ctrl.poweron.immediate  = "" ;
    bmc_info.power_ctrl.poweroff.graceful  = "" ;
    bmc_info.power_ctrl.poweroff.immediate = "" ;
}

/*************************************************************************
 *
 * Name       : bmcUtil_create_pw_file
 *
 * Purpose    : Create a randomly named password filename
 *
 * Description: Create based on protocol.
 *              Add the password info to the file.
 *              Attach filename to thread info.
 *
 * Returns    : Error status passed through thread info status
 *              and status string
 *
 *************************************************************************/

void bmcUtil_create_pw_file ( thread_info_type * info_ptr,
                                        string   pw_file_content,
                             bmc_protocol_enum   protocol )
{
    string password_tempfile ;

    info_ptr->password_file.clear ();

    /* protocol specific output dir */
    if ( protocol == BMC_PROTOCOL__REDFISHTOOL )
        password_tempfile = REDFISHTOOL_OUTPUT_DIR ;
    else
        password_tempfile = IPMITOOL_OUTPUT_DIR ;

    password_tempfile.append(".") ;
    password_tempfile.append(program_invocation_short_name);
    password_tempfile.append("-");
    password_tempfile.append(info_ptr->hostname);
    password_tempfile.append("-");

    info_ptr->pw_file_fd = hostUtil_mktmpfile ( info_ptr->hostname,
                                                password_tempfile,
                                                info_ptr->password_file,
                                                pw_file_content );
    if ( info_ptr->pw_file_fd <= 0 )
    {
        info_ptr->status_string = "failed to get an open temporary password filedesc" ;
        info_ptr->status = FAIL_FILE_CREATE ;
        info_ptr->password_file.clear();
    }
    else
    {
        /* clean-up */
        if ( info_ptr->pw_file_fd > 0 )
            close(info_ptr->pw_file_fd);
        info_ptr->pw_file_fd = 0 ;
        info_ptr->status_string = "" ;
        info_ptr->status = PASS ;
    }
}


/*************************************************************************
 *
 * Name       : bmcUtil_create_data_fn
 *
 * Purpose    : Create a outout data filename
 *
 * Description: Create based on protocol.
 *
 * Returns    : datafile name as a string
 *
 *************************************************************************/

string bmcUtil_create_data_fn ( string & hostname,
                                string   file_suffix,
                     bmc_protocol_enum   protocol )
{
    /* create the output filename */
    string datafile ;

    /* protocol specific output dir */
    if ( protocol == BMC_PROTOCOL__REDFISHTOOL )
        datafile = REDFISHTOOL_OUTPUT_DIR ;
    else
        datafile = IPMITOOL_OUTPUT_DIR ;

    datafile.append(program_invocation_short_name);
    datafile.append("_");
    datafile.append(hostname);

    /* add the sensor list command */
    datafile.append(file_suffix);

    return ( datafile );
}

/*************************************************************************
 *
 * Name       : bmcUtil_is_power_on
 *
 * Purpose    : Get power state from query response data.
 *
 * Description: Parse a BMC protocol specific response for current
 *              power state.
 *
 * Assumptions: supplied power state is not changed on failure.
 *
 * Parameters : hostname - string
 *              protocol - BMC_PROTOCOL__REDFISHTOOL | BMC_PROTOCOL__IPMITOOL
 *              response - protocol specific power query response data
 *
 * Updates    : power_on - updated if response is queried ok
 *                         set true if power is on
 *                         set false if power is off
 *
 * Returns    : PASS or
 *              FAIL_NO_DATA , FAIL_JSON_PARSE
 *
 *************************************************************************/

int bmcUtil_is_power_on ( string   hostname,
               bmc_protocol_enum   protocol,
                          string & response,
                          bool   & power_on)
{
    if ( response.empty() )
    {
        wlog ("%s bmc power status query response empty",
                  hostname.c_str());
        return (FAIL_NO_DATA);
    }
    else if ( protocol == BMC_PROTOCOL__REDFISHTOOL )
    {
        struct json_object *json_obj = json_tokener_parse((char*)response.data());
        if ( !json_obj )
        {
            wlog ("%s failed to tokenize bmc info", hostname.c_str());
            return (FAIL_JSON_PARSE) ;
        }
        else if (tolowercase(jsonUtil_get_key_value_string(json_obj,REDFISH_LABEL__POWER_STATE)) == "on" )
            power_on = true ;
        else
            power_on = false ;

        /* free the json object */
        json_object_put(json_obj );
    }
    else /* IPMI */
    {
        if ( response.find (IPMITOOL_POWER_ON_STATUS) != std::string::npos )
            power_on = true ;
        else
            power_on = false ;
    }
    return (PASS);
}


/****************************************************************************
 *
 * Name       : bmcUtil_remove_files
 *
 * Purpose    : cleanup temp files.
 *
 * Description: Called during de-provision to remove temporary files created
 *              by host provisioning and command output.
 *
 *              Function detects which process is calling it and removes
 *              only the temp files that daemon created for a specific host.
 *
 * Assumptions: Keeps the temp dirs clean and current.
 *
 ****************************************************************************/

extern char *program_invocation_short_name;
void bmcUtil_remove_files ( string hostname, bmc_protocol_enum protocol )
{
    /* Read in the list of config files and their contents */

    std::list<string> filelist ;
    std::list<string>::iterator file_ptr ;

    string dir = BMC_OUTPUT_DIR ;
    dir.append(bmcUtil_getProtocol_str(protocol));

    int rc = load_filenames_in_dir ( dir.data(), filelist ) ;
    if ( rc )
        return ;

    /* files exist as <process>_<hostname>_<suffix> */
    if ( !strcmp(MTC_SERVICE_MTCAGENT_NAME, program_invocation_short_name ))
    {
        for ( file_ptr  = filelist.begin();
              file_ptr != filelist.end() ;
              file_ptr++ )
        {
             if ( file_ptr->find (program_invocation_short_name) != string::npos )
             {
                  if ( file_ptr->find (hostname) != string::npos )
                  {
                      daemon_remove_file ( file_ptr->data() );
                      blog2 ("%s %s removed", hostname.c_str(), file_ptr->c_str());
                  }
             }
        }
    }
    else if ( !strcmp(MTC_SERVICE_HWMOND_NAME, program_invocation_short_name ))
    {
        for ( file_ptr  = filelist.begin();
              file_ptr != filelist.end() ;
              file_ptr++ )
        {
             if ( file_ptr->find (program_invocation_short_name) != string::npos )
             {
                  if ( file_ptr->find (hostname) != string::npos )
                  {
                      daemon_remove_file ( file_ptr->data() );
                      blog2 ("%s %s removed", hostname.c_str(), file_ptr->c_str());
                  }
             }
        }
    }
}
