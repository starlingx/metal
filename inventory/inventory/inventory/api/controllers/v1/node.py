# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
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
# Copyright (c) 2013-2016 Wind River Systems, Inc.
#


import six

import pecan
from pecan import rest

from wsme import types as wtypes
import wsmeext.pecan as wsme_pecan

from inventory.api.controllers.v1 import base
from inventory.api.controllers.v1 import collection
from inventory.api.controllers.v1 import cpu
from inventory.api.controllers.v1 import link
from inventory.api.controllers.v1 import memory
from inventory.api.controllers.v1 import port
from inventory.api.controllers.v1 import types
from inventory.api.controllers.v1 import utils
from inventory.common import exception
from inventory.common.i18n import _
from inventory import objects

from oslo_log import log

LOG = log.getLogger(__name__)


class NodePatchType(types.JsonPatchType):

    @staticmethod
    def mandatory_attrs():
        return ['/address', '/host_uuid']


class Node(base.APIBase):
    """API representation of a host node.

    This class enforces type checking and value constraints, and converts
    between the internal object model and the API representation of
    an node.
    """

    uuid = types.uuid
    "Unique UUID for this node"

    numa_node = int
    "numa node zone for this node"

    capabilities = {wtypes.text: utils.ValidTypes(wtypes.text,
                    six.integer_types)}
    "This node's meta data"

    host_id = int
    "The hostid that this node belongs to"

    host_uuid = types.uuid
    "The UUID of the host this node belongs to"

    links = [link.Link]
    "A list containing a self link and associated node links"

    icpus = [link.Link]
    "Links to the collection of cpus on this node"

    imemorys = [link.Link]
    "Links to the collection of memorys on this node"

    ports = [link.Link]
    "Links to the collection of ports on this node"

    def __init__(self, **kwargs):
        self.fields = objects.Node.fields.keys()
        for k in self.fields:
            setattr(self, k, kwargs.get(k))

    @classmethod
    def convert_with_links(cls, rpc_node, expand=True):
        minimum_fields = ['uuid', 'numa_node', 'capabilities',
                          'host_uuid', 'host_id',
                          'created_at'] if not expand else None
        fields = minimum_fields if not expand else None

        node = Node.from_rpc_object(rpc_node, fields)

        # never expose the host_id attribute
        node.host_id = wtypes.Unset

        node.links = [link.Link.make_link('self', pecan.request.host_url,
                                          'nodes', node.uuid),
                      link.Link.make_link('bookmark',
                                          pecan.request.host_url,
                                          'nodes', node.uuid,
                                          bookmark=True)
                      ]
        if expand:
            node.icpus = [link.Link.make_link('self',
                                              pecan.request.host_url,
                                              'nodes',
                                              node.uuid + "/cpus"),
                          link.Link.make_link('bookmark',
                                              pecan.request.host_url,
                                              'nodes',
                                              node.uuid + "/cpus",
                                              bookmark=True)
                          ]

            node.imemorys = [link.Link.make_link('self',
                                                 pecan.request.host_url,
                                                 'nodes',
                                                 node.uuid + "/memorys"),
                             link.Link.make_link('bookmark',
                                                 pecan.request.host_url,
                                                 'nodes',
                                                 node.uuid + "/memorys",
                                                 bookmark=True)
                             ]

            node.ports = [link.Link.make_link('self',
                                              pecan.request.host_url,
                                              'nodes',
                                              node.uuid + "/ports"),
                          link.Link.make_link('bookmark',
                                              pecan.request.host_url,
                                              'nodes',
                                              node.uuid + "/ports",
                                              bookmark=True)
                          ]

        return node


class NodeCollection(collection.Collection):
    """API representation of a collection of nodes."""

    nodes = [Node]
    "A list containing node objects"

    def __init__(self, **kwargs):
        self._type = 'nodes'

    @classmethod
    def convert_with_links(cls, rpc_nodes, limit, url=None,
                           expand=False, **kwargs):
        collection = NodeCollection()
        collection.nodes = [Node.convert_with_links(p, expand)
                            for p in rpc_nodes]
        collection.next = collection.get_next(limit, url=url, **kwargs)
        return collection


LOCK_NAME = 'NodeController'


class NodeController(rest.RestController):
    """REST controller for nodes."""

    icpus = cpu.CPUController(from_node=True)
    "Expose cpus as a sub-element of nodes"

    imemorys = memory.MemoryController(from_node=True)
    "Expose memorys as a sub-element of nodes"

    ports = port.PortController(from_node=True)
    "Expose ports as a sub-element of nodes"

    _custom_actions = {
        'detail': ['GET'],
    }

    def __init__(self, from_hosts=False):
        self._from_hosts = from_hosts

    def _get_nodes_collection(self, host_uuid, marker, limit, sort_key,
                              sort_dir, expand=False, resource_url=None):
        if self._from_hosts and not host_uuid:
            raise exception.InvalidParameterValue(_(
                "Host id not specified."))

        limit = utils.validate_limit(limit)
        sort_dir = utils.validate_sort_dir(sort_dir)

        marker_obj = None
        if marker:
            marker_obj = objects.Node.get_by_uuid(pecan.request.context,
                                                  marker)

        if host_uuid:
            nodes = objects.Node.get_by_host(pecan.request.context,
                                             host_uuid,
                                             limit,
                                             marker=marker_obj,
                                             sort_key=sort_key,
                                             sort_dir=sort_dir)
        else:
            nodes = objects.Node.list(pecan.request.context,
                                      limit,
                                      marker=marker_obj,
                                      sort_key=sort_key,
                                      sort_dir=sort_dir)

        return NodeCollection.convert_with_links(nodes, limit,
                                                 url=resource_url,
                                                 expand=expand,
                                                 sort_key=sort_key,
                                                 sort_dir=sort_dir)

    @wsme_pecan.wsexpose(NodeCollection,
                         types.uuid, types.uuid, int, wtypes.text, wtypes.text)
    def get_all(self, host_uuid=None, marker=None, limit=None,
                sort_key='id', sort_dir='asc'):
        """Retrieve a list of nodes."""

        return self._get_nodes_collection(host_uuid, marker, limit,
                                          sort_key, sort_dir)

    @wsme_pecan.wsexpose(NodeCollection, types.uuid, types.uuid, int,
                         wtypes.text, wtypes.text)
    def detail(self, host_uuid=None, marker=None, limit=None,
               sort_key='id', sort_dir='asc'):
        """Retrieve a list of nodes with detail."""
        # NOTE(lucasagomes): /detail should only work agaist collections
        parent = pecan.request.path.split('/')[:-1][-1]
        if parent != "nodes":
            raise exception.HTTPNotFound

        expand = True
        resource_url = '/'.join(['nodes', 'detail'])
        return self._get_nodes_collection(host_uuid,
                                          marker, limit,
                                          sort_key, sort_dir,
                                          expand, resource_url)

    @wsme_pecan.wsexpose(Node, types.uuid)
    def get_one(self, node_uuid):
        """Retrieve information about the given node."""

        if self._from_hosts:
            raise exception.OperationNotPermitted

        rpc_node = objects.Node.get_by_uuid(pecan.request.context, node_uuid)
        return Node.convert_with_links(rpc_node)
