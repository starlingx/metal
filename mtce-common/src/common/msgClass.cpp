/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
#include "msgClass.h"
#include "daemon_common.h"

/**
 * Creating a msgClassAddr address without port and protocol is
 * to allow an easy way to check the version of an address.  It can
 * not be used to create a socket.
 *
 * @param IP address or hostname
 */
msgClassAddr::msgClassAddr(const char* address)
{
    initAddr(address, 0, 0);
    this->return_status = FAIL_BAD_PARM;
}


/**
 * @param IP address or hostname
 * @param host-byte order port
 * @param IP protocol
 */
msgClassAddr::msgClassAddr(const char* address, int port, int proto)
{
    this->return_status = initAddr(address, port, proto);
}


/* copy constructor */
msgClassAddr::msgClassAddr(const msgClassAddr& addr)
{
    this->return_status = initAddr(addr.address_str, addr.port, addr.proto);
}


/**
 * To be used by constructors to initialize the instance.  Uses
 * getaddrinfo to allocate the sockaddr and determine the IP
 * version, then builds based on the IP version.  Also sets
 * the sockaddr's port to the network-byte order version of the
 * host-byte order port value passed in.
 *
 *
 * @param IP address or hostname
 * @param host-byte order port
 * @param IP protocol
 *
 * @return PASS if successful, failure code otherwise
 */
int msgClassAddr::initAddr(const char* address, int port, int proto)
{
    int rc;
    struct addrinfo *res = NULL;
    struct addrinfo hints;
    this->proto = proto;
    this->port = port;
    this->address_str = new char[strlen(address)+1];
    this->address_numeric_string = new char[INET6_ADDRSTRLEN];
    snprintf(this->address_str, strlen(address)+1, "%s", address);
    this->addr_any = false;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET6;
    rc = getaddrinfo(this->address_str, NULL, &hints, &res);
    if (!rc) {
        // It is to resolve the issue of devstack found on Ubuntu Bionic. To
        // avoid impacting normal StarlingX, add below control gate to only
        // enable the code on Ubuntu Bionic.
        if (getenv("UBUNTU_BIONIC")) {
            /* check if returned ipv6 address is correct*/
            if (res->ai_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)res->ai_addr;
                if ((in6->sin6_scope_id == 0) &&
                    (in6->sin6_addr.s6_addr[0] == 0xFE) &&
                    ((in6->sin6_addr.s6_addr[1] & 0xC0) == 0x80))
                {
                    /* ipv6 link-local address should has non-zero sin6_scope_id */
                    ilog("link-local ipv6 address has zero sin6_scope_id, switch to ipv4\n");
                    rc = 1;
                }
            }
        }
    }
    if(rc)
    {
        dlog("IPv6 address resolution failed, rc=%d", rc);
        if(res)
        {
            freeaddrinfo(res);
        }
        hints.ai_family = AF_INET;
        rc = getaddrinfo(this->address_str, NULL, &hints, &res);
        if(rc)
        {
            dlog("IPv4 address resolution failed, rc=%d", rc);
            this->address_info = NULL;
            delete[] this->address_numeric_string;
            this->address_numeric_string = NULL;
            return FAIL_HOSTADDR_LOOKUP;
        }
    }
    this->address_info = res;
    switch(this->getIPVersion())
    {
    case AF_INET:
        inet_ntop(AF_INET, &((sockaddr_in*)this->address_info->ai_addr)->sin_addr, this->address_numeric_string, INET_ADDRSTRLEN);
        ((sockaddr_in*)this->address_info->ai_addr)->sin_port = htons(port);
        return PASS;
        break;
    case AF_INET6:
        inet_ntop(AF_INET6, &((sockaddr_in6*)this->address_info->ai_addr)->sin6_addr, this->address_numeric_string, INET6_ADDRSTRLEN);
        ((sockaddr_in6*)this->address_info->ai_addr)->sin6_port = htons(port);
        return PASS;
        break;
    default:
        delete[] this->address_numeric_string;
        this->address_numeric_string = NULL;
        wlog("IP version %d not supported", this->getIPVersion());
        return FAIL_NO_IP_SUPPORT;
        break;
    }
}


