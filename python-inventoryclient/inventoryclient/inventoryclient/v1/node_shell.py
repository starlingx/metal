#!/usr/bin/env python
#
# Copyright (c) 2013-2014 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4
# All Rights Reserved.
#

from inventoryclient.common import utils
from inventoryclient import exc
from inventoryclient.v1 import host as host_utils


def _print_node_show(node):
    fields = ['numa_node',
              'uuid', 'host_uuid',
              'created_at']
    data = [(f, getattr(node, f, '')) for f in fields]
    utils.print_tuple_list(data)


def _find_node(cc, host, nodeuuid):
    nodes = cc.node.list(host.uuid)
    for i in nodes:
        if i.uuid == nodeuuid:
            break
    else:
        raise exc.CommandError('Inode not found: host %s if %s' %
                               (host.hostname, nodeuuid))
    return i


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
@utils.arg('nodeuuid',
           metavar='<node name or uuid>',
           help="Name or UUID of node")
def donot_host_node_show(cc, args):
    """Show a node. DEBUG only"""
    host = host_utils._find_host(cc, args.hostnameorid)
    # API actually doesnt need hostid once it has node uuid

    i = _find_node(cc, host, args.nodeuuid)

    _print_node_show(i)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
def donot_host_node_list(cc, args):
    """List nodes.  DEBUG only"""
    host = host_utils._find_host(cc, args.hostnameorid)

    nodes = cc.node.list(host.uuid)

    field_labels = ['uuid', 'numa_node', 'capabilities']
    fields = ['uuid', 'numa_node', 'capabilities']
    utils.print_list(nodes, fields, field_labels, sortby=0)
