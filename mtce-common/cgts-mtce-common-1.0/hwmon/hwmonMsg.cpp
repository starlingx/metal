/*
 * Copyright (c) 2013, 2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Process Monitor Service Messaging
  */


#include "hwmon.h"       /*                                    */
#include "hwmonClass.h"  /* for ... get_hwmonHostClass_ptr     */
#include "nodeMacro.h"   /*                                    */
#include "hwmonJson.h"   /* for ... hwmonJson_load_inv         */

/* Where to send events */
string mtcAgent_ip = "" ;

/**
 * Messaging Socket Control Struct - The allocated struct
 */
static
hwmon_socket_type   hwmon_sock;
hwmon_socket_type * getSock_ptr ( void )
{
    return ( &hwmon_sock );
}

msgSock_type * get_mtclogd_sockPtr ( void )
{
    return (&hwmon_sock.mtclogd);
}

/****************************/
/* Initialization Utilities */
/****************************/

/* Init the messaging socket control structure */
void hwmon_msg_init ( void )
{
    memset(&hwmon_sock, 0, sizeof(hwmon_sock));
}

void hwmon_msg_fini ( void )
{
    if ( hwmon_sock.event_sock )
    {
        delete (hwmon_sock.event_sock);
        hwmon_sock.event_sock = 0 ;
    }
    if ( hwmon_sock.cmd_sock )
    {
        delete (hwmon_sock.cmd_sock);
        hwmon_sock.cmd_sock = 0 ;
    }
    if ( hwmon_sock.mtclogd.sock > 0 )
    {
       close (hwmon_sock.mtclogd.sock);
       hwmon_sock.mtclogd.sock = 0 ;
    }
}


/*********************************************************************/
/* Setup hwmon command receive port/socket                          */
/*********************************************************************/
int cmd_rx_port_init ( int port )
{
    int rc = PASS ;
    hwmon_sock.cmd_port = port ;

    mtcAgent_ip = getipbyname ( CONTROLLER );

    /* setup unicast maintenance command receive socket */
    hwmon_sock.cmd_sock = new msgClassRx(mtcAgent_ip.c_str(), hwmon_sock.cmd_port, IPPROTO_UDP, NULL, true);
    if ( rc  )
    {
        elog ("Failed to setup maintenance command receive socket (rc:%d)", rc);
        return (rc);
    }

    return (rc);
}

/*********************************************************************/
/* Setup hwmon broadcast transmit port/socket                        */
/*********************************************************************/
int event_tx_port_init ( int port, const char * iface )
{
    int rc  = PASS ;

    /* Load the event port number */
    hwmon_sock.event_port  = port ;

    mtcAgent_ip = getipbyname ( CONTROLLER );
    ilog ("ControllerIP: %s\n", mtcAgent_ip.c_str());

    hwmon_sock.event_sock = new msgClassTx(mtcAgent_ip.c_str(),hwmon_sock.event_port , IPPROTO_UDP, iface);
    if ( rc )
    {
        elog ("Failed to setup mtce to hbs transmit command port %d\n", port );
        return (rc) ;
    }

    return (PASS);
}

int mtclogd_tx_port_init ( void )
{
    int rc = PASS ;
    int port = hwmon_sock.mtclogd.port = daemon_get_cfg_ptr()->daemon_log_port ;
    CREATE_REUSABLE_INET_UDP_TX_SOCKET ( LOOPBACK_IP,
                                         port,
                                         hwmon_sock.mtclogd.sock,
                                         hwmon_sock.mtclogd.addr,
                                         hwmon_sock.mtclogd.port,
                                         hwmon_sock.mtclogd.len,
                                         "mtc logger message",
                                         rc );
    if ( rc )
    {
        elog ("Failed to setup messaging to mtclogd on port %d\n", port );
    }
    return (rc);
}


