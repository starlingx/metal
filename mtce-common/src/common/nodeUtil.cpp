/*
* Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <dirent.h>         /* for dir reading */
#include <net/if.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netdb.h>          /* for hostent     */
#include <errno.h>
#include <vector>
#include <algorithm>
#include <sys/stat.h>       /* for ... file stat                  */
#include <sys/types.h>
#include <sys/wait.h>
#include <openssl/md5.h>
#include <ctype.h>
#include <iterator>
#include <dirent.h>
#include <string>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>


using namespace std;

#include "nodeBase.h"
#include "nodeUtil.h"
#include "daemon_common.h"
#include "msgClass.h"

#ifdef __AREA__
#undef __AREA__
#endif
#define __AREA__ "com"

/***************************************************************************
 *
 * Name       : nodeUtil_latency_log
 *
 * Description: Measures execution time of a section of code.
 *              Produce a latency log if the specified duration
 *              in msec is exceeded.
 *
 * Warning    : Not multi thread safe.
 *
 * Parms:
 *
 *      hostname - for hostname.
 *
 *     label_ptr - "start" to init the prev_timer or
 *               - "some label" to identify the point in the code and to
 *                  measure time against the previous call.
 *
 *     msecs     - the latency log threshold
 *
 * Usage:
 *
 *     nodeUtil_latency_log ( NODEUTIL_LATENCY_MON_START, 0 );
 *
 *     [ timed code ]
 *
 *     nodeUtil_latency_log ( hostname, "label 1" , msecs );
 *
 *     [ timed code ]
 *
 *     nodeUtil_latency_log ( hostname, "label 2", msecs );
 *
 *     ...
 *
 *****************************************************************************/

void nodeUtil_latency_log ( string hostname, const char * label_ptr, int msecs )
{
    static unsigned long long prev__time = 0 ;
    static unsigned long long this__time = 0 ;

    this__time = gettime_monotonic_nsec () ;

    /* If label_ptr is != NULL and != start then take the measurement */
    if ( label_ptr && strncmp ( label_ptr, NODEUTIL_LATENCY_MON_START, strlen(NODEUTIL_LATENCY_MON_START)))
    {
        if ( this__time > (prev__time + (NSEC_TO_MSEC*(msecs))))
        {
            llog ("%s ... %4llu.%-4llu msec - %s\n", hostname.c_str(),
                 ((this__time-prev__time) > NSEC_TO_MSEC) ? ((this__time-prev__time)/NSEC_TO_MSEC) : 0,
                 ((this__time-prev__time) > NSEC_TO_MSEC) ? ((this__time-prev__time)%NSEC_TO_MSEC) : 0,
                 label_ptr );
        }
    }
    /* reset to be equal for next round */
    prev__time = this__time ;
}

void node_inv_init (node_inv_type & inv)
{
    inv.type.clear();
    inv.uuid.clear();
    inv.name.clear();
    inv.ip.clear();
    inv.clstr_ip.clear();
    inv.mac.clear();
    inv.admin.clear();
    inv.oper.clear();
    inv.avail.clear();
    inv.id.clear();
    inv.task.clear();
    inv.bm_ip.clear();
    inv.bm_un.clear();
    inv.bm_type.clear();
    inv.action.clear();
    inv.uptime.clear();
    inv.oper_subf.clear();
    inv.avail_subf.clear();
    inv.nodetype = 0;
}

void print_inv ( node_inv_type & info )
{
    const char bar [] = { "+------------+--------------------------------------+" };
    const char uar [] = { "+ Host Info  +--------------------------------------+" };


    syslog ( LOG_INFO, "%s\n", &uar[0]);
    syslog ( LOG_INFO, "| action     : %s\n", info.action.c_str());
    syslog ( LOG_INFO, "| personality: %s\n", info.type.c_str());
    syslog ( LOG_INFO, "| hostname   : %s\n", info.name.c_str());
    syslog ( LOG_INFO, "| task       : %s\n", info.task.c_str());
    syslog ( LOG_INFO, "| ip         : %s\n", info.ip.c_str());
    syslog ( LOG_INFO, "| mac        : %s\n", info.mac.c_str());
    syslog ( LOG_INFO, "| uuid       : %s\n", info.uuid.c_str());
    syslog ( LOG_INFO, "|   operState: %s\n", info.oper_subf.c_str());
    syslog ( LOG_INFO, "|  adminState: %s\n", info.admin.c_str());
    syslog ( LOG_INFO, "|   operState: %s\n", info.oper.c_str());
    syslog ( LOG_INFO, "| availStatus: %s\n", info.avail.c_str());
    syslog ( LOG_INFO, "| bm ip      : %s\n", info.bm_ip.c_str());
    syslog ( LOG_INFO, "| bm un      : %s\n", info.bm_un.c_str());
    syslog ( LOG_INFO, "| bm type    : %s\n", info.bm_type.c_str());
    syslog ( LOG_INFO, "| subFunction: %s\n", info.func.c_str());
    syslog ( LOG_INFO, "|   operState: %s\n", info.oper_subf.c_str());
    syslog ( LOG_INFO, "| availStatus: %s\n", info.avail_subf.c_str());
    syslog ( LOG_INFO, "%s\n", &bar[0]);
}

unsigned int get_host_function_mask ( string & nodeType_str )
{
    unsigned int nodeType = CGTS_NODE_NULL ;
    if ( nodeType_str.find("worker") != string::npos )
        nodeType |= WORKER_TYPE ;
    if ( nodeType_str.find("controller") != string::npos )
        nodeType |= CONTROLLER_TYPE ;
    if ( nodeType_str.find("storage") != string::npos )
        nodeType |= STORAGE_TYPE ;

    return (nodeType);
}

bool is_combo_system (unsigned int nodetype_mask )
{
    if ( nodetype_mask & CONTROLLER_TYPE )
    {
        if ( nodetype_mask & WORKER_TYPE )
        {
            return (true);
        }
        if ( nodetype_mask & STORAGE_TYPE )
        {
            return (true);
        }
    }
    return (false);
}


int set_host_functions ( string         nodetype_str, 
                         unsigned int * nodetype_bits_ptr, 
                         unsigned int * nodetype_function_ptr, 
                         unsigned int * nodetype_subfunction_ptr )
{
    int rc = PASS ;
    *nodetype_bits_ptr         = get_host_function_mask ( nodetype_str ) ;
    *nodetype_function_ptr     = CGTS_NODE_NULL ;
    *nodetype_subfunction_ptr  = CGTS_NODE_NULL ;

    /* Load up the host function and subfunction */
    if ( *nodetype_bits_ptr & CONTROLLER_TYPE )
    {
        *nodetype_function_ptr = CONTROLLER_TYPE ;
        dlog2 ("Function    : controller\n");

        /* Check for subfunctions */
        if ( *nodetype_bits_ptr & WORKER_TYPE )
        {
            dlog2 ("Sub Function: worker\n");
            *nodetype_subfunction_ptr = WORKER_TYPE ;
        }
        if ( *nodetype_bits_ptr & STORAGE_TYPE )
        {
            *nodetype_subfunction_ptr |= STORAGE_TYPE ;
            dlog2 ("Sub Function: storage\n");
        }
    }
    else
    {
        if ( *nodetype_bits_ptr & WORKER_TYPE )
        {
            *nodetype_function_ptr = WORKER_TYPE ;
            dlog2 ("Function    : worker\n");
        }
        else if ( *nodetype_bits_ptr & STORAGE_TYPE )
        {
            *nodetype_function_ptr |= STORAGE_TYPE ;
            dlog2 ("Function    : storage\n");
        }
        else
        {
            elog ("Unsupported nodetype (%u:%s)\n", *nodetype_bits_ptr, nodetype_str.c_str());
            rc = FAIL ;
        }
    }
    return (rc);
}

