#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


from inventoryclient.common import exceptions as exc
from inventoryclient.common import http
from inventoryclient.common.http import DEFAULT_VERSION
from inventoryclient.common.i18n import _
from inventoryclient.v1 import cpu
from inventoryclient.v1 import ethernetport
from inventoryclient.v1 import host
from inventoryclient.v1 import lldp_agent
from inventoryclient.v1 import lldp_neighbour
from inventoryclient.v1 import memory
from inventoryclient.v1 import node
from inventoryclient.v1 import pci_device
from inventoryclient.v1 import port


class Client(object):
    """Client for the INVENTORY v1 API.

    :param string endpoint: A user-supplied endpoint URL for the inventory
                            service.
    :param function token: Provides token for authentication.
    :param integer timeout: Allows customization of the timeout for client
                            http requests. (optional)
    """

    def __init__(self, endpoint=None, session=None, **kwargs):
        """Initialize a new client for the INVENTORY v1 API."""
        if not session:
            if kwargs.get('os_inventory_api_version'):
                kwargs['api_version_select_state'] = "user"
            else:
                if not endpoint:
                    raise exc.EndpointException(
                        _("Must provide 'endpoint' "
                          "if os_inventory_api_version isn't specified"))

            # If the user didn't specify a version, use a default version
            kwargs['api_version_select_state'] = "default"
            kwargs['os_inventory_api_version'] = DEFAULT_VERSION

        self.http_client = http.get_http_client(endpoint, session, **kwargs)
        self.host = host.HostManager(self.http_client)
        self.cpu = cpu.CpuManager(self.http_client)
        self.ethernetport = ethernetport.EthernetPortManager(self.http_client)
        self.lldp_agent = lldp_agent.LldpAgentManager(self.http_client)
        self.lldp_neighbour = lldp_neighbour.LldpNeighbourManager(
            self.http_client)
        self.memory = memory.MemoryManager(self.http_client)
        self.node = node.NodeManager(self.http_client)
        self.pci_device = pci_device.PciDeviceManager(self.http_client)
        self.port = port.PortManager(self.http_client)
