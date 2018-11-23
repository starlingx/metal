#!/usr/bin/env python
#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4

# All Rights Reserved.
#

from collections import OrderedDict
import datetime
import os

from inventoryclient.common.i18n import _
from inventoryclient.common import utils
from inventoryclient import exc
from inventoryclient.v1 import host as host_utils


def _print_host_show(host):
    fields = ['id', 'uuid', 'personality', 'hostname', 'invprovision',
              'administrative', 'operational', 'availability', 'task',
              'action', 'mgmt_mac', 'mgmt_ip', 'serialid',
              'capabilities', 'bm_type', 'bm_username', 'bm_ip',
              'location', 'uptime', 'reserved', 'created_at', 'updated_at',
              'boot_device', 'rootfs_device', 'install_output', 'console',
              'tboot', 'vim_progress_status', 'software_load', 'install_state',
              'install_state_info']
    optional_fields = ['ttys_dcd']
    if host.subfunctions != host.personality:
        fields.append('subfunctions')
        if 'controller' in host.subfunctions:
            fields.append('subfunction_oper')
            fields.append('subfunction_avail')

    # Do not display the trailing '+' which indicates the audit iterations
    if host.install_state_info:
        host.install_state_info = host.install_state_info.rstrip('+')
    if host.install_state:
        host.install_state = host.install_state.rstrip('+')

    data_list = [(f, getattr(host, f, '')) for f in fields]
    data_list += [(f, getattr(host, f, '')) for f in optional_fields
                  if hasattr(host, f)]
    data = dict(data_list)
    ordereddata = OrderedDict(sorted(data.items(), key=lambda t: t[0]))
    utils.print_dict(ordereddata, wrap=72)


@utils.arg('hostnameorid', metavar='<hostname or id>',
           help="Name or ID of host")
def do_host_show(cc, args):
    """Show host attributes."""
    host = host_utils._find_host(cc, args.hostnameorid)
    _print_host_show(host)


def do_host_list(cc, args):
    """List hosts."""
    hosts = cc.host.list()
    field_labels = ['id', 'hostname', 'personality',
                    'administrative', 'operational', 'availability']
    fields = ['id', 'hostname', 'personality',
              'administrative', 'operational', 'availability']
    utils.print_list(hosts, fields, field_labels, sortby=0)


@utils.arg('-n', '--hostname',
           metavar='<hostname>',
           help='Hostname of the host')
@utils.arg('-p', '--personality',
           metavar='<personality>',
           choices=['controller', 'compute', 'storage', 'network', 'profile'],
           help='Personality or type of host [REQUIRED]')
@utils.arg('-s', '--subfunctions',
           metavar='<subfunctions>',
           choices=['lowlatency'],
           help='Performance profile or subfunctions of host.[Optional]')
@utils.arg('-m', '--mgmt_mac',
           metavar='<mgmt_mac>',
           help='MAC Address of the host mgmt interface [REQUIRED]')
@utils.arg('-i', '--mgmt_ip',
           metavar='<mgmt_ip>',
           help='IP Address of the host mgmt interface (when using static '
                'address allocation)')
@utils.arg('-I', '--bm_ip',
           metavar='<bm_ip>',
           help="IP Address of the host board management interface, "
                "only necessary if this host's board management controller "
                "is not in the primary region")
@utils.arg('-T', '--bm_type',
           metavar='<bm_type>',
           help='Type of the host board management interface')
@utils.arg('-U', '--bm_username',
           metavar='<bm_username>',
           help='Username for the host board management interface')
@utils.arg('-P', '--bm_password',
           metavar='<bm_password>',
           help='Password for the host board management interface')
@utils.arg('-b', '--boot_device',
           metavar='<boot_device>',
           help='Device for boot partition, relative to /dev. Default: sda')
@utils.arg('-r', '--rootfs_device',
           metavar='<rootfs_device>',
           help='Device for rootfs partition, relative to /dev. Default: sda')
@utils.arg('-o', '--install_output',
           metavar='<install_output>',
           choices=['text', 'graphical'],
           help='Installation output format, text or graphical. Default: text')
@utils.arg('-c', '--console',
           metavar='<console>',
           help='Serial console. Default: ttyS0,115200')
@utils.arg('-l', '--location',
           metavar='<location>',
           help='Physical location of the host')
@utils.arg('-D', '--ttys_dcd',
           metavar='<true/false>',
           help='Enable/disable serial console data carrier detection')
def do_host_add(cc, args):
    """Add a new host."""
    field_list = ['hostname', 'personality', 'subfunctions',
                  'mgmt_mac', 'mgmt_ip',
                  'bm_ip', 'bm_type', 'bm_username', 'bm_password',
                  'boot_device', 'rootfs_device', 'install_output', 'console',
                  'location', 'ttys_dcd']
    fields = dict((k, v) for (k, v) in vars(args).items()
                  if k in field_list and not (v is None))

    # This is the expected format of the location field
    if 'location' in fields:
        fields['location'] = {"locn": fields['location']}

    host = cc.host.create(**fields)
    suuid = getattr(host, 'uuid', '')

    try:
        host = cc.host.get(suuid)
    except exc.HTTPNotFound:
        raise exc.CommandError('Host not found: %s' % suuid)
    else:
        _print_host_show(host)