/* Checks that the goenabled tests are in the READY or PASS states
 * If pass parameter is true, then we check for the appropriate PASS state.
 * If pass parameter is false, we check for the appropriate READY state.
 *
 * Returns true if the appropriate state is found.
 */
bool is_goenabled ( int nodeType, bool pass )
{
    char* file;

    if ( is_combo_system ( nodeType ) == true )
    {
        if ( pass )
        {
            file = (char*) GOENABLED_SUBF_PASS;
        }
        else
        {
            file = (char*) GOENABLED_SUBF_READY;
        }
    }
    else
    {
        if ( pass )
        {
            file = (char*) GOENABLED_MAIN_PASS;
        }
        else
        {
            file = (char*) GOENABLED_MAIN_READY;
        }
    }
    return daemon_is_file_present ( file );
}

#define LOG_MEMORY(buf) syslog ( LOG_INFO, "%s", buf ); \
                        buf_ptr = &buf[0]; \
                        MEMSET_ZERO ( buf );

void dump_memory ( void * raw_ptr , int format, size_t bytes )
{
    uint32_t * word_ptr = (uint32_t*)raw_ptr ;
    uint8_t * byte_ptr = (uint8_t*)raw_ptr ;

    char buf[0x1024] ;
    char * buf_ptr = &buf[0];
    MEMSET_ZERO ( buf );
    syslog ( LOG_INFO, "Dumping Memory: %ld bytes", bytes );
    if ( format == 4 )
    {
        int loops = bytes/format ;

        for ( int i = 0 ; i < loops ; i++ )
        {
            buf_ptr += sprintf ( buf_ptr, "0x%p : 0x%08x : ", word_ptr, *word_ptr );
            byte_ptr = (uint8_t*)word_ptr ;
            for ( int c = 0 ; c < format ; c++ )
            {
                if (( *byte_ptr >= ' ' ) && ( *byte_ptr <= '~' ))
                    buf_ptr += sprintf ( buf_ptr, "%c", *byte_ptr) ;
                else
                    buf_ptr += sprintf ( buf_ptr, "%c", '.');
                byte_ptr++ ;
            }
            LOG_MEMORY(buf);
            word_ptr++ ;
        }
    }
    else if ( format == 8 )
    {
        int loops = bytes/format ;

        for ( int i = 0 ; i < loops ; i++ )
        {
            buf_ptr += sprintf ( buf_ptr, "0x%p : 0x%08x 0x%08x : ", word_ptr, *word_ptr, *(word_ptr+1) );
            byte_ptr = (uint8_t*)word_ptr ;
            for ( int c = 0 ; c < format ; c++ )
            {
                if (( *byte_ptr >= ' ' ) && ( *byte_ptr <= '~' ))
                    buf_ptr += sprintf ( buf_ptr , "%c", *byte_ptr) ;
                else
                    buf_ptr += sprintf ( buf_ptr , "%c", '.');
                byte_ptr++ ;
            }
            LOG_MEMORY(buf);
            word_ptr += 2 ;
        }
    }
    else if ( format == 16 )
    {
        int loops = bytes/format ;

        for ( int i = 0 ; i < loops ; i++ )
        {
            buf_ptr += sprintf ( buf_ptr, "0x%p : 0x%08x 0x%08x 0x%08x 0x%08x : ", word_ptr, *word_ptr, *(word_ptr+1), *(word_ptr+2), *(word_ptr+3));
            byte_ptr = (uint8_t*)word_ptr ;
            for ( int c = 0 ; c < format ; c++ )
            {
                if (( *byte_ptr >= ' ' ) && ( *byte_ptr <= '~' ))
                    buf_ptr += sprintf ( buf_ptr , "%c", *byte_ptr) ;
                else
                    buf_ptr += sprintf ( buf_ptr , "%c", '.');
                byte_ptr++ ;
            }
            LOG_MEMORY(buf);
            word_ptr += 4 ;
        }
    }
}


char hostname_floating [] = { "controller" } ;

string getipbyiface ( const char * iface )
{
    string ip_string = "";
    char ip_cstr[INET6_ADDRSTRLEN];
    if(msgClassAddr::getAddressFromInterface(iface, ip_cstr, INET6_ADDRSTRLEN)==PASS)
    {
        ip_string = ip_cstr;
    }
    return ip_string;
}

string getipbyname ( string name )
{
    string ip_string = "" ;
    const char* address_string;
    int count = 0 ;
    do
    {
        msgClassAddr addr = msgClassAddr(name.c_str());
        address_string = addr.toNumericString();
        if(address_string)
        {
            ip_string = address_string;
        }
        if(ip_string.empty())
        {
            wlog_throttled ( count, 50, "Unable to get ip address list for '%s', retrying ...\n", name.c_str());
            mtcWait_secs (2);
        }
        daemon_signal_hdlr ();
    } while ( ip_string.empty() ) ;
    return (ip_string);
}

string getipbynameifexists ( string name )
{
    string ip_string = "" ;
    const char* address_string;
    msgClassAddr addr = msgClassAddr(name.c_str());
    address_string = addr.toNumericString();
    if(address_string)
    {
        ip_string = address_string;
    }
    return (ip_string);
}


/* Reads the local hostname, ip and the floating ip address. 
 * Returns RETRY if the information has changed compared to what is 
 * passed in. Reference strings are updated if values are changed */
int get_ip_addresses ( string & my_hostname , string & my_local_ip , string & my_float_ip )
{
    int rc = PASS ;

    string temp_hostname = "" ;
    string temp_local_ip = "" ;
    string temp_float_ip = "" ;

    char     hostname_str       [MAX_HOST_NAME_SIZE+1];
    memset (&hostname_str[0], 0, MAX_HOST_NAME_SIZE+1);

    /* read the host name */
    rc = gethostname(&hostname_str[0], MAX_HOST_NAME_SIZE );
    if ( rc == PASS )
    {
        /* Load as a string and then compare */
        temp_hostname = hostname_str ;
        if ( temp_hostname != my_hostname )
        {
            /* update control struct and set rc for reload (RETRY) */
            my_hostname = temp_hostname ;
            ilog ("My Hostname : %s\n", my_hostname.c_str());
            rc = RETRY ;
        }
    }
    else        
    {
        /* get the host info */
        elog ("Unable to get controller local hostname\n");
        return (FAIL);
    }
    
    set_hn (hostname_str);
 
    /* Get the Primary hostname ip address */
    temp_local_ip = getipbyname (hostname_str);

    /* See if the local ip address has changed */
    if ( temp_local_ip != my_local_ip )
    {
        ilog ("   Local IP : %s\n", temp_local_ip.c_str());

        /* update control struct and set rc for reload (RETRY) */
        my_local_ip = temp_local_ip ;
        rc = RETRY ;
    }

    /* Move on to read the floating ip */
    temp_float_ip = getipbyname (hostname_floating);

    /* See if the floating ip address has changed */
    if ( temp_float_ip != my_float_ip )
    {
        ilog ("Floating IP : %s\n", temp_float_ip.c_str());


        /* update control struct and set rc for reload (RETRY) */
        my_float_ip = temp_float_ip ;
        rc = RETRY ;
    }
    return rc;
}        

