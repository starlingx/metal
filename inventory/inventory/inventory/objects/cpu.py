#
# Copyright (c) 2013-2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8
#

from inventory.db import api as db_api
from inventory.objects import base
from inventory.objects import fields as object_fields
from oslo_versionedobjects import base as object_base


@base.InventoryObjectRegistry.register
class CPU(base.InventoryObject, object_base.VersionedObjectDictCompat):

    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(),
        'uuid': object_fields.StringField(nullable=True),
        'host_id': object_fields.IntegerField(),
        'host_uuid': object_fields.StringField(nullable=True),
        'node_id': object_fields.IntegerField(nullable=True),
        'node_uuid': object_fields.StringField(nullable=True),
        'numa_node': object_fields.IntegerField(nullable=True),
        'cpu': object_fields.IntegerField(),
        'core': object_fields.IntegerField(nullable=True),
        'thread': object_fields.IntegerField(nullable=True),
        'cpu_family': object_fields.StringField(nullable=True),
        'cpu_model': object_fields.StringField(nullable=True),
        'capabilities': object_fields.FlexibleDictField(nullable=True),
        # part of config
        # 'allocated_function': object_fields.StringField(nullable=True),
    }

    _foreign_fields = {'host_uuid': 'host:uuid',
                       'node_uuid': 'node:uuid',
                       'numa_node': 'node:numa_node'}

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_cpu = cls.dbapi.cpu_get(uuid)
        return cls._from_db_object(context, cls(), db_cpu)

    def save_changes(self, context, updates):
        self.dbapi.cpu_update(self.uuid, updates)

    @classmethod
    def list(cls, context, limit=None, marker=None, sort_key=None,
             sort_dir=None, filters=None):
        """Return a list of CPU objects.

        :param cls: the :class:`CPU`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :param filters: Filters to apply.
        :returns: a list of :class:`CPU` object.

        """
        db_cpus = cls.dbapi.cpu_get_list(
            filters=filters,
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_cpus)

    @classmethod
    def get_by_host(cls, context, host_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_cpus = cls.dbapi.cpu_get_by_host(
            host_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_cpus)

    @classmethod
    def get_by_node(cls, context, node_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_cpus = cls.dbapi.cpu_get_by_node(
            node_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_cpus)

    @classmethod
    def get_by_host_node(cls, context, host_uuid, node_uuid,
                         limit=None, marker=None,
                         sort_key=None, sort_dir=None):
        db_cpus = cls.dbapi.cpu_get_by_host_node(
            host_uuid,
            node_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_cpus)

    def create(self, context=None):
        """Create a CPU record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context.
        """
        values = self.do_version_changes_for_db()
        db_cpu = self.dbapi.cpu_create(values)
        return self._from_db_object(self._context, self, db_cpu)
