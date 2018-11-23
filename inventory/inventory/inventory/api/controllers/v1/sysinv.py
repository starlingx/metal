#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


from cgtsclient.v1 import client as cgts_client
from inventory.api import config
from keystoneauth1 import loading as ks_loading
from oslo_config import cfg
from oslo_log import log


CONF = cfg.CONF

LOG = log.getLogger(__name__)

_SESSION = None


def cgtsclient(context, version=1, endpoint=None):
    """Constructs a cgts client object for making API requests.

    :param context: The FM request context for auth.
    :param version: API endpoint version.
    :param endpoint: Optional If the endpoint is not available, it will be
                     retrieved from session
    """
    global _SESSION

    if not _SESSION:
        _SESSION = ks_loading.load_session_from_conf_options(
            CONF, config.sysinv_group.name)

    auth_token = context.auth_token
    if endpoint is None:
        auth = context.get_auth_plugin()
        service_type, service_name, interface = \
            CONF.sysinv.catalog_info.split(':')
        service_parameters = {'service_type': service_type,
                              'service_name': service_name,
                              'interface': interface,
                              'region_name': CONF.sysinv.os_region_name}
        endpoint = _SESSION.get_endpoint(auth, **service_parameters)

    return cgts_client.Client(version=version,
                              endpoint=endpoint,
                              token=auth_token)
