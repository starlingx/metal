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
class Port(base.InventoryObject, object_base.VersionedObjectDictCompat):

    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(),
        'uuid': object_fields.UUIDField(nullable=True),
        'host_id': object_fields.IntegerField(nullable=True),
        'host_uuid': object_fields.UUIDField(nullable=True),
        'node_id': object_fields.IntegerField(nullable=True),
        'node_uuid': object_fields.UUIDField(nullable=True),

        'type': object_fields.StringField(nullable=True),
        'name': object_fields.StringField(nullable=True),
        'namedisplay': object_fields.StringField(nullable=True),
        'pciaddr': object_fields.StringField(nullable=True),
        'dev_id': object_fields.IntegerField(nullable=True),
        'pclass': object_fields.StringField(nullable=True),
        'pvendor': object_fields.StringField(nullable=True),
        'pdevice': object_fields.StringField(nullable=True),
        'psvendor': object_fields.StringField(nullable=True),
        'dpdksupport': object_fields.BooleanField(nullable=True),
        'psdevice': object_fields.StringField(nullable=True),
        'numa_node': object_fields.IntegerField(nullable=True),
        'sriov_totalvfs': object_fields.IntegerField(nullable=True),
        'sriov_numvfs': object_fields.IntegerField(nullable=True),
        'sriov_vfs_pci_address': object_fields.StringField(nullable=True),
        'driver': object_fields.StringField(nullable=True),
        'capabilities': object_fields.FlexibleDictField(nullable=True),
    }

    # interface_uuid is in systemconfig
    _foreign_fields = {'host_uuid': 'host:uuid',
                       'node_uuid': 'node:uuid',
                       }

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_port = cls.dbapi.port_get(uuid)
        return cls._from_db_object(context, cls(), db_port)

    def save_changes(self, context, updates):
        self.dbapi.port_update(self.uuid, updates)

    @classmethod
    def list(cls, context, limit=None, marker=None, sort_key=None,
             sort_dir=None, filters=None):
        """Return a list of Port objects.

        :param cls: the :class:`Port`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :param filters: Filters to apply.
        :returns: a list of :class:`Port` object.

        """
        db_ports = cls.dbapi.port_get_list(
            filters=filters,
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_ports)

    @classmethod
    def get_by_host(cls, context, host_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_ports = cls.dbapi.port_get_by_host(
            host_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_ports)

    @classmethod
    def get_by_numa_node(cls, context, node_uuid,
                         limit=None, marker=None,
                         sort_key=None, sort_dir=None):
        db_ports = cls.dbapi.port_get_by_numa_node(
            node_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_ports)

    def create(self, context=None):
        """Create a EthernetPort record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context.
        :raises: InvalidParameterValue if some property values are invalid.
        """
        values = self.do_version_changes_for_db()
        db_port = self.dbapi.port_create(values)
        return self._from_db_object(self._context, self, db_port)
