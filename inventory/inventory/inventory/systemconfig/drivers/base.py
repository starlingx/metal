#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4

# All Rights Reserved.
#

import abc
import six


@six.add_metaclass(abc.ABCMeta)
class SystemConfigDriverBase(object):
    """SystemConfig Driver Base Class."""

    @abc.abstractmethod
    def system_get_one(self):
        pass

    @abc.abstractmethod
    def network_get_by_type(self, network_type):
        pass

    @abc.abstractmethod
    def address_get_by_name(self, name):
        pass

    @abc.abstractmethod
    def host_configure_check(self, host_uuid):
        pass

    @abc.abstractmethod
    def host_configure(self, host_uuid, do_compute_apply=False):
        pass

    @abc.abstractmethod
    def host_unconfigure(self, host_uuid):
        pass