int open_ioctl_socket ( void )
{
    int flags;

    int ioctl_socket = socket( PF_PACKET, SOCK_DGRAM, 0 );
    if( 0 > ioctl_socket )
    {
        elog ( "Failed to open ioctl socket (%d:%s)\n", 
                errno, strerror( errno ) );
        return( ioctl_socket );
    }

    flags = fcntl( ioctl_socket, F_GETFL, 0 );
    if( 0 > flags )
    {
        elog ( "Failed to get ioctl socket flags (%d:%s)\n", 
                errno, strerror( errno ) );
        close( ioctl_socket );
        return( flags );
    }

    if( 0 > fcntl( ioctl_socket, F_SETFL, flags | O_NONBLOCK ) )
    {
        elog ( "Failed to set ioctl socket flags (%d:%s)",
                errno, strerror( errno ) );
        close( ioctl_socket );
        return( -1 );
    }
    
    return ( ioctl_socket );
}

/* returns true if the link is up for the specified interface */
int get_link_state_throttle = 0 ;
int get_link_state ( int ioctl_socket, const char * iface_ptr, bool * running_ptr )
{
    struct ifreq        if_data;
    int rc = FAIL ;

    if (!iface_ptr || !*iface_ptr) 
    {
        dlog ("Null interface name\n");
        return ( rc ) ;
    }

    memset( &if_data, 0, sizeof(if_data) );
    sprintf( if_data.ifr_name, "%s", iface_ptr );
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
                         iface_ptr, if_data.ifr_name, errno, strerror(errno));
    }
    return ( rc );
}

int get_iface_attrs ( const char * iface_ptr, 
                             int & index, 
                             int & speed, 
                             int & duplex, 
                          string & autoneg )
{
    struct ifreq        ifr;
    struct ethtool_cmd  cmd;
    int                 fd, result;

    if (!iface_ptr || !*iface_ptr) 
    {
        elog ("Null interface name\n");
        return FAIL ;
    }

    index = -1;
    speed = -1;
    duplex = -1;
    autoneg = "N/A" ;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        const int err = errno;
        elog ("%s: Cannot create AF_INET socket: (%s)\n", iface_ptr, strerror(err));
        return FAIL;
    }

    snprintf (ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface_ptr);
    ifr.ifr_data = (char*)&cmd;
    cmd.cmd = ETHTOOL_GSET;
    if (ioctl(fd, SIOCETHTOOL, &ifr) < 0) 
    {
        const int err = errno;
        do
        {
            result = close(fd);
                    
            daemon_signal_hdlr ();
    
        } while (result == -1 && errno == EINTR);
        elog ("%s: SIOCETHTOOL ioctl: (%s)\n", iface_ptr, strerror(err));
        return FAIL;
    }

    speed = ethtool_cmd_speed(&cmd);

    if ( cmd.advertising & SUPPORTED_Autoneg )
    {
        ilog ("Autoneg: %d\n", cmd.autoneg );
        if ( cmd.autoneg )
            autoneg = "Yes" ;
        else
            autoneg = "No";
    }

    switch (cmd.duplex)
    {
        case DUPLEX_HALF: duplex = 0; break;
        case DUPLEX_FULL: duplex = 1; break;
        default:
            elog ("%s: Unknown mode (0x%x).\n", iface_ptr, cmd.duplex);
        }

    if (index && ioctl(fd, SIOCGIFINDEX, &ifr) >= 0)
    {
        index = ifr.ifr_ifindex;
    }

    do 
    {
        result = close(fd);
    } while (result == -1 && errno == EINTR);

    if (result == -1) 
    {
        const int err = errno;
        elog ("%s: Error closing socket: %s\n", iface_ptr, strerror(err));
        return FAIL ;
    }

    return PASS;
}


int get_iface_macaddr ( const char * iface_ptr , string & macaddr )
{  
    int rc ; 
    struct ifreq s;
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    memset ( (void*)&s, 0 , sizeof(struct ifreq) );
    strcpy(s.ifr_name, iface_ptr );
    rc = ioctl(fd, SIOCGIFHWADDR, &s) ;
    if ( rc == PASS ) 
    {
        char str [COL_CHARS_IN_MAC_ADDR+1] ; /* and terminator */
        memset  ( &str[0], 0 , COL_CHARS_IN_MAC_ADDR);

        snprintf ( &str[0], sizeof(str),
                  "%02x:%02x:%02x:%02x:%02x:%02x",
               (unsigned char)(s.ifr_hwaddr.sa_data[0]),
               (unsigned char)(s.ifr_hwaddr.sa_data[1]),
               (unsigned char)(s.ifr_hwaddr.sa_data[2]), 
               (unsigned char)(s.ifr_hwaddr.sa_data[3]),
               (unsigned char)(s.ifr_hwaddr.sa_data[4]),
               (unsigned char)(s.ifr_hwaddr.sa_data[5]));

        macaddr = str ;
        ilog ("Mac Address : %s\n", macaddr.c_str() );
    }
    else
    {
        elog ("Mac Address : Unknown\n");
        elog ("Failed to get %s's mac address (rc:%d)\n", iface_ptr , rc ); 
    }
    close(fd);
    return (rc);
}

string get_iface_mac ( const char * iface_ptr )
{
    int rc ; 
    struct ifreq s;
    string mac = "---" ;
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    memset ( (void*)&s, 0 , sizeof(struct ifreq) );
    strcpy(s.ifr_name, iface_ptr );
    rc = ioctl(fd, SIOCGIFHWADDR, &s) ;
    if ( rc == PASS ) 
    {
        char str [COL_CHARS_IN_MAC_ADDR+1] ; /* and terminator */
        memset  ( &str[0], 0 , COL_CHARS_IN_MAC_ADDR);

        snprintf ( &str[0], sizeof(str),
                  "%02x:%02x:%02x:%02x:%02x:%02x",
               (unsigned char)(s.ifr_hwaddr.sa_data[0]),
               (unsigned char)(s.ifr_hwaddr.sa_data[1]),
               (unsigned char)(s.ifr_hwaddr.sa_data[2]), 
               (unsigned char)(s.ifr_hwaddr.sa_data[3]),
               (unsigned char)(s.ifr_hwaddr.sa_data[4]),
               (unsigned char)(s.ifr_hwaddr.sa_data[5]));

        mac = str ;
        ilog ("Mac Address : %s\n", mac.c_str() );
    }
    else
    {
        elog ("Mac Address : Unknown\n");
        elog ("Failed to get %s's mac address (rc:%d)\n", iface_ptr , rc ); 
    }
    close(fd);
    return (mac);
}




