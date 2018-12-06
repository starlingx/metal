#
# Copyright (c) 2013-2014 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# -*- encoding: utf-8 -*-
#

from inventoryclient.common import base
from inventoryclient import exc
import json

CREATION_ATTRIBUTES = ['host_uuid', 'memtotal_mib', 'memavail_mib',
                       'platform_reserved_mib', 'hugepages_configured',
                       'vswitch_hugepages_size_mib', 'vswitch_hugepages_reqd',
                       'vswitch_hugepages_nr', 'vswitch_hugepages_avail',
                       'vm_hugepages_nr_2M_pending',
                       'vm_hugepages_nr_1G_pending',
                       'vm_hugepages_nr_2M', 'vm_hugepages_avail_2M',
                       'vm_hugepages_nr_1G', 'vm_hugepages_avail_1G',
                       'vm_hugepages_avail_1G', 'vm_hugepages_use_1G',
                       'vm_hugepages_possible_2M', 'vm_hugepages_possible_1G',
                       'capabilities', 'numa_node',
                       'minimum_platform_reserved_mib']


class Memory(base.Resource):
    def __repr__(self):
        return "<memory %s>" % self._info


class MemoryManager(base.Manager):
    resource_class = Memory

    @staticmethod
    def _path(id=None):
        return '/v1/memorys/%s' % id if id else '/v1/memorys'

    def list(self, host_id):
        path = '/v1/hosts/%s/memorys' % host_id
        return self._list(path, "memorys")

    def get(self, memory_id):
        path = '/v1/memorys/%s' % memory_id
        try:
            return self._list(path)[0]
        except IndexError:
            return None

    def update(self, memory_id, patch):
        return self._update(self._path(memory_id),
                            data=(json.dumps(patch)))

    def create(self, **kwargs):
        path = '/v1/memorys'
        new = {}
        for (key, value) in kwargs.items():
            if key in CREATION_ATTRIBUTES:
                new[key] = value
            else:
                raise exc.InvalidAttribute('%s' % key)
        return self._create(path, new)
