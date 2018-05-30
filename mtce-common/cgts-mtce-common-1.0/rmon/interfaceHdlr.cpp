/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River CGCS Platform Resource Monitor Interface Handler
 */
#include <iostream>
#include <stdio.h>
#include <unistd.h>    
#include <stdlib.h>    
#include <dirent.h>    
#include <list>        
#include <syslog.h> 
#include <sys/wait.h>  
#include <time.h>      
#include <fstream>
#include <sstream>      
#include <dirent.h>
#include <signal.h>
#include "rmon.h"        /* rmon header file */ 
#include <algorithm> 
#include "nlEvent.h"     /* for ... get_netlink_events */
#include <string.h>
#include <net/if.h>

/* Used to set interface alarms through the FM API */
static SFmAlarmDataT alarmData; 

/* Used to set port alarms through the FM API */
static SFmAlarmDataT alarmDataPOne; 

/* Used to set port alarms through the FM API */
static SFmAlarmDataT alarmDataPTwo; 

const char rmonStages_str [RMON_STAGE__STAGES][32] =
{
    "Handler-Init", 
    "Handler-Start",
    "Manage-Restart",
    "Monitor-Wait",
    "Monitor-Resource",
    "Restart-Wait",
    "Ignore-Resource",
    "Handler-Finish",
    "Failed-Resource",
    "Failed-Resource-clr",
} ;

/*****************************************************************************
 *
 * Name    : interfaceResourceStageChange
 *
 * Purpose : Put an interface resource in the requested stage for use by the 
 * interface resource handler
 *
 *******************************************}
 **********************************/
int interfaceResourceStageChange ( interface_resource_config_type * ptr , rmonStage_enum newStage )
{
    if ((   newStage < RMON_STAGE__STAGES ) &&
            ( ptr->stage < RMON_STAGE__STAGES ))
    {
        clog ("%s %s -> %s (%d->%d)\n",
                ptr->resource,
                rmonStages_str[ptr->stage], 
                rmonStages_str[newStage], 
                ptr->stage, newStage);
        ptr->stage = newStage ;
        return (PASS);
    }
    else
    {
        slog ("%s Invalid Stage (now:%d new:%d)\n", 
                ptr->resource, ptr->stage, newStage );
        ptr->stage = RMON_STAGE__FINISH ;
        return (FAIL);
    }
}

/************************************************************
 *
 * Name    : get_iflink_interface
 *
 * Purpose : get the ifname of the linked parent interface
 ***********************************************************/
string get_iflink_interface (string ifname)
{

    string iflink_file = INTERFACES_DIR + ifname + "/iflink";

    ifstream finIflink ( iflink_file.c_str() );
    string iflink_line;
    string ret = "";
    char iface_buffer [INTERFACE_NAME_LEN] = "";
    int iflink = -1;

    if (finIflink.is_open())
    {

        while ( getline (finIflink, iflink_line) ) {
            iflink = atoi(iflink_line.c_str());
        }
        finIflink.close();
        
        if_indextoname (iflink, iface_buffer);
        
        if (iface_buffer[0] != '\0') 
            ret = iface_buffer;
        
    }
    return ret;
}


/*****************************************************************************
 *
 * Name    : init_physical_interfaces
 *
 * Purpose : Map an interface (mgmt, oam or infra) to a physical port 
 * 
 *****************************************************************************/