@utils.arg('hostsfile',
           metavar='<hostsfile>',
           help='File containing the XML descriptions of hosts to be '
                'provisioned [REQUIRED]')
def do_host_bulk_add(cc, args):
    """Add multiple new hosts."""
    field_list = ['hostsfile']
    fields = dict((k, v) for (k, v) in vars(args).items()
                  if k in field_list and not (v is None))

    hostsfile = fields['hostsfile']
    if os.path.isdir(hostsfile):
        raise exc.CommandError("Error: %s is a directory." % hostsfile)
    try:
        req = open(hostsfile, 'rb')
    except Exception:
        raise exc.CommandError("Error: Could not open file %s." % hostsfile)

    response = cc.host.create_many(req)
    if not response:
        raise exc.CommandError("The request timed out or there was an "
                               "unknown error")
    success = response.get('success')
    error = response.get('error')
    if success:
        print("Success: " + success + "\n")
    if error:
        print("Error:\n" + error)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           nargs='+',
           help="Name or ID of host")
def do_host_delete(cc, args):
    """Delete a host."""
    for n in args.hostnameorid:
        try:
            cc.host.delete(n)
            print('Deleted host %s' % n)
        except exc.HTTPNotFound:
            raise exc.CommandError('host not found: %s' % n)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
@utils.arg('attributes',
           metavar='<path=value>',
           nargs='+',
           action='append',
           default=[],
           help="Attributes to update ")
def do_host_update(cc, args):
    """Update host attributes."""
    patch = utils.args_array_to_patch("replace", args.attributes[0])
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
@utils.arg('-f', '--force',
           action='store_true',
           default=False,
           help="Force a lock operation ")
def do_host_lock(cc, args):
    """Lock a host."""
    attributes = []

    if args.force is True:
        # Forced lock operation
        attributes.append('action=force-lock')
    else:
        # Normal lock operation
        attributes.append('action=lock')

    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
@utils.arg('-f', '--force',
           action='store_true',
           default=False,
           help="Force an unlock operation ")
def do_host_unlock(cc, args):
    """Unlock a host."""
    attributes = []

    if args.force is True:
        # Forced unlock operation
        attributes.append('action=force-unlock')
    else:
        # Normal unlock operation
        attributes.append('action=unlock')

    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
@utils.arg('-f', '--force',
           action='store_true',
           default=False,
           help="Force a host swact operation ")
def do_host_swact(cc, args):
    """Switch activity away from this active host."""
    attributes = []

    if args.force is True:
        # Forced swact operation
        attributes.append('action=force-swact')
    else:
        # Normal swact operation
        attributes.append('action=swact')

    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
def do_host_reset(cc, args):
    """Reset a host."""
    attributes = []
    attributes.append('action=reset')
    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
def do_host_reboot(cc, args):
    """Reboot a host."""
    attributes = []
    attributes.append('action=reboot')
    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
def do_host_reinstall(cc, args):
    """Reinstall a host."""
    attributes = []
    attributes.append('action=reinstall')
    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
def do_host_power_on(cc, args):
    """Power on a host."""
    attributes = []
    attributes.append('action=power-on')
    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


@utils.arg('hostnameorid',
           metavar='<hostname or id>',
           help="Name or ID of host")
def do_host_power_off(cc, args):
    """Power off a host."""
    attributes = []
    attributes.append('action=power-off')
    patch = utils.args_array_to_patch("replace", attributes)
    host = host_utils._find_host(cc, args.hostnameorid)
    try:
        host = cc.host.update(host.id, patch)
    except exc.HTTPNotFound:
        raise exc.CommandError('host not found: %s' % args.hostnameorid)
    _print_host_show(host)


def _timestamped(dname, fmt='%Y-%m-%d-%H-%M-%S_{dname}'):
    return datetime.datetime.now().strftime(fmt).format(dname=dname)


@utils.arg('--filename',
           help="The full file path to store the host file. Default './hosts.xml'")  # noqa
def do_host_bulk_export(cc, args):
    """Export host bulk configurations."""
    result = cc.host.bulk_export()

    xml_content = result['content']
    config_filename = './hosts.xml'
    if hasattr(args, 'filename') and args.filename:
        config_filename = args.filename
    try:
        with open(config_filename, 'wb') as fw:
            fw.write(xml_content)
        print(_('Export successfully to %s') % config_filename)
    except IOError:
        print(_('Cannot write to file: %s') % config_filename)

    return
