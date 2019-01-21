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
Client side of the conductor RPC API.
"""

from oslo_log import log
import oslo_messaging as messaging

from inventory.common import rpc
from inventory.objects import base as objects_base

LOG = log.getLogger(__name__)

MANAGER_TOPIC = 'inventory.conductor_manager'


class ConductorAPI(object):
    """Client side of the conductor RPC API.

    API version history:

        1.0 - Initial version.
    """

    RPC_API_VERSION = '1.0'

    # The default namespace, which can be overridden in a subclass.
    RPC_API_NAMESPACE = None

    def __init__(self, topic=None):
        super(ConductorAPI, self).__init__()
        self.topic = topic
        if self.topic is None:
            self.topic = MANAGER_TOPIC
        target = messaging.Target(topic=self.topic,
                                  version='1.0')
        serializer = objects_base.InventoryObjectSerializer()
        # release_ver = versions.RELEASE_MAPPING.get(CONF.pin_release_version)
        # version_cap = (release_ver['rpc'] if release_ver
        #                else self.RPC_API_VERSION)
        version_cap = self.RPC_API_VERSION
        self.client = rpc.get_client(target,
                                     version_cap=version_cap,
                                     serializer=serializer)

    @staticmethod
    def make_namespaced_msg(method, namespace, **kwargs):
        return {'method': method, 'namespace': namespace, 'args': kwargs}

    def make_msg(self, method, **kwargs):
        return self.make_namespaced_msg(method, self.RPC_API_NAMESPACE,
                                        **kwargs)

    # This is to be in inventory? However, it'll need to know the ip_address!
    def handle_dhcp_lease(self, context, tags, mac, ip_address, cid=None,
                          topic=None):
        """Synchronously, have a conductor handle a DHCP lease update.

        Handling depends on the interface:
        - management interface: creates an ihost
        - infrastructure interface: just updated the dnsmasq config

        :param context: request context.
        :param tags: specifies the interface type (mgmt or infra)
        :param mac: MAC for the lease
        :param ip_address: IP address for the lease
        :param cid: Client ID for the lease
        """
        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')
        return cctxt.call(context,
                          'handle_dhcp_lease',
                          tags=tags,
                          mac=mac,
                          ip_address=ip_address,
                          cid=cid)

    def create_host(self, context, values, topic=None):
        """Synchronously, have a conductor create an ihost.

        Create an ihost in the database and return an object.

        :param context: request context.
        :param values: dictionary with initial values for new ihost object
        :returns: created ihost object, including all fields.
        """
        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')
        return cctxt.call(context,
                          'create_host',
                          values=values)

    def update_host(self, context, ihost_obj, topic=None):
        """Synchronously, have a conductor update the hosts's information.

        Update the ihost's information in the database and return an object.

        :param context: request context.
        :param ihost_obj: a changed (but not saved) ihost object.
        :returns: updated ihost object, including all fields.
        """
        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')
        return cctxt.call(context,
                          'update_host',
                          ihost_obj=ihost_obj)

    def configure_host(self, context, host_obj,
                       do_compute_apply=False,
                       topic=None):
        """Synchronously, have a conductor configure an ihost.

        Does the following tasks:
        - invoke systemconfig to perform host configuration
            - Update puppet hiera configuration files for the ihost.
            - Add (or update) a host entry in the dnsmasq.conf file.
            - Set up PXE configuration to run installer

        :param context: request context.
        :param host_obj: an ihost object.
        :param do_compute_apply: apply the newly created compute manifests.
        """
        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')
        return cctxt.call(context,
                          'configure_host',
                          host_obj=host_obj,
                          do_compute_apply=do_compute_apply)

    def unconfigure_host(self, context, host_obj, topic=None):
        """Synchronously, have a conductor unconfigure a host.

        Does the following tasks:
        - Remove hiera config files for the host.
        - Remove the host entry from the dnsmasq.conf file.
        - Remove the PXE configuration

        :param context: request context.
        :param host_obj: a host object.
        """
        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')
        return cctxt.call(context,
                          'unconfigure_host',
                          host_obj=host_obj)

    def get_host_by_macs(self, context, host_macs, topic=None):
        """Finds hosts db entry based upon the mac list

        This method returns a host if it matches a mac

        :param context: an admin context
        :param host_macs: list of mac addresses
        :returns: host object
        """
        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')

        return cctxt.call(context,
                          'get_host_by_macs',
                          host_macs=host_macs)

    def get_host_by_hostname(self, context, hostname, topic=None):
        """Finds host db entry based upon the ihost hostname

        This method returns an ihost if it matches the
        hostname.

        :param context: an admin context
        :param hostname: host hostname
        :returns: host object, including all fields.
        """

        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')
        return cctxt.call(context,
                          'get_host_by_hostname',
                          hostname=hostname)

    def platform_update_by_host(self, context,
                                host_uuid, imsg_dict, topic=None):
        """Create or update memory for an ihost with the supplied data.

        This method allows records for memory for ihost to be created,
        or updated.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param imsg_dict: inventory message dict
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic,
                                    version='1.0')
        return cctxt.call(context,
                          'platform_update_by_host',
                          host_uuid=host_uuid,
                          imsg_dict=imsg_dict)

    def subfunctions_update_by_host(self, context,
                                    host_uuid, subfunctions, topic=None):
        """Create or update local volume group for an ihost with the supplied
        data.

        This method allows records for a local volume group for ihost to be
        created, or updated.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param subfunctions: subfunctions of the host
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'subfunctions_update_by_host',
                          host_uuid=host_uuid,
                          subfunctions=subfunctions)

    def mgmt_ip_set_by_host(self,
                            context,
                            host_uuid,
                            mgmt_ip,
                            topic=None):
        """Call inventory to update host mgmt_ip (removes previous entry if
           necessary)

        :param context: an admin context
        :param host_uuid: ihost uuid
        :param mgmt_ip: mgmt_ip
        :returns: Address
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'mgmt_ip_set_by_host',
                          host_uuid=host_uuid,
                          mgmt_ip=mgmt_ip)

    def infra_ip_set_by_host(self,
                             context,
                             host_uuid,
                             infra_ip, topic=None):
        """Call inventory to update host infra_ip (removes previous entry if
           necessary)

        :param context: an admin context
        :param host_uuid: ihost uuid
        :param infra_ip: infra_ip
        :returns: Address
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'infra_ip_set_by_host',
                          host_uuid=host_uuid,
                          infra_ip=infra_ip)

    def vim_host_add(self, context, api_token, host_uuid,
                     hostname, subfunctions, administrative,
                     operational, availability,
                     subfunction_oper, subfunction_avail, timeout, topic=None):
        """
        Asynchronously, notify VIM of host add
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.cast(context,
                          'vim_host_add',
                          api_token=api_token,
                          host_uuid=host_uuid,
                          hostname=hostname,
                          personality=subfunctions,
                          administrative=administrative,
                          operational=operational,
                          availability=availability,
                          subfunction_oper=subfunction_oper,
                          subfunction_avail=subfunction_avail,
                          timeout=timeout)

    def notify_subfunctions_config(self, context,
                                   host_uuid, ihost_notify_dict, topic=None):
        """
        Synchronously, notify inventory of host subfunctions config status
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'notify_subfunctions_config',
                          host_uuid=host_uuid,
                          ihost_notify_dict=ihost_notify_dict)

    def bm_deprovision_by_host(self, context,
                               host_uuid, ibm_msg_dict, topic=None):
        """Update ihost upon notification of board management controller
           deprovisioning.

        This method also allows a dictionary of values to be passed in to
        affort additional controls, if and as needed.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param ibm_msg_dict: values for additional controls or changes
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'bm_deprovision_by_host',
                          host_uuid=host_uuid,
                          ibm_msg_dict=ibm_msg_dict)

    def configure_ttys_dcd(self, context, uuid, ttys_dcd, topic=None):
        """Synchronously, have a conductor configure the dcd.

        Does the following tasks:
        - sends a message to conductor
        - who sends a message to all inventory agents
        - who has the uuid updates dcd

        :param context: request context.
        :param uuid: the host uuid
        :param ttys_dcd: the flag to enable/disable dcd
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        LOG.debug("ConductorApi.configure_ttys_dcd: sending (%s %s) to "
                  "conductor" % (uuid, ttys_dcd))
        return cctxt.call(context,
                          'configure_ttys_dcd',
                          uuid=uuid,
                          ttys_dcd=ttys_dcd)

    def get_host_ttys_dcd(self, context, ihost_id, topic=None):
        """Synchronously, have a agent collect carrier detect state for this
           ihost.

        :param context: request context.
        :param ihost_id: id of this host
        :returns: ttys_dcd.
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'get_host_ttys_dcd',
                          ihost_id=ihost_id)

    def port_update_by_host(self, context,
                            host_uuid, inic_dict_array, topic=None):
        """Create iports for an ihost with the supplied data.

        This method allows records for iports for ihost to be created.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param inic_dict_array: initial values for iport objects
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'port_update_by_host',
                          host_uuid=host_uuid,
                          inic_dict_array=inic_dict_array)

    def lldp_agent_update_by_host(self, context, host_uuid, agent_dict_array,
                                  topic=None):
        """Create lldp_agents for an ihost with the supplied data.

        This method allows records for lldp_agents for a host to be created.

        :param context: an admin context
        :param host_uuid: host uuid unique id
        :param agent_dict_array: initial values for lldp_agent objects
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'lldp_agent_update_by_host',
                          host_uuid=host_uuid,
                          agent_dict_array=agent_dict_array)

    def lldp_neighbour_update_by_host(self, context,
                                      host_uuid, neighbour_dict_array,
                                      topic=None):
        """Create lldp_neighbours for an ihost with the supplied data.

        This method allows records for lldp_neighbours for a host to be
        created.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param neighbour_dict_array: initial values for lldp_neighbour objects
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(
            context,
            'lldp_neighbour_update_by_host',
            host_uuid=host_uuid,
            neighbour_dict_array=neighbour_dict_array)

    def pci_device_update_by_host(self, context,
                                  host_uuid, pci_device_dict_array,
                                  topic=None):
        """Create pci_devices for an ihost with the supplied data.

        This method allows records for pci_devices for ihost to be created.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param pci_device_dict_array: initial values for device objects
        :returns: pass or fail
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'pci_device_update_by_host',
                          host_uuid=host_uuid,
                          pci_device_dict_array=pci_device_dict_array)

    def numas_update_by_host(self,
                             context,
                             host_uuid,
                             inuma_dict_array,
                             topic=None):
        """Create inumas for an ihost with the supplied data.

        This method allows records for inumas for ihost to be created.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param inuma_dict_array: initial values for inuma objects
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'numas_update_by_host',
                          host_uuid=host_uuid,
                          inuma_dict_array=inuma_dict_array)

    def cpus_update_by_host(self,
                            context,
                            host_uuid,
                            icpu_dict_array,
                            force_grub_update,
                            topic=None):
        """Create cpus for an ihost with the supplied data.

        This method allows records for cpus for ihost to be created.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param icpu_dict_array: initial values for cpu objects
        :param force_grub_update: bool value to force grub update
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'cpus_update_by_host',
                          host_uuid=host_uuid,
                          icpu_dict_array=icpu_dict_array,
                          force_grub_update=force_grub_update)

    def memory_update_by_host(self, context,
                              host_uuid, imemory_dict_array,
                              force_update=False,
                              topic=None):
        """Create or update memory for an ihost with the supplied data.

        This method allows records for memory for ihost to be created,
        or updated.

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :param imemory_dict_array: initial values for memory objects
        :param force_update: force a memory update
        :returns: pass or fail
        """

        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'memory_update_by_host',
                          host_uuid=host_uuid,
                          imemory_dict_array=imemory_dict_array,
                          force_update=force_update)

    def update_cpu_config(self, context, topic=None):
        """Synchronously, have the conductor update the cpu
        configuration.

        :param context: request context.
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context, 'update_cpu_config')

    def create_barbican_secret(self, context, name, payload, topic=None):
        """Calls Barbican API to create a secret

        :param context: request context.
        :param name: secret name
        :param payload: secret payload
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'create_barbican_secret',
                          name=name,
                          payload=payload)

    def delete_barbican_secret(self, context, name, topic=None):
        """Calls Barbican API to delete a secret

        :param context: request context.
        :param name: secret name
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'delete_barbican_secret',
                          name=name)

    def reload_snmp_config(self, context, topic=None):
        """Synchronously, have a conductor reload the SNMP configuration.

        Does the following tasks:
        - sighup snmpd to reload the snmpd configuration.

        :param context: request context.
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context,
                          'reload_snmp_config')

    def region_has_ceph_backend(self, context, topic=None):
        """
        Send a request to primary region to see if ceph backend is configured
        """
        cctxt = self.client.prepare(topic=topic or self.topic, version='1.0')
        return cctxt.call(context, 'region_has_ceph_backend')
