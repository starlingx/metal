# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright 2013 UnitedStack Inc.
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

import six

import pecan
from pecan import rest

from wsme import types as wtypes
import wsmeext.pecan as wsme_pecan

from inventory.api.controllers.v1 import base
from inventory.api.controllers.v1 import collection
from inventory.api.controllers.v1 import link
from inventory.api.controllers.v1 import types
from inventory.api.controllers.v1 import utils
from inventory.common import exception
from inventory.common.i18n import _
from inventory import objects

from oslo_log import log

LOG = log.getLogger(__name__)


class CPUPatchType(types.JsonPatchType):

    @staticmethod
    def mandatory_attrs():
        return []


class CPU(base.APIBase):
    """API representation of a host CPU.

    This class enforces type checking and value constraints, and converts
    between the internal object model and the API representation of a cpu.
    """

    uuid = types.uuid
    "Unique UUID for this cpu"

    cpu = int
    "Represent the cpu id cpu"

    core = int
    "Represent the core id cpu"

    thread = int
    "Represent the thread id cpu"

    cpu_family = wtypes.text
    "Represent the cpu family of the cpu"

    cpu_model = wtypes.text
    "Represent the cpu model of the cpu"

    function = wtypes.text
    "Represent the function of the cpu"

    num_cores_on_processor0 = wtypes.text
    "The number of cores on processors 0"

    num_cores_on_processor1 = wtypes.text
    "The number of cores on processors 1"

    num_cores_on_processor2 = wtypes.text
    "The number of cores on processors 2"

    num_cores_on_processor3 = wtypes.text
    "The number of cores on processors 3"

    numa_node = int
    "The numa node or zone the cpu. API only attribute"

    capabilities = {wtypes.text: utils.ValidTypes(wtypes.text,
                    six.integer_types)}
    "This cpu's meta data"

    host_id = int
    "The hostid that this cpu belongs to"

    node_id = int
    "The nodeId that this cpu belongs to"

    host_uuid = types.uuid
    "The UUID of the host this cpu belongs to"

    node_uuid = types.uuid
    "The UUID of the node this cpu belongs to"

    links = [link.Link]
    "A list containing a self link and associated cpu links"

    def __init__(self, **kwargs):
        self.fields = objects.CPU.fields.keys()
        for k in self.fields:
            setattr(self, k, kwargs.get(k))

        # API only attributes
        self.fields.append('function')
        setattr(self, 'function', kwargs.get('function', None))
        self.fields.append('num_cores_on_processor0')
        setattr(self, 'num_cores_on_processor0',
                kwargs.get('num_cores_on_processor0', None))
        self.fields.append('num_cores_on_processor1')
        setattr(self, 'num_cores_on_processor1',
                kwargs.get('num_cores_on_processor1', None))
        self.fields.append('num_cores_on_processor2')
        setattr(self, 'num_cores_on_processor2',
                kwargs.get('num_cores_on_processor2', None))
        self.fields.append('num_cores_on_processor3')
        setattr(self, 'num_cores_on_processor3',
                kwargs.get('num_cores_on_processor3', None))

    @classmethod
    def convert_with_links(cls, rpc_port, expand=True):
        cpu = CPU(**rpc_port.as_dict())
        if not expand:
            cpu.unset_fields_except(
                ['uuid', 'cpu', 'core', 'thread',
                 'cpu_family', 'cpu_model',
                 'numa_node', 'host_uuid', 'node_uuid',
                 'host_id', 'node_id',
                 'capabilities',
                 'created_at', 'updated_at'])

        # never expose the id attribute
        cpu.host_id = wtypes.Unset
        cpu.node_id = wtypes.Unset

        cpu.links = [link.Link.make_link('self', pecan.request.host_url,
                                         'cpus', cpu.uuid),
                     link.Link.make_link('bookmark',
                                         pecan.request.host_url,
                                         'cpus', cpu.uuid,
                                         bookmark=True)
                     ]
        return cpu


class CPUCollection(collection.Collection):
    """API representation of a collection of cpus."""

    cpus = [CPU]
    "A list containing cpu objects"

    def __init__(self, **kwargs):
        self._type = 'cpus'

    @classmethod
    def convert_with_links(cls, rpc_ports, limit, url=None,
                           expand=False, **kwargs):
        collection = CPUCollection()
        collection.cpus = [
            CPU.convert_with_links(p, expand) for p in rpc_ports]
        collection.next = collection.get_next(limit, url=url, **kwargs)
        return collection