void init_physical_interfaces ( interface_resource_config_type * ptr )
{
    FILE * pFile;
    char line_buf[50];
    string str;
    string physical_interface = "";
    enum interface_type { single, vlan, bond };
    interface_type ifaceType; // default assumption  

    memset(ptr->interface_one, 0, sizeof(ptr->interface_one));
    memset(ptr->interface_two, 0, sizeof(ptr->interface_two));
    memset(ptr->bond, 0, sizeof(ptr->bond));
 
    if ( strcmp(ptr->resource, MGMT_INTERFACE_NAME) == 0 )
    {
        str = MGMT_INTERFACE_FULLNAME;
    }
    else if ( strcmp(ptr->resource, INFRA_INTERFACE_NAME) == 0 )
    {
        str = INFRA_INTERFACE_FULLNAME;
    }
    else if ( strcmp(ptr->resource, OAM_INTERFACE_NAME) == 0 )
    {
        str = OAM_INTERFACE_FULLNAME;
    }


    pFile = fopen (PLATFORM_DIR , "r");
    /* get the physical interface */ 
    if (pFile != NULL) 
    {
        ifstream fin( PLATFORM_DIR );
        string line;
        while ( getline( fin, line ) ) 
        {

            if ( line.find(str) != string::npos ) 
            {
                stringstream ss( line );
                getline( ss, physical_interface, '=' ); // token = string before =
                getline( ss, physical_interface, '=' ); // token = string after =

                // determine the interface type
                string uevent_interface_file = INTERFACES_DIR +
                                                physical_interface + "/uevent";
                ifstream finUevent( uevent_interface_file.c_str() );

                // if we cannot locate this file then instead of disabling
                // Kernel interface monitoring all together, we will use
                // use the interface naming convention to do a best effort 
                // estimate of the interface type... the show must go on!
                if (!finUevent) {
                    elog ("Cannot find uevent interface file (%s) to "
                          "resolve interface type for resource %s. "
                          "Disabling monitoring\n" , 
                          uevent_interface_file.c_str(), ptr->resource);
                    
                    ptr->interface_used = false;
                    fclose(pFile);
                    return;
                }
                else { // proceed with uevent method
                    string line;
                    ifaceType = single;
                    while( getline( finUevent, line ) )
                    {
                        if ( line.find ("DEVTYPE") == 0 )
                        {
                            if ( line.find ("=vlan") != string::npos )
                                ifaceType = vlan;
                            else if ( line.find ("=bond") != string::npos )
                                ifaceType = bond;
                            break;
                        }
                    }
                }

                switch (ifaceType) {
                    case single:
                        memcpy(ptr->interface_one, 
                               physical_interface.c_str(), 
                               physical_interface.size());
                        ilog("Interface   : %s : %s \n",  
                             ptr->interface_one, ptr->resource );
                        break;
                    case bond:
                        memcpy(ptr->bond, 
                               physical_interface.c_str(), 
                               physical_interface.size());
                        ilog("Bond Interface   : %s : %s \n",  
                             ptr->bond, ptr->resource );
                        break;
                    case vlan:
                        ilog("VLAN Interface   : %s : %s \n",
                             physical_interface.c_str(), ptr->resource);
                                // if it is a VLAN interface, we need
                                // to determine its parent interface, 
                                // which may be a single interface or 
                                // a bonded interface
                        string parent_interface = get_iflink_interface(physical_interface); 
                        if (!parent_interface.empty()) {
                            
                            dlog ("Parent interface for VLAN   : %s\n", 
                                  parent_interface.c_str());
                            
                            physical_interface = parent_interface;

                            string uevent_parent_file = INTERFACES_DIR +
                                                         parent_interface + "/uevent";
                            
                            ifstream finUevent2( uevent_parent_file.c_str() );
                            string line;
                            bool bond_configured = false;
                            while( getline( finUevent2, line ) )
                            {
                                // if this uevent does not have a DEVTYPE
                                // then its a single interface. If this
                                // does have a DEVTYPE then check explicity
                                // for bond. Since we don't allow vlan over
                                // vlan, for all other DEVTYPEs, assume 
                                // this is a single interface.
                                if ( (line.find ("DEVTYPE") == 0) &&
                                     (line.find ("=bond") != string::npos) ) {

                                    ilog ("Parent interface of VLAN interface "
                                          "resolved as Bond\n");
                                    bond_configured = true;
                                    break;
                                }
                            }
                            if (!bond_configured) {
                                memcpy(ptr->interface_one, 
                                       parent_interface.c_str(), 
                                       parent_interface.size());
                            }
                        }
                        break;
                } // end of switch
                break;
            }
        }
        fclose(pFile);
    }

    /* Lagged interface */ 
    if ((ptr->interface_one[0] == '\0') && (!physical_interface.empty()))
    {        
                
        string lagged_interface_file = INTERFACES_DIR + 
                                       physical_interface + "/bonding/slaves";
        
        ifstream finTwo( lagged_interface_file.c_str() );
        if (!finTwo) {
            elog ("Cannot find bond interface file (%s) to "
                  "resolve slave interfaces\n", lagged_interface_file.c_str());
        }
        else {
            string line;
            while( getline( finTwo, line ) ) 
            {
                strcpy(line_buf, line.c_str());
                // the slave interfaces are listed as enXYYY enXYYY...
                // starting with the primary. Read all other slaves
                // as interface_two
                sscanf(line_buf, "%19s %19s", ptr->interface_one, ptr->interface_two);
                ilog("%s interface: %s, interface two: %s \n", ptr->resource,  
                     ptr->interface_one, ptr->interface_two);
                break;
            }
        }
    }
  
    if ( ptr->interface_one[0] == '\0' )
    {
        ptr->interface_used = false;
    }
    else 
    {
        ptr->interface_used = true;
        if ( ptr->interface_two[0] == '\0' )
        {
            /* this is not a lagged interface */
            ptr->lagged = false;
        } else {
            /* this is a lagged interface */
            ptr->lagged = true;
        }
    }
}

/*****************************************************************************
 *
 * Name    : service_resource_state
 *
 * Purpose : Set the interface resource in the correct state for the interface 
 * resource handler 
 * 
 *****************************************************************************/