int hwmon_send_event ( string hostname, unsigned int event_code , const char * sensor_ptr )
{
    mtc_message_type event ;

    int rc    = FAIL ;
    int bytes = 0    ;

    memset (&event, 0 , sizeof(mtc_message_type));

    if (( event_code == MTC_EVENT_MONITOR_READY)||
        ( event_code == MTC_EVENT_HWMON_CLEAR  )||
        ( event_code == MTC_EVENT_HWMON_CONFIG) ||
        ( event_code == MTC_EVENT_HWMON_MINOR)  ||
        ( event_code == MTC_EVENT_HWMON_MAJOR)  ||
        ( event_code == MTC_EVENT_HWMON_CRIT )  ||
        ( event_code == MTC_EVENT_HWMON_RESET )    ||
        ( event_code == MTC_EVENT_HWMON_POWERDOWN) ||
        ( event_code == MTC_EVENT_HWMON_POWERCYCLE)||
        ( event_code == MTC_DEGRADE_RAISE ) ||
        ( event_code == MTC_DEGRADE_CLEAR ))
    {
        mlog ("%s sending '%s' event to mtcAgent for '%s'\n", 
                  hostname.c_str(), 
                  get_event_str(event_code).c_str(),
                  sensor_ptr );

        snprintf ( &event.hdr[0], MSG_HEADER_SIZE, "%s", get_mtce_event_header());
       
        snprintf ( &event.hdr[MSG_HEADER_SIZE] , MAX_CHARS_HOSTNAME , "%s", hostname.data());
        if ( sensor_ptr )
        {
            size_t len = strnlen ( sensor_ptr, MAX_SENSOR_NAME_LEN );
            
            /* We don't use the buffer for hwmon events to remove it from the size */
            bytes = ((sizeof(mtc_message_type))-(BUF_SIZE-len));

            snprintf ( &event.buf[0], MAX_SENSOR_NAME_LEN, "%s", sensor_ptr );
        }
    }
    else if ( event_code == MTC_EVENT_LOOPBACK )
    {
        snprintf ( &event.hdr[MSG_HEADER_SIZE] , MAX_CHARS_HOSTNAME , "%s", hostname.data());
        snprintf ( &event.hdr[0] , MSG_HEADER_SIZE, "%s", get_loopback_header());

        /* We don't use the buffer for hwmon events to remove it from the size */
        bytes = ((sizeof(mtc_message_type))-(BUF_SIZE));
    }
    else
    {
        elog ("Unsupported process monitor event (%d)\n", event_code );
        return ( FAIL_BAD_PARM );
    }

    /* Update the event code */
    event.cmd = event_code ;

    /* Send the event */
    if ((rc = hwmon_sock.event_sock->write((char*)&event.hdr[0],bytes)) != bytes )
    {
        elog ("Message send failed. (%d)\n", rc);
        elog ("Message: %d bytes to <%s:%d>\n", bytes,
                hwmon_sock.event_sock->get_dst_str(),
                hwmon_sock.event_sock->get_dst_addr()->getPort());
        rc = FAIL_SOCKET_SENDTO ; 
    }
    else
    {
        mlog ("Sending '%s' Event with %d bytes to %s:%d\n",
                  get_event_str (event.cmd).c_str(), bytes,
                  hwmon_sock.event_sock->get_dst_str(),
                  hwmon_sock.event_sock->get_dst_addr()->getPort());
        print_mtc_message (&event);
        rc = PASS ;
    }
    return rc ;
}

