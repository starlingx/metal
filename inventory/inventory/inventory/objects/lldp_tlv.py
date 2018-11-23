#
# Copyright (c) 2016 Wind River Systems, Inc.
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
class LLDPTLV(base.InventoryObject, object_base.VersionedObjectDictCompat):

    dbapi = db_api.get_instance()

    fields = {'id': object_fields.IntegerField(nullable=True),
              'agent_id': object_fields.IntegerField(nullable=True),
              'agent_uuid': object_fields.UUIDField(nullable=True),
              'neighbour_id': object_fields.IntegerField(nullable=True),
              'neighbour_uuid': object_fields.UUIDField(nullable=True),
              'type': object_fields.StringField(nullable=True),
              'value': object_fields.StringField(nullable=True)}

    _foreign_fields = {
        'agent_uuid': 'lldp_agent:uuid',
        'neighbour_uuid': 'lldp_neighbour:uuid',
    }

    @classmethod
    def get_by_id(cls, context, id):
        db_lldp_tlv = cls.dbapi.lldp_tlv_get_by_id(id)
        return cls._from_db_object(context, cls(), db_lldp_tlv)

    def save_changes(self, context, updates):
        self.dbapi.lldp_tlv_update(self.id, updates)

    @classmethod
    def list(cls, context, limit=None, marker=None, sort_key=None,
             sort_dir=None, filters=None):
        """Return a list of LLDPTLV objects.

        :param cls: the :class:`LLDPTLV`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :param filters: Filters to apply.
        :returns: a list of :class:`LLDPTLV` object.

        """
        db_lldp_tlvs = cls.dbapi.lldp_tlv_get_list(
            filters=filters,
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_lldp_tlvs)

    @classmethod
    def get_by_neighbour(cls, context, neighbour_uuid,
                         limit=None, marker=None,
                         sort_key=None, sort_dir=None):
        db_lldp_tlvs = cls.dbapi.lldp_tlv_get_by_neighbour(
            neighbour_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_lldp_tlvs)

    @classmethod
    def get_by_agent(cls, context, agent_uuid,
                     limit=None, marker=None,
                     sort_key=None, sort_dir=None):
        db_lldp_tlvs = cls.dbapi.lldp_tlv_get_by_agent(
            agent_uuid,
            limit=limit,
            marker=marker,
            sort_key=sort_key,
            sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_lldp_tlvs)

    def create(self, values, context=None, agentid=None, neighbourid=None):
        """Create a LLDPTLV record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context.
        :param agentid: agent id
        :param neighbourid: neighbour id
        :param values:  dictionary of values
        """
        values = self.do_version_changes_for_db()
        db_lldp_tlv = self.dbapi.lldp_tlv_create(
            values, agentid, neighbourid)
        return self._from_db_object(self._context, self, db_lldp_tlv)

    def destroy(self, context=None):
        """Delete the LLDPTLV from the DB.

        :param context: Security context. NOTE: This should only
                        be used internally by the indirection_api.
                        Unfortunately, RPC requires context as the first
                        argument, even though we don't use it.
                        A context should be set when instantiating the
                        object, e.g.: Node(context)
        """
        self.dbapi.lldp_tlv_destroy(self.id)
        self.obj_reset_changes()