int get_hostname ( char * hostname_ptr, int max_len )
{
    int rc ;
    memset ( hostname_ptr, 0, max_len-1);
    do
    {
        rc = gethostname(hostname_ptr, max_len );
        if ( rc == PASS )
        {
            ilog ("%s", hostname_ptr);
        }
        else
        {
            wlog ("No hostname, retrying ...\n" );
            mtcWait_secs (5);
        }
    
        daemon_signal_hdlr ();
 
    } while ( rc != PASS ) ;
       
    set_hn (hostname_ptr);

    return (rc);
}

int get_iface_hostname ( const char * iface_ptr, char * hostname_ptr)
{
    int rc ;
    memset ( hostname_ptr, 0, MAX_HOST_NAME_SIZE-1);
    do
    {
        rc = gethostname(hostname_ptr, MAX_HOST_NAME_SIZE );
        if ( rc == PASS )
        {
            ilog ("Hostname    : %s\n", hostname_ptr);
        }
        else
        {
            wlog ("%s has no hostname, retrying ...\n", iface_ptr);
            mtcWait_secs (5);
        }
        daemon_signal_hdlr ();
    } while ( rc != PASS ) ;
    return (rc);
}

int get_iface_address ( const char * iface_ptr, string & ip_addr , bool retry )
{
    int rc ;
    int count ;
    char ip_cstr[INET6_ADDRSTRLEN];

    /* Now fetch the IP address. We stay here till we have one. */
    count = 0 ;
    do
    {
        rc = msgClassAddr::getAddressFromInterface(iface_ptr, ip_cstr, INET6_ADDRSTRLEN);
        if ( rc == PASS )
        {
            ip_addr = ip_cstr;
            ilog ("%s %s\n", iface_ptr, ip_addr.c_str());
        }
        else
        {
            wlog_throttled ( count, 24, "%s has no IP address (rc=%d), retrying ...\n", iface_ptr, rc );
            /* get out if the caller does not want retries */
            if ( retry == false )
                return (RETRY);

            mtcWait_secs (5);
        }
        daemon_signal_hdlr ();
    } while ( rc != PASS ) ;
    return (rc);
}

void get_clstr_iface ( char ** clstr_iface_ptr )
{
    char * iface_ptr ;
    string clstr = daemon_clstr_iface();

    /* remove .None from the interface name if it exists */
    size_t found = clstr.find(".None");
    if ( found != string::npos)
    {
        clstr.erase(found, string::npos);
    }
    if ( clstr.size() )
    {
        iface_ptr = daemon_get_iface_master ( (char*)clstr.data());
        *clstr_iface_ptr = strdup((const char*)iface_ptr);
        dlog("Clstr iface : %s\n", *clstr_iface_ptr );
    }
    else
    {
        *clstr_iface_ptr = strdup((const char*)"");
    }
}

/*****************************************************************************
 *
 * Name    : load_filenames_in_dir
 *
 * Purpose : Load the supplied list with all the file names
 *           in the specified directory
 *
 *****************************************************************************/

int load_filenames_in_dir ( const char * directory, std::list<string> & filelist )
{
    DIR           *d;
    struct dirent *dir;

    /* Clear the content of the config file list and running counter */
    filelist.clear ();

    d = opendir(directory);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            dlog3 ("File: %s\n", dir->d_name);
            if ( strcmp ( dir->d_name , "."  ) && 
                 strcmp ( dir->d_name , ".." ))
            {
                string temp = directory ;
                temp.append("/");
                temp.append(dir->d_name);
                filelist.push_back ( temp );
            }
            daemon_signal_hdlr ();
        }
        closedir(d);
    }
    else
    {
        elog ("Failed to open %s\n", directory );
    }
    return(PASS);
}

int setup_child ( bool close_file_descriptors )
{
    /* Create a new process group for the child process */
    if ( 0 > setpgid (0,0))
        return (FAIL); 

    /* Change the current working directory. */  
    if ((chdir("/")) < 0)
        return (FAIL);

    if ( close_file_descriptors == true )
    {
        struct rlimit file_limits ;

        if ( 0 < getrlimit ( RLIMIT_NOFILE, &file_limits ) )
            return (FAIL); 

        /* Close all existing file descriptors */
        for ( unsigned int fd_i = 0 ; fd_i < file_limits.rlim_cur ; ++fd_i )
        {
            close (fd_i);
        }

        int fd = open("/dev/null",O_RDWR, 0);  
  
        if (fd != -1)  
        {  
            dup2 (fd, STDIN_FILENO);  
            dup2 (fd, STDOUT_FILENO);  
            dup2 (fd, STDERR_FILENO);  
  
            if (fd > 2)  
            {  
                close (fd);  
            }  
        }
    }
    return (PASS);
}

/* 
 * Issue a double fork to run the specified command string. Its a
 * double fork to avoid need to stick around ; avoids defunct processes.
 * On success the grandchild gets 0 and the parent gets 1.
 * On failure the parent gets -1.
 */
int double_fork_host_cmd ( string hostname , char * cmd_string, const char * cmd_oper )
{
    int status = 0 ;
    

    UNUSED(cmd_oper); /* for future use maybe */
    blog ("%s %s\n", hostname.c_str(), cmd_string);

    /* Flush the logs before fork */
    fflush (stdout);
    fflush (stderr);

    pid_t child_pid = fork ();
    if ( child_pid == 0 )
    {
        /* In child process */
        pid_t grandchild_pid = fork();
        if ( grandchild_pid > 0 )
        {
            /* child exits immediately */
            exit (0);
        }
        else if ( grandchild_pid == -1 )
        {
            // in child, error forking
            wlog("problem forking grandchild: %m\n");
            fflush(stdout);
            exit (-1);
        }
        else
        {
            /* grandchild runs system command */
            int rc = FAIL ;
            bool close_file_descriptors = false ;
            if ( setup_child ( close_file_descriptors ) != PASS )
            {
                exit(EXIT_FAILURE);
            }

            /* Set child to ignore child exit */
            signal (SIGCHLD, SIG_DFL);

            if (( rc = system(cmd_string) ) > 0 )
            {
                wlog("system call failed: '%s' (%d:%m)\n", cmd_string, errno );
                // decode_error ( hostname, rc , cmd_string ) ;
            }
            else
            {
                dlog ( "%s forked cmd '%s' passed\n", hostname.c_str(), cmd_string);
            }
            exit(0);
        }
    }
    else if ( child_pid > 0 )
    {
        // In Parent, child successfully forked
        /* Wait for first child to exit ; happens immediately */
        waitpid(child_pid , &status, 0 );
        if ( WIFEXITED(status) && WEXITSTATUS(status) == 0 )
        {
            return 1;
        }
        else
        {
            ilog ("Waiting for child to exit ...\n");
            usleep (1000);
        }
    }
    wlog("problem forking child: %m\n");
    fflush(stdout);
    return -1;
}


