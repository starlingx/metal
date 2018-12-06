#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

"""
Interface for database access.
"""

import abc

from oslo_config import cfg
from oslo_db import api as db_api
import six

CONF = cfg.CONF

_BACKEND_MAPPING = {'sqlalchemy': 'inventory.db.sqlalchemy.api'}
IMPL = db_api.DBAPI.from_config(CONF, backend_mapping=_BACKEND_MAPPING,
                                lazy=True)


def get_instance():
    """Return a DB API instance."""
    return IMPL


def get_engine():
    return IMPL.get_engine()


def get_session():
    return IMPL.get_session()


@six.add_metaclass(abc.ABCMeta)
class Connection(object):
    """Base class for database connections."""

    @abc.abstractmethod
    def __init__(self):
        """Constructor."""

    # TODO(sc) Enforcement of required methods for db api
