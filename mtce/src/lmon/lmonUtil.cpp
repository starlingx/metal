/*
 * Copyright (c) 2019 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Starling-X Maintenance Link Monitor Utility
  */

#include "lmon.h"
#include <fstream>           /* for ... ifstream                            */
#include <sstream>           /* for ... stringstream                        */
#include <net/if.h>          /* for ... if_indextoname , IF_NAMESIZE        */
#include <sys/ioctl.h>       /* for ... SIOCGIFFLAGS                        */
#include "nlEvent.h"         /* for ... get_netlink_events                  */

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "mon"

/*****************************************************************************
 *
 * Name    : iface_type
 *
 * Purpose : convert interface type enum to representative string.
 *
 * Returns : 0:ethernet returns "ethernet"
 *           1:vlan     returns "vlan"
 *           2:bond     returns "bond"
 *           ?          returns "unknown"   ... error case
 *
 ****************************************************************************/

string iface_type ( interface_type type_enum )
{
    switch(type_enum)
    {
        case ethernet: return "ethernet";
        case vlan:     return "vlan"    ;
        case bond:     return "bond"    ;
        default:       return "unknown" ;
    }
}

/*****************************************************************************
 *
 * Name    : get_iflink_interface
 *
 * Purpose : Gets the ifname of the linked parent interface
 *
 * Returns : Returns a string containing the ifname.
 *
 ****************************************************************************/

string get_iflink_interface (string & ifname )
{
    string ret = "";

    /* build the full file path */
    string iflink_file = INTERFACES_DIR + ifname + "/iflink";

    /* declare a file stream based on the full file path */
    ifstream iflink_file_stream ( iflink_file.c_str() );

    /* open the file stream */
    if (iflink_file_stream.is_open())
    {
        int iflink = -1;
        string iflink_line;
        char * dummy_ptr  ;
        char iface_buffer [IF_NAMESIZE] = "";
        memset (&iface_buffer[0], 0, IF_NAMESIZE);
        while ( getline (iflink_file_stream, iflink_line) )
        {
            iflink = strtol(iflink_line.c_str(), &dummy_ptr, 10);
        }
        iflink_file_stream.close();

        /*
         * load iface_buffer with the name of the network interface
         * corresponding to iflink.
         */
        if_indextoname (iflink, iface_buffer);

        if (iface_buffer[0] != '\0')
        {
            ret = iface_buffer;
        }
        else
        {
            slog ("no ifname from linked parent interface\n");
        }
    }
    return ret;
}

/*****************************************************************************
 *
 * Name       : lmon_fm_timestamp
 *
 * Purpose    : Get a microsecond timestamp of the current time.
 *
 * Description: Used to record the time of link state changes.
 *
 *              The value is included in link state query responses.
 *
 * Uses       : FMTimeT from fmAPI.h
 *
 ****************************************************************************/

FMTimeT lmon_fm_timestamp ( void )
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ( ts.tv_sec*1000000 + ts.tv_nsec/1000 );
}

/*****************************************************************************
 *
 * Name    : lmon_get_link_state
 *
 * Purpose : Query the link up/down state of the specified interface.
 *
 * Updates : Sets the callers boolean pointer to ...
 *
 *            true  if interface is up
 *            false if interface is doewn
 *
 * Returns : PASS           on query success.
 *           FAIL_OPERATION if the query was not successful.
 *
 ****************************************************************************/

static int get_link_state_throttle = 0 ;
#define GET_LINK_STATE__LOG_THROTTLE (100)

int lmon_get_link_state ( int    ioctl_socket,
                          char   iface[IF_NAMESIZE],
                          bool & link_up )
{
    int rc = FAIL_OPERATION ;

    link_up = false ; /* default to link down */

    if (iface[0] == '\0')
    {
        slog ("supplied interface name is invalid ; null\n");
        return ( rc ) ;
    }

    /* Declare and load interface data for ioctl query */
    struct ifreq if_data;
    memset( &if_data, 0, sizeof(if_data) );
    snprintf( if_data.ifr_name, IF_NAMESIZE, "%s", iface );

    /* read the interface up/down state */
    if( 0 <= ioctl( ioctl_socket, SIOCGIFFLAGS, &if_data ) )
    {
        if( if_data.ifr_flags & IFF_RUNNING )
            link_up = true;

        /* reset log flood gate counter */
        get_link_state_throttle = 0 ;

        rc = PASS ;
    }
    else
    {
        wlog_throttled (get_link_state_throttle,
                        GET_LINK_STATE__LOG_THROTTLE,
                        "failed to get %s (%s) interface state (%d:%s)\n",
                        iface,
                        if_data.ifr_name,
                        errno,
                        strerror(errno));
    }
    return ( rc );
}


/*****************************************************************************
 *
 * Name    : lmon_interfaces_init
 *
 * Purpose : Map an interface (mgmt, oam or infra) to a physical port.
 *           See interface_type enum in lmon.h
 *
 *****************************************************************************/