/* Issue a double fork to avoid need to 
 * stick around to avoid defunct processes.
 * On success the grandchild gets 0 and the parent gets 1.
 * On failure the parent gets -1.
 */
int double_fork ( void )
{
    int status = 0 ;

    /* Theoretically we should flush the logs before forking  otherwise it can
     * cause duplicate messages in parent and child.  But we want to minimize
     * the work being done in the main thread, so we accept the tradeoff.
     */
    //fflush (stdout);
    //fflush (stderr);

    pid_t child_pid = fork ();
    if ( child_pid == 0 )
    {
        pid_t grandchild_pid = fork();
        if ( grandchild_pid == 0 ) {
            // in grandchild
            return 0;
        } else if ( grandchild_pid == -1 ) {
            // in child, error forking
            wlog("problem forking grandchild: %m\n");
            fflush(stdout);
            exit (-1);
        } else
            // in child, grandchild successfully forked
            exit (0);
    }
    else if ( child_pid > 0 )
    {
        // in parent, child successfully forked
        /* Wait for first child to exit ; happens immediately */
        waitpid(child_pid , &status, 0 );
        if ( WIFEXITED(status) && WEXITSTATUS(status) == 0 )
            return 1;
    }
    wlog("problem forking child: %m\n");
    fflush(stdout);
    return -1;
}

/***************************************************************************
 *
 * Name    : fork_sysreq_reboot
 *
 * Purpose : Timed SYSREQ Reset service used as a backup mechanism
 *           to force a self reset after a specified period of time.
 *
 **************************************************************************/

/* This is a common utility that forces a sysreq reboot */
void fork_sysreq_reboot ( int delay_in_secs )
{
    int parent = 0 ;

    /* Fork child to do a sysreq reboot. */
    if ( 0 > ( parent = double_fork()))
    {
        elog ("failed to fork fail-safe (backup) sysreq reboot\n");
        return ;
    } 
    else if( 0 == parent ) /* we're the child */
    {
        int sysrq_handler_fd;
        int sysrq_tigger_fd ;
        size_t temp ;
        
        setup_child ( false ) ; 

        ilog ("*** Failsafe Reset Thread ***\n");

        /* Commented this out because blocking SIGTERM in systemd environment
         * causes any processes that spawn this sysreq will stall shutdown
         *
         * sigset_t mask , mask_orig ;
         * sigemptyset (&mask);
         * sigaddset (&mask, SIGTERM );
         * sigprocmask (SIG_BLOCK, &mask, &mask_orig );
         *
         */

        // Enable sysrq handling.
        sysrq_handler_fd = open( "/proc/sys/kernel/sysrq", O_RDWR | O_CLOEXEC );
        if( 0 > sysrq_handler_fd )
        {
            ilog ( "failed sysrq_handler open\n");
            return ;
        }

        temp = write( sysrq_handler_fd, "1", 1 );
        close( sysrq_handler_fd );

        for ( int i = delay_in_secs ; i >= 0 ; --i )
        {
            sleep (1);
            {
                if ( 0 == (i % 5) )
                {
                    ilog ( "sysrq reset in %d seconds\n", i );
                }
            }
        }

        // Trigger sysrq command.
        sysrq_tigger_fd = open( "/proc/sysrq-trigger", O_RDWR | O_CLOEXEC );
        if( 0 > sysrq_tigger_fd )
        {
            ilog ( "failed sysrq_trigger open\n");
            return ;
        }

        temp = write( sysrq_tigger_fd, "b", 1 ); 
        close( sysrq_tigger_fd );
                                
        ilog ( "sysreq rc:%ld\n", temp );

        UNUSED(temp);

        sleep (10); 
    
        // Shouldn't get this far, else there was an error.
        exit(-1);
    }
    ilog ("Forked Fail-Safe (Backup) Reboot Action\n");
}

/***************************************************************************
 *
 * Name    : fork_graceful_reboot
 *
 * Purpose : Timed reset via /sbin/reboot which attempts to use graceful
 *           mechanisms (like unmounting filesystems) to perform a reset.
 *           Note that in cases where blocking can occur (like I/O failure)
 *           the forked process may become blocked.
 *
 **************************************************************************/

/* This is a common utility that forces a /sbin/reboot reboot */
void fork_graceful_reboot ( int delay_in_secs )
{
    int parent = double_fork ();
    if (0 > parent) /* problem forking */
    {
        elog ("failed to fork graceful reboot process\n");
        return ;
    }
    else if (0 == parent) /* if we're the child */
    {
        char* reboot_args[] = { (char*) "/sbin/reboot", NULL };
        char* reboot_env[] = { NULL };

        setup_child(false); /* initialize the process group, etc */
        ilog ("*** Graceful Reset Thread ***\n");

        /* Commented this out because blocking SIGTERM in systemd environment
         * causes any processes that spawn this sysreq will stall shutdown
         *
         * sigset_t mask , mask_orig ;
         * sigemptyset (&mask);
         * sigaddset (&mask, SIGTERM );
         * sigprocmask (SIG_BLOCK, &mask, &mask_orig );
         *
         */
        sleep (delay_in_secs);

        execve ("/sbin/reboot", reboot_args, reboot_env);

        /* execve returns -1 on error, and does not return on success */
        elog ("Could not execute graceful reboot - error code = %d (%s)\n",
            errno, strerror(errno));
        exit (-1);
    }
}

bool is_string_in_string_list ( std::list<string> & l , string & str )
{
    if ((std::find (l.begin(), l.end(), str )) == l.end())
    {
        return (false);
    }
    return (true);
}
#define WANT_MANUAL_SEARCH
bool is_int_in_int_list ( std::list<int> & l , int & val )
{
#ifdef WANT_MANUAL_SEARCH
    std::list<int>::iterator iter_ptr ;
    for ( iter_ptr = l.begin() ; iter_ptr != l.end() ; iter_ptr++ )
    {
        if ( *iter_ptr == val )
        {
            ilog ("%d found\n", val );
            return (true) ;
        }
    }
    return (false);
#else
    if ((std::find (l.begin(), l.end(), val )) == l.end())
    {
        return (false);
    }
    return (true);
#endif
}

string get_strings_in_string_list ( std::list<string> & l )
{
    std::list<string>::iterator iter_ptr ;
    string s = "" ;

    if ( l.empty() )
        return (s);

    for ( iter_ptr = l.begin() ; iter_ptr != l.end() ; iter_ptr++ )
    {
        s.append(iter_ptr->c_str());
        s.append(" ");
    }
    return (s);
}


bool string_contains ( string buffer, string sequence )
{
    size_t found = buffer.find(sequence);
    if ( found != string::npos )
        return (true);
    else
        return (false);
}


