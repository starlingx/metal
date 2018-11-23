#    Copyright 2016 Ericsson AB
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

"""
File to store configurations
"""
from inventory.common import rpc
from inventory import version
from oslo_config import cfg

global_opts = [
    cfg.BoolOpt('use_default_quota_class',
                default=True,
                help='Enables or disables use of default quota class '
                     'with default quota.'),
    cfg.IntOpt('report_interval',
               default=60,
               help='Seconds between running periodic reporting tasks.'),
]

# Pecan_opts
pecan_opts = [
    cfg.StrOpt(
        'root',
        default='inventory.api.controllers.root.RootController',
        help='Pecan root controller'
    ),
    cfg.ListOpt(
        'modules',
        default=["inventory.api"],
        help='A list of modules where pecan will search for applications.'
    ),
    cfg.BoolOpt(
        'debug',
        default=False,
        help='Enables the ability to display tracebacks in the browser and'
             'interactively debug during development.'
    ),
    cfg.BoolOpt(
        'auth_enable',
        default=True,
        help='Enables user authentication in pecan.'
    )
]


# OpenStack credentials used for Endpoint Cache
cache_opts = [
    cfg.StrOpt('auth_uri',
               help='Keystone authorization url'),
    cfg.StrOpt('identity_uri',
               help='Keystone service url'),
    cfg.StrOpt('admin_username',
               help='Username of admin account, needed when'
                    ' auto_refresh_endpoint set to True'),
    cfg.StrOpt('admin_password',
               help='Password of admin account, needed when'
                    ' auto_refresh_endpoint set to True'),
    cfg.StrOpt('admin_tenant',
               help='Tenant name of admin account, needed when'
                    ' auto_refresh_endpoint set to True'),
    cfg.StrOpt('admin_user_domain_name',
               default='Default',
               help='User domain name of admin account, needed when'
                    ' auto_refresh_endpoint set to True'),
    cfg.StrOpt('admin_project_domain_name',
               default='Default',
               help='Project domain name of admin account, needed when'
                    ' auto_refresh_endpoint set to True')
]

scheduler_opts = [
    cfg.BoolOpt('periodic_enable',
                default=True,
                help='boolean value for enable/disenable periodic tasks'),
    cfg.IntOpt('periodic_interval',
               default=600,
               help='periodic time interval for automatic quota sync job'
                    ' and resource sync audit')
]

common_opts = [
    cfg.IntOpt('workers', default=1,
               help='number of workers'),
]

scheduler_opt_group = cfg.OptGroup('scheduler',
                                   title='Scheduler options for periodic job')

# The group stores the pecan configurations.
pecan_group = cfg.OptGroup(name='pecan',
                           title='Pecan options')

cache_opt_group = cfg.OptGroup(name='cache',
                               title='OpenStack Credentials')


def list_opts():
    yield cache_opt_group.name, cache_opts
    yield scheduler_opt_group.name, scheduler_opts
    yield pecan_group.name, pecan_opts
    yield None, global_opts
    yield None, common_opts


def register_options():
    for group, opts in list_opts():
        cfg.CONF.register_opts(opts, group=group)


def parse_args(argv, default_config_files=None):
    rpc.set_defaults(control_exchange='inventory')
    cfg.CONF(argv[1:],
             project='inventory',
             version=version.version_info.release_string(),
             default_config_files=default_config_files)
    rpc.init(cfg.CONF)
