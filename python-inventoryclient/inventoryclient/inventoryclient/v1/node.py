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


CREATION_ATTRIBUTES = ['numa_node', 'capabilities', 'host_uuid']


class Node(base.Resource):
    def __repr__(self):
        return "<node %s>" % self._info


class NodeManager(base.Manager):
    resource_class = Node

    def list(self, host_id):
        path = '/v1/hosts/%s/nodes' % host_id
        return self._list(path, "nodes")

    def get(self, node_id):
        path = '/v1/nodes/%s' % node_id
        try:
            return self._list(path)[0]
        except IndexError:
            return None

    def create(self, **kwargs):
        path = '/v1/nodes'
        new = {}
        for (key, value) in kwargs.items():
            if key in CREATION_ATTRIBUTES:
                new[key] = value
            else:
                raise exc.InvalidAttribute('%s' % key)
        return self._create(path, new)

    def delete(self, node_id):
        path = '/v1/nodes/%s' % node_id
        return self._delete(path)

    def update(self, node_id, patch):
        path = '/v1/nodes/%s' % node_id
        return self._update(path,
                            data=(json.dumps(patch)))
