#
# Copyright (c) 2013-2015 Wind River Systems, Inc.
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
class SensorAnalog(base.InventoryObject,
                   object_base.VersionedObjectDictCompat):
    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(nullable=True),
        'uuid': object_fields.UUIDField(nullable=True),

        'host_id': object_fields.IntegerField(nullable=True),
        'host_uuid': object_fields.UUIDField(nullable=True),

        'sensorgroup_id': object_fields.IntegerField(nullable=True),
        'sensorgroup_uuid': object_fields.UUIDField(nullable=True),

        'sensorname': object_fields.StringField(nullable=True),
        'path': object_fields.StringField(nullable=True),
        'datatype': object_fields.StringField(nullable=True),
        'sensortype': object_fields.StringField(nullable=True),

        'status': object_fields.StringField(nullable=True),
        'state': object_fields.StringField(nullable=True),
        'state_requested': object_fields.IntegerField(nullable=True),
        'sensor_action_requested': object_fields.StringField(nullable=True),
        'audit_interval': object_fields.IntegerField(nullable=True),
        'algorithm': object_fields.StringField(nullable=True),
        'actions_minor': object_fields.StringField(nullable=True),
        'actions_major': object_fields.StringField(nullable=True),
        'actions_critical': object_fields.StringField(nullable=True),

        'unit_base': object_fields.StringField(nullable=True),
        'unit_modifier': object_fields.StringField(nullable=True),
        'unit_rate': object_fields.StringField(nullable=True),

        't_minor_lower': object_fields.StringField(nullable=True),
        't_minor_upper': object_fields.StringField(nullable=True),
        't_major_lower': object_fields.StringField(nullable=True),
        't_major_upper': object_fields.StringField(nullable=True),
        't_critical_lower': object_fields.StringField(nullable=True),
        't_critical_upper': object_fields.StringField(nullable=True),

        'suppress': object_fields.StringField(nullable=True),
        'capabilities': object_fields.FlexibleDictField(nullable=True),
    }

    _foreign_fields = {
        'host_uuid': 'host:uuid',
        'sensorgroup_uuid': 'sensorgroup:uuid',
    }

    @object_base.remotable_classmethod
    def get_by_uuid(cls, context, uuid):
        return cls.dbapi.isensor_analog_get(uuid)

    def save_changes(self, context, updates):
        self.dbapi.isensor_analog_update(self.uuid, updates)
