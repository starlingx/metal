#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


import contextlib
import jsonpatch
import netaddr
import os
import pecan
import re
import socket
import sys
import traceback
import tsconfig.tsconfig as tsc
import wsme

from inventory.api.controllers.v1.sysinv import cgtsclient
from inventory.common import constants
from inventory.common import exception
from inventory.common.i18n import _
from inventory.common import k_host
from inventory.common.utils import memoized
from inventory import objects
from oslo_config import cfg
from oslo_log import log

CONF = cfg.CONF
LOG = log.getLogger(__name__)
KEY_VALUE_SEP = '='
JSONPATCH_EXCEPTIONS = (jsonpatch.JsonPatchException,
                        jsonpatch.JsonPointerException,
                        KeyError)


def ip_version_to_string(ip_version):
    return str(constants.IP_FAMILIES[ip_version])


def validate_limit(limit):
    if limit and limit < 0:
        raise wsme.exc.ClientSideError(_("Limit must be positive"))

    return min(CONF.api.limit_max, limit) or CONF.api.limit_max


def validate_sort_dir(sort_dir):
    if sort_dir not in ['asc', 'desc']:
        raise wsme.exc.ClientSideError(_("Invalid sort direction: %s. "
                                         "Acceptable values are "
                                         "'asc' or 'desc'") % sort_dir)
    return sort_dir


def validate_patch(patch):
    """Performs a basic validation on patch."""

    if not isinstance(patch, list):
        patch = [patch]

    for p in patch:
        path_pattern = re.compile("^/[a-zA-Z0-9-_]+(/[a-zA-Z0-9-_]+)*$")

        if not isinstance(p, dict) or \
                any(key for key in ["path", "op"] if key not in p):
            raise wsme.exc.ClientSideError(
                _("Invalid patch format: %s") % str(p))

        path = p["path"]
        op = p["op"]

        if op not in ["add", "replace", "remove"]:
            raise wsme.exc.ClientSideError(
                _("Operation not supported: %s") % op)

        if not path_pattern.match(path):
            raise wsme.exc.ClientSideError(_("Invalid path: %s") % path)

        if op == "add":
            if path.count('/') == 1:
                raise wsme.exc.ClientSideError(
                    _("Adding an additional attribute (%s) to the "
                      "resource is not allowed") % path)


def validate_mtu(mtu):
    """Check if MTU is valid"""
    if mtu < 576 or mtu > 9216:
        raise wsme.exc.ClientSideError(_(
            "MTU must be between 576 and 9216 bytes."))


def validate_address_within_address_pool(ip, pool):
    """Determine whether an IP address is within the specified IP address pool.
       :param ip netaddr.IPAddress object
       :param pool objects.AddressPool object
    """
    ipset = netaddr.IPSet()
    for start, end in pool.ranges:
        ipset.update(netaddr.IPRange(start, end))

    if netaddr.IPAddress(ip) not in ipset:
        raise wsme.exc.ClientSideError(_(
            "IP address %s is not within address pool ranges") % str(ip))


def validate_address_within_nework(ip, network):
    """Determine whether an IP address is within the specified IP network.
       :param ip netaddr.IPAddress object
       :param network objects.Network object
    """
    LOG.info("TODO(sc) validate_address_within_address_pool "
             "ip=%s, network=%s" % (ip, network))


class ValidTypes(wsme.types.UserType):
    """User type for validate that value has one of a few types."""

    def __init__(self, *types):
        self.types = types

    def validate(self, value):
        for t in self.types:
            if t is wsme.types.text and isinstance(value, wsme.types.bytes):
                value = value.decode()
            if isinstance(value, t):
                return value
        else:
            raise ValueError("Wrong type. Expected '%s', got '%s'" % (
                             self.types, type(value)))


def is_valid_hostname(hostname):
    """Determine whether an address is valid as per RFC 1123.
    """

    # Maximum length of 255
    rc = True
    length = len(hostname)
    if length > 255:
        raise wsme.exc.ClientSideError(_(
            "Hostname {} is too long.  Length {} is greater than 255."
            "Please configure valid hostname.").format(hostname, length))

    # Allow a single dot on the right hand side
    if hostname[-1] == ".":
        hostname = hostname[:-1]
    # Create a regex to ensure:
    # - hostname does not begin or end with a dash
    # - each segment is 1 to 63 characters long
    # - valid characters are A-Z (any case) and 0-9
    valid_re = re.compile("(?!-)[A-Z\d-]{1,63}(?<!-)$", re.IGNORECASE)
    rc = all(valid_re.match(x) for x in hostname.split("."))
    if not rc:
        raise wsme.exc.ClientSideError(_(
            "Hostname %s is invalid.  Hostname may not begin or end with"
            " a dash. Each segment is 1 to 63 chars long and valid"
            " characters are A-Z, a-z,  and 0-9."
            " Please configure valid hostname.") % (hostname))

    return rc


