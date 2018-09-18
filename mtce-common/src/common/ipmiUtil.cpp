/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 *
 *
 * @file
 * Wind River Titanium Cloud Common IPMI Utilities
 */
#include <stdio.h>
#include <iostream>
#include <string.h>

using namespace std;

#include "nodeBase.h"      /* for ... mtce node common definitions     */
#include "hostUtil.h"      /* for ... mtce host common definitions     */
#include "ipmiUtil.h"      /* for ... this module header               */

/* Create a randomly named password filename */
void ipmiUtil_create_pw_fn ( thread_info_type * info_ptr, string pw )
{
    info_ptr->password_file.clear ();
    string password_tempfile = IPMITOOL_OUTPUT_DIR ;
    password_tempfile.append(".") ;
    password_tempfile.append(program_invocation_short_name);
    password_tempfile.append("-");
    password_tempfile.append(info_ptr->hostname);
    password_tempfile.append("-");

    info_ptr->pw_file_fd = hostUtil_mktmpfile (info_ptr->hostname,
                                                password_tempfile,
                                                info_ptr->password_file,
                                                pw );
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
    }
}

/* Create the ipmitool output_filename */
string ipmiUtil_create_data_fn ( string & hostname, string file_suffix )
{
    /* create the output filename */
    string ipmitool_datafile = IPMITOOL_OUTPUT_DIR ;
    ipmitool_datafile.append(program_invocation_short_name);
    ipmitool_datafile.append("_");
    ipmitool_datafile.append(hostname);

    /* add the sensor list command */
    ipmitool_datafile.append(file_suffix);

    return ( ipmitool_datafile );
}

/* Create the ipmi request */
string ipmiUtil_create_request ( string cmd, string & ip, string & un, string & pw, string & out )
{
    /* ipmitool -I lanplus -H $uut_ip -U $uut_un -E */
    /* build the ipmitool command */
    string ipmitool_request = IPMITOOL_PATH_AND_FILENAME ;

    /* Specify lanplus network mode for centralized power control, 1 retry
     * followed by the bm ip address and password file */
    ipmitool_request.append(" -I lanplus -R 1 -H ");
    ipmitool_request.append(ip);

    /* then specify the bmc username */
    ipmitool_request.append(" -U ");
    ipmitool_request.append(un);

    if ( daemon_is_file_present ( MTC_CMD_FIT__LOUD_BM_PW ) == true )
    {
        /* get the password from the file and put it on the command line */
        ipmitool_request.append(" -P ");
        ipmitool_request.append(daemon_get_file_str(pw.data()));
    }
    else
    {
        /* add the password file option and file */
        ipmitool_request.append(" -f ");
        ipmitool_request.append(pw);
    }

    /* add the command */
    ipmitool_request.append(" ");
    ipmitool_request.append(cmd);

    /* output filename */
    ipmitool_request.append (" > ");
    ipmitool_request.append (out);

    return (ipmitool_request);
}

/* init the mc info struct */
void ipmiUtil_mc_info_init ( mc_info_type & mc_info )
{
    mc_info.device_id.clear();
    mc_info.manufacturer_name.clear();
    mc_info.manufacturer_id.clear();
    mc_info.product_name.clear();
    mc_info.product_id.clear();
    mc_info.fw_version.clear();
    mc_info.hw_version.clear();
}

/* print a log of the mc info data */
void mc_info_log ( string hostname, mc_info_type & mc_info, int rc )
{
    if ( rc )
    {
        elog ("%s mc info load failed (rc:%d)\n", hostname.c_str(), rc );
    }
    else
    {
        ilog ("%s Manufacturer: %s [id:%s] [ Device: %s  ver %s ]\n",
                  hostname.c_str(),
                  mc_info.manufacturer_name.c_str(),
                  mc_info.manufacturer_id.c_str(),
                  mc_info.device_id.c_str(),
                  mc_info.hw_version.c_str());

        ilog ("%s Product Name: %s [id:%s] [ BMC FW: ver %s ]\n",
                  hostname.c_str(),
                  mc_info.product_name.c_str(),
                  mc_info.product_id.c_str(),
                  mc_info.fw_version.c_str());
    }
}