class CPUController(rest.RestController):
    """REST controller for cpus."""

    _custom_actions = {
        'detail': ['GET'],
    }

    def __init__(self, from_hosts=False, from_node=False):
        self._from_hosts = from_hosts
        self._from_node = from_node

    def _get_cpus_collection(self, i_uuid, node_uuid, marker,
                             limit, sort_key, sort_dir,
                             expand=False, resource_url=None):

        if self._from_hosts and not i_uuid:
            raise exception.InvalidParameterValue(_(
                "Host id not specified."))

        if self._from_node and not i_uuid:
            raise exception.InvalidParameterValue(_(
                "Node id not specified."))

        limit = utils.validate_limit(limit)
        sort_dir = utils.validate_sort_dir(sort_dir)

        marker_obj = None
        if marker:
            marker_obj = objects.CPU.get_by_uuid(pecan.request.context,
                                                 marker)

        if self._from_hosts:
            # cpus = pecan.request.dbapi.cpu_get_by_host(
            cpus = objects.CPU.get_by_host(
                pecan.request.context,
                i_uuid, limit,
                marker_obj,
                sort_key=sort_key,
                sort_dir=sort_dir)
        elif self._from_node:
            # cpus = pecan.request.dbapi.cpu_get_by_node(
            cpus = objects.CPU.get_by_node(
                pecan.request.context,
                i_uuid, limit,
                marker_obj,
                sort_key=sort_key,
                sort_dir=sort_dir)
        else:
            if i_uuid and not node_uuid:
                # cpus = pecan.request.dbapi.cpu_get_by_host(
                cpus = objects.CPU.get_by_host(
                    pecan.request.context,
                    i_uuid, limit,
                    marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)
            elif i_uuid and node_uuid:
                # cpus = pecan.request.dbapi.cpu_get_by_host_node(
                cpus = objects.CPU.get_by_host_node(
                    pecan.request.context,
                    i_uuid,
                    node_uuid,
                    limit,
                    marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)

            elif node_uuid:
                # cpus = pecan.request.dbapi.cpu_get_by_host_node(
                cpus = objects.CPU.get_by_node(
                    pecan.request.context,
                    i_uuid,
                    node_uuid,
                    limit,
                    marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)

            else:
                # cpus = pecan.request.dbapi.icpu_get_list(
                cpus = objects.CPU.list(
                    pecan.request.context,
                    limit, marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)

        return CPUCollection.convert_with_links(cpus, limit,
                                                url=resource_url,
                                                expand=expand,
                                                sort_key=sort_key,
                                                sort_dir=sort_dir)

    @wsme_pecan.wsexpose(CPUCollection, types.uuid, types.uuid,
                         types.uuid, int, wtypes.text, wtypes.text)
    def get_all(self, host_uuid=None, node_uuid=None,
                marker=None, limit=None, sort_key='id', sort_dir='asc'):
        """Retrieve a list of cpus."""
        return self._get_cpus_collection(host_uuid, node_uuid,
                                         marker, limit,
                                         sort_key, sort_dir)

    @wsme_pecan.wsexpose(CPUCollection, types.uuid, types.uuid, int,
                         wtypes.text, wtypes.text)
    def detail(self, host_uuid=None, marker=None, limit=None,
               sort_key='id', sort_dir='asc'):
        """Retrieve a list of cpus with detail."""
        # NOTE(lucasagomes): /detail should only work agaist collections
        parent = pecan.request.path.split('/')[:-1][-1]
        if parent != "cpus":
            raise exception.HTTPNotFound

        expand = True
        resource_url = '/'.join(['cpus', 'detail'])
        return self._get_cpus_collection(host_uuid, marker, limit, sort_key,
                                         sort_dir, expand, resource_url)

    @wsme_pecan.wsexpose(CPU, types.uuid)
    def get_one(self, cpu_uuid):
        """Retrieve information about the given cpu."""
        if self._from_hosts:
            raise exception.OperationNotPermitted

        rpc_port = objects.CPU.get_by_uuid(pecan.request.context, cpu_uuid)
        return CPU.convert_with_links(rpc_port)
