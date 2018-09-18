/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River Titanium Cloud Platform remote logging Monitor Handler
 */

#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <list>
#include <map>
#include <syslog.h>
#include <sys/wait.h>
#include <time.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <vector>        /* for storing dynamic resource names */
#include <dirent.h>
#include <signal.h>
#include "rmon.h"        /* rmon header file */
#include "rmonHttp.h"    /* for rmon HTTP libEvent utilties */
#include "rmonApi.h"
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cctype>
#include <pthread.h>
#include <linux/rtnetlink.h> /* for ... RTMGRP_LINK */
#include "nlEvent.h"       /* for ... open_netlink_socket */
#include "nodeEvent.h"     /* for inotify */
#include <json-c/json.h>   /* for ... json-c json string parsing */
#include "jsonUtil.h"
#include <cstring>
#include <iomanip>
#include <cstdlib>

static libEvent_type remoteLoggingAudit;  // for system remotelogging-show

static inline SFmAlarmDataT
create_remoteLogging_controller_connectivity_alarm (SFmAlarmDataT data,
                                AlarmFilter filter)
{
    snprintf (data.alarm_id, FM_MAX_BUFFER_LENGTH, "%s",
              filter.alarm_id);
    data.alarm_state = FM_ALARM_STATE_SET;
    snprintf(data.entity_type_id, FM_MAX_BUFFER_LENGTH, "system.host");
    snprintf(data.entity_instance_id, FM_MAX_BUFFER_LENGTH, "%s",
             filter.entity_instance_id);
    data.severity = FM_ALARM_SEVERITY_MINOR;
    snprintf(data.reason_text, sizeof(data.reason_text),
              "Controller cannot establish connection with remote logging server.");
    data.alarm_type = FM_ALARM_COMM;
    data.probable_cause = FM_ALARM_COMM_SUBSYS_FAILURE;
    data.service_affecting = FM_FALSE;
    snprintf(data.proposed_repair_action, sizeof(data.proposed_repair_action),
             "Ensure Remote Log Server IP is reachable from Controller through "
             "OAM interface; otherwise contact next level of support.");
    return data;
}

// alarm data for all remote loggin alarms
static SFmAlarmDataT alarmData;

int rmonHdlr_remotelogging_query (resource_config_type * ptr);

// this is used to create a buffer to store output from a command
// that gets the connection status of a port.
// the command filters the /proc/net/tcp(udp) files leaving only the status
// you generally expect a 1 character integer value for the status
#define CONNECTION_STATUS_COMMAND_OUTPUT_BUFFER_SIZE 8

/*****************************************************************************
 *
 * Name    : rmonHdlr_remotelogging_handler
 *
 * Purpose : Handles the remote logging response message
 *
 *****************************************************************************/
void rmonHdlr_remotelogging_handler ( struct evhttp_request *req, void *arg )
{

    if ( !req )
    {
        elog (" Request Timeout\n");
        remoteLoggingAudit.status = FAIL_TIMEOUT ;
        goto  _remote_logging_handler_done ;
    }

    remoteLoggingAudit.status = rmonHttpUtil_status ( remoteLoggingAudit ) ;
    if ( remoteLoggingAudit.status == HTTP_NOTFOUND )
    {

        goto  _remote_logging_handler_done ;
    }
    else if ( remoteLoggingAudit.status != PASS )
    {
        dlog (" remote logging HTTP Request Failed (%d)\n",
                remoteLoggingAudit.status);

        goto  _remote_logging_handler_done ;
    }

    if ( rmonHttpUtil_get_response ( remoteLoggingAudit ) != PASS )
        goto _remote_logging_handler_done ;

_remote_logging_handler_done:
    /* This is needed to get out of the loop */
    event_base_loopbreak((struct event_base *)arg);
}

/*****************************************************************************
 *
 * Name    : rmonHdlr_remotelogging_query
 *
 * Purpose : Send a HTTP remotelogging show request
 *
 *****************************************************************************/

