#    Copyright 2013 IBM Corp.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from inventory import objects
from oslo_log import log
from oslo_utils import versionutils
from oslo_versionedobjects import base as object_base
# from oslo_versionedobjects import exception as ovo_exception

from oslo_versionedobjects import fields as object_fields
LOG = log.getLogger(__name__)


class InventoryObjectRegistry(object_base.VersionedObjectRegistry):
    def registration_hook(self, cls, index):
        # NOTE(jroll): blatantly stolen from nova
        # NOTE(danms): This is called when an object is registered,
        # and is responsible for maintaining inventory.objects.$OBJECT
        # as the highest-versioned implementation of a given object.
        version = versionutils.convert_version_to_tuple(cls.VERSION)
        if not hasattr(objects, cls.obj_name()):  # noqa
            setattr(objects, cls.obj_name(), cls)  # noqa
        else:
            cur_version = versionutils.convert_version_to_tuple(
                getattr(objects, cls.obj_name()).VERSION)  # noqa
            if version >= cur_version:
                setattr(objects, cls.obj_name(), cls)  # noqa


class InventoryObject(object_base.VersionedObject):
    """Base class and object factory.

    This forms the base of all objects that can be remoted or instantiated
    via RPC. Simply defining a class that inherits from this base class
    will make it remotely instantiatable. Objects should implement the
    necessary "get" classmethod routines as well as "save" object methods
    as appropriate.
    """

    OBJ_SERIAL_NAMESPACE = 'inventory_object'
    OBJ_PROJECT_NAMESPACE = 'inventory'

    fields = {
        'created_at': object_fields.DateTimeField(nullable=True),
        'updated_at': object_fields.DateTimeField(nullable=True),
    }

    _foreign_fields = {}
    _optional_fields = []

    def _get_foreign_field(self, field, db_object):
        """Retrieve data from a foreign relationship on a DB entry.

        Depending on how the field was described in _foreign_fields the data
        may be retrieved by calling a function to do the work, or by accessing
        the specified remote field name if specified as a string.
        """
        accessor = self._foreign_fields[field]
        if callable(accessor):
            return accessor(field, db_object)

        # Split as "local object reference:remote field name"
        local, remote = accessor.split(':')
        try:
            local_object = db_object[local]
            if local_object:
                return local_object[remote]
        except KeyError:
            pass  # foreign relationships are not always available
        return None

    def __getitem__(self, name):
        return getattr(self, name)

    def __setitem__(self, name, value):
        setattr(self, name, value)

    def as_dict(self):
        return dict((k, getattr(self, k))
                    for k in self.fields
                    if hasattr(self, k))

    @classmethod
    def get_defaults(cls):
        """Return a dict of its fields with their default value."""
        return dict((k, v(None))
                    for k, v in cls.fields.iteritems()
                    if k != "id" and callable(v))

    def get(self, key, value=None):
        """For backwards-compatibility with dict-based objects.

        NOTE(danms): May be removed in the future.
        """
        return self[key]

    def _set_from_db_object(self, context, cls_object, db_object, fields):
        """Sets object fields.

        :param context: security context
        :param db_object: A DB entity of the object
        :param fields: list of fields to set on obj from values from db_object.
        """

        for field in cls_object.fields:
            if field in cls_object._optional_fields:
                if not hasattr(db_object, field):
                    continue

            if field in cls_object._foreign_fields:
                setattr(self, field,
                        cls_object._get_foreign_field(field, db_object))
                continue

            setattr(self, field, db_object[field])
            # cls_object[field] = db_object[field]

    @staticmethod
    def _from_db_object(context, obj, db_object, fields=None):
        """Converts a database entity to a formal object.

        This always converts the database entity to the latest version
        of the object. Note that the latest version is available at
        object.__class__.VERSION. object.VERSION is the version of this
        particular object instance; it is possible that it is not the latest
        version.

        :param context: security context
        :param obj: An object of the class.
        :param db_object: A DB entity of the object
        :param fields: list of fields to set on obj from values from db_object.
        :return: The object of the class with the database entity added
        :raises: ovo_exception.IncompatibleObjectVersion
        """
        # objname = obj.obj_name()
        # db_version = db_object['version']

        # if not versionutils.is_compatible(db_version, obj.__class__.VERSION):
        #     raise ovo_exception.IncompatibleObjectVersion(
        #         objname=objname, objver=db_version,
        #         supported=obj.__class__.VERSION)

        obj._set_from_db_object(context, obj, db_object, fields)

        obj._context = context

        # NOTE(rloo). We now have obj, a versioned object that corresponds to
        # its DB representation. A versioned object has an internal attribute
        # ._changed_fields; this is a list of changed fields -- used, e.g.,
        # when saving the object to the DB (only those changed fields are
        # saved to the DB). The obj.obj_reset_changes() clears this list
        # since we didn't actually make any modifications to the object that
        # we want saved later.
        obj.obj_reset_changes()

        # if db_version != obj.__class__.VERSION:
        #     # convert to the latest version
        #     obj.VERSION = db_version
        #     obj.convert_to_version(obj.__class__.VERSION,
        #                            remove_unavailable_fields=False)
        return obj

    def _convert_to_version(self, target_version,
                            remove_unavailable_fields=True):
        """Convert to the target version.

        Subclasses should redefine this method, to do the conversion of the
        object to the target version.

        Convert the object to the target version. The target version may be
        the same, older, or newer than the version of the object. This is
        used for DB interactions as well as for serialization/deserialization.

        The remove_unavailable_fields flag is used to distinguish these two
        cases:

        1) For serialization/deserialization, we need to remove the unavailable
           fields, because the service receiving the object may not know about
           these fields. remove_unavailable_fields is set to True in this case.

        2) For DB interactions, we need to set the unavailable fields to their
           appropriate values so that these fields are saved in the DB. (If
           they are not set, the VersionedObject magic will not know to
           save/update them to the DB.) remove_unavailable_fields is set to
           False in this case.

        :param target_version: the desired version of the object
        :param remove_unavailable_fields: True to remove fields that are
            unavailable in the target version; set this to True when
            (de)serializing. False to set the unavailable fields to appropriate
            values; set this to False for DB interactions.
        """
        pass

    def convert_to_version(self, target_version,
                           remove_unavailable_fields=True):
        """Convert this object to the target version.

        Convert the object to the target version. The target version may be
        the same, older, or newer than the version of the object. This is
        used for DB interactions as well as for serialization/deserialization.

        The remove_unavailable_fields flag is used to distinguish these two
        cases:

        1) For serialization/deserialization, we need to remove the unavailable
           fields, because the service receiving the object may not know about
           these fields. remove_unavailable_fields is set to True in this case.

        2) For DB interactions, we need to set the unavailable fields to their
           appropriate values so that these fields are saved in the DB. (If
           they are not set, the VersionedObject magic will not know to
           save/update them to the DB.) remove_unavailable_fields is set to
           False in this case.

        _convert_to_version() does the actual work.

        :param target_version: the desired version of the object
        :param remove_unavailable_fields: True to remove fields that are
            unavailable in the target version; set this to True when
            (de)serializing. False to set the unavailable fields to appropriate
            values; set this to False for DB interactions.
        """
        if self.VERSION != target_version:
            self._convert_to_version(
                target_version,
                remove_unavailable_fields=remove_unavailable_fields)
            if remove_unavailable_fields:
                # NOTE(rloo): We changed the object, but don't keep track of
                # any of these changes, since it is inaccurate anyway (because
                # it doesn't keep track of any 'changed' unavailable fields).
                self.obj_reset_changes()

        # NOTE(rloo): self.__class__.VERSION is the latest version that
        # is supported by this service. self.VERSION is the version of
        # this object instance -- it may get set via e.g. the
        # serialization or deserialization process, or here.
        if (self.__class__.VERSION != target_version or
                self.VERSION != self.__class__.VERSION):
            self.VERSION = target_version

    @classmethod
    def get_target_version(cls):
        return cls.VERSION

    def do_version_changes_for_db(self):
        """Change the object to the version needed for the database.

        If needed, this changes the object (modifies object fields) to be in
        the correct version for saving to the database.

        The version used to save the object in the DB is determined as follows:

        * If the object is pinned, we save the object in the pinned version.
          Since it is pinned, we must not save in a newer version, in case
          a rolling upgrade is happening and some services are still using the
          older version of inventory, with no knowledge of this newer version.
        * If the object isn't pinned, we save the object in the latest version.

        Because the object may be converted to a different object version, this
        method must only be called just before saving the object to the DB.

        :returns: a dictionary of changed fields and their new values
                  (could be an empty dictionary). These are the fields/values
                  of the object that would be saved to the DB.
        """
        target_version = self.get_target_version()

        if target_version != self.VERSION:
            # Convert the object so we can save it in the target version.
            self.convert_to_version(target_version,
                                    remove_unavailable_fields=False)

        changes = self.obj_get_changes()
        # NOTE(rloo): Since this object doesn't keep track of the version that
        #             is saved in the DB and we don't want to make a DB call
        #             just to find out, we always update 'version' in the DB.
        changes['version'] = self.VERSION

        return changes

    @classmethod
    def _from_db_object_list(cls, context, db_objects):
        """Returns objects corresponding to database entities.

        Returns a list of formal objects of this class that correspond to
        the list of database entities.

        :param cls: the VersionedObject class of the desired object
        :param context: security context
        :param db_objects: A  list of DB models of the object
        :returns: A list of objects corresponding to the database entities
        """
        return [cls._from_db_object(context, cls(), db_obj)
                for db_obj in db_objects]

    def save(self, context=None):
        updates = {}
        changes = self.do_version_changes_for_db()

        for field in changes:
            if field == 'version':
                continue
            updates[field] = self[field]

        self.save_changes(context, updates)
        self.obj_reset_changes()


class InventoryObjectSerializer(object_base.VersionedObjectSerializer):
    # Base class to use for object hydration
    OBJ_BASE_CLASS = InventoryObject


def obj_to_primitive(obj):
    """Recursively turn an object into a python primitive.

    An InventoryObject becomes a dict, and anything that implements
    ObjectListBase becomes a list.
    """
    if isinstance(obj, object_base.ObjectListBase):
        return [obj_to_primitive(x) for x in obj]
    elif isinstance(obj, InventoryObject):
        result = {}
        for key, value in obj.iteritems():
            result[key] = obj_to_primitive(value)
        return result
    else:
        return obj