def is_host_active_controller(host):
    """Returns True if the supplied host is the active controller."""
    if host['personality'] == k_host.CONTROLLER:
        return host['hostname'] == socket.gethostname()
    return False


def is_host_simplex_controller(host):
    return host['personality'] == k_host.CONTROLLER and \
        os.path.isfile(tsc.PLATFORM_SIMPLEX_FLAG)


def is_aio_simplex_host_unlocked(host):
    return (get_system_mode() == constants.SYSTEM_MODE_SIMPLEX and
            host['administrative'] != k_host.ADMIN_LOCKED and
            host['invprovision'] != k_host.PROVISIONING)


def get_vswitch_type():
    system = pecan.request.dbapi.system_get_one()
    return system.capabilities.get('vswitch_type')


def get_https_enabled():
    system = pecan.request.dbapi.system_get_one()
    return system.capabilities.get('https_enabled', False)


def get_tpm_config():
    tpmconfig = None
    try:
        tpmconfig = pecan.request.dbapi.tpmconfig_get_one()
    except exception.InventoryException:
        pass
    return tpmconfig


def get_sdn_enabled():
    system = pecan.request.dbapi.system_get_one()
    return system.capabilities.get('sdn_enabled', False)


def get_region_config():
    system = pecan.request.dbapi.system_get_one()
    # TODO(mpeters): this should to be updated to return a boolean value
    # requires integration changes between horizon, cgts-client and users to
    # transition to a proper boolean value
    return system.capabilities.get('region_config', False)


def get_shared_services():
    system = pecan.request.dbapi.system_get_one()
    return system.capabilities.get('shared_services', None)


class SystemHelper(object):
    @staticmethod
    def get_product_build():
        active_controller = HostHelper.get_active_controller()
        if k_host.COMPUTE in active_controller.subfunctions:
            return constants.TIS_AIO_BUILD
        return constants.TIS_STD_BUILD


class HostHelper(object):
    @staticmethod
    @memoized
    def get_active_controller(dbapi=None):
        """Returns host object for active controller."""
        if not dbapi:
            dbapi = pecan.request.dbapi
        hosts = objects.Host.list(pecan.request.context,
                                  filters={'personality': k_host.CONTROLLER})
        active_controller = None
        for host in hosts:
            if is_host_active_controller(host):
                active_controller = host
                break

        return active_controller


def get_system_mode(dbapi=None):
    if not dbapi:
        dbapi = pecan.request.dbapi
    system = dbapi.system_get_one()
    return system.system_mode


def get_distributed_cloud_role(dbapi=None):
    if not dbapi:
        dbapi = pecan.request.dbapi
    system = dbapi.system_get_one()
    return system.distributed_cloud_role


def is_kubernetes_config(dbapi=None):
    if not dbapi:
        dbapi = pecan.request.dbapi
    system = dbapi.system_get_one()
    return system.capabilities.get('kubernetes_enabled', False)


def is_aio_duplex_system():
    return get_system_mode() == constants.SYSTEM_MODE_DUPLEX and \
        SystemHelper.get_product_build() == constants.TIS_AIO_BUILD


def get_compute_count(dbapi=None):
    if not dbapi:
        dbapi = pecan.request.dbapi
    return len(dbapi.host_get_by_personality(k_host.COMPUTE))