int rmonHdlr_remotelogging_query (resource_config_type * ptr)
{

    // we want this handler to run once every 5 minutes
    // rmon currently runs once every 30 seconds
    static bool first_execution = true;
    static int exec_counter = 9;
    exec_counter = (exec_counter + 1) % 10;
    if(exec_counter != 0)
    {
        return 0;
    }
    // extract the ip and port for the remote logging server
    FILE* pFile;
    string remote_ip_address = "";
    string remote_port = "";
    string transport_type = "";
    string line;
    bool feature_enabled = false;

    std::ifstream syslog_config("/etc/syslog-ng/syslog-ng.conf");
    // look for this line in the config file:
    // destination remote_log_server  {tcp("128.224.140.219" port(514));};
    while(std::getline(syslog_config, line))
    {
        // include remotelogging.conf is present if the feature is enabled
        if(line.find("@include \"remotelogging.conf\"") == 0)
        {
            feature_enabled = true;
        }
        if(line.find("destination remote_log_server") != 0)
        {
            continue;
        }
        int start = line.find("{") + 1;
        int end = line.find("(", start + 1);
        transport_type= line.substr(start, end - start);
        start = line.find("\"") + 1;
        end = line.find("\"", start + 1);
        remote_ip_address = line.substr(start, end - start);
        start = line.find("port(") + 5;
        end = line.find(")", start + 1);
        remote_port = line.substr(start, end - start);
    }

    syslog_config.close();

    // cleanup of any alarms if the remotelogging feature is not enabled
    // this is important for when users turn off the remote logging feature when an alarm is active
    // if the line containing this information is not in config, remote logging is not used
    if(remote_ip_address.empty() || remote_port.empty() || transport_type.empty() || !feature_enabled)
    {
        // currently, only controllers raise alarms
        if(is_controller())
        {
            // clear any raised alarms
            if(ptr->alarm_raised)
            {
                rmon_ctrl_type* _rmon_ctrl_ptr;
                _rmon_ctrl_ptr = get_rmon_ctrl_ptr();
                AlarmFilter alarmFilter;
                snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, REMOTE_LOGGING_CONTROLLER_CONNECTIVITY_ALARM_ID);
                snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH,
                         "%s", _rmon_ctrl_ptr->my_hostname);
                int rc;
                if ((rc = rmon_fm_clear(&alarmFilter)) != FM_ERR_OK)
                {
                    wlog ("Failed to clear stale remotelogging connectivity alarm for"
                          "entity instance id: %s error: %d",
                          alarmFilter.entity_instance_id, rc);
                }
                else
                {
                    ptr->alarm_raised = false;
                }
            }
        }
        return 0;
    }

    // construct the remote logging server IP string
    // the files being looked at(/proc/net/tcp(udp)) uses hex values, so convert the
    // string we got from the config file to that format
    // - convert all numbers to hex and hex to capitals
    // reverse ordering of the "ipv4" values
    std::stringstream config_ip(remote_ip_address); // the ip string from the config file
    std::stringstream proc_file_ip; // the ip string formatted to compare to /proc/net/tcp(udp)
    int ipv = 4;

    // IPv4
    if(remote_ip_address.find(".") != string::npos)
    {
        // ipv4 example: config file 10.10.10.45, /proc/net/tcp 2D0A0A0A
        int a, b, c, d;
        char trash;
        config_ip >> a >> trash >> b >> trash >> c >> trash >> d;
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << d;
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << c;
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << b;
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << a;
        proc_file_ip << ":";
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << atoi(remote_port.c_str());
    }
    // IPv6
    else if (remote_ip_address.find(":") != string::npos)
    {
        ipv = 6;
        // ipv6 example: config file 0:0:0:0:ffff:0:80e0:8d6c , /proc/net/tcp6 0000000000000000FFFF00006C8D0E080
        int a, b, c, d;
        char trash;
        // first, the hex that are in the same order from config to /proc/net/...
        for(int i = 0; i < 6; i++)
        {
            config_ip >> std::hex >> a >> trash;
            proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << a;
        }

        // now the hex that needs to be re ordered
        config_ip >> std::hex >> a >> trash >> c;
        b = (a & 0xFF);
        a = (a >> 8);
        d = (c & 0xFF);
        c = (c >> 8);

        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << d;
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << c;
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << b;
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << a;
        proc_file_ip << ":";
        proc_file_ip << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << atoi(remote_port.c_str());
    }
    // garbage
    else
    {
        wlog("Unrecognized ip format in syslog config file\n");
    }

    string connection_check_filename;
    if(transport_type == "tcp")
    {
        connection_check_filename = "tcp";
    }
    else if (transport_type == "udp")
    {
        connection_check_filename = "udp";
    }
    // todo: eventually we will have TLS as a transport type and potentially others
    else
    {
        wlog("Unrecognized remote logging transport type: %s \n", transport_type.c_str());
    }

    if(ipv == 6)
    {
        connection_check_filename = connection_check_filename + "6";
    }

    std::string command = "cat /proc/net/" + connection_check_filename +" | awk '{print $3 \" \" $4}' | grep " \
     + proc_file_ip.str() + " | awk '{print $2}'";
    if(!(pFile = popen(command.c_str(), "r")))
    {
        elog ("Failed to execute command for getting remotelogging tcp port status");
    }
    else
    {
        char cmd_output[CONNECTION_STATUS_COMMAND_OUTPUT_BUFFER_SIZE];
        int connection_status = 0;
        rmon_ctrl_type* _rmon_ctrl_ptr;
        _rmon_ctrl_ptr = get_rmon_ctrl_ptr();
        AlarmFilter alarmFilter;
        SFmAlarmDataT active_alarm;

        memset(cmd_output, 0, CONNECTION_STATUS_COMMAND_OUTPUT_BUFFER_SIZE);
        fgets((char*) &cmd_output, CONNECTION_STATUS_COMMAND_OUTPUT_BUFFER_SIZE, pFile);
        pclose(pFile);
        std::stringstream s(cmd_output);
        s >> std::hex >> connection_status;

        snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, REMOTE_LOGGING_CONTROLLER_CONNECTIVITY_ALARM_ID);
        snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH,
                 "%s", _rmon_ctrl_ptr->my_hostname);

        if(first_execution)
        {
            if (fm_get_fault (&alarmFilter, &active_alarm) == FM_ERR_OK)
            {
                ptr->alarm_raised = true;
            }
            else
            {
                ptr->alarm_raised = false;
            }
        }
        if(connection_status == 1)
        {
            if(is_controller())
            {
                // connection is established, clear the alarm
                if(ptr->alarm_raised)
                {
                    int rc;
                    if ((rc = rmon_fm_clear(&alarmFilter)) != FM_ERR_OK)
                    {
                        wlog ("Failed to clear stale remotelogging connectivity alarm for"
                              "entity instance id: %s error: %d",
                              alarmFilter.entity_instance_id, rc);
                    }
                    else
                    {
                        ptr->alarm_raised = false;
                    }
                }
            }
        }
        else
        {
            if(is_controller())
            {
                // connection is not established, raise an alarm
                if (!ptr->alarm_raised)
                {
                    int rc;
                    alarmData = \
                        create_remoteLogging_controller_connectivity_alarm(alarmData,
                                                                     alarmFilter);

                    if ((rc = rmon_fm_set(&alarmData, NULL)) != FM_ERR_OK)
                    {
                        wlog("Failed to create alarm %s for entity instance id: %s"
                             "error: %d \n", REMOTE_LOGGING_CONTROLLER_CONNECTIVITY_ALARM_ID,
                             alarmData.entity_instance_id, (int) rc);
                    }
                    else
                    {
                        ptr->alarm_raised = true;
                    }
                }
            }
            else
            {
                elog ("%s cannot connect to remote log server", _rmon_ctrl_ptr->my_hostname);
            }
        }
    }

    return 0;
}

