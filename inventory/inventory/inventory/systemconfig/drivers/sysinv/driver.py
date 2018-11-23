#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from inventory.systemconfig.drivers import base
from inventory.systemconfig import plugin

import cgtsclient as sysinv_client
from keystoneauth1.access import service_catalog as k_service_catalog
from keystoneclient.auth.identity import v3
from keystoneclient import session
from oslo_config import cfg
from oslo_log import log

CONF = cfg.CONF

LOG = log.getLogger(__name__)

sysinv_group = cfg.OptGroup(
    'sysinv',
    title='SysInv Options',
    help="Configuration options for the sysinv service")

sysinv_opts = [
    cfg.StrOpt('catalog_info',
               default='platform:sysinv:internalURL',
               help="Service catalog Look up info."),
    cfg.StrOpt('os_region_name',
               default='RegionOne',
               help="Region name of this node. It is used for catalog lookup")
]

CONF.register_group(sysinv_group)
CONF.register_opts(sysinv_opts, group=sysinv_group)


def _get_keystone_session(auth_url):
    auth = v3.Password(auth_url=auth_url,
                       username=cfg.CONF.KEYSTONE_AUTHTOKEN.username,
                       password=cfg.CONF.KEYSTONE_AUTHTOKEN.password,
                       user_domain_name=cfg.CONF.KEYSTONE_AUTHTOKEN.
                       user_domain_name,
                       project_name=cfg.CONF.KEYSTONE_AUTHTOKEN.
                       project_name,
                       project_domain_name=cfg.CONF.KEYSTONE_AUTHTOKEN.
                       project_domain_name)
    keystone_session = session.Session(auth=auth)
    return keystone_session


def sysinvclient(context, version=1, endpoint=None):
    """Constructs a sysinv client object for making API requests.

    :param context: The request context for auth.
    :param version: API endpoint version.
    :param endpoint: Optional If the endpoint is not available,
                     it will be retrieved from context
    """

    region_name = CONF.sysinv.os_region_name
    if not context.service_catalog:
        # Obtain client via keystone session
        auth_url = CONF.KEYSTONE_AUTHTOKEN.auth_url + "/v3"
        session = _get_keystone_session(auth_url)
        LOG.debug("sysinvclient auth_url=%s region_name=%s session=%s" %
                  (auth_url, region_name, session))

        return sysinv_client.Client(
            session=session,
            version=version,
            auth_url=auth_url,
            endpoint_type='internalURL',
            region_name=region_name)

    auth_token = context.auth_token
    if endpoint is None:
        sc = k_service_catalog.ServiceCatalogV2(context.service_catalog)
        service_type, service_name, interface = \
            CONF.sysinv.catalog_info.split(':')

        service_parameters = {'service_type': service_type,
                              'service_name': service_name,
                              'interface': interface,
                              'region_name': region_name}
        endpoint = sc.url_for(**service_parameters)

    return sysinv_client.Client(version=version,
                                endpoint=endpoint,
                                auth_token=auth_token)


class SysinvSystemConfigDriver(base.SystemConfigDriverBase):
    """Class to encapsulate SystemConfig driver operations"""
    def __init__(self, **kwargs):
        self.context = kwargs.get('context')
        self.neighbours = []
        self.neighbour_audit_count = 0
        self._client = sysinvclient(self.context)
        LOG.info("SysinvSystemConfigDriver kwargs=%s self.context=%s" %
                 (kwargs, self.context))

    def initialize(self):
        self.__init__()

    def system_get(self):
        systems = self._client.isystem.list()
        if not systems:
            return None
        return [plugin.System(n) for n in systems]

    def system_get_one(self):
        systems = self._client.isystem.list()
        if not systems:
            return None
        return [plugin.System(n) for n in systems][0]

    def host_interface_list(self, host_id):
        interfaces = self._client.iinterface.list(host_id)
        return [plugin.Interface(n) for n in interfaces]

    def host_interface_get(self, interface_id):
        interface = self._client.iinterface.get(interface_id)
        if not interface:
            raise ValueError(
                'No match found for interface_id "%s".' % interface_id)
        return plugin.Interface(interface)

    def host_configure_check(self, host_uuid):
        LOG.info("host_configure_check %s" % host_uuid)
        # host = self._client.ihost.get(host_uuid)
        capabilities = []
        host = self._client.ihost.configure_check(host_uuid, capabilities)
        LOG.info("host_configure_check host=%s" % host)
        if host:
            return True
        else:
            return False

    def host_configure(self, host_uuid, do_compute_apply=False):
        LOG.info("simulate host_configure")
        # host = self._client.ihost.get(host_uuid)
        # TODO(sc) for host configuration
        host = self._client.ihost.configure(host_uuid, do_compute_apply)
        if host:
            return plugin.Host(host)
        else:
            return None

    def host_unconfigure(self, host_uuid):
        LOG.info("simulate host_unconfigure")
        host = self._client.ihost.get(host_uuid)
        if host:
            return plugin.Host(host)
        else:
            return None

        host = self._client.ihost.unconfigure(host_uuid)

        return host

    def network_list(self):
        networks = self._client.network.list()
        return [plugin.Network(n) for n in networks]

    def network_get_by_type(self, network_type):
        networks = self._client.network.list()
        if networks:
            return [plugin.Network(n) for n in networks
                    if n.type == network_type][0]
        return []

    def network_get(self, network_uuid):
        network = self._client.network.get(network_uuid)
        if not network:
            raise ValueError(
                'No match found for network_uuid "%s".' % network_uuid)
        return plugin.Network(network)

    def address_list_by_interface(self, interface_id):
        addresses = self._client.address.list_by_interface(interface_id)
        return [plugin.Address(n) for n in addresses]

    def address_list_by_field_value(self, field, value):
        q = [{'field': field,
              'type': '',
              'value': value,
              'op': 'eq'}]
        addresses = self._client.address.list(q)
        return [plugin.Address(n) for n in addresses]

    def address_get(self, address_uuid):
        address = self._client.address.get(address_uuid)
        if not address:
            raise ValueError(
                'No match found for address uuid "%s".' % address_uuid)
        return plugin.Address(address)

    def address_pool_list(self):
        pools = self._client.address_pool.list()
        return [plugin.AddressPool(p) for p in pools]

    def address_pool_get(self, address_pool_uuid):
        pool = self._client.address_pool.get(address_pool_uuid)
        if not pool:
            raise ValueError(
                'No match found for address pool uuid "%s".' %
                address_pool_uuid)
        return plugin.AddressPool(pool)

    def route_list_by_interface(self, interface_id):
        routees = self._client.route.list_by_interface(interface_id)
        return [plugin.Route(n) for n in routees]

    def route_get(self, route_uuid):
        route = self._client.route.get(route_uuid)
        if not route:
            raise ValueError(
                'No match found for route uuid "%s".' % route_uuid)
        return plugin.Route(route)

    def address_get_by_name(self, name):
        field = 'name'
        value = name
        addresses = self.address_list_by_field_value(field, value)
        if len(addresses) == 1:
            address = addresses[0]
            LOG.info("address_get_by_name via systemconfig "
                     "name=%s address=%s" %
                     (address.name, address.address))
        else:
            LOG.error("Unexpected address_get_by_name %s %s" %
                      (name, addresses))
            return None
