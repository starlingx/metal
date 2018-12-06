#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


from inventory.api import hooks
from inventory.common import config
from inventory import objects
from keystoneauth1 import loading as ks_loading
from oslo_config import cfg
from oslo_log import log as logging
import pbr.version
import sys

LOG = logging.getLogger(__name__)

sysinv_group = cfg.OptGroup(
    'sysinv',
    title='Sysinv Options',
    help="Configuration options for the platform service")

sysinv_opts = [
    cfg.StrOpt('catalog_info',
               default='platform:sysinv:internalURL',
               help="Service catalog Look up info."),
    cfg.StrOpt('os_region_name',
               default='RegionOne',
               help="Region name of this node. It is used for catalog lookup"),
]

version_info = pbr.version.VersionInfo('inventory')

# Pecan Application Configurations
app = {
    'root': 'inventory.api.controllers.root.RootController',
    'modules': ['inventory.api'],
    'hooks': [
        hooks.DBHook(),
        hooks.ContextHook(),
        hooks.RPCHook(),
        hooks.SystemConfigHook(),
    ],
    'acl_public_routes': [
        '/',
        '/v1',
    ],
}


def init(args, **kwargs):
    cfg.CONF.register_group(sysinv_group)
    cfg.CONF.register_opts(sysinv_opts, group=sysinv_group)
    ks_loading.register_session_conf_options(cfg.CONF,
                                             sysinv_group.name)
    logging.register_options(cfg.CONF)

    cfg.CONF(args=args, project='inventory',
             version='%%(prog)s %s' % version_info.release_string(),
             **kwargs)
    objects.register_all()
    config.parse_args(args)


def setup_logging():
    """Sets up the logging options for a log with supplied name."""
    logging.setup(cfg.CONF, "inventory")
    LOG.debug("Logging enabled!")
    LOG.debug("%(prog)s version %(version)s",
              {'prog': sys.argv[0],
               'version': version_info.release_string()})
    LOG.debug("command line: %s", " ".join(sys.argv))