/* destructor */
msgClassAddr::~msgClassAddr()
{
    if(this->address_str)
    {
        delete[] this->address_str;
    }

    if(this->address_numeric_string)
    {
        delete[] this->address_numeric_string;
    }

    if(this->address_info)
    {
        freeaddrinfo(this->address_info);
    }
}


/**
 * @return IP address or unresolved hostname this instance was created with
 */
const char* msgClassAddr::toString() const
{
    if(this->address_info == NULL)
    {
        return NULL;
    }
    return this->address_str;
}


/**
 * @return IP address or resolved hostname this instance was created with
 */
const char* msgClassAddr::toNumericString() const
{
    if(this->address_info == NULL)
    {
        return NULL;
    }
    if(this->address_numeric_string == NULL)
    {
        return NULL;
    }
    switch(this->getIPVersion())
    {
    case AF_INET:
        inet_ntop(AF_INET, &((sockaddr_in*)this->address_info->ai_addr)->sin_addr, this->address_numeric_string, INET6_ADDRSTRLEN);
        break;
    case AF_INET6:
        inet_ntop(AF_INET6, &((sockaddr_in6*)this->address_info->ai_addr)->sin6_addr, this->address_numeric_string, INET6_ADDRSTRLEN);
        break;
    }
    return this->address_numeric_string;
}


/**
 * @return IP version of address
 */
int msgClassAddr::getIPVersion() const
{
    if(this->address_info == NULL)
    {
        return AF_UNSPEC;
    }
    return this->address_info->ai_family;
}


/**
 * @return IP protocol instance was created with
 */
int msgClassAddr::getIPProtocol() const
{
    return this->proto;
}


/**
 * @return port in host-byte order
 */
int msgClassAddr::getPort() const
{
    return this->port;
}


/**
 * Constant accessor for sockaddr
 * Intended to be used external to msgClassSock
 */
const struct sockaddr* msgClassAddr::getSockAddr() const
{
    if(this->address_info == NULL)
    {
        return NULL;
    }
    return this->address_info->ai_addr;
}


/**
 * Non-constant accessor for sockaddr.
 * Intended to be used internally by msgClassSock
 */
struct sockaddr* msgClassAddr::getSockAddr()
{
    if(this->address_info == NULL)
    {
        return NULL;
    }
    return this->address_info->ai_addr;
}


/*
 * if(this->getIPVersion==AF_INET)
 *     return sizeof(sockaddr_in);
 * if(this->getIPVersion==AF_INET)
 *     return sizeof(sockaddr_in6);
 *
 * @return size of the sockaddr
 */
socklen_t msgClassAddr::getSockLen() const
{
    if(this->address_info == NULL)
    {
        return 0;
    }
    return this->address_info->ai_addrlen;
}


/**
 * Given an interface, find the hostname that resolves on this interface
 * and use that to get the IP address of this interface. Will only resolve
 * hostnames on the Management or Infra interfaces
 *
 * @param Name of the interface to get address for
 * @param Character pointer to pass back address of interface
 * @param Length of character array.
 * @return Returns PASS if (mgmnt or infra) interface has address, FAIL otherwise
 */