class SBApiHelper(object):
    """API Helper Class for manipulating Storage Backends.

       Common functionality needed by the storage_backend API and it's derived
       APIs: storage_ceph, storage_lvm, storage_file.
    """
    @staticmethod
    def validate_backend(storage_backend_dict):

        backend = storage_backend_dict.get('backend')
        if not backend:
            raise wsme.exc.ClientSideError("This operation requires a "
                                           "storage backend to be specified.")

        if backend not in constants.SB_SUPPORTED:
            raise wsme.exc.ClientSideError("Supplied storage backend (%s) is "
                                           "not supported." % backend)

        name = storage_backend_dict.get('name')
        if not name:
            # Get the list of backends of this type. If none are present, then
            # this is the system default backend for this type. Therefore use
            # the default name.
            backend_list = \
                pecan.request.dbapi.storage_backend_get_list_by_type(
                    backend_type=backend)
            if not backend_list:
                storage_backend_dict['name'] = constants.SB_DEFAULT_NAMES[
                    backend]
            else:
                raise wsme.exc.ClientSideError(
                    "This operation requires storage "
                    "backend name to be specified.")

    @staticmethod
    def common_checks(operation, storage_backend_dict):
        backend = SBApiHelper.validate_backend(storage_backend_dict)

        backend_type = storage_backend_dict['backend']
        backend_name = storage_backend_dict['name']

        try:
            existing_backend = pecan.request.dbapi.storage_backend_get_by_name(
                backend_name)
        except exception.StorageBackendNotFoundByName:
            existing_backend = None

        # The "shared_services" of an external backend can't have any internal
        # backend, vice versa. Note: This code needs to be revisited when
        # "non_shared_services" external backend (e.g. emc) is added into
        # storage-backend.
        if operation in [
                constants.SB_API_OP_CREATE, constants.SB_API_OP_MODIFY]:
            current_bk_svcs = []
            backends = pecan.request.dbapi.storage_backend_get_list()
            for bk in backends:
                if backend_type == constants.SB_TYPE_EXTERNAL:
                    if bk.as_dict()['backend'] != backend_type:
                        current_bk_svcs += \
                            SBApiHelper.getListFromServices(bk.as_dict())
                else:
                    if bk.as_dict()['backend'] == constants.SB_TYPE_EXTERNAL:
                        current_bk_svcs += \
                            SBApiHelper.getListFromServices(bk.as_dict())

            new_bk_svcs = SBApiHelper.getListFromServices(storage_backend_dict)
            for svc in new_bk_svcs:
                if svc in current_bk_svcs:
                    raise wsme.exc.ClientSideError("Service (%s) already has "
                                                   "a backend." % svc)

        # Deny any change while a backend is configuring
        backends = pecan.request.dbapi.storage_backend_get_list()
        for bk in backends:
            if bk['state'] == constants.SB_STATE_CONFIGURING:
                msg = _("%s backend is configuring, please wait for "
                        "current operation to complete before making "
                        "changes.") % bk['backend'].title()
                raise wsme.exc.ClientSideError(msg)

        if not existing_backend:
            existing_backends_by_type = set(bk['backend'] for bk in backends)

            if (backend_type in existing_backends_by_type and
                    backend_type not in [
                        constants.SB_TYPE_CEPH,
                        constants.SB_TYPE_CEPH_EXTERNAL]):
                msg = _("Only one %s backend is supported.") % backend_type
                raise wsme.exc.ClientSideError(msg)

            elif (backend_type != constants.SB_TYPE_CEPH_EXTERNAL and
                    backend_type not in existing_backends_by_type and
                    backend_name != constants.SB_DEFAULT_NAMES[backend_type]):
                msg = _("The primary {} backend must use the "
                        "default name: {}.").format(
                            backend_type,
                            constants.SB_DEFAULT_NAMES[backend_type])
                raise wsme.exc.ClientSideError(msg)

        # Deny operations with a single, unlocked, controller.
        # TODO(oponcea): Remove this once sm supports in-service config reload
        ctrls = objects.Host.list(pecan.request.context,
                                  filters={'personality': k_host.CONTROLLER})
        if len(ctrls) == 1:
            if ctrls[0].administrative == k_host.ADMIN_UNLOCKED:
                if get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
                    msg = _("Storage backend operations require controller "
                            "host to be locked.")
                else:
                    msg = _("Storage backend operations require "
                            "both controllers to be enabled and available.")
                raise wsme.exc.ClientSideError(msg)
        else:
            for ctrl in ctrls:
                if ctrl.availability not in [k_host.AVAILABILITY_AVAILABLE,
                                             k_host.AVAILABILITY_DEGRADED]:
                    msg = _("Storage backend operations require "
                            "both controllers "
                            "to be enabled and available/degraded.")
                    raise wsme.exc.ClientSideError(msg)

        if existing_backend and operation == constants.SB_API_OP_CREATE:
            if (existing_backend.state == constants.SB_STATE_CONFIGURED or
                    existing_backend.state == constants.SB_STATE_CONFIG_ERR):
                msg = (
                    _("Initial (%s) backend was previously created. Use the "
                      "modify API for further provisioning or supply a unique "
                      "name to add an additional backend.") %
                    existing_backend.name)
                raise wsme.exc.ClientSideError(msg)
        elif not existing_backend and operation == constants.SB_API_OP_MODIFY:
            raise wsme.exc.ClientSideError(
                "Attempting to modify non-existant (%s) backend." % backend)

    @staticmethod
    def set_backend_data(requested, defaults, checks, supported_svcs,
                         current=None):
        """Returns a valid backend dictionary based on current inputs

        :param requested: data from the API
        :param defaults: values that should be set if missing or
                         not currently set
        :param checks: a set of valid data to be mapped into the
                       backend capabilities
        :param supported_svcs: services that are allowed to be used
                               with this backend
        :param current: the existing view of this data (typically from the DB)
        """
        if current:
            merged = current.copy()
        else:
            merged = requested.copy()

        # go through the requested values
        for key in requested:
            if key in merged and merged[key] != requested[key]:
                merged[key] = requested[key]

        # Set existing defaults
        for key in merged:
            if merged[key] is None and key in defaults:
                merged[key] = defaults[key]

        # Add the missing defaults
        for key in defaults:
            if key not in merged:
                merged[key] = defaults[key]

        # Pop the current set of data and make sure only supported parameters
        # are populated
        hiera_data = merged.pop('capabilities', {})
        merged['capabilities'] = {}

        merged_hiera_data = defaults.pop('capabilities', {})
        merged_hiera_data.update(hiera_data)

        for key in merged_hiera_data:
            if key in checks['backend']:
                merged['capabilities'][key] = merged_hiera_data[key]
                continue
            for svc in supported_svcs:
                if key in checks[svc]:
                    merged['capabilities'][key] = merged_hiera_data[key]

        return merged

    @staticmethod
    def check_minimal_number_of_controllers(min_number):
        chosts = pecan.request.dbapi.host_get_by_personality(
            k_host.CONTROLLER)

        if len(chosts) < min_number:
            raise wsme.exc.ClientSideError(
                "This operation requires %s controllers provisioned." %
                min_number)

        for chost in chosts:
            if chost.invprovision != k_host.PROVISIONED:
                raise wsme.exc.ClientSideError(
                    "This operation requires %s controllers provisioned." %
                    min_number)

    @staticmethod
    def getListFromServices(be_dict):
        return [] if be_dict['services'] is None \
            else be_dict['services'].split(',')

    @staticmethod
    def setServicesFromList(be_dict, svc_list):
        be_dict['services'] = ','.join(svc_list)

    @staticmethod
    def is_svc_enabled(sb_list, svc):
        for b in sb_list:
            if b.services:
                if svc in b.services:
                    return True
        return False

    @staticmethod
    def enable_backend(sb, backend_enable_function):
        """In-service enable storage backend """
        try:
            # Initiate manifest application
            LOG.info(_("Initializing configuration of storage %s backend.") %
                     sb.backend.title())
            backend_enable_function(pecan.request.context)
            LOG.info("Configuration of storage %s backend initialized, "
                     "continuing in background." % sb.backend.title())
        except exception.InventoryException:
            LOG.exception("Manifests failed!")
            # Set lvm backend to error so that it can be recreated
            values = {'state': constants.SB_STATE_CONFIG_ERR, 'task': None}
            pecan.request.dbapi.storage_backend_update(sb.uuid, values)
            msg = (_("%s configuration failed, check node status and retry. "
                     "If problem persists contact next level of support.") %
                   sb.backend.title())
            raise wsme.exc.ClientSideError(msg)

    @staticmethod
    def is_primary_ceph_tier(name_string):
        """Check if a tier name string is for the primary ceph tier. """
        if name_string == constants.SB_TIER_DEFAULT_NAMES[
                constants.SB_TYPE_CEPH]:
            return True
        return False

    @staticmethod
    def is_primary_ceph_backend(name_string):
        """Check if a backend name string is for the primary ceph backend. """
        if name_string == constants.SB_DEFAULT_NAMES[constants.SB_TYPE_CEPH]:
            return True
        return False


@contextlib.contextmanager
def save_and_reraise_exception():
    """Save current exception, run some code and then re-raise.

    In some cases the exception context can be cleared, resulting in None
    being attempted to be re-raised after an exception handler is run. This
    can happen when eventlet switches greenthreads or when running an
    exception handler, code raises and catches an exception. In both
    cases the exception context will be cleared.

    To work around this, we save the exception state, run handler code, and
    then re-raise the original exception. If another exception occurs, the
    saved exception is logged and the new exception is re-raised.
    """
    type_, value, tb = sys.exc_info()
    try:
        yield
    except Exception:
        LOG.error(_('Original exception being dropped: %s'),
                  traceback.format_exception(type_, value, tb))
        raise
    raise (type_, value, tb)


def _get_port(host_name, port_name):
    hosts = cgtsclient(pecan.request.context).ihost.list()
    for h in hosts:
        if h.hostname == host_name:
            ports = cgtsclient(pecan.request.context).port.list(h.uuid)
            for p in ports:
                if p.name == port_name:
                    return p
    return None
