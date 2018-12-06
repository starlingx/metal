#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


import pecan
from pecan import rest

from inventory.api.controllers.v1 import base
from inventory.api.controllers.v1 import cpu
from inventory.api.controllers.v1 import ethernet_port
from inventory.api.controllers.v1 import host
from inventory.api.controllers.v1 import link
from inventory.api.controllers.v1 import lldp_agent
from inventory.api.controllers.v1 import lldp_neighbour
from inventory.api.controllers.v1 import memory
from inventory.api.controllers.v1 import node
from inventory.api.controllers.v1 import pci_device
from inventory.api.controllers.v1 import port
from inventory.api.controllers.v1 import sensor
from inventory.api.controllers.v1 import sensorgroup

from inventory.api.controllers.v1 import system
from wsme import types as wtypes
import wsmeext.pecan as wsme_pecan


class MediaType(base.APIBase):
    """A media type representation."""

    base = wtypes.text
    type = wtypes.text

    def __init__(self, base, type):
        self.base = base
        self.type = type


class V1(base.APIBase):
    """The representation of the version 1 of the API."""

    id = wtypes.text
    "The ID of the version, also acts as the release number"

    media_types = [MediaType]
    "An array of supported media types for this version"

    links = [link.Link]
    "Links that point to a specific URL for this version and documentation"

    systems = [link.Link]
    "Links to the system resource"

    hosts = [link.Link]
    "Links to the host resource"

    lldp_agents = [link.Link]
    "Links to the lldp agents resource"

    lldp_neighbours = [link.Link]
    "Links to the lldp neighbours resource"

    @classmethod
    def convert(self):
        v1 = V1()
        v1.id = "v1"
        v1.links = [link.Link.make_link('self', pecan.request.host_url,
                                        'v1', '', bookmark=True),
                    link.Link.make_link('describedby',
                                        'http://www.starlingx.io/',
                                        'developer/inventory/dev',
                                        'api-spec-v1.html',
                                        bookmark=True, type='text/html')
                    ]
        v1.media_types = [MediaType('application/json',
                          'application/vnd.openstack.inventory.v1+json')]

        v1.systems = [link.Link.make_link('self', pecan.request.host_url,
                                          'systems', ''),
                      link.Link.make_link('bookmark',
                                          pecan.request.host_url,
                                          'systems', '',
                                          bookmark=True)
                      ]

        v1.hosts = [link.Link.make_link('self', pecan.request.host_url,
                                        'hosts', ''),
                    link.Link.make_link('bookmark',
                                        pecan.request.host_url,
                                        'hosts', '',
                                        bookmark=True)
                    ]

        v1.nodes = [link.Link.make_link('self', pecan.request.host_url,
                                        'nodes', ''),
                    link.Link.make_link('bookmark',
                                        pecan.request.host_url,
                                        'nodes', '',
                                        bookmark=True)
                    ]

        v1.cpus = [link.Link.make_link('self', pecan.request.host_url,
                                       'cpus', ''),
                   link.Link.make_link('bookmark',
                                       pecan.request.host_url,
                                       'cpus', '',
                                       bookmark=True)
                   ]

        v1.memory = [link.Link.make_link('self', pecan.request.host_url,
                                         'memory', ''),
                     link.Link.make_link('bookmark',
                                         pecan.request.host_url,
                                         'memory', '',
                                         bookmark=True)
                     ]

        v1.ports = [link.Link.make_link('self',
                                        pecan.request.host_url,
                                        'ports', ''),
                    link.Link.make_link('bookmark',
                                        pecan.request.host_url,
                                        'ports', '',
                                        bookmark=True)
                    ]

        v1.ethernet_ports = [link.Link.make_link('self',
                                                 pecan.request.host_url,
                                                 'ethernet_ports', ''),
                             link.Link.make_link('bookmark',
                                                 pecan.request.host_url,
                                                 'ethernet_ports', '',
                                                 bookmark=True)
                             ]

        v1.lldp_agents = [link.Link.make_link('self',
                                              pecan.request.host_url,
                                              'lldp_agents', ''),
                          link.Link.make_link('bookmark',
                                              pecan.request.host_url,
                                              'lldp_agents', '',
                                              bookmark=True)
                          ]

        v1.lldp_neighbours = [link.Link.make_link('self',
                                                  pecan.request.host_url,
                                                  'lldp_neighbours', ''),
                              link.Link.make_link('bookmark',
                                                  pecan.request.host_url,
                                                  'lldp_neighbours', '',
                                                  bookmark=True)
                              ]

        v1.sensors = [link.Link.make_link('self',
                                          pecan.request.host_url,
                                          'sensors', ''),
                      link.Link.make_link('bookmark',
                                          pecan.request.host_url,
                                          'sensors', '',
                                          bookmark=True)
                      ]

        v1.sensorgroups = [link.Link.make_link('self',
                                               pecan.request.host_url,
                                               'sensorgroups', ''),
                           link.Link.make_link('bookmark',
                                               pecan.request.host_url,
                                               'sensorgroups', '',
                                               bookmark=True)
                           ]

        return v1


class Controller(rest.RestController):
    """Version 1 API controller root."""

    systems = system.SystemController()
    hosts = host.HostController()
    nodes = node.NodeController()
    cpus = cpu.CPUController()
    memorys = memory.MemoryController()
    ports = port.PortController()
    ethernet_ports = ethernet_port.EthernetPortController()
    lldp_agents = lldp_agent.LLDPAgentController()
    lldp_neighbours = lldp_neighbour.LLDPNeighbourController()
    pci_devices = pci_device.PCIDeviceController()
    sensors = sensor.SensorController()
    sensorgroups = sensorgroup.SensorGroupController()

    @wsme_pecan.wsexpose(V1)
    def get(self):
        return V1.convert()


__all__ = ('Controller',)
