#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


import os
import sqlalchemy

from migrate import exceptions as versioning_exceptions
from migrate.versioning import api as versioning_api
from migrate.versioning.repository import Repository
from oslo_db.sqlalchemy import enginefacade

from inventory.common import exception
from inventory.common.i18n import _
from inventory.db import migration

_REPOSITORY = None

get_engine = enginefacade.get_legacy_facade().get_engine


def db_sync(version=None):
    if version is not None:
        try:
            version = int(version)
        except ValueError:
            raise exception.ApiError(_("version should be an integer"))

    current_version = db_version()
    repository = _find_migrate_repo()
    if version is None or version > current_version:
        return versioning_api.upgrade(get_engine(), repository, version)
    else:
        return versioning_api.downgrade(get_engine(), repository,
                                        version)


def db_version():
    repository = _find_migrate_repo()
    try:
        return versioning_api.db_version(get_engine(), repository)
    except versioning_exceptions.DatabaseNotControlledError:
        meta = sqlalchemy.MetaData()
        engine = get_engine()
        meta.reflect(bind=engine)
        tables = meta.tables
        if len(tables) == 0:
            db_version_control(migration.INIT_VERSION)
            return versioning_api.db_version(get_engine(), repository)


def db_version_control(version=None):
    repository = _find_migrate_repo()
    versioning_api.version_control(get_engine(), repository, version)
    return version


def _find_migrate_repo():
    """Get the path for the migrate repository."""
    global _REPOSITORY
    path = os.path.join(os.path.abspath(os.path.dirname(__file__)),
                        'migrate_repo')
    assert os.path.exists(path)
    if _REPOSITORY is None:
        _REPOSITORY = Repository(path)
    return _REPOSITORY
