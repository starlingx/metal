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

#include "ipmiUtil.h"      /* for ... module header                    */
#include "hostUtil.h"      /* for ... mtce host common definitions     */

/***********************************************************************
 *
 * Name       : ipmiUtil_init
 *
 * Purpose    : Module init
 *
 * Description: Performs the following functions
 *
 *              1. creates the ipmitool runtime temp dir
 *
 ***********************************************************************/
int ipmiUtil_init ( void )
{
    daemon_make_dir(IPMITOOL_OUTPUT_DIR) ;
    return(PASS);
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

/* print a log of the mc info data */
void ipmiUtil_bmc_info_log ( string hostname, bmc_info_type & bmc_info, int rc )
{
    if ( rc )
    {
        elog ("%s mc info load failed (rc:%d)\n", hostname.c_str(), rc );
    }
    else
    {
        ilog ("%s manufacturer: %s [id:%s] [ Device: %s  ver %s ]\n",
                  hostname.c_str(),
                  bmc_info.manufacturer.c_str(),
                  bmc_info.manufacturer_id.c_str(),
                  bmc_info.device_id.c_str(),
                  bmc_info.hw_version.c_str());

        ilog ("%s product name: %s [id:%s] [ BMC FW: ver %s ]\n",
                  hostname.c_str(),
                  bmc_info.product_name.c_str(),
                  bmc_info.product_id.c_str(),
                  bmc_info.fw_version.c_str());
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
 * Name       : ipmiUtil_bmc_info_load
 *
 * Description: Load the contents of a file containing an ipmitool formatted
 *              output from an mc info request into the passed in bmc_info
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
int ipmiUtil_bmc_info_load ( string hostname, const char * filename, bmc_info_type & bmc_info )
{
    int rc = FAIL ;
    if ( daemon_is_file_present ( filename ) )
    {
        FILE * _stream = fopen ( filename, "r" );
        if ( _stream )
        {
            char buffer [BUFFER];
            MEMSET_ZERO(buffer);
            while ( fgets (buffer, BUFFER, _stream) )
            {
                if ( _got_delimited_value ( buffer, BMC_INFO_LABEL_FW_VERSION, BMC_INFO_LABEL_DELIMITER, bmc_info.fw_version ))
                {
                    rc = PASS ;
                    continue;
                }
                if ( _got_delimited_value ( buffer, BMC_INFO_LABEL_HW_VERSION, BMC_INFO_LABEL_DELIMITER, bmc_info.hw_version ))
                    continue;
                if ( _got_delimited_value ( buffer, BMC_INFO_LABEL_DEVICE_ID, BMC_INFO_LABEL_DELIMITER, bmc_info.device_id ))
                    continue;
                if ( _got_delimited_value ( buffer, BMC_INFO_LABEL_PRODUCT_ID, BMC_INFO_LABEL_DELIMITER, bmc_info.product_id ))
                    continue;
                if ( _got_delimited_value ( buffer, BMC_INFO_LABEL_PRODUCT_NAME, BMC_INFO_LABEL_DELIMITER, bmc_info.product_name ))
                    continue;
                if ( _got_delimited_value ( buffer, BMC_INFO_LABEL_MANUFACTURE_ID, BMC_INFO_LABEL_DELIMITER, bmc_info.manufacturer_id ))
                    continue;
                if ( _got_delimited_value ( buffer, BMC_INFO_LABEL_MANUFACTURE_NAME, BMC_INFO_LABEL_DELIMITER, bmc_info.manufacturer ))
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

    ipmiUtil_bmc_info_log ( hostname, bmc_info, rc );
    return (rc);
}
