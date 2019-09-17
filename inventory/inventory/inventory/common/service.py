# -*- encoding: utf-8 -*-
#
# Copyright © 2012 eNovance <licensing@enovance.com>
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations
# under the License.

from inventory.common import config
from inventory.conf import opts
from inventory import objects
from oslo_config import cfg
from oslo_log import log
from oslo_service import service


cfg.CONF.register_opts([
    cfg.IntOpt('periodic_interval',
               default=60,
               help='seconds between running periodic tasks'),
])

CONF = cfg.CONF


def prepare_service(argv=None):
    argv = [] if argv is None else argv

    opts.update_opt_defaults()
    log.register_options(CONF)
    CONF(argv[1:], project='inventory')
    config.parse_args(argv)
    log.setup(CONF, 'inventory')
    objects.register_all()


def process_launcher():
    return service.ProcessLauncher(CONF)