int msgClassAddr::getAddressFromInterface(const char* interface, char* address, int len)
{
    int rc = FAIL;

    // before proceeding further, confirm if the interface
    // is either the management interface or the infra interface.
    // Mtce doesn't care about others besides these.
    iface_enum interface_type = iface_enum(0);
    char *infra_iface_name = NULL;

    get_infra_iface(&infra_iface_name);
    if (infra_iface_name && strlen(infra_iface_name)) {
        if (!strcmp(interface, infra_iface_name)) {
            if (!strcmp(infra_iface_name, daemon_mgmnt_iface().data())) {
                // infra and mgmt interface name are the same
                interface_type = MGMNT_IFACE;
            }
            else {
                // requesting address for the infra interface
                interface_type = INFRA_IFACE;
            }
        }
        free (infra_iface_name);
    }

    if (interface_type != INFRA_IFACE) {
        // check if this is the mgmt interface
        // otherwise return error
        if (!strcmp(interface, daemon_mgmnt_iface().data())) {
            interface_type = MGMNT_IFACE;
            dlog ("Resolving %s as Management interface", interface);
        } else {
            return rc;
        }
    }
    char hostname[MAX_HOST_NAME_SIZE+1] = {0};
    if (gethostname(hostname,
            MAX_HOST_NAME_SIZE) < 0) {
        elog("Failed to get system host name (err: %d)", errno);
        return rc;
    }

    // if hostname is localhost then resolution will give us
    // the interface loopback address. Detect this case and
    // return.
    if (!strncmp(hostname, "localhost", 9)) {
        wlog ("Detected localhost as system hostname."
              " Cannot resolve IP address");
       return rc;
    }

    // if it is infra then we need to determine the interface
    // host name. For management interface, the system hostname
    // is the intf hostname
    const char* infra_suffix = "-infra";
    size_t infra_suffix_len = sizeof(infra_suffix);
    char iface_hostname[MAX_HOST_NAME_SIZE+infra_suffix_len];
    memset(iface_hostname, 0, sizeof(iface_hostname));
    snprintf(iface_hostname, sizeof(iface_hostname),
             "%s%s", hostname,
             (((interface_type == INFRA_IFACE)) ? infra_suffix : ""));

    struct addrinfo *res = NULL;
    int ret = getaddrinfo(iface_hostname, NULL, NULL, &res);
    if(ret)
    {
        elog("IP address resolution failed for %s (err: %s)",
             iface_hostname, gai_strerror(ret));
        return rc;
    }

    struct addrinfo *if_address;
    void *src = NULL;
    for(if_address=res; ((if_address!=NULL)&&(rc!=PASS)); if_address=if_address->ai_next)
    {
        switch(if_address->ai_family)
        {
            case AF_INET:
                src = (void *) &((sockaddr_in*)if_address->ai_addr)->sin_addr;
                if (inet_ntop(AF_INET, src, address, len))
                    rc = PASS;

                break;
            case AF_INET6:
                src = (void *) &((sockaddr_in6*)if_address->ai_addr)->sin6_addr;
                // skip if this is a link-local address
                if (IN6_IS_ADDR_LINKLOCAL(src))
                    continue;

                if (inet_ntop(AF_INET6, src, address, INET6_ADDRSTRLEN))
                    rc = PASS;

                break;
        }
    }

    freeaddrinfo(res);
    return rc;
}



/**
 * Loop through all addresses on an interface until the first one is found that
 * is either AF_INET or AF_INET6.  This should get the address family of the
 * first address on the interface.  This is only intended to be used if there is
 * only one family of addresses on the interface; otherwise, the value of address
 * is undefined.  Ignores the default IPv6 link-local address.
 *
 * @param name of interface
 * @return IP version of interface
 */
int msgClassAddr::getVersionFromInterface(const char* interface)
{
    struct ifaddrs *if_address_list = NULL;
    struct ifaddrs *if_address;
    char ip_address[INET6_ADDRSTRLEN];

    if(getifaddrs(&if_address_list))
    {
        if(if_address_list)
        {
            freeifaddrs(if_address_list);
        }
        return FAIL;
    }

    for(if_address=if_address_list; if_address!=NULL; if_address=if_address->ifa_next)
    {
        if((strlen(interface)==strlen(if_address->ifa_name)) && (!strncmp(interface, if_address->ifa_name, strlen(if_address->ifa_name))) && if_address->ifa_addr)
        {
            switch(if_address->ifa_addr->sa_family)
            {
            case AF_INET:
                freeifaddrs(if_address_list);
                return AF_INET;
            case AF_INET6:
                inet_ntop(AF_INET6, &((sockaddr_in6*)if_address->ifa_addr)->sin6_addr, ip_address, INET6_ADDRSTRLEN);
                if(strncmp(ip_address,"fe80", 4))
                {
                    freeifaddrs(if_address_list);
                    return AF_INET6;
                }
            }
        }
    }
    if(if_address_list)
    {
        freeifaddrs(if_address_list);
    }
    return AF_UNSPEC;
}





