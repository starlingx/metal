#ifndef __INCLUDE_NODEUTIL_H__
#define __INCLUDE_NODEUTIL_H__

/*
* Copyright (c) 2013-2014, 2016, 2019, 2024 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
*/

#include <string.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <list>
#include <evhttp.h>          /* for ... HTTP_ status definitions */

using namespace std;

#include "nodeBase.h"

#define LATENCY_1500MSECS              (1500)
#define LATENCY_1SEC                   (1000)
#define LATENCY_50MSECS                  (50)
#define LATENCY_100MSECS                (100)
#define LATENCY_200MSECS                (200)
#define LATENCY_600MSECS                (600)
#define NODEUTIL_LATENCY_MON_START ((const char *)"start")
void nodeUtil_latency_log ( string hostname, const char * label_ptr, int msecs );

// path to the Debian network interfaces directory
#define NETWORK_INTERFACES_DIR (const char *)("/etc/network/interfaces.d")

/* Common socket type struct */
typedef struct
{

    int                 port   ; /**< ... port number         */
    int                 sock   ; /**< ... socket fd           */
    struct sockaddr_in  addr   ; /**< ... attributes struct   */
    socklen_t           len    ; /**< ... length              */

} msgSock_type ;

int send_log_message ( msgSock_type * sock_ptr,
                       const char   * hostname,
                       const char   * filename,
                       const char   * log_str );

msgSock_type * get_mtclogd_sockPtr ( void ) ;

void mem_log_list_init ( void );

string md5sum_string ( string str );

string getipbyname   ( string name );
string getipbynameifexists   ( string name );
string getipbyiface ( const char * iface );

int  get_ip_addresses   ( string & my_hostname , string & my_local_ip , string & my_float_ip );
int  get_iface_address  ( const char * iface_ptr, string & ip_addr , bool retry );
int  get_iface_hostname ( const char * iface_ptr, char * hostname_ptr);
int  get_iface_macaddr  ( const char * iface_ptr , string & macaddr );
void get_clstr_iface    ( char ** clstr_iface_ptr );
int  get_hostname ( char * hostname_ptr, int max_len );
string get_iface_mac ( const char * iface_ptr );

void print_inv ( node_inv_type & info );
int  get_iface_attrs ( const char * iface_ptr, int & index, int & speed , int & duplex , string & autoneg );

const char * get_iface_name_str ( int iface );
const char * get_interface_name_str ( int iface );

/* Used to learn the pxeboot address */
enum iface_type_enum { ethernet = 0, vlan = 1, bond = 2 };
typedef struct
{
    string iface_name ;
    iface_type_enum iface_type = ethernet ;

    /* vlan link ; physical or bond
     *
     * The parent interface is the physical network interface
     * to which the VLAN is associated.*/
    string parent = "" ;

    /* bond links ; two physical interfaces
     *
     * A bond is a logical interface created by combining multiple
     * physical network interfaces, known as "slaves"*/
    string slave1 = "" ;
    string slave2 = "" ;

    /* bonding mode ;  active-backup, balanced-xor, 802.3ad, etc.
     * A string that represents the bonding mode string and id
     * Example: 802.3ad 4 */
    string bond_mode = "" ;

    /* string representing the iface hierarchy.
     *
     *  ethernet
     *  bond -> slaves
     *  vlan -> bond -> slaves
     *
     * This interface chain string exists soley for the purpose
     * of logging for the report tool system info. */
    string chain = "" ;
} iface_info_type ;

#define INTERFACES_DIR ((const char *)"/sys/class/net/")

const char * get_iface_type_str ( iface_type_enum type_enum );
int    get_iface_type     ( string iface,
                            iface_type_enum & iface_type );
int    get_iface_parent   ( int network,
                            string & ifname,
                            string & parent );
int    get_bond_slaves    ( int network,
                            string bonded_iface,
                            string & slave1,
                            string & slave2 );
int    get_bond_mode      ( int network,
                            string bonded_iface,
                            string & bond_mode);
int    get_iface_info     ( int network,
                            string iface,
                            iface_info_type & iface_info);

// For the mtcClient pxeboot address learning.
string get_pxeboot_dhcp_addr   ( string iface ); // worker/storage
string get_pxeboot_static_addr ( string iface ); // controllers

unsigned int  get_host_function_mask ( string & nodeType_str );
bool          is_combo_system (unsigned int nodetype_mask );

int set_host_functions ( string         nodetype_str,
                         unsigned int * nodetype_bits_ptr,
                         unsigned int * nodetype_function_ptr,
                         unsigned int * nodetype_subfunction_ptr );

bool is_goenabled ( int nodeType, bool pass );

string get_strings_in_string_list ( std::list<string> & l );
bool is_string_in_string_list ( std::list<string> & l , string & str );
bool is_int_in_int_list ( std::list<int> & l , int & val );
bool string_contains    ( string buffer, string sequence );
int  load_filenames_in_dir ( const char * directory, std::list<string> & filelist );

int  double_fork ( void );
int  double_fork_host_cmd ( string hostname , char * cmd_string, const char * cmd_oper );
int  setup_child ( bool close_file_descriptors );
void fork_sysreq_reboot ( int delay_in_secs );
void fork_graceful_reboot ( int delay_in_secs );

int  get_node_health         ( string hostname );
int  clean_bm_response_files ( string hostname );
int  get_pid_by_name_proc    ( string procname );
int  get_pid_by_name_pipe    ( string procname );

int  get_link_state    ( int   ioctl_socket , const char * iface_ptr, bool * running_ptr );
int  open_ioctl_socket ( void );

string get_event_str ( int event_code );

string itos ( int val );
string lltos (long long unsigned int val );
string ftos ( float val, int resolution );
unsigned short checksum(void *b, int len);

std::string tolowercase ( const std::string in );


int get_delimited_list ( string str , char delimiter, list<string> & the_list, bool remove_whitespace );
void update_config_option ( const char ** config_ptr_ptr, string str2dup );


void dump_memory ( void * raw_ptr , int format, size_t bytes );

int execute_pipe_cmd(const char *command, char *result, unsigned int result_size);

typedef enum
{
   MTC_SYSTEM_STATE__INITIALIZING,
   MTC_SYSTEM_STATE__STARTING,
   MTC_SYSTEM_STATE__RUNNING,
   MTC_SYSTEM_STATE__DEGRADED,
   MTC_SYSTEM_STATE__MAINTENANCE,
   MTC_SYSTEM_STATE__STOPPING,
   MTC_SYSTEM_STATE__OFFLINE,
   MTC_SYSTEM_STATE__UNKNOWN
} system_state_enum ;

system_state_enum get_system_state ( bool verbose=true );
const char * get_system_state_str ( system_state_enum system_state );

/**
 * @brief                   execute command string using fork execv
 *
 * @param hostname          The hostname
 * @param cmd               command string
 * @param output_filename   output redirection file
 *                          stdout and stderr will be redirected to
 *                          this file
 *
 * @return int              return code, 0 (PASS) on success
 */
int fork_execv (const string& hostname,
                const string& cmd,
                const string& output_filename);

#endif
