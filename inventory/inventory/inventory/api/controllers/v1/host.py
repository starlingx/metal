# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright 2013 Hewlett-Packard Development Company, L.P.
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
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import ast
import cgi
from configutilities import HOST_XML_ATTRIBUTES
import copy
from fm_api import constants as fm_constants
from inventory.api.controllers.v1 import base
from inventory.api.controllers.v1 import collection
from inventory.api.controllers.v1 import cpu as cpu_api
# TODO(LK) from inventory.api.controllers.v1 import disk
# TODO(LK) from inventory.api.controllers.v1 import partition
from inventory.api.controllers.v1 import ethernet_port
from inventory.api.controllers.v1 import link
from inventory.api.controllers.v1 import lldp_agent
from inventory.api.controllers.v1 import lldp_neighbour
from inventory.api.controllers.v1 import memory
from inventory.api.controllers.v1 import node as node_api
from inventory.api.controllers.v1 import pci_device
from inventory.api.controllers.v1 import port
from inventory.api.controllers.v1.query import Query
from inventory.api.controllers.v1 import sensor as sensor_api
from inventory.api.controllers.v1 import sensorgroup
from inventory.api.controllers.v1 import types
from inventory.api.controllers.v1 import utils
from inventory.common import ceph
from inventory.common import constants
from inventory.common import exception
from inventory.common import health
from inventory.common.i18n import _
from inventory.common import k_host
from inventory.common import mtce_api
from inventory.common import patch_api
from inventory.common import sm_api
from inventory.common.storage_backend_conf import StorageBackendConfig
# TODO(sc) to be removed StorageBackendConfig
from inventory.common import utils as cutils
from inventory.common import vim_api
from inventory import objects
import json
import jsonpatch
from oslo_log import log
from oslo_utils import uuidutils
import pecan
from pecan import expose
from pecan import rest
import psutil
import re
import six
from six import text_type as unicode
import tsconfig.tsconfig as tsc
import wsme
from wsme import types as wtypes
import wsmeext.pecan as wsme_pecan
from xml.dom import minidom as dom
import xml.etree.ElementTree as ET
import xml.etree.ElementTree as et

LOG = log.getLogger(__name__)
KEYRING_BM_SERVICE = "BM"
ERR_CODE_LOCK_SOLE_SERVICE_PROVIDER = "-1003"


