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
#include <json-c/json.h>   /* for ... json-c json string parsing */

using namespace std;

#include "nodeBase.h"      /* for ... mtce node common definitions     */
#include "nodeUtil.h"      /* for ... tolowercase                      */
#include "hostUtil.h"      /* for ... mtce host common definitions     */
#include "jsonUtil.h"      /* for ...      */
#include "redfishUtil.h"   /* for ... this module header               */

/* static prioritized list of redfish <named> actions.
 * Higher priority action first. */
static std::list<string> reset_actions ;
static std::list<string> poweron_actions ;
static std::list<string> poweroff_actions ;

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

    /* Stock reset actions in order of priority */
    reset_actions.push_front(REDFISHTOOL_RESET__GRACEFUL_RESTART); /* P1 */
    reset_actions.push_back (REDFISHTOOL_RESET__FORCE_RESTART);    /* P2 */

    poweron_actions.push_front(REDFISHTOOL_POWER_ON__ON);
    poweron_actions.push_back (REDFISHTOOL_POWER_ON__FORCE_ON);

    poweroff_actions.push_front(REDFISHTOOL_POWER_OFF__GRACEFUL_SHUTDOWN);
    poweroff_actions.push_back (REDFISHTOOL_POWER_OFF__FORCE_OFF);

    return (PASS);
}

/*************************************************************************
 *
 * Name       : _load_action_lists
 *
 * Purpose    : Load supported host actions.
 *
 * Description: Filter stock actions through host actions.
 *
 * Parameters : hostname         - this host amer
 *              host_action_list - what actions this host reports support for.
 *
 * Updates:     bmc_info         - reference that includes host action lists
 *
 *************************************************************************/

void _load_action_lists (        string & hostname,
                          bmc_info_type & bmc_info,
                      std::list<string> & host_action_list)
{
    bmc_info.reset_action_list.clear();
    bmc_info.power_on_action_list.clear();
    bmc_info.power_off_action_list.clear();

    /* Walk through the host action list looking for and updating
     * this host's bmc_info supported actions lists */
    std::list<string>::iterator _host_action_list_ptr ;
    for ( _host_action_list_ptr  = host_action_list.begin();
          _host_action_list_ptr != host_action_list.end() ;
          _host_action_list_ptr++ )
    {
        std::list<string>::iterator _action_list_ptr ;
        for ( _action_list_ptr  = poweroff_actions.begin();
              _action_list_ptr != poweroff_actions.end() ;
              _action_list_ptr++ )
        {
            if ( (*_host_action_list_ptr) == (*_action_list_ptr) )
            {
                bmc_info.power_off_action_list.push_back(*_action_list_ptr) ;
                break ;
            }
        }
        for ( _action_list_ptr  = poweron_actions.begin();
              _action_list_ptr != poweron_actions.end() ;
              _action_list_ptr++ )
        {
            if ( (*_host_action_list_ptr) == (*_action_list_ptr) )
            {
                bmc_info.power_on_action_list.push_back(*_action_list_ptr) ;
                break ;
            }
        }
        for ( _action_list_ptr  = reset_actions.begin();
              _action_list_ptr != reset_actions.end() ;
              _action_list_ptr++ )
        {
            if ( (*_host_action_list_ptr) == (*_action_list_ptr) )
            {
                bmc_info.reset_action_list.push_back(*_action_list_ptr) ;
                break ;
            }
        }
    }
    string reset_tmp = "" ;
    string poweron_tmp = "" ;
    string poweroff_tmp = "" ;
    std::list<string>::iterator _ptr ;
    for ( _ptr  = bmc_info.reset_action_list.begin();
          _ptr != bmc_info.reset_action_list.end() ;
          _ptr++ )
    {
        if ( !reset_tmp.empty() )
            reset_tmp.append(",");
        reset_tmp.append(*_ptr);
    }
    for ( _ptr  = bmc_info.power_on_action_list.begin();
          _ptr != bmc_info.power_on_action_list.end() ;
          _ptr++ )
    {
        if ( !poweron_tmp.empty() )
            poweron_tmp.append(",");
        poweron_tmp.append(*_ptr);
    }
    for ( _ptr  = bmc_info.power_off_action_list.begin();
          _ptr != bmc_info.power_off_action_list.end() ;
          _ptr++ )
    {
        if ( !poweroff_tmp.empty() )
            poweroff_tmp.append(",");
        poweroff_tmp.append(*_ptr);
    }
    ilog ("%s bmc actions ; reset:%s  power-on:%s  power-off:%s",
              hostname.c_str(),
              reset_tmp.empty() ? "none" : reset_tmp.c_str(),
              poweron_tmp.empty() ? "none" : poweron_tmp.c_str(),
              poweroff_tmp.empty() ? "none" : poweroff_tmp.c_str());
}

