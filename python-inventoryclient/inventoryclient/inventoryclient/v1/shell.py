#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from inventoryclient.common import utils
from inventoryclient.v1 import cpu_shell
from inventoryclient.v1 import ethernetport_shell
from inventoryclient.v1 import host_shell
from inventoryclient.v1 import lldp_agent_shell
from inventoryclient.v1 import lldp_neighbour_shell
from inventoryclient.v1 import memory_shell
from inventoryclient.v1 import node_shell
from inventoryclient.v1 import pci_device_shell
from inventoryclient.v1 import port_shell


COMMAND_MODULES = [
    cpu_shell,
    ethernetport_shell,
    host_shell,
    lldp_agent_shell,
    lldp_neighbour_shell,
    memory_shell,
    node_shell,
    pci_device_shell,
    port_shell,
]


def enhance_parser(parser, subparsers, cmd_mapper):
    '''Take a basic (nonversioned) parser and enhance it with
    commands and options specific for this version of API.

    :param parser: top level parser :param subparsers: top level
        parser's subparsers collection where subcommands will go
    '''
    for command_module in COMMAND_MODULES:
        utils.define_commands_from_module(subparsers, command_module,
                                          cmd_mapper)
