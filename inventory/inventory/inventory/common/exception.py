# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright 2010 United States Government as represented by the
# Administrator of the National Aeronautics and Space Administration.
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

"""Inventory base exception handling.
"""

import six
import webob.exc

from inventory.common.i18n import _
from inventory.conf import CONF
from oslo_log import log

LOG = log.getLogger(__name__)


class ProcessExecutionError(IOError):
    def __init__(self, stdout=None, stderr=None, exit_code=None, cmd=None,
                 description=None):
        self.exit_code = exit_code
        self.stderr = stderr
        self.stdout = stdout
        self.cmd = cmd
        self.description = description

        if description is None:
            description = _('Unexpected error while running command.')
        if exit_code is None:
            exit_code = '-'
        message = (_('%(description)s\nCommand: %(cmd)s\n'
                     'Exit code: %(exit_code)s\nStdout: %(stdout)r\n'
                     'Stderr: %(stderr)r') %
                   {'description': description, 'cmd': cmd,
                    'exit_code': exit_code, 'stdout': stdout,
                    'stderr': stderr})
        IOError.__init__(self, message)


def _cleanse_dict(original):
    """Strip all admin_password, new_pass, rescue_pass keys from a dict."""
    return dict((k, v) for k, v in original.iteritems() if "_pass" not in k)


class InventoryException(Exception):
    """Base Inventory Exception

    To correctly use this class, inherit from it and define
    a 'message' property. That message will get printf'd
    with the keyword arguments provided to the constructor.

    """
    message = _("An unknown exception occurred.")
    code = 500
    headers = {}
    safe = False

    def __init__(self, message=None, **kwargs):
        self.kwargs = kwargs

        if 'code' not in self.kwargs:
            try:
                self.kwargs['code'] = self.code
            except AttributeError:
                pass

        if not message:
            try:
                message = self.message % kwargs

            except Exception as e:
                # kwargs doesn't match a variable in the message
                # log the issue and the kwargs
                LOG.exception(_('Exception in string format operation'))
                for name, value in kwargs.iteritems():
                    LOG.error("%s: %s" % (name, value))

                if CONF.fatal_exception_format_errors:
                    raise e
                else:
                    # at least get the core message out if something happened
                    message = self.message

        super(InventoryException, self).__init__(message)

    def format_message(self):
        if self.__class__.__name__.endswith('_Remote'):
            return self.args[0]
        else:
            return six.text_type(self)


class NotAuthorized(InventoryException):
    message = _("Not authorized.")
    code = 403


class AdminRequired(NotAuthorized):
    message = _("User does not have admin privileges")


class PolicyNotAuthorized(NotAuthorized):
    message = _("Policy doesn't allow %(action)s to be performed.")


class OperationNotPermitted(NotAuthorized):
    message = _("Operation not permitted.")


class Invalid(InventoryException):
    message = _("Unacceptable parameters.")
    code = 400


class Conflict(InventoryException):
    message = _('Conflict.')
    code = 409


class InvalidCPUInfo(Invalid):
    message = _("Unacceptable CPU info") + ": %(reason)s"


class InvalidIpAddressError(Invalid):
    message = _("%(address)s is not a valid IP v4/6 address.")


class IpAddressOutOfRange(Invalid):
    message = _("%(address)s is not in the range: %(low)s to %(high)s")


class InfrastructureNetworkNotConfigured(Invalid):
    message = _("An infrastructure network has not been configured")


class InvalidDiskFormat(Invalid):
    message = _("Disk format %(disk_format)s is not acceptable")


class InvalidUUID(Invalid):
    message = _("Expected a uuid but received %(uuid)s.")


class InvalidIPAddress(Invalid):
    message = _("Expected an IPv4 or IPv6 address but received %(address)s.")


class InvalidIdentity(Invalid):
    message = _("Expected an uuid or int but received %(identity)s.")


class PatchError(Invalid):
    message = _("Couldn't apply patch '%(patch)s'. Reason: %(reason)s")


class InvalidMAC(Invalid):
    message = _("Expected a MAC address but received %(mac)s.")


class ManagedIPAddress(Invalid):
    message = _("The infrastructure IP address for this nodetype is "
                "specified by the system configuration and cannot be "
                "modified.")


class IncorrectPrefix(Invalid):
    message = _("A prefix length of %(length)s must be used for "
                "addresses on the infrastructure network, as is specified in "
                "the system configuration.")


class InterfaceNameAlreadyExists(Conflict):
    message = _("Interface with name %(name)s already exists.")


