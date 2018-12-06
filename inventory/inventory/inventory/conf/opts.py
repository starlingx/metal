#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
from oslo_log import log


def update_opt_defaults():
    log.set_defaults(
        default_log_levels=[
            'amqp=WARNING',
            'amqplib=WARNING',
            'qpid.messaging=INFO',
            # TODO(therve): when bug #1685148 is fixed in oslo.messaging, we
            # should be able to remove one of those 2 lines.
            'oslo_messaging=INFO',
            'oslo.messaging=INFO',
            'sqlalchemy=WARNING',
            'stevedore=INFO',
            'eventlet.wsgi.server=INFO',
            'iso8601=WARNING',
            'requests=WARNING',
            'neutronclient=WARNING',
            'urllib3.connectionpool=WARNING',
            'keystonemiddleware.auth_token=INFO',
            'keystoneauth.session=INFO',
        ]
    )

#            'glanceclient=WARNING',