/* Receive maintnance command messages */
int  hwmon_service_inbox  ( void )
{
    int bytes ;
    mtc_message_type msg ;

    int rc = PASS ;

    /* clean the rx/tx buffer */ 
    memset ((void*)&msg,0,sizeof(mtc_message_type));
    bytes = hwmon_sock.cmd_sock->read((char*)&msg.hdr[0], sizeof(mtc_message_type));
    if( bytes <= 0 )
    {
        if ( ( errno == EINTR ) || ( errno == EAGAIN ))
        {
            return (RETRY);
        }
        else
        {
            elog ("receive error (%d:%s)\n", errno , strerror (errno));
            return (FAIL_TO_RECEIVE);
        }
    }
    /* Check for response messages */
    else if ( strstr ( &msg.hdr[0], get_cmd_req_msg_header() ) )
    {
        node_inv_type inv ;
        node_inv_init (inv);

        mlog("Receive <%s> from %s:%x\n", &msg.hdr[0],
                hwmon_sock.cmd_sock->get_src_str(),
                hwmon_sock.cmd_sock->get_dst_addr()->getPort());

        print_mtc_message ( &msg );

        if ( !strnlen ( &msg.hdr[MSG_HEADER_SIZE], MAX_CHARS_HOSTNAME ))
        {
            wlog ("Mtce message (%x) did not specify target hostname\n", msg.cmd );
            return (FAIL_UNKNOWN_HOSTNAME);
        }

        rc = hwmonJson_load_inv ( &msg.buf[0], inv );
        if ( rc )
        {
            wlog ("%s failed to parse host info\n", inv.name.c_str());
            return (FAIL_KEY_VALUE_PARSE);
        }

        rc = PASS;
        if ( msg.cmd == MTC_CMD_ADD_HOST )
        {
            /* If the add returns a RETRY that means this host was already
             * provisioned so turn around and run the modify */
            if ( get_hwmonHostClass_ptr()->add_host ( inv ) == RETRY )
            {
                mlog ("%s modify host (from add ) message\n", inv.name.c_str());
                get_hwmonHostClass_ptr()->mod_host ( inv );
            }
            else
            {
                mlog ("%s add host message\n", inv.name.c_str());
            }

        }
        else if ( msg.cmd == MTC_CMD_DEL_HOST )
        {
            hwmonHostClass * obj_ptr = get_hwmonHostClass_ptr();
            ilog ("%s Delete Host message\n", inv.name.c_str());
            obj_ptr->request_del_host ( inv.name );
        }
        else if ( msg.cmd == MTC_CMD_START_HOST )
        {
            mlog ("%s start monitoring message\n", inv.name.c_str());
            get_hwmonHostClass_ptr()->mon_host ( inv.name , true );
        }
        else if ( msg.cmd == MTC_CMD_STOP_HOST )
        {
            mlog ("%s stop monitoring message\n", inv.name.c_str());
            get_hwmonHostClass_ptr()->mon_host ( inv.name , false );
        }
        else if ( msg.cmd == MTC_CMD_MOD_HOST )
        {
            /* If the add returns a RETRY that means this host was already
             * provisioned so turn around and run the modify otherwise
             * default the modify to be an add */
            if ( get_hwmonHostClass_ptr()->add_host ( inv ) == RETRY )
            {
                mlog ("%s modify host message\n", inv.name.c_str());
                get_hwmonHostClass_ptr()->mod_host ( inv );
            }
            else
            {
                mlog ("%s add host (from modify) message\n", inv.name.c_str());
            }
        }
        else if ( msg.cmd == MTC_CMD_QRY_HOST )
        {
            mlog ("%s query host message - NOT IMPLEMENTED YET !!!\n", inv.name.c_str());
        }
        else if ( msg.cmd == MTC_CMD_LOOPBACK )
        {
            mlog ("Loopback command received\n");
        }
        else
        {
            rc = FAIL_BAD_PARM ;
            elog ( "Unsupported maintenance command (%d)\n", msg.cmd );
        }   
    }
    else 
    {
        elog ("Unsupported Message\n");
        print_mtc_message ( &msg ) ;
        rc = FAIL_BAD_CASE ;
    }

#ifdef WANT_COMMAND_RESPONSE
       
    /* TODO: Test and enable reply message */
    // snprintf ( &msg.hdr[0], MSG_HEADER_SIZE, "%s", get_cmd_rsp_msg_header());           
    if ( rc == PASS )
    {      
        bytes = sizeof(mtc_message_type)-BUF_SIZE;
        rc = sendto( hwmon_sock.mtc_client_tx_sock, 
                     (char*)&msg.hdr[0], bytes , 0,
                     (struct sockaddr *) &hwmon_sock.agent_addr, 
                                   sizeof(hwmon_sock.agent_addr));
        if (rc != bytes )
        {
            elog ("message send failed. (%d)\n", rc);
            elog ("message: %d bytes to <%s>\n", bytes,
                   inet_ntoa(hwmon_sock.client_addr.sin_addr ));
            rc = FAIL ;
        }
        else
        {
            mlog ("Response: <%s> to %s:%d\n", &msg.hdr[0],
                     inet_ntoa(hwmon_sock.client_addr.sin_addr),
                         ntohs(hwmon_sock.agent_addr.sin_port)); 
        }
        fflush(stdout);
    }
#endif
    return (rc);
}

