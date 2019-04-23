/*
 * Copyright (c) 2013, 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform netlink listener event support for maintenance
  */

void close_netlink_socket ( int socket );
int   open_netlink_socket ( int groups );
int    get_netlink_events ( int nl_socket , 
                            std::list<string> & links_gone_down, 
                            std::list<string> & links_gone_up );
void log_link_events ( int netlink_sock,
                       int ioctl_sock, 
                       const char * mgmnt_iface_ptr, 
                       const char * clstr_iface_ptr,
                       bool & mgmnt_link_up_and_running,
                       bool & clstr_link_up_and_running);