/**
 * @return source msgClassAddr
 */
const msgClassAddr* msgClassSock::get_src_addr()
{
    return this->src_addr;
}


/**
 * @return source msgClassAddr
 */
const msgClassAddr* msgClassSock::get_dst_addr()
{
    return this->dst_addr;
}


/**
 * @return character array representation of source IP address
 */
const char* msgClassSock::get_src_str() const
{
    return this->src_addr->toNumericString();
}


/**
 * @return character array representation of destination IP address
 */
const char* msgClassSock::get_dst_str() const
{
    return this->dst_addr->toNumericString();
}


/**
 * Calls a specialized method to create a socket, based on the given
 * IP version and protocol.
 *
 * @param IP version to be used to created socket.  Either AF_INET or AF_INET6.
 * @param Protocol of socket. Supports IPPROTO_UDP and IPPROTO_RAW for now.
 * @return true if created successfully, false otherwise
 */
bool msgClassSock::createSocket(int ip_version, int proto)
{
    if(proto==IPPROTO_UDP)
    {
        switch(ip_version)
        {
        case AF_INET:
            return createSocketUDP4();
        case AF_INET6:
            return createSocketUDP6();
        default:
            elog("Failed to create UDP socket: address family %d is invalid", ip_version);
            errno = EAFNOSUPPORT;
            return false;
        };
    }
    else if(proto==IPPROTO_RAW)
    {
        switch(ip_version)
        {
        case AF_INET:
            return createSocketRaw4();
        case AF_INET6:
            return createSocketRaw6();
        default:
            elog("Failed to create Raw socket: address family %d is invalid", ip_version);
            errno = EAFNOSUPPORT;
            return false;
        };
    }
    else
    {
         elog("Failed to create socket: protocol %d not supported", proto);
         errno = EPFNOSUPPORT;
         return false;
    }
}

/* get the current socket status,
 * ok = true  if the socket is initialized and seemingly working or
 * ok = false if the socket has failed and needs reinitialization
 **/
bool  msgClassSock::sock_ok ( void )
{
    return (this->ok);
}

void  msgClassSock::sock_ok ( bool status )
{
    this->ok = status ;
}

/**
 * Creates an IPv4 UDP socket
 *
 * @return true if created successfully, false otherwise
 */
bool msgClassSock::createSocketUDP4()
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0)
    {
        elog("Failed to create IPv4 UDP socket");
        return false;
    }
    else
    {
        this->sock = sock;
        return true;
    }
}

/**
 * Creates an IPv4 Raw (ping) socket
 *
 * @return true if created successfully, false otherwise
 */
bool msgClassSock::createSocketRaw4()
{
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if(sock < 0)
    {
        elog("Failed to create IPv4 Raw socket");
        return false;
    }
    else
    {
        this->sock = sock;
        return true;
    }
}

/**
 * Creates an IPv6 UDP socket
 *
 * @return true if created successfully, false otherwise
 */
bool msgClassSock::createSocketUDP6()
{
    int sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0)
    {
        elog("Failed to create IPv6 UDP socket");
        return false;
    }
    else
    {
        this->sock = sock;
        return true;
    }
}

/**
 * Creates an IPv6 Raw (ping) socket
 *
 * @return true if created successfully, false otherwise
 */
bool msgClassSock::createSocketRaw6()
{
    int sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if(sock < 0)
    {
        elog("Failed to create IPv6 Raw socket");
        return false;
    }
    else
    {
        this->sock = sock;
        return true;
    }
}


/* destructor */
msgClassSock::~msgClassSock()
{
    delete this->src_addr;
    delete this->dst_addr;
    if(sock >= 0)
    {
        close(sock);
    }
    if(this->interface)
    {
        delete[] this->interface;
    }
}


/* default constructor */
msgClassSock::msgClassSock()
{
    this->dst_addr = NULL;
    this->interface= NULL;
    this->src_addr = NULL;
    this->sock = 0;
    this->return_status = RETRY;
    this->ok = false ;
}