void service_resource_state ( interface_resource_config_type * ptr )
{

    if (ptr->lagged == true)
    {
        /* the lagged interface is initialized */ 
        if ((ptr->resource_value == INTERFACE_UP) && (ptr->resource_value_lagged == INTERFACE_UP) && 
            (ptr->failed == true ))
        {
            /* If both interfaces are up and there is a fault, it needs to be cleared */
            ptr->sev = SEVERITY_CLEARED;
            interfaceResourceStageChange ( ptr, RMON_STAGE__FINISH );
        } 
        else if ((((ptr->resource_value == INTERFACE_UP) && (ptr->resource_value_lagged == INTERFACE_DOWN)) || 
                ((ptr->resource_value_lagged == INTERFACE_UP) && (ptr->resource_value == INTERFACE_DOWN))) && 
                (ptr->sev != SEVERITY_MAJOR))
        {
            /* if one interface failed its a major condition */ 

            if (ptr->sev == SEVERITY_CRITICAL)
            {
                /* need to clear port alarm but not interface alarm */ 
                interfaceResourceStageChange ( ptr,  RMON_STAGE__FAILED_CLR );
            }
            else 
            {
                interfaceResourceStageChange ( ptr, RMON_STAGE__MANAGE);
            }

            ptr->failed = true;
            ptr->sev = SEVERITY_MAJOR;  
        }
        else if (((ptr->resource_value == INTERFACE_DOWN) && (ptr->resource_value_lagged == INTERFACE_DOWN)) && 
                (ptr->sev != SEVERITY_CRITICAL))
        {
            /* both lagged interfaces failed, this is a critical condition */
            ptr->failed = true;
            ptr->sev = SEVERITY_CRITICAL ;  
            interfaceResourceStageChange ( ptr, RMON_STAGE__MANAGE); 
        }
    }
    else 
    {
        /* interface is not lagged */
        if ( (ptr->resource_value == INTERFACE_DOWN) && (ptr->sev != SEVERITY_CRITICAL) )
        {
            /* the interface has failed */ 
            ptr->failed = true;
            ptr->sev = SEVERITY_CRITICAL ;  
            interfaceResourceStageChange ( ptr, RMON_STAGE__MANAGE);
        }
        else if ((ptr->resource_value == INTERFACE_UP) && (ptr->failed == true ))
        {
            /* If the interface is up and there is a fault, it needs to be cleared */ 
            ptr->sev = SEVERITY_CLEARED;
            interfaceResourceStageChange ( ptr, RMON_STAGE__FINISH );
        }
    }
}

/*****************************************************************************
 *
 * Name    : get_link_state
 *
 * Purpose : Check to see if the current interface link is up or down  
 * 
 *****************************************************************************/
int get_link_state ( int ioctl_socket, char iface[20], bool * running_ptr )
{
    int get_link_state_throttle = 0 ;
    struct ifreq  if_data;
    int rc = FAIL ;

    if (iface[0] == '\0') 
    {
        elog ("Null interface name\n");
        return ( rc ) ;
    }

    memset( &if_data, 0, sizeof(if_data) );
    sprintf( if_data.ifr_name, "%s", iface );
    if( 0 <= ioctl( ioctl_socket, SIOCGIFFLAGS, &if_data ) )
    {
        if( if_data.ifr_flags & IFF_RUNNING )
        {
            *running_ptr = true;
        } 
        else
        {
            *running_ptr = false;
        }

        /* reset log flood gate counter */
        get_link_state_throttle = 0 ;

        rc = PASS ;
    }
    else
    {
        wlog_throttled (get_link_state_throttle, 100, 
                "Failed to get %s (%s) interface state (%d:%s)\n",
                iface, if_data.ifr_name, errno, strerror(errno));
    }
    return ( rc );
}

/*****************************************************************************
 *
 * Name    : service_interface_events
 *
 * Purpose : Service state changes for monitored interfaces  
 * 
 *****************************************************************************/
