#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4

# All Rights Reserved.
#

from inventory.common import exception
from oslo_config import cfg
from oslo_log import log
from stevedore.named import NamedExtensionManager

LOG = log.getLogger(__name__)
cfg.CONF.import_opt('drivers',
                    'inventory.systemconfig.config',
                    group='configuration')


class SystemConfigDriverManager(NamedExtensionManager):
    """Implementation of Sysinv SystemConfig drivers."""

    def __init__(self, invoke_kwds={},
                 namespace='inventory.systemconfig.drivers'):

        # Registered configuration drivers, keyed by name.
        self.drivers = {}

        # Ordered list of inventory configuration drivers, defining
        # the order in which the drivers are called.
        self.ordered_drivers = []

        names = cfg.CONF.configuration.drivers
        LOG.info("Configured inventory configuration drivers: args %s "
                 "names=%s" %
                 (invoke_kwds, names))

        super(SystemConfigDriverManager, self).__init__(
            namespace,
            names,
            invoke_kwds=invoke_kwds,
            invoke_on_load=True,
            name_order=True)

        LOG.info("Loaded systemconfig drivers: %s" % self.names())
        self._register_drivers()

    def _register_drivers(self):
        """Register all configuration drivers.

        This method should only be called once in the
        SystemConfigDriverManager constructor.
        """
        for ext in self:
            self.drivers[ext.name] = ext
            self.ordered_drivers.append(ext)
        LOG.info("Registered systemconfig drivers: %s",
                 [driver.name for driver in self.ordered_drivers])

    def _call_drivers_and_return_array(self, method_name, attr=None,
                                       raise_orig_exc=False):
        """Helper method for calling a method across all drivers.

        :param method_name: name of the method to call
        :param attr: an optional attribute to provide to the drivers
        :param raise_orig_exc: whether or not to raise the original
        driver exception, or use a general one
        """
        ret = []
        for driver in self.ordered_drivers:
            try:
                method = getattr(driver.obj, method_name)
                if attr:
                    ret = ret + method(attr)
                else:
                    ret = ret + method()
            except Exception as e:
                LOG.exception(e)
                LOG.error(
                    "Inventory SystemConfig driver '%(name)s' "
                    "failed in %(method)s",
                    {'name': driver.name, 'method': method_name}
                )
                if raise_orig_exc:
                    raise
                else:
                    raise exception.SystemConfigDriverError(
                        method=method_name
                    )
        return list(set(ret))

    def _call_drivers(self, method_name,
                      raise_orig_exc=False,
                      return_first=True,
                      **kwargs):
        """Helper method for calling a method across all drivers.

        :param method_name: name of the method to call
        :param attr: an optional attribute to provide to the drivers
        :param raise_orig_exc: whether or not to raise the original
        driver exception, or use a general one
        """
        for driver in self.ordered_drivers:
            try:
                method = getattr(driver.obj, method_name)
                LOG.info("_call_drivers_kwargs method_name=%s kwargs=%s"
                         % (method_name, kwargs))

                ret = method(**kwargs)
                if return_first:
                    return ret

            except Exception as e:
                LOG.exception(e)
                LOG.error(
                    "Inventory SystemConfig driver '%(name)s' "
                    "failed in %(method)s",
                    {'name': driver.name, 'method': method_name}
                )
                if raise_orig_exc:
                    raise
                else:
                    raise exception.SystemConfigDriverError(
                        method=method_name
                    )

    def system_get_one(self):
        try:
            return self._call_drivers(
                "system_get_one",
                raise_orig_exc=True)
        except Exception as e:
            LOG.exception(e)

    def network_get_by_type(self, network_type):
        try:
            return self._call_drivers(
                "network_get_by_type",
                raise_orig_exc=True,
                network_type=network_type)
        except Exception as e:
            LOG.exception(e)

    def address_get_by_name(self, name):
        try:
            return self._call_drivers(
                "address_get_by_name",
                raise_orig_exc=True,
                name=name)
        except Exception as e:
            LOG.exception(e)

    def host_configure_check(self, host_uuid):
        try:
            return self._call_drivers("host_configure_check",
                                      raise_orig_exc=True,
                                      host_uuid=host_uuid)
        except Exception as e:
            LOG.exception(e)

    def host_configure(self, host_uuid, do_compute_apply=False):
        try:
            return self._call_drivers("host_configure",
                                      raise_orig_exc=True,
                                      host_uuid=host_uuid,
                                      do_compute_apply=do_compute_apply)
        except Exception as e:
            LOG.exception(e)

    def host_unconfigure(self, host_uuid):
        try:
            return self._call_drivers("host_unconfigure",
                                      raise_orig_exc=True,
                                      host_uuid=host_uuid)
        except Exception as e:
            LOG.exception(e)
