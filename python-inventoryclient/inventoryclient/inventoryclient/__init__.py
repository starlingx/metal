#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


try:
    import inventoryclient.client
    Client = inventoryclient.client.get_client
except ImportError:
    import warnings
    warnings.warn("Could not import inventoryclient.client", ImportWarning)

import pbr.version

version_info = pbr.version.VersionInfo('inventoryclient')

try:
    __version__ = version_info.version_string()
except AttributeError:
    __version__ = None
