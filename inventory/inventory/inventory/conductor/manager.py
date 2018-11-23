# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8
# Copyright 2013 Hewlett-Packard Development Company, L.P.
# Copyright 2013 International Business Machines Corporation
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

"""Conduct all activity related Inventory.

A single instance of :py:class:`inventory.conductor.manager.ConductorManager`
is created within the inventory-conductor process, and is responsible for
performing actions for hosts managed by inventory.

Commands are received via RPC calls.
"""

import grp
import keyring
import os
import oslo_messaging as messaging
import pwd
import socket
import subprocess
import tsconfig.tsconfig as tsc

from fm_api import constants as fm_constants
from fm_api import fm_api
from futurist import periodics
from inventory.agent import rpcapi as agent_rpcapi
from inventory.api.controllers.v1 import cpu_utils
from inventory.api.controllers.v1 import utils
from inventory.common import constants
from inventory.common import exception
from inventory.common import fm
from inventory.common.i18n import _
from inventory.common import k_host
from inventory.common import k_lldp
from inventory.common import mtce_api
from inventory.common import rpc as inventory_oslo_rpc
from inventory.common import utils as cutils
from inventory.conductor import base_manager
from inventory.conductor import openstack
from inventory.db import api as dbapi
from inventory import objects
from inventory.systemconfig import plugin as systemconfig_plugin
from netaddr import IPAddress
from netaddr import IPNetwork
from oslo_config import cfg
from oslo_log import log

MANAGER_TOPIC = 'inventory.conductor_manager'

LOG = log.getLogger(__name__)

conductor_opts = [
    cfg.StrOpt('api_url',
               default=None,
               help=('Url of Inventory API service. If not set Inventory can '
                     'get current value from Keystone service catalog.')),
    cfg.IntOpt('audit_interval',
               default=60,
               help='Interval to run conductor audit'),
]

CONF = cfg.CONF
CONF.register_opts(conductor_opts, 'conductor')
MTC_ADDRESS = 'localhost'
MTC_PORT = 2112


