#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


from oslo_log import log
LOG = log.getLogger(__name__)


class APIResourceWrapper(object):
    """Simple wrapper for api objects.

    Define _attrs on the child class and pass in the
    api object as the only argument to the constructor
    """
    _attrs = []
    _apiresource = None  # Make sure _apiresource is there even in __init__.

    def __init__(self, apiresource):
        self._apiresource = apiresource

    def __getattribute__(self, attr):
        try:
            return object.__getattribute__(self, attr)
        except AttributeError:
            if attr not in self._attrs:
                raise
            # __getattr__ won't find properties
            return getattr(self._apiresource, attr)

    def __repr__(self):
        return "<%s: %s>" % (self.__class__.__name__,
                             dict((attr, getattr(self, attr))
                                  for attr in self._attrs
                                  if hasattr(self, attr)))

    def as_dict(self):
        obj = {}
        for key in self._attrs:
            obj[key] = getattr(self._apiresource, key, None)
        return obj