int service_interface_events ( int nl_socket , int ioctl_socket )
{
    list<string> links_gone_down ;
    list<string> links_gone_up   ;
    list<string>::iterator iter_curr_ptr ;
    rmon_ctrl_type * _rmon_ctrl_ptr;
    interface_resource_config_type * ptr;

    _rmon_ctrl_ptr = get_rmon_ctrl_ptr();
    if ( get_netlink_events ( nl_socket, links_gone_down, links_gone_up ) )
    {
        for (int i=0; i<_rmon_ctrl_ptr->interface_resources; i++)
        {
            ptr = get_interface_ptr(i);
            if ( ptr->interface_used == true )
            {

                bool running = false ;

                if ( !links_gone_down.empty() )
                {
                    /* Look at the down list */
                    for ( iter_curr_ptr  = links_gone_down.begin();
                            iter_curr_ptr != links_gone_down.end() ;
                            iter_curr_ptr++ )
                    {
                        if ( strcmp ( ptr->interface_one, iter_curr_ptr->c_str()) == 0 )
                        {
                            wlog ("link %s is down\n", ptr->interface_one );

                            if ( get_link_state ( ioctl_socket, iter_curr_ptr->c_str(), &running ) == PASS )
                            {
                                wlog ("%s is down (oper:%s)\n", 
                                        iter_curr_ptr->c_str(), 
                                        running ? "up" : "down" );
                                if (!running) 
                                {
                                    ptr->resource_value = INTERFACE_DOWN;
                                } 
                                else 
                                {
                                    ptr->resource_value = INTERFACE_UP;
                                }
                            }
                            else
                            {
                                wlog ("%s is down (driver query failed)\n", iter_curr_ptr->c_str() );
                                ptr->resource_value = INTERFACE_DOWN;
                            }
                        }

                        if (ptr->lagged == true) 
                        {
                            if ( strcmp ( ptr->interface_two, iter_curr_ptr->c_str()) == 0 )
                            {
                                wlog ("link %s is down\n", ptr->interface_two);

                                if ( get_link_state ( ioctl_socket, iter_curr_ptr->c_str(), &running ) == PASS )
                                {  
                                    wlog ("%s is down (oper:%s)\n", 
                                            iter_curr_ptr->c_str(), 
                                            running ? "up" : "down" );
                                    if (!running) 
                                    {
                                        ptr->resource_value_lagged = INTERFACE_DOWN;
                                    } 
                                    else 
                                    {
                                        ptr->resource_value_lagged = INTERFACE_UP;
                                    }
                                }
                                else
                                {
                                    wlog ("%s is down (driver query failed)\n", iter_curr_ptr->c_str() );
                                    ptr->resource_value_lagged = INTERFACE_DOWN;
                                }
                            }
                            if ( strcmp ( ptr->bond, iter_curr_ptr->c_str()) == 0 )
                            {
                                wlog ("bond: %s is down\n", ptr->bond);
                                //ptr->resource_value_lagged = INTERFACE_DOWN;
                                //ptr->resource_value = INTERFACE_DOWN;
                            }
                        }
                    }
                }
                if ( !links_gone_up.empty() )
                {
                    //wlog ("one or more links have dropped\n");
                    /* Look at the down list */
                    for ( iter_curr_ptr  = links_gone_up.begin();
                            iter_curr_ptr != links_gone_up.end() ;
                            iter_curr_ptr++ )
                    {

                        if ( strcmp ( ptr->interface_one, iter_curr_ptr->c_str()) == 0 )
                        {
                            wlog ("link %s is up\n", ptr->interface_one );

                            if ( get_link_state ( ioctl_socket, iter_curr_ptr->c_str(), &running ) == PASS )
                            {
                                wlog ("%s is up (oper:%s)\n", 
                                        iter_curr_ptr->c_str(), 
                                        running ? "up" : "down" );
                                if (!running) 
                                {
                                    ptr->resource_value = INTERFACE_DOWN;
                                } 
                                else 
                                {
                                    ptr->resource_value = INTERFACE_UP;
                                }
                            }
                            else
                            {
                                wlog ("%s is down(driver query failed)\n", iter_curr_ptr->c_str() );
                                ptr->resource_value = INTERFACE_DOWN;
                            }
                        }
                        if (ptr->lagged == true) 
                        {
                            if ( strcmp ( ptr->interface_two, iter_curr_ptr->c_str()) == 0 )
                            {
                                wlog ("link %s is up\n", ptr->interface_two );

                                if ( get_link_state ( ioctl_socket, iter_curr_ptr->c_str(), &running ) == PASS )
                                {  
                                    wlog ("%s is up (oper:%s)\n", 
                                            iter_curr_ptr->c_str(), 
                                            running ? "up" : "down" );
                                    if (!running) 
                                    {
                                        ptr->resource_value_lagged = INTERFACE_DOWN;
                                    } 
                                    else 
                                    {
                                        ptr->resource_value_lagged = INTERFACE_UP;
                                    }
                                }
                                else
                                {
                                    wlog ("%s is down (driver query failed)\n", iter_curr_ptr->c_str() );
                                    ptr->resource_value_lagged = INTERFACE_DOWN;
                                }
                            }
                            if ( strcmp ( ptr->bond, iter_curr_ptr->c_str()) == 0 )
                            {
                                wlog ("bond: %s is up\n", ptr->bond);
                                //ptr->resource_value_lagged = INTERFACE_UP;
                                //ptr->resource_value = INTERFACE_UP;
                            }
                        }
                    }
                }
                /* set the states for the interface handler */
                service_resource_state( ptr );
            }
        }
    }

    return (PASS);
}

/*****************************************************************************
 *
 * Name    : interface_alarming_init
 *
 * Purpose : Initializes any previously raised interface alarms if rmon is restarted
 *
 *****************************************************************************/
void interface_alarming_init ( interface_resource_config_type * ptr) 
{
    AlarmFilter alarmFilter;
    ptr->failed = false;
    rmon_ctrl_type * _rmon_ctrl_ptr;

    _rmon_ctrl_ptr = get_rmon_ctrl_ptr();

    /* handle active alarms for the interface ports */
    SFmAlarmDataT *intf_alarms = NULL;
    unsigned int num_intf_alarms = 0;
    strcpy(alarmFilter.alarm_id, ptr->alarm_id_port);
    strcpy(alarmFilter.entity_instance_id, _rmon_ctrl_ptr->my_hostname); 
    strcat(alarmFilter.entity_instance_id, ".port=");

    if (rmon_fm_get(&alarmFilter, &intf_alarms, &num_intf_alarms) == FM_ERR_OK)
    {
        bool intf_one_found = false;
        bool intf_two_found = false;
        SFmAlarmDataT *a = intf_alarms;

        for( unsigned int i = 0; i < num_intf_alarms; i++, a++ )
        {
            /* only handle specific port alarm */
            if (strncmp(a->alarm_id, ptr->alarm_id_port, sizeof(a->alarm_id)) == 0)
            {
                /* check interface port one alarm */
                if (!intf_one_found && ptr->interface_one[0] != '\0')
                {
                    if (strstr(a->entity_instance_id, ptr->interface_one))
                    {
                        ptr->failed = true;
                        intf_one_found = true;
                    }
                }

                /* check interface port two alarm */
                if (!intf_two_found && ptr->interface_two[0] != '\0')
                {
                    if (strstr(a->entity_instance_id, ptr->interface_two))
                    {
                        ptr->failed = true;
                        intf_two_found = true;
                    }
                }

                /* clear this alarm as it is no longer valid as the interface ports have
                   changed */
                if (!intf_one_found && !intf_two_found)
                {
                    ilog("clearing alarm %s", a->entity_instance_id);
                    strcpy(alarmFilter.entity_instance_id, a->entity_instance_id);
                    rmon_fm_clear (&alarmFilter);
                }
            }
        }
 
        free(intf_alarms);
    }

    /* handle interface alarm */
    SFmAlarmDataT *active_alarm = (SFmAlarmDataT*) malloc (sizeof (SFmAlarmDataT));
    strcpy(alarmFilter.alarm_id, ptr->alarm_id);
    strcpy(alarmFilter.entity_instance_id, _rmon_ctrl_ptr->my_hostname); 
    strcat(alarmFilter.entity_instance_id, ".interface="); 
    strcat(alarmFilter.entity_instance_id, ptr->resource); 

    if (fm_get_fault( &alarmFilter, active_alarm) == FM_ERR_OK) 
    {
        if (active_alarm != NULL) 
        {
            ptr->failed = true; 
        }
    }

    free(active_alarm);

    /*
     * If the interface is DOWN, and neither a port
     * nor an interface alarm is found for that interface,
     * then that implies that the interface was DOWN before
     * RMON came up. Consider that as a failed case as well
     */
    if (ptr->interface_used && !ptr->failed &&
        (ptr->resource_value == INTERFACE_DOWN || 
         ptr->resource_value_lagged == INTERFACE_DOWN)) {
        ilog("Interface %s has initial state DOWN. Marked as failed\n",
             ptr->resource);
        ptr->failed = true;
    }

    /* service interface resource */
    if (ptr->failed)
    {
        ptr->alarm_raised = true;
        service_resource_state ( ptr );
    }
}