class InterfaceNetworkTypeNotSet(Conflict):
    message = _("The Interface must have a networktype configured to "
                "support addresses. (data or infra)")


class AddressInUseByRouteGateway(Conflict):
    message = _("Address %(address)s is in use by a route to "
                "%(network)s/%(prefix)s via %(gateway)s")


class DuplicateAddressDetectionNotSupportedOnIpv4(Conflict):
    message = _("Duplicate Address Detection (DAD) not supported on "
                "IPv4 Addresses")


class DuplicateAddressDetectionRequiredOnIpv6(Conflict):
    message = _("Duplicate Address Detection (DAD) required on "
                "IPv6 Addresses")


class RouteAlreadyExists(Conflict):
    message = _("Route %(network)s/%(prefix)s via %(gateway)s already "
                "exists on this host.")


class RouteMaxPathsForSubnet(Conflict):
    message = _("Maximum number of paths (%(count)s) already reached for "
                "%(network)s/%(prefix)s already reached.")


class RouteGatewayNotReachable(Conflict):
    message = _("Route gateway %(gateway)s is not reachable by any address "
                " on this interface")


class RouteGatewayCannotBeLocal(Conflict):
    message = _("Route gateway %(gateway)s cannot be another local interface")


class RoutesNotSupportedOnInterfaces(Conflict):
    message = _("Routes may not be configured against interfaces with network "
                "type '%(iftype)s'")


class DefaultRouteNotAllowedOnVRSInterface(Conflict):
    message = _("Default route not permitted on 'data-vrs' interfaces")


class CannotDeterminePrimaryNetworkType(Conflict):
    message = _("Cannot determine primary network type of interface "
                "%(iface)s from %(types)s")


class AlarmAlreadyExists(Conflict):
    message = _("An Alarm with UUID %(uuid)s already exists.")


class CPUAlreadyExists(Conflict):
    message = _("A CPU with cpu ID %(cpu)s already exists.")


class MACAlreadyExists(Conflict):
    message = _("A Port with MAC address %(mac)s already exists "
                "on host %(host)s.")


class PCIAddrAlreadyExists(Conflict):
    message = _("A Device with PCI address %(pciaddr)s "
                "for %(host)s already exists.")


class DiskAlreadyExists(Conflict):
    message = _("A Disk with UUID %(uuid)s already exists.")


class PortAlreadyExists(Conflict):
    message = _("A Port with UUID %(uuid)s already exists.")


class SystemAlreadyExists(Conflict):
    message = _("A System with UUID %(uuid)s already exists.")


class SensorAlreadyExists(Conflict):
    message = _("A Sensor with UUID %(uuid)s already exists.")


class SensorGroupAlreadyExists(Conflict):
    message = _("A SensorGroup with UUID %(uuid)s already exists.")


class TrapDestAlreadyExists(Conflict):
    message = _("A TrapDest with UUID %(uuid)s already exists.")


class UserAlreadyExists(Conflict):
    message = _("A User with UUID %(uuid)s already exists.")


class CommunityAlreadyExists(Conflict):
    message = _("A Community with UUID %(uuid)s already exists.")


class ServiceAlreadyExists(Conflict):
    message = _("A Service with UUID %(uuid)s already exists.")


class ServiceGroupAlreadyExists(Conflict):
    message = _("A ServiceGroup with UUID %(uuid)s already exists.")


class NodeAlreadyExists(Conflict):
    message = _("A Node with UUID %(uuid)s already exists.")


class MemoryAlreadyExists(Conflict):
    message = _("A Memeory with UUID %(uuid)s already exists.")


class LLDPAgentExists(Conflict):
    message = _("An LLDP agent with uuid %(uuid)s already exists.")


class LLDPNeighbourExists(Conflict):
    message = _("An LLDP neighbour with uuid %(uuid)s already exists.")


class LLDPTlvExists(Conflict):
    message = _("An LLDP TLV with type %(type) already exists.")


class LLDPDriverError(Conflict):
    message = _("An LLDP driver error has occurred. method=%(method)")


class SystemConfigDriverError(Conflict):
    message = _("A SystemConfig driver error has occurred. method=%(method)")


# Cannot be templated as the error syntax varies.
# msg needs to be constructed when raised.
class InvalidParameterValue(Invalid):
    message = _("%(err)s")


