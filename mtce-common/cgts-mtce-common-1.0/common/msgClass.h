#ifndef __INCLUDE_MSGCLASS_H__
#define __INCLUDE_MSGCLASS_H__

/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/ip.h>
#include "nodeBase.h"
#include "returnCodes.h"
#include "nodeUtil.h"


/*
 * The msgClassAddr class is a version-independent representation
 *  of an IP address with TLP and port specified.  Note that only UDP sockets
 *  are supported at this point.
 */
class msgClassAddr
{
public:
    msgClassAddr(const char* address, int port, int proto);
    msgClassAddr(const msgClassAddr& addr);
    msgClassAddr(const char* address);
    ~msgClassAddr();
    int getAddress(char* address, int addr_len, int* port, int* proto) const;
    int setAddress(char* address, int port, int proto);
    const char* toString() const;
    const char* toNumericString() const;
    int getIPVersion() const;
    int getIPProtocol() const;
    int getPort() const;
    const struct sockaddr* getSockAddr() const;
    struct sockaddr* getSockAddr();
    socklen_t getSockLen() const;
    msgClassAddr& operator= ( const msgClassAddr &rhs )
    {
        this->return_status = initAddr(rhs.address_str, rhs.port, rhs.proto);
        return *this;
    }
    /**
     * Is to be set to allow the status of the last operation to be checked.
     * Useful in checking if an instance was initialized correctly.
     */
    int return_status;

    /**
     * Intended to mark the address as being all, equivalent to either
     * "0.0.0.0" or "::" depending on the address family.  Is to allow
     * a normal address to be passed in to determine the IP family, but
     * uses that value instead of the passed in value.
     **/
    bool addr_any;
    static int getAddressFromInterface(const char* interface, char* address, int len);
    static int getVersionFromInterface(const char* interface);

protected:
    /**
     * Stores the protocol of the address.
     */
    int proto;

    /**
     * Stores the port in host-byte order.
     */
    int port;

    /**
     * Stores the address string used to create this msgClassAddr instance.
     * If it is resolved to an IP address, this can be used to access the
     * original unresolved value.
     */
    char* address_str;

    /**
     * Stores a numeric representation of the IP address of the address.
     * This differs from address_str if address_str was a hostname that
     * was resolved.
     */
    char* address_numeric_string;

    /**
     * Stores the addrinfo struct created by getaddrinfo.  This contains
     * both the IP version and the sockaddr.
     */
    struct addrinfo* address_info;

private:
    int initAddr(const char* address, int port, int proto);

};

/*
 * The msgClassSock class is an abstraction of the inet sockets used
 *  by maintenance,which are not dependent on the socket protocol. This is needed
 *  to allow both IPv4 and IPv6 addresses to be used on the management network.
 */
class msgClassSock
{
public:
    ~msgClassSock();
    int read(char* data, int len);
    int write(const char* data, int len, const char* dest=NULL, int port=0);
    int reply(const msgClassSock* source, const char* data, int len);
    int readReply(char* data, int len);
    int getFD();
    int interfaceBind();
    int setPriortyMessaging( const char * iface );
    int setSocketMemory    ( const char * iface, const char * name, int rmem );
    int setSocketNonBlocking ( void );

    const msgClassAddr* get_src_addr();
    const msgClassAddr* get_dst_addr();
    const char* get_src_str() const;
    const char* get_dst_str() const;

    /* get the current socket status,
     * ok = true  if the socket is initialized and seemingly working or
     * ok = false if the socket has failed and needs reinitialization
     **/
    bool  sock_ok ( void );
    void  sock_ok ( bool status );

    /**
     * Is to be set to allow the status of the last operation to be checked.
     * Useful in checking if an instance was initialized correctly.
     */
    int return_status;
    const char* toString();

protected:
    msgClassSock();

    /**
     * Stores the file descriptor of the allocated socket.  Should
     * only be accessed via the getFD() accessor.
     */
    int sock;

    bool createSocket(int ip_version, int proto);

    /**
     * Source address of outgoing packets.  May be specified for Tx sockets,
     * and is set by Rx sockets when receiving packets.
     */
    msgClassAddr* src_addr;

    /**
     * Address that this socket is to be either transmitting to or receiving
     * on, depending if it is Rx or Tx.
     */
    msgClassAddr* dst_addr;

    /**
     * If the socket is to be bound to a specific interface, that the
     * interface name is stores here; otherwise, this is NULL.
     */
    char* interface;

    /**
     * Set true when interface is initialized and working
     * or false if it is not initialized or failed.
     * This boolean can be used in a main loop to decide
     * if a socket needs to be re initialized or not.
     */
    bool  ok ;

private:
    bool createSocketUDP4();
    bool createSocketUDP6();
    bool createSocketRaw4();
    bool createSocketRaw6();
};

/*
 * A socket for receiving data, this replaces the Rx sockets
 * that are tied to inet. The dst_addr is set to the address to
 * be receiving packets on when reading.
 */
class msgClassRx : public msgClassSock
{
public:
    msgClassRx(const char* address, int port, int proto, const char* interface=NULL, bool allow_any=false, bool is_multicast=false);
    msgClassRx(const msgClassAddr& addr, const char* interface=NULL, bool allow_any=false, bool is_multicast=false);
private:
    int initSocket(bool allow_any, bool is_multicast=false);
};

/*
 * A socket for receiving data, this replaces the Tx sockets
 * that are tied to inet. The dst_addr is set to the address to
 * be sending packets to when writing.
 */
class msgClassTx : public msgClassSock
{
public:
    msgClassTx(const char* address, int port, int proto, const char* interface=NULL);
    msgClassTx(const msgClassAddr& addr, const char* interface=NULL);
private:
    int initSocket();
};

#endif /* __INCLUDE_MSGCLASS_H__ */
