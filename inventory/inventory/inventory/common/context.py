#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from keystoneauth1.access import service_catalog as k_service_catalog
from keystoneauth1 import plugin
from oslo_config import cfg
from oslo_context import context

from inventory.common import policy

REQUIRED_SERVICE_TYPES = ('faultmanagement',
                          'nfv',
                          'patching',
                          'platform',
                          'smapi',
                          )


CONF = cfg.CONF


class _ContextAuthPlugin(plugin.BaseAuthPlugin):
    """A keystoneauth auth plugin that uses the values from the Context.

    Ideally we would use the plugin provided by auth_token middleware however
    this plugin isn't serialized yet so we construct one from the serialized
    auth data.
    """

    def __init__(self, auth_token, sc):
        super(_ContextAuthPlugin, self).__init__()

        self.auth_token = auth_token
        self.service_catalog = k_service_catalog.ServiceCatalogV2(sc)

    def get_token(self, *args, **kwargs):
        return self.auth_token

    def get_endpoint(self, session, service_type=None, interface=None,
                     region_name=None, service_name=None, **kwargs):
        return self.service_catalog.url_for(service_type=service_type,
                                            service_name=service_name,
                                            interface=interface,
                                            region_name=region_name)


class RequestContext(context.RequestContext):
    """Extends security contexts from the OpenStack common library."""

    def __init__(self, auth_token=None, auth_url=None, domain_id=None,
                 domain_name=None, user_name=None, user_id=None,
                 user_domain_name=None, user_domain_id=None,
                 project_name=None, project_id=None, roles=None,
                 is_admin=None, read_only=False, show_deleted=False,
                 request_id=None, trust_id=None, auth_token_info=None,
                 all_tenants=False, password=None, service_catalog=None,
                 user_auth_plugin=None,
                 **kwargs):
        """Stores several additional request parameters:

        :param domain_id: The ID of the domain.
        :param domain_name: The name of the domain.
        :param user_domain_id: The ID of the domain to
                               authenticate a user against.
        :param user_domain_name: The name of the domain to
                                 authenticate a user against.
        :param service_catalog: Specifies the service_catalog
        """
        super(RequestContext, self).__init__(auth_token=auth_token,
                                             user=user_name,
                                             tenant=project_name,
                                             is_admin=is_admin,
                                             read_only=read_only,
                                             show_deleted=show_deleted,
                                             request_id=request_id,
                                             roles=roles)

        self.user_name = user_name
        self.user_id = user_id
        self.project_name = project_name
        self.project_id = project_id
        self.domain_id = domain_id
        self.domain_name = domain_name
        self.user_domain_id = user_domain_id
        self.user_domain_name = user_domain_name
        self.auth_url = auth_url
        self.auth_token_info = auth_token_info
        self.trust_id = trust_id
        self.all_tenants = all_tenants
        self.password = password

        if service_catalog:
            # Only include required parts of service_catalog
            self.service_catalog = [s for s in service_catalog
                                    if s.get('type') in
                                    REQUIRED_SERVICE_TYPES]
        else:
            # if list is empty or none
            self.service_catalog = []

        self.user_auth_plugin = user_auth_plugin
        if is_admin is None:
            self.is_admin = policy.check_is_admin(self)
        else:
            self.is_admin = is_admin

    def to_dict(self):
        value = super(RequestContext, self).to_dict()
        value.update({'auth_token': self.auth_token,
                      'auth_url': self.auth_url,
                      'domain_id': self.domain_id,
                      'domain_name': self.domain_name,
                      'user_domain_id': self.user_domain_id,
                      'user_domain_name': self.user_domain_name,
                      'user_name': self.user_name,
                      'user_id': self.user_id,
                      'project_name': self.project_name,
                      'project_id': self.project_id,
                      'is_admin': self.is_admin,
                      'read_only': self.read_only,
                      'roles': self.roles,
                      'show_deleted': self.show_deleted,
                      'request_id': self.request_id,
                      'trust_id': self.trust_id,
                      'auth_token_info': self.auth_token_info,
                      'password': self.password,
                      'all_tenants': self.all_tenants,
                      'service_catalog': self.service_catalog})
        return value

    @classmethod
    def from_dict(cls, values):
        return cls(**values)

    def get_auth_plugin(self):
        if self.user_auth_plugin:
            return self.user_auth_plugin
        else:
            return _ContextAuthPlugin(self.auth_token, self.service_catalog)


def make_context(*args, **kwargs):
    return RequestContext(*args, **kwargs)


def get_admin_context(show_deleted="no"):
    context = make_context(tenant=None,
                           is_admin=True,
                           show_deleted=show_deleted)
    return context