/**
 * Reads data from socket into buffer.  Data is to be destined
 * to the instance's dst_addr, or any address if allow_any is true,
 * and once read, the src_addr flag will be set to the address that
 * the data came originated from.
 *
 * @param Buffer to read data to
 * @param maximum size of buffer
 * @return number of bytes read
 */
int msgClassSock::read(char* data, int len)
{
    socklen_t socklen = this->src_addr->getSockLen();
    return recvfrom(this->sock, data, len, 0, this->src_addr->getSockAddr(), &socklen);
}

/**
 * Reads a reply from the specified socket into buffer ignoring the address
 * of where it came from.
 *
 * @param Buffer to read data to
 * @param maximum size of buffer
 * @return number of bytes read
 */
int msgClassSock::readReply(char* data, int len)
{
    return recv(this->sock, data, len, 0 );
}

/**
 * If it is sending to the "correct" port and address, then it can use
 *  the existing sockaddr.  While that would ideally always be the case,
 *  in order to preserve the existing structure, there are some cases
 *  where the port and/or address are only specified when the data is
 *  being sent.  In these cases, a new sockaddr is allocated to be
 *  used for the call to sendto.  If src_addr is set, then the source
 *  address will be that address.
 *
 *  @param data to be sent
 *  @param size of data
 *  @param destination IP address.  If none is specified, uses instance's dst_addr
 *  @param destination port.  If none is specified, uses instance's stored port
 *  @return number of bytes sent
 */
int msgClassSock::write(const char* data, int len, const char* dst, int port)
{
    int ret = 0 ;
    sockaddr* dst_sock_addr = this->dst_addr->getSockAddr();

    sockaddr_in  dst_addr_in;
    sockaddr_in6  dst_addr_in6;

    if((port!=0) || (dst!=NULL))
    {
        ret = 1 ;
        switch(this->dst_addr->getIPVersion())
        {
        case AF_INET:
            dst_sock_addr = (sockaddr*) &dst_addr_in;
            memcpy(dst_sock_addr, this->dst_addr->getSockAddr(), sizeof(sockaddr_in));
            if(port!=0)
            {
                ((sockaddr_in*)dst_sock_addr)->sin_port = htons(port);
            }
            if(dst!=NULL)
            {
                ret = inet_pton(AF_INET, dst, &(((sockaddr_in*)dst_sock_addr)->sin_addr));
            }
            break;
        case AF_INET6:
            dst_sock_addr = (sockaddr*) &dst_addr_in6;
            memcpy(dst_sock_addr, this->dst_addr->getSockAddr(), sizeof(sockaddr_in6));
            if(port!=0)
            {
                ((sockaddr_in6*)dst_sock_addr)->sin6_port = htons(port);
            }
            if(dst!=NULL)
            {
                ret = inet_pton(AF_INET6, dst, &(((sockaddr_in6*)dst_sock_addr)->sin6_addr));
            }
            break;
        default:
            slog ("invalid AF network family (%d)\n", this->dst_addr->getIPVersion());
            return (-1);
        }
        if ( ret != 1 )
        {
            wlog ("write requires address resolution; inet_pton returned %d for ip '%s'\n", ret, dst );
            return (-1);
        }
    }

    ret = sendto(this->sock, data, len, 0, dst_sock_addr, this->dst_addr->getSockLen());
    if(ret<0)
    {
        elog("Failed to send with errno=%d", errno);
    }
    return ret;
}

/**
 * Given an Rx socket, send a message to the last address that a messaged has received from.
 *
 *  @param data to be sent
 *  @param size of data
 *  @param destination IP address.  If none is specified, uses instance's dst_addr
 *  @param destination port.  If none is specified, uses instance's stored port
 *  @return number of bytes sent
 */
int msgClassSock::reply(const msgClassSock* source, const char* data, int len)
{
    return write(data, len, source->get_src_str());
}


/*
 * This is to be used when code needs to know the actual file descriptor
 * of socket, for example to poll for received data.
 *
 * @return the file descriptor as an int
 */
int msgClassSock::getFD()
{
    return this->sock;
}


/**
 * Forces packets to be sent from interface of Tx socket
 *
 * @return PASS if successful, failure code otherwise
 */
