#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
from inventory.common import base
from inventory.common import exception
from inventory.systemconfig import manager
from oslo_log import log
from oslo_utils import excutils

LOG = log.getLogger(__name__)


class SystemConfigPlugin(object):
    """Implementation of the Plugin."""

    def __init__(self, invoke_kwds):
        self.manager = manager.SystemConfigDriverManager(
            invoke_kwds=invoke_kwds)

    def system_get_one(self):
        try:
            system = self.manager.system_get_one()
        except exception.SystemConfigDriverError as e:
            LOG.exception(e)
            with excutils.save_and_reraise_exception():
                LOG.error("system_get failed")

        return system

    def network_get_by_type(self, network_type):
        try:
            network = self.manager.network_get_by_type(
                network_type=network_type)
        except exception.SystemConfigDriverError as e:
            LOG.exception(e)
            with excutils.save_and_reraise_exception():
                LOG.error("network_get_by_type failed")

        return network

    def address_get_by_name(self, name):
        try:
            address = self.manager.address_get_by_name(
                name=name)
        except exception.SystemConfigDriverError as e:
            LOG.exception(e)
            with excutils.save_and_reraise_exception():
                LOG.error("address_get_by_name failed")

        return address

    def host_configure_check(self, host_uuid):
        try:
            return self.manager.host_configure_check(
                host_uuid=host_uuid)
        except exception.SystemConfigDriverError as e:
            LOG.exception(e)
            with excutils.save_and_reraise_exception():
                LOG.error("host_configure_check failed")

    def host_configure(self, host_uuid, do_compute_apply=False):
        try:
            host = self.manager.host_configure(
                host_uuid=host_uuid,
                do_compute_apply=do_compute_apply)
        except exception.SystemConfigDriverError as e:
            LOG.exception(e)
            with excutils.save_and_reraise_exception():
                LOG.error("host_configure failed")

        return host

    def host_unconfigure(self, host_uuid):
        try:
            host = self.manager.host_unconfigure(
                host_uuid=host_uuid)
        except exception.SystemConfigDriverError as e:
            LOG.exception(e)
            with excutils.save_and_reraise_exception():
                LOG.error("host_unconfigure failed")

        return host


class System(base.APIResourceWrapper):
    """Wrapper for SystemConfig System"""

    _attrs = ['uuid', 'name', 'system_type', 'system_mode', 'description',
              'software_version', 'capabilities', 'region_name',
              'updated_at', 'created_at']

    def __init__(self, apiresource):
        super(System, self).__init__(apiresource)

    def get_short_software_version(self):
        if self.software_version:
            return self.software_version.split(" ")[0]
        return None


class Host(base.APIResourceWrapper):
    """Wrapper for Inventory Hosts"""

    _attrs = ['uuid', 'hostname', 'personality',
              'mgmt_mac', 'mgmt_ip', 'bm_ip',
              'subfunctions',
              'capabilities',
              'created_at', 'updated_at',
              ]

    # Removed 'id', 'requires_reboot'
    # Add this back to models, migrate_repo: peers

    def __init__(self, apiresource):
        super(Host, self).__init__(apiresource)
        self._personality = self.personality
        self._capabilities = self.capabilities


class Interface(base.APIResourceWrapper):
    """Wrapper for SystemConfig Interfaces"""

    _attrs = ['id', 'uuid', 'ifname', 'ifclass', 'iftype',
              'networktype', 'networks', 'vlan_id',
              'uses', 'used_by', 'ihost_uuid',
              'ipv4_mode', 'ipv6_mode', 'ipv4_pool', 'ipv6_pool',
              'sriov_numvfs',
              # VLAN and virtual interfaces
              'imac', 'imtu', 'providernetworks', 'providernetworksdict',
              # AE-only
              'aemode', 'txhashpolicy', 'schedpolicy',
              ]

    def __init__(self, apiresource):
        super(Interface, self).__init__(apiresource)
        if not self.ifname:
            self.ifname = '(' + str(self.uuid)[-8:] + ')'


class Network(base.APIResourceWrapper):
    """Wrapper for SystemConfig Networks"""
    _attrs = ['id', 'uuid', 'type', 'name', 'dynamic', 'pool_uuid']

    def __init__(self, apiresource):
        super(Network, self).__init__(apiresource)


class Address(base.APIResourceWrapper):
    """Wrapper for SystemConfig Addresses"""

    _attrs = ['uuid', 'name', 'interface_uuid',
              'address', 'prefix', 'enable_dad']

    def __init__(self, apiresource):
        super(Address, self).__init__(apiresource)


class AddressPool(base.APIResourceWrapper):
    """Wrapper for SystemConfig Address Pools"""

    _attrs = ['uuid', 'name', 'network', 'family', 'prefix', 'order', 'ranges']

    def __init__(self, apiresource):
        super(AddressPool, self).__init__(apiresource)


class Route(base.APIResourceWrapper):
    """Wrapper for SystemConfig Routers"""

    _attrs = ['uuid', 'interface_uuid', 'network',
              'prefix', 'gateway', 'metric']

    def __init__(self, apiresource):
        super(Route, self).__init__(apiresource)