static int health = NODE_HEALTH_UNKNOWN ;
int get_node_health ( string hostname )
{
    struct stat p ;
    memset ( &p, 0 , sizeof(struct stat));
    stat ( CONFIG_PASS_FILE, &p ) ;
    if ((p.st_ino != 0 ) && (p.st_dev != 0))
    {
        if ( health != NODE_HEALTHY )
        {
            ilog ("%s is Healthy (%d)\n", hostname.c_str(), health );
        }
        health = NODE_HEALTHY ;
    }
    else
    {
       memset ( &p, 0 , sizeof(struct stat));
       stat ( CONFIG_FAIL_FILE, &p ) ;
       if ((p.st_ino != 0 ) && (p.st_dev != 0))
       {
           if ( health != NODE_UNHEALTHY )
           {
               elog ("%s is UnHealthy\n", hostname.c_str());
           }
           else if ( health == NODE_HEALTH_UNKNOWN )
           {
               wlog ("%s is UnHealthy\n", hostname.c_str());
           }
           health = NODE_UNHEALTHY ;
       }
       else
       {
           if ( health != NODE_HEALTH_UNKNOWN )
           {
               wlog ("%s has Unknown Health\n", hostname.c_str());
           }
           health = NODE_HEALTH_UNKNOWN ;
       }
    }
    return (health);
}

int clean_bm_response_files ( string hostname )
{
    char cmd_string [200] ;
    sprintf ( &cmd_string[0], "rm -f /var/run/.bm*.%s", hostname.data());
    int rc = system(cmd_string);
    return (rc);
}


string md5sum_string ( string str )
{
    string temp ;
    unsigned char digest[MD5_DIGEST_LENGTH];
    char md5str         [MD5_STRING_LENGTH];
    memset ( &digest, 0, MD5_DIGEST_LENGTH );
    memset ( &md5str, 0, MD5_STRING_LENGTH );
    
    MD5 ((unsigned char*)str.data(), str.length(), (unsigned char*)&digest);    
 
    for(int i = 0; i < MD5_DIGEST_LENGTH; i++)
        sprintf(&md5str[i*2], "%02x", (unsigned int)digest[i]);
 
    // ilog ("user value: %s\n", buffer );
    // ilog ("md5 digest: %s\n", md5str );
    temp = md5str ;
    return (temp);
}

/* get a processid by the processname using a pipe */
int get_pid_by_name_pipe ( string procname  )
{
    pid_t pid = 0 ;
    if ( procname.length() )
    {
        char buffer[MAX_CHARS_FILENAME] ;
        snprintf ( buffer, MAX_CHARS_FILENAME, "pidof -s %s" , procname.data() );
        FILE *cmd_pipe = popen (buffer, "r" );
        if ( cmd_pipe )
        {
            memset(buffer, 0, MAX_CHARS_FILENAME );
            char * c = fgets (buffer, MAX_CHARS_FILENAME, cmd_pipe );
            UNUSED(c);
            if ( strnlen ( buffer , MAX_CHARS_FILENAME ) )
            {
                pid = strtoul (buffer, NULL, 10) ;
            }
            pclose(cmd_pipe);
        }
    }
    return (pid);
}

/* get a processid by the processname searching the proc file system */
int get_pid_by_name_proc ( string procname )
{
    int pid = -1;

    /* Open the /proc dir */
    DIR *dp = opendir("/proc");
    if (dp != NULL)
    {
        /* Enumerate all entries in directory until we find the process */
        struct dirent *dirp;
        while (pid < 0 && (dirp = readdir(dp)))
        {
            /* Skip non-numeric entries */
            int id = atoi(dirp->d_name);
            if (id > 0)
            {
                /* Read contents of virtual /proc/{pid}/cmdline file */
                string cmdPath = string("/proc/") + dirp->d_name + "/cmdline";
                ifstream cmdFile(cmdPath.c_str());
                string cmdLine;
                getline(cmdFile, cmdLine);
                if (!cmdLine.empty())
                {
                    size_t pos = cmdLine.find("python");
                    //printf ("\nCmdLine: %s (length:%ld)\n", cmdLine.c_str(), cmdLine.length());
                    if (pos != string::npos)
                    {
                        cmdLine = cmdLine.substr(7, cmdLine.length()-8);
                        //printf ("\nCmdLine Next: %s (length:%ld)\n", cmdLine.c_str(), cmdLine.length());
                        std::size_t found = cmdLine.find(procname);
                        if ( found != std::string::npos )
                        {
                            closedir(dp);
                            return(id) ;
                        }
                    }

                    /* Keep first cmdline item which contains the program path */
                    pos = cmdLine.find('\0');
                    if (pos != string::npos)
                    {
                        cmdLine = cmdLine.substr(0, pos);
                    }
                    /* removing the path prefix */
                    pos = cmdLine.rfind('/');
                    if (pos != string::npos)
                        cmdLine = cmdLine.substr(pos + 1);

                    /* is this the process ? */
                    if (procname == cmdLine)
                        pid = id;
                }
            }
        }
    }

    closedir(dp);

    return pid;
}



const char mgmnt_iface_str[] = { "Mgmnt" } ;
const char clstr_iface_str[] = { "Clstr" } ;
const char  null_iface_str[] = { "Null" } ;

const char * get_iface_name_str ( int iface )
{
    switch ( iface )
    {
        case MGMNT_IFACE:
            return mgmnt_iface_str;     
        case CLSTR_IFACE:
            return clstr_iface_str;
        default:
            return null_iface_str ;
    }
}


string get_event_str ( int event_code )
{
    switch ( event_code )
    {
        case MTC_EVENT_MONITOR_READY:
            return "ready" ;
        case MTC_EVENT_PMOND_CLEAR:
        case MTC_EVENT_PMON_CLEAR:
        case MTC_EVENT_HWMON_CLEAR:
            return "clear" ;
        case MTC_EVENT_PMON_CRIT:
        case MTC_EVENT_HWMON_CRIT:
            return "critical" ;
        case MTC_EVENT_PMON_LOG:
            return "log" ;
        case MTC_EVENT_PMON_MAJOR:
        case MTC_EVENT_HWMON_MAJOR:
            return "major" ;
        case MTC_EVENT_PMON_MINOR:
        case MTC_EVENT_HWMON_MINOR:
            return "minor" ;
        case MTC_EVENT_HWMON_CONFIG:
            return "config" ;
        case MTC_EVENT_HWMON_RESET:
            return "reset" ;
        case MTC_EVENT_HWMON_POWERDOWN:
            return "power-down" ;
        case MTC_EVENT_HWMON_POWERCYCLE:
            return "power-cycle" ;
        case MTC_DEGRADE_RAISE:
            return "degrade raise" ;
        case MTC_DEGRADE_CLEAR:
            return "degrade clear" ;
        case MTC_CMD_ADD_HOST:
            return "add" ;
        case MTC_CMD_DEL_HOST:
            return "delete" ;
        case MTC_CMD_MOD_HOST:
            return "modify" ;
        case MTC_CMD_QRY_HOST:
            return "query" ;
        case MTC_CMD_START_HOST:
            return "start" ;
        case MTC_CMD_STOP_HOST:
            return "stop" ;
        default:
        {
            slog ("Unknown event code (0x%x)\n", event_code );
            return "unknown" ;
        }
    }
}

#define MAX_NUM_LEN 64
string itos ( int val )
{
    char int_str[MAX_NUM_LEN] ;
    string temp ;
    memset  ( &int_str[0], 0, MAX_NUM_LEN );
    snprintf ( &int_str[0], MAX_NUM_LEN, "%d" , val );
    temp = int_str ;
    return (temp);
}

