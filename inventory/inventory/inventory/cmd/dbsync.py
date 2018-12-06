#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


from oslo_config import cfg
import sys

from inventory.db import migration

CONF = cfg.CONF


def main():
    cfg.CONF(sys.argv[1:],
             project='inventory')
    migration.db_sync()
