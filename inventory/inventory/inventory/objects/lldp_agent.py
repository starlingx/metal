#
# Copyright (c) 2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8
#

from oslo_versionedobjects import base as object_base

from inventory.common import k_lldp
from inventory.db import api as db_api
from inventory.objects import base
from inventory.objects import fields as object_fields


def get_lldp_tlvs(field, db_object):
    if hasattr(db_object, field):
        return db_object[field]
    if hasattr(db_object, 'lldptlvs'):
        tlv_object = db_object['lldptlvs']
        if tlv_object:
            for tlv in tlv_object:
                if tlv['type'] == field:
                    return tlv['value']
    return None


@base.InventoryObjectRegistry.register
class LLDPAgent(base.InventoryObject, object_base.VersionedObjectDictCompat):

    dbapi = db_api.get_instance()

    fields = {'id': object_fields.IntegerField(nullable=True),
              'uuid': object_fields.UUIDField(nullable=True),
              'status': object_fields.StringField(nullable=True),
              'host_id': object_fields.IntegerField(nullable=True),
              'host_uuid': object_fields.StringField(nullable=True),
              'port_id': object_fields.IntegerField(nullable=True),
              'port_uuid': object_fields.UUIDField(nullable=True),
              'port_name': object_fields.StringField(nullable=True),
              'port_namedisplay': object_fields.StringField(nullable=True)}

    _foreign_fields = {
        'host_uuid': 'host:uuid',
        'port_uuid': 'port:uuid',
        'port_name': 'port:name',
        'port_namedisplay': 'port:namedisplay',
    }

    for tlv in k_lldp.LLDP_TLV_VALID_LIST:
        fields.update({tlv: object_fields.StringField(nullable=True)})
        _foreign_fields.update({tlv: get_lldp_tlvs})

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_lldp_agent = cls.dbapi.lldp_agent_get(uuid)
        return cls._from_db_object(context, cls(), db_lldp_agent)

    def save_changes(self, context, updates):
        self.dbapi.lldp_agent_update(self.uuid, updates)

    @classmethod
    def list(cls, context, limit=None, marker=None, sort_key=None,
             sort_dir=None, filters=None):
        """Return a list of LLDPAgent objects.

        :param cls: the :class:`LLDPAgent`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :param filters: Filters to apply.
        :returns: a list of :class:`LLDPAgent` object.

        """
        db_lldp_agents = cls.dbapi.lldp_agent_get_list(
            filters=filters,
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_lldp_agents)

    @classmethod
    def get_by_host(cls, context, host_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_lldp_agents = cls.dbapi.lldp_agent_get_by_host(
            host_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_lldp_agents)

    @classmethod
    def get_by_port(cls, context, port_uuid,
                    limit=None, marker=None,
                    sort_key=None, sort_dir=None):
        db_lldp_agents = cls.dbapi.lldp_agent_get_by_port(
            port_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_lldp_agents)

    def create(self, context, portid, hostid, values):
        """Create a LLDPAgent record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context.
        :param portid:  port id
        :param hostid:  host id
        :param values:  dictionary of values
        """
        values = self.do_version_changes_for_db()
        db_lldp_agent = self.dbapi.lldp_agent_create(portid, hostid, values)
        return self._from_db_object(self._context, self, db_lldp_agent)
