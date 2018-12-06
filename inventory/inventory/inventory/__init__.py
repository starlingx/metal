#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import pbr.version


__version__ = pbr.version.VersionInfo(
    'inventory').version_string()
