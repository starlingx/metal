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
class Node(base.InventoryObject,
           object_base.VersionedObjectDictCompat):

    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(nullable=True),
        'uuid': object_fields.UUIDField(nullable=True),
        'host_id': object_fields.IntegerField(nullable=False),
        'host_uuid': object_fields.StringField(nullable=True),

        'numa_node': object_fields.IntegerField(nullable=False),
        'capabilities': object_fields.FlexibleDictField(nullable=True),
    }

    _foreign_fields = {'host_uuid': 'host:uuid'}

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_node = cls.dbapi.node_get(uuid)
        return cls._from_db_object(context, cls(), db_node)

    def save_changes(self, context, updates):
        self.dbapi.node_update(self.uuid, updates)

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
        db_nodes = cls.dbapi.node_get_list(
            filters=filters,
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_nodes)

    @classmethod
    def get_by_host(cls, context, host_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_nodes = cls.dbapi.node_get_by_host(
            host_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_nodes)
