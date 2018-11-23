# Copyright 2013 Wind River, Inc.
# Copyright 2012 OpenStack LLC.
# All Rights Reserved.
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

"""
Base utilities to build API operation managers and objects on top of.
"""

import copy
from inventoryclient import exc


def getid(obj):
    """Abstracts the common pattern of allowing both an object or an
    object's ID (UUID) as a parameter when dealing with relationships.
    """
    try:
        return obj.id
    except AttributeError:
        return obj


class Resource(object):
    """A resource represents a particular instance of an object (tenant, user,
    etc). This is pretty much just a bag for attributes.

    :param manager: Manager object
    :param info: dictionary representing resource attributes
    :param loaded: prevent lazy-loading if set to True
    """
    def __init__(self, manager, info, loaded=False):
        self.manager = manager
        self._info = info
        self._add_details(info)
        self._loaded = loaded

    def _add_details(self, info):
        for (k, v) in info.iteritems():
            setattr(self, k, v)

    def __getattr__(self, k):
        if k not in self.__dict__:
            # NOTE(bcwaldon): disallow lazy-loading if already loaded once
            if not self.is_loaded():
                self.get()
                return self.__getattr__(k)

            raise AttributeError(k)
        else:
            return self.__dict__[k]

    def __repr__(self):
        reprkeys = sorted(k for k in self.__dict__.keys() if k[0] != '_' and
                          k != 'manager')
        info = ", ".join("%s=%s" % (k, getattr(self, k)) for k in reprkeys)
        return "<%s %s>" % (self.__class__.__name__, info)

    def get(self):
        # set_loaded() first ... so if we have to bail, we know we tried.
        self.set_loaded(True)
        if not hasattr(self.manager, 'get'):
            return

        new = self.manager.get(self.id)
        if new:
            self._add_details(new._info)

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return False
        if hasattr(self, 'id') and hasattr(other, 'id'):
            return self.id == other.id
        return self._info == other._info

    def is_loaded(self):
        return self._loaded

    def set_loaded(self, val):
        self._loaded = val

    def to_dict(self):
        return copy.deepcopy(self._info)


class ResourceClassNone(Resource):
    def __repr__(self):
        return "<ResourceClassNone %s>" % self._info


class Manager(object):
    """Managers interact with a particular type of API and provide CRUD
    operations for them.
    """
    resource_class = ResourceClassNone

    def __init__(self, api):
        self.api = api

    def _create(self, url, body):
        resp, body = self.api.post(url, data=body)
        if body:
            if callable(self.resource_class):
                return self.resource_class(self, body)
            else:
                raise exc.InvalidAttribute(url)

    def _upload(self, url, body, data=None):
        files = {'file': ("for_upload",
                          body,
                          )}
        resp = self.api.post(url, files=files, data=data)
        return resp

    def _json_get(self, url):
        """send a GET request and return a json serialized object"""
        resp, body = self.api.get(url)
        return body

    def _format_body_data(self, body, response_key):
        if response_key:
            try:
                data = body[response_key]
            except KeyError:
                return []
        else:
            data = body

        if not isinstance(data, list):
            data = [data]

        return data

    def _list(self, url, response_key=None, obj_class=None, body=None):
        resp, body = self.api.get(url)

        if obj_class is None:
            obj_class = self.resource_class

        data = self._format_body_data(body, response_key)
        return [obj_class(self, res, loaded=True) for res in data if res]

    def _update(self, url, **kwargs):
        resp, body = self.api.patch(url, **kwargs)
        # PATCH/PUT requests may not return a body
        if body:
            if callable(self.resource_class):
                return self.resource_class(self, body)
            else:
                raise exc.InvalidAttribute(url)

    def _delete(self, url):
        self.api.delete(url)
