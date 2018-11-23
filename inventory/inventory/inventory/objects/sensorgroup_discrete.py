#
# Copyright (c) 2013-2015 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8
#

from oslo_versionedobjects import base as object_base

from inventory.db import api as db_api
from inventory.objects import base
from inventory.objects import fields as object_fields


@base.InventoryObjectRegistry.register
class SensorGroupDiscrete(base.InventoryObject,
                          object_base.VersionedObjectDictCompat):
    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(nullable=True),
        'uuid': object_fields.UUIDField(nullable=True),
        'host_id': object_fields.IntegerField(nullable=True),

        'sensorgroupname': object_fields.StringField(nullable=True),
        'path': object_fields.StringField(nullable=True),

        'datatype': object_fields.StringField(nullable=True),
        'sensortype': object_fields.StringField(nullable=True),
        'description': object_fields.StringField(nullable=True),

        'state': object_fields.StringField(nullable=True),
        'possible_states': object_fields.StringField(nullable=True),
        'audit_interval_group': object_fields.IntegerField(nullable=True),
        'record_ttl': object_fields.StringField(nullable=True),

        'algorithm': object_fields.StringField(nullable=True),
        'actions_critical_choices': object_fields.StringField(nullable=True),
        'actions_major_choices': object_fields.StringField(nullable=True),
        'actions_minor_choices': object_fields.StringField(nullable=True),
        'actions_minor_group': object_fields.StringField(nullable=True),
        'actions_major_group': object_fields.StringField(nullable=True),
        'actions_critical_group': object_fields.StringField(nullable=True),

        'suppress': object_fields.StringField(nullable=True),
        'capabilities': object_fields.FlexibleDictField(nullable=True)

    }

    @object_base.remotable_classmethod
    def get_by_uuid(cls, context, uuid):
        return cls.dbapi.isensorgroup_discrete_get(uuid)

    def save_changes(self, context, updates):
        self.dbapi.isensorgroup_discrete_update(self.uuid, updates)