/* load the specified key value in buffer line into 'value' */
bool _got_delimited_value ( char * buf_ptr, const char * key, const char * delimiter, string & value )
{
    if ( strstr ( buf_ptr, key ))
    {
        string _str = buf_ptr ;
        if ( _str.find(key) != std::string::npos )
        {
            if ( _str.find( delimiter ) != std::string::npos )
            {
                int y = _str.find( delimiter ) ;
                value = _str.substr ( y+strlen(delimiter), std::string::npos) ;
                value.erase ( value.size()-1, std::string::npos ) ;
                return (true);
            }
        }
    }
    return (false);
}

/*****************************************************************************
 *
 * Name       : ipmiUtil_mc_info_load
 *
 * Description: Load the contents of a file containing an ipmitool formatted
 *              output from an mc info request into the passed in mc_info
 *              struct. Loaded info includes
 *
 *              Manufacturer (id/name)
 *              Product      (id/name)
 *              Device       (id/version)
 *              Firmware     (version)
 *
 * A log like the following is generated.
 *
 * controller-0 mc info: Nokia:7244 - Quanta:12866 (0x3242) [bmc fw:3.29] [device:32 ver:1]
 *
 * Example MC Info output from ipmitool
 *
 *     Device ID                 : 32
 *     Device Revision           : 1
 *     Firmware Revision         : 3.29
 *     IPMI Version              : 2.0
 *     Manufacturer ID           : 7244
 *     Manufacturer Name         : Nokia
 *     Product ID                : 12866 (0x3242)
 *     Product Name              : Quanta
 *     Device Available          : yes
 *     Provides Device SDRs      : no
 *     Additional Device Support :
 *         Sensor Device
 *         SDR Repository Device
 *         SEL Device
 *         FRU Inventory Device
 *         Chassis Device
 *
 **************************************************************************/

#define BUFFER (80)
int ipmiUtil_mc_info_load ( string hostname, const char * filename, mc_info_type & mc_info )
{
    int rc = FAIL ;
    ipmiUtil_mc_info_init ( mc_info );
    if ( daemon_is_file_present ( filename ) )
    {
        FILE * _stream = fopen ( filename, "r" );
        if ( _stream )
        {
            char buffer [BUFFER];
            MEMSET_ZERO(buffer);
            while ( fgets (buffer, BUFFER, _stream) )
            {
                if ( _got_delimited_value ( buffer, MC_INFO_LABEL_FW_VERSION, MC_INFO_LABEL_DELIMITER, mc_info.fw_version ))
                {
                    rc = PASS ;
                    continue;
                }
                if ( _got_delimited_value ( buffer, MC_INFO_LABEL_HW_VERSION, MC_INFO_LABEL_DELIMITER, mc_info.hw_version ))
                    continue;
                if ( _got_delimited_value ( buffer, MC_INFO_LABEL_DEVICE_ID, MC_INFO_LABEL_DELIMITER, mc_info.device_id ))
                    continue;
                if ( _got_delimited_value ( buffer, MC_INFO_LABEL_PRODUCT_ID, MC_INFO_LABEL_DELIMITER, mc_info.product_id ))
                    continue;
                if ( _got_delimited_value ( buffer, MC_INFO_LABEL_PRODUCT_NAME, MC_INFO_LABEL_DELIMITER, mc_info.product_name ))
                    continue;
                if ( _got_delimited_value ( buffer, MC_INFO_LABEL_MANUFACTURE_ID, MC_INFO_LABEL_DELIMITER, mc_info.manufacturer_id ))
                    continue;
                if ( _got_delimited_value ( buffer, MC_INFO_LABEL_MANUFACTURE_NAME, MC_INFO_LABEL_DELIMITER, mc_info.manufacturer_name ))
                    continue;
                else
                    blog3 ("buffer: %s\n", buffer );
                MEMSET_ZERO(buffer);
            }
            fclose(_stream);
        }
    }
    else
    {
        elog ("%s failed to open mc info file '%s'\n", hostname.c_str(), filename);
        rc = FAIL_FILE_ACCESS ;
    }

    mc_info_log ( hostname, mc_info, rc );
    return (rc);
}
