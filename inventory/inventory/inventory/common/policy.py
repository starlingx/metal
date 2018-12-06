# Copyright (c) 2011 OpenStack Foundation
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
#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Policy Engine For Inventory."""

from oslo_concurrency import lockutils
from oslo_config import cfg
from oslo_log import log
from oslo_policy import policy


base_rules = [
    policy.RuleDefault('admin_required', 'role:admin or is_admin:1',
                       description='Who is considered an admin'),
    policy.RuleDefault('admin_api', 'is_admin_required:True',
                       description='admin API requirement'),
    policy.RuleDefault('default', 'rule:admin_api',
                       description='default rule'),
]

_ENFORCER = None
CONF = cfg.CONF
LOG = log.getLogger(__name__)


# we can get a policy enforcer by this init.
# oslo policy support change policy rule dynamically.
# at present, policy.enforce will reload the policy rules when it checks
# the policy files have been touched.
@lockutils.synchronized('policy_enforcer')
def init_enforcer(policy_file=None, rules=None,
                  default_rule=None, use_conf=True, overwrite=True):
    """Init an Enforcer class.

        :param policy_file: Custom policy file to use, if none is
                            specified, ``conf.policy_file`` will be
                            used.
        :param rules: Default dictionary / Rules to use. It will be
                      considered just in the first instantiation. If
                      :meth:`load_rules` with ``force_reload=True``,
                      :meth:`clear` or :meth:`set_rules` with
                      ``overwrite=True`` is called this will be overwritten.
        :param default_rule: Default rule to use, conf.default_rule will
                             be used if none is specified.
        :param use_conf: Whether to load rules from cache or config file.
        :param overwrite: Whether to overwrite existing rules when reload rules
                          from config file.
    """
    global _ENFORCER
    if not _ENFORCER:
        # http://docs.openstack.org/developer/oslo.policy/usage.html
        _ENFORCER = policy.Enforcer(CONF,
                                    policy_file=policy_file,
                                    rules=rules,
                                    default_rule=default_rule,
                                    use_conf=use_conf,
                                    overwrite=overwrite)
        _ENFORCER.register_defaults(base_rules)
    return _ENFORCER


def get_enforcer():
    """Provides access to the single instance of Policy enforcer."""

    if not _ENFORCER:
        init_enforcer()

    return _ENFORCER


def check_is_admin(context):
    """Whether or not role contains 'admin' role according to policy setting.

    """
    init_enforcer()

    target = {}
    credentials = context.to_dict()

    return _ENFORCER.enforce('context_is_admin', target, credentials)