int msgClassSock::interfaceBind()
{
    struct ifreq ifr;
    if(this->interface)
    {
        memset(&ifr, 0, sizeof(ifreq));
        snprintf(ifr.ifr_name, IFNAMSIZ, "%s", this->interface);
        if(setsockopt(this->sock, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr)))
        {
            elog("Failed to bind socket to interface (%d:%m)\n", errno);
            return FAIL_SOCKET_BIND;
        }
        return PASS;
    }
    return FAIL_BAD_CASE;
}


int msgClassSock::setPriortyMessaging( const char * iface )
{
    int flags = 0 ;
    int result = 0 ;

    switch( this->dst_addr->getIPVersion())
    {
    case AF_INET:

        ilog ("Setting %s with IPv4 priority messaging\n", iface );
        flags = IPTOS_CLASS_CS6;
        result = setsockopt( this->sock, IPPROTO_IP, IP_TOS,
                             &flags, sizeof(flags) );
        if( 0 > result )
        {
            elog ( "Failed to set socket send priority for interface (%s) (%d:%m)\n", iface, errno );
            return( FAIL_SOCKET_OPTION );
        }
        break ;

    case AF_INET6:

        ilog ("Setting %s with IPv6 priority messaging\n", iface );
        flags = IPTOS_CLASS_CS6;
        result = setsockopt( this->sock, IPPROTO_IPV6, IPV6_TCLASS, &flags, sizeof(flags) );
        if( 0 > result )
        {
            elog ( "Failed to set socket send priority for interface (%s) (%d:%m)\n", iface, errno );
            return( FAIL_SOCKET_OPTION );
        }
        break ;
    }

    flags = 6;
    result = setsockopt( this->sock, SOL_SOCKET, SO_PRIORITY, &flags, sizeof(flags) );
    if( 0 > result )
    {
        elog ( "Failed to set socket send priority for interface (%s) (%d:%m)\n", iface, errno );
        return( FAIL_SOCKET_OPTION );
    }
    return(PASS);
}


/* Set socket memory size */
int msgClassSock::setSocketMemory( const char * iface, const char * name, int size )
{
    /* don't use whats on the stack ; create a local var */
    int _size = size ;
    int rx_buff_memory = 0 ;
    int len = sizeof(rx_buff_memory);
    int result = setsockopt( this->sock, SOL_SOCKET, SO_RCVBUF, &_size, sizeof(_size) );
    if( 0 > result )
    {
        elog ( "failed to set socket memory size for '%s' (%d:%m)\n", iface, errno );
        return( FAIL_SOCKET_OPTION );
    }
    else
    {
        getsockopt(this->sock , SOL_SOCKET, SO_RCVBUF, &rx_buff_memory, (socklen_t*)&len);
    }
    /***********************************************************************************
     * From setsockopt SO_RCVBUF man page.
     *
     * Sets or gets the maximum socket receive buffer in bytes.  The
     * kernel doubles this value (to allow space for bookkeeping
     * overhead) when it is set using setsockopt(2), and this doubled
     * value is returned by getsockopt(2).  The default value is set
     * by the /proc/sys/net/core/rmem_default file, and the maximum
     * allowed value is set by the /proc/sys/net/core/rmem_max file.
     * The minimum (doubled) value for this option is 256.
     ************************************************************************************
     * Note: Value is divided by 2 because when you set the SO_RCVBUF the
     *       kernel doubles the value for book keeping.
     ************************************************************************************/
    ilog ("Setting %s %s to %d bytes\n", iface, name, (rx_buff_memory/2) );
    return(PASS);
}

/* Set the socket as non-blocking */
int msgClassSock::setSocketNonBlocking ( void )
{
    int on = 1;

    if ( 0 > ioctl(this->sock, FIONBIO, (char *)&on))
    {
       elog ("unable to set socket to non-blocking [%d:%m]\n", errno );
       return FAIL_SOCKET_NOBLOCK;
    }
    return PASS;
}

