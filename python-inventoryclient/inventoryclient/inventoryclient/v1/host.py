#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# -*- encoding: utf-8 -*-
#

from inventoryclient.common import base
from inventoryclient.common import utils
from inventoryclient import exc
import json


CREATION_ATTRIBUTES = ['hostname', 'personality', 'subfunctions',
                       'mgmt_mac', 'mgmt_ip',
                       'bm_ip', 'bm_type', 'bm_username',
                       'bm_password', 'serialid', 'location',
                       'boot_device', 'rootfs_device', 'install_output',
                       'console', 'tboot', 'ttys_dcd',
                       'administrative', 'operational', 'availability',
                       'invprovision']


class Host(base.Resource):
    def __repr__(self):
        return "<Host %s>" % self._info


class HostManager(base.Manager):
    resource_class = Host

    @staticmethod
    def _path(id=None):
        return '/v1/hosts/%s' % id if id else '/v1/hosts'

    def list(self):
        return self._list(self._path(), "hosts")

    def list_port(self, host_id):
        path = "%s/ports" % host_id
        return self._list(self._path(path), "ports")

    def list_ethernet_port(self, host_id):
        path = "%s/ethernet_ports" % host_id
        return self._list(self._path(path), "ethernet_ports")

    def list_personality(self, personality):
        path = self._path() + "?personality=%s" % personality
        return self._list(path, "hosts")

    def get(self, host_id):
        try:
            return self._list(self._path(host_id))[0]
        except IndexError:
            return None

    def create(self, **kwargs):
        new = {}
        for (key, value) in kwargs.items():
            if key in CREATION_ATTRIBUTES:
                new[key] = value
            else:
                raise exc.InvalidAttribute()
        return self._create(self._path(), new)

    def upgrade(self, hostid, force):
        new = {}
        new['force'] = force
        resp, body = self.api.json_request(
            'POST', self._path(hostid) + "/upgrade", body=new)
        return self.resource_class(self, body)

    def downgrade(self, hostid, force):
        new = {}
        new['force'] = force
        resp, body = self.api.json_request(
            'POST', self._path(hostid) + "/downgrade", body=new)
        return self.resource_class(self, body)

    def create_many(self, body):
        return self._upload(self._path() + "/bulk_add", body)

    def update_install_uuid(self, hostid, install_uuid):
        path = self._path(hostid) + "/state/update_install_uuid"

        self.api.json_request('PUT', path, body=install_uuid)

    def delete(self, host_id):
        return self._delete(self._path(host_id))

    def update(self, host_id, patch):
        return self._update(self._path(host_id),
                            data=(json.dumps(patch)))

    def bulk_export(self):
        result = self._json_get(self._path('bulk_export'))
        return result


def _find_host(cc, host):
    if host.isdigit() or utils.is_uuid_like(host):
        try:
            h = cc.host.get(host)
        except exc.HTTPNotFound:
            raise exc.CommandError('host not found: %s' % host)
        else:
            return h
    else:
        hostlist = cc.host.list()
        for h in hostlist:
            if h.hostname == host:
                return h
        else:
            raise exc.CommandError('host not found: %s' % host)
