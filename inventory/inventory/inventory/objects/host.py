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
class Host(base.InventoryObject, object_base.VersionedObjectDictCompat):

    VERSION = '1.0'

    dbapi = db_api.get_instance()

    fields = {
        'id': object_fields.IntegerField(nullable=True),
        'uuid': object_fields.UUIDField(nullable=True),

        'recordtype': object_fields.StringField(nullable=True),
        'hostname': object_fields.StringField(nullable=True),

        'personality': object_fields.StringField(nullable=True),
        'subfunctions': object_fields.StringField(nullable=True),
        'subfunction_oper': object_fields.StringField(nullable=True),
        'subfunction_avail': object_fields.StringField(nullable=True),
        'reserved': object_fields.StringField(nullable=True),

        'invprovision': object_fields.StringField(nullable=True),
        'mgmt_mac': object_fields.StringField(nullable=True),
        'mgmt_ip': object_fields.StringField(nullable=True),

        # Board management members
        'bm_ip': object_fields.StringField(nullable=True),
        'bm_mac': object_fields.StringField(nullable=True),
        'bm_type': object_fields.StringField(nullable=True),
        'bm_username': object_fields.StringField(nullable=True),

        'location': object_fields.FlexibleDictField(nullable=True),
        'serialid': object_fields.StringField(nullable=True),
        'administrative': object_fields.StringField(nullable=True),
        'operational': object_fields.StringField(nullable=True),
        'availability': object_fields.StringField(nullable=True),
        'host_action': object_fields.StringField(nullable=True),
        'action_state': object_fields.StringField(nullable=True),
        'mtce_info': object_fields.StringField(nullable=True),
        'vim_progress_status': object_fields.StringField(nullable=True),
        'action': object_fields.StringField(nullable=True),
        'task': object_fields.StringField(nullable=True),
        'uptime': object_fields.IntegerField(nullable=True),
        'capabilities': object_fields.FlexibleDictField(nullable=True),

        'boot_device': object_fields.StringField(nullable=True),
        'rootfs_device': object_fields.StringField(nullable=True),
        'install_output': object_fields.StringField(nullable=True),
        'console': object_fields.StringField(nullable=True),
        'tboot': object_fields.StringField(nullable=True),
        'ttys_dcd': object_fields.StringField(nullable=True),
        'install_state': object_fields.StringField(nullable=True),
        'install_state_info': object_fields.StringField(nullable=True),
        'iscsi_initiator_name': object_fields.StringField(nullable=True),
    }

    @classmethod
    def get_by_uuid(cls, context, uuid):
        db_host = cls.dbapi.host_get(uuid)
        return cls._from_db_object(context, cls(), db_host)

    @classmethod
    def get_by_filters_one(cls, context, filters):
        db_host = cls.dbapi.host_get_by_filters_one(filters)
        return cls._from_db_object(context, cls(), db_host)

    def save_changes(self, context, updates):
        self.dbapi.host_update(self.uuid, updates)

    @classmethod
    def list(cls, context, limit=None, marker=None, sort_key=None,
             sort_dir=None, filters=None):
        """Return a list of Host objects.

        :param cls: the :class:`Host`
        :param context: Security context.
        :param limit: maximum number of resources to return in a single result.
        :param marker: pagination marker for large data sets.
        :param sort_key: column to sort results by.
        :param sort_dir: direction to sort. "asc" or "desc".
        :param filters: Filters to apply.
        :returns: a list of :class:`Host` object.

        """
        db_hosts = cls.dbapi.host_get_list(filters=filters, limit=limit,
                                           marker=marker, sort_key=sort_key,
                                           sort_dir=sort_dir)
        return cls._from_db_object_list(context, db_hosts)

    def create(self, context=None):
        """Create a Host record in the DB.

        Column-wise updates will be made based on the result of
        self.what_changed().

        :param context: Security context. NOTE: This should only
                        be used internally by the indirection_api.
                        Unfortunately, RPC requires context as the first
                        argument, even though we don't use it.
                        A context should be set when instantiating the
                        object, e.g.: Host(context)
        :raises: InvalidParameterValue if some property values are invalid.
        """
        values = self.do_version_changes_for_db()
        # self._validate_property_values(values.get('properties'))
        db_host = self.dbapi.host_create(values)
        return self._from_db_object(self._context, self, db_host)
