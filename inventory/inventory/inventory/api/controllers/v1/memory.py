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
#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import jsonpatch
import six

import pecan
from pecan import rest

import wsme
from wsme import types as wtypes
import wsmeext.pecan as wsme_pecan

from inventory.api.controllers.v1 import base
from inventory.api.controllers.v1 import collection
from inventory.api.controllers.v1 import link
from inventory.api.controllers.v1 import types
from inventory.api.controllers.v1 import utils
from inventory.common import exception
from inventory.common.i18n import _
from inventory.common import utils as cutils
from inventory import objects
from oslo_log import log


LOG = log.getLogger(__name__)


class MemoryPatchType(types.JsonPatchType):

    @staticmethod
    def mandatory_attrs():
        return []


class Memory(base.APIBase):
    """API representation of host memory.

    This class enforces type checking and value constraints, and converts
    between the internal object model and the API representation of a memory.
    """

    _minimum_platform_reserved_mib = None

    def _get_minimum_platform_reserved_mib(self):
        return self._minimum_platform_reserved_mib

    def _set_minimum_platform_reserved_mib(self, value):
        if self._minimum_platform_reserved_mib is None:
            try:
                ihost = objects.Host.get_by_uuid(pecan.request.context, value)
                self._minimum_platform_reserved_mib = \
                    cutils.get_minimum_platform_reserved_memory(ihost,
                                                                self.numa_node)
            except exception.HostNotFound as e:
                # Change error code because 404 (NotFound) is inappropriate
                # response for a POST request to create
                e.code = 400  # BadRequest
                raise e
        elif value == wtypes.Unset:
            self._minimum_platform_reserved_mib = wtypes.Unset

    uuid = types.uuid
    "Unique UUID for this memory"

    memtotal_mib = int
    "Represent the imemory total in MiB"

    memavail_mib = int
    "Represent the imemory available in MiB"

    platform_reserved_mib = int
    "Represent the imemory platform reserved in MiB"

    hugepages_configured = wtypes.text
    "Represent whether huge pages are configured"

    vswitch_hugepages_size_mib = int
    "Represent the imemory vswitch huge pages size in MiB"

    vswitch_hugepages_reqd = int
    "Represent the imemory vswitch required number of hugepages"

    vswitch_hugepages_nr = int
    "Represent the imemory vswitch number of hugepages"

    vswitch_hugepages_avail = int
    "Represent the imemory vswitch number of hugepages available"

    vm_hugepages_nr_2M_pending = int
    "Represent the imemory vm number of hugepages pending (2M pages)"

    vm_hugepages_nr_2M = int
    "Represent the imemory vm number of hugepages (2M pages)"

    vm_hugepages_avail_2M = int
    "Represent the imemory vm number of hugepages available (2M pages)"

    vm_hugepages_nr_1G_pending = int
    "Represent the imemory vm number of hugepages pending (1G pages)"

    vm_hugepages_nr_1G = int
    "Represent the imemory vm number of hugepages (1G pages)"

    vm_hugepages_nr_4K = int
    "Represent the imemory vm number of hugepages (4K pages)"

    vm_hugepages_use_1G = wtypes.text
    "1G hugepage is supported 'True' or not 'False' "

    vm_hugepages_avail_1G = int
    "Represent the imemory vm number of hugepages available (1G pages)"

    vm_hugepages_possible_2M = int
    "Represent the total possible number of vm hugepages available (2M pages)"

    vm_hugepages_possible_1G = int
    "Represent the total possible number of vm hugepages available (1G pages)"

    minimum_platform_reserved_mib = wsme.wsproperty(
        int,
        _get_minimum_platform_reserved_mib,
        _set_minimum_platform_reserved_mib,
        mandatory=True)
    "Represent the default platform reserved memory in MiB. API only attribute"

    numa_node = int
    "The numa node or zone the imemory. API only attribute"

    capabilities = {wtypes.text: utils.ValidTypes(wtypes.text,
                    six.integer_types)}
    "This memory's meta data"

    host_id = int
    "The ihostid that this imemory belongs to"

    node_id = int
    "The nodeId that this imemory belongs to"

    ihost_uuid = types.uuid
    "The UUID of the ihost this memory belongs to"

    node_uuid = types.uuid
    "The UUID of the node this memory belongs to"

    links = [link.Link]
    "A list containing a self link and associated memory links"

    def __init__(self, **kwargs):
        self.fields = objects.Memory.fields.keys()
        for k in self.fields:
            setattr(self, k, kwargs.get(k))

        # API only attributes
        self.fields.append('minimum_platform_reserved_mib')
        setattr(self, 'minimum_platform_reserved_mib',
                kwargs.get('host_id', None))

    @classmethod
    def convert_with_links(cls, rpc_mem, expand=True):
        # fields = ['uuid', 'address'] if not expand else None
        # memory = imemory.from_rpc_object(rpc_mem, fields)

        memory = Memory(**rpc_mem.as_dict())
        if not expand:
            memory.unset_fields_except(
                ['uuid', 'memtotal_mib', 'memavail_mib',
                 'platform_reserved_mib', 'hugepages_configured',
                 'vswitch_hugepages_size_mib', 'vswitch_hugepages_nr',
                 'vswitch_hugepages_reqd',
                 'vswitch_hugepages_avail',
                 'vm_hugepages_nr_2M',
                 'vm_hugepages_nr_1G', 'vm_hugepages_use_1G',
                 'vm_hugepages_nr_2M_pending',
                 'vm_hugepages_avail_2M',
                 'vm_hugepages_nr_1G_pending',
                 'vm_hugepages_avail_1G',
                 'vm_hugepages_nr_4K',
                 'vm_hugepages_possible_2M', 'vm_hugepages_possible_1G',
                 'numa_node', 'ihost_uuid', 'node_uuid',
                 'host_id', 'node_id',
                 'capabilities',
                 'created_at', 'updated_at',
                 'minimum_platform_reserved_mib'])

        # never expose the id attribute
        memory.host_id = wtypes.Unset
        memory.node_id = wtypes.Unset

        memory.links = [link.Link.make_link('self', pecan.request.host_url,
                                            'memorys', memory.uuid),
                        link.Link.make_link('bookmark',
                                            pecan.request.host_url,
                                            'memorys', memory.uuid,
                                            bookmark=True)
                        ]
        return memory


