#
# Copyright (c) 2013-2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8
#

from inventory.objects import base
from inventory.objects import fields as object_fields
from inventory.objects import port
from oslo_versionedobjects import base as object_base


@base.InventoryObjectRegistry.register
class EthernetPort(port.Port, object_base.VersionedObjectDictCompat):

    fields = dict({
        'mac': object_fields.StringField(nullable=True),
        'mtu': object_fields.IntegerField(nullable=True),
        'speed': object_fields.IntegerField(nullable=True),
        'link_mode': object_fields.StringField(nullable=True),
        'duplex': object_fields.IntegerField(nullable=True),
        'autoneg': object_fields.StringField(nullable=True),
        'bootp': object_fields.StringField(nullable=True)},
        **port.Port.fields)

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_ethernet_port = cls.dbapi.ethernet_port_get(uuid)
        return cls._from_db_object(context, cls(), db_ethernet_port)

    def save_changes(self, context, updates):
        self.dbapi.ethernet_port_update(self.uuid, updates)

    @classmethod
    def list(cls, context, limit=None, marker=None, sort_key=None,
             sort_dir=None, filters=None):
        """Return a list of EthernetPort objects.

        :param cls: the :class:`EthernetPort`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :param filters: Filters to apply.
        :returns: a list of :class:`EthernetPort` object.

        """
        db_ethernet_ports = cls.dbapi.ethernet_port_get_list(
            filters=filters,
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_ethernet_ports)

    @classmethod
    def get_by_host(cls, context, host_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_ethernet_ports = cls.dbapi.ethernet_port_get_by_host(
            host_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_ethernet_ports)

    @classmethod
    def get_by_numa_node(cls, context, node_uuid,
                         limit=None, marker=None,
                         sort_key=None, sort_dir=None):
        db_ethernet_ports = cls.dbapi.ethernet_port_get_by_numa_node(
            node_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_ethernet_ports)

    def create(self, context=None):
        """Create a EthernetPort record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context.
        :raises: InvalidParameterValue if some property values are invalid.
        """
        values = self.do_version_changes_for_db()
        db_ethernet_port = self.dbapi.ethernet_port_create(values)
        return self._from_db_object(self._context, self, db_ethernet_port)