/**
 * Creates a socket to be used to receive data on a given address/port.
 * First, it creates the destination msgClassAddr, which is the address
 * that is is listening on, then it stores the interface if it is to be
 * used, and then calls the Rx socket initialization function, which
 * does the majority of the socket initialization.  The return_status is set to PASS if it succeeds, or the failure code elsewise.
 *
 * @param IP address or hostname
 * @param host-byte order port
 * @param IP protocol
 * @param name of interface to bind to, or null otherwise
 * @param Whether to listen on all addresses instead
 * @param Whether the address is multicast
 */
msgClassRx::msgClassRx(const char* address, int port, int proto, const char* interface, bool allow_any, bool is_multicast)
{
    this->dst_addr = new msgClassAddr(address, port, proto);
    this->src_addr = new msgClassAddr(address, port, proto);
    if(interface)
    {
        ilog ("Creating %s socket on port %d with address: %s\n", interface, port, address);
        this->interface = new char[strlen(interface)+1];
        snprintf(this->interface, strlen(interface)+1, "%s", interface);
    }
    else
    {
        ilog ("Creating localhost socket on port %d with address: %s\n", port, address);
        this->interface = NULL;
    }
    this->return_status = initSocket(allow_any, is_multicast);
}


/**
 * Creates a socket to be used to receive data on a given address/port.
 * First, it copies the destination msgClassAddr, which is the address
 * that is is listening on, then it stores the interface if it is to be
 * used, and then calls the Rx socket initialization function, which
 * does the majority of the socket initialization.  The return_status
 * is set to PASS if it succeeds, or the failure code elsewise.
 *
 * @param destination address
 * @param name of interface to bind to, or null otherwise
 * @param Whether to listen on all addresses instead
 * @param Whether the address is multicast
 */
msgClassRx::msgClassRx(const msgClassAddr& addr, const char* interface, bool allow_any, bool is_multicast)
{
    this->dst_addr = new msgClassAddr(addr);
    this->src_addr = new msgClassAddr(addr.toString());
    if(interface)
    {
        this->interface = new char[strlen(interface)+1];
        snprintf(this->interface, strlen(interface)+1, "%s", interface);
    }
    else
    {
        this->interface = NULL;
    }
    this->return_status = initSocket(allow_any, is_multicast);
}


/**
 * Creates socket, sets necessary socket options for Rx socket,
 * and binds to the correct address and interface if applicable
 *
 * @param whether to bind to all sockets of that family.
 * @param Whether the address is multicast
 * @return PASS if successful, failure code otherwise
 */
int msgClassRx::initSocket(bool allow_any, bool is_multicast)
{
    int on = 1;
    char address[INET6_ADDRSTRLEN];
    if(createSocket(this->dst_addr->getIPVersion(), this->dst_addr->getIPProtocol()) == false)
    {
        return FAIL_SOCKET_CREATE;
    }
    if(setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
    {
        return FAIL;
    }

    if(is_multicast)
    {
        switch(this->dst_addr->getIPVersion())
        {
        case AF_INET:
        {
            struct ip_mreqn mreq;
            memset(&mreq, 0, sizeof(mreq));
            mreq.imr_multiaddr.s_addr = ((sockaddr_in*)this->dst_addr->getSockAddr())->sin_addr.s_addr;
            mreq.imr_ifindex = if_nametoindex(this->interface);
            if(setsockopt(this->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))
            {
                elog("Failed to set multicast address (%d:%m)\n", errno);
                return FAIL_SOCKET_OPTION;
            }
            break;
        }
        case AF_INET6:
        {
            struct ipv6_mreq mreq;
            memset(&mreq, 0, sizeof(mreq));
            mreq.ipv6mr_multiaddr = ((sockaddr_in6*)this->dst_addr->getSockAddr())->sin6_addr;
            mreq.ipv6mr_interface = if_nametoindex(this->interface);
            if(setsockopt(this->sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)))
            {
                elog("Failed to set multicast address (%d:%m)\n", errno);
                return FAIL_SOCKET_OPTION;
            }
            break;
        }
        }
    }
    else if(allow_any)
    {
        switch(this->dst_addr->getIPVersion())
        {
        case AF_INET:
            ((sockaddr_in*)this->dst_addr->getSockAddr())->sin_addr.s_addr = htonl(INADDR_ANY);
            break;
        case AF_INET6:
            ((sockaddr_in6*)this->dst_addr->getSockAddr())->sin6_addr = in6addr_any;
            break;
        }
    }
    else if(this->interface)
    {
        msgClassAddr::getAddressFromInterface(this->interface, address, INET6_ADDRSTRLEN);
        switch(this->dst_addr->getIPVersion())
        {
        case AF_INET:
            inet_pton(AF_INET, address, &((sockaddr_in*)this->dst_addr->getSockAddr())->sin_addr);
            break;
        case AF_INET6:
            inet_pton(AF_INET6, address, &((sockaddr_in6*)this->dst_addr->getSockAddr())->sin6_addr);
            break;
        }
    }

    if(bind(this->sock, this->dst_addr->getSockAddr(), this->dst_addr->getSockLen()))
    {
        elog("Failed to bind socket to address (%d:%m)\n", errno);
        return FAIL_SOCKET_BIND;
    }

    return (this->setSocketNonBlocking ());
}