#define MAX_NUM_LEN 64
string lltos (long long unsigned int val )
{
    char int_str[MAX_NUM_LEN] ;
    string temp ;
    memset  ( &int_str[0], 0, MAX_NUM_LEN );
    snprintf ( &int_str[0], MAX_NUM_LEN, "%llu" , val );
    temp = int_str ;
    return (temp);
}

string ftos ( float val, int resolution )
{
    char float_str[MAX_NUM_LEN] ;
    string temp ;
    memset  ( &float_str[0], 0, MAX_NUM_LEN );
    if ( resolution == 2 )
        snprintf ( &float_str[0], MAX_NUM_LEN, "%.2f" , val );
    else if ( resolution == 3 )
        snprintf ( &float_str[0], MAX_NUM_LEN, "%.3f" , val );
    else
        snprintf ( &float_str[0], MAX_NUM_LEN, "%.1f" , val );
    temp = float_str ;
    return (temp);
}

/* standard 1s complement checksum */
unsigned short checksum(void *b, int len)
{
    unsigned short *buf = (unsigned short*)b;
    unsigned int sum=0;
    unsigned short result;

    for ( sum = 0; len > 1; len -= 2 )
        sum += *buf++;
    if ( len == 1 )
        sum += *(unsigned char*)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}


std::string tolowercase ( const std::string in )
{
  std::string out;

  std::transform( in.begin(), in.end(), std::back_inserter( out ), ::tolower );
  return out;
}



int send_log_message ( msgSock_type * sock_ptr,
                       const char   * hostname, 
                       const char   * filename,
                       const char   * log_str )
{
    int bytes = 0 ;
    int bytes_to_send = 0 ;
    int rc = PASS ;
    
    log_message_type log ;

    if (( log_str == NULL ) || ( filename == NULL ) || ( hostname == NULL ))
    {
        slog ("null parm\n");
        return (FAIL_NULL_POINTER);
    }

    if ( sock_ptr == NULL )
    {
        slog ("%s mtclogd not setup for file '%s'\n", hostname, filename );
        return (FAIL_NULL_POINTER);
    }
    else if ( sock_ptr->sock == 0 )
    {
        dlog ("%s mtclogd not setup for file '%s'\n", hostname, filename );
        return (FAIL_INVALID_OPERATION);
    }

    memset   ( &log, 0 , sizeof(log_message_type)); 
    snprintf ( &log.header   [0], MSG_HEADER_SIZE   , "%s",get_mtc_log_msg_hdr());
    snprintf ( &log.filename [0], MAX_FILENAME_LEN  , "%s",filename );
    snprintf ( &log.hostname [0], MAX_HOST_NAME_SIZE, "%s",hostname );
    snprintf ( &log.logbuffer[0], MAX_LOG_MSG       , "%s",log_str  );

    /* There is no buffer data in any of these messages */
    bytes_to_send = sizeof(log_message_type)-(MAX_LOG_MSG-(strlen(log_str))) ;

    bytes = sendto (     sock_ptr->sock, (char*) &log, bytes_to_send, 0,
    (struct sockaddr *) &sock_ptr->addr, 
                         sock_ptr->len);
    if ( bytes <= 0 )
    {
        wlog ("%s send log message failed (%s)\n", log.hostname, log.filename );
        rc = FAIL_TO_TRANSMIT ;
    }
    else
    {
        mlog2 ("%s:%s\n%s", &log.hostname[0], &log.filename[0], log_str );
    }
    return rc ;
}


/**********************************************************************************
 *
 * Name       : get_delimited_list
 *
 * Description: Update the_list with the individual items in the passed in string.
 * 
 *              valid delimiters include , : = ; . - +
 *
 * Updates: the_list
 *
 * Returns: PASS for success and FAIL_STRING_EMPTY or FAIL_INVALID_DATA otherwise
 *
 **********************************************************************************/
int get_delimited_list ( string str , char delimiter, list<string> & the_list, bool remove_whitespace )
{
    std::size_t last  = 0 ;
    std::size_t first = 0 ;

    /* Error handling - empty string and invalid delimitors */
    if ( str.empty () )
    {
        dlog ("empty string\n");
        return ( FAIL_STRING_EMPTY ) ;
    }
    else if (( delimiter != '.' ) && ( delimiter != ',' ) &&
             ( delimiter != '-' ) && ( delimiter != '+' ) &&
             ( delimiter != '=' ) && ( delimiter != ';' ) &&
             ( delimiter != ':' ))
    {
        dlog ("invalid delimiter\n");
        return ( FAIL_INVALID_DATA ) ;
    }
    
    // ilog ("String: <%s>\n", str.c_str());

    do
    {
        last = str.find_first_of(delimiter, first );
        string temp_str = str.substr(first, last-first) ;
            
        /* TODO: Add support for stripping off whitespace */
        if ( remove_whitespace == true )
        {
            // std::string::iterator _str ;
            // _str = std::remove(temp_str.begin(), temp_str.end(), ' ');
            // string xx = std::remove_if(temp_str.begin(), temp_str.end(), isspace);
            // ilog ("XX: <%s>\n", (*_str).c_str());
            ;
        }
        
        // dlog ("List Item: <%s> (%ld:%ld)\n", temp_str.c_str(), first, last);
        the_list.push_back(temp_str);

        /* prepare for next loop */
        if ( last != std::string::npos )
        {
            first = last+1 ; // dlog (" > First: %ld\n", first );
        }
    } while ( last != std::string::npos ) ;

#ifdef WANT_DEBUG
    std::list<string>::iterator iter_ptr ;
    for ( iter_ptr  = the_list.begin();
          iter_ptr != the_list.end() ;
          iter_ptr++ )
    {
        ilog ("List: <%s>\n", iter_ptr->c_str());
    }
#endif

    return (PASS);
}

/* Name: update_config_option
 *
 * 1. free what is in *config_ptr_ptr (if not null)
 * 2. allocate new memory pointed for the supplied string
 */
void update_config_option ( const char ** config_ptr_ptr, string str2dup )
{
    if ( *config_ptr_ptr != NULL )
    {
        dlog1 ("Modifying config from '%s' to '%s'\n", *config_ptr_ptr, str2dup.c_str());
        free ( (void*)(*config_ptr_ptr) ) ;
    }
    else
    {
        dlog1 ("Adding %s config\n", str2dup.c_str());
    }
    *config_ptr_ptr = strdup(str2dup.data());
    dlog1 ("New Config %s\n", *config_ptr_ptr);
}


static const char bar [] = { "-----------------------------------------------------------------------------------------\n"} ;
static const char ban [] = { "Service State and Traceback -------------------------------------------------------------\n"} ;
std::list<string>           mem_log_list ;
std::list<string>::iterator mem_log_iter ;

void mem_log_list_init ( void )
{
    mem_log_list.clear();
}

/* Log a label int value and string of other data */
void mem_log ( string label , int value , string data )
{
    char str[MAX_MEM_LOG_LEN] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s %d %s\n", label.c_str(), value, data.c_str());
    mem_log (str);
}