/*****************************************************************************
 *
 * Name    : _set_alarm_defaults
 *
 * Purpose : Set the defaults for the interface and port alarms 
 * *****************************************************************************/
void _set_alarm_defaults( interface_resource_config_type * ptr, rmon_ctrl_type * _rmon_ctrl_ptr )
{
    /* common data for all alarm messages */ 

    /* Interface alarms */
    strcpy(alarmData.uuid, "");                                      
    strcpy(alarmData.entity_type_id ,"system.host");
    strcpy(alarmData.entity_instance_id, _rmon_ctrl_ptr->my_hostname);
    strcat(alarmData.entity_instance_id, ".interface=");
    alarmData.alarm_state =  FM_ALARM_STATE_SET;
    alarmData.alarm_type =  FM_ALARM_OPERATIONAL;
    alarmData.probable_cause =  FM_ALARM_CAUSE_UNKNOWN;
    snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action), 
            "Check cabling and far-end port configuration and status on adjacent equipment.");
    alarmData.timestamp = 0; 
    alarmData.service_affecting = FM_TRUE;
    alarmData.suppression = FM_TRUE;     
    strcpy(alarmData.alarm_id, ptr->alarm_id);

    /* Port One alarms */ 
    strcpy(alarmDataPOne.uuid, "");                                      
    strcpy(alarmDataPOne.entity_type_id ,"system.host");
    strcpy(alarmDataPOne.entity_instance_id, _rmon_ctrl_ptr->my_hostname);
    strcat(alarmDataPOne.entity_instance_id, ".port=");
    alarmDataPOne.alarm_state =  FM_ALARM_STATE_SET;
    alarmDataPOne.alarm_type =  FM_ALARM_OPERATIONAL;
    alarmDataPOne.probable_cause =  FM_ALARM_CAUSE_UNKNOWN;
    snprintf(alarmDataPOne.proposed_repair_action , sizeof(alarmDataPOne.proposed_repair_action), 
            "Check cabling and far-end port configuration and status on adjacent equipment.");
    alarmDataPOne.timestamp = 0; 
    alarmDataPOne.service_affecting = FM_TRUE;
    alarmDataPOne.suppression = FM_TRUE;     
    strcpy(alarmDataPOne.alarm_id, ptr->alarm_id_port);

    /* Port Two alarms */
    strcpy(alarmDataPTwo.uuid, "");                                      
    strcpy(alarmDataPTwo.entity_type_id ,"system.host");
    strcpy(alarmDataPTwo.entity_instance_id, _rmon_ctrl_ptr->my_hostname);
    strcat(alarmDataPTwo.entity_instance_id, ".port=");
    alarmDataPTwo.alarm_state =  FM_ALARM_STATE_SET;
    alarmDataPTwo.alarm_type =  FM_ALARM_OPERATIONAL;
    alarmDataPTwo.probable_cause =  FM_ALARM_CAUSE_UNKNOWN;
    snprintf(alarmDataPTwo.proposed_repair_action , sizeof(alarmDataPTwo.proposed_repair_action), 
            "Check cabling and far-end port configuration and status on adjacent equipment.");
    alarmDataPTwo.timestamp = 0; 
    alarmDataPTwo.service_affecting = FM_TRUE;
    alarmDataPTwo.suppression = FM_TRUE;     
    strcpy(alarmDataPTwo.alarm_id, ptr->alarm_id_port);         
}

/*****************************************************************************
 *
 * Name    : interface_handler
 *
 * Purpose : Handle the failed interfaces and raise alarms through
 * the FM API as well as sending events to registered clients 
 *****************************************************************************/
