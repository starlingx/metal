# Copyright (c) 2015 Intel Corporation
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
from oslo_config import cfg

CONF = cfg.CONF

# This is the version 1 API
BASE_VERSION = 1

# Here goes a short log of changes in every version.
# Refer to doc/source/dev/webapi-version-history.rst for a detailed explanation
# of what each version contains.
#
# v1.0: corresponds to Initial API

MINOR_0_INITIAL_VERSION = 0

# When adding another version, update:
# - MINOR_MAX_VERSION
# - doc/source/contributor/webapi-version-history.rst with a detailed
#   explanation of what changed in the new version
# - common/release_mappings.py, RELEASE_MAPPING['master']['api']

MINOR_MAX_VERSION = MINOR_0_INITIAL_VERSION

# String representations of the minor and maximum versions
_MIN_VERSION_STRING = '{}.{}'.format(BASE_VERSION, MINOR_0_INITIAL_VERSION)
_MAX_VERSION_STRING = '{}.{}'.format(BASE_VERSION, MINOR_MAX_VERSION)


def min_version_string():
    """Returns the minimum supported API version (as a string)"""
    return _MIN_VERSION_STRING


def max_version_string():
    """Returns the maximum supported API version (as a string).

    If the service is pinned, the maximum API version is the pinned
    version. Otherwise, it is the maximum supported API version.
    """

    # TODO(jkung): enable when release versions supported
    # release_ver = release_mappings.RELEASE_MAPPING.get(
    #     CONF.pin_release_version)
    # if release_ver:
    #     return release_ver['api']
    # else:
    return _MAX_VERSION_STRING