int lmon_interfaces_init ( interface_ctrl_type * ptr )
{
    FILE * file_ptr;
    char line_buf[MAX_CHARS_ON_LINE];
    string str;
    string physical_interface = "";

    /* iface enum to pltform.conf iface name */
    if ( strcmp(ptr->name, MGMT_INTERFACE_NAME) == 0 )
        str = MGMT_INTERFACE_FULLNAME;
    else if ( strcmp(ptr->name, INFRA_INTERFACE_NAME) == 0 )
        str = INFRA_INTERFACE_FULLNAME;
    else if ( strcmp(ptr->name, OAM_INTERFACE_NAME) == 0 )
        str = OAM_INTERFACE_FULLNAME;
    else
    {
        slog ("%s is an unsupported iface\n", ptr->name );
        return (FAIL_BAD_PARM);
    }

    /* open platform.conf and find the line containing this interface name. */
    file_ptr = fopen (PLATFORM_DIR , "r");
    if (file_ptr)
    {
        ifstream fin( PLATFORM_DIR );
        string line;

        while ( getline( fin, line ))
        {
            /* does this line contain it ? */
            if ( line.find(str) != string::npos )
            {
                stringstream ss( line );
                getline( ss, physical_interface, '=' ); // string before
                getline( ss, physical_interface, '=' ); // string after

                plog ("%s is the %s primary network interface",
                          physical_interface.c_str(),
                          ptr->name);

                /* determine the interface type */
                string uevent_interface_file =
                       INTERFACES_DIR + physical_interface + "/uevent";
                ifstream finUevent( uevent_interface_file.data() );

                if (!finUevent)
                {
                    elog ("Cannot find '%s' ; unable to monitor '%s' interface\n",
                          uevent_interface_file.c_str(), ptr->name );

                    ptr->used = false;
                    fclose(file_ptr);
                    return FAIL_OPERATION ;
                }
                else
                {
                    string line;
                    ptr->type_enum = ethernet;
                    while( getline( finUevent, line ) )
                    {
                        if ( line.find ("DEVTYPE") == 0 )
                        {
                            if ( line.find ("=vlan") != string::npos )
                                ptr->type_enum = vlan;
                            else if ( line.find ("=bond") != string::npos )
                                ptr->type_enum = bond;
                            break;
                        }
                    }
                }

                switch (ptr->type_enum)
                {
                    case ethernet:
                    {
                        memcpy(ptr->interface_one,
                               physical_interface.c_str(),
                               physical_interface.size());

                        ilog("%s is a %s ethernet interface\n",
                             ptr->interface_one, ptr->name );

                        break;
                    }
                    case bond:
                    {
                        memcpy(ptr->bond,
                               physical_interface.c_str(),
                               physical_interface.size());

                        ilog("%s is a bonded %s network interface\n",
                             ptr->bond, ptr->name);

                        break;
                    }
                    case vlan:
                    {
                        /****************************************************
                         *
                         * If it is a VLAN interface, we need to determine its
                         * parent interface, which may be a single ethernet
                         * link or a bonded interface.
                         *
                         ****************************************************/

                        string parent = get_iflink_interface(physical_interface);
                        if (!parent.empty())
                        {
                            string physical_interface_save = physical_interface ;
                            physical_interface = parent;

                            string uevent_parent_file =
                                   INTERFACES_DIR + parent + "/uevent";

                            ifstream finUevent2( uevent_parent_file.c_str() );

                            string line;
                            bool bond_configured = false;
                            while( getline( finUevent2, line ) )
                            {
                                // if this uevent does not have a DEVTYPE
                                // then its a ethernet interface. If this
                                // does have a DEVTYPE then check explicity
                                // for bond. Since we don't allow vlan over
                                // vlan, for all other DEVTYPEs, assume
                                // this is a ethernet interface.
                                if ( (line.find ("DEVTYPE") == 0) &&
                                     (line.find ("=bond") != string::npos) ) {

                                     ilog("%s is a vlan off the %s network whose parent is %s\n",
                                              physical_interface_save.c_str(),
                                              ptr->name,
                                              parent.c_str());
                                    bond_configured = true;
                                    break;
                                }
                            }

                            if (!bond_configured)
                            {
                                ilog("%s is a vlan off the %s network whose parent is %s\n",
                                     physical_interface.c_str(),
                                     ptr->name,
                                     parent.c_str());

                                memcpy(ptr->interface_one,
                                       parent.c_str(),
                                       parent.size());
                            }
                        }
                        else
                        {
                            ilog("%s is a vlan %s network\n",
                                     physical_interface.c_str(), ptr->name);
                        }
                        break;
                    }
                } // end of switch
                break;
            }
        }
        fclose(file_ptr);
    }

    /* Lagged interface */
    if ((ptr->interface_one[0] == '\0') && (!physical_interface.empty()))
    {
        string lagged_interface_file =
               INTERFACES_DIR + physical_interface + "/bonding/slaves";

        ifstream finTwo( lagged_interface_file.c_str() );
        if (!finTwo)
        {
            elog ("Cannot find bond interface file (%s) to "
                  "resolve slave interfaces\n", lagged_interface_file.c_str());
            ptr->used = false ;
            return (FAIL_OPERATION);
        }
        else
        {
            string line;
            while ( getline( finTwo, line ) )
            {
                strncpy(line_buf, line.c_str(), MAX_CHARS_ON_LINE);

                // the slave interfaces are listed as enXYYY enXYYY...
                // starting with the primary. Read all other slaves
                // as interface_two
                sscanf(line_buf, "%19s %19s", ptr->interface_one, ptr->interface_two);

                ilog("%s and %s are %s network aggregated interfaces\n",
                         ptr->interface_one,
                         ptr->interface_two,
                         ptr->name);
                break;
            }
        }
    }

    if ( ptr->interface_one[0] == '\0' )
    {
        ptr->used = false;
    }
    else
    {
        ptr->used = true;
        if ( ptr->interface_two[0] == '\0' )
        {
            /* this is not a lagged interface */
            ptr->lagged = false;
        }
        else
        {
            /* this is a lagged interface */
            ptr->lagged = true;
        }
    }
    return (PASS);
}