class MemoryCollection(collection.Collection):
    """API representation of a collection of memorys."""

    memorys = [Memory]
    "A list containing memory objects"

    def __init__(self, **kwargs):
        self._type = 'memorys'

    @classmethod
    def convert_with_links(cls, memorys, limit, url=None,
                           expand=False, **kwargs):
        collection = MemoryCollection()
        collection.memorys = [
            Memory.convert_with_links(n, expand) for n in memorys]
        collection.next = collection.get_next(limit, url=url, **kwargs)
        return collection


LOCK_NAME = 'MemoryController'


class MemoryController(rest.RestController):
    """REST controller for memorys."""

    _custom_actions = {
        'detail': ['GET'],
    }

    def __init__(self, from_hosts=False, from_node=False):
        self._from_hosts = from_hosts
        self._from_node = from_node

    def _get_memorys_collection(self, i_uuid, node_uuid,
                                marker, limit, sort_key, sort_dir,
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
            marker_obj = objects.Memory.get_by_uuid(pecan.request.context,
                                                    marker)

        if self._from_hosts:
            # memorys = pecan.request.dbapi.imemory_get_by_ihost(
            memorys = objects.Memory.get_by_host(
                pecan.request.context,
                i_uuid, limit,
                marker_obj,
                sort_key=sort_key,
                sort_dir=sort_dir)

        elif self._from_node:
            # memorys = pecan.request.dbapi.imemory_get_by_node(
            memorys = objects.Memory.get_by_node(
                pecan.request.context,
                i_uuid, limit,
                marker_obj,
                sort_key=sort_key,
                sort_dir=sort_dir)
        else:
            if i_uuid and not node_uuid:
                # memorys = pecan.request.dbapi.imemory_get_by_ihost(
                memorys = objects.Memory.get_by_host(
                    pecan.request.context,
                    i_uuid, limit,
                    marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)
            elif i_uuid and node_uuid:   # Need ihost_uuid ?
                # memorys = pecan.request.dbapi.imemory_get_by_ihost_node(
                memorys = objects.Memory.get_by_host_node(
                    pecan.request.context,
                    i_uuid,
                    node_uuid,
                    limit,
                    marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)
            elif node_uuid:
                # memorys = pecan.request.dbapi.imemory_get_by_ihost_node(
                memorys = objects.Memory.get_by_node(
                    pecan.request.context,
                    node_uuid,
                    limit,
                    marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)
            else:
                # memorys = pecan.request.dbapi.imemory_get_list(
                memorys = objects.Memory.list(
                    pecan.request.context,
                    limit,
                    marker_obj,
                    sort_key=sort_key,
                    sort_dir=sort_dir)

        return MemoryCollection.convert_with_links(memorys, limit,
                                                   url=resource_url,
                                                   expand=expand,
                                                   sort_key=sort_key,
                                                   sort_dir=sort_dir)

    @wsme_pecan.wsexpose(MemoryCollection, types.uuid, types.uuid,
                         types.uuid, int, wtypes.text, wtypes.text)
    def get_all(self, ihost_uuid=None, node_uuid=None,
                marker=None, limit=None, sort_key='id', sort_dir='asc'):
        """Retrieve a list of memorys."""

        return self._get_memorys_collection(
            ihost_uuid, node_uuid, marker, limit, sort_key, sort_dir)

    @wsme_pecan.wsexpose(MemoryCollection, types.uuid, types.uuid, int,
                         wtypes.text, wtypes.text)
    def detail(self, ihost_uuid=None, marker=None, limit=None,
               sort_key='id', sort_dir='asc'):
        """Retrieve a list of memorys with detail."""
        # NOTE(lucasagomes): /detail should only work agaist collections
        parent = pecan.request.path.split('/')[:-1][-1]
        if parent != "memorys":
            raise exception.HTTPNotFound

        expand = True
        resource_url = '/'.join(['memorys', 'detail'])
        return self._get_memorys_collection(ihost_uuid, marker, limit,
                                            sort_key, sort_dir,
                                            expand, resource_url)

    @wsme_pecan.wsexpose(Memory, types.uuid)
    def get_one(self, memory_uuid):
        """Retrieve information about the given memory."""
        if self._from_hosts:
            raise exception.OperationNotPermitted

        rpc_mem = objects.Memory.get_by_uuid(pecan.request.context,
                                             memory_uuid)
        return Memory.convert_with_links(rpc_mem)

    @cutils.synchronized(LOCK_NAME)
    @wsme_pecan.wsexpose(Memory, body=Memory)
    def post(self, memory):
        """Create a new memory."""
        if self._from_hosts:
            raise exception.OperationNotPermitted

        try:
            ihost_uuid = memory.ihost_uuid
            new_memory = pecan.request.dbapi.imemory_create(ihost_uuid,
                                                            memory.as_dict())

        except exception.InventoryException as e:
            LOG.exception(e)
            raise wsme.exc.ClientSideError(_("Invalid data"))
        return Memory.convert_with_links(new_memory)

    @cutils.synchronized(LOCK_NAME)
    @wsme.validate(types.uuid, [MemoryPatchType])
    @wsme_pecan.wsexpose(Memory, types.uuid,
                         body=[MemoryPatchType])
    def patch(self, memory_uuid, patch):
        """Update an existing memory."""
        if self._from_hosts:
            raise exception.OperationNotPermitted

        rpc_mem = objects.Memory.get_by_uuid(
            pecan.request.context, memory_uuid)

        if 'host_id' in rpc_mem:
            ihostId = rpc_mem['host_id']
        else:
            ihostId = rpc_mem['ihost_uuid']

        host_id = pecan.request.dbapi.ihost_get(ihostId)

        vm_hugepages_nr_2M_pending = None
        vm_hugepages_nr_1G_pending = None
        platform_reserved_mib = None
        for p in patch:
            if p['path'] == '/platform_reserved_mib':
                platform_reserved_mib = p['value']
            if p['path'] == '/vm_hugepages_nr_2M_pending':
                vm_hugepages_nr_2M_pending = p['value']

            if p['path'] == '/vm_hugepages_nr_1G_pending':
                vm_hugepages_nr_1G_pending = p['value']

        # The host must be locked
        if host_id:
            _check_host(host_id)
        else:
            raise wsme.exc.ClientSideError(_(
                "Hostname or uuid must be defined"))

        try:
            # Semantics checks and update hugepage memory accounting
            patch = _check_huge_values(
                rpc_mem, patch,
                vm_hugepages_nr_2M_pending, vm_hugepages_nr_1G_pending)
        except wsme.exc.ClientSideError as e:
            node = pecan.request.dbapi.node_get(node_id=rpc_mem.node_id)
            numa_node = node.numa_node
            msg = _('Processor {0}:').format(numa_node) + e.message
            raise wsme.exc.ClientSideError(msg)

        # Semantics checks for platform memory
        _check_memory(rpc_mem, host_id, platform_reserved_mib,
                      vm_hugepages_nr_2M_pending, vm_hugepages_nr_1G_pending)

        # only allow patching allocated_function and capabilities
        # replace ihost_uuid and node_uuid with corresponding
        patch_obj = jsonpatch.JsonPatch(patch)

        for p in patch_obj:
            if p['path'] == '/ihost_uuid':
                p['path'] = '/host_id'
                ihost = objects.Host.get_by_uuid(pecan.request.context,
                                                 p['value'])
                p['value'] = ihost.id

            if p['path'] == '/node_uuid':
                p['path'] = '/node_id'
                try:
                    node = objects.Node.get_by_uuid(
                        pecan.request.context, p['value'])
                    p['value'] = node.id
                except exception.InventoryException:
                    p['value'] = None

        try:
            memory = Memory(**jsonpatch.apply_patch(rpc_mem.as_dict(),
                                                    patch_obj))

        except utils.JSONPATCH_EXCEPTIONS as e:
            raise exception.PatchError(patch=patch, reason=e)

        # Update only the fields that have changed
        for field in objects.Memory.fields:
            if rpc_mem[field] != getattr(memory, field):
                rpc_mem[field] = getattr(memory, field)

        rpc_mem.save()
        return Memory.convert_with_links(rpc_mem)

    @cutils.synchronized(LOCK_NAME)
    @wsme_pecan.wsexpose(None, types.uuid, status_code=204)
    def delete(self, memory_uuid):
        """Delete a memory."""
        if self._from_hosts:
            raise exception.OperationNotPermitted

        pecan.request.dbapi.imemory_destroy(memory_uuid)

##############
# UTILS
##############


def _update(mem_uuid, mem_values):

    rpc_mem = objects.Memory.get_by_uuid(pecan.request.context, mem_uuid)
    if 'host_id' in rpc_mem:
        ihostId = rpc_mem['host_id']
    else:
        ihostId = rpc_mem['ihost_uuid']

    host_id = pecan.request.dbapi.ihost_get(ihostId)

    if 'platform_reserved_mib' in mem_values:
        platform_reserved_mib = mem_values['platform_reserved_mib']

    if 'vm_hugepages_nr_2M_pending' in mem_values:
        vm_hugepages_nr_2M_pending = mem_values['vm_hugepages_nr_2M_pending']

    if 'vm_hugepages_nr_1G_pending' in mem_values:
        vm_hugepages_nr_1G_pending = mem_values['vm_hugepages_nr_1G_pending']

        # The host must be locked
        if host_id:
            _check_host(host_id)
        else:
            raise wsme.exc.ClientSideError((
                "Hostname or uuid must be defined"))

        # Semantics checks and update hugepage memory accounting
        mem_values = _check_huge_values(
            rpc_mem, mem_values,
            vm_hugepages_nr_2M_pending, vm_hugepages_nr_1G_pending)

        # Semantics checks for platform memory
        _check_memory(rpc_mem, host_id, platform_reserved_mib,
                      vm_hugepages_nr_2M_pending, vm_hugepages_nr_1G_pending)

        # update memory values
        pecan.request.dbapi.imemory_update(mem_uuid, mem_values)


def _check_host(ihost):
    if utils.is_aio_simplex_host_unlocked(ihost):
        raise wsme.exc.ClientSideError(_("Host must be locked."))
    elif ihost['administrative'] != 'locked':
        unlocked = False
        current_ihosts = pecan.request.dbapi.ihost_get_list()
        for h in current_ihosts:
            if (h['administrative'] != 'locked' and
                    h['hostname'] != ihost['hostname']):
                unlocked = True
        if unlocked:
            raise wsme.exc.ClientSideError(_("Host must be locked."))


def _check_memory(rpc_mem, ihost,
                  platform_reserved_mib=None,
                  vm_hugepages_nr_2M_pending=None,
                  vm_hugepages_nr_1G_pending=None):
    if platform_reserved_mib:
        # Check for invalid characters
        try:
            val = int(platform_reserved_mib)
        except ValueError:
            raise wsme.exc.ClientSideError((
                "Platform memory must be a number"))
        if val < 0:
            raise wsme.exc.ClientSideError((
                "Platform memory must be greater than zero"))

        # Check for lower limit
        node_id = rpc_mem['node_id']
        node = pecan.request.dbapi.node_get(node_id)
        min_platform_memory = \
            cutils.get_minimum_platform_reserved_memory(ihost, node.numa_node)
        if int(platform_reserved_mib) < min_platform_memory:
            raise wsme.exc.ClientSideError(
                _("Platform reserved memory for numa node {} "
                  "must be greater than the minimum value {}").format(
                    (node.numa_node, min_platform_memory)))

        # Check if it is within 2/3 percent of the total memory
        node_memtotal_mib = rpc_mem['node_memtotal_mib']
        max_platform_reserved = node_memtotal_mib * 2 / 3
        if int(platform_reserved_mib) > max_platform_reserved:
            low_core = cutils.is_low_core_system(ihost, pecan.request.dbapi)
            required_platform_reserved = \
                cutils.get_required_platform_reserved_memory(
                    ihost, node.numa_node, low_core)
            msg_platform_over = (
                _("Platform reserved memory {} MiB on node {} "
                  "is not within range [{}, {}]").format(
                    (int(platform_reserved_mib),
                     node.numa_node,
                     required_platform_reserved,
                     max_platform_reserved)))

            if cutils.is_virtual() or cutils.is_virtual_compute(ihost):
                LOG.warn(msg_platform_over)
            else:
                raise wsme.exc.ClientSideError(msg_platform_over)

        # Check if it is within the total amount of memory
        mem_alloc = 0
        if vm_hugepages_nr_2M_pending:
            mem_alloc += int(vm_hugepages_nr_2M_pending) * 2
        elif rpc_mem['vm_hugepages_nr_2M']:
            mem_alloc += int(rpc_mem['vm_hugepages_nr_2M']) * 2
        if vm_hugepages_nr_1G_pending:
            mem_alloc += int(vm_hugepages_nr_1G_pending) * 1000
        elif rpc_mem['vm_hugepages_nr_1G']:
            mem_alloc += int(rpc_mem['vm_hugepages_nr_1G']) * 1000
        LOG.debug("vm total=%s" % (mem_alloc))

        vs_hp_size = rpc_mem['vswitch_hugepages_size_mib']
        vs_hp_nr = rpc_mem['vswitch_hugepages_nr']
        mem_alloc += vs_hp_size * vs_hp_nr
        LOG.debug("vs_hp_nr=%s vs_hp_size=%s" % (vs_hp_nr, vs_hp_size))
        LOG.debug("memTotal %s mem_alloc %s" % (node_memtotal_mib, mem_alloc))

        # Initial configuration defaults mem_alloc to consume 100% of 2M pages,
        # so we may marginally exceed available non-huge memory.
        # Note there will be some variability in total available memory,
        # so we need to allow some tolerance so we do not hit the limit.
        avail = node_memtotal_mib - mem_alloc
        delta = int(platform_reserved_mib) - avail
        mem_thresh = 32
        if int(platform_reserved_mib) > avail + mem_thresh:
            msg = (_("Platform reserved memory {} MiB exceeds {} MiB "
                     "available by {} MiB (2M: {} pages; 1G: {} pages). "
                     "total memory={} MiB, allocated={} MiB.").format(
                (platform_reserved_mib, avail,
                 delta, delta / 2, delta / 1024,
                 node_memtotal_mib, mem_alloc)))
            raise wsme.exc.ClientSideError(msg)
        else:
            msg = (_("Platform reserved memory {} MiB, {} MiB available, "
                     "total memory={} MiB, allocated={} MiB.").format(
                platform_reserved_mib, avail,
                node_memtotal_mib, mem_alloc))
            LOG.info(msg)


def _check_huge_values(rpc_mem, patch, vm_hugepages_nr_2M=None,
                       vm_hugepages_nr_1G=None):

    if rpc_mem['vm_hugepages_use_1G'] == 'False' and vm_hugepages_nr_1G:
        # cannot provision 1G huge pages if the processor does not support them
        raise wsme.exc.ClientSideError(_(
            "Processor does not support 1G huge pages."))

    # Check for invalid characters
    if vm_hugepages_nr_2M:
        try:
            val = int(vm_hugepages_nr_2M)
        except ValueError:
            raise wsme.exc.ClientSideError(_(
                "VM huge pages 2M must be a number"))
        if int(vm_hugepages_nr_2M) < 0:
            raise wsme.exc.ClientSideError(_(
                "VM huge pages 2M must be greater than or equal to zero"))

    if vm_hugepages_nr_1G:
        try:
            val = int(vm_hugepages_nr_1G)
        except ValueError:
            raise wsme.exc.ClientSideError(_(
                "VM huge pages 1G must be a number"))
        if val < 0:
            raise wsme.exc.ClientSideError(_(
                "VM huge pages 1G must be greater than or equal to zero"))

    # Check to make sure that the huge pages aren't over committed
    if rpc_mem['vm_hugepages_possible_2M'] is None and vm_hugepages_nr_2M:
        raise wsme.exc.ClientSideError(_(
            "No available space for 2M huge page allocation"))

    if rpc_mem['vm_hugepages_possible_1G'] is None and vm_hugepages_nr_1G:
        raise wsme.exc.ClientSideError(_(
            "No available space for 1G huge page allocation"))

    # Update the number of available huge pages
    num_2M_for_1G = 512

    # None == unchanged
    if vm_hugepages_nr_1G is not None:
        new_1G_pages = int(vm_hugepages_nr_1G)
    elif rpc_mem['vm_hugepages_nr_1G_pending']:
        new_1G_pages = int(rpc_mem['vm_hugepages_nr_1G_pending'])
    elif rpc_mem['vm_hugepages_nr_1G']:
        new_1G_pages = int(rpc_mem['vm_hugepages_nr_1G'])
    else:
        new_1G_pages = 0

    # None == unchanged
    if vm_hugepages_nr_2M is not None:
        new_2M_pages = int(vm_hugepages_nr_2M)
    elif rpc_mem['vm_hugepages_nr_2M_pending']:
        new_2M_pages = int(rpc_mem['vm_hugepages_nr_2M_pending'])
    elif rpc_mem['vm_hugepages_nr_2M']:
        new_2M_pages = int(rpc_mem['vm_hugepages_nr_2M'])
    else:
        new_2M_pages = 0

    LOG.debug('new 2M pages: %s, 1G pages: %s' % (new_2M_pages, new_1G_pages))
    vm_possible_2M = 0
    vm_possible_1G = 0
    if rpc_mem['vm_hugepages_possible_2M']:
        vm_possible_2M = int(rpc_mem['vm_hugepages_possible_2M'])

    if rpc_mem['vm_hugepages_possible_1G']:
        vm_possible_1G = int(rpc_mem['vm_hugepages_possible_1G'])

    LOG.debug("max possible 2M pages: %s, max possible 1G pages: %s" %
              (vm_possible_2M, vm_possible_1G))

    if vm_possible_2M < new_2M_pages:
        msg = _("No available space for 2M huge page allocation, "
                "max 2M pages: %d") % vm_possible_2M
        raise wsme.exc.ClientSideError(msg)

    if vm_possible_1G < new_1G_pages:
        msg = _("No available space for 1G huge page allocation, "
                "max 1G pages: %d") % vm_possible_1G
        raise wsme.exc.ClientSideError(msg)

    # always use vm_possible_2M to compare,
    if vm_possible_2M < (new_2M_pages + new_1G_pages * num_2M_for_1G):
        max_1G = int((vm_possible_2M - new_2M_pages) / num_2M_for_1G)
        max_2M = vm_possible_2M - new_1G_pages * num_2M_for_1G
        if new_2M_pages > 0 and new_1G_pages > 0:
            msg = _("No available space for new settings."
                    "Max 1G pages is {} when 2M is {}, or "
                    "Max 2M pages is %s when 1G is {}.").format(
                        max_1G, new_2M_pages, max_2M, new_1G_pages)
        elif new_1G_pages > 0:
            msg = _("No available space for 1G huge page allocation, "
                    "max 1G pages: %d") % vm_possible_1G
        else:
            msg = _("No available space for 2M huge page allocation, "
                    "max 2M pages: %d") % vm_possible_2M

        raise wsme.exc.ClientSideError(msg)

    return patch