class ApiError(Exception):

    message = _("An unknown exception occurred.")

    code = webob.exc.HTTPInternalServerError

    def __init__(self, message=None, **kwargs):

        self.kwargs = kwargs

        if 'code' not in self.kwargs and hasattr(self, 'code'):
            self.kwargs['code'] = self.code

        if message:
            self.message = message

        try:
            super(ApiError, self).__init__(self.message % kwargs)
            self.message = self.message % kwargs
        except Exception:
            LOG.exception('Exception in string format operation, '
                          'kwargs: %s', kwargs)
            raise

    def __str__(self):
        return repr(self.value)

    def __unicode__(self):
        return self.message

    def format_message(self):
        if self.__class__.__name__.endswith('_Remote'):
            return self.args[0]
        else:
            return six.text_type(self)


class NotFound(InventoryException):
    message = _("Resource could not be found.")
    code = 404


class MultipleResults(InventoryException):
    message = _("More than one result found.")


class SystemNotFound(NotFound):
    message = _("No System %(system)s found.")


class CPUNotFound(NotFound):
    message = _("No CPU %(cpu)s found.")


class NTPNotFound(NotFound):
    message = _("No NTP with id %(uuid)s found.")


class PTPNotFound(NotFound):
    message = _("No PTP with id %(uuid)s found.")


class DiskNotFound(NotFound):
    message = _("No disk with id %(disk_id)s")


class DiskPartitionNotFound(NotFound):
    message = _("No disk partition with id %(partition_id)s")


class PartitionAlreadyExists(Conflict):
    message = _("Disk partition %(device_path)s already exists.")


class LvmLvgNotFound(NotFound):
    message = _("No LVM Local Volume Group with id %(lvg_id)s")


class LvmPvNotFound(NotFound):
    message = _("No LVM Physical Volume with id %(pv_id)s")


class DriverNotFound(NotFound):
    message = _("Failed to load driver %(driver_name)s.")


class PCIDeviceNotFound(NotFound):
    message = _("Failed to load pci device %(pcidevice_id)s.")


class ImageNotFound(NotFound):
    message = _("Image %(image_id)s could not be found.")


class HostNotFound(NotFound):
    message = _("Host %(host)s could not be found.")


class HostAlreadyExists(Conflict):
    message = _("Host %(uuid)s already exists.")


class ClonedInterfaceNotFound(NotFound):
    message = _("Cloned Interface %(intf)s could not be found.")


class StaticAddressNotConfigured(Invalid):
    message = _("The IP address for this interface is assigned "
                "dynamically as specified during system configuration.")


class HostLocked(InventoryException):
    message = _("Unable to complete the action %(action)s because "
                "Host %(host)s is in administrative state = unlocked.")


class HostMustBeLocked(InventoryException):
    message = _("Unable to complete the action because "
                "Host %(host)s is in administrative state = unlocked.")


class ConsoleNotFound(NotFound):
    message = _("Console %(console_id)s could not be found.")


class FileNotFound(NotFound):
    message = _("File %(file_path)s could not be found.")


class NoValidHost(NotFound):
    message = _("No valid host was found. %(reason)s")


class NodeNotFound(NotFound):
    message = _("Node %(node)s could not be found.")


class MemoryNotFound(NotFound):
    message = _("Memory %(memory)s could not be found.")


class PortNotFound(NotFound):
    message = _("Port %(port)s could not be found.")


class SensorNotFound(NotFound):
    message = _("Sensor %(sensor)s could not be found.")


class ServerNotFound(NotFound):
    message = _("Server %(server)s could not be found.")


class ServiceNotFound(NotFound):
    message = _("Service %(service)s could not be found.")


class AlarmNotFound(NotFound):
    message = _("Alarm %(alarm)s could not be found.")


class EventLogNotFound(NotFound):
    message = _("Event Log %(eventLog)s could not be found.")


class ExclusiveLockRequired(NotAuthorized):
    message = _("An exclusive lock is required, "
                "but the current context has a shared lock.")


class SSHConnectFailed(InventoryException):
    message = _("Failed to establish SSH connection to host %(host)s.")


class UnsupportedObjectError(InventoryException):
    message = _('Unsupported object type %(objtype)s')


class OrphanedObjectError(InventoryException):
    message = _('Cannot call %(method)s on orphaned %(objtype)s object')


class IncompatibleObjectVersion(InventoryException):
    message = _('Version %(objver)s of %(objname)s is not supported')


class GlanceConnectionFailed(InventoryException):
    message = "Connection to glance host %(host)s:%(port)s failed: %(reason)s"


