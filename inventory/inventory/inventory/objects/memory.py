#
# Copyright (c) 2013-2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8
#
#

from inventory.db import api as db_api
from inventory.objects import base
from inventory.objects import fields as object_fields
from oslo_versionedobjects import base as object_base


@base.InventoryObjectRegistry.register
class Memory(base.InventoryObject, object_base.VersionedObjectDictCompat):

    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(nullable=True),
        'uuid': object_fields.UUIDField(nullable=True),
        'node_id': object_fields.IntegerField(nullable=True),
        'node_uuid': object_fields.UUIDField(nullable=True),
        'host_id': object_fields.IntegerField(nullable=True),
        'host_uuid': object_fields.UUIDField(nullable=True),
        'numa_node': object_fields.IntegerField(nullable=True),

        'memtotal_mib': object_fields.IntegerField(nullable=True),
        'memavail_mib': object_fields.IntegerField(nullable=True),
        'platform_reserved_mib': object_fields.IntegerField(nullable=True),
        'node_memtotal_mib': object_fields.IntegerField(nullable=True),

        'hugepages_configured': object_fields.StringField(nullable=True),

        'vswitch_hugepages_size_mib':
            object_fields.IntegerField(nullable=True),
        'vswitch_hugepages_reqd': object_fields.IntegerField(nullable=True),
        'vswitch_hugepages_nr': object_fields.IntegerField(nullable=True),
        'vswitch_hugepages_avail': object_fields.IntegerField(nullable=True),

        'vm_hugepages_nr_2M_pending':
            object_fields.IntegerField(nullable=True),
        'vm_hugepages_nr_1G_pending':
            object_fields.IntegerField(nullable=True),
        'vm_hugepages_nr_2M': object_fields.IntegerField(nullable=True),
        'vm_hugepages_avail_2M': object_fields.IntegerField(nullable=True),
        'vm_hugepages_nr_1G': object_fields.IntegerField(nullable=True),
        'vm_hugepages_avail_1G': object_fields.IntegerField(nullable=True),
        'vm_hugepages_nr_4K': object_fields.IntegerField(nullable=True),


        'vm_hugepages_use_1G': object_fields.StringField(nullable=True),
        'vm_hugepages_possible_2M': object_fields.IntegerField(nullable=True),
        'vm_hugepages_possible_1G': object_fields.IntegerField(nullable=True),
        'capabilities': object_fields.FlexibleDictField(nullable=True),
    }

    _foreign_fields = {'host_uuid': 'host:uuid',
                       'node_uuid': 'node:uuid',
                       'numa_node': 'node:numa_node'}

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_memory = cls.dbapi.memory_get(uuid)
        return cls._from_db_object(context, cls(), db_memory)

    def save_changes(self, context, updates):
        self.dbapi.memory_update(self.uuid, updates)

    @classmethod
    def list(cls, context, limit=None, marker=None, sort_key=None,
             sort_dir=None, filters=None):
        """Return a list of Memory objects.

        :param cls: the :class:`Memory`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :param filters: Filters to apply.
        :returns: a list of :class:`Memory` object.

        """
        db_memorys = cls.dbapi.memory_get_list(
            filters=filters,
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_memorys)

    @classmethod
    def get_by_host(cls, context, host_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_memorys = cls.dbapi.memory_get_by_host(
            host_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_memorys)

    @classmethod
    def get_by_host_node(cls, context, host_uuid, node_uuid,
                         limit=None, marker=None,
                         sort_key=None, sort_dir=None):
        db_memorys = cls.dbapi.memory_get_by_host_node(
            host_uuid,
            node_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_memorys)

    @classmethod
    def get_by_node(cls, context, node_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_memorys = cls.dbapi.memory_get_by_node(
            node_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_memorys)

    def create(self, context=None):
        """Create a Memory record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context.
        """
        values = self.do_version_changes_for_db()
        db_memory = self.dbapi.memory_create(values)
        return self._from_db_object(self._context, self, db_memory)
