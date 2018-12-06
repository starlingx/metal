#!/usr/bin/env python
#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4

# All Rights Reserved.
#

from inventoryclient.common import utils
from inventoryclient import exc
from inventoryclient.v1 import host as host_utils


def _print_cpu_show(cpu):
    fields = ['cpu', 'numa_node', 'core', 'thread',
              'cpu_model', 'cpu_family',
              'capabilities',
              'uuid', 'host_uuid', 'node_uuid',
              'created_at', 'updated_at']
    labels = ['logical_core', 'processor (numa_node)', 'physical_core',
              'thread', 'processor_model', 'processor_family',
              'capabilities',
              'uuid', 'host_uuid', 'node_uuid',
              'created_at', 'updated_at']
    data = [(f, getattr(cpu, f, '')) for f in fields]
    utils.print_tuple_list(data, labels)


def _find_cpu(cc, host, cpunameoruuid):
    cpus = cc.cpu.list(host.uuid)

    if cpunameoruuid.isdigit():
        cpunameoruuid = int(cpunameoruuid)

    for c in cpus:
        if c.uuid == cpunameoruuid or c.cpu == cpunameoruuid:
            break
    else:
        raise exc.CommandError('CPU logical core not found: host %s cpu %s' %
                               (host.hostname, cpunameoruuid))
    return c


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
@utils.arg('cpulcoreoruuid',
           metavar='<cpu l_core or uuid>',
           help="CPU logical core ID or UUID of cpu")
def do_host_cpu_show(cc, args):
    """Show cpu core attributes."""
    host = host_utils._find_host(cc, args.hostnameorid)
    cpu = _find_cpu(cc, host, args.cpulcoreoruuid)
    _print_cpu_show(cpu)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
def do_host_cpu_list(cc, args):
    """List cpu cores."""

    host = host_utils._find_host(cc, args.hostnameorid)

    cpus = cc.cpu.list(host.uuid)

    field_labels = ['uuid', 'log_core', 'processor', 'phy_core', 'thread',
                    'processor_model']
    fields = ['uuid', 'cpu', 'numa_node', 'core', 'thread',
              'cpu_model']

    utils.print_list(cpus, fields, field_labels, sortby=1)