void interface_handler( interface_resource_config_type * ptr ) 
{
    #define MAX_CLEAR_COUNT (10)
    AlarmFilter alarmFilter;
    bool portOne = false;
    bool portTwo = false;

    rmon_ctrl_type * _rmon_ctrl_ptr;

    _rmon_ctrl_ptr = get_rmon_ctrl_ptr();

    if ( ptr->stage < RMON_STAGE__STAGES )
    {
        dlog2 ("%s %s Stage %d\n", ptr->resource, rmonStages_str[ptr->stage], ptr->stage );
    }
    else
    {
        interfaceResourceStageChange ( ptr, RMON_STAGE__FINISH ); 
    }

    switch ( ptr->stage )
    {
        case RMON_STAGE__START:
            {
                dlog ( "%s failed:%d set_cnt:%d debounce_cnt:%d\n", 
                        ptr->resource,  
                        ptr->failed,
                        ptr->count, 
                        ptr->debounce_cnt);
                break ;
            }
        case RMON_STAGE__MANAGE:


            {
                /* sets alarms if thresholds are crossed */
                if (ptr->alarm_status == ALARM_ON)
                {
                    
                    _set_alarm_defaults( ptr, _rmon_ctrl_ptr );

                    /* Interface and Port alarming */
                    if (strcmp(ptr->resource, OAM_INTERFACE_NAME) == 0)
                    {
                        if ( ptr->sev == SEVERITY_CRITICAL )
                        {  
                            alarmData.severity = FM_ALARM_SEVERITY_CRITICAL;
                            ilog ("'OAM' Interface failed. \n"); 
                            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                    "'OAM' Interface failed."); 
                         }
                         else if ( ptr->sev == SEVERITY_MAJOR )
                         {
                            alarmData.severity = FM_ALARM_SEVERITY_MAJOR;
                            ilog ("'OAM' Interface degraded. \n"); 
                            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                    "'OAM' Interface degraded."); 
                         }

                         if ((ptr->interface_one[0] != '\0') && (ptr->resource_value == INTERFACE_DOWN))
                         {
                            portOne = true;
                            ilog ("'OAM' Port failed. \n"); 
                            snprintf(alarmDataPOne.reason_text, sizeof(alarmDataPOne.reason_text), 
                                    "'OAM' Port failed."); 
                            /* Set port name in entity instance ID */
                            strcat(alarmDataPOne.entity_instance_id, ptr->interface_one);

                         }
                         if ((ptr->interface_two[0] != '\0') && (ptr->resource_value_lagged == INTERFACE_DOWN))
                         {
                             portTwo = true;
                             ilog ("'OAM' Port failed. \n"); 
                             snprintf(alarmDataPTwo.reason_text, sizeof(alarmDataPTwo.reason_text), 
                                      "'OAM' Port failed."); 
                             /* Set port name in entity instance ID */
                             strcat(alarmDataPTwo.entity_instance_id, ptr->interface_two);
                         }                           
                    }
                    else if (strcmp(ptr->resource, MGMT_INTERFACE_NAME) == 0)
                    {
                        if ( ptr->sev == SEVERITY_CRITICAL )
                        {  
                            alarmData.severity = FM_ALARM_SEVERITY_CRITICAL;
                            ilog ("'MGMT' Interface failed. \n"); 
                            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                    "'MGMT' Interface failed."); 
                         }
                         else if ( ptr->sev == SEVERITY_MAJOR )
                         {
                            alarmData.severity = FM_ALARM_SEVERITY_MAJOR;
                            ilog ("'MGMT' Interface degraded. \n"); 
                            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                    "'MGMT' Interface degraded."); 
                         }

                         if ((ptr->interface_one[0] != '\0') && (ptr->resource_value == INTERFACE_DOWN))
                         {
                            portOne = true;
                            ilog ("'MGMT' Port failed. \n"); 
                            snprintf(alarmDataPOne.reason_text, sizeof(alarmDataPOne.reason_text), 
                                    "'MGMT' Port failed."); 
                            /* Set port name in entity instance ID */
                            strcat(alarmDataPOne.entity_instance_id, ptr->interface_one);

                         }
                         if ((ptr->interface_two[0] != '\0') && (ptr->resource_value_lagged == INTERFACE_DOWN))
                         {
                             portTwo = true;
                             ilog ("'MGMT' Port failed. \n"); 
                             snprintf(alarmDataPTwo.reason_text, sizeof(alarmDataPTwo.reason_text), 
                                      "'MGMT' Port failed."); 
                             /* Set port name in entity instance ID */
                             strcat(alarmDataPTwo.entity_instance_id, ptr->interface_two);
                         }                           
                    }
                    else if (strcmp(ptr->resource, INFRA_INTERFACE_NAME) == 0)
                    {
                        if ( ptr->sev == SEVERITY_CRITICAL )
                        {  
                            alarmData.severity = FM_ALARM_SEVERITY_CRITICAL;
                            ilog ("'INFRA' Interface failed. \n"); 
                            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                    "'INFRA' Interface failed."); 
                         }
                         else if ( ptr->sev == SEVERITY_MAJOR )
                         {
                            alarmData.severity = FM_ALARM_SEVERITY_MAJOR;
                            ilog ("'INFRA' Interface degraded. \n"); 
                            snprintf(alarmData.reason_text, sizeof(alarmData.reason_text), 
                                    "'INFRA' Interface degraded."); 
                         }

                         if ((ptr->interface_one[0] != '\0') && (ptr->resource_value == INTERFACE_DOWN))
                         {
                            portOne = true;
                            ilog ("'INFRA' Port failed. \n"); 
                            snprintf(alarmDataPOne.reason_text, sizeof(alarmDataPOne.reason_text), 
                                    "'INFRA' Port failed."); 
                            /* Set port name in entity instance ID */
                            strcat(alarmDataPOne.entity_instance_id, ptr->interface_one);

                         }
                         if ((ptr->interface_two[0] != '\0') && (ptr->resource_value_lagged == INTERFACE_DOWN))
                         {
                             portTwo = true;
                             ilog ("'INFRA' Port failed. \n"); 
                             snprintf(alarmDataPTwo.reason_text, sizeof(alarmDataPTwo.reason_text), 
                                      "'INFRA' Port failed."); 
                             /* Set port name in entity instance ID */
                             strcat(alarmDataPTwo.entity_instance_id, ptr->interface_two);
                         }  
                    }
                    snprintf(ptr->errorMsg, sizeof(ptr->errorMsg),
                             "%s major_threshold_set",ptr->resource);                 
                               
                    /* Set interface name in entity instance ID */
                    strcat(alarmData.entity_instance_id, ptr->resource);

                    dlog("Creating Interface Alarm: %s \n", ptr->resource);
                    if (rmon_fm_set(&alarmData, NULL) == FM_ERR_OK )
                    {
                        ilog("Alarm created for resource: %s \n", ptr->resource);
                        ptr->alarm_raised = true;
                    }
                    else 
                    {
                            ilog("Alarm create for resource: %s failed \n", ptr->resource);
                    }
                  
                    
                    if (portOne) 
                    {
                        alarmDataPOne.severity = FM_ALARM_SEVERITY_MAJOR;
                        dlog("Creating Port One Alarm: %s \n", ptr->resource);
                        if (rmon_fm_set(&alarmDataPOne, NULL) == FM_ERR_OK )
                        {
                            ilog("Alarm created for resource: %s port one \n", ptr->resource);
                        } else
                        {
                            ilog("Alarm create for resource: %s port one failed \n", ptr->resource);
                        }
                    }

                    if (portTwo)
                    {
                        alarmDataPTwo.severity = FM_ALARM_SEVERITY_MAJOR;
                        dlog("Creating Port Two Alarm: %s \n", ptr->resource);
                        if (rmon_fm_set(&alarmDataPTwo, NULL) == FM_ERR_OK )
                        {
                            ilog("Alarm created for resource: %s port two \n", ptr->resource);
                        } else
                        {
                            ilog("Alarm create for resource: %s port two failed \n", ptr->resource);
                        }        
                    }
                    

                    if (ptr->alarm_raised)
                    {
                        if ((_rmon_ctrl_ptr->clients > 0) && (ptr->failed_send < MAX_FAIL_SEND)) 
                        {   
                            if ( send_interface_msg ( ptr, _rmon_ctrl_ptr->clients ) != PASS )
                            {
                                wlog ("%s  request send failed \n", ptr->resource);
                                ptr->failed_send++;
                            } 
                            else 
                            {
                                ptr->failed_send = 0;                          
                            }
                            interfaceResourceStageChange ( ptr, RMON_STAGE__MONITOR_WAIT );  
                        }  
                        else 
                        {
                            ptr->failed_send = 0;
                            interfaceResourceStageChange ( ptr, RMON_STAGE__MONITOR_WAIT );  
                        }
                    }
                }
                else 
                {
                    interfaceResourceStageChange ( ptr, RMON_STAGE__FINISH ); 
                }
                break; 
            } 

        case RMON_STAGE__MONITOR_WAIT:
            {               
                if ((_rmon_ctrl_ptr->clients > 0) && (ptr->failed_send < MAX_FAIL_SEND) && (ptr->failed_send > 0))
                { 
                    if ( send_interface_msg ( ptr, _rmon_ctrl_ptr->clients ) != PASS )
                    {
                        wlog ("%s  request send failed \n", ptr->resource);
                        ptr->failed_send++;
                    } 
                    else 
                    {
                        ptr->failed_send = 0;                             
                    }
                }
                break; 
            }

        case RMON_STAGE__FAILED_CLR:
            {
                /* clear raised port alarms if one port comes back up */
                if (ptr->alarm_raised)
                {  
                    SFmAlarmDataT *active_alarm = (SFmAlarmDataT*) malloc (sizeof (SFmAlarmDataT));
                    strcpy(alarmFilter.alarm_id, ptr->alarm_id_port);

                    if (ptr->interface_one[0] != '\0')
                    {
                        strcpy(alarmFilter.entity_instance_id, _rmon_ctrl_ptr->my_hostname);
                        strcat(alarmFilter.entity_instance_id, ".port=");
                        strcat(alarmFilter.entity_instance_id, ptr->interface_one); 

                        if (fm_get_fault( &alarmFilter, active_alarm) == FM_ERR_OK) 
                        {
                            if (active_alarm != NULL) 
                            { 
                                if ( ptr->resource_value == INTERFACE_UP )
                                {
                                    if (rmon_fm_clear(&alarmFilter) ==  FM_ERR_OK)
                                    {
                                        ilog ("Cleared alarms for port one, interface: %s \n", ptr->resource);
                                    } 
                                    else 
                                    {
                                        ilog ("Failed to cleared alarms for port one, interface: %s \n", ptr->resource);
                                    }
                                }
                            }
                        }
                    }

                    if (ptr->interface_two[0] != '\0')
                    {
                        strcpy(alarmFilter.entity_instance_id, _rmon_ctrl_ptr->my_hostname);
                        strcat(alarmFilter.entity_instance_id, ".port=");
                        strcat(alarmFilter.entity_instance_id, ptr->interface_two);

                        if (fm_get_fault( &alarmFilter, active_alarm) == FM_ERR_OK) 
                        {
                            if (active_alarm != NULL) 
                            {   
                                if ( ptr->resource_value_lagged == INTERFACE_UP )
                                {
                                    if (rmon_fm_clear(&alarmFilter) ==  FM_ERR_OK)
                                    {
                                        ilog ("Cleared alarms for port two, interface: %s \n", ptr->resource);
                                    } 
                                    else 
                                    {
                                        ilog ("Failed to cleared alarms for port two, interface: %s \n", ptr->resource);
                                    }
                                }
                            }
                        }
                    }
                    free(active_alarm);
                }

                interfaceResourceStageChange ( ptr, RMON_STAGE__MANAGE);
                break; 
            }

            case RMON_STAGE__FINISH:
            {

                if ((ptr->alarm_status == ALARM_ON) && (ptr->alarm_raised))
                {
                    strcpy(alarmFilter.alarm_id, ptr->alarm_id_port);

                    if (ptr->interface_one[0] != '\0')
                    {
                        /* clear port one alarm */
                        strcpy(alarmFilter.entity_instance_id,_rmon_ctrl_ptr->my_hostname);
                        strcat(alarmFilter.entity_instance_id, ".port=");
                        strcat(alarmFilter.entity_instance_id, ptr->interface_one);

                        EFmErrorT err = rmon_fm_clear(&alarmFilter);
                        if ((err == FM_ERR_OK) || (err == FM_ERR_ENTITY_NOT_FOUND))
                        {
                            ilog ("Cleared alarms for port one, interface: %s \n", ptr->resource);
                        }
                        else
                        {
                            ilog ("Failed to cleared alarm for port one, interface: %s (rc:%d)\n", ptr->resource, err);
                        }
                    }
                    if (ptr->interface_two[0] != '\0')
                    {
                        /* clear port two alarm */
                        strcpy(alarmFilter.entity_instance_id,_rmon_ctrl_ptr->my_hostname);
                        strcat(alarmFilter.entity_instance_id, ".port=");
                        strcat(alarmFilter.entity_instance_id, ptr->interface_two);

                        EFmErrorT err = rmon_fm_clear(&alarmFilter);
                        if ((err == FM_ERR_OK) || (err == FM_ERR_ENTITY_NOT_FOUND))
                        {
                            ilog ("Cleared alarms for port two, interface: %s \n", ptr->resource);
                        }
                        else
                        {
                            elog ("Failed to cleared alarms for port two, interface: %s (rc:%d)\n", ptr->resource, err );
                        }
                    }

                    /* clear interface alarm */
                    strcpy(alarmFilter.alarm_id, ptr->alarm_id);
                    strcpy(alarmFilter.entity_instance_id,_rmon_ctrl_ptr->my_hostname);
                    strcat(alarmFilter.entity_instance_id, ".interface=");
                    strcat(alarmFilter.entity_instance_id, ptr->resource);

                    EFmErrorT err = rmon_fm_clear(&alarmFilter);
                    if ((err == FM_ERR_OK) || (err == FM_ERR_ENTITY_NOT_FOUND))
                    {
                        ilog ("Cleared alarms for interface: %s \n", ptr->resource);
                        snprintf(ptr->errorMsg, sizeof(ptr->errorMsg),
                                "%s cleared_alarms_for_resource:", ptr->resource);

                        if ((_rmon_ctrl_ptr->clients > 0) && (ptr->failed_send < MAX_FAIL_SEND))
                        {
                            while (( send_interface_msg ( ptr, _rmon_ctrl_ptr->clients ) != PASS ) &&
                                   ( ptr->failed_send < MAX_FAIL_SEND ))
                            {
                                wlog ("%s  request send failed \n", ptr->resource);
                                ptr->failed_send++;
                            }
                            ptr->alarm_raised = false;
                            ptr->failed_send = 0;
                            ptr->failed = false ;
                            ptr->count = 0;
                            ptr->sev = SEVERITY_CLEARED ;
                            ptr->stage = RMON_STAGE__START ;
                        }
                        else
                        {
                            ptr->alarm_raised = false;
                            ptr->failed_send = 0;
                            ptr->failed = false ;
                            ptr->count = 0 ;
                            ptr->sev = SEVERITY_CLEARED ;
                            ptr->stage = RMON_STAGE__START ;
                        }
                    }
                    else
                    {
                        wlog ("%s alarm clear failed (rc:%d)\n", ptr->resource, err);
                    }
                 }
                else
                {
                    ptr->failed_send = 0;
                    ptr->failed = false ;
                    ptr->count = 0 ;
                    ptr->sev = SEVERITY_CLEARED ;
                    ptr->stage = RMON_STAGE__START ;
                }
                break ;
            }

        default:
            {
                slog ("%s Invalid stage (%d)\n", ptr->resource, ptr->stage );

                /* Default to finish for invalid case. 
                 * If there is an issue then it will be detected */
                interfaceResourceStageChange ( ptr, RMON_STAGE__FINISH );
            }
    }
}
