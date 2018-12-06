# Copyright 2016 Intel Corporation
# Copyright 2013 Hewlett-Packard Development Company, L.P.
# Copyright 2013 Red Hat, Inc.
# Copyright 2010 United States Government as represented by the
# Administrator of the National Aeronautics and Space Administration.
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

import os
import socket
import tempfile

from inventory.common.i18n import _
from oslo_config import cfg


api_opts = [
    cfg.StrOpt(
        'auth_strategy',
        default='keystone',
        choices=['noauth', 'keystone'],
        help=_('Authentication strategy used by inventory-api. "noauth" '
               'should not be used in a production environment because all '
               'authentication will be disabled.')),
    cfg.BoolOpt('debug_tracebacks_in_api',
                default=False,
                help=_('Return server tracebacks in the API response for any '
                       'error responses. WARNING: this is insecure '
                       'and should not be used in a production environment.')),
    cfg.BoolOpt('pecan_debug',
                default=False,
                help=_('Enable pecan debug mode. WARNING: this is insecure '
                       'and should not be used in a production environment.')),
    cfg.StrOpt('default_resource_class',
               help=_('Resource class to use for new nodes when no resource '
                      'class is provided in the creation request.')),
]

exc_log_opts = [
    cfg.BoolOpt('fatal_exception_format_errors',
                default=False,
                help=_('Used if there is a formatting error when generating '
                       'an exception message (a programming error). If True, '
                       'raise an exception; if False, use the unformatted '
                       'message.')),
]

# NOTE(mariojv) By default, accessing this option when it's unset will return
# None, indicating no notifications will be sent. oslo.config returns None by
# default for options without set defaults that aren't required.
notification_opts = [
    cfg.StrOpt('notification_level',
               choices=[('debug', _('"debug" level')),
                        ('info', _('"info" level')),
                        ('warning', _('"warning" level')),
                        ('error', _('"error" level')),
                        ('critical', _('"critical" level'))],
               help=_('Specifies the minimum level for which to send '
                      'notifications. If not set, no notifications will '
                      'be sent. The default is for this option to be unset.'))
]

path_opts = [
    cfg.StrOpt(
        'pybasedir',
        default=os.path.abspath(os.path.join(os.path.dirname(__file__),
                                             '../')),
        sample_default='/usr/lib64/python/site-packages/inventory',
        help=_('Directory where the inventory python module is '
               'installed.')),
    cfg.StrOpt('bindir',
               default='$pybasedir/bin',
               help=_('Directory where inventory binaries are installed.')),
    cfg.StrOpt('state_path',
               default='$pybasedir',
               help=_("Top-level directory for maintaining inventory's "
                      "state.")),
]

service_opts = [
    cfg.StrOpt('host',
               default=socket.getfqdn(),
               sample_default='localhost',
               help=_('Name of this node. This can be an opaque identifier. '
                      'It is not necessarily a hostname, FQDN, or IP address. '
                      'However, the node name must be valid within '
                      'an AMQP key, and if using ZeroMQ (will be removed in '
                      'the Stein release), a valid hostname, FQDN, '
                      'or IP address.')),
]

utils_opts = [
    cfg.StrOpt('rootwrap_config',
               default="/etc/inventory/rootwrap.conf",
               help=_('Path to the rootwrap configuration file to use for '
                      'running commands as root.')),
    cfg.StrOpt('tempdir',
               default=tempfile.gettempdir(),
               sample_default='/tmp',
               help=_('Temporary working directory, default is Python temp '
                      'dir.')),
]


def register_opts(conf):
    conf.register_opts(exc_log_opts)
    conf.register_opts(notification_opts)
    conf.register_opts(path_opts)
    conf.register_opts(service_opts)
    conf.register_opts(utils_opts)
