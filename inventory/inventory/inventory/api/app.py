#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from oslo_config import cfg
from oslo_log import log
from oslo_service import service
from oslo_service import wsgi
import pecan

from inventory.api import config
from inventory.api import middleware
from inventory.common.i18n import _
from inventory.common import policy

CONF = cfg.CONF

LOG = log.getLogger(__name__)

_launcher = None
_launcher_pxe = None


def get_pecan_config():
    # Set up the pecan configuration
    filename = config.__file__.replace('.pyc', '.py')
    return pecan.configuration.conf_from_file(filename)


def setup_app(config=None):
    policy.init_enforcer()

    if not config:
        config = get_pecan_config()

    pecan.configuration.set_config(dict(config), overwrite=True)
    app_conf = dict(config.app)

    app = pecan.make_app(
        app_conf.pop('root'),
        debug=CONF.debug,
        logging=getattr(config, 'logging', {}),
        force_canonical=getattr(config.app, 'force_canonical', True),
        guess_content_type_from_ext=False,
        wrap_app=middleware.ParsableErrorMiddleware,
        **app_conf
    )
    return app


def load_paste_app(app_name=None):
    """Loads a WSGI app from a paste config file."""
    if app_name is None:
        app_name = cfg.CONF.prog

    loader = wsgi.Loader(cfg.CONF)
    app = loader.load_app(app_name)
    return app


def app_factory(global_config, **local_conf):
    return setup_app()


def serve(api_service, conf, workers=1):
    global _launcher

    if _launcher:
        raise RuntimeError(_('serve() _launcher can only be called once'))

    _launcher = service.launch(conf, api_service, workers=workers)


def serve_pxe(api_service, conf, workers=1):
    global _launcher_pxe

    if _launcher_pxe:
        raise RuntimeError(_('serve() _launcher_pxe can only be called once'))

    _launcher_pxe = service.launch(conf, api_service, workers=workers)


def wait():
    _launcher.wait()


def wait_pxe():
    _launcher_pxe.wait()