class Host(base.APIBase):
    """API representation of a host.

    This class enforces type checking and value constraints, and
    converts between the internal object model and
    the API representation of a host.
    """

    id = int

    uuid = wtypes.text

    hostname = wtypes.text
    "Represent the hostname of the host"

    invprovision = wtypes.text
    "Represent the current provision state of the host"

    administrative = wtypes.text
    "Represent the administrative state of the host"

    operational = wtypes.text
    "Represent the operational state of the host"

    availability = wtypes.text
    "Represent the availability status of the host"

    mgmt_mac = wtypes.text
    "Represent the boot mgmt MAC address of the host."

    mgmt_ip = wtypes.text
    "Represent the boot mgmt IP address of the host."

    infra_ip = wtypes.text
    "Represent the infrastructure IP address of the host."

    bm_ip = wtypes.text
    "Represent the board management IP address of the host."

    bm_type = wtypes.text
    "Represent the board management type of the host."

    bm_username = wtypes.text
    "Represent the board management username of the host."

    bm_password = wtypes.text
    "Represent the board management password of the host."

    personality = wtypes.text
    "Represent the personality of the host"

    subfunctions = wtypes.text
    "Represent the subfunctions of the host"

    subfunction_oper = wtypes.text
    "Represent the subfunction operational state of the host"

    subfunction_avail = wtypes.text
    "Represent the subfunction availability status of the host"

    serialid = wtypes.text
    "Represent the serial id of the host"

    action = wtypes.text
    'Represent the action on the host'

    host_action = wtypes.text
    'Represent the current action task in progress'

    vim_progress_status = wtypes.text
    'Represent the vim progress status'

    task = wtypes.text
    "Represent the mtce task state"

    mtce_info = wtypes.text
    "Represent the mtce info"

    uptime = int
    "Represent the uptime, in seconds, of the host."

    location = {wtypes.text: utils.ValidTypes(wtypes.text, six.integer_types)}
    "Represent the location of the host"

    capabilities = {wtypes.text: utils.ValidTypes(wtypes.text,
                                                  six.integer_types)}
    "Represent the capabilities of the host"

    system_uuid = types.uuid
    "The UUID of the system this host belongs to"

    boot_device = wtypes.text
    "Represent the boot device of the host"

    rootfs_device = wtypes.text
    "Represent the rootfs device of the host"

    install_output = wtypes.text
    "Represent the install_output of the host"

    console = wtypes.text
    "Represent the console of the host"

    tboot = wtypes.text
    "Represent the tboot of the host"

    ttys_dcd = wtypes.text
    "Enable or disable serial console carrier detect"

    install_state = wtypes.text
    "Represent the install state"

    install_state_info = wtypes.text
    "Represent install state extra information if there is any"

    iscsi_initiator_name = wtypes.text
    "The iscsi initiator name (only used for compute hosts)"

    links = [link.Link]
    "A list containing a self link and associated host links"

    ports = [link.Link]
    "Links to the collection of Ports on this host"

    ethernet_ports = [link.Link]
    "Links to the collection of EthernetPorts on this host"

    nodes = [link.Link]
    "Links to the collection of nodes on this host"

    cpus = [link.Link]
    "Links to the collection of cpus on this host"

    memorys = [link.Link]
    "Links to the collection of memorys on this host"

    # idisks = [link.Link]
    # "Links to the collection of idisks on this ihost"

    sensors = [link.Link]
    "Links to the collection of sensors on this host"

    sensorgroups = [link.Link]
    "Links to the collection of sensorgruops on this host"

    pci_devices = [link.Link]
    "Links to the collection of pci_devices on this host"

    lldp_agents = [link.Link]
    "Links to the collection of LldpAgents on this ihost"

    lldp_neighbours = [link.Link]
    "Links to the collection of LldpNeighbours on this ihost"

    def __init__(self, **kwargs):
        self.fields = objects.Host.fields.keys()
        for k in self.fields:
            setattr(self, k, kwargs.get(k))

    @classmethod
    def convert_with_links(cls, rpc_ihost, expand=True):
        minimum_fields = [
            'id', 'uuid', 'hostname',
            'personality', 'subfunctions',
            'subfunction_oper', 'subfunction_avail',
            'administrative', 'operational', 'availability',
            'invprovision', 'task', 'mtce_info', 'action', 'uptime',
            'host_action', 'mgmt_mac', 'mgmt_ip', 'infra_ip', 'location',
            'bm_ip', 'bm_type', 'bm_username',
            'system_uuid', 'capabilities', 'serialid',
            'created_at', 'updated_at', 'boot_device',
            'rootfs_device', 'install_output', 'console',
            'tboot', 'ttys_dcd',
            'install_state', 'install_state_info',
            'iscsi_initiator_name']

        fields = minimum_fields if not expand else None
        uhost = Host.from_rpc_object(rpc_ihost, fields)
        uhost.links = [link.Link.make_link('self', pecan.request.host_url,
                                           'hosts', uhost.uuid),
                       link.Link.make_link('bookmark',
                                           pecan.request.host_url,
                                           'hosts', uhost.uuid,
                                           bookmark=True)
                       ]
        if expand:
            uhost.ports = [link.Link.make_link('self',
                                               pecan.request.host_url,
                                               'hosts',
                                               uhost.uuid + "/ports"),
                           link.Link.make_link(
                               'bookmark',
                               pecan.request.host_url,
                               'hosts',
                               uhost.uuid + "/ports",
                               bookmark=True)
                           ]
            uhost.ethernet_ports = [
                link.Link.make_link('self',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/ethernet_ports"),
                link.Link.make_link('bookmark',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/ethernet_ports",
                                    bookmark=True)
            ]
            uhost.nodes = [link.Link.make_link('self',
                                               pecan.request.host_url,
                                               'hosts',
                                               uhost.uuid + "/nodes"),
                           link.Link.make_link(
                               'bookmark',
                               pecan.request.host_url,
                               'hosts',
                               uhost.uuid + "/nodes",
                               bookmark=True)
                           ]
            uhost.cpus = [link.Link.make_link('self',
                                              pecan.request.host_url,
                                              'hosts',
                                              uhost.uuid + "/cpus"),
                          link.Link.make_link(
                              'bookmark',
                              pecan.request.host_url,
                              'hosts',
                              uhost.uuid + "/cpus",
                              bookmark=True)
                          ]

            uhost.memorys = [link.Link.make_link('self',
                                                 pecan.request.host_url,
                                                 'hosts',
                                                 uhost.uuid + "/memorys"),
                             link.Link.make_link(
                                 'bookmark',
                                 pecan.request.host_url,
                                 'hosts',
                                 uhost.uuid + "/memorys",
                                 bookmark=True)
                             ]

            uhost.disks = [link.Link.make_link('self',
                                               pecan.request.host_url,
                                               'hosts',
                                               uhost.uuid + "/disks"),
                           link.Link.make_link(
                               'bookmark',
                               pecan.request.host_url,
                               'hosts',
                               uhost.uuid + "/disks",
                               bookmark=True)
                           ]

            uhost.sensors = [link.Link.make_link('self',
                                                 pecan.request.host_url,
                                                 'hosts',
                                                 uhost.uuid + "/sensors"),
                             link.Link.make_link('bookmark',
                                                 pecan.request.host_url,
                                                 'hosts',
                                                 uhost.uuid + "/sensors",
                                                 bookmark=True)
                             ]

            uhost.sensorgroups = [
                link.Link.make_link('self',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/sensorgroups"),
                link.Link.make_link('bookmark',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/sensorgroups",
                                    bookmark=True)
            ]

            uhost.pci_devices = [
                link.Link.make_link('self',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/pci_devices"),
                link.Link.make_link('bookmark',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/pci_devices",
                                    bookmark=True)
            ]

            uhost.lldp_agents = [
                link.Link.make_link('self',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/lldp_agents"),
                link.Link.make_link('bookmark',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/lldp_agents",
                                    bookmark=True)
            ]

            uhost.lldp_neighbours = [
                link.Link.make_link('self',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/lldp_neighbors"),
                link.Link.make_link('bookmark',
                                    pecan.request.host_url,
                                    'hosts',
                                    uhost.uuid + "/lldp_neighbors",
                                    bookmark=True)
            ]

        return uhost


class HostCollection(collection.Collection):
    """API representation of a collection of hosts."""

    hosts = [Host]
    "A list containing hosts objects"

    def __init__(self, **kwargs):
        self._type = 'hosts'

    @classmethod
    def convert_with_links(cls, ihosts, limit, url=None,
                           expand=False, **kwargs):
        collection = HostCollection()
        collection.hosts = [
            Host.convert_with_links(n, expand) for n in ihosts]
        collection.next = collection.get_next(limit, url=url, **kwargs)
        return collection


class HostUpdate(object):
    """Host update helper class.
    """

    CONTINUE = "continue"
    EXIT_RETURN_HOST = "exit_return_host"
    EXIT_UPDATE_PREVAL = "exit_update_preval"
    FAILED = "failed"
    PASSED = "passed"

    ACTIONS_TO_TASK_DISPLAY_CHOICES = (
        (None, ""),
        ("", ""),
        (k_host.ACTION_UNLOCK, _("Unlocking")),
        (k_host.ACTION_FORCE_UNLOCK, _("Force Unlocking")),
        (k_host.ACTION_LOCK, _("Locking")),
        (k_host.ACTION_FORCE_LOCK, _("Force Locking")),
        (k_host.ACTION_RESET, _("Resetting")),
        (k_host.ACTION_REBOOT, _("Rebooting")),
        (k_host.ACTION_REINSTALL, _("Reinstalling")),
        (k_host.ACTION_POWERON, _("Powering-on")),
        (k_host.ACTION_POWEROFF, _("Powering-off")),
        (k_host.ACTION_SWACT, _("Swacting")),
        (k_host.ACTION_FORCE_SWACT, _("Force-Swacting")),
    )

    def __init__(self, host_orig, host_patch, delta):
        self.ihost_orig = dict(host_orig)
        self.ihost_patch = dict(host_patch)
        self._delta = list(delta)
        self._ihost_val_prenotify = {}
        self._ihost_val = {}

        self._configure_required = False
        self._notify_vim = False
        self._notify_mtce = False
        self._notify_availability = None
        self._notify_vim_add_host = False
        self._notify_action_lock = False
        self._notify_action_lock_force = False
        self._skip_notify_mtce = False
        self._bm_type_changed_to_none = False
        self._nextstep = self.CONTINUE

        self._action = None
        self.displayid = host_patch.get('hostname')
        if not self.displayid:
            self.displayid = host_patch.get('uuid')

        LOG.debug("host_orig=%s, host_patch=%s, delta=%s" %
                  (self.ihost_orig, self.ihost_patch, self.delta))

    @property
    def action(self):
        return self._action

    @action.setter
    def action(self, val):
        self._action = val

    @property
    def delta(self):
        return self._delta

    @property
    def nextstep(self):
        return self._nextstep

    @nextstep.setter
    def nextstep(self, val):
        self._nextstep = val

    @property
    def configure_required(self):
        return self._configure_required

    @configure_required.setter
    def configure_required(self, val):
        self._configure_required = val

    @property
    def bm_type_changed_to_none(self):
        return self._bm_type_changed_to_none

    @bm_type_changed_to_none.setter
    def bm_type_changed_to_none(self, val):
        self._bm_type_changed_to_none = val

    @property
    def notify_vim_add_host(self):
        return self._notify_vim_add_host

    @notify_vim_add_host.setter
    def notify_vim_add_host(self, val):
        self._notify_vim_add_host = val

    @property
    def skip_notify_mtce(self):
        return self._skip_notify_mtce

    @skip_notify_mtce.setter
    def skip_notify_mtce(self, val):
        self._skip_notify_mtce = val

    @property
    def notify_action_lock(self):
        return self._notify_action_lock

    @notify_action_lock.setter
    def notify_action_lock(self, val):
        self._notify_action_lock = val

    @property
    def notify_action_lock_force(self):
        return self._notify_action_lock_force

    @notify_action_lock_force.setter
    def notify_action_lock_force(self, val):
        self._notify_action_lock_force = val

    @property
    def ihost_val_prenotify(self):
        return self._ihost_val_prenotify

    def ihost_val_prenotify_update(self, val):
        self._ihost_val_prenotify.update(val)

    @property
    def ihost_val(self):
        return self._ihost_val

    def ihost_val_update(self, val):
        self._ihost_val.update(val)

    @property
    def notify_vim(self):
        return self._notify_vim

    @notify_vim.setter
    def notify_vim(self, val):
        self._notify_vim = val

    @property
    def notify_mtce(self):
        return self._notify_mtce

    @notify_mtce.setter
    def notify_mtce(self, val):
        self._notify_mtce = val

    @property
    def notify_availability(self):
        return self._notify_availability

    @notify_availability.setter
    def notify_availability(self, val):
        self._notify_availability = val

    def get_task_from_action(self, action):
        """Lookup the task value in the action to task dictionary."""

        display_choices = self.ACTIONS_TO_TASK_DISPLAY_CHOICES

        display_value = [display for (value, display) in display_choices
                         if value and value.lower() == (action or '').lower()]

        if display_value:
            return display_value[0]
        return None


LOCK_NAME = 'HostController'
LOCK_NAME_SYS = 'HostControllerSys'


class HostController(rest.RestController):
    """REST controller for hosts."""

    ports = port.PortController(
        from_hosts=True)
    "Expose ports as a sub-element of hosts"

    ethernet_ports = ethernet_port.EthernetPortController(
        from_hosts=True)
    "Expose ethernet_ports as a sub-element of hosts"

    nodes = node_api.NodeController(from_hosts=True)
    "Expose nodes as a sub-element of hosts"

    cpus = cpu_api.CPUController(from_hosts=True)
    "Expose cpus as a sub-element of hosts"

    memorys = memory.MemoryController(from_hosts=True)
    "Expose memorys as a sub-element of hosts"

    # TODO(LK) idisks = disk.DiskController(from_hosts=True)
    # "Expose idisks as a sub-element of hosts"

    sensors = sensor_api.SensorController(from_hosts=True)
    "Expose sensors as a sub-element of hosts"

    sensorgroups = sensorgroup.SensorGroupController(from_hosts=True)
    "Expose sensorgroups as a sub-element of hosts"

    pci_devices = pci_device.PCIDeviceController(from_hosts=True)
    "Expose pci_devices as a sub-element of hosts"

    lldp_agents = lldp_agent.LLDPAgentController(
        from_hosts=True)
    "Expose lldp_agents as a sub-element of hosts"

    lldp_neighbours = lldp_neighbour.LLDPNeighbourController(
        from_hosts=True)
    "Expose lldp_neighbours as a sub-element of hosts"

    _custom_actions = {
        'detail': ['GET'],
        'bulk_add': ['POST'],
        'bulk_export': ['GET'],
        'install_progress': ['POST'],
    }

    def __init__(self, from_system=False):
        self._from_system = from_system
        self._mtc_address = k_host.LOCALHOST_HOSTNAME
        self._mtc_port = 2112
        self._ceph = ceph.CephApiOperator()
        self._api_token = None

    def _ihosts_get(self, isystem_id, marker, limit, personality,
                    sort_key, sort_dir, q=None):
        if self._from_system and not isystem_id:
            raise exception.InvalidParameterValue(_(
                "System id not specified."))

        limit = utils.validate_limit(limit)
        sort_dir = utils.validate_sort_dir(sort_dir)

        filters = {}
        if q is not None:
            for i in q:
                if i.op == 'eq':
                    filters[i.field] = i.value

        marker_obj = None
        if marker:
            marker_obj = objects.Host.get_by_uuid(pecan.request.context,
                                                  marker)

        if isystem_id:
            ihosts = pecan.request.dbapi.host_get_by_isystem(
                isystem_id, limit,
                marker_obj,
                sort_key=sort_key,
                sort_dir=sort_dir)
        else:
            if personality:
                ihosts = objects.Host.list(
                    pecan.request.context,
                    limit, marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir,
                    filters={'personality': personality})
            else:
                ihosts = objects.Host.list(
                    pecan.request.context,
                    limit, marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir,
                    filters=filters)

        for h in ihosts:
            self._update_controller_personality(h)

        return ihosts

    @staticmethod
    def _get_controller_address(hostname):
        networktype = constants.NETWORK_TYPE_MGMT
        name = '%s-%s' % (hostname, networktype)
        address = pecan.request.systemconfig.address_get_by_name(name)
        LOG.info("systemconfig _get_controller_address=%s" % address)
        return address

    @staticmethod
    def _get_storage_address(hostname):
        networktype = constants.NETWORK_TYPE_MGMT
        name = '%s-%s' % (hostname, networktype)
        return pecan.request.systemconfig.address_get_by_name(name)

    @staticmethod
    def _update_subfunctions(ihost):
        subfunctions = ihost.get('subfunctions') or ""
        personality = ihost.get('personality') or ""
        # handle race condition with subfunctions being updated late.
        if not subfunctions:
            LOG.info("update_subfunctions: subfunctions not set. "
                     "personality=%s" % personality)
            if personality == k_host.CONTROLLER:
                subfunctions = ','.join(tsc.subfunctions)
            else:
                subfunctions = personality
            ihost['subfunctions'] = subfunctions

        subfunctions_set = set(subfunctions.split(','))
        if personality not in subfunctions_set:
            # Automatically add it
            subfunctions_list = list(subfunctions_set)
            subfunctions_list.insert(0, personality)
            subfunctions = ','.join(subfunctions_list)
            LOG.info("%s personality=%s update subfunctions=%s" %
                     (ihost.get('hostname'), personality, subfunctions))
        LOG.debug("update_subfunctions:  personality=%s subfunctions=%s" %
                  (personality, subfunctions))
        return subfunctions

    @staticmethod
    def _update_controller_personality(host):
        if host['personality'] == k_host.CONTROLLER:
            if utils.is_host_active_controller(host):
                activity = 'Controller-Active'
            else:
                activity = 'Controller-Standby'
            host['capabilities'].update({'Personality': activity})

    @wsme_pecan.wsexpose(HostCollection, [Query], unicode, unicode, int,
                         unicode, unicode, unicode)
    def get_all(self, q=[], isystem_id=None, marker=None, limit=None,
                personality=None, sort_key='id', sort_dir='asc'):
        """Retrieve a list of hosts."""
        ihosts = self._ihosts_get(
            isystem_id, marker, limit, personality, sort_key, sort_dir, q=q)
        return HostCollection.convert_with_links(ihosts, limit,
                                                 sort_key=sort_key,
                                                 sort_dir=sort_dir)

    @wsme_pecan.wsexpose(unicode, unicode, body=unicode)
    def install_progress(self, uuid, install_state,
                         install_state_info=None):
        """Update the install status for the given host."""
        LOG.debug("Update host uuid %s with install_state=%s "
                  "and install_state_info=%s" %
                  (uuid, install_state, install_state_info))
        if install_state == constants.INSTALL_STATE_INSTALLED:
            # After an install a node will reboot right away. Change the state
            # to reflect this.
            install_state = constants.INSTALL_STATE_BOOTING

        host = objects.Host.get_by_uuid(pecan.request.context, uuid)
        pecan.request.dbapi.host_update(host['uuid'],
                                        {'install_state': install_state,
                                         'install_state_info':
                                         install_state_info})

    @wsme_pecan.wsexpose(HostCollection, unicode, unicode, int, unicode,
                         unicode, unicode)
    def detail(self, isystem_id=None, marker=None, limit=None,
               personality=None, sort_key='id', sort_dir='asc'):
        """Retrieve a list of hosts with detail."""
        # /detail should only work against collections
        parent = pecan.request.path.split('/')[:-1][-1]
        if parent != "hosts":
            raise exception.HTTPNotFound

        ihosts = self._ihosts_get(
            isystem_id, marker, limit, personality, sort_key, sort_dir)
        resource_url = '/'.join(['hosts', 'detail'])
        return HostCollection.convert_with_links(ihosts, limit,
                                                 url=resource_url,
                                                 expand=True,
                                                 sort_key=sort_key,
                                                 sort_dir=sort_dir)

    @wsme_pecan.wsexpose(Host, unicode)
    def get_one(self, uuid):
        """Retrieve information about the given ihost."""
        if self._from_system:
            raise exception.OperationNotPermitted

        rpc_ihost = objects.Host.get_by_uuid(pecan.request.context, uuid)
        self._update_controller_personality(rpc_ihost)

        return Host.convert_with_links(rpc_ihost)

    def _add_host_semantic_checks(self, ihost_dict):
        chosts = self._get_controllers()
        if chosts and ihost_dict.get('personality') is None:
            # Prevent adding any new host(s) until there is
            # an unlocked-enabled controller to manage them.
            for c in chosts:
                if (c.administrative == k_host.ADMIN_UNLOCKED and
                        c.operational == k_host.OPERATIONAL_ENABLED):
                    break
            else:
                raise wsme.exc.ClientSideError(
                    _("Provisioning request for new host '%s' is not permitted"
                      " while there is no unlocked-enabled controller. Unlock "
                      "controller-0, wait for it to enable and then retry.") %
                    ihost_dict.get('mgmt_mac'))

    def _new_host_semantic_checks(self, ihost_dict):

        if self._get_controllers():
            self._add_host_semantic_checks(ihost_dict)

            mgmt_network = pecan.request.systemconfig.network_get_by_type(
                constants.NETWORK_TYPE_MGMT)
            LOG.info("systemconfig mgmt_network={}".format(mgmt_network))

            if mgmt_network.dynamic and ihost_dict.get('mgmt_ip'):
                # raise wsme.exc.ClientSideError(_(
                LOG.info(_(
                    "Host-add Allowed: Specifying a mgmt_ip when dynamic "
                    "address allocation is configured"))
            elif (not mgmt_network.dynamic and
                  not ihost_dict.get('mgmt_ip') and
                  ihost_dict.get('personality') not in
                  [k_host.STORAGE, k_host.CONTROLLER]):
                raise wsme.exc.ClientSideError(_(
                    "Host-add Rejected: Cannot add a compute host without "
                    "specifying a mgmt_ip when static address allocation is "
                    "configured."))

            # Check whether the system mode is simplex
            if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
                raise wsme.exc.ClientSideError(_(
                    "Host-add Rejected: Adding a host on a simplex system "
                    "is not allowed."))

        personality = ihost_dict['personality']
        if not ihost_dict['hostname']:
            if personality not in (k_host.CONTROLLER, k_host.STORAGE):
                raise wsme.exc.ClientSideError(_(
                    "Host-add Rejected. Must provide a hostname for a node of "
                    "personality %s") % personality)
        else:
            self._validate_hostname(ihost_dict['hostname'], personality)

        HostController._personality_license_check(personality)

    def _do_post(self, ihost_dict):
        """Create a new ihost based off a dictionary of attributes """

        log_start = cutils.timestamped("ihost_post_start")
        LOG.info("SYS_I host %s %s add" % (ihost_dict['hostname'],
                                           log_start))

        power_on = ihost_dict.get('power_on', None)

        ihost_obj = None

        # Semantic checks for adding a new node
        if self._from_system:
            raise exception.OperationNotPermitted

        self._new_host_semantic_checks(ihost_dict)
        current_ihosts = objects.Host.list(pecan.request.context)

        # Check for missing/invalid hostname
        # ips/hostnames are automatic for controller & storage nodes
        if ihost_dict['personality'] not in (k_host.CONTROLLER,
                                             k_host.STORAGE):
            host_names = [h.hostname for h in current_ihosts]
            if ihost_dict['hostname'] in host_names:
                raise wsme.exc.ClientSideError(
                    _("Host-add Rejected: Hostname already exists"))
            host_ips = [h.mgmt_ip for h in current_ihosts]
            if (ihost_dict.get('mgmt_ip') and
                    ihost_dict['mgmt_ip'] in host_ips):
                raise wsme.exc.ClientSideError(
                    _("Host-add Rejected: Host with mgmt_ip %s already "
                      "exists") % ihost_dict['mgmt_ip'])

        try:
            ihost_obj = objects.Host.get_by_filters_one(
                pecan.request.context,
                {'mgmt_mac': ihost_dict['mgmt_mac']})
            # A host with this MAC already exists. We will allow it to be
            # added if the hostname and personality have not been set.
            if ihost_obj['hostname'] or ihost_obj['personality']:
                raise wsme.exc.ClientSideError(
                    _("Host-add Rejected: Host with mgmt_mac {} already "
                      "exists").format(ihost_dict['mgmt_mac']))
            # Check DNSMASQ for ip/mac already existing
            # -> node in use by someone else or has already been booted
            elif (not ihost_obj and self._dnsmasq_mac_exists(
                    ihost_dict['mgmt_mac'])):
                raise wsme.exc.ClientSideError(
                    _("Host-add Rejected: mgmt_mac {} has already been "
                      "active").format(ihost_dict['mgmt_mac']))

            # Use the uuid from the existing host
            ihost_dict['uuid'] = ihost_obj['uuid']
        except exception.HostNotFound:
            ihost_dict['mgmt_mac'] = cutils.validate_and_normalize_mac(
                ihost_dict['mgmt_mac'])
            # This is a new host
            pass

        if not ihost_dict.get('uuid'):
            ihost_dict['uuid'] = uuidutils.generate_uuid()

        # BM handling
        ihost_orig = copy.deepcopy(ihost_dict)

        subfunctions = self._update_subfunctions(ihost_dict)
        ihost_dict['subfunctions'] = subfunctions

        changed_paths = []
        delta = set()

        for key in objects.Host.fields:
            # Internal values that aren't being modified
            if key in ['id', 'updated_at', 'created_at']:
                continue

            # Update only the new fields
            if key in ihost_dict and ihost_dict[key] != ihost_orig[key]:
                delta.add(key)
                ihost_orig[key] = ihost_dict[key]

        bm_list = ['bm_type', 'bm_ip', 'bm_username', 'bm_password']
        for bmi in bm_list:
            if bmi in ihost_dict:
                delta.add(bmi)
                changed_paths.append({'path': '/' + str(bmi),
                                      'value': ihost_dict[bmi],
                                      'op': 'replace'})

        self._bm_semantic_check_and_update(ihost_orig, ihost_dict,
                                           delta, changed_paths,
                                           current_ihosts)

        if not ihost_dict.get('capabilities', {}):
            ihost_dict['capabilities'] = {}

        # If this is the first controller being set up,
        # configure and return
        if ihost_dict['personality'] == k_host.CONTROLLER:
            if not self._get_controllers():
                pecan.request.rpcapi.create_controller_filesystems(
                    pecan.request.context, ihost_dict['rootfs_device'])
                controller_ihost = pecan.request.rpcapi.create_host(
                    pecan.request.context, ihost_dict)
                pecan.request.rpcapi.configure_host(
                    pecan.request.context,
                    controller_ihost)
                return Host.convert_with_links(controller_ihost)

        if ihost_dict['personality'] in (
                k_host.CONTROLLER, k_host.STORAGE):
            self._controller_storage_node_setup(ihost_dict)

        # Validate that management name and IP do not already exist
        # If one exists, other value must match in addresses table
        mgmt_address_name = cutils.format_address_name(
            ihost_dict['hostname'], constants.NETWORK_TYPE_MGMT)
        self._validate_address_not_allocated(mgmt_address_name,
                                             ihost_dict.get('mgmt_ip'))

        if ihost_dict.get('mgmt_ip'):
            self._validate_ip_in_mgmt_network(ihost_dict['mgmt_ip'])
        else:
            del ihost_dict['mgmt_ip']

        # Set host to reinstalling
        ihost_dict.update({k_host.HOST_ACTION_STATE:
                           k_host.HAS_REINSTALLING})

        # Creation/Configuration
        if ihost_obj:
            # The host exists - do an update.
            for key in objects.Host.fields:
                # Internal values that shouldn't be updated
                if key in ['id', 'uuid', 'updated_at', 'created_at']:
                    continue

                # Update only the fields that are not empty and have changed
                if (key in ihost_dict and ihost_dict[key] and
                        (ihost_obj[key] != ihost_dict[key])):
                    ihost_obj[key] = ihost_dict[key]
            ihost_obj = pecan.request.rpcapi.update_host(
                pecan.request.context, ihost_obj)
        else:
            # The host doesn't exist - do an add.
            LOG.info("create_host=%s" % ihost_dict.get('hostname'))
            ihost_obj = pecan.request.rpcapi.create_host(
                pecan.request.context, ihost_dict)

        ihost_obj = objects.Host.get_by_uuid(pecan.request.context,
                                             ihost_obj.uuid)

        # mgmt_network = pecan.request.systemconfig.network_get_by_type(
        #     constants.NETWORK_TYPE_MGMT)

        # Configure the new ihost, gets info about its addresses
        host = pecan.request.rpcapi.configure_host(
            pecan.request.context,
            ihost_obj)

        if not host:
            raise wsme.exc.ClientSideError(
                _("Host-add Rejected: Host configure {} rejected ").format(
                    ihost_obj.hostname))

        # Add host to mtc
        ihost_obj['mgmt_ip'] = host.get('mgmt_ip')
        new_ihost_mtc = ihost_obj.as_dict()
        new_ihost_mtc.update({'operation': 'add'})
        new_ihost_mtc = cutils.removekeys_nonmtce(new_ihost_mtc)
        # new_ihost_mtc.update(
        #     {'infra_ip': self._get_infra_ip_by_ihost(ihost_obj['uuid'])})

        mtce_response = mtce_api.host_add(
            self._api_token,
            self._mtc_address,
            self._mtc_port,
            new_ihost_mtc,
            constants.MTC_ADD_TIMEOUT_IN_SECS)

        self._handle_mtce_response('host_add', mtce_response)

        # once the host is added to mtc, attempt to power it on if requested
        if power_on is not None and ihost_obj['bm_type'] is not None:
            new_ihost_mtc.update({'action': k_host.ACTION_POWERON})

            mtce_response = mtce_api.host_modify(
                self._api_token,
                self._mtc_address,
                self._mtc_port,
                new_ihost_mtc,
                constants.MTC_ADD_TIMEOUT_IN_SECS)

            self._handle_mtce_response('power_on', mtce_response)

        # Notify the VIM that the host has been added - must be done after
        # the host has been added to mtc and saved to the DB.
        LOG.info("VIM notify add host add %s subfunctions={}").format((
            ihost_obj['hostname'], subfunctions))
        try:
            self._vim_host_add(ihost_obj)
        except Exception as e:
            LOG.warn(_("No response from vim_api {} e={}").format(
                ihost_obj['hostname'], e))
            self._api_token = None
            pass  # VIM audit will pickup

        log_end = cutils.timestamped("ihost_post_end")
        LOG.info("SYS_I host %s %s" % (ihost_obj.hostname, log_end))

        return Host.convert_with_links(ihost_obj)

    @cutils.synchronized(LOCK_NAME)
    @expose('json')
    def bulk_add(self):
        pending_creation = []
        success_str = ""
        error_str = ""

        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            return dict(
                success="",
                error="Bulk add on a simplex system is not allowed."
            )

        # Semantic Check: Prevent bulk add until there is an unlocked
        #                 and enabled controller to manage them.
        controller_list = objects.Host.list(
            pecan.request.context,
            filters={'personality': k_host.CONTROLLER})

        have_unlocked_enabled_controller = False
        for c in controller_list:
            if (c['administrative'] == k_host.ADMIN_UNLOCKED and
                    c['operational'] == k_host.OPERATIONAL_ENABLED):
                have_unlocked_enabled_controller = True
                break

        if not have_unlocked_enabled_controller:
            return dict(
                success="",
                error="Bulk_add requires enabled controller. "
                      "Please unlock controller-0, wait for it to enable "
                      "and then retry."
            )

        LOG.info("Starting ihost bulk_add operation")
        assert isinstance(pecan.request.POST['file'], cgi.FieldStorage)
        fileitem = pecan.request.POST['file']
        if not fileitem.filename:
            return dict(success="", error="Error: No file uploaded")

        try:
            contents = fileitem.file.read()
            # Generate an array of hosts' attributes to be used in creation
            root = ET.fromstring(contents)
        except Exception:
            return dict(
                success="",
                error="No hosts have been added, invalid XML document"
            )

        for idx, xmlhost in enumerate(root.findall('host')):

            new_ihost = {}
            for attr in HOST_XML_ATTRIBUTES:
                elem = xmlhost.find(attr)
                if elem is not None:
                    # If the element is found, set the attribute.
                    # If the text field is empty, set it to the empty string.
                    new_ihost[attr] = elem.text or ""
                else:
                    # If the element is not found, set the attribute to None.
                    new_ihost[attr] = None

            # This is the expected format of the location field
            if new_ihost['location'] is not None:
                new_ihost['location'] = {"locn": new_ihost['location']}

            # Semantic checks
            try:
                LOG.debug(new_ihost)
                self._new_host_semantic_checks(new_ihost)
            except Exception as ex:
                culprit = new_ihost.get('hostname') or "with index " + str(idx)
                return dict(
                    success="",
                    error=" No hosts have been added, error parsing host %s: "
                          "%s" % (culprit, ex)
                )
            pending_creation.append(new_ihost)

        # Find local network adapter MACs
        my_macs = list()
        for liSnics in psutil.net_if_addrs().values():
            for snic in liSnics:
                if snic.family == psutil.AF_LINK:
                    my_macs.append(snic.address)

        # Perform the actual creations
        for new_host in pending_creation:
            try:
                # Configuring for the setup controller, only uses BMC fields
                if new_host['mgmt_mac'].lower() in my_macs:
                    changed_paths = list()

                    bm_list = ['bm_type', 'bm_ip',
                               'bm_username', 'bm_password']
                    for bmi in bm_list:
                        if bmi in new_host:
                            changed_paths.append({
                                'path': '/' + str(bmi),
                                'value': new_host[bmi],
                                'op': 'replace'
                            })

                    ihost_obj = [ihost for ihost in
                                 objects.Host.list(pecan.request.context)
                                 if ihost['mgmt_mac'] in my_macs]
                    if len(ihost_obj) != 1:
                        raise Exception(
                            "Unexpected: no/more_than_one host(s) contain(s) "
                            "a management mac address from "
                            "local network adapters")
                    self._patch(ihost_obj[0]['uuid'],
                                changed_paths, None)
                else:
                    self._do_post(new_host)

                if (new_host['power_on'] is not None and
                        new_host['bm_type'] is None):
                    success_str = (
                        "%s\n %s Warning: Ignoring <power_on> due to "
                        "insufficient board management (bm) data." %
                        (success_str, new_host['hostname']))
                else:
                    success_str = "%s\n %s" % (success_str,
                                               new_host['hostname'])
            except Exception as ex:
                LOG.exception(ex)
                error_str += " " + (new_host.get('hostname') or
                                    new_host.get('personality')) + \
                             ": " + str(ex) + "\n"

        return dict(
            success=success_str,
            error=error_str
        )

    @expose('json')
    def bulk_export(self):
        def host_personality_name_sort_key(host_obj):
            if host_obj.personality == k_host.CONTROLLER:
                rank = 0
            elif host_obj.personality == k_host.STORAGE:
                rank = 1
            elif host_obj.personality == k_host.COMPUTE:
                rank = 2
            else:
                rank = 3
            return rank, host_obj.hostname

        xml_host_node = et.Element('hosts',
                                   {'version': cutils.get_sw_version()})
        mgmt_network = pecan.request.systemconfig.network_get_by_type(
            constants.NETWORK_TYPE_MGMT)

        host_list = objects.Host.list(pecan.request.context)
        sorted_hosts = sorted(host_list, key=host_personality_name_sort_key)

        for host in sorted_hosts:
            _create_node(host, xml_host_node, host.personality,
                         mgmt_network.dynamic)

        xml_text = dom.parseString(et.tostring(xml_host_node)).toprettyxml()
        result = {'content': xml_text}
        return result

    @cutils.synchronized(LOCK_NAME)
    @wsme_pecan.wsexpose(Host, body=Host)
    def post(self, host):
        """Create a new ihost."""
        ihost_dict = host.as_dict()

        # bm_password is not a part of ihost, so retrieve it from the body
        body = json.loads(pecan.request.body)
        if 'bm_password' in body:
            ihost_dict['bm_password'] = body['bm_password']
        else:
            ihost_dict['bm_password'] = ''

        return self._do_post(ihost_dict)

    @wsme_pecan.wsexpose(Host, unicode, body=[unicode])
    def patch(self, uuid, patch):
        """Update an existing ihost.
        """
        utils.validate_patch(patch)

        optimizable = 0
        optimize_list = ['/uptime', '/location', '/serialid', '/task']
        for p in patch:
            path = p['path']
            if path in optimize_list:
                optimizable += 1

        if len(patch) == optimizable:
            return self._patch(uuid, patch)
        elif (pecan.request.user_agent.startswith('mtce') or
                pecan.request.user_agent.startswith('vim')):
            return self._patch_sys(uuid, patch)
        else:
            return self._patch_gen(uuid, patch)

    @cutils.synchronized(LOCK_NAME_SYS)
    def _patch_sys(self, uuid, patch):
        return self._patch(uuid, patch)

    @cutils.synchronized(LOCK_NAME)
    def _patch_gen(self, uuid, patch):
        return self._patch(uuid, patch)

    @staticmethod
    def _validate_capability_is_not_set(old, new):
        is_set, _ = new
        return not is_set

    @staticmethod
    def _validate_capability_is_equal(old, new):
        return old == new

    def _validate_capabilities(self, old_caps, new_caps):
        """Reject updating read-only host capabilities:
            1. stor_function. This field is set to 'monitor' for hosts that are
               running ceph monitor process:
               controller-0, controller-1, storage-0.
            2. Personality. This field is "virtual":
               not saved in the database but
               returned via API and displayed via "system host-show".

            :param old_caps: current host capabilities
            :type old_caps: dict
            :param new_caps: updated host capabilies (to  be set)
            :type new_caps: str
            :raises: wsme.exc.ClientSideError when attempting to
                     change read-only capabilities
        """
        if type(new_caps) == str:
            try:
                new_caps = ast.literal_eval(new_caps)
            except SyntaxError:
                pass
        if type(new_caps) != dict:
            raise wsme.exc.ClientSideError(
                _("Changing capabilities type is not allowed: "
                  "old_value={}, new_value={}").format(
                    old_caps, new_caps))
        PROTECTED_CAPABILITIES = [
            ('Personality',
             self._validate_capability_is_not_set),
            (k_host.HOST_STOR_FUNCTION,
             self._validate_capability_is_equal)]
        for capability, validate in PROTECTED_CAPABILITIES:
            old_is_set, old_value = (
                capability in old_caps, old_caps.get(capability))
            new_is_set, new_value = (
                capability in new_caps, new_caps.get(capability))
            if not validate((old_is_set, old_value),
                            (new_is_set, new_value)):
                if old_is_set:
                    raise wsme.exc.ClientSideError(
                        _("Changing capability not allowed: "
                          "name={}, old_value={}, new_value={}. ").format(
                            capability, old_value, new_value))
                else:
                    raise wsme.exc.ClientSideError(
                        _("Setting capability not allowed: "
                          "name={}, value={}. ").format(
                            capability, new_value))

    def _patch(self, uuid, patch):
        log_start = cutils.timestamped("host_patch_start")

        patch_obj = jsonpatch.JsonPatch(patch)

        ihost_obj = objects.Host.get_by_uuid(pecan.request.context, uuid)
        ihost_dict = ihost_obj.as_dict()

        self._add_host_semantic_checks(ihost_dict)

        # Add transient fields that are not stored in the database
        ihost_dict['bm_password'] = None

        try:
            patched_ihost = jsonpatch.apply_patch(ihost_dict,
                                                  patch_obj)
        except jsonpatch.JsonPatchException as e:
            LOG.exception(e)
            raise wsme.exc.ClientSideError(_("Patching Error: %s") % e)

        self._validate_capabilities(
            ihost_dict['capabilities'], patched_ihost['capabilities'])

        ihost_dict_orig = dict(ihost_obj.as_dict())
        # defaults = objects.Host.get_defaults()
        for key in objects.Host.fields:
            # Internal values that shouldn't be part of the patch
            if key in ['id', 'updated_at', 'created_at', 'infra_ip']:
                continue

            # In case of a remove operation, add the missing fields back
            # to the document with their default value
            if key in ihost_dict and key not in patched_ihost:
                # patched_ihost[key] = defaults[key]
                patched_ihost[key] = ihost_obj[key]

            # Update only the fields that have changed
            if ihost_obj[key] != patched_ihost[key]:
                ihost_obj[key] = patched_ihost[key]

        delta = ihost_obj.obj_what_changed()
        delta_handle = list(delta)

        uptime_update = False
        if 'uptime' in delta_handle:
            # There is a log of uptime updates, so just do a debug log
            uptime_update = True
            LOG.debug("%s %s patch" % (ihost_obj.hostname,
                                       log_start))
        else:
            LOG.info("%s %s patch" % (ihost_obj.hostname,
                                      log_start))

        hostupdate = HostUpdate(ihost_dict_orig, patched_ihost, delta)
        if delta_handle:
            self._validate_delta(delta_handle)
            if delta_handle == ['uptime']:
                LOG.debug("%s 1. delta_handle %s" %
                          (hostupdate.displayid, delta_handle))
            else:
                LOG.info("%s 1. delta_handle %s" %
                         (hostupdate.displayid, delta_handle))
        else:
            LOG.info("%s ihost_patch_end.  No changes from %s." %
                     (hostupdate.displayid, pecan.request.user_agent))
            return Host.convert_with_links(ihost_obj)

        myaction = patched_ihost.get('action')
        if self.action_check(myaction, hostupdate):
            LOG.info("%s post action_check hostupdate "
                     "action=%s notify_vim=%s notify_mtc=%s "
                     "skip_notify_mtce=%s" %
                     (hostupdate.displayid,
                      hostupdate.action,
                      hostupdate.notify_vim,
                      hostupdate.notify_mtce,
                      hostupdate.skip_notify_mtce))

            if self.stage_action(myaction, hostupdate):
                LOG.info("%s Action staged: %s" %
                         (hostupdate.displayid, myaction))
            else:
                LOG.info("%s ihost_patch_end stage_action rc %s" %
                         (hostupdate.displayid, hostupdate.nextstep))
                if hostupdate.nextstep == hostupdate.EXIT_RETURN_HOST:
                    return Host.convert_with_links(ihost_obj)
                elif hostupdate.nextstep == hostupdate.EXIT_UPDATE_PREVAL:
                    if hostupdate.ihost_val_prenotify:
                        # update value in db  prior to notifications
                        LOG.info("update ihost_val_prenotify: %s" %
                                 hostupdate.ihost_val_prenotify)
                        ihost_obj = pecan.request.dbapi.host_update(
                            ihost_obj['uuid'], hostupdate.ihost_val_prenotify)
                    return Host.convert_with_links(ihost_obj)

            if myaction == k_host.ACTION_SUBFUNCTION_CONFIG:
                self.perform_action_subfunction_config(ihost_obj)

            if myaction in delta_handle:
                delta_handle.remove(myaction)

            LOG.info("%s post action_stage hostupdate "
                     "action=%s notify_vim=%s notify_mtc=%s "
                     "skip_notify_mtce=%s" %
                     (hostupdate.displayid,
                      hostupdate.action,
                      hostupdate.notify_vim,
                      hostupdate.notify_mtce,
                      hostupdate.skip_notify_mtce))

        self._optimize_delta_handling(delta_handle)

        if 'administrative' in delta or 'operational' in delta:
            self.stage_administrative_update(hostupdate)

        if delta_handle:
            LOG.info("%s 2. delta_handle %s" %
                     (hostupdate.displayid, delta_handle))
            self._check_provisioning(hostupdate, patch)
            if (hostupdate.ihost_orig['administrative'] ==
                    k_host.ADMIN_UNLOCKED):
                self.check_updates_while_unlocked(hostupdate, delta)

            current_ihosts = None
            hostupdate.bm_type_changed_to_none = \
                self._bm_semantic_check_and_update(hostupdate.ihost_orig,
                                                   hostupdate.ihost_patch,
                                                   delta, patch_obj,
                                                   current_ihosts,
                                                   hostupdate)
            LOG.info("%s post delta_handle hostupdate "
                     "action=%s notify_vim=%s notify_mtc=%s "
                     "skip_notify_mtce=%s" %
                     (hostupdate.displayid,
                      hostupdate.action,
                      hostupdate.notify_vim,
                      hostupdate.notify_mtce,
                      hostupdate.skip_notify_mtce))

            if hostupdate.bm_type_changed_to_none:
                hostupdate.ihost_val_update({'bm_ip': None,
                                             'bm_username': None,
                                             'bm_password': None})

        if hostupdate.ihost_val_prenotify:
            # update value in db  prior to notifications
            LOG.info("update ihost_val_prenotify: %s" %
                     hostupdate.ihost_val_prenotify)
            pecan.request.dbapi.host_update(ihost_obj['uuid'],
                                            hostupdate.ihost_val_prenotify)

        if hostupdate.ihost_val:
            # apply the staged updates in preparation for update
            LOG.info("%s apply ihost_val %s" %
                     (hostupdate.displayid, hostupdate.ihost_val))
            for k, v in hostupdate.ihost_val.iteritems():
                ihost_obj[k] = v
            LOG.debug("AFTER Apply ihost_val %s to  iHost %s" %
                      (hostupdate.ihost_val, ihost_obj.as_dict()))

        if 'personality' in delta:
            self._update_subfunctions(ihost_obj)

        if hostupdate.notify_vim:
            action = hostupdate.action
            LOG.info("Notify VIM host action %s action=%s" % (
                ihost_obj['hostname'], action))
            try:
                vim_api.vim_host_action(
                    pecan.request.context,
                    ihost_obj['uuid'],
                    ihost_obj['hostname'],
                    action,
                    constants.VIM_DEFAULT_TIMEOUT_IN_SECS)
            except Exception as e:
                LOG.warn(_("No response vim_api {} on action={} e={}").format(
                    ihost_obj['hostname'], action, e))
                self._api_token = None
                if action == k_host.ACTION_FORCE_LOCK:
                    pass
                else:
                    # reject continuation if VIM rejects action
                    raise wsme.exc.ClientSideError(_(
                        "VIM API Error or Timeout on action = %s "
                        "Please retry and if problem persists then "
                        "contact your system administrator.") % action)

        if hostupdate.configure_required:
            LOG.info("%s Perform configure_host." % hostupdate.displayid)
            if not ((ihost_obj['hostname']) and (ihost_obj['personality'])):
                raise wsme.exc.ClientSideError(
                    _("Please provision 'hostname' and 'personality'."))

            ihost_ret = pecan.request.rpcapi.configure_host(
                pecan.request.context, ihost_obj)

            pecan.request.dbapi.host_update(
                ihost_obj['uuid'],
                {'capabilities': ihost_obj['capabilities']})

            # Notify maintenance about updated mgmt_ip
            ihost_obj['mgmt_ip'] = ihost_ret.get('mgmt_ip')

            hostupdate.notify_mtce = True

        pecan.request.dbapi.host_update(
            ihost_obj['uuid'],
            {'capabilities': ihost_obj['capabilities']})

        if (k_host.TASK_REINSTALLING == ihost_obj.task and
                k_host.CONFIG_STATUS_REINSTALL == ihost_obj.config_status):
            # Clear reinstall flag when reinstall starts
            ihost_obj.config_status = None

        mtce_response = {'status': None}
        nonmtc_change_count = 0
        if hostupdate.notify_mtce and not hostupdate.skip_notify_mtce:
            nonmtc_change_count = self.check_notify_mtce(myaction, hostupdate)
            if nonmtc_change_count > 0:
                LOG.info("%s Action %s perform notify_mtce" %
                         (hostupdate.displayid, myaction))
                new_ihost_mtc = ihost_obj.as_dict()
                new_ihost_mtc = cutils.removekeys_nonmtce(new_ihost_mtc)

                if hostupdate.ihost_orig['invprovision'] == \
                        k_host.PROVISIONED:
                    new_ihost_mtc.update({'operation': 'modify'})
                else:
                    new_ihost_mtc.update({'operation': 'add'})
                new_ihost_mtc.update({"invprovision":
                                      ihost_obj['invprovision']})

                if hostupdate.notify_action_lock:
                    new_ihost_mtc['action'] = k_host.ACTION_LOCK
                elif hostupdate.notify_action_lock_force:
                    new_ihost_mtc['action'] = k_host.ACTION_FORCE_LOCK
                elif myaction == k_host.ACTION_FORCE_UNLOCK:
                    new_ihost_mtc['action'] = k_host.ACTION_UNLOCK

                new_ihost_mtc.update({
                    'infra_ip': self._get_infra_ip_by_ihost(ihost_obj['uuid'])
                })

                if new_ihost_mtc['operation'] == 'add':
                    mtce_response = mtce_api.host_add(
                        self._api_token, self._mtc_address, self._mtc_port,
                        new_ihost_mtc,
                        constants.MTC_DEFAULT_TIMEOUT_IN_SECS)
                elif new_ihost_mtc['operation'] == 'modify':
                    mtce_response = mtce_api.host_modify(
                        self._api_token, self._mtc_address, self._mtc_port,
                        new_ihost_mtc,
                        constants.MTC_DEFAULT_TIMEOUT_IN_SECS,
                        3)
                else:
                    LOG.warn("Unsupported Operation: %s" % new_ihost_mtc)
                    mtce_response = None

                if mtce_response is None:
                    mtce_response = {'status': 'fail',
                                     'reason': 'no response',
                                     'action': 'retry'}

        ihost_obj['action'] = k_host.ACTION_NONE
        hostupdate.ihost_val_update({'action': k_host.ACTION_NONE})

        if ((mtce_response['status'] == 'pass') or
                (nonmtc_change_count == 0) or hostupdate.skip_notify_mtce):

            ihost_obj.save()

            if hostupdate.ihost_patch['operational'] == \
                    k_host.OPERATIONAL_ENABLED:
                self._update_add_ceph_state()

            if hostupdate.notify_availability:
                if (hostupdate.notify_availability ==
                        k_host.VIM_SERVICES_DISABLED):
                    imsg_dict = {'availability':
                                 k_host.AVAILABILITY_OFFLINE}
                else:
                    imsg_dict = {'availability':
                                 k_host.VIM_SERVICES_ENABLED}
                    if (hostupdate.notify_availability !=
                            k_host.VIM_SERVICES_ENABLED):
                        LOG.error(
                            _("Unexpected notify_availability={}").format(
                                hostupdate.notify_availability))

                LOG.info(_("{} notify_availability={}").format(
                    hostupdate.displayid,
                    hostupdate.notify_availability))

                pecan.request.rpcapi.platform_update_by_host(
                    pecan.request.context, ihost_obj['uuid'], imsg_dict)

            if hostupdate.bm_type_changed_to_none:
                ibm_msg_dict = {}
                pecan.request.rpcapi.bm_deprovision_by_host(
                    pecan.request.context,
                    ihost_obj['uuid'],
                    ibm_msg_dict)

        elif mtce_response['status'] is None:
            raise wsme.exc.ClientSideError(
                _("Timeout waiting for maintenance response. "
                  "Please retry and if problem persists then "
                  "contact your system administrator."))
        else:
            if hostupdate.configure_required:
                # rollback to unconfigure host as mtce has failed the request
                invprovision_state = hostupdate.ihost_orig.get(
                    'invprovision') or ""
                if invprovision_state != k_host.PROVISIONED:
                    LOG.warn("unconfigure ihost %s provision=%s" %
                             (ihost_obj.uuid, invprovision_state))
                    pecan.request.rpcapi.unconfigure_host(
                        pecan.request.context,
                        ihost_obj)

            raise wsme.exc.ClientSideError(
                _("Operation Rejected: {}.{}.").format(
                    mtce_response['reason'],
                    mtce_response['action']))

        if hostupdate.notify_vim_add_host:
            # Notify the VIM that the host has been added - must be done after
            # the host has been added to mtc and saved to the DB.
            LOG.info("inventory notify add host add %s subfunctions=%s" %
                     (ihost_obj['hostname'], ihost_obj['subfunctions']))
            try:
                self._vim_host_add(ihost_obj)
            except Exception as e:
                LOG.warn(_("No response from vim_api {} e={}").format(
                    ihost_obj['hostname'], e))
                self._api_token = None
                pass  # VIM audit will pickup

        # check if ttys_dcd is updated and notify the agent via conductor
        # if necessary
        if 'ttys_dcd' in hostupdate.delta:
            self._handle_ttys_dcd_change(hostupdate.ihost_orig,
                                         hostupdate.ihost_patch['ttys_dcd'])

        log_end = cutils.timestamped("host_patch_end")
        if uptime_update:
            LOG.debug("host %s %s patch" % (ihost_obj.hostname,
                                            log_end))
        else:
            LOG.info("host %s %s patch" % (ihost_obj.hostname,
                                           log_end))

        if ('administrative' in hostupdate.delta and
                hostupdate.ihost_patch['administrative'] ==
                k_host.ADMIN_LOCKED):
            LOG.info("Update host memory for (%s)" % ihost_obj['hostname'])
            pecan.request.rpcapi.update_host_memory(pecan.request.context,
                                                    ihost_obj['uuid'])
        return Host.convert_with_links(ihost_obj)

    def _vim_host_add(self, ihost):
        LOG.info("inventory notify vim add host %s personality=%s" % (
            ihost['hostname'], ihost['personality']))

        subfunctions = self._update_subfunctions(ihost)
        try:
            vim_api.vim_host_add(
                pecan.request.context,
                ihost['uuid'],
                ihost['hostname'],
                subfunctions,
                ihost['administrative'],
                ihost['operational'],
                ihost['availability'],
                ihost['subfunction_oper'],
                ihost['subfunction_avail'])
        except Exception as e:
            LOG.warn(_("No response from vim_api {} e={}").format(
                     (ihost['hostname'], e)))
            self._api_token = None
            pass  # VIM audit will pickup

    @cutils.synchronized(LOCK_NAME)
    @wsme_pecan.wsexpose(None, unicode, status_code=204)
    def delete(self, host_id):
        """Delete a host.
        """

        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            raise wsme.exc.ClientSideError(_(
                "Deleting a host on a simplex system is not allowed."))

        ihost = objects.Host.get_by_uuid(pecan.request.context,
                                         host_id)

        if ihost.administrative == k_host.ADMIN_UNLOCKED:
            if not ihost.hostname:
                host = ihost.uuid
            else:
                host = ihost.hostname

            raise exception.HostLocked(
                action=k_host.ACTION_DELETE, host=host)

        personality = ihost.personality
        # allow delete of unprovisioned locked disabled & offline storage hosts
        skip_ceph_checks = (
            (not ihost.invprovision or
             ihost.invprovision == k_host.UNPROVISIONED) and
            ihost.administrative == k_host.ADMIN_LOCKED and
            ihost.operational == k_host.OPERATIONAL_DISABLED and
            ihost.availability == k_host.AVAILABILITY_OFFLINE)

        if (personality is not None and
                personality.find(k_host.STORAGE_HOSTNAME) != -1 and
                not skip_ceph_checks):
            # perform self.sc_op.check_delete; send to systemconfig
            # to check monitors
            LOG.info("TODO storage check with systemconfig for quoroum, "
                     "delete storage pools and tiers")
            hosts = objects.Host.list(pecan.request.context)
            num_monitors, required_monitors = \
                self._ceph.get_monitors_status(hosts)
            if num_monitors < required_monitors:
                raise wsme.exc.ClientSideError(
                    _("Only %d storage "
                      "monitor available. At least {} unlocked and "
                      "enabled hosts with monitors are required. Please "
                      "ensure hosts with monitors are unlocked and "
                      "enabled - candidates: {}, {}, {}").format(
                        (num_monitors, constants.MIN_STOR_MONITORS,
                         k_host.CONTROLLER_0_HOSTNAME,
                         k_host.CONTROLLER_1_HOSTNAME,
                         k_host.STORAGE_0_HOSTNAME)))
            # send to systemconfig to delete storage pools and tiers

        LOG.warn("REST API delete host=%s user_agent=%s" %
                 (ihost['uuid'], pecan.request.user_agent))
        if not pecan.request.user_agent.startswith('vim'):
            try:
                vim_api.vim_host_delete(
                    pecan.request.context,
                    ihost.uuid,
                    ihost.hostname)
            except Exception:
                LOG.warn(_("No response from vim_api {} ").format(
                    ihost['uuid']))
                raise wsme.exc.ClientSideError(
                    _("System rejected delete request.  "
                      "Please retry and if problem persists then "
                      "contact your system administrator."))

            if (ihost.hostname and ihost.personality and
                    ihost.invprovision and
                    ihost.invprovision == k_host.PROVISIONED and
                    (k_host.COMPUTE in ihost.subfunctions)):
                # wait for VIM signal
                return

        idict = {'operation': k_host.ACTION_DELETE,
                 'uuid': ihost.uuid,
                 'invprovision': ihost.invprovision}

        mtce_response = mtce_api.host_delete(
            self._api_token, self._mtc_address, self._mtc_port,
            idict, constants.MTC_DELETE_TIMEOUT_IN_SECS)

        # Check mtce response prior to attempting delete
        if mtce_response.get('status') != 'pass':
            self._vim_host_add(ihost)
            self._handle_mtce_response(k_host.ACTION_DELETE,
                                       mtce_response)

        pecan.request.rpcapi.unconfigure_host(pecan.request.context,
                                              ihost)

        # Delete the stor entries associated with this host
        # Notify sysinv of host-delete
        LOG.info("notify systemconfig of host-delete which will"
                 "also do stors, lvgs, pvs, ceph crush remove")

        # tell conductor to delete the keystore entry associated
        # with this host (if present)
        try:
            pecan.request.rpcapi.unconfigure_keystore_account(
                pecan.request.context,
                KEYRING_BM_SERVICE,
                ihost.uuid)
        except exception.NotFound:
            pass

        # Notify patching to drop the host
        if ihost.hostname is not None:
            try:
                system = objects.System.get_one(pecan.request.context)
                patch_api.patch_drop_host(
                    pecan.request.context,
                    hostname=ihost.hostname,
                    region_name=system.region_name)
            except Exception as e:
                LOG.warn(_("No response from drop-host patch api {}"
                           "e={}").format(ihost.hostname, e))
                pass

        pecan.request.dbapi.host_destroy(host_id)

    @staticmethod
    def _handle_mtce_response(action, mtce_response):
        LOG.info("mtce action %s response: %s" %
                 (action, mtce_response))
        if mtce_response is None:
            mtce_response = {'status': 'fail',
                             'reason': 'no response',
                             'action': 'retry'}

        if mtce_response.get('reason') != 'no response':
            raise wsme.exc.ClientSideError(_(
                "Mtce rejected %s request."
                "Please retry and if problem persists then contact your "
                "system administrator.") % action)
        else:
            raise wsme.exc.ClientSideError(_(
                "Timeout waiting for system response to %s. Please wait for a "
                "few moments. If the host is not deleted,please retry. If "
                "problem persists then contact your system administrator.") %
                action)

    @staticmethod
    def _get_infra_ip_by_ihost(ihost_uuid):
        try:
            # Get the list of interfaces for this ihost
            iinterfaces = pecan.request.dbapi.iinterface_get_by_ihost(
                ihost_uuid)
            # Make a list of only the infra interfaces
            infra_interfaces = [
                i for i in iinterfaces
                if i['networktype'] == constants.NETWORK_TYPE_INFRA]
            # Get the UUID of the infra interface (there is only one)
            infra_interface_uuid = infra_interfaces[0]['uuid']
            # Return the first address for this interface (there is only one)
            return pecan.request.dbapi.addresses_get_by_interface(
                infra_interface_uuid)[0]['address']
        except Exception as ex:
            LOG.debug("Could not find infra ip for host %s: %s" % (
                ihost_uuid, ex))
            return None

    @staticmethod
    def _validate_ip_in_mgmt_network(ip):
        network = pecan.request.systemconfig.network_get_by_type(
            constants.NETWORK_TYPE_MGMT)
        utils.validate_address_within_nework(ip, network)

    @staticmethod
    def _validate_address_not_allocated(name, ip_address):
        """Validate that address isn't allocated

        :param name: Address name to check isn't allocated.
        :param ip_address: IP address to check isn't allocated.
        """
        # When a host is added by systemconfig, this would already
        # have been checked
        LOG.info("TODO(sc) _validate_address_not_allocated name={} "
                 "ip_address={}".format(name, ip_address))

    @staticmethod
    def _dnsmasq_mac_exists(mac_addr):
        """Check the dnsmasq.hosts file for an existing mac.

        :param mac_addr: mac address to check for.
        """

        dnsmasq_hosts_file = tsc.CONFIG_PATH + 'dnsmasq.hosts'
        with open(dnsmasq_hosts_file, 'r') as f_in:
            for line in f_in:
                if mac_addr in line:
                    return True
        return False

    @staticmethod
    def _get_controllers():
        return objects.Host.list(
            pecan.request.context,
            filters={'personality': k_host.CONTROLLER})

    @staticmethod
    def _validate_delta(delta):
        restricted_updates = ['uuid', 'id', 'created_at', 'updated_at',
                              'cstatus',
                              'mgmt_mac', 'mgmt_ip', 'infra_ip',
                              'invprovision', 'recordtype',
                              'host_action',
                              'action_state']

        if not pecan.request.user_agent.startswith('mtce'):
            # Allow mtc to modify these through inventory-api.
            mtce_only_updates = ['administrative',
                                 'availability',
                                 'operational',
                                 'subfunction_oper',
                                 'subfunction_avail',
                                 'reserved',
                                 'mtce_info',
                                 'task',
                                 'uptime']
            restricted_updates.extend(mtce_only_updates)

        if not pecan.request.user_agent.startswith('vim'):
            vim_only_updates = ['vim_progress_status']
            restricted_updates.extend(vim_only_updates)

        intersection = set.intersection(set(delta), set(restricted_updates))
        if intersection:
            raise wsme.exc.ClientSideError(
                _("Change {} contains restricted {}.").format(
                    delta, intersection))
        else:
            LOG.debug("PASS deltaset=%s restricted_updates %s" %
                      (delta, intersection))

    @staticmethod
    def _valid_storage_hostname(hostname):
        return bool(re.match('^%s-[0-9]+$' % k_host.STORAGE_HOSTNAME,
                             hostname))

    def _validate_hostname(self, hostname, personality):

        if personality and personality == k_host.COMPUTE:
            # Check for invalid hostnames
            err_tl = 'Name restricted to at most 255 characters.'
            err_ic = 'Name may only contain letters, ' \
                     'numbers, underscores, periods and hyphens.'
            myexpression = re.compile("^[\w\.\-]+$")
            if not myexpression.match(hostname):
                raise wsme.exc.ClientSideError(_("Error: {}").format(err_ic))
            if len(hostname) > 255:
                raise wsme.exc.ClientSideError(_("Error: {}").format(err_tl))
            non_compute_hosts = ([k_host.CONTROLLER_0_HOSTNAME,
                                  k_host.CONTROLLER_1_HOSTNAME])
            if (hostname and (hostname in non_compute_hosts) or
                    hostname.startswith(k_host.STORAGE_HOSTNAME)):
                raise wsme.exc.ClientSideError(
                    _("{} Reject attempt to configure "
                      "invalid hostname for personality {}.").format(
                        (hostname, personality)))
        else:
            if personality and personality == k_host.CONTROLLER:
                valid_hostnames = [k_host.CONTROLLER_0_HOSTNAME,
                                   k_host.CONTROLLER_1_HOSTNAME]
                if hostname not in valid_hostnames:
                    raise wsme.exc.ClientSideError(
                        _("Host with personality={} can only have a hostname "
                          "from {}").format(personality, valid_hostnames))
            elif personality and personality == k_host.STORAGE:
                if not self._valid_storage_hostname(hostname):
                    raise wsme.exc.ClientSideError(
                        _("Host with personality={} can only have a hostname "
                          "starting with %s-(number)").format(
                            (personality, k_host.STORAGE_HOSTNAME)))

            else:
                raise wsme.exc.ClientSideError(
                    _("{}: Reject attempt to configure with "
                      "invalid personality={} ").format(
                        (hostname, personality)))

    def _check_compute(self, patched_ihost, hostupdate=None):
        # Check for valid compute node setup
        hostname = patched_ihost.get('hostname') or ""

        if not hostname:
            raise wsme.exc.ClientSideError(
                _("Host {} of personality {}, must be provisioned "
                  "with a hostname.").format(
                    (patched_ihost.get('uuid'),
                     patched_ihost.get('personality'))))

        non_compute_hosts = ([k_host.CONTROLLER_0_HOSTNAME,
                              k_host.CONTROLLER_1_HOSTNAME])
        if (hostname in non_compute_hosts or
                self._valid_storage_hostname(hostname)):
            raise wsme.exc.ClientSideError(
                _("Hostname {} is not allowed for personality 'compute'. "
                  "Please check hostname and personality.").format(hostname))

    def _controller_storage_node_setup(self, patched_ihost, hostupdate=None):
        # Initially set the subfunction of the host to it's personality

        if hostupdate:
            patched_ihost = hostupdate.ihost_patch

        patched_ihost['subfunctions'] = patched_ihost['personality']

        if patched_ihost['personality'] == k_host.CONTROLLER:
            controller_0_exists = False
            controller_1_exists = False
            current_ihosts = objects.Host.list(
                pecan.request.context,
                filters={'personality': k_host.CONTROLLER})

            for h in current_ihosts:
                if h['hostname'] == k_host.CONTROLLER_0_HOSTNAME:
                    controller_0_exists = True
                elif h['hostname'] == k_host.CONTROLLER_1_HOSTNAME:
                    controller_1_exists = True
            if controller_0_exists and controller_1_exists:
                raise wsme.exc.ClientSideError(
                    _("Two controller nodes have already been configured. "
                      "This host can not be configured as a controller."))

            # Look up the IP address to use for this controller and set
            # the hostname.
            if controller_0_exists:
                hostname = k_host.CONTROLLER_1_HOSTNAME
                mgmt_ip = self._get_controller_address(hostname)
                if hostupdate:
                    hostupdate.ihost_val_update({'hostname': hostname,
                                                 'mgmt_ip': mgmt_ip})
                else:
                    patched_ihost['hostname'] = hostname
                    patched_ihost['mgmt_ip'] = mgmt_ip
            elif controller_1_exists:
                hostname = k_host.CONTROLLER_0_HOSTNAME
                mgmt_ip = self._get_controller_address(hostname)
                if hostupdate:
                    hostupdate.ihost_val_update({'hostname': hostname,
                                                 'mgmt_ip': mgmt_ip})
                else:
                    patched_ihost['hostname'] = hostname
                    patched_ihost['mgmt_ip'] = mgmt_ip
            else:
                raise wsme.exc.ClientSideError(
                    _("Attempting to provision a controller when none "
                      "exists. This is impossible."))

            # Subfunctions can be set directly via the config file
            subfunctions = ','.join(tsc.subfunctions)
            if hostupdate:
                hostupdate.ihost_val_update({'subfunctions': subfunctions})
            else:
                patched_ihost['subfunctions'] = subfunctions

        elif patched_ihost['personality'] == k_host.STORAGE:
            # Storage nodes are only allowed if we are configured to use
            # ceph for the cinder backend.
            if not StorageBackendConfig.has_backend_configured(
                    pecan.request.dbapi,
                    constants.CINDER_BACKEND_CEPH
            ):
                raise wsme.exc.ClientSideError(
                    _("Storage nodes can only be configured if storage "
                      "cluster is configured for the cinder backend."))

            current_storage_ihosts = objects.Host.list(
                pecan.request.context,
                filters={'personality': k_host.STORAGE})

            current_storage = []
            for h in current_storage_ihosts:
                if self._valid_storage_hostname(h['hostname']):
                    current_storage.append(h['hostname'])

            max_storage_hostnames = ["storage-%s" % x for x in
                                     range(len(current_storage_ihosts) + 1)]

            # Look up IP address to use storage hostname
            for h in reversed(max_storage_hostnames):
                if h not in current_storage:
                    hostname = h
                    mgmt_ip = self._get_storage_address(hostname)
                    LOG.info("Found new hostname=%s mgmt_ip=%s "
                             "current_storage=%s" %
                             (hostname, mgmt_ip, current_storage))
                    break

            if patched_ihost['hostname']:
                if patched_ihost['hostname'] != hostname:
                    raise wsme.exc.ClientSideError(
                        _("Storage name {} not allowed.  Expected {}. "
                          "Storage nodes can be one of: "
                          "storage-#.").format(
                            (patched_ihost['hostname'], hostname)))

            if hostupdate:
                hostupdate.ihost_val_update({'hostname': hostname,
                                             'mgmt_ip': mgmt_ip})
            else:
                patched_ihost['hostname'] = hostname
                patched_ihost['mgmt_ip'] = mgmt_ip

    @staticmethod
    def _optimize_delta_handling(delta_handle):
        """Optimize specific patch operations.
           Updates delta_handle to identify remaining patch semantics to check.
        """
        optimizable = ['location', 'serialid']
        if pecan.request.user_agent.startswith('mtce'):
            mtc_optimizable = ['operational', 'availability', 'task', 'uptime',
                               'subfunction_oper', 'subfunction_avail']
            optimizable.extend(mtc_optimizable)

        for k in optimizable:
            if k in delta_handle:
                delta_handle.remove(k)

    @staticmethod
    def _semantic_mtc_check_action(hostupdate, action):
        """
        Perform semantic checks with patch action vs current state

        returns:  notify_mtc_check_action
        """
        notify_mtc_check_action = True
        ihost = hostupdate.ihost_orig
        patched_ihost = hostupdate.ihost_patch

        if action in [k_host.VIM_SERVICES_DISABLED,
                      k_host.VIM_SERVICES_DISABLE_FAILED,
                      k_host.VIM_SERVICES_DISABLE_EXTEND,
                      k_host.VIM_SERVICES_ENABLED,
                      k_host.VIM_SERVICES_DELETE_FAILED]:
            # These are not mtce actions
            return notify_mtc_check_action

        LOG.info("%s _semantic_mtc_check_action %s" %
                 (hostupdate.displayid, action))

        # Semantic Check: Auto-Provision: Reset, Reboot or Power-On case
        if ((cutils.host_has_function(ihost, k_host.COMPUTE)) and
                (ihost['administrative'] == k_host.ADMIN_LOCKED) and
                ((patched_ihost['action'] == k_host.ACTION_RESET) or
                 (patched_ihost['action'] == k_host.ACTION_REBOOT) or
                 (patched_ihost['action'] == k_host.ACTION_POWERON) or
                 (patched_ihost['action'] == k_host.ACTION_POWEROFF))):
            notify_mtc_check_action = True

        return notify_mtc_check_action

    @staticmethod
    def _bm_semantic_check_and_update(ohost, phost, delta, patch_obj,
                                      current_ihosts=None, hostupdate=None):
        """Parameters:
           ohost:         object original host
           phost:         mutable dictionary patch host
           delta:         default keys changed
           patch_obj:     all changed paths
           returns bm_type_changed_to_none
        """

        # NOTE: since the bm_mac is still in the DB;
        # this is just to disallow user to modify it.
        if 'bm_mac' in delta:
            raise wsme.exc.ClientSideError(
                _("Patching Error: can't replace non-existent object "
                  "'bm_mac' "))

        bm_type_changed_to_none = False

        bm_set = {'bm_type',
                  'bm_ip',
                  'bm_username',
                  'bm_password'}

        password_exists = any(p['path'] == '/bm_password' for p in patch_obj)
        if not (delta.intersection(bm_set) or password_exists):
            return bm_type_changed_to_none

        if hostupdate:
            hostupdate.notify_mtce = True

        patch_bm_password = None
        for p in patch_obj:
            if p['path'] == '/bm_password':
                patch_bm_password = p['value']

        password_exists = password_exists and patch_bm_password is not None

        bm_type_orig = ohost.get('bm_type') or ""
        bm_type_patch = phost.get('bm_type') or ""
        if bm_type_patch.lower() == 'none':
            bm_type_patch = ''
        if (not bm_type_patch) and (bm_type_orig != bm_type_patch):
            LOG.info("bm_type None from %s to %s." %
                     (ohost['bm_type'], phost['bm_type']))

            bm_type_changed_to_none = True

        if 'bm_ip' in delta:
            obm_ip = ohost['bm_ip'] or ""
            nbm_ip = phost['bm_ip'] or ""
            LOG.info("bm_ip in delta=%s obm_ip=%s nbm_ip=%s" %
                     (delta, obm_ip, nbm_ip))
            if obm_ip != nbm_ip:
                if (pecan.request.user_agent.startswith('mtce') and
                        not bm_type_changed_to_none):
                    raise wsme.exc.ClientSideError(
                        _("Rejected: {} Board Management "
                          "controller IP Address is not"
                          "user-modifiable.").format(phost['hostname']))

        if phost['bm_ip'] or phost['bm_type'] or phost['bm_username']:
            if (not phost['bm_type'] or
                    (phost['bm_type'] and phost['bm_type'].lower() ==
                     k_host.BM_TYPE_NONE)) and not bm_type_changed_to_none:
                raise wsme.exc.ClientSideError(
                    _("{}: Rejected: Board Management controller Type "
                      "is not provisioned.  Provisionable values: "
                      "'bmc'.").format(phost['hostname']))
            elif not phost['bm_username']:
                raise wsme.exc.ClientSideError(
                    _("{}: Rejected: Board Management controller username "
                      "is not configured.").format(phost['hostname']))

        # Semantic Check: Validate BM type against supported list
        # ilo, quanta is kept for backwards compatability only
        valid_bm_type_list = [None, 'None', k_host.BM_TYPE_NONE,
                              k_host.BM_TYPE_GENERIC,
                              'ilo', 'ilo3', 'ilo4', 'quanta']

        if not phost['bm_type']:
            phost['bm_type'] = None

        if not (phost['bm_type'] in valid_bm_type_list):
            raise wsme.exc.ClientSideError(
                _("{}: Rejected: '{}' is not a supported board management "
                  "type. Must be one of {}").format(
                    (phost['hostname'], phost['bm_type'], valid_bm_type_list)))

        bm_type_str = phost['bm_type']
        if (phost['bm_type'] and
                bm_type_str.lower() != k_host.BM_TYPE_NONE):
            LOG.info("Updating bm_type from %s to %s" %
                     (phost['bm_type'], k_host.BM_TYPE_GENERIC))
            phost['bm_type'] = k_host.BM_TYPE_GENERIC
            if hostupdate:
                hostupdate.ihost_val_update(
                    {'bm_type': k_host.BM_TYPE_GENERIC})
        else:
            phost['bm_type'] = None
            if hostupdate:
                hostupdate.ihost_val_update({'bm_type': None})

        if (phost['bm_type'] and phost['bm_ip'] and
                (ohost['bm_ip'] != phost['bm_ip'])):
            if not cutils.is_valid_ip(phost['bm_ip']):
                raise wsme.exc.ClientSideError(
                    _("{}: Rejected: Board Management controller IP Address "
                      "is not valid.").format(phost['hostname']))

        if current_ihosts and ('bm_ip' in phost):
            bm_ips = [h['bm_ip'] for h in current_ihosts]

            if phost['bm_ip'] and (phost['bm_ip'] in bm_ips):
                raise wsme.exc.ClientSideError(
                    _("Host-add Rejected: bm_ip %s already exists") %
                    phost['bm_ip'])

        # Update keyring with updated board management credentials, if supplied
        if (ohost['bm_username'] and phost['bm_username'] and
                (ohost['bm_username'] != phost['bm_username'])):
            if not password_exists:
                raise wsme.exc.ClientSideError(
                    _("{} Rejected: username change attempt from {} to {} "
                      "without corresponding password.").format(
                        (phost['hostname'],
                         ohost['bm_username'],
                         phost['bm_username'])))

        if password_exists:
            # The conductor will handle creating the keystore acct
            pecan.request.rpcapi.configure_keystore_account(
                pecan.request.context,
                KEYRING_BM_SERVICE,
                phost['uuid'],
                patch_bm_password)
        LOG.info("%s bm semantic checks for user_agent %s passed" %
                 (phost['hostname'], pecan.request.user_agent))

        return bm_type_changed_to_none

    @staticmethod
    def _semantic_check_nova_local_storage(ihost_uuid, personality):
        """
        Perform semantic checking for nova local storage
        :param ihost_uuid: uuid of host with compute functionality
        :param personality: personality of host with compute functionality
        """

        LOG.info("TODO _semantic_check_nova_local_storage nova local obsol")
        # TODO(sc) configure_check (unlock_compute)
        return

    @staticmethod
    def _handle_ttys_dcd_change(ihost, ttys_dcd):
        """
        Handle serial line carrier detection enable or disable request.
        :param ihost: unpatched ihost dictionary
        :param ttys_dcd: attribute supplied in patch
        """
        LOG.info("%s _handle_ttys_dcd_change from %s to %s" %
                 (ihost['hostname'], ihost['ttys_dcd'], ttys_dcd))

        # check if the flag is changed
        if ttys_dcd is not None:
            if ihost['ttys_dcd'] is None or ihost['ttys_dcd'] != ttys_dcd:
                if ((ihost['administrative'] == k_host.ADMIN_LOCKED and
                     ihost['availability'] == k_host.AVAILABILITY_ONLINE) or
                    (ihost['administrative'] == k_host.ADMIN_UNLOCKED and
                     ihost['operational'] == k_host.OPERATIONAL_ENABLED)):
                    LOG.info("Notify conductor ttys_dcd change: (%s) (%s)" %
                             (ihost['uuid'], ttys_dcd))
                    pecan.request.rpcapi.configure_ttys_dcd(
                        pecan.request.context, ihost['uuid'], ttys_dcd)

    def action_check(self, action, hostupdate):
        """Performs semantic checks related to action"""

        if not action or (action.lower() == k_host.ACTION_NONE):
            rc = False
            return rc

        valid_actions = [k_host.ACTION_UNLOCK,
                         k_host.ACTION_FORCE_UNLOCK,
                         k_host.ACTION_LOCK,
                         k_host.ACTION_FORCE_LOCK,
                         k_host.ACTION_SWACT,
                         k_host.ACTION_FORCE_SWACT,
                         k_host.ACTION_RESET,
                         k_host.ACTION_REBOOT,
                         k_host.ACTION_REINSTALL,
                         k_host.ACTION_POWERON,
                         k_host.ACTION_POWEROFF,
                         k_host.VIM_SERVICES_ENABLED,
                         k_host.VIM_SERVICES_DISABLED,
                         k_host.VIM_SERVICES_DISABLE_FAILED,
                         k_host.VIM_SERVICES_DISABLE_EXTEND,
                         k_host.VIM_SERVICES_DELETE_FAILED,
                         k_host.ACTION_SUBFUNCTION_CONFIG]

        if action not in valid_actions:
            raise wsme.exc.ClientSideError(
                _("'%s' is not a supported maintenance action") % action)

        force_unlock = False
        if action == k_host.ACTION_FORCE_UNLOCK:
            # set force_unlock for semantic check and update action
            # for compatability with vim and mtce
            action = k_host.ACTION_UNLOCK
            force_unlock = True
        hostupdate.action = action
        rc = True

        if action == k_host.ACTION_UNLOCK:
            # Set host_action in DB as early as possible as we need
            # it as a synchronization point for things like lvg/pv
            # deletion which is not allowed when ihost is unlokced
            # or in the process of unlocking.
            rc = self.update_host_action(action, hostupdate)
            if rc:
                pecan.request.dbapi.host_update(hostupdate.ihost_orig['uuid'],
                                                hostupdate.ihost_val_prenotify)
                try:
                    self.check_unlock(hostupdate, force_unlock)
                except Exception as e:
                    LOG.info("host unlock check didn't pass, "
                             "so set the host_action back to None "
                             "and re-raise the exception")
                    self.update_host_action(None, hostupdate)
                    pecan.request.dbapi.host_update(
                        hostupdate.ihost_orig['uuid'],
                        hostupdate.ihost_val_prenotify)
                    raise e
        elif action == k_host.ACTION_LOCK:
            if self.check_lock(hostupdate):
                rc = self.update_host_action(action, hostupdate)
        elif action == k_host.ACTION_FORCE_LOCK:
            if self.check_force_lock(hostupdate):
                rc = self.update_host_action(action, hostupdate)
        elif action == k_host.ACTION_SWACT:
            self.check_swact(hostupdate)
        elif action == k_host.ACTION_FORCE_SWACT:
            self.check_force_swact(hostupdate)
        elif action == k_host.ACTION_REBOOT:
            self.check_reboot(hostupdate)
        elif action == k_host.ACTION_RESET:
            self.check_reset(hostupdate)
        elif action == k_host.ACTION_REINSTALL:
            self.check_reinstall(hostupdate)
        elif action == k_host.ACTION_POWERON:
            self.check_poweron(hostupdate)
        elif action == k_host.ACTION_POWEROFF:
            self.check_poweroff(hostupdate)
        elif action == k_host.VIM_SERVICES_ENABLED:
            self.update_vim_progress_status(action, hostupdate)
        elif action == k_host.VIM_SERVICES_DISABLED:
            self.update_vim_progress_status(action, hostupdate)
        elif action == k_host.VIM_SERVICES_DISABLE_FAILED:
            self.update_vim_progress_status(action, hostupdate)
        elif action == k_host.VIM_SERVICES_DISABLE_EXTEND:
            self.update_vim_progress_status(action, hostupdate)
        elif action == k_host.VIM_SERVICES_DELETE_FAILED:
            self.update_vim_progress_status(action, hostupdate)
        elif action == k_host.ACTION_SUBFUNCTION_CONFIG:
            self._check_subfunction_config(hostupdate)
            self._semantic_check_nova_local_storage(
                hostupdate.ihost_patch['uuid'],
                hostupdate.ihost_patch['personality'])
        else:
            raise wsme.exc.ClientSideError(
                _("action_check unrecognized action: {}").format(action))

        if action in k_host.ACTIONS_MTCE:
            if self._semantic_mtc_check_action(hostupdate, action):
                hostupdate.notify_mtce = True
                task_val = hostupdate.get_task_from_action(action)
                if task_val:
                    hostupdate.ihost_val_update({'task': task_val})

        elif 'administrative' in hostupdate.delta:
            # administrative state changed, update task, host_action in case
            hostupdate.ihost_val_update({'task': "",
                                         'host_action': ""})

        LOG.info("%s action=%s ihost_val_prenotify: %s ihost_val: %s" %
                 (hostupdate.displayid,
                  hostupdate.action,
                  hostupdate.ihost_val_prenotify,
                  hostupdate.ihost_val))

        if hostupdate.ihost_val_prenotify:
            LOG.info("%s host.update.ihost_val_prenotify %s" %
                     (hostupdate.displayid, hostupdate.ihost_val_prenotify))

        if self.check_notify_vim(action):
            hostupdate.notify_vim = True

        if self.check_notify_mtce(action, hostupdate) > 0:
            hostupdate.notify_mtce = True

        LOG.info("%s action_check action=%s, notify_vim=%s "
                 "notify_mtce=%s rc=%s" %
                 (hostupdate.displayid,
                  action,
                  hostupdate.notify_vim,
                  hostupdate.notify_mtce,
                  rc))

        return rc

    @staticmethod
    def check_notify_vim(action):
        if action in k_host.ACTIONS_VIM:
            return True
        else:
            return False

    @staticmethod
    def check_notify_mtce(action, hostupdate):
        """Determine whether mtce should be notified of this patch request
           returns: Integer (nonmtc_change_count)
        """

        nonmtc_change_count = 0
        if action in k_host.ACTIONS_VIM:
            return nonmtc_change_count
        elif action in k_host.ACTIONS_CONFIG:
            return nonmtc_change_count
        elif action in k_host.VIM_SERVICES_ENABLED:
            return nonmtc_change_count

        mtc_ignore_list = ['administrative', 'availability', 'operational',
                           'task', 'uptime', 'capabilities',
                           'host_action',
                           'subfunction_oper', 'subfunction_avail',
                           'vim_progress_status'
                           'location', 'serialid', 'invprovision']

        if pecan.request.user_agent.startswith('mtce'):
            mtc_ignore_list.append('bm_ip')

        nonmtc_change_count = len(set(hostupdate.delta) - set(mtc_ignore_list))

        return nonmtc_change_count

    @staticmethod
    def stage_administrative_update(hostupdate):
        # Always configure when the host is unlocked - this will set the
        # hostname and allow the node to boot and configure itself.
        # NOTE: This is being hit the second time through this function on
        # the unlock. The first time through, the "action" is set to unlock
        # on the patched_iHost, but the "administrative" is still locked.
        # Once maintenance processes the unlock, they do another patch and
        # set the "administrative" to unlocked.
        if ('administrative' in hostupdate.delta and
                hostupdate.ihost_patch['administrative'] ==
                k_host.ADMIN_UNLOCKED):
            if hostupdate.ihost_orig['invprovision'] == \
                    k_host.UNPROVISIONED or \
                    hostupdate.ihost_orig['invprovision'] is None:
                LOG.info("stage_administrative_update: provisioning")
                hostupdate.ihost_val_update({'invprovision':
                                            k_host.PROVISIONING})

        if ('operational' in hostupdate.delta and
                hostupdate.ihost_patch['operational'] ==
                k_host.OPERATIONAL_ENABLED):
            if hostupdate.ihost_orig['invprovision'] == k_host.PROVISIONING:
                # first time unlocked successfully
                LOG.info("stage_administrative_update: provisioned")
                hostupdate.ihost_val_update(
                    {'invprovision': k_host.PROVISIONED})

    @staticmethod
    def _update_add_ceph_state():
        # notify systemconfig of the new ceph state
        LOG.info("TODO(SC) _update_add_ceph_state")

    @staticmethod
    def update_host_action(action, hostupdate):
        if action is None:
            preval = {'host_action': ''}
        elif action == k_host.ACTION_FORCE_LOCK:
            preval = {'host_action': k_host.ACTION_FORCE_LOCK}
        elif action == k_host.ACTION_LOCK:
            preval = {'host_action': k_host.ACTION_LOCK}
        elif (action == k_host.ACTION_UNLOCK or
                action == k_host.ACTION_FORCE_UNLOCK):
            preval = {'host_action': k_host.ACTION_UNLOCK}
        else:
            LOG.error("update_host_action unsupported action: %s" % action)
            return False
        hostupdate.ihost_val_prenotify.update(preval)
        hostupdate.ihost_val.update(preval)

        task_val = hostupdate.get_task_from_action(action)
        if task_val:
            hostupdate.ihost_val_update({'task': task_val})
        return True

    @staticmethod
    def update_vim_progress_status(action, hostupdate):
        LOG.info("%s Pending update_vim_progress_status %s" %
                 (hostupdate.displayid, action))
        return True

    def _check_provisioning(self, hostupdate, patch):
        # Once the host has been provisioned lock down additional fields

        ihost = hostupdate.ihost_patch
        delta = hostupdate.delta

        provision_state = [k_host.PROVISIONED, k_host.PROVISIONING]
        if hostupdate.ihost_orig['invprovision'] in provision_state:
            state_rel_path = ['hostname', 'personality', 'subfunctions']
            if any(p in state_rel_path for p in delta):
                raise wsme.exc.ClientSideError(
                    _("The following fields can not be modified because "
                      "this host {} has been configured: "
                      "hostname, personality, subfunctions").format(
                        hostupdate.ihost_orig['hostname']))

        # Check whether any configurable installation parameters are updated
        install_parms = ['boot_device', 'rootfs_device', 'install_output',
                         'console', 'tboot']
        if any(p in install_parms for p in delta):
            # Disallow changes if the node is not locked
            if ihost['administrative'] != k_host.ADMIN_LOCKED:
                raise wsme.exc.ClientSideError(
                    _("Host must be locked before updating "
                      "installation parameters."))

            # An update to PXE boot information is required
            hostupdate.configure_required = True

        if 'personality' in delta:
            LOG.info("iHost['personality']=%s" %
                     hostupdate.ihost_orig['personality'])

            if hostupdate.ihost_orig['personality']:
                raise wsme.exc.ClientSideError(
                    _("Can not change personality after it has been set. "
                      "Host {} must be deleted and re-added in order to change"
                      " the personality.").format(
                        hostupdate.ihost_orig['hostname']))

            if (hostupdate.ihost_patch['personality'] in
                    (k_host.CONTROLLER, k_host.STORAGE)):
                self._controller_storage_node_setup(hostupdate.ihost_patch,
                                                    hostupdate)
                # check the subfunctions are updated properly
                LOG.info("hostupdate.ihost_patch.subfunctions %s" %
                         hostupdate.ihost_patch['subfunctions'])
            elif hostupdate.ihost_patch['personality'] == k_host.COMPUTE:
                self._check_compute(hostupdate.ihost_patch, hostupdate)
            else:
                LOG.error("Unexpected personality: %s" %
                          hostupdate.ihost_patch['personality'])

            # Always configure when the personality has been set - this will
            # set up the PXE boot information so the software can be installed
            hostupdate.configure_required = True

            # Notify VIM when the personality is set.
            hostupdate.notify_vim_add_host = True

        if k_host.SUBFUNCTIONS in delta:
            if hostupdate.ihost_orig[k_host.SUBFUNCTIONS]:
                raise wsme.exc.ClientSideError(
                    _("Can not change subfunctions after it has been set. Host"
                      "{} must be deleted and re-added in order to change "
                      "the subfunctions.").format(
                        hostupdate.ihost_orig['hostname']))

            if hostupdate.ihost_patch['personality'] == k_host.COMPUTE:
                valid_subfunctions = (k_host.COMPUTE,
                                      k_host.LOWLATENCY)
            elif hostupdate.ihost_patch['personality'] == k_host.CONTROLLER:
                valid_subfunctions = (k_host.CONTROLLER,
                                      k_host.COMPUTE,
                                      k_host.LOWLATENCY)
            elif hostupdate.ihost_patch['personality'] == k_host.STORAGE:
                # Comparison is expecting a list
                valid_subfunctions = (k_host.STORAGE, k_host.STORAGE)

            subfunctions_set = \
                set(hostupdate.ihost_patch[k_host.SUBFUNCTIONS].split(','))

            if not subfunctions_set.issubset(valid_subfunctions):
                raise wsme.exc.ClientSideError(
                    ("%s subfunctions %s contains unsupported values.  "
                     "Allowable: %s." %
                     (hostupdate.displayid,
                      subfunctions_set,
                      valid_subfunctions)))

            if hostupdate.ihost_patch['personality'] == k_host.COMPUTE:
                if k_host.COMPUTE not in subfunctions_set:
                    # Automatically add it
                    subfunctions_list = list(subfunctions_set)
                    subfunctions_list.insert(0, k_host.COMPUTE)
                    subfunctions = ','.join(subfunctions_list)

                    LOG.info("%s update subfunctions=%s" %
                             (hostupdate.displayid, subfunctions))
                    hostupdate.ihost_val_prenotify.update(
                        {'subfunctions': subfunctions})
                    hostupdate.ihost_val.update({'subfunctions': subfunctions})

        # The hostname for a controller or storage node cannot be modified

        # Disallow hostname changes
        if 'hostname' in delta:
            if hostupdate.ihost_orig['hostname']:
                if (hostupdate.ihost_patch['hostname'] !=
                        hostupdate.ihost_orig['hostname']):
                    raise wsme.exc.ClientSideError(
                        _("The hostname field can not be modified because "
                          "the hostname {} has already been configured. "
                          "If changing hostname is required, please delete "
                          "this host, then readd.").format(
                            hostupdate.ihost_orig['hostname']))

        for attribute in patch:
            # check for duplicate attributes
            for attribute2 in patch:
                if attribute['path'] == attribute2['path']:
                    if attribute['value'] != attribute2['value']:
                        raise wsme.exc.ClientSideError(
                            _("Illegal duplicate parameters passed."))

        if 'personality' in delta or 'hostname' in delta:
            personality = hostupdate.ihost_patch.get('personality') or ""
            hostname = hostupdate.ihost_patch.get('hostname') or ""
            if personality and hostname:
                self._validate_hostname(hostname, personality)

        if 'personality' in delta:
            HostController._personality_license_check(
                hostupdate.ihost_patch['personality'])

    @staticmethod
    def _personality_license_check(personality):
        if personality == k_host.CONTROLLER:
            return

        if not personality:
            return

        if personality == k_host.COMPUTE and utils.is_aio_duplex_system():
            if utils.get_compute_count() >= constants.AIO_DUPLEX_MAX_COMPUTES:
                msg = _("All-in-one Duplex is restricted to "
                        "%s computes.") % constants.AIO_DUPLEX_MAX_COMPUTES
                raise wsme.exc.ClientSideError(msg)
            else:
                return

        if (utils.SystemHelper.get_product_build() ==
                constants.TIS_AIO_BUILD):
            msg = _("Personality [%s] for host is not compatible "
                    "with installed software. ") % personality

            raise wsme.exc.ClientSideError(msg)

    @staticmethod
    def check_reset(hostupdate):
        """Check semantics on  host-reset."""
        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            raise wsme.exc.ClientSideError(
                _("Can not 'Reset' a simplex system"))

        if hostupdate.ihost_orig['administrative'] == k_host.ADMIN_UNLOCKED:
            raise wsme.exc.ClientSideError(
                _("Can not 'Reset' an 'unlocked' host {}; "
                  "Please 'Lock' first").format(hostupdate.displayid))

        return True

    @staticmethod
    def check_poweron(hostupdate):
        # Semantic Check: State Dependency: Power-On case
        if (hostupdate.ihost_orig['administrative'] ==
                k_host.ADMIN_UNLOCKED):
            raise wsme.exc.ClientSideError(
                _("Can not 'Power-On' an already Powered-on "
                  "and 'unlocked' host {}").format(hostupdate.displayid))

        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            raise wsme.exc.ClientSideError(
                _("Can not 'Power-On' an already Powered-on "
                  "simplex system"))

    @staticmethod
    def check_poweroff(hostupdate):
        # Semantic Check: State Dependency: Power-Off case
        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            raise wsme.exc.ClientSideError(
                _("Can not 'Power-Off' a simplex system via "
                  "system commands"))

        if (hostupdate.ihost_orig['administrative'] ==
                k_host.ADMIN_UNLOCKED):
            raise wsme.exc.ClientSideError(
                _("Can not 'Power-Off' an 'unlocked' host {}; "
                  "Please 'Lock' first").format(hostupdate.displayid))

    @staticmethod
    def check_reinstall(hostupdate):
        """Semantic Check: State Dependency: Reinstall case"""
        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            raise wsme.exc.ClientSideError(_(
                "Reinstalling a simplex system is not allowed."))

        ihost = hostupdate.ihost_orig
        if ihost['administrative'] == k_host.ADMIN_UNLOCKED:
            raise wsme.exc.ClientSideError(
                _("Can not 'Reinstall' an 'unlocked' host {}; "
                  "Please 'Lock' first").format(hostupdate.displayid))
        elif ((ihost['administrative'] == k_host.ADMIN_LOCKED) and
                (ihost['availability'] != "online")):
            raise wsme.exc.ClientSideError(
                _("Can not 'Reinstall' {} while it is 'offline'. "
                  "Please wait for this host's availability state "
                  "to be 'online' and then re-issue the reinstall "
                  "command.").format(hostupdate.displayid))

    def check_unlock(self, hostupdate, force_unlock=False):
        """Check semantics on  host-unlock."""
        if (hostupdate.action != k_host.ACTION_UNLOCK and
                hostupdate.action != k_host.ACTION_FORCE_UNLOCK):
            LOG.error("check_unlock unexpected action: %s" % hostupdate.action)
            return False

        # Semantic Check: Don't unlock if installation failed
        if (hostupdate.ihost_orig['install_state'] ==
                constants.INSTALL_STATE_FAILED):
            raise wsme.exc.ClientSideError(
                _("Cannot unlock host {} due to installation failure").format(
                    hostupdate.displayid))

        # Semantic Check: Avoid Unlock of Unlocked Host
        if hostupdate.ihost_orig['administrative'] == k_host.ADMIN_UNLOCKED:
            raise wsme.exc.ClientSideError(
                _("Avoiding 'unlock' action on already "
                  "'unlocked' host {}").format(
                    hostupdate.ihost_orig['hostname']))

        # Semantic Check: Action Dependency: Power-Off / Unlock case
        if (hostupdate.ihost_orig['availability'] ==
                k_host.ACTION_POWEROFF):
            raise wsme.exc.ClientSideError(
                _("Can not 'Unlock a Powered-Off' host {}; Power-on, "
                  "wait for 'online' status and then 'unlock'").format(
                    hostupdate.displayid))

        # Semantic Check: Action Dependency: Online / Unlock case
        if (not force_unlock and hostupdate.ihost_orig['availability'] !=
                k_host.AVAILABILITY_ONLINE):
            raise wsme.exc.ClientSideError(
                _("Host {} is not online. "
                  "Wait for 'online' availability status and "
                  "then 'unlock'").format(hostupdate.displayid))

        # To unlock, we need the following additional fields
        if not (hostupdate.ihost_patch['mgmt_mac'] and
                hostupdate.ihost_patch['mgmt_ip'] and
                hostupdate.ihost_patch['hostname'] and
                hostupdate.ihost_patch['personality'] and
                hostupdate.ihost_patch['subfunctions']):
            raise wsme.exc.ClientSideError(
                _("Can not unlock an unprovisioned host {}. "
                  "Please perform 'Edit Host' to provision host.").format(
                    hostupdate.displayid))

        # To unlock, ensure reinstall has completed
        action_state = hostupdate.ihost_orig[k_host.HOST_ACTION_STATE]
        if (action_state and
                action_state == k_host.HAS_REINSTALLING):
            if not force_unlock:
                raise wsme.exc.ClientSideError(
                    _("Can not unlock host {} undergoing reinstall. "
                      "Please ensure host has completed reinstall "
                      "prior to unlock.").format(hostupdate.displayid))
            else:
                LOG.warn("Allowing force-unlock of host %s "
                         "undergoing reinstall." % hostupdate.displayid)

        personality = hostupdate.ihost_patch.get('personality')
        if personality == k_host.CONTROLLER:
            self.check_unlock_controller(hostupdate, force_unlock)
        if cutils.host_has_function(hostupdate.ihost_patch, k_host.COMPUTE):
            self.check_unlock_compute(hostupdate)
        elif personality == k_host.STORAGE:
            self.check_unlock_storage(hostupdate)

        # self.check_unlock_interfaces(hostupdate)
        # self.unlock_update_mgmt_infra_interface(hostupdate.ihost_patch)
        # TODO(storage) self.check_unlock_partitions(hostupdate)
        self.check_unlock_patching(hostupdate, force_unlock)

        hostupdate.configure_required = True
        hostupdate.notify_vim = True

        return True

    def check_unlock_patching(self, hostupdate, force_unlock):
        """Check whether the host is patch current.
        """

        if force_unlock:
            return

        try:
            system = objects.System.get_one(pecan.request.context)
            response = patch_api.patch_query_hosts(
                pecan.request.context,
                system.region_name)
            phosts = response['data']
        except Exception as e:
            LOG.warn(_("No response from patch api {} e={}").format(
                (hostupdate.displayid, e)))
            self._api_token = None
            return

        for phost in phosts:
            if phost.get('hostname') == hostupdate.ihost_patch.get('hostname'):
                if not phost.get('patch_current'):
                    raise wsme.exc.ClientSideError(
                        _("host-unlock rejected: Not patch current. "
                          "'sw-patch host-install {}' is required.").format(
                            hostupdate.displayid))

    def check_lock(self, hostupdate):
        """Check semantics on  host-lock."""
        LOG.info("%s ihost check_lock" % hostupdate.displayid)
        if hostupdate.action != k_host.ACTION_LOCK:
            LOG.error("%s check_lock unexpected action: %s" %
                      (hostupdate.displayid, hostupdate.action))
            return False

        # Semantic Check: Avoid Lock of Locked Host
        if hostupdate.ihost_orig['administrative'] == k_host.ADMIN_LOCKED:
            raise wsme.exc.ClientSideError(
                _("Avoiding {} action on already "
                  "'locked' host {}").format(
                    hostupdate.ihost_patch['action'],
                    hostupdate.ihost_orig['hostname']))

        # personality specific lock checks
        personality = hostupdate.ihost_patch.get('personality')
        if personality == k_host.CONTROLLER:
            self.check_lock_controller(hostupdate)
        elif personality == k_host.STORAGE:
            self.check_lock_storage(hostupdate)

        subfunctions_set = \
            set(hostupdate.ihost_patch[k_host.SUBFUNCTIONS].split(','))
        if k_host.COMPUTE in subfunctions_set:
            self.check_lock_compute(hostupdate)

        hostupdate.notify_vim = True
        hostupdate.notify_mtce = True

        return True

    def check_force_lock(self, hostupdate):
        # personality specific lock checks
        personality = hostupdate.ihost_patch.get('personality')
        if personality == k_host.CONTROLLER:
            self.check_lock_controller(hostupdate, force=True)

        elif personality == k_host.STORAGE:
            self.check_lock_storage(hostupdate, force=True)
        return True

    @staticmethod
    def check_lock_controller(hostupdate, force=False):
        """Pre lock semantic checks for controller"""

        LOG.info("%s ihost check_lock_controller" % hostupdate.displayid)

        if utils.get_system_mode() != constants.SYSTEM_MODE_SIMPLEX:
            if utils.is_host_active_controller(hostupdate.ihost_orig):
                raise wsme.exc.ClientSideError(
                    _("%s : Rejected: Can not lock an active controller") %
                    hostupdate.ihost_orig['hostname'])

        if StorageBackendConfig.has_backend_configured(
                pecan.request.dbapi,
                constants.CINDER_BACKEND_CEPH):
            try:
                st_nodes = objects.Host.list(
                    pecan.request.context,
                    filters={'personality': k_host.STORAGE})

            except exception.HostNotFound:
                # If we don't have any storage nodes we don't need to
                # check for quorum. We'll allow the node to be locked.
                return
            # TODO(oponcea) remove once SM supports in-service config reload
            # Allow locking controllers when all storage nodes are locked.
            for node in st_nodes:
                if node['administrative'] == k_host.ADMIN_UNLOCKED:
                    break
            else:
                return

        if not force:
            # sm-lock-pre-check
            node_name = hostupdate.displayid
            response = sm_api.lock_pre_check(pecan.request.context, node_name)
            if response:
                error_code = response.get('error_code')
                if ERR_CODE_LOCK_SOLE_SERVICE_PROVIDER == error_code:
                    impact_svc_list = response.get('impact_service_list')
                    svc_list = ','.join(impact_svc_list)
                    if len(impact_svc_list) > 1:
                        msg = _("Services {svc_list} are only running on "
                                "{host}, locking {host} will result "
                                "service outage. If lock {host} is required, "
                                "please use \"force lock\" command.").format(
                            svc_list=svc_list, host=node_name)
                    else:
                        msg = _("Service {svc_list} is only running on "
                                "{host}, locking {host} will result "
                                "service outage. If lock {host} is required, "
                                "please use \"force lock\" command.").format(
                            svc_list=svc_list, host=node_name)

                    raise wsme.exc.ClientSideError(msg)
                elif "0" != error_code:
                    raise wsme.exc.ClientSideError(
                        _("{}").format(response['error_details']))

    @staticmethod
    def _host_configure_check(host_uuid):
        # check with systemconfig host/<uuid>/state/configure_check
        if pecan.request.systemconfig.host_configure_check(host_uuid):
            LOG.info("Configuration check {} passed".format(host_uuid))
            raise wsme.exc.ClientSideError("host_configure_check Passed")
        else:
            LOG.info("Configuration check {} failed".format(host_uuid))
            raise wsme.exc.ClientSideError("host_configure_check Failed")

    def check_unlock_controller(self, hostupdate, force_unlock=False):
        """Pre unlock semantic checks for controller"""
        LOG.info("{} host check_unlock_controller".format(
            hostupdate.displayid))
        self._host_configure_check(hostupdate.ihost_orig['uuid'])

    def check_unlock_compute(self, hostupdate):
        """Check semantics on  host-unlock of a compute."""
        LOG.info("%s ihost check_unlock_compute" % hostupdate.displayid)
        ihost = hostupdate.ihost_orig
        if ihost['invprovision'] is None:
            raise wsme.exc.ClientSideError(
                _("Can not unlock an unconfigured host {}. Please "
                  "configure host and wait for Availability State "
                  "'online' prior to unlock.").format(hostupdate.displayid))
        self._host_configure_check(ihost['uuid'])

    def check_unlock_storage(self, hostupdate):
        """Storage unlock semantic checks"""
        self._host_configure_check(hostupdate.ihost_orig['uuid'])

    @staticmethod
    def check_updates_while_unlocked(hostupdate, delta):
        """Check semantics host-update of an unlocked host."""

        ihost = hostupdate.ihost_patch
        if ihost['administrative'] == k_host.ADMIN_UNLOCKED:
            deltaset = set(delta)

            restricted_updates = ()
            if not pecan.request.user_agent.startswith('mtce'):
                # Allow mtc to modify the state throughthe REST API.
                # Eventually mtc should switch to using the
                # conductor API to modify hosts because this check will also
                # allow users to modify these states (which is bad).
                restricted_updates = ('administrative',
                                      'availability',
                                      'operational',
                                      'subfunction_oper',
                                      'subfunction_avail',
                                      'task', 'uptime')

            if deltaset.issubset(restricted_updates):
                raise wsme.exc.ClientSideError(
                    ("Change set %s contains a subset of restricted %s." %
                     (deltaset, restricted_updates)))
            else:
                LOG.debug("PASS deltaset=%s restricted_updates=%s" %
                          (deltaset, restricted_updates))

            if 'administrative' in delta:
                # Transition to unlocked
                if ihost['host_action']:
                    LOG.info("Host: %s Admin state change to: %s "
                             "Clearing host_action=%s" %
                             (ihost['uuid'],
                              ihost['administrative'],
                              ihost['host_action']))
                    hostupdate.ihost_val_update({'host_action': ""})
                pass

    @staticmethod
    def check_force_swact(hostupdate):
        """Pre swact semantic checks for controller"""
        # Allow force-swact to continue
        return True

    @staticmethod
    def check_reboot(hostupdate):
        """Pre reboot semantic checks"""
        # Semantic Check: State Dependency: Reboot case
        if hostupdate.ihost_orig['administrative'] == k_host.ADMIN_UNLOCKED:
            raise wsme.exc.ClientSideError(
                _("Can not 'Reboot' an 'unlocked' host {}; "
                  "Please 'Lock' first").format(hostupdate.displayid))

        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            raise wsme.exc.ClientSideError(_(
                "Rebooting a simplex system is not allowed."))
        return True

    def check_swact(self, hostupdate):
        """Pre swact semantic checks for controller"""

        if hostupdate.ihost_orig['personality'] != k_host.CONTROLLER:
            raise wsme.exc.ClientSideError(
                _("Swact action not allowed for "
                  "non controller host {}.").format(
                    hostupdate.ihost_orig['hostname']))

        if hostupdate.ihost_orig['administrative'] == k_host.ADMIN_LOCKED:
            raise wsme.exc.ClientSideError(
                _("Controller is Locked ; No services to Swact"))

        if utils.get_system_mode() == constants.SYSTEM_MODE_SIMPLEX:
            raise wsme.exc.ClientSideError(_(
                "Swact action not allowed for a simplex system."))

        # check target controller
        ihost_ctrs = objects.Host.list(
            pecan.request.context,
            filters={'personality': k_host.CONTROLLER})

        for ihost_ctr in ihost_ctrs:
            if ihost_ctr.hostname != hostupdate.ihost_orig['hostname']:
                if (ihost_ctr.operational !=
                        k_host.OPERATIONAL_ENABLED):
                    raise wsme.exc.ClientSideError(
                        _("{} is not enabled and has operational "
                          "state {}."
                          "Standby controller must be  operationally "
                          "enabled.").format(
                            (ihost_ctr.hostname, ihost_ctr.operational)))

                if (ihost_ctr.availability ==
                        k_host.AVAILABILITY_DEGRADED):
                    health_helper = health.Health(
                        pecan.request.context,
                        pecan.request.dbapi)
                    degrade_alarms = health_helper.get_alarms_degrade(
                        pecan.request.context,
                        alarm_ignore_list=[
                            fm_constants.FM_ALARM_ID_HA_SERVICE_GROUP_STATE,
                            fm_constants.FM_ALARM_ID_HA_SERVICE_GROUP_REDUNDANCY,  # noqa
                            fm_constants.FM_ALARM_ID_HA_NODE_LICENSE,
                            fm_constants.FM_ALARM_ID_HA_COMMUNICATION_FAILURE
                        ],
                        entity_instance_id_filter=ihost_ctr.hostname)
                    if degrade_alarms:
                        raise wsme.exc.ClientSideError(
                            _("%s has degraded availability status.  Standby "
                              "controller must be in available status.") %
                            ihost_ctr.hostname)

                if k_host.COMPUTE in ihost_ctr.subfunctions:
                    if (ihost_ctr.subfunction_oper !=
                            k_host.OPERATIONAL_ENABLED):
                        raise wsme.exc.ClientSideError(
                            _("{} subfunction is not enabled and has "
                              "operational state {}."
                              "Standby controller subfunctions {} "
                              "must all be operationally enabled.").format(
                                (ihost_ctr.hostname,
                                 ihost_ctr.subfunction_oper,
                                 ihost_ctr.subfunctions)))

        LOG.info("TODO sc_op to check swact (storage_backend, tpm_config"
                 ", DRBD resizing")

        # Check: Valid Swact action: Pre-Swact Check
        response = sm_api.swact_pre_check(
            pecan.request.context,
            hostupdate.ihost_orig['hostname'])
        if response and "0" != response['error_code']:
            raise wsme.exc.ClientSideError(
                _("%s").format(response['error_details']))

    def check_lock_storage(self, hostupdate, force=False):
        """Pre lock semantic checks for storage"""
        LOG.info("%s ihost check_lock_storage" % hostupdate.displayid)

        if (hostupdate.ihost_orig['administrative'] ==
                k_host.ADMIN_UNLOCKED and
                hostupdate.ihost_orig['operational'] ==
                k_host.OPERATIONAL_ENABLED):
            num_monitors, required_monitors, quorum_names = \
                self._ceph.get_monitors_status(pecan.request.dbapi)

            if (hostupdate.ihost_orig['hostname'] in quorum_names and
                    num_monitors - 1 < required_monitors):
                raise wsme.exc.ClientSideError(_(
                    "Only {} storage monitor available. "
                    "At least {} unlocked and enabled hosts with monitors "
                    "are required. Please ensure hosts with monitors are "
                    "unlocked and enabled - candidates: {}, {}, {}").format(
                    (num_monitors, constants.MIN_STOR_MONITORS,
                     k_host.CONTROLLER_0_HOSTNAME,
                     k_host.CONTROLLER_1_HOSTNAME,
                     k_host.STORAGE_0_HOSTNAME)))

            # send request to systemconfig to check disable storage
            LOG.info("TODO sc_op to perform disable storage checks")

    @staticmethod
    def check_lock_compute(hostupdate, force=False):
        """Pre lock semantic checks for compute"""

        LOG.info("%s host check_lock_compute" % hostupdate.displayid)
        if force:
            return

        system = objects.System.get_one(pecan.request.context)
        if system.system_mode == constants.SYSTEM_MODE_SIMPLEX:
            return

        # send request to systemconfig to check disable storage
        LOG.info("TODO sc_op to perform disable storage checks")

    def stage_action(self, action, hostupdate):
        """Stage the action to be performed.
        """
        LOG.info("%s stage_action %s" % (hostupdate.displayid, action))
        rc = True
        if not action or (
                action and action.lower() == k_host.ACTION_NONE):
            LOG.error("Unrecognized action perform: %s" % action)
            return False

        if (action == k_host.ACTION_UNLOCK or
                action == k_host.ACTION_FORCE_UNLOCK):
            self._handle_unlock_action(hostupdate)
        elif action == k_host.ACTION_LOCK:
            self._handle_lock_action(hostupdate)
        elif action == k_host.ACTION_FORCE_LOCK:
            self._handle_force_lock_action(hostupdate)
        elif action == k_host.ACTION_SWACT:
            self._stage_swact(hostupdate)
        elif action == k_host.ACTION_FORCE_SWACT:
            self._stage_force_swact(hostupdate)
        elif action == k_host.ACTION_REBOOT:
            self._stage_reboot(hostupdate)
        elif action == k_host.ACTION_RESET:
            self._stage_reset(hostupdate)
        elif action == k_host.ACTION_REINSTALL:
            self._stage_reinstall(hostupdate)
        elif action == k_host.ACTION_POWERON:
            self._stage_poweron(hostupdate)
        elif action == k_host.ACTION_POWEROFF:
            self._stage_poweroff(hostupdate)
        elif action == k_host.VIM_SERVICES_ENABLED:
            self._handle_vim_services_enabled(hostupdate)
        elif action == k_host.VIM_SERVICES_DISABLED:
            if not self._handle_vim_services_disabled(hostupdate):
                LOG.warn(_("{} exit _handle_vim_services_disabled").format(
                    hostupdate.ihost_patch['hostname']))
                hostupdate.nextstep = hostupdate.EXIT_RETURN_HOST
                rc = False
        elif action == k_host.VIM_SERVICES_DISABLE_FAILED:
            if not self._handle_vim_services_disable_failed(hostupdate):
                LOG.warn(
                    _("{} Exit _handle_vim_services_disable failed").format(
                        hostupdate.ihost_patch['hostname']))
                hostupdate.nextstep = hostupdate.EXIT_RETURN_HOST
                rc = False
        elif action == k_host.VIM_SERVICES_DISABLE_EXTEND:
            self._handle_vim_services_disable_extend(hostupdate)
            hostupdate.nextstep = hostupdate.EXIT_UPDATE_PREVAL
            rc = False
        elif action == k_host.VIM_SERVICES_DELETE_FAILED:
            self._handle_vim_services_delete_failed(hostupdate)
            hostupdate.nextstep = hostupdate.EXIT_UPDATE_PREVAL
            rc = False
        elif action == k_host.ACTION_SUBFUNCTION_CONFIG:
            # Not a mtc action; disable mtc checks and config
            self._stage_subfunction_config(hostupdate)
        else:
            LOG.error("%s Unrecognized action perform: %s" %
                      (hostupdate.displayid, action))
            rc = False

        if hostupdate.nextstep == hostupdate.EXIT_RETURN_HOST:
            LOG.info("%s stage_action aborting request %s %s" %
                     (hostupdate.displayid,
                      hostupdate.action,
                      hostupdate.delta))

        return rc

    @staticmethod
    def _check_subfunction_config(hostupdate):
        """Check subfunction config."""
        LOG.info("%s _check_subfunction_config" % hostupdate.displayid)
        patched_ihost = hostupdate.ihost_patch

        if patched_ihost['action'] == "subfunction_config":
            if (not patched_ihost['subfunctions'] or
                    patched_ihost['personality'] ==
                    patched_ihost['subfunctions']):
                raise wsme.exc.ClientSideError(
                    _("This host is not configured with a subfunction."))

        return True

    @staticmethod
    def _stage_subfunction_config(hostupdate):
        """Stage subfunction config."""
        LOG.info("%s _stage_subfunction_config" % hostupdate.displayid)

        hostupdate.notify_mtce = False
        hostupdate.skip_notify_mtce = True

    @staticmethod
    def perform_action_subfunction_config(ihost_obj):
        """Perform subfunction config via RPC to conductor."""
        LOG.info("%s perform_action_subfunction_config" %
                 ihost_obj['hostname'])
        pecan.request.rpcapi.configure_host(pecan.request.context,
                                            ihost_obj,
                                            do_compute_apply=True)

    @staticmethod
    def _stage_reboot(hostupdate):
        """Stage reboot action."""
        LOG.info("%s stage_reboot" % hostupdate.displayid)
        hostupdate.notify_mtce = True

    def _stage_reinstall(self, hostupdate):
        """Stage reinstall action."""
        LOG.info("%s stage_reinstall" % hostupdate.displayid)

        # Remove manifests to enable standard install without manifests
        # and enable storage allocation change
        pecan.request.rpcapi.remove_host_config(
            pecan.request.context,
            hostupdate.ihost_orig['uuid'])

        hostupdate.notify_mtce = True
        if hostupdate.ihost_orig['personality'] == k_host.STORAGE:
            istors = pecan.request.dbapi.istor_get_by_ihost(
                hostupdate.ihost_orig['uuid'])
            for stor in istors:
                istor_obj = objects.storage.get_by_uuid(
                    pecan.request.context, stor.uuid)
                self._ceph.remove_osd_key(istor_obj['osdid'])

        hostupdate.ihost_val_update({k_host.HOST_ACTION_STATE:
                                    k_host.HAS_REINSTALLING})

    @staticmethod
    def _stage_poweron(hostupdate):
        """Stage poweron action."""
        LOG.info("%s stage_poweron" % hostupdate.displayid)
        hostupdate.notify_mtce = True

    @staticmethod
    def _stage_poweroff(hostupdate):
        """Stage poweroff action."""
        LOG.info("%s stage_poweroff" % hostupdate.displayid)
        hostupdate.notify_mtce = True

    @staticmethod
    def _stage_swact(hostupdate):
        """Stage swact action."""
        LOG.info("%s stage_swact" % hostupdate.displayid)
        hostupdate.notify_mtce = True

    @staticmethod
    def _stage_force_swact(hostupdate):
        """Stage force-swact action."""
        LOG.info("%s stage_force_swact" % hostupdate.displayid)
        hostupdate.notify_mtce = True

    @staticmethod
    def _handle_vim_services_enabled(hostupdate):
        """Handle VIM services-enabled signal."""
        vim_progress_status = hostupdate.ihost_orig.get(
            'vim_progress_status') or ""
        LOG.info("%s received services-enabled task=%s vim_progress_status=%s"
                 % (hostupdate.displayid,
                    hostupdate.ihost_orig['task'],
                    vim_progress_status))

        if (not vim_progress_status or
                not vim_progress_status.startswith(
                k_host.VIM_SERVICES_ENABLED)):
            hostupdate.notify_availability = k_host.VIM_SERVICES_ENABLED
            if (not vim_progress_status or
                    vim_progress_status == k_host.VIM_SERVICES_DISABLED):
                # otherwise allow the audit to clear the error message
                hostupdate.ihost_val_update({'vim_progress_status':
                                            k_host.VIM_SERVICES_ENABLED})

        hostupdate.skip_notify_mtce = True

    @staticmethod
    def _handle_vim_services_disabled(hostupdate):
        """Handle VIM services-disabled signal."""

        LOG.info("%s _handle_vim_services_disabled'" % hostupdate.displayid)
        ihost = hostupdate.ihost_orig

        hostupdate.ihost_val_update(
            {'vim_progress_status': k_host.VIM_SERVICES_DISABLED})

        ihost_task_string = ihost['host_action'] or ""
        if ((ihost_task_string.startswith(k_host.ACTION_LOCK) or
                ihost_task_string.startswith(k_host.ACTION_FORCE_LOCK)) and
                ihost['administrative'] != k_host.ADMIN_LOCKED):
            # passed - skip reset for force-lock
            # iHost['host_action'] = k_host.ACTION_LOCK
            hostupdate.notify_availability = k_host.VIM_SERVICES_DISABLED
            hostupdate.notify_action_lock = True
            hostupdate.notify_mtce = True
        else:
            # return False rather than failing request.
            LOG.warn(_("{} Admin action task not Locking or Force Locking "
                       "upon receipt of 'services-disabled'.").format(
                hostupdate.displayid))
            hostupdate.skip_notify_mtce = True
            return False

        return True

    @staticmethod
    def _handle_vim_services_disable_extend(hostupdate):
        """Handle VIM services-disable-extend signal."""
        host_action = hostupdate.ihost_orig['host_action'] or ""
        result_reason = hostupdate.ihost_patch.get('vim_progress_status') or ""
        LOG.info("%s handle_vim_services_disable_extend "
                 "host_action=%s reason=%s" %
                 (hostupdate.displayid, host_action, result_reason))

        hostupdate.skip_notify_mtce = True
        if host_action.startswith(k_host.ACTION_LOCK):
            val = {'task': k_host.LOCKING + '-',
                   'host_action': k_host.ACTION_LOCK}
            hostupdate.ihost_val_prenotify_update(val)
        else:
            LOG.warn("%s Skip vim services disable extend ihost action=%s" %
                     (hostupdate.displayid, host_action))
            return False

        LOG.info("services-disable-extend reason=%s" % result_reason)
        return True

    @staticmethod
    def _handle_vim_services_disable_failed(hostupdate):
        """Handle VIM services-disable-failed signal."""
        ihost_task_string = hostupdate.ihost_orig['host_action'] or ""
        LOG.info("%s handle_vim_services_disable_failed host_action=%s" %
                 (hostupdate.displayid, ihost_task_string))

        result_reason = hostupdate.ihost_patch.get('vim_progress_status') or ""

        if ihost_task_string.startswith(k_host.ACTION_LOCK):
            hostupdate.skip_notify_mtce = True
            val = {'host_action': '',
                   'task': '',
                   'vim_progress_status': result_reason}
            hostupdate.ihost_val_prenotify_update(val)
            hostupdate.ihost_val.update(val)
            hostupdate.skip_notify_mtce = True
        elif ihost_task_string.startswith(k_host.ACTION_FORCE_LOCK):
            # allow mtce to reset the host
            hostupdate.notify_mtce = True
            hostupdate.notify_action_lock_force = True
        else:
            hostupdate.skip_notify_mtce = True
            LOG.warn("%s Skipping vim services disable notification task=%s" %
                     (hostupdate.displayid, ihost_task_string))
            return False

        if result_reason:
            LOG.info("services-disable-failed reason=%s" % result_reason)
            hostupdate.ihost_val_update({'vim_progress_status':
                                        result_reason})
        else:
            hostupdate.ihost_val_update({'vim_progress_status':
                                        k_host.VIM_SERVICES_DISABLE_FAILED})

        return True

    @staticmethod
    def _handle_vim_services_delete_failed(hostupdate):
        """Handle VIM services-delete-failed signal."""

        ihost_admin = hostupdate.ihost_orig['administrative'] or ""
        result_reason = hostupdate.ihost_patch.get('vim_progress_status') or ""
        LOG.info("%s handle_vim_services_delete_failed admin=%s reason=%s" %
                 (hostupdate.displayid, ihost_admin, result_reason))

        hostupdate.skip_notify_mtce = True
        if ihost_admin.startswith(k_host.ADMIN_LOCKED):
            val = {'host_action': '',
                   'task': '',
                   'vim_progress_status': result_reason}
            hostupdate.ihost_val_prenotify_update(val)
            # hostupdate.ihost_val.update(val)
        else:
            LOG.warn("%s Skip vim services delete failed notify admin=%s" %
                     (hostupdate.displayid, ihost_admin))
            return False

        if result_reason:
            hostupdate.ihost_val_prenotify_update({'vim_progress_status':
                                                  result_reason})
        else:
            hostupdate.ihost_val_prenotify_update(
                {'vim_progress_status': k_host.VIM_SERVICES_DELETE_FAILED})

        LOG.info("services-disable-failed reason=%s" % result_reason)
        return True

    @staticmethod
    def _stage_reset(hostupdate):
        """Handle host-reset action."""
        LOG.info("%s _stage_reset" % hostupdate.displayid)
        hostupdate.notify_mtce = True

    def _handle_unlock_action(self, hostupdate):
        """Handle host-unlock action."""
        LOG.info("%s _handle_unlock_action" % hostupdate.displayid)
        if hostupdate.ihost_patch.get('personality') == k_host.STORAGE:
            self._handle_unlock_storage_host(hostupdate)
        hostupdate.notify_vim_action = False
        hostupdate.notify_mtce = True
        val = {'host_action': k_host.ACTION_UNLOCK}
        hostupdate.ihost_val_prenotify_update(val)
        hostupdate.ihost_val.update(val)

    def _handle_unlock_storage_host(self, hostupdate):
        self._ceph.update_crushmap(hostupdate)

    @staticmethod
    def _handle_lock_action(hostupdate):
        """Handle host-lock action."""
        LOG.info("%s _handle_lock_action" % hostupdate.displayid)

        hostupdate.notify_vim_action = True
        hostupdate.skip_notify_mtce = True
        val = {'host_action': k_host.ACTION_LOCK}
        hostupdate.ihost_val_prenotify_update(val)
        hostupdate.ihost_val.update(val)

    @staticmethod
    def _handle_force_lock_action(hostupdate):
        """Handle host-force-lock action."""
        LOG.info("%s _handle_force_lock_action" % hostupdate.displayid)

        hostupdate.notify_vim_action = True
        hostupdate.skip_notify_mtce = True
        val = {'host_action': k_host.ACTION_FORCE_LOCK}
        hostupdate.ihost_val_prenotify_update(val)
        hostupdate.ihost_val.update(val)


def _create_node(host, xml_node, personality, is_dynamic_ip):
    host_node = et.SubElement(xml_node, 'host')
    et.SubElement(host_node, 'personality').text = personality
    if personality == k_host.COMPUTE:
        et.SubElement(host_node, 'hostname').text = host.hostname
        et.SubElement(host_node, 'subfunctions').text = host.subfunctions

    et.SubElement(host_node, 'mgmt_mac').text = host.mgmt_mac
    if not is_dynamic_ip:
        et.SubElement(host_node, 'mgmt_ip').text = host.mgmt_ip
    if host.location is not None and 'locn' in host.location:
        et.SubElement(host_node, 'location').text = host.location['locn']

    pw_on_instruction = _('Uncomment the statement below to power on the host '
                          'automatically through board management.')
    host_node.append(et.Comment(pw_on_instruction))
    host_node.append(et.Comment('<power_on />'))
    et.SubElement(host_node, 'bm_type').text = host.bm_type
    et.SubElement(host_node, 'bm_username').text = host.bm_username
    et.SubElement(host_node, 'bm_password').text = ''

    et.SubElement(host_node, 'boot_device').text = host.boot_device
    et.SubElement(host_node, 'rootfs_device').text = host.rootfs_device
    et.SubElement(host_node, 'install_output').text = host.install_output
    et.SubElement(host_node, 'console').text = host.console
    et.SubElement(host_node, 'tboot').text = host.tboot