void mem_log ( string one, string two )
{
    char str[MAX_MEM_LOG_LEN] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s%s\n", one.c_str(), two.c_str());
    mem_log (str);
}

void mem_log ( string one, string two, string three )
{
    char str[MAX_MEM_LOG_LEN] ;
    snprintf (&str[0], MAX_MEM_LOG_DATA, "%s%s%s\n", one.c_str(), two.c_str(), three.c_str());
    mem_log (str);
}

/* log a character string */
void mem_log ( char * log )
{
    // string full_log = pt() ;
    // full_log.append(": ");
    string full_log = log ;

    mem_log_list.push_back ( full_log ) ;

    /* Don't allow the in-memory list to exceed MAX_MEM_LIST_SIZE */
    if ( mem_log_list.size() > MAX_MEM_LIST_SIZE )
    {
        mem_log_list.pop_front();
    }
}

/* Log a single character ; typically used to add a linefeed to the trace log */
void mem_log ( char char_log )
{
    string tmp = "";
    tmp.insert(tmp.begin(),char_log) ;
    mem_log ( tmp ); /* Call string proto */
}

/* log a string */
void mem_log ( string log )
{
    // string full_log = pt() ;
    // full_log.append(": ");
    string full_log = log ;

    mem_log_list.push_back ( full_log ) ;

    /* Don't allow the in-memory list to exceed MAX_MEM_LIST_SIZE */
    if ( mem_log_list.size() >= MAX_MEM_LIST_SIZE )
    {
        mem_log_list.pop_front();
    }
}

void daemon_dump_membuf_banner ( void )
{
    syslog ( LOG_INFO, "%s", &bar[0]);
    syslog ( LOG_INFO, "%s", &ban[0]);
    syslog ( LOG_INFO, "%s", &bar[0]);
}

/* Dump the in-memory trace buffer to syslog */
void daemon_dump_membuf ( void )
{
    int i = 0 ;
    int usec_delay = 1 ;

    if ( mem_log_list.empty () )
        return ;

    /* as the data grows so do we have to accept loosing data over stalling process */
    if ( mem_log_list.size() < 200 )
        usec_delay = 99 ;
    else if ( mem_log_list.size() < 1000 )
        usec_delay = 10 ;

    /* Run Maintenance on Inventory */
    for ( mem_log_iter  = mem_log_list.begin () ;
          mem_log_iter != mem_log_list.end   () ;
          mem_log_iter++ )
    {
        /* sleep for usec_delay every 10 logs so we don't overload syslog */
        if (( ++i % 10 ) == 0 )
        {
            usleep (usec_delay);
        }
        syslog ( LOG_INFO, "%3d| %s", i, mem_log_iter->c_str() );
    }
    mem_log_list.clear();
}

#define BUFFER_SIZE 128

/*****************************************************************************
 *
 * Name    : execute_pipe_cmd
 *
 * Purpose : Obtain the result of a bash command.
 *
 * Params  : command     - char buffer containing the bash command
 *           result      - char buffer for storing the result of the command
 *           result_size - size of result buffer
 *
 * Return  : PASS/FAIL
 *
 *****************************************************************************/
int execute_pipe_cmd(const char *command, char *result, unsigned int result_size) {

    /* Local variables. */
    char fsLine[BUFFER_SIZE];
    char *pos;
    string data;
    FILE *pFile;
    int rc = 0;

    /* Initialize to zero the result buffer. */
    memset(result, 0, result_size);

    /* Execute command. */
    if ((pFile = popen(command, "r")) == NULL) {
        elog("Error executing command: %s", command);
        return (FAIL);
    } else {
        while ((memset(fsLine, 0, sizeof(fsLine))) &&
               (fgets((char *) &fsLine, sizeof(fsLine), pFile) != NULL)) {
            data.append(fsLine);
        }
        int ret = pclose(pFile);
        rc = WEXITSTATUS(ret);
    }

    /* Extract result. */
    strncpy(result, data.c_str(), result_size);
    if (data.length() < result_size - 1) {
        /* Eliminate trailing newline. */
        if ((pos=strchr(result, '\n')) != NULL)
            *pos = '\0';
    }
    else {
        *(result + result_size -1) = '\0'; // in this case, strncpy does not terminate string
        elog("Result of executed command is larger than result buffer; "
             "result size: %i, buffer size: %i", int(data.length()), int(result_size));
        wlog("...cmd: '%s' exit status: %i truncated result: '%s'", command, rc, result);
        return (FAIL);
    }

    dlog("cmd: '%s' exit status: %i result: '%s'\n",
         command, rc, result);

    return (rc);
}

/****************************************************************************
 *
 * Name:       get_system_state
 *
 * Purpose:    Query and return system running state
 *
 * https://www.freedesktop.org/software/systemd/man/systemctl.html
 *
 * Refer to is-system-running command.
 *
 * Note: Return code is > 0 for all cases except for running.
 *
 *         Name - Description
 * ------------   --------------------------------------------
 * initializing - Early bootup, before basic.target is reached.
 *     starting - Late bootup, before the job queue becomes idle for the first time.
 *      running - The system is fully operational. rc = 0
 *     degraded - The system is operational but one or more units failed.
 *  maintenance - The rescue or emergency target is active.
 *     stopping - The manager is shutting down.
 *      offline - The manager is not running, faulty system manager (PID 1).
 *      unknown - The operational state could not be determined.
 *
 * Returns one of corresponding 'mtc_system_state_enum' defined in nodeUtil.h
 *
 ****************************************************************************/

#ifndef PIPE_COMMAND_RESPON_LEN
#define PIPE_COMMAND_RESPON_LEN (100)
#endif

system_state_enum get_system_state ( void )
{
    char pipe_cmd_output [PIPE_COMMAND_RESPON_LEN] ;
    execute_pipe_cmd ( "systemctl is-system-running", &pipe_cmd_output[0], PIPE_COMMAND_RESPON_LEN );
    if ( strnlen ( pipe_cmd_output, PIPE_COMMAND_RESPON_LEN ) > 0 )
    {
        ilog ("systemctl reports host as '%s'\n", pipe_cmd_output );
        string temp = pipe_cmd_output ;
        if ( temp.find ("stopping") != string::npos )
            return MTC_SYSTEM_STATE__STOPPING;
        if ( temp.find ("running") != string::npos )
            return MTC_SYSTEM_STATE__RUNNING;
        if ( temp.find ("degraded") != string::npos )
            return MTC_SYSTEM_STATE__DEGRADED;
        if ( temp.find ("starting") != string::npos )
            return MTC_SYSTEM_STATE__STARTING;
        if ( temp.find ("initializing") != string::npos )
            return MTC_SYSTEM_STATE__INITIALIZING;
        if ( temp.find ("offline") != string::npos )
            return MTC_SYSTEM_STATE__OFFLINE;
        if ( temp.find ("maintenance") != string::npos )
            return MTC_SYSTEM_STATE__MAINTENANCE;
        slog ("unexpected response: <%s>\n", temp.c_str());
    }
    else
    {
        wlog ("systemctl is-system-running yielded no response\n");
    }
    return MTC_SYSTEM_STATE__UNKNOWN ;
}