class ImageNotAuthorized(InventoryException):
    message = "Not authorized for image %(image_id)s."


class LoadNotFound(NotFound):
    message = _("Load %(load)s could not be found.")


class LldpAgentNotFound(NotFound):
    message = _("LLDP agent %(agent)s could not be found")


class LldpAgentNotFoundForPort(NotFound):
    message = _("LLDP agent for port %(port)s could not be found")


class LldpNeighbourNotFound(NotFound):
    message = _("LLDP neighbour %(neighbour)s could not be found")


class LldpNeighbourNotFoundForMsap(NotFound):
    message = _("LLDP neighbour could not be found for msap %(msap)s")


class LldpTlvNotFound(NotFound):
    message = _("LLDP TLV %(type)s could not be found")


class InvalidImageRef(InventoryException):
    message = "Invalid image href %(image_href)s."
    code = 400


class ServiceUnavailable(InventoryException):
    message = "Connection failed"


class Forbidden(InventoryException):
    message = "Requested OpenStack Images API is forbidden"


class BadRequest(InventoryException):
    pass


class HTTPException(InventoryException):
    message = "Requested version of OpenStack Images API is not available."


class InventorySignalTimeout(InventoryException):
    message = "Inventory Timeout."


class InvalidEndpoint(InventoryException):
    message = "The provided endpoint is invalid"


class CommunicationError(InventoryException):
    message = "Unable to communicate with the server."


class HTTPForbidden(Forbidden):
    pass


class Unauthorized(InventoryException):
    pass


class HTTPNotFound(NotFound):
    pass


class ConfigNotFound(InventoryException):
    pass


class ConfigInvalid(InventoryException):
    message = _("Invalid configuration file. %(error_msg)s")


class NotSupported(InventoryException):
    message = "Action %(action)s is not supported."


class PeerAlreadyExists(Conflict):
    message = _("Peer %(uuid)s already exists")


class PeerAlreadyContainsThisHost(Conflict):
    message = _("Host %(host)s is already present in peer group %(peer_name)s")


class PeerNotFound(NotFound):
    message = _("Peer %(peer_uuid)s not found")


class PeerContainsDuplicates(Conflict):
    message = _("Peer with name % already exists")


class StoragePeerGroupUnexpected(InventoryException):
    message = _("Host %(host)s cannot be assigned to group %(peer_name)s. "
                "group-0 is reserved for storage-0 and storage-1")


class StorageBackendNotFoundByName(NotFound):
    message = _("StorageBackend %(name)s not found")


class PickleableException(Exception):
    """
    Pickleable Exception
      Used to mark custom exception classes that can be pickled.
    """
    pass


class OpenStackException(PickleableException):
    """
    OpenStack Exception
    """
    def __init__(self, message, reason):
        """
        Create an OpenStack exception
        """
        super(OpenStackException, self).__init__(message, reason)
        self._reason = reason  # a message string or another exception
        self._message = message

    def __str__(self):
        """
        Return a string representing the exception
        """
        return "[OpenStack Exception:reason=%s]" % self._reason

    def __repr__(self):
        """
        Provide a representation of the exception
        """
        return str(self)

    def __reduce__(self):
        """
        Return a tuple so that we can properly pickle the exception
        """
        return OpenStackException, (self.message, self._reason)

    @property
    def message(self):
        """
        Returns the message for the exception
        """
        return self._message

    @property
    def reason(self):
        """
        Returns the reason for the exception
        """
        return self._reason


class OpenStackRestAPIException(PickleableException):
    """
    OpenStack Rest-API Exception
    """
    def __init__(self, message, http_status_code, reason):
        """
        Create an OpenStack Rest-API exception
        """
        super(OpenStackRestAPIException, self).__init__(message)
        self._http_status_code = http_status_code  # as defined in RFC 2616
        self._reason = reason  # a message string or another exception

    def __str__(self):
        """
        Return a string representing the exception
        """
        return ("[OpenStack Rest-API Exception: code=%s, reason=%s]"
                % (self._http_status_code, self._reason))

    def __repr__(self):
        """
        Provide a representation of the exception
        """
        return str(self)

    def __reduce__(self):
        """
        Return a tuple so that we can properly pickle the exception
        """
        return OpenStackRestAPIException, (self.message,
                                           self._http_status_code,
                                           self._reason)

    @property
    def http_status_code(self):
        """
        Returns the HTTP status code
        """
        return self._http_status_code

    @property
    def reason(self):
        """
        Returns the reason for the exception
        """
        return self._reason
