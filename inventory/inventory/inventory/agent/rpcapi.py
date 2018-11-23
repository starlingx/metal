# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8

# Copyright 2013 Hewlett-Packard Development Company, L.P.
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
#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""
Client side of the agent RPC API.
"""

from oslo_log import log
import oslo_messaging as messaging

from inventory.common import rpc
from inventory.objects import base as objects_base


LOG = log.getLogger(__name__)

MANAGER_TOPIC = 'inventory.agent_manager'


class AgentAPI(object):
    """Client side of the agent RPC API.

    API version history:

        1.0 - Initial version.
    """

    RPC_API_VERSION = '1.0'

    def __init__(self, topic=None):

        super(AgentAPI, self).__init__()
        self.topic = topic
        if self.topic is None:
            self.topic = MANAGER_TOPIC
        target = messaging.Target(topic=self.topic,
                                  version='1.0')
        serializer = objects_base.InventoryObjectSerializer()
        version_cap = self.RPC_API_VERSION
        self.client = rpc.get_client(target,
                                     version_cap=version_cap,
                                     serializer=serializer)

    def host_inventory(self, context, values, topic=None):
        """Synchronously, have a agent collect inventory for this host.

        Collect ihost inventory and report to conductor.

        :param context: request context.
        :param values: dictionary with initial values for new host object
        :returns: created ihost object, including all fields.
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'host_inventory',
                          values=values)

    def configure_ttys_dcd(self, context, uuid, ttys_dcd, topic=None):
        """Asynchronously, have the agent configure the getty on the serial
           console.

        :param context: request context.
        :param uuid: the host uuid
        :param ttys_dcd: the flag to enable/disable dcd
        :returns: none ... uses asynchronous cast().
        """
        # fanout / broadcast message to all inventory agents
        LOG.debug("AgentApi.configure_ttys_dcd: fanout_cast: sending "
                  "dcd update to agent: (%s) (%s" % (uuid, ttys_dcd))
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0',
                                    fanout=True)
        retval = cctxt.cast(context,
                            'configure_ttys_dcd',
                            uuid=uuid,
                            ttys_dcd=ttys_dcd)

        return retval

    def execute_command(self, context, host_uuid, command, topic=None):
        """Asynchronously, have the agent execute a command

        :param context: request context.
        :param host_uuid: the host uuid
        :param command: the command to execute
        :returns: none ... uses asynchronous cast().
        """
        # fanout / broadcast message to all inventory agents
        LOG.debug("AgentApi.update_cpu_config: fanout_cast: sending "
                  "host uuid: (%s) " % host_uuid)
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0',
                                    fanout=True)
        retval = cctxt.cast(context,
                            'execute_command',
                            host_uuid=host_uuid,
                            command=command)
        return retval

    def agent_update(self, context, host_uuid, force_updates,
                     cinder_device=None,
                     topic=None):
        """
        Asynchronously, have the agent update partitions, ipv and ilvg state

        :param context: request context
        :param host_uuid: the host uuid
        :param force_updates: list of inventory objects to update
        :param cinder_device: device by path of cinder volumes
        :return:  none ... uses asynchronous cast().
        """

        # fanout / broadcast message to all inventory agents
        LOG.info("AgentApi.agent_update: fanout_cast: sending "
                 "update request to agent for: (%s)" %
                 (', '.join(force_updates)))
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0',
                                    fanout=True)
        retval = cctxt.cast(context,
                            'agent_audit',
                            host_uuid=host_uuid,
                            force_updates=force_updates,
                            cinder_device=cinder_device)
        return retval

    def disk_format_gpt(self, context, host_uuid, idisk_dict,
                        is_cinder_device, topic=None):
        """Asynchronously, GPT format a disk.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param idisk_dict: values for disk object
        :param is_cinder_device: bool value tells if the idisk is for cinder
        :returns: pass or fail
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0',
                                    fanout=True)

        return cctxt.cast(context,
                          'disk_format_gpt',
                          host_uuid=host_uuid,
                          idisk_dict=idisk_dict,
                          is_cinder_device=is_cinder_device)
