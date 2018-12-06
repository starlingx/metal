#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


import sys

import eventlet
from oslo_config import cfg
from oslo_log import log as logging
from oslo_service import systemd
from oslo_service import wsgi

import logging as std_logging

from inventory.api import app
from inventory.api import config
from inventory.common.i18n import _

api_opts = [
    cfg.StrOpt('bind_host',
               default="0.0.0.0",
               help=_('IP address for inventory api to listen')),
    cfg.IntOpt('bind_port',
               default=6380,
               help=_('listen port for inventory api')),
    cfg.StrOpt('bind_host_pxe',
               default="0.0.0.0",
               help=_('IP address for inventory api pxe to listen')),
    cfg.IntOpt('api_workers', default=2,
               help=_("number of api workers")),
    cfg.IntOpt('limit_max',
               default=1000,
               help='the maximum number of items returned in a single '
                    'response from a collection resource')
]


CONF = cfg.CONF


LOG = logging.getLogger(__name__)
eventlet.monkey_patch(os=False)


def main():

    config.init(sys.argv[1:])
    config.setup_logging()

    application = app.load_paste_app()

    CONF.register_opts(api_opts, 'api')

    host = CONF.api.bind_host
    port = CONF.api.bind_port
    workers = CONF.api.api_workers

    if workers < 1:
        LOG.warning("Wrong worker number, worker = %(workers)s", workers)
        workers = 1

    LOG.info("Serving on http://%(host)s:%(port)s with %(workers)s",
             {'host': host, 'port': port, 'workers': workers})
    systemd.notify_once()
    service = wsgi.Server(CONF, CONF.prog, application, host, port)

    app.serve(service, CONF, workers)

    pxe_host = CONF.api.bind_host_pxe
    if pxe_host:
        pxe_service = wsgi.Server(CONF, CONF.prog, application, pxe_host, port)
        app.serve_pxe(pxe_service, CONF, 1)

    LOG.debug("Configuration:")
    CONF.log_opt_values(LOG, std_logging.DEBUG)

    app.wait()
    if pxe_host:
        app.wait_pxe()


if __name__ == '__main__':
    main()