/**
 * Creates a socket to be used to transmit data to a given address/port.
 * First, it creates the destination msgClassAddr, then stores the interface
 * if it is to be used, and then calls the Tx socket initialization function,
 * which does the majority of the socket initialization.  The return_status is set to PASS if it succeeds, or the failure code elsewise.
 *
 * @param IP address or hostname
 * @param host-byte order port
 * @param IP protocol
 * @param name of interface to bind to, or null otherwise
 */
msgClassTx::msgClassTx(const char* address, int port, int proto, const char* interface)
{
    this->dst_addr = new msgClassAddr(address, port, proto);
    this->src_addr = new msgClassAddr(address, port, proto);
    if(interface)
    {
        ilog ("Creating %s socket on port %d with address: %s\n", interface, port, address);
        this->interface = new char[strlen(interface)+1];
        snprintf(this->interface, strlen(interface)+1, "%s", interface);
    }
    else
    {
        ilog ("Creating socket on port %d with address: %s\n", port, address);
        this->interface = NULL;
    }
    this->return_status = initSocket();
}


/**
 * Creates a socket to be used to transmit data to a given address/port.
 * First, it copies the destination msgClassAddr, then stores the interface
 * if it is to be used, and then calls the Tx socket initialization function,
 * which does the majority of the socket initialization.  The return_status is set to PASS if it succeeds, or the failure code elsewise.
 *
 * @param destination address
 * @param name of interface to bind to, or null otherwise
 */
msgClassTx::msgClassTx(const msgClassAddr& addr, const char* interface)
{
    this->dst_addr = new msgClassAddr(addr);
    this->src_addr = new msgClassAddr(addr);
    if(interface)
    {
        this->interface = new char[strlen(interface)+1];
        snprintf(this->interface, strlen(interface)+1, "%s", interface);
    }
    else
    {
        this->interface = NULL;
    }
    this->return_status = initSocket();
}


/**
 * Creates socket, sets necessary socket options for Tx socket,
 * and binds to the correct address and interface if applicable
 *
 * @return PASS if successful, failure code otherwise
 */
int msgClassTx::initSocket()
{
    int on = 1;
    char address[INET6_ADDRSTRLEN];
    if(createSocket(this->dst_addr->getIPVersion(), this->dst_addr->getIPProtocol()) == false)
    {
        return FAIL_SOCKET_CREATE;
    }
    if(setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
    {
        return FAIL;
    }
    if(this->interface)
    {
        msgClassAddr::getAddressFromInterface(this->interface, address, INET6_ADDRSTRLEN);
        dlog("Address of interface %s is %s", this->interface, address);
        delete this->src_addr;
        this->src_addr = new msgClassAddr(address);
        if(bind(this->sock, this->src_addr->getSockAddr(), this->src_addr->getSockLen()))
        {
            elog("Failed to bind socket to address (%d:%m)\n", errno);
            return FAIL_SOCKET_BIND;
        }
    }
    return PASS;
}