#ifdef SAVE_IMP
int _get_action_list (              string   hostname,
                       redfish_action_enum   action,
                       std::list<string>     host_action_list,
                       std::list<string>   & supp_action_list)
{
    int status = PASS ;
    std::list<string> * action_ptr = NULL ;
    string action_str = "" ;
    supp_action_list.clear();
    switch ( action )
    {
        case REDFISH_ACTION__RESET:
        {
            action_ptr = &reset_actions ;
            action_str = "reset" ;
            break ;
        }
        case REDFISH_ACTION__POWER_ON:
        {
            action_ptr = &poweron_actions ;
            action_str = "power-on" ;
            break ;
        }
        case REDFISH_ACTION__POWER_OFF:
        {
            action_ptr = &poweroff_actions ;
            action_str = "power-off" ;
            break ;
        }
        default:
        {
            status = FAIL_BAD_CASE ;
        }
    }

    /* Filter */
    if (( status == PASS ) && (action_ptr))
    {
        /* get the best supported action command
         * for the specified action group. */
        std::list<string>::iterator _action_list_ptr ;
        std::list<string>::iterator _host_action_list_ptr ;
        for ( _action_list_ptr  = action_ptr->begin();
              _action_list_ptr != action_ptr->end() ;
              _action_list_ptr++ )
        {
            for ( _host_action_list_ptr  = host_action_list.begin();
                  _host_action_list_ptr != host_action_list.end() ;
                  _host_action_list_ptr++ )
            {
                if ( (*_host_action_list_ptr) == (*_action_list_ptr) )
                {
                    supp_action_list.push_back(*_action_list_ptr) ;
                    break ;
                }
            }
        }
    }
    if ( supp_action_list.empty() )
    {
        elog ("%s has no %s actions", hostname.c_str(), action_str.c_str());
        if ( status == PASS )
            status = FAIL_STRING_EMPTY ;
    }
    else
    {
        string tmp = "" ;
        std::list<string>::iterator _ptr ;
        for ( _ptr  = supp_action_list.begin();
              _ptr != supp_action_list.end() ;
              _ptr++ )
        {
            if ( !tmp.empty() )
                tmp.append(", ");
            tmp.append(*_ptr);
        }
        ilog ("%s redfish %s actions: %s",
                  hostname.c_str(),
                  action_str.c_str(),
                  tmp.c_str());
    }
    return (status);
}
#endif

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
                if ( fields )
                {
                    if (( major >= REDFISH_MIN_MAJOR_VERSION ) && ( minor >= REDFISH_MIN_MINOR_VERSION ))
                    {
                        ilog ("%s bmc supports redfish version %s",
                                  hostname.c_str(),
                                  redfish_version.c_str());
                        return true ;
                    }
                    else
                    {
                        ilog ("%s bmc redfish version '%s' is below minimum baseline %d.%d.x (%d:%d.%d.%d)",
                                  hostname.c_str(),
                                  redfish_version.c_str(),
                                  REDFISH_MIN_MAJOR_VERSION,
                                  REDFISH_MIN_MINOR_VERSION,
                                  fields, major, minor, revision);
                    }
                }
                else
                {
                    wlog ("%s failed to parse redfish version %s",
                              hostname.c_str(),
                              redfish_version.c_str());
                    blog ("%s response: %s",
                              hostname.c_str(),
                              response.c_str());
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

    /* redfishtool default timeout is 10 seconds.
     * Seeing requests that are taking a little longer than that.
     * defaulting to 20 sec timeout */
    command_request.append(" -T 30");

    /* specify the bmc ip address */
    command_request.append(" -r ");
    command_request.append(ip);

#ifdef WANT_INLINE_CREDS
    if ( daemon_is_file_present ( MTC_CMD_FIT__INLINE_CREDS ) )
    {
        string cfg_str = daemon_read_file (config_file.data());
        struct json_object *_obj = json_tokener_parse( cfg_str.data() );
        if ( _obj )
        {
            command_request.append(" -u ");
            command_request.append(jsonUtil_get_key_value_string(_obj,"username"));
            command_request.append(" -p ");
            command_request.append(jsonUtil_get_key_value_string(_obj,"password"));
        }
        else
        {
            slog("FIT: failed to get creds from config file");
        }
    }
    else
#endif
    {
        /* add the config file option and config filename */
        command_request.append(" -c ");
        command_request.append(config_file);
    }

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
 * Name       : redfishUtil_health_info
 *
 * Purpose    : Parse the supplied object.
 *
 * Description: Update callers health state, health and health_rollup
 *              variables with what is contained in the supplied object.
 *
 *       "Status": {
 *           "HealthRollup": "OK",
 *           "State": "Enabled",
 *           "Health": "OK"
 *       },
 *
 * Assumptions: Status label must be a first order label.
 *              This utility does nto walk the object looking for status.
 *
 * Returns    : PASS if succesful
 *              FAIL_OPERATION if unsuccessful
 *
 ************************************************************************/

int redfishUtil_health_info (             string & hostname,
                                          string   entity,
                              struct json_object * info_obj,
                           redfish_entity_status & status )
{
    if ( info_obj )
    {
        struct json_object *status_obj = (struct json_object *)(NULL);
        json_bool json_rc = json_object_object_get_ex( info_obj,
                                                      REDFISH_LABEL__STATUS,
                                                      &status_obj );
        if (( json_rc == TRUE ) && ( status_obj ))
        {
            status.state         = jsonUtil_get_key_value_string( status_obj,
                                   REDFISH_LABEL__STATE );
            status.health        = jsonUtil_get_key_value_string( status_obj,
                                   REDFISH_LABEL__HEALTH );
            status.health_rollup = jsonUtil_get_key_value_string( status_obj,
                                   REDFISH_LABEL__HEALTHROLLUP );
            return (PASS);
        }
    }
    wlog ("%s unable to get %s state and health info",
              hostname.c_str(), entity.c_str());

    status.state = UNKNOWN ;
    status.health = UNKNOWN ;
    status.health_rollup = UNKNOWN ;
    return (FAIL_OPERATION);
}

/*************************************************************************
 *
 * Name       : redfishUtil_get_bmc_info
 *
 * Purpose    : Parse the Systems get output
 *
 * Description: Log all important BMC server info such as processors, memory,
 *              model number, firmware version, hardware part number, etc.
 *
 * Returns    : PASS if succesful
 *              FAIL_OPERATION if unsuccessful
 *
 ************************************************************************/

int redfishUtil_get_bmc_info ( string & hostname,
                               string & bmc_info_filename,
                               bmc_info_type & bmc_info )
{
#ifdef WANT_FIT_TESTING
    if ( daemon_is_file_present ( MTC_CMD_FIT__MEM_LEAK_DEBUG ))
        return (PASS) ;
#endif

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

    /* load the power state */
    string power_state = tolowercase(jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__POWER_STATE));
    if ( power_state == "on" )
        bmc_info.power_on = true ;
    else
        bmc_info.power_on = false ;
    ilog ("%s power is %s", hostname.c_str(), power_state.c_str());

    bmc_info.manufacturer  = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__MANUFACTURER );
    bmc_info.sn            = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__SERIAL_NUMBER);
    bmc_info.mn            = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__MODEL_NUMBER );
    bmc_info.pn            = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__PART_NUMBER  );
    ilog ("%s manufacturer is %s ; model:%s  part:%s  serial:%s ",
              hostname.c_str(),
              bmc_info.manufacturer.c_str(),
              bmc_info.mn.c_str(),
              bmc_info.pn.c_str(),
              bmc_info.sn.c_str());

    bmc_info.bios_ver = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__BIOS_VERSION );
    if (( !bmc_info.bios_ver.empty() ) && ( bmc_info.bios_ver != NONE ))
    {
        ilog ("%s BIOS fw version %s",
                  hostname.c_str(),
                  bmc_info.bios_ver.c_str());
    }

    bmc_info.bmc_ver = jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__BMC_VERSION  );
    if (( !bmc_info.bmc_ver.empty() ) && ( bmc_info.bmc_ver != NONE ))
    {
        ilog ("%s BMC  fw version %s",
                  hostname.c_str(),
                  bmc_info.bmc_ver.c_str());
    }

    struct json_object *json_obj_actions;
    if ( json_object_object_get_ex(json_obj, REDFISH_LABEL__ACTIONS, &json_obj_actions ))
    {
        std::list<string> action_list ;

        /* get the first level reset action label content */
        string json_actions =
        jsonUtil_get_key_value_string (json_obj_actions,
                                       REDFISHTOOL_RESET_ACTIONS_LABEL);

        if ( jsonUtil_get_list ((char*)json_actions.data(), REDFISHTOOL_RESET_ACTIONS_ALLOWED_LABEL, action_list ) == PASS )
        {
             _load_action_lists ( hostname, bmc_info, action_list);
        }
        else
        {
             elog ("%s actions list get failed ; [%s]", hostname.c_str(), json_actions.c_str());
        }
    }
    else
    {
        elog ("%s action object get failed", hostname.c_str());
    }

    /* get number of processors */
    struct json_object *proc_obj = (struct json_object *)(NULL);
    json_bool json_rc = json_object_object_get_ex( json_obj,
                                                   REDFISH_LABEL__PROCESSOR,
                                                  &proc_obj );
    if (( json_rc == TRUE ) && ( proc_obj ))
    {
        redfish_entity_status status ;
        bmc_info.processors = jsonUtil_get_key_value_int ( proc_obj, REDFISH_LABEL__COUNT );
        redfishUtil_health_info ( hostname, REDFISH_LABEL__PROCESSOR,
                                  proc_obj, status) ;
        ilog ("%s has %2d Processors ; %s and %s:%s",
                  hostname.c_str(),
                  bmc_info.processors,
                  status.state.c_str(),
                  status.health.c_str(),
                  status.health_rollup.c_str());
    }
    else
    {
         wlog ("%s processor object not found", hostname.c_str());
    }

    /* get amount of memory */
    struct json_object *mem_obj = (struct json_object *)(NULL);
    json_rc = json_object_object_get_ex( json_obj,
                                         REDFISH_LABEL__MEMORY,
                                        &mem_obj );
    if (( json_rc == TRUE ) && ( mem_obj ))
    {
        redfish_entity_status status ;
        bmc_info.memory_in_gigs = jsonUtil_get_key_value_int ( mem_obj, REDFISH_LABEL__MEMORY_TOTAL );
        redfishUtil_health_info ( hostname, REDFISH_LABEL__MEMORY,
                                  mem_obj, status) ;
        ilog ("%s has %d GiB Memory ; %s and %s:%s",
                  hostname.c_str(),
                  bmc_info.memory_in_gigs,
                  status.state.c_str(),
                  status.health.c_str(),
                  status.health_rollup.c_str() );
    }
    else
    {
        wlog ("%s memory object not found", hostname.c_str());
    }

    json_object_put(json_obj );

    return (PASS) ;
}
