#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from inventory.db import api as db_api
from inventory.objects import base
from inventory.objects import fields as object_fields
from oslo_versionedobjects import base as object_base


@base.InventoryObjectRegistry.register
class System(base.InventoryObject, object_base.VersionedObjectDictCompat):

    VERSION = '1.0'

    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(nullable=True),
        'uuid': object_fields.UUIDField(nullable=True),
        'name': object_fields.StringField(nullable=True),
        'system_type': object_fields.StringField(nullable=True),
        'system_mode': object_fields.StringField(nullable=True),
        'description': object_fields.StringField(nullable=True),
        'capabilities': object_fields.FlexibleDictField(nullable=True),
        'contact': object_fields.StringField(nullable=True),
        'location': object_fields.StringField(nullable=True),
        'services': object_fields.IntegerField(nullable=True),
        'software_version': object_fields.StringField(nullable=True),
        'timezone': object_fields.StringField(nullable=True),
        'security_profile': object_fields.StringField(nullable=True),
        'region_name': object_fields.StringField(nullable=True),
        'service_project_name': object_fields.StringField(nullable=True),
        'distributed_cloud_role': object_fields.StringField(nullable=True),
        'security_feature': object_fields.StringField(nullable=True),
    }

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_system = cls.dbapi.system_get(uuid)
        return cls._from_db_object(context, cls(), db_system)

    @classmethod
    def get_one(cls, context):
        db_system = cls.dbapi.system_get_one()
        system = cls._from_db_object(context, cls(), db_system)
        return system

    @classmethod
    def list(cls, context,
             limit=None, marker=None, sort_key=None, sort_dir=None):
        """Return a list of System objects.

        :param cls: the :class:`System`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :returns: a list of :class:`System` object.

        """
        db_systems = cls.dbapi.system_get_list(
            limit=limit, marker=marker, sort_key=sort_key, sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_systems)

    def save_changes(self, context, updates):
        self.dbapi.system_update(self.uuid, updates)

    def create(self, context=None):
        """Create a System record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context. NOTE: This should only
                        be used internally by the indirection_api.
                        Unfortunately, RPC requires context as the first
                        argument, even though we don't use it.
                        A context should be set when instantiating the
                        object, e.g.: System(context)
        """
        values = self.do_version_changes_for_db()
        db_system = self.dbapi.system_create(values)
        return self._from_db_object(self._context, self, db_system)
