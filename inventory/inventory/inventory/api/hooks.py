#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from inventory.common import context
from inventory.common.i18n import _
from inventory.conductor import rpcapi
from inventory.db import api as dbapi
from inventory.systemconfig import plugin as systemconfig_plugin
from oslo_config import cfg
from oslo_log import log
from oslo_serialization import jsonutils
from pecan import hooks
import webob

CONF = cfg.CONF
LOG = log.getLogger(__name__)


class ContextHook(hooks.PecanHook):
    """Configures a request context and attaches it to the request.

    The following HTTP request headers are used:

    X-User-Name:
        Used for context.user_name.

    X-User-Id:
        Used for context.user_id.

    X-Project-Name:
        Used for context.project.

    X-Project-Id:
        Used for context.project_id.

    X-Auth-Token:
        Used for context.auth_token.

    X-Roles:
        Used for context.roles.

    X-Service_Catalog:
        Used for context.service_catalog.
    """

    def before(self, state):
        headers = state.request.headers
        environ = state.request.environ
        user_name = headers.get('X-User-Name')
        user_id = headers.get('X-User-Id')
        project = headers.get('X-Project-Name')
        project_id = headers.get('X-Project-Id')
        domain_id = headers.get('X-User-Domain-Id')
        domain_name = headers.get('X-User-Domain-Name')
        auth_token = headers.get('X-Auth-Token')
        roles = headers.get('X-Roles', '').split(',')
        catalog_header = headers.get('X-Service-Catalog')
        service_catalog = None
        if catalog_header:
            try:
                service_catalog = jsonutils.loads(catalog_header)
            except ValueError:
                raise webob.exc.HTTPInternalServerError(
                    _('Invalid service catalog json.'))

        auth_token_info = environ.get('keystone.token_info')
        auth_url = CONF.keystone_authtoken.auth_uri

        state.request.context = context.make_context(
            auth_token=auth_token,
            auth_url=auth_url,
            auth_token_info=auth_token_info,
            user_name=user_name,
            user_id=user_id,
            project_name=project,
            project_id=project_id,
            domain_id=domain_id,
            domain_name=domain_name,
            roles=roles,
            service_catalog=service_catalog
        )


class DBHook(hooks.PecanHook):
    """Attach the dbapi object to the request so controllers can get to it."""

    def before(self, state):
        state.request.dbapi = dbapi.get_instance()


class RPCHook(hooks.PecanHook):
    """Attach the rpcapi object to the request so controllers can get to it."""

    def before(self, state):
        state.request.rpcapi = rpcapi.ConductorAPI()


class SystemConfigHook(hooks.PecanHook):
    """Attach the rpcapi object to the request so controllers can get to it."""

    def before(self, state):
        state.request.systemconfig = systemconfig_plugin.SystemConfigPlugin(
            invoke_kwds={'context': state.request.context})

        # state.request.systemconfig = systemconfig.SystemConfigOperator(
        #     state.request.context,
        #     state.request.dbapi)
