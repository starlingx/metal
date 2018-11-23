#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Base agent manager functionality."""

import inspect

import futurist
from futurist import periodics
from futurist import rejection
from oslo_config import cfg
from oslo_log import log

from inventory.common import exception
from inventory.common.i18n import _
from inventory.db import api as dbapi

LOG = log.getLogger(__name__)


class BaseConductorManager(object):

    def __init__(self, host, topic):
        super(BaseConductorManager, self).__init__()
        if not host:
            host = cfg.CONF.host
        self.host = host
        self.topic = topic
        self._started = False

    def init_host(self, admin_context=None):
        """Initialize the conductor host.

        :param admin_context: the admin context to pass to periodic tasks.
        :raises: RuntimeError when conductor is already running.
        """
        if self._started:
            raise RuntimeError(_('Attempt to start an already running '
                                 'conductor manager'))

        self.dbapi = dbapi.get_instance()

        rejection_func = rejection.reject_when_reached(64)
        # CONF.conductor.workers_pool_size)
        self._executor = futurist.GreenThreadPoolExecutor(
            64, check_and_reject=rejection_func)
        """Executor for performing tasks async."""

        # Collect driver-specific periodic tasks.
        # Conductor periodic tasks accept context argument,
        LOG.info('Collecting periodic tasks')
        self._periodic_task_callables = []
        self._collect_periodic_tasks(self, (admin_context,))

        self._periodic_tasks = periodics.PeriodicWorker(
            self._periodic_task_callables,
            executor_factory=periodics.ExistingExecutor(self._executor))

        # Start periodic tasks
        self._periodic_tasks_worker = self._executor.submit(
            self._periodic_tasks.start, allow_empty=True)
        self._periodic_tasks_worker.add_done_callback(
            self._on_periodic_tasks_stop)

        self._started = True

    def del_host(self, deregister=True):
        # Conductor deregistration fails if called on non-initialized
        # conductor (e.g. when rpc server is unreachable).
        if not hasattr(self, 'conductor'):
            return

        self._periodic_tasks.stop()
        self._periodic_tasks.wait()
        self._executor.shutdown(wait=True)
        self._started = False

    def _collect_periodic_tasks(self, obj, args):
        """Collect periodic tasks from a given object.

        Populates self._periodic_task_callables with tuples
        (callable, args, kwargs).

        :param obj: object containing periodic tasks as methods
        :param args: tuple with arguments to pass to every task
        """
        for name, member in inspect.getmembers(obj):
            if periodics.is_periodic(member):
                LOG.debug('Found periodic task %(owner)s.%(member)s',
                          {'owner': obj.__class__.__name__,
                           'member': name})
                self._periodic_task_callables.append((member, args, {}))

    def _on_periodic_tasks_stop(self, fut):
        try:
            fut.result()
        except Exception as exc:
            LOG.critical('Periodic tasks worker has failed: %s', exc)
        else:
            LOG.info('Successfully shut down periodic tasks')

    def _spawn_worker(self, func, *args, **kwargs):

        """Create a greenthread to run func(*args, **kwargs).

        Spawns a greenthread if there are free slots in pool, otherwise raises
        exception. Execution control returns immediately to the caller.

        :returns: Future object.
        :raises: NoFreeConductorWorker if worker pool is currently full.

        """
        try:
            return self._executor.submit(func, *args, **kwargs)
        except futurist.RejectedSubmission:
            raise exception.NoFreeConductorWorker()
