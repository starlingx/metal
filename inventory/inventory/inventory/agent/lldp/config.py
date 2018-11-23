#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4

# All Rights Reserved.
#

from oslo_config import cfg
from oslo_utils._i18n import _

INVENTORY_LLDP_OPTS = [
    cfg.ListOpt('drivers',
                default=['lldpd'],
                help=_("An ordered list of inventory LLDP driver "
                       "entrypoints to be loaded from the "
                       "inventory.agent namespace.")),
]

cfg.CONF.register_opts(INVENTORY_LLDP_OPTS, group="lldp")
