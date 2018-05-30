/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform - Host Server Utility Module
  */

#include "hostUtil.h"

string hostUtil_getServiceIp   ( mtc_service_enum service )
{
    string ip = "0.0.0.0" ;

    daemon_config_type * cfg_ptr = daemon_get_cfg_ptr();

    switch (service)
    {
        case SERVICE_SYSINV:
        {
            ip = cfg_ptr->sysinv_api_bind_ip ;
            break ;
        }
        case SERVICE_TOKEN:
        {
            if ( cfg_ptr->keystone_auth_host)
            {
                ip = cfg_ptr->keystone_auth_host;
            }
            else
            {
                ip = "localhost";
            }
            break ;
        }
        case SERVICE_SMGR:
        case SERVICE_VIM:
        {
            ip = "localhost" ;
            break ;
        }
        default:
        {
            slog ("Unsupported service (%d)\n", service );
            break ;
        }
    }
    return (ip);
}

string hostUtil_getPrefixPath ( )
{
  string prefix_path = "";
    
  daemon_config_type * cfg_ptr = daemon_get_cfg_ptr();

  if ( cfg_ptr->keystone_prefix_path)
  {
      prefix_path = cfg_ptr->keystone_prefix_path;
  }

  return (prefix_path);
}

int hostUtil_getServicePort ( mtc_service_enum service )
{
    daemon_config_type * cfg_ptr = daemon_get_cfg_ptr();

    switch (service)
    {
        case SERVICE_SYSINV:
            return(cfg_ptr->sysinv_api_port);

        case SERVICE_SMGR:
            return(cfg_ptr->ha_port);

        case SERVICE_VIM:
            return(cfg_ptr->vim_cmd_port);

        case SERVICE_TOKEN:
            return(cfg_ptr->keystone_port);

        default:
        {
            slog ("Unsupported service (%d)\n", service );
            break ;
        }
    }
    return (0);
}

bool hostUtil_is_valid_ip_addr ( string ip )
{
    if ( !ip.empty() )
        if ( ip.compare(NONE) )
            return (true);
    return (false);
}

bool hostUtil_is_valid_mac_addr ( string mac )
{
    if ( !mac.empty() )
        if ( mac.length() == COL_CHARS_IN_MAC_ADDR )
            return (true);
    return (false);
}

bool hostUtil_is_valid_bm_type ( string bm_type )
{
    dlog3 ("BM_Type:%s\n", bm_type.c_str());
    if ( !bm_type.empty() )
    {
        if (( bm_type == "bmc" ) ||
            ( bm_type == "ilo" ) ||
            ( bm_type == "ilo3" ) ||
            ( bm_type == "ilo4" ) ||
            ( bm_type == "quanta" ))
        {
            return (true);
        }
    }
    return ( false );
}

bool hostUtil_is_valid_uuid ( string uuid )
{
    if (( !uuid.empty() ) && ( uuid.length() == UUID_LEN ) )
        return (true);
    return (false);
}

/*****************************************************************************
 *
 * Name        : hostUtil_tmpfile
 *
 * Description : Create a temporary file with a randomized suffix.
 *               Write the specified 'data' to it and return its
 *               open file descriptor.
 *
 * The file is unlinked so that it is automatically deleted by the kernel
 * when the file descriptor is closed or the program exits.
 *
 * TODO: fix or figure out why the unlink removes the file right away even
 *       with the file open.
 *
 *****************************************************************************/

int hostUtil_mktmpfile ( string hostname, string basename, string & filename, string data )
{
    // buffer to hold the temporary file name
    char tempBuff[MAX_FILENAME_LEN];

    int fd = -1;

    memset(tempBuff,0,sizeof(tempBuff));

    if ( basename.empty() || data.empty() )
    {
        slog ("%s called with one or more bad parameters (%d:%d)\n",
                  hostname.c_str(), basename.empty(), data.empty());
        return (0);
    }

    /* add what mkstemp will make unique */
    basename.append("XXXXXX");

    // Copy the relevant information in the buffers
    snprintf ( &tempBuff[0], MAX_FILENAME_LEN, "%s", basename.data());

    // Create the temporary file, this function will
    // replace the 'X's with random letters
    fd = mkstemp(tempBuff);

    // Call unlink so that whenever the file is closed or the program exits
    // the temporary file is deleted.
    //
    // Note: Unlinking removes the file immediately.
    // Commenting out. Caller must remove file.
    //
    // unlink(tempBuff);

    if(fd<1)
    {
        elog ("%s failed to create temp file (%d:%m)\n", hostname.c_str(), errno );
        return 0 ;
    }
    else
    {
        filename = tempBuff ;
        dlog2 ("%s temporary file [%s] created\n", hostname.c_str(), tempBuff );
    }

    // Write the data to the temporary file
    if ( write ( fd, data.data(), data.size()) < 0 )
    {
        elog ("%s failed to write data to '%s' (%d:%m)\n",
                  hostname.c_str(), filename.c_str(), errno );
        return 0 ;
    }
    else
    {
        dlog2 ("%s wrote %s to %s\n", hostname.c_str(), data.c_str(), filename.c_str());
    }
    return (fd);
}