class ConductorManager(base_manager.BaseConductorManager):
    """Inventory Conductor service main class."""

    # Must be in sync with rpcapi.ConductorAPI's
    RPC_API_VERSION = '1.0'
    my_host_id = None

    target = messaging.Target(version=RPC_API_VERSION)

    def __init__(self, host, topic):
        super(ConductorManager, self).__init__(host, topic)
        self.dbapi = None
        self.fm_api = None
        self.fm_log = None
        self.sc_op = None

        self._openstack = None
        self._api_token = None
        self._mtc_address = MTC_ADDRESS
        self._mtc_port = MTC_PORT

    def start(self):
        self._start()
        LOG.info("Start inventory-conductor")

    def init_host(self, admin_context=None):
        super(ConductorManager, self).init_host(admin_context)
        self._start(admin_context)

    def del_host(self, deregister=True):
        return

    def _start(self, context=None):
        self.dbapi = dbapi.get_instance()
        self.fm_api = fm_api.FaultAPIs()
        self.fm_log = fm.FmCustomerLog()
        self.sc_op = systemconfig_plugin.SystemConfigPlugin(
            invoke_kwds={'context': context})
        self._openstack = openstack.OpenStackOperator(self.dbapi)

        # create /var/run/inventory if required. On DOR, the manifests
        # may not run to create this volatile directory.
        self._create_volatile_dir()

        system = self._populate_default_system(context)

        inventory_oslo_rpc.init(cfg.CONF)
        LOG.info("inventory-conductor start system=%s" % system.as_dict())

    def periodic_tasks(self, context, raise_on_error=False):
        """Periodic tasks are run at pre-specified intervals. """
        return self.run_periodic_tasks(context, raise_on_error=raise_on_error)

    @periodics.periodic(spacing=CONF.conductor.audit_interval)
    def _conductor_audit(self, context):
        # periodically, perform audit of inventory
        LOG.info("Inventory Conductor running periodic audit task.")

        system = self._populate_default_system(context)
        LOG.info("Inventory Conductor from systemconfig system=%s" %
                 system.as_dict())

        hosts = objects.Host.list(context)

        for host in hosts:
            self._audit_install_states(host)

            if not host.personality:
                continue
            # audit of configured hosts
            self._audit_host_action(host)

        LOG.debug("Inventory Conductor audited hosts=%s" % hosts)

    @staticmethod
    def _create_volatile_dir():
        """Create the volatile directory required for inventory service"""
        if not os.path.isdir(constants.INVENTORY_LOCK_PATH):
            try:
                uid = pwd.getpwnam(constants.INVENTORY_USERNAME).pw_uid
                gid = grp.getgrnam(constants.INVENTORY_GRPNAME).gr_gid
                os.makedirs(constants.INVENTORY_LOCK_PATH)
                os.chown(constants.INVENTORY_LOCK_PATH, uid, gid)
                LOG.info("Created directory=%s" %
                         constants.INVENTORY_LOCK_PATH)
            except OSError as e:
                LOG.exception("makedir %s OSError=%s encountered" %
                              (constants.INVENTORY_LOCK_PATH, e))
                pass

    def _populate_default_system(self, context):
        """Populate the default system tables"""

        try:
            system = self.dbapi.system_get_one()
            # TODO(sc) return system  # system already configured
        except exception.NotFound:
            pass  # create default system

        # Get the system from systemconfig
        system = self.sc_op.system_get_one()
        LOG.info("system retrieved from systemconfig=%s" % system.as_dict())

        if not system:
            # The audit will need to populate system
            return

        values = {
            'uuid': system.uuid,
            'name': system.name,
            'system_mode': system.system_mode,
            'region_name': system.region_name,
            'software_version': cutils.get_sw_version(),
            'capabilities': {}}

        try:
            system = self.dbapi.system_create(values)
        except exception.SystemAlreadyExists:
            system = self.dbapi.system_update(system.uuid, values)

        return system

    def _using_static_ip(self, ihost, personality=None, hostname=None):
        using_static = False
        if ihost:
            ipersonality = ihost['personality']
            ihostname = ihost['hostname'] or ""
        else:
            ipersonality = personality
            ihostname = hostname or ""

        if ipersonality and ipersonality == k_host.CONTROLLER:
            using_static = True
        elif ipersonality and ipersonality == k_host.STORAGE:
            # only storage-0 and storage-1 have static (later storage-2)
            if (ihostname[:len(k_host.STORAGE_0_HOSTNAME)] in
                    [k_host.STORAGE_0_HOSTNAME,
                     k_host.STORAGE_1_HOSTNAME]):
                using_static = True

        return using_static

    def handle_dhcp_lease(self, context, tags, mac, ip_address, cid=None):
        """Synchronously, have a conductor handle a DHCP lease update.

        Handling depends on the interface:
        - management interface: do nothing
        - infrastructure interface: do nothing
        - pxeboot interface: create i_host

        :param cid:
        :param context: request context.
        :param tags: specifies the interface type (mgmt or infra)
        :param mac: MAC for the lease
        :param ip_address: IP address for the lease
        """

        LOG.info("receiving dhcp_lease: %s %s %s %s %s" %
                 (context, tags, mac, ip_address, cid))
        # Get the first field from the tags
        first_tag = tags.split()[0]

        if 'pxeboot' == first_tag:
            mgmt_network = \
                self.sc_op.network_get_by_type(
                    constants.NETWORK_TYPE_MGMT)
            if not mgmt_network.dynamic:
                return

            # This is a DHCP lease for a node on the pxeboot network
            # Create the ihost (if necessary).
            ihost_dict = {'mgmt_mac': mac}
            self.create_host(context, ihost_dict, reason='dhcp pxeboot')

    def handle_dhcp_lease_from_clone(self, context, mac):
        """Handle dhcp request from a cloned controller-1.
           If MAC address in DB is still set to well known
           clone label, then this is the first boot of the
           other controller. Real MAC address from PXE request
           is updated in the DB.
        """
        controller_hosts = \
            self.dbapi.host_get_by_personality(k_host.CONTROLLER)
        for host in controller_hosts:
            if (constants.CLONE_ISO_MAC in host.mgmt_mac and
                    host.personality == k_host.CONTROLLER and
                    host.administrative == k_host.ADMIN_LOCKED):
                LOG.info("create_host (clone): Host found: {}:{}:{}->{}"
                         .format(host.hostname, host.personality,
                                 host.mgmt_mac, mac))
                values = {'mgmt_mac': mac}
                self.dbapi.host_update(host.uuid, values)
                host.mgmt_mac = mac
                self._configure_controller_host(context, host)
                if host.personality and host.hostname:
                    ihost_mtc = host.as_dict()
                    ihost_mtc['operation'] = 'modify'
                    ihost_mtc = cutils.removekeys_nonmtce(ihost_mtc)
                    mtce_api.host_modify(
                        self._api_token, self._mtc_address,
                        self._mtc_port, ihost_mtc,
                        constants.MTC_DEFAULT_TIMEOUT_IN_SECS)
                return host
        return None

    def create_host(self, context, values, reason=None):
        """Create an ihost with the supplied data.

        This method allows an ihost to be created.

        :param reason:
        :param context: an admin context
        :param values: initial values for new ihost object
        :returns: updated ihost object, including all fields.
        """

        if 'mgmt_mac' not in values:
            raise exception.InventoryException(_(
                "Invalid method call: create_host requires mgmt_mac."))

        try:
            mgmt_update_required = False
            mac = values['mgmt_mac']
            mac = mac.rstrip()
            mac = cutils.validate_and_normalize_mac(mac)
            ihost = self.dbapi.host_get_by_mgmt_mac(mac)
            LOG.info("Not creating ihost for mac: %s because it "
                     "already exists with uuid: %s" % (values['mgmt_mac'],
                                                       ihost['uuid']))
            mgmt_ip = values.get('mgmt_ip') or ""

            if mgmt_ip and not ihost.mgmt_ip:
                LOG.info("%s create_host setting mgmt_ip to %s" %
                         (ihost.uuid, mgmt_ip))
                mgmt_update_required = True
            elif mgmt_ip and ihost.mgmt_ip and \
                    (ihost.mgmt_ip.strip() != mgmt_ip.strip()):
                # Changing the management IP on an already configured
                # host should not occur nor be allowed.
                LOG.error("DANGER %s create_host mgmt_ip dnsmasq change "
                          "detected from %s to %s." %
                          (ihost.uuid, ihost.mgmt_ip, mgmt_ip))

            if mgmt_update_required:
                ihost = self.dbapi.host_update(ihost.uuid, values)

                if ihost.personality and ihost.hostname:
                    ihost_mtc = ihost.as_dict()
                    ihost_mtc['operation'] = 'modify'
                    ihost_mtc = cutils.removekeys_nonmtce(ihost_mtc)
                    LOG.info("%s create_host update mtce %s " %
                             (ihost.hostname, ihost_mtc))
                    mtce_api.host_modify(
                        self._api_token, self._mtc_address, self._mtc_port,
                        ihost_mtc,
                        constants.MTC_DEFAULT_TIMEOUT_IN_SECS)

            return ihost
        except exception.HostNotFound:
            # If host is not found, check if this is cloning scenario.
            # If yes, update management MAC in the DB and create PXE config.
            clone_host = self.handle_dhcp_lease_from_clone(context, mac)
            if clone_host:
                return clone_host

        # assign default system
        system = self.dbapi.system_get_one()
        values.update({'system_id': system.id})
        values.update({k_host.HOST_ACTION_STATE:
                       k_host.HAS_REINSTALLING})

        # get tboot value from the active controller
        active_controller = None
        hosts = self.dbapi.host_get_by_personality(k_host.CONTROLLER)
        for h in hosts:
            if utils.is_host_active_controller(h):
                active_controller = h
                break

        if active_controller is not None:
            tboot_value = active_controller.get('tboot')
            if tboot_value is not None:
                values.update({'tboot': tboot_value})

        host = objects.Host(context, **values).create()

        # A host is being created, generate discovery log.
        self._log_host_create(host, reason)

        ihost_id = host.get('uuid')
        LOG.info("RPC create_host called and created ihost %s." % ihost_id)

        return host

    def update_host(self, context, ihost_obj):
        """Update an ihost with the supplied data.

        This method allows an ihost to be updated.

        :param context: an admin context
        :param ihost_obj: a changed (but not saved) ihost object
        :returns: updated ihost object, including all fields.
        """

        delta = ihost_obj.obj_what_changed()
        if ('id' in delta) or ('uuid' in delta):
            raise exception.InventoryException(_(
                "Invalid method call: update_host cannot change id or uuid "))

        ihost_obj.save(context)
        return ihost_obj

    def _dnsmasq_host_entry_to_string(self, ip_addr, hostname,
                                      mac_addr=None, cid=None):
        if IPNetwork(ip_addr).version == constants.IPV6_FAMILY:
            ip_addr = "[%s]" % ip_addr
        if cid:
            line = "id:%s,%s,%s,1d\n" % (cid, hostname, ip_addr)
        elif mac_addr:
            line = "%s,%s,%s,1d\n" % (mac_addr, hostname, ip_addr)
        else:
            line = "%s,%s\n" % (hostname, ip_addr)
        return line

    def _dnsmasq_addn_host_entry_to_string(self, ip_addr, hostname,
                                           aliases=[]):
        line = "%s %s" % (ip_addr, hostname)
        for alias in aliases:
            line = "%s %s" % (line, alias)
        line = "%s\n" % line
        return line

    def get_my_host_id(self):
        if not ConductorManager.my_host_id:
            local_hostname = socket.gethostname()
            controller = self.dbapi.host_get(local_hostname)
            ConductorManager.my_host_id = controller['id']
        return ConductorManager.my_host_id

    def get_dhcp_server_duid(self):
        """Retrieves the server DUID from the local DHCP server lease file."""
        lease_filename = tsc.CONFIG_PATH + 'dnsmasq.leases'
        with open(lease_filename, 'r') as lease_file:
            for columns in (line.strip().split() for line in lease_file):
                if len(columns) != 2:
                    continue
                keyword, value = columns
                if keyword.lower() == "duid":
                    return value

    def _dhcp_release(self, interface, ip_address, mac_address, cid=None):
        """Release a given DHCP lease"""
        params = [interface, ip_address, mac_address]
        if cid:
            params += [cid]
        if IPAddress(ip_address).version == 6:
            params = ["--ip", ip_address,
                      "--iface", interface,
                      "--server-id", self.get_dhcp_server_duid(),
                      "--client-id", cid,
                      "--iaid", str(cutils.get_dhcp_client_iaid(mac_address))]
            LOG.warning("Invoking dhcp_release6 for {}".format(params))
            subprocess.call(["dhcp_release6"] + params)
        else:
            LOG.warning("Invoking dhcp_release for {}".format(params))
            subprocess.call(["dhcp_release"] + params)

    def _find_networktype_for_address(self, ip_address):
        LOG.info("SC to be queried from systemconfig")
        # TODO(sc) query from systemconfig

    def _find_local_interface_name(self, network_type):
        """Lookup the local interface name for a given network type."""
        host_id = self.get_my_host_id()
        interface_list = self.dbapi.iinterface_get_all(host_id, expunge=True)
        ifaces = dict((i['ifname'], i) for i in interface_list)
        port_list = self.dbapi.port_get_all(host_id)
        ports = dict((p['interface_id'], p) for p in port_list)
        for interface in interface_list:
            if interface.networktype == network_type:
                return cutils.get_interface_os_ifname(interface, ifaces, ports)

    def _find_local_mgmt_interface_vlan_id(self):
        """Lookup the local interface name for a given network type."""
        host_id = self.get_my_host_id()
        interface_list = self.dbapi.iinterface_get_all(host_id, expunge=True)
        for interface in interface_list:
            if interface.networktype == constants.NETWORK_TYPE_MGMT:
                if 'vlan_id' not in interface:
                    return 0
                else:
                    return interface['vlan_id']

    def _remove_leases_by_mac_address(self, mac_address):
        """Remove any leases that were added without a CID that we were not
        able to delete.  This is specifically looking for leases on the pxeboot
        network that may still be present but will also handle the unlikely
        event of deleting an old host during an upgrade.  Hosts on previous
        releases did not register a CID on the mgmt interface.
        """
        lease_filename = tsc.CONFIG_PATH + 'dnsmasq.leases'
        try:
            with open(lease_filename, 'r') as lease_file:
                for columns in (line.strip().split() for line in lease_file):
                    if len(columns) != 5:
                        continue
                    timestamp, address, ip_address, hostname, cid = columns
                    if address != mac_address:
                        continue
                    network_type = self._find_networktype_for_address(
                        ip_address)
                    if not network_type:
                        # Not one of our managed networks
                        LOG.warning("Lease for unknown network found in "
                                    "dnsmasq.leases file: {}".format(columns))
                        continue
                    interface_name = self._find_local_interface_name(
                        network_type
                    )
                    self._dhcp_release(interface_name, ip_address, mac_address)
        except Exception as e:
            LOG.error("Failed to remove leases for %s: %s" % (mac_address,
                                                              str(e)))

    def configure_host(self, context, host_obj,
                       do_compute_apply=False):
        """Configure a host.

        :param context: an admin context
        :param host_obj: the host object
        :param do_compute_apply: configure the compute subfunctions of the host
        """

        LOG.info("rpc conductor configure_host %s" % host_obj.uuid)

        # Request systemconfig plugin to configure_host
        sc_host = self.sc_op.host_configure(
            host_uuid=host_obj.uuid,
            do_compute_apply=do_compute_apply)

        LOG.info("sc_op sc_host=%s" % sc_host)

        if sc_host:
            return sc_host.as_dict()

    def unconfigure_host(self, context, host_obj):
        """Unconfigure a host.

        :param context: an admin context.
        :param host_obj: a host object.
        """
        LOG.info("unconfigure_host %s." % host_obj.uuid)

        # Request systemconfig plugin to unconfigure_host
        self.sc_op.host_unconfigure(host_obj.uuid)

    def port_update_by_host(self, context,
                            host_uuid, inic_dict_array):
        """Create iports for an ihost with the supplied data.

        This method allows records for iports for ihost to be created.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param inic_dict_array: initial values for iport objects
        :returns: pass or fail
        """

        LOG.debug("Entering port_update_by_host %s %s" %
                  (host_uuid, inic_dict_array))
        host_uuid.strip()
        try:
            ihost = self.dbapi.host_get(host_uuid)
        except exception.HostNotFound:
            LOG.exception("Invalid host_uuid %s" % host_uuid)
            return

        for inic in inic_dict_array:
            LOG.info("Processing inic %s" % inic)
            bootp = None
            port = None
            # ignore port if no MAC address present, this will
            # occur for data port after they are configured via DPDK driver
            if not inic['mac']:
                continue

            try:
                inic_dict = {'host_id': ihost['id']}
                inic_dict.update(inic)
                if cutils.is_valid_mac(inic['mac']):
                    # Is this the port that the management interface is on?
                    if inic['mac'].strip() == ihost['mgmt_mac'].strip():
                        # SKIP auto create management/pxeboot network
                        # was for all nodes but the active controller
                        bootp = 'True'
                        inic_dict.update({'bootp': bootp})

                try:
                    LOG.debug("Attempting to create new port %s on host %s" %
                              (inic_dict, ihost['id']))

                    port = self.dbapi.ethernet_port_get_by_mac(inic['mac'])
                    # update existing port with updated attributes
                    try:
                        port_dict = {
                            'sriov_totalvfs': inic['sriov_totalvfs'],
                            'sriov_numvfs': inic['sriov_numvfs'],
                            'sriov_vfs_pci_address':
                                inic['sriov_vfs_pci_address'],
                            'driver': inic['driver'],
                            'dpdksupport': inic['dpdksupport'],
                            'speed': inic['speed'],
                        }

                        LOG.info("port %s update attr: %s" %
                                 (port.uuid, port_dict))
                        self.dbapi.ethernet_port_update(port.uuid, port_dict)
                    except Exception:
                        LOG.exception("Failed to update port %s" % inic['mac'])
                        pass

                except Exception:
                    # adjust for field naming differences between the NIC
                    # dictionary returned by the agent and the Port model
                    port_dict = inic_dict.copy()
                    port_dict['name'] = port_dict.pop('pname', None)
                    port_dict['namedisplay'] = port_dict.pop('pnamedisplay',
                                                             None)

                    LOG.info("Attempting to create new port %s "
                             "on host %s" % (inic_dict, ihost.uuid))
                    port = self.dbapi.ethernet_port_create(
                        ihost.uuid, port_dict)

            except exception.HostNotFound:
                raise exception.InventoryException(
                    _("Invalid host_uuid: host not found: %s") %
                    host_uuid)

            except Exception:
                pass

        if ihost.invprovision not in [k_host.PROVISIONED,
                                      k_host.PROVISIONING]:
            value = {'invprovision': k_host.UNPROVISIONED}
            self.dbapi.host_update(host_uuid, value)

    def lldp_tlv_dict(self, agent_neighbour_dict):
        tlv_dict = {}
        for k, v in agent_neighbour_dict.iteritems():
            if v is not None and k in k_lldp.LLDP_TLV_VALID_LIST:
                tlv_dict.update({k: v})
        return tlv_dict

    def lldp_agent_tlv_update(self, tlv_dict, agent):
        tlv_update_list = []
        tlv_create_list = []
        agent_id = agent['id']
        agent_uuid = agent['uuid']

        tlvs = self.dbapi.lldp_tlv_get_by_agent(agent_uuid)
        for k, v in tlv_dict.iteritems():
            for tlv in tlvs:
                if tlv['type'] == k:
                    tlv_value = tlv_dict.get(tlv['type'])
                    entry = {'type': tlv['type'],
                             'value': tlv_value}
                    if tlv['value'] != tlv_value:
                        tlv_update_list.append(entry)
                    break
            else:
                tlv_create_list.append({'type': k,
                                        'value': v})

        if tlv_update_list:
            try:
                tlvs = self.dbapi.lldp_tlv_update_bulk(tlv_update_list,
                                                       agentid=agent_id)
            except Exception as e:
                LOG.exception("Error during bulk TLV update for agent %s: %s",
                              agent_id, str(e))
                raise
        if tlv_create_list:
            try:
                self.dbapi.lldp_tlv_create_bulk(tlv_create_list,
                                                agentid=agent_id)
            except Exception as e:
                LOG.exception("Error during bulk TLV create for agent %s: %s",
                              agent_id, str(e))
                raise

    def lldp_neighbour_tlv_update(self, tlv_dict, neighbour):
        tlv_update_list = []
        tlv_create_list = []
        neighbour_id = neighbour['id']
        neighbour_uuid = neighbour['uuid']

        tlvs = self.dbapi.lldp_tlv_get_by_neighbour(neighbour_uuid)
        for k, v in tlv_dict.iteritems():
            for tlv in tlvs:
                if tlv['type'] == k:
                    tlv_value = tlv_dict.get(tlv['type'])
                    entry = {'type': tlv['type'],
                             'value': tlv_value}
                    if tlv['value'] != tlv_value:
                        tlv_update_list.append(entry)
                    break
            else:
                tlv_create_list.append({'type': k,
                                        'value': v})

        if tlv_update_list:
            try:
                tlvs = self.dbapi.lldp_tlv_update_bulk(
                    tlv_update_list,
                    neighbourid=neighbour_id)
            except Exception as e:
                LOG.exception("Error during bulk TLV update for neighbour"
                              "%s: %s", neighbour_id, str(e))
                raise
        if tlv_create_list:
            try:
                self.dbapi.lldp_tlv_create_bulk(tlv_create_list,
                                                neighbourid=neighbour_id)
            except Exception as e:
                LOG.exception("Error during bulk TLV create for neighbour"
                              "%s: %s",
                              neighbour_id, str(e))
                raise

    def lldp_agent_update_by_host(self, context,
                                  host_uuid, agent_dict_array):
        """Create or update lldp agents for an host with the supplied data.

        This method allows records for lldp agents for ihost to be created or
        updated.

        :param context: an admin context
        :param host_uuid: host uuid unique id
        :param agent_dict_array: initial values for lldp agent objects
        :returns: pass or fail
        """
        LOG.debug("Entering lldp_agent_update_by_host %s %s" %
                  (host_uuid, agent_dict_array))
        host_uuid.strip()
        try:
            db_host = self.dbapi.host_get(host_uuid)
        except exception.HostNotFound:
            raise exception.InventoryException(_(
                "Invalid host_uuid: %s") % host_uuid)

        try:
            db_ports = self.dbapi.port_get_by_host(host_uuid)
        except Exception:
            raise exception.InventoryException(_(
                "Error getting ports for host %s") % host_uuid)

        try:
            db_agents = self.dbapi.lldp_agent_get_by_host(host_uuid)
        except Exception:
            raise exception.InventoryException(_(
                "Error getting LLDP agents for host %s") % host_uuid)

        for agent in agent_dict_array:
            port_found = None
            for db_port in db_ports:
                if (db_port['name'] == agent['name_or_uuid'] or
                        db_port['uuid'] == agent['name_or_uuid']):
                    port_found = db_port
                    break

            if not port_found:
                LOG.debug("Could not find port for agent %s",
                          agent['name_or_uuid'])
                return

            hostid = db_host['id']
            portid = db_port['id']

            agent_found = None
            for db_agent in db_agents:
                if db_agent['port_id'] == portid:
                    agent_found = db_agent
                    break

            LOG.debug("Processing agent %s" % agent)

            agent_dict = {'host_id': hostid,
                          'port_id': portid,
                          'status': agent['status']}
            update_tlv = False
            try:
                if not agent_found:
                    LOG.info("Attempting to create new LLDP agent "
                             "%s on host %s" % (agent_dict, hostid))
                    if agent['state'] != k_lldp.LLDP_AGENT_STATE_REMOVED:
                        db_agent = self.dbapi.lldp_agent_create(portid,
                                                                hostid,
                                                                agent_dict)
                        update_tlv = True
                else:
                    # If the agent exists, try to update some of the fields
                    # or remove it
                    agent_uuid = db_agent['uuid']
                    if agent['state'] == k_lldp.LLDP_AGENT_STATE_REMOVED:
                        db_agent = self.dbapi.lldp_agent_destroy(agent_uuid)
                    else:
                        attr = {'status': agent['status'],
                                'system_name': agent['system_name']}
                        db_agent = self.dbapi.lldp_agent_update(agent_uuid,
                                                                attr)
                        update_tlv = True

                if update_tlv:
                    tlv_dict = self.lldp_tlv_dict(agent)
                    self.lldp_agent_tlv_update(tlv_dict, db_agent)

            except exception.InvalidParameterValue:
                raise exception.InventoryException(_(
                    "Failed to update/delete non-existing"
                    "lldp agent %s") % agent_uuid)
            except exception.LLDPAgentExists:
                raise exception.InventoryException(_(
                    "Failed to add LLDP agent %s. "
                    "Already exists") % agent_uuid)
            except exception.HostNotFound:
                raise exception.InventoryException(_(
                    "Invalid host_uuid: host not found: %s") %
                    host_uuid)
            except exception.PortNotFound:
                raise exception.InventoryException(_(
                    "Invalid port id: port not found: %s") %
                    portid)
            except Exception as e:
                raise exception.InventoryException(_(
                    "Failed to update lldp agent: %s") % e)

    def lldp_neighbour_update_by_host(self, context,
                                      host_uuid, neighbour_dict_array):
        """Create or update lldp neighbours for an ihost with the supplied data.

        This method allows records for lldp neighbours for ihost to be created
        or updated.

        :param context: an admin context
        :param host_uuid: host uuid unique id
        :param neighbour_dict_array: initial values for lldp neighbour objects
        :returns: pass or fail
        """
        LOG.debug("Entering lldp_neighbour_update_by_host %s %s" %
                  (host_uuid, neighbour_dict_array))
        host_uuid.strip()
        try:
            db_host = self.dbapi.host_get(host_uuid)
        except Exception:
            raise exception.InventoryException(_(
                "Invalid host_uuid: %s") % host_uuid)

        try:
            db_ports = self.dbapi.port_get_by_host(host_uuid)
        except Exception:
            raise exception.InventoryException(_(
                "Error getting ports for host %s") % host_uuid)

        try:
            db_neighbours = self.dbapi.lldp_neighbour_get_by_host(host_uuid)
        except Exception:
            raise exception.InventoryException(_(
                "Error getting LLDP neighbours for host %s") % host_uuid)

        reported = set([(d['msap']) for d in neighbour_dict_array])
        stale = [d for d in db_neighbours if (d['msap']) not in reported]
        for neighbour in stale:
            db_neighbour = self.dbapi.lldp_neighbour_destroy(
                neighbour['uuid'])

        for neighbour in neighbour_dict_array:
            port_found = None
            for db_port in db_ports:
                if (db_port['name'] == neighbour['name_or_uuid'] or
                        db_port['uuid'] == neighbour['name_or_uuid']):
                    port_found = db_port
                    break

            if not port_found:
                LOG.debug("Could not find port for neighbour %s",
                          neighbour['name'])
                return

            LOG.debug("Processing lldp neighbour %s" % neighbour)

            hostid = db_host['id']
            portid = db_port['id']
            msap = neighbour['msap']
            state = neighbour['state']

            neighbour_dict = {'host_id': hostid,
                              'port_id': portid,
                              'msap': msap}

            neighbour_found = False
            for db_neighbour in db_neighbours:
                if db_neighbour['msap'] == msap:
                    neighbour_found = db_neighbour
                    break

            update_tlv = False
            try:
                if not neighbour_found:
                    LOG.info("Attempting to create new lldp neighbour "
                             "%r on host %s" % (neighbour_dict, hostid))
                    db_neighbour = self.dbapi.lldp_neighbour_create(
                        portid, hostid, neighbour_dict)
                    update_tlv = True
                else:
                    # If the neighbour exists, remove it if requested by
                    # the agent. Otherwise, trigger a TLV update.  There
                    # are currently no neighbour attributes that need to
                    # be updated.
                    if state == k_lldp.LLDP_NEIGHBOUR_STATE_REMOVED:
                        db_neighbour = self.dbapi.lldp_neighbour_destroy(
                            db_neighbour['uuid'])
                    else:
                        update_tlv = True
                if update_tlv:
                    tlv_dict = self.lldp_tlv_dict(neighbour)
                    self.lldp_neighbour_tlv_update(tlv_dict,
                                                   db_neighbour)
            except exception.InvalidParameterValue:
                raise exception.InventoryException(_(
                    "Failed to update/delete lldp neighbour. "
                    "Invalid parameter: %r") % tlv_dict)
            except exception.LLDPNeighbourExists:
                raise exception.InventoryException(_(
                    "Failed to add lldp neighbour %r. "
                    "Already exists") % neighbour_dict)
            except exception.HostNotFound:
                raise exception.InventoryException(_(
                    "Invalid host_uuid: host not found: %s") %
                    host_uuid)
            except exception.PortNotFound:
                raise exception.InventoryException(
                    _("Invalid port id: port not found: %s") %
                    portid)
            except Exception as e:
                raise exception.InventoryException(_(
                    "Couldn't update LLDP neighbour: %s") % e)

    def pci_device_update_by_host(self, context,
                                  host_uuid, pci_device_dict_array):
        """Create devices for an ihost with the supplied data.

        This method allows records for devices for ihost to be created.

        :param context: an admin context
        :param host_uuid: host uuid unique id
        :param pci_device_dict_array: initial values for device objects
        :returns: pass or fail
        """
        LOG.debug("Entering device_update_by_host %s %s" %
                  (host_uuid, pci_device_dict_array))
        host_uuid.strip()
        try:
            host = self.dbapi.host_get(host_uuid)
        except exception.HostNotFound:
            LOG.exception("Invalid host_uuid %s" % host_uuid)
            return
        for pci_dev in pci_device_dict_array:
            LOG.debug("Processing dev %s" % pci_dev)
            try:
                pci_dev_dict = {'host_id': host['id']}
                pci_dev_dict.update(pci_dev)
                dev_found = None
                try:
                    dev = self.dbapi.pci_device_get(pci_dev['pciaddr'],
                                                    hostid=host['id'])
                    dev_found = dev
                    if not dev:
                        LOG.info("Attempting to create new device "
                                 "%s on host %s" % (pci_dev_dict, host['id']))
                        dev = self.dbapi.pci_device_create(host['id'],
                                                           pci_dev_dict)
                except Exception:
                    LOG.info("Attempting to create new device "
                             "%s on host %s" % (pci_dev_dict, host['id']))
                    dev = self.dbapi.pci_device_create(host['id'],
                                                       pci_dev_dict)

                # If the device exists, try to update some of the fields
                if dev_found:
                    try:
                        attr = {
                            'pclass_id': pci_dev['pclass_id'],
                            'pvendor_id': pci_dev['pvendor_id'],
                            'pdevice_id': pci_dev['pdevice_id'],
                            'pclass': pci_dev['pclass'],
                            'pvendor': pci_dev['pvendor'],
                            'psvendor': pci_dev['psvendor'],
                            'psdevice': pci_dev['psdevice'],
                            'sriov_totalvfs': pci_dev['sriov_totalvfs'],
                            'sriov_numvfs': pci_dev['sriov_numvfs'],
                            'sriov_vfs_pci_address':
                                pci_dev['sriov_vfs_pci_address'],
                            'driver': pci_dev['driver']}
                        LOG.info("attr: %s" % attr)
                        dev = self.dbapi.pci_device_update(dev['uuid'], attr)
                    except Exception:
                        LOG.exception("Failed to update port %s" %
                                      dev['pciaddr'])
                        pass

            except exception.HostNotFound:
                raise exception.InventoryException(
                    _("Invalid host_uuid: host not found: %s") %
                    host_uuid)
            except Exception:
                pass

    def numas_update_by_host(self, context,
                             host_uuid, inuma_dict_array):
        """Create inumas for an ihost with the supplied data.

        This method allows records for inumas for ihost to be created.
        Updates the port node_id once its available.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param inuma_dict_array: initial values for inuma objects
        :returns: pass or fail
        """

        host_uuid.strip()
        try:
            ihost = self.dbapi.host_get(host_uuid)
        except exception.HostNotFound:
            LOG.exception("Invalid host_uuid %s" % host_uuid)
            return

        try:
            # Get host numa nodes which may already be in db
            mynumas = self.dbapi.node_get_by_host(host_uuid)
        except exception.HostNotFound:
            raise exception.InventoryException(_(
                "Invalid host_uuid: host not found: %s") % host_uuid)

        mynuma_nodes = [n.numa_node for n in mynumas]

        # perform update for ports
        ports = self.dbapi.ethernet_port_get_by_host(host_uuid)
        for i in inuma_dict_array:
            if 'numa_node' in i and i['numa_node'] in mynuma_nodes:
                LOG.info("Already in db numa_node=%s mynuma_nodes=%s" %
                         (i['numa_node'], mynuma_nodes))
                continue

            try:
                inuma_dict = {'host_id': ihost['id']}

                inuma_dict.update(i)

                inuma = self.dbapi.node_create(ihost['id'], inuma_dict)

                for port in ports:
                    port_node = port['numa_node']
                    if port_node == -1:
                        port_node = 0  # special handling

                    if port_node == inuma['numa_node']:
                        attr = {'node_id': inuma['id']}
                        self.dbapi.ethernet_port_update(port['uuid'], attr)

            except exception.HostNotFound:
                raise exception.InventoryException(
                    _("Invalid host_uuid: host not found: %s") %
                    host_uuid)
            except Exception:  # this info may have been posted previously
                pass

    def _get_default_platform_cpu_count(self, ihost, node,
                                        cpu_count, hyperthreading):
        """Return the initial number of reserved logical cores for platform
           use.  This can be overridden later by the end user.
        """
        cpus = 0
        if cutils.host_has_function(ihost, k_host.COMPUTE) and node == 0:
            cpus += 1 if not hyperthreading else 2
            if cutils.host_has_function(ihost, k_host.CONTROLLER):
                cpus += 1 if not hyperthreading else 2
        return cpus

    def _get_default_vswitch_cpu_count(self, ihost, node,
                                       cpu_count, hyperthreading):
        """Return the initial number of reserved logical cores for vswitch use.
           This can be overridden later by the end user.
        """
        if cutils.host_has_function(ihost, k_host.COMPUTE) and node == 0:
            physical_cores = (cpu_count / 2) if hyperthreading else cpu_count
            system_mode = self.dbapi.system_get_one().system_mode
            if system_mode == constants.SYSTEM_MODE_SIMPLEX:
                return 1 if not hyperthreading else 2
            else:
                if physical_cores > 4:
                    return 2 if not hyperthreading else 4
                elif physical_cores > 1:
                    return 1 if not hyperthreading else 2
        return 0

    def _get_default_shared_cpu_count(self, ihost, node,
                                      cpu_count, hyperthreading):
        """Return the initial number of reserved logical cores for shared
           use.  This can be overridden later by the end user.
        """
        return 0

    def _sort_by_socket_and_coreid(self, icpu_dict):
        """Sort a list of cpu dict objects such that lower numbered sockets
           appear first and that threads of the same core are adjacent in the
           list with the lowest thread number appearing first.
        """
        return int(icpu_dict['numa_node']), int(icpu_dict['core']), int(icpu_dict['thread'])  # noqa

    def _get_hyperthreading_enabled(self, cpu_list):
        """Determine if hyperthreading is enabled based on whether any threads
           exist with a threadId greater than 0
        """
        for cpu in cpu_list:
            if int(cpu['thread']) > 0:
                return True
        return False

    def _get_node_cpu_count(self, cpu_list, node):
        count = 0
        for cpu in cpu_list:
            count += 1 if int(cpu['numa_node']) == node else 0
        return count

    def _get_default_cpu_functions(self, host, node, cpu_list, hyperthreading):
        """Return the default list of CPU functions to be reserved for this
           host on the specified numa node.
        """
        functions = []
        cpu_count = self._get_node_cpu_count(cpu_list, node)
        # Determine how many platform cpus need to be reserved
        count = self._get_default_platform_cpu_count(
            host, node, cpu_count, hyperthreading)
        for i in range(0, count):
            functions.append(constants.PLATFORM_FUNCTION)
        # Determine how many vswitch cpus need to be reserved
        count = self._get_default_vswitch_cpu_count(
            host, node, cpu_count, hyperthreading)
        for i in range(0, count):
            functions.append(constants.VSWITCH_FUNCTION)
        # Determine how many shared cpus need to be reserved
        count = self._get_default_shared_cpu_count(
            host, node, cpu_count, hyperthreading)
        for i in range(0, count):
            functions.append(constants.SHARED_FUNCTION)
        # Assign the default function to the remaining cpus
        for i in range(0, (cpu_count - len(functions))):
            functions.append(cpu_utils.get_default_function(host))
        return functions

    def print_cpu_topology(self, hostname=None, subfunctions=None,
                           reference=None,
                           sockets=None, cores=None, threads=None):
        """Print logical cpu topology table (for debug reasons).

        :param hostname: hostname
        :param subfunctions: subfunctions
        :param reference: reference label
        :param sockets: dictionary of socket_ids, sockets[cpu_id]
        :param cores:   dictionary of core_ids,   cores[cpu_id]
        :param threads: dictionary of thread_ids, threads[cpu_id]
        :returns: None
        """
        if sockets is None or cores is None or threads is None:
            LOG.error("print_cpu_topology: topology not defined. "
                      "sockets=%s, cores=%s, threads=%s"
                      % (sockets, cores, threads))
            return

        # calculate overall cpu topology stats
        n_sockets = len(set(sockets.values()))
        n_cores = len(set(cores.values()))
        n_threads = len(set(threads.values()))
        if n_sockets < 1 or n_cores < 1 or n_threads < 1:
            LOG.error("print_cpu_topology: unexpected topology. "
                      "n_sockets=%d, n_cores=%d, n_threads=%d"
                      % (n_sockets, n_cores, n_threads))
            return

        # build each line of output
        ll = ''
        s = ''
        c = ''
        t = ''
        for cpu in sorted(cores.keys()):
            ll += '%3d' % cpu
            s += '%3d' % sockets[cpu]
            c += '%3d' % cores[cpu]
            t += '%3d' % threads[cpu]

        LOG.info('Logical CPU topology: host:%s (%s), '
                 'sockets:%d, cores/socket=%d, threads/core=%d, reference:%s'
                 % (hostname, subfunctions, n_sockets, n_cores, n_threads,
                    reference))
        LOG.info('%9s : %s' % ('cpu_id', ll))
        LOG.info('%9s : %s' % ('socket_id', s))
        LOG.info('%9s : %s' % ('core_id', c))
        LOG.info('%9s : %s' % ('thread_id', t))

    def update_cpu_config(self, context, host_uuid):
        LOG.info("TODO send to systemconfig update_cpu_config")

    def cpus_update_by_host(self, context,
                            host_uuid, icpu_dict_array,
                            force_grub_update=False):
        """Create cpus for an ihost with the supplied data.

        This method allows records for cpus for ihost to be created.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param icpu_dict_array: initial values for cpu objects
        :param force_grub_update: bool value to force grub update
        :returns: pass or fail
        """

        host_uuid.strip()
        try:
            ihost = self.dbapi.host_get(host_uuid)
        except exception.HostNotFound:
            LOG.exception("Invalid host_uuid %s" % host_uuid)
            return

        host_id = ihost['id']
        ihost_inodes = self.dbapi.node_get_by_host(host_uuid)

        icpus = self.dbapi.cpu_get_by_host(host_uuid)

        num_cpus_dict = len(icpu_dict_array)
        num_cpus_db = len(icpus)

        # Capture 'current' topology in dictionary format
        cs = {}
        cc = {}
        ct = {}
        if num_cpus_dict > 0:
            for icpu in icpu_dict_array:
                cpu_id = icpu.get('cpu')
                cs[cpu_id] = icpu.get('numa_node')
                cc[cpu_id] = icpu.get('core')
                ct[cpu_id] = icpu.get('thread')

        # Capture 'previous' topology in dictionary format
        ps = {}
        pc = {}
        pt = {}
        if num_cpus_db > 0:
            for icpu in icpus:
                cpu_id = icpu.get('cpu')
                core_id = icpu.get('core')
                thread_id = icpu.get('thread')
                node_id = icpu.get('node_id')
                socket_id = None
                for inode in ihost_inodes:
                    if node_id == inode.get('id'):
                        socket_id = inode.get('numa_node')
                        break
                ps[cpu_id] = socket_id
                pc[cpu_id] = core_id
                pt[cpu_id] = thread_id

        if num_cpus_dict > 0 and num_cpus_db == 0:
            self.print_cpu_topology(hostname=ihost.get('hostname'),
                                    subfunctions=ihost.get('subfunctions'),
                                    reference='current (initial)',
                                    sockets=cs, cores=cc, threads=ct)

        if num_cpus_dict > 0 and num_cpus_db > 0:
            LOG.debug("num_cpus_dict=%d num_cpus_db= %d. "
                      "icpud_dict_array= %s cpus.as_dict= %s" %
                      (num_cpus_dict, num_cpus_db, icpu_dict_array, icpus))

            # Skip update if topology has not changed
            if ps == cs and pc == cc and pt == ct:
                self.print_cpu_topology(hostname=ihost.get('hostname'),
                                        subfunctions=ihost.get('subfunctions'),
                                        reference='current (unchanged)',
                                        sockets=cs, cores=cc, threads=ct)
                if ihost.administrative == k_host.ADMIN_LOCKED and \
                        force_grub_update:
                    self.update_cpu_config(context, host_uuid)
                return

            self.print_cpu_topology(hostname=ihost.get('hostname'),
                                    subfunctions=ihost.get('subfunctions'),
                                    reference='previous',
                                    sockets=ps, cores=pc, threads=pt)
            self.print_cpu_topology(hostname=ihost.get('hostname'),
                                    subfunctions=ihost.get('subfunctions'),
                                    reference='current (CHANGED)',
                                    sockets=cs, cores=cc, threads=ct)

            # there has been an update.  Delete db entries and replace.
            for icpu in icpus:
                self.dbapi.cpu_destroy(icpu.uuid)

        # sort the list of cpus by socket and coreid
        cpu_list = sorted(icpu_dict_array, key=self._sort_by_socket_and_coreid)

        # determine if hyperthreading is enabled
        hyperthreading = self._get_hyperthreading_enabled(cpu_list)

        # build the list of functions to be assigned to each cpu
        functions = {}
        for n in ihost_inodes:
            numa_node = int(n.numa_node)
            functions[numa_node] = self._get_default_cpu_functions(
                ihost, numa_node, cpu_list, hyperthreading)

        for data in cpu_list:
            try:
                node_id = None
                for n in ihost_inodes:
                    numa_node = int(n.numa_node)
                    if numa_node == int(data['numa_node']):
                        node_id = n['id']
                        break

                cpu_dict = {'host_id': host_id,
                            'node_id': node_id,
                            'allocated_function': functions[numa_node].pop(0)}

                cpu_dict.update(data)

                self.dbapi.cpu_create(host_id, cpu_dict)

            except exception.HostNotFound:
                raise exception.InventoryException(
                    _("Invalid host_uuid: host not found: %s") %
                    host_uuid)
            except Exception:
                # info may have already been posted
                pass

        # if it is the first controller wait for the initial config to
        # be completed
        if ((utils.is_host_simplex_controller(ihost) and
                os.path.isfile(tsc.INITIAL_CONFIG_COMPLETE_FLAG)) or
                (not utils.is_host_simplex_controller(ihost) and
                 ihost.administrative == k_host.ADMIN_LOCKED)):
            LOG.info("Update CPU grub config, host_uuid (%s), name (%s)"
                     % (host_uuid, ihost.get('hostname')))
            self.update_cpu_config(context, host_uuid)

        return

    def _get_platform_reserved_memory(self, ihost, node):
        low_core = cutils.is_low_core_system(ihost, self.dbapi)
        reserved = cutils.get_required_platform_reserved_memory(
            ihost, node, low_core)
        return {'platform_reserved_mib': reserved} if reserved else {}

    def memory_update_by_host(self, context,
                              host_uuid, imemory_dict_array,
                              force_update):
        """Create or update memory for a host with the supplied data.

        This method allows records for memory for host to be created,
        or updated.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param imemory_dict_array: initial values for cpu objects
        :param force_update: force host memory update
        :returns: pass or fail
        """

        host_uuid.strip()
        try:
            ihost = self.dbapi.host_get(host_uuid)
        except exception.ServerNotFound:
            LOG.exception("Invalid host_uuid %s" % host_uuid)
            return

        if ihost['administrative'] == k_host.ADMIN_LOCKED and \
            ihost['invprovision'] == k_host.PROVISIONED and \
                not force_update:
            LOG.debug("Ignore the host memory audit after the host is locked")
            return

        forihostid = ihost['id']
        ihost_inodes = self.dbapi.node_get_by_host(host_uuid)

        for i in imemory_dict_array:
            forinodeid = None
            inode_uuid = None
            for n in ihost_inodes:
                numa_node = int(n.numa_node)
                if numa_node == int(i['numa_node']):
                    forinodeid = n['id']
                    inode_uuid = n['uuid']
                    inode_uuid.strip()
                    break
            else:
                # not found in host_nodes, do not add memory element
                continue

            mem_dict = {'forihostid': forihostid,
                        'forinodeid': forinodeid}

            mem_dict.update(i)

            # Do not allow updates to the amounts of reserved memory.
            mem_dict.pop('platform_reserved_mib', None)

            # numa_node is not stored against imemory table
            mem_dict.pop('numa_node', None)

            # clear the pending hugepage number for unlocked nodes
            if ihost.administrative == k_host.ADMIN_UNLOCKED:
                mem_dict['vm_hugepages_nr_2M_pending'] = None
                mem_dict['vm_hugepages_nr_1G_pending'] = None

            try:
                imems = self.dbapi.memory_get_by_host_node(host_uuid,
                                                           inode_uuid)
                if not imems:
                    # Set the amount of memory reserved for platform use.
                    mem_dict.update(self._get_platform_reserved_memory(
                        ihost, i['numa_node']))
                    self.dbapi.memory_create(forihostid, mem_dict)
                else:
                    for imem in imems:
                        # Include 4K pages in the displayed VM memtotal
                        if imem.vm_hugepages_nr_4K is not None:
                            vm_4K_mib = \
                                (imem.vm_hugepages_nr_4K /
                                 constants.NUM_4K_PER_MiB)
                            mem_dict['memtotal_mib'] += vm_4K_mib
                            mem_dict['memavail_mib'] += vm_4K_mib
                        self.dbapi.memory_update(imem['uuid'],
                                                 mem_dict)
            except Exception:
                # Set the amount of memory reserved for platform use.
                mem_dict.update(self._get_platform_reserved_memory(
                    ihost, i['numa_node']))
                self.dbapi.memory_create(forihostid, mem_dict)
                pass

        return

    def _get_disk_available_mib(self, disk, agent_disk_dict):
        partitions = self.dbapi.partition_get_by_idisk(disk['uuid'])

        if not partitions:
            LOG.debug("Disk %s has no partitions" % disk.uuid)
            return agent_disk_dict['available_mib']

        available_mib = agent_disk_dict['available_mib']
        for part in partitions:
            if (part.status in
                    [constants.PARTITION_CREATE_IN_SVC_STATUS,
                     constants.PARTITION_CREATE_ON_UNLOCK_STATUS]):
                available_mib = available_mib - part.size_mib

        LOG.debug("Disk available mib host - %s disk - %s av - %s" %
                  (disk.host_id, disk.device_node, available_mib))
        return available_mib

    def platform_update_by_host(self, context,
                                host_uuid, imsg_dict):
        """Create or update imemory for an ihost with the supplied data.

        This method allows records for memory for ihost to be created,
        or updated.

        This method is invoked on initialization once.  Note, swact also
        results in restart, but not of inventory-agent?

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param imsg_dict:  inventory message
        :returns: pass or fail
        """

        host_uuid.strip()
        try:
            ihost = self.dbapi.host_get(host_uuid)
        except exception.HostNotFound:
            LOG.exception("Invalid host_uuid %s" % host_uuid)
            return

        availability = imsg_dict.get('availability')

        val = {}
        action_state = imsg_dict.get(k_host.HOST_ACTION_STATE)
        if action_state and action_state != ihost.action_state:
            LOG.info("%s updating action_state=%s" % (ihost.hostname,
                                                      action_state))
            val[k_host.HOST_ACTION_STATE] = action_state

        iscsi_initiator_name = imsg_dict.get('iscsi_initiator_name')
        if (iscsi_initiator_name and
                ihost.iscsi_initiator_name is None):
            LOG.info("%s updating iscsi initiator=%s" %
                     (ihost.hostname, iscsi_initiator_name))
            val['iscsi_initiator_name'] = iscsi_initiator_name

        if val:
            ihost = self.dbapi.host_update(host_uuid, val)

        if not availability:
            return

        if cutils.host_has_function(ihost, k_host.COMPUTE):
            if availability == k_host.VIM_SERVICES_ENABLED:
                # TODO(sc) report to systemconfig platform available, it will
                # independently also update with config applied
                LOG.info("Need report to systemconfig  iplatform available "
                         "for ihost=%s imsg=%s"
                         % (host_uuid, imsg_dict))
            elif availability == k_host.AVAILABILITY_OFFLINE:
                # TODO(sc) report to systemconfig platform AVAILABILITY_OFFLINE
                LOG.info("Need report iplatform not available for host=%s "
                         "imsg= %s" % (host_uuid, imsg_dict))

        if ((ihost.personality == k_host.STORAGE and
                ihost.hostname == k_host.STORAGE_0_HOSTNAME) or
                (ihost.personality == k_host.CONTROLLER)):
            # TODO(sc) report to systemconfig platform available
            LOG.info("TODO report to systemconfig platform available")

    def subfunctions_update_by_host(self, context,
                                    host_uuid, subfunctions):
        """Update subfunctions for a host.

        This method allows records for subfunctions to be updated.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param subfunctions: subfunctions provided by the ihost
        :returns: pass or fail
        """
        host_uuid.strip()

        # Create the host entry in neutron to allow for data interfaces to
        # be configured on a combined node
        if (k_host.CONTROLLER in subfunctions and
                k_host.COMPUTE in subfunctions):
            try:
                ihost = self.dbapi.host_get(host_uuid)
            except exception.HostNotFound:
                LOG.exception("Invalid host_uuid %s" % host_uuid)
                return

            try:
                neutron_host_id = \
                    self._openstack.get_neutron_host_id_by_name(
                        context, ihost['hostname'])
                if not neutron_host_id:
                    self._openstack.create_neutron_host(context,
                                                        host_uuid,
                                                        ihost['hostname'])
                elif neutron_host_id != host_uuid:
                    self._openstack.delete_neutron_host(context,
                                                        neutron_host_id)
                    self._openstack.create_neutron_host(context,
                                                        host_uuid,
                                                        ihost['hostname'])
            except Exception:  # TODO(sc) Needs better exception
                LOG.exception("Failed in neutron stuff")

        ihost_val = {'subfunctions': subfunctions}
        self.dbapi.host_update(host_uuid, ihost_val)

    def get_host_by_macs(self, context, host_macs):
        """Finds ihost db entry based upon the mac list

        This method returns an ihost if it matches a mac

        :param context: an admin context
        :param host_macs: list of mac addresses
        :returns: ihost object, including all fields.
        """

        ihosts = objects.Host.list(context)

        LOG.debug("Checking ihost db for macs: %s" % host_macs)
        for mac in host_macs:
            try:
                mac = mac.rstrip()
                mac = cutils.validate_and_normalize_mac(mac)
            except Exception:
                LOG.warn("get_host_by_macs invalid mac: %s" % mac)
                continue

            for host in ihosts:
                if host.mgmt_mac == mac:
                    LOG.info("Host found ihost db for macs: %s" %
                             host.hostname)
                    return host
        LOG.debug("RPC get_host_by_macs called but found no ihost.")

    def get_host_by_hostname(self, context, hostname):
        """Finds host db entry based upon the host hostname

        This method returns a host if it matches the host
        hostname.

        :param context: an admin context
        :param hostname: host hostname
        :returns: host object, including all fields.
        """

        try:
            return objects.Host.get_by_filters_one(context,
                                                   {'hostname': hostname})
        except exception.HostNotFound:
            pass

        LOG.info("RPC host_get_by_hostname called but found no host.")

    def _audit_host_action(self, host):
        """Audit whether the host_action needs to be terminated or escalated.
        """

        if host.administrative == k_host.ADMIN_UNLOCKED:
            host_action_str = host.host_action or ""

            if (host_action_str.startswith(k_host.ACTION_FORCE_LOCK) or
                    host_action_str.startswith(k_host.ACTION_LOCK)):

                task_str = host.task or ""
                if (('--' in host_action_str and
                        host_action_str.startswith(
                            k_host.ACTION_FORCE_LOCK)) or
                        ('----------' in host_action_str and
                         host_action_str.startswith(k_host.ACTION_LOCK))):
                    ihost_mtc = host.as_dict()
                    keepkeys = ['host_action', 'vim_progress_status']
                    ihost_mtc = cutils.removekeys_nonmtce(ihost_mtc,
                                                          keepkeys)

                    if host_action_str.startswith(
                            k_host.ACTION_FORCE_LOCK):
                        timeout_in_secs = 6
                        ihost_mtc['operation'] = 'modify'
                        ihost_mtc['action'] = k_host.ACTION_FORCE_LOCK
                        ihost_mtc['task'] = k_host.FORCE_LOCKING
                        LOG.warn("host_action override %s" %
                                 ihost_mtc)
                        mtce_api.host_modify(
                            self._api_token, self._mtc_address, self._mtc_port,
                            ihost_mtc, timeout_in_secs)

                    # need time for FORCE_LOCK mtce to clear
                    if '----' in host_action_str:
                        host_action_str = ""
                    else:
                        host_action_str += "-"

                    if (task_str.startswith(k_host.FORCE_LOCKING) or
                       task_str.startswith(k_host.LOCKING)):
                        val = {'task': "",
                               'host_action': host_action_str,
                               'vim_progress_status': ""}
                    else:
                        val = {'host_action': host_action_str,
                               'vim_progress_status': ""}
                else:
                    host_action_str += "-"
                    if (task_str.startswith(k_host.FORCE_LOCKING) or
                       task_str.startswith(k_host.LOCKING)):
                        task_str += "-"
                        val = {'task': task_str,
                               'host_action': host_action_str}
                    else:
                        val = {'host_action': host_action_str}

                self.dbapi.host_update(host.uuid, val)
        else:  # Administrative locked already
            task_str = host.task or ""
            if (task_str.startswith(k_host.FORCE_LOCKING) or
               task_str.startswith(k_host.LOCKING)):
                val = {'task': ""}
                self.dbapi.host_update(host.uuid, val)

        vim_progress_status_str = host.get('vim_progress_status') or ""
        if (vim_progress_status_str and
           (vim_progress_status_str != k_host.VIM_SERVICES_ENABLED) and
           (vim_progress_status_str != k_host.VIM_SERVICES_DISABLED)):
            if '..' in vim_progress_status_str:
                LOG.info("Audit clearing vim_progress_status=%s" %
                         vim_progress_status_str)
                vim_progress_status_str = ""
            else:
                vim_progress_status_str += ".."

            val = {'vim_progress_status': vim_progress_status_str}
            self.dbapi.host_update(host.uuid, val)

    def _audit_install_states(self, host):
        # A node could shutdown during it's installation and the install_state
        # for example could get stuck at the value "installing". To avoid
        # this situation we audit the sanity of the states by appending the
        # character '+' to the states in the database. After 15 minutes of the
        # states not changing, set the install_state to failed.

        # The audit's interval is 60sec
        MAX_COUNT = 15

        # Allow longer duration for booting phase
        MAX_COUNT_BOOTING = 40

        LOG.info("Auditing %s, install_state is %s",
                 host.hostname, host.install_state)
        LOG.debug("Auditing %s, availability is %s",
                  host.hostname, host.availability)

        if (host.administrative == k_host.ADMIN_LOCKED and
                host.install_state is not None):

            install_state = host.install_state.rstrip('+')

            if host.install_state != constants.INSTALL_STATE_FAILED:
                if (install_state == constants.INSTALL_STATE_BOOTING and
                        host.availability !=
                        k_host.AVAILABILITY_OFFLINE):
                    host.install_state = constants.INSTALL_STATE_COMPLETED

                if (install_state != constants.INSTALL_STATE_INSTALLED and
                        install_state !=
                        constants.INSTALL_STATE_COMPLETED):
                    if (install_state ==
                            constants.INSTALL_STATE_INSTALLING and
                            host.install_state_info is not None):
                        if host.install_state_info.count('+') >= MAX_COUNT:
                            LOG.info(
                                "Auditing %s, install_state changed from "
                                "'%s' to '%s'", host.hostname,
                                host.install_state,
                                constants.INSTALL_STATE_FAILED)
                            host.install_state = \
                                constants.INSTALL_STATE_FAILED
                        else:
                            host.install_state_info += "+"
                    else:
                        if (install_state ==
                                constants.INSTALL_STATE_BOOTING):
                            max_count = MAX_COUNT_BOOTING
                        else:
                            max_count = MAX_COUNT
                        if host.install_state.count('+') >= max_count:
                            LOG.info(
                                "Auditing %s, install_state changed from "
                                "'%s' to '%s'", host.hostname,
                                host.install_state,
                                constants.INSTALL_STATE_FAILED)
                            host.install_state = \
                                constants.INSTALL_STATE_FAILED
                        else:
                            host.install_state += "+"

            # It is possible we get stuck in an installed failed state. For
            # example if a node gets powered down during an install booting
            # state and then powered on again. Clear it if the node is
            # online.
            elif (host.availability == k_host.AVAILABILITY_ONLINE and
                    host.install_state == constants.INSTALL_STATE_FAILED):
                host.install_state = constants.INSTALL_STATE_COMPLETED

            self.dbapi.host_update(host.uuid,
                                   {'install_state': host.install_state,
                                    'install_state_info':
                                        host.install_state_info})

    def configure_systemname(self, context, systemname):
        """Configure the systemname with the supplied data.

        :param context: an admin context.
        :param systemname: the systemname
        """

        LOG.debug("configure_systemname: sending systemname to agent(s)")
        rpcapi = agent_rpcapi.AgentAPI()
        rpcapi.configure_systemname(context, systemname=systemname)

        return

    @staticmethod
    def _get_fm_entity_instance_id(host_obj):
        """
        Create 'entity_instance_id' from host_obj data
        """

        entity_instance_id = "%s=%s" % (fm_constants.FM_ENTITY_TYPE_HOST,
                                        host_obj.hostname)
        return entity_instance_id

    def _log_host_create(self, host, reason=None):
        """
        Create host discovery event customer log.
        """
        if host.hostname:
            hostid = host.hostname
        else:
            hostid = host.mgmt_mac

        if reason is not None:
            reason_text = ("%s has been 'discovered' on the network. (%s)" %
                           (hostid, reason))
        else:
            reason_text = ("%s has been 'discovered'." % hostid)

        # action event -> FM_ALARM_TYPE_4 = 'equipment'
        # FM_ALARM_SEVERITY_CLEAR to be consistent with 200.x series Info
        log_data = {'hostid': hostid,
                    'event_id': fm_constants.FM_LOG_ID_HOST_DISCOVERED,
                    'entity_type': fm_constants.FM_ENTITY_TYPE_HOST,
                    'entity': 'host=%s.event=discovered' % hostid,
                    'fm_severity': fm_constants.FM_ALARM_SEVERITY_CLEAR,
                    'fm_event_type': fm_constants.FM_ALARM_TYPE_4,
                    'reason_text': reason_text,
                    }
        self.fm_log.customer_log(log_data)

    def _update_subfunctions(self, context, ihost_obj):
        """Update subfunctions."""

        ihost_obj.invprovision = k_host.PROVISIONED
        ihost_obj.save(context)

    def notify_subfunctions_config(self, context,
                                   host_uuid, ihost_notify_dict):
        """
        Notify inventory of host subfunctions configuration status
        """

        subfunctions_configured = ihost_notify_dict.get(
            'subfunctions_configured') or ""
        try:
            ihost_obj = self.dbapi.host_get(host_uuid)
        except Exception as e:
            LOG.exception("notify_subfunctions_config e=%s "
                          "ihost=%s subfunctions=%s" %
                          (e, host_uuid, subfunctions_configured))
            return False

        if not subfunctions_configured:
            self._update_subfunctions(context, ihost_obj)

    def _add_port_to_list(self, interface_id, networktype, port_list):
        info = {}
        ports = self.dbapi.port_get_all(interfaceid=interface_id)
        if ports:
            info['name'] = ports[0]['name']
            info['numa_node'] = ports[0]['numa_node']
            info['networktype'] = networktype
            if info not in port_list:
                port_list.append(info)
        return port_list

    def bm_deprovision_by_host(self, context, host_uuid, ibm_msg_dict):
        """Update ihost upon notification of board management controller
           deprovisioning.

        This method also allows a dictionary of values to be passed in to
        affort additional controls, if and as needed.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param ibm_msg_dict: values for additional controls or changes
        :returns: pass or fail
        """
        LOG.info("bm_deprovision_by_host=%s msg=%s" %
                 (host_uuid, ibm_msg_dict))

        isensorgroups = self.dbapi.sensorgroup_get_by_host(host_uuid)

        for isensorgroup in isensorgroups:
            isensors = self.dbapi.sensor_get_by_sensorgroup(isensorgroup.uuid)
            for isensor in isensors:
                self.dbapi.sensor_destroy(isensor.uuid)

            self.dbapi.sensorgroup_destroy(isensorgroup.uuid)

        isensors = self.dbapi.sensor_get_by_host(host_uuid)
        if isensors:
            LOG.info("bm_deprovision_by_host=%s Non-group sensors=%s" %
                     (host_uuid, isensors))
            for isensor in isensors:
                self.dbapi.sensor_destroy(isensor.uuid)

        isensors = self.dbapi.sensor_get_by_host(host_uuid)

        return True

    def configure_ttys_dcd(self, context, uuid, ttys_dcd):
        """Notify agent to configure the dcd with the supplied data.

        :param context: an admin context.
        :param uuid: the host uuid
        :param ttys_dcd: the flag to enable/disable dcd
        """

        LOG.debug("ConductorManager.configure_ttys_dcd: sending dcd update %s "
                  "%s to agents" % (ttys_dcd, uuid))
        rpcapi = agent_rpcapi.AgentAPI()
        rpcapi.configure_ttys_dcd(context, uuid=uuid, ttys_dcd=ttys_dcd)

    def get_host_ttys_dcd(self, context, ihost_id):
        """
        Retrieve the serial line carrier detect state for a given host
        """
        ihost = self.dbapi.host_get(ihost_id)
        if ihost:
            return ihost.ttys_dcd
        else:
            LOG.error("Host: %s not found in database" % ihost_id)
            return None

    def _get_cinder_address_name(self, network_type):
        ADDRESS_FORMAT_ARGS = (k_host.CONTROLLER_HOSTNAME,
                               network_type)
        return "%s-cinder-%s" % ADDRESS_FORMAT_ARGS

    def configure_keystore_account(self, context, service_name,
                                   username, password):
        """Synchronously, have a conductor configure a ks(keyring) account.

        Does the following tasks:
        - call keyring API to create an account under a service.

        :param context: request context.
        :param service_name: the keystore service.
        :param username: account username
        :param password: account password
        """
        if not service_name.strip():
            raise exception.InventoryException(_(
                "Keystore service is a blank value"))

        keyring.set_password(service_name, username, password)

    def unconfigure_keystore_account(self, context, service_name, username):
        """Synchronously, have a conductor unconfigure a ks(keyring) account.

        Does the following tasks:
        - call keyring API to delete an account under a service.

        :param context: request context.
        :param service_name: the keystore service.
        :param username: account username
        """
        try:
            keyring.delete_password(service_name, username)
        except keyring.errors.PasswordDeleteError:
            pass
