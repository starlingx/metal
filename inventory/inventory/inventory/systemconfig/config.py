#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


from oslo_config import cfg
from oslo_utils._i18n import _

INVENTORY_CONFIG_OPTS = [
    cfg.ListOpt('drivers',
                default=['systemconfig'],
                help=_("SystemConfig driver "
                       "entrypoints to be loaded from the "
                       "inventory.systemconfig namespace.")),
]

cfg.CONF.register_opts(INVENTORY_CONFIG_OPTS, group="configuration")
