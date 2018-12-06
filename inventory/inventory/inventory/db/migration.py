#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#   under the License.

"""Database setup and migration commands."""

from inventory.db.sqlalchemy import api as db_api
import os
from oslo_config import cfg
from oslo_db import options
from stevedore import driver

options.set_defaults(cfg.CONF)


_IMPL = None

MIGRATE_REPO_PATH = os.path.join(
    os.path.abspath(os.path.dirname(__file__)),
    'sqlalchemy',
    'migrate_repo',
)


def get_backend():
    global _IMPL
    if not _IMPL:
        _IMPL = driver.DriverManager("inventory.database.migration_backend",
                                     cfg.CONF.database.backend).driver
    return _IMPL


def db_sync(version=None, engine=None):
    """Migrate the database to `version` or the most recent version."""

    if engine is None:
        engine = db_api.get_engine()
    return get_backend().db_sync(engine=engine,
                                 abs_path=MIGRATE_REPO_PATH,
                                 version=version
                                 )


def upgrade(version=None):
    """Migrate the database to `version` or the most recent version."""
    return get_backend().upgrade(version)


def version():
    return get_backend().version()


def create_schema():
    return get_backend().create_schema()
