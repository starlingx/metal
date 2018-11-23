# vim: tabstop=4 shiftwidth=4 softtabstop=4
# -*- encoding: utf-8 -*-
#
# Copyright 2013 Hewlett-Packard Development Company, L.P.
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


import json
import urlparse

from oslo_config import cfg
from oslo_db.sqlalchemy import models
from sqlalchemy import Column, Enum, ForeignKey, Integer, Boolean
from sqlalchemy import UniqueConstraint, String, Text
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.ext.declarative import declared_attr
from sqlalchemy.types import TypeDecorator, VARCHAR
from sqlalchemy.orm import relationship, backref


def table_args():
    engine_name = urlparse.urlparse(cfg.CONF.database_connection).scheme
    if engine_name == 'mysql':
        return {'mysql_engine': 'InnoDB',
                'mysql_charset': "utf8"}
    return None


class JSONEncodedDict(TypeDecorator):
    """Represents an immutable structure as a json-encoded string."""

    impl = VARCHAR

    def process_bind_param(self, value, dialect):
        if value is not None:
            value = json.dumps(value)
        return value

    def process_result_value(self, value, dialect):
        if value is not None:
            value = json.loads(value)
        return value


class InventoryBase(models.TimestampMixin, models.ModelBase):

    metadata = None

    def as_dict(self):
        d = {}
        for c in self.__table__.columns:
            d[c.name] = self[c.name]
        return d


Base = declarative_base(cls=InventoryBase)


class Systems(Base):
    __tablename__ = 'systems'

    # The reference for system is from systemconfig
    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36), unique=True)

    name = Column(String(255), unique=True)
    system_type = Column(String(255))
    system_mode = Column(String(255))
    description = Column(String(255))
    capabilities = Column(JSONEncodedDict)
    contact = Column(String(255))
    location = Column(String(255))
    services = Column(Integer, default=72)
    software_version = Column(String(255))
    timezone = Column(String(255))
    security_profile = Column(String(255))
    region_name = Column(Text)
    service_project_name = Column(Text)
    distributed_cloud_role = Column(String(255))
    security_feature = Column(String(255))


class Hosts(Base):
    recordTypeEnum = Enum('standard',
                          'profile',
                          'sprofile',
                          'reserve1',
                          'reserve2',
                          name='recordtypeEnum')

    adminEnum = Enum('locked',
                     'unlocked',
                     'reserve1',
                     'reserve2',
                     name='administrativeEnum')

    operEnum = Enum('disabled',
                    'enabled',
                    'reserve1',
                    'reserve2',
                    name='operationalEnum')

    availEnum = Enum('available',
                     'intest',
                     'degraded',
                     'failed',
                     'power-off',
                     'offline',
                     'offduty',
                     'online',
                     'dependency',
                     'not-installed',
                     'reserv1',
                     'reserve2',
                     name='availabilityEnum')

    actionEnum = Enum('none',
                      'lock',
                      'force-lock',
                      'unlock',
                      'reset',
                      'swact',
                      'force-swact',
                      'reboot',
                      'power-on',
                      'power-off',
                      'reinstall',
                      'reserve1',
                      'reserve2',
                      name='actionEnum')

    __tablename__ = 'hosts'
    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36), unique=True)

    hostname = Column(String(255), unique=True, index=True)
    recordtype = Column(recordTypeEnum, default="standard")
    reserved = Column(Boolean, default=False)

    invprovision = Column(String(64), default="unprovisioned")

    mgmt_mac = Column(String(255), unique=True)
    mgmt_ip = Column(String(255))

    # board management IP address, MAC, type and username
    bm_ip = Column(String(255))
    bm_mac = Column(String(255))
    bm_type = Column(String(255))
    bm_username = Column(String(255))

    personality = Column(String(255))
    subfunctions = Column(String(255))
    subfunction_oper = Column(operEnum, default="disabled")
    subfunction_avail = Column(availEnum, default="not-installed")
    serialid = Column(String(255))
    location = Column(JSONEncodedDict)
    administrative = Column(adminEnum, default="locked")
    operational = Column(operEnum, default="disabled")
    availability = Column(availEnum, default="offline")
    action = Column(actionEnum, default="none")
    host_action = Column(String(255))
    action_state = Column(String(255))
    mtce_info = Column(String(255))
    install_state = Column(String(255))
    install_state_info = Column(String(255))
    vim_progress_status = Column(String(255))
    task = Column(String(64))
    uptime = Column(Integer, default=0)
    capabilities = Column(JSONEncodedDict)

    boot_device = Column(String(255), default="sda")
    rootfs_device = Column(String(255), default="sda")
    install_output = Column(String(255), default="text")
    console = Column(String(255), default="ttyS0,115200")
    tboot = Column(String(64), default="")
    ttys_dcd = Column(Boolean)
    iscsi_initiator_name = Column(String(64))


class Nodes(Base):
    __tablename__ = 'nodes'

    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36), unique=True)

    numa_node = Column(Integer)
    capabilities = Column(JSONEncodedDict)

    host_id = Column(Integer, ForeignKey('hosts.id', ondelete='CASCADE'))
    host = relationship("Hosts",
                        backref="nodes", lazy="joined", join_depth=1)

    UniqueConstraint('numa_node', 'host_id', name='u_hostnuma')


class Cpus(Base):
    __tablename__ = 'cpus'

    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36), unique=True)

    cpu = Column(Integer)
    core = Column(Integer)
    thread = Column(Integer)
    cpu_family = Column(String(255))
    cpu_model = Column(String(255))
    # allocated_function = Column(String(255))  # systemconfig allocates
    capabilities = Column(JSONEncodedDict)
    host_id = Column(Integer, ForeignKey('hosts.id', ondelete='CASCADE'))
    node_id = Column(Integer, ForeignKey('nodes.id', ondelete='CASCADE'))

    host = relationship("Hosts",
                        backref="cpus", lazy="joined", join_depth=1)
    node = relationship("Nodes",
                        backref="cpus", lazy="joined", join_depth=1)

    UniqueConstraint('cpu', 'host_id', name='u_hostcpu')


class Memorys(Base):
    __tablename__ = 'memorys'

    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36), unique=True)

    memtotal_mib = Column(Integer)
    memavail_mib = Column(Integer)
    platform_reserved_mib = Column(Integer)
    node_memtotal_mib = Column(Integer)

    hugepages_configured = Column(Boolean, default=False)

    vswitch_hugepages_size_mib = Column(Integer)
    vswitch_hugepages_reqd = Column(Integer)
    vswitch_hugepages_nr = Column(Integer)
    vswitch_hugepages_avail = Column(Integer)

    vm_hugepages_nr_2M_pending = Column(Integer)
    vm_hugepages_nr_1G_pending = Column(Integer)
    vm_hugepages_nr_2M = Column(Integer)
    vm_hugepages_nr_1G = Column(Integer)
    vm_hugepages_nr_4K = Column(Integer)
    vm_hugepages_avail_2M = Column(Integer)
    vm_hugepages_avail_1G = Column(Integer)

    vm_hugepages_use_1G = Column(Boolean, default=False)
    vm_hugepages_possible_2M = Column(Integer)
    vm_hugepages_possible_1G = Column(Integer)
    capabilities = Column(JSONEncodedDict)

    host_id = Column(Integer, ForeignKey('hosts.id', ondelete='CASCADE'))
    node_id = Column(Integer, ForeignKey('nodes.id'))

    host = relationship("Hosts", backref="memory", lazy="joined", join_depth=1)
    node = relationship("Nodes", backref="memory", lazy="joined", join_depth=1)

    UniqueConstraint('host_id', 'node_id', name='u_hostnode')


class Ports(Base):
    __tablename__ = 'ports'

    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36))
    host_id = Column(Integer, ForeignKey('hosts.id', ondelete='CASCADE'))
    node_id = Column(Integer, ForeignKey('nodes.id'))

    type = Column(String(255))

    name = Column(String(255))
    namedisplay = Column(String(255))
    pciaddr = Column(String(255))
    pclass = Column(String(255))
    pvendor = Column(String(255))
    pdevice = Column(String(255))
    psvendor = Column(String(255))
    psdevice = Column(String(255))
    dpdksupport = Column(Boolean, default=False)
    numa_node = Column(Integer)
    dev_id = Column(Integer)
    sriov_totalvfs = Column(Integer)
    sriov_numvfs = Column(Integer)
    # Each PCI Address is 12 char, 1020 char is enough for 64 devices
    sriov_vfs_pci_address = Column(String(1020))
    driver = Column(String(255))
    capabilities = Column(JSONEncodedDict)

    node = relationship("Nodes", backref="ports", lazy="joined", join_depth=1)
    host = relationship("Hosts", backref="ports", lazy="joined", join_depth=1)

    UniqueConstraint('pciaddr', 'dev_id', 'host_id', name='u_pciaddrdevhost')

    __mapper_args__ = {
        'polymorphic_identity': 'port',
        'polymorphic_on': type
    }


class EthernetPorts(Ports):
    __tablename__ = 'ethernet_ports'

    id = Column(Integer,
                ForeignKey('ports.id'), primary_key=True, nullable=False)

    mac = Column(String(255), unique=True)
    mtu = Column(Integer)
    speed = Column(Integer)
    link_mode = Column(String(255))
    duplex = Column(String(255))
    autoneg = Column(String(255))
    bootp = Column(String(255))

    UniqueConstraint('mac', name='u_mac')

    __mapper_args__ = {
        'polymorphic_identity': 'ethernet'
    }


class LldpAgents(Base):
    __tablename__ = 'lldp_agents'

    id = Column('id', Integer, primary_key=True, nullable=False)
    uuid = Column('uuid', String(36))
    host_id = Column('host_id', Integer, ForeignKey('hosts.id',
                                                    ondelete='CASCADE'))
    port_id = Column('port_id', Integer, ForeignKey('ports.id',
                                                    ondelete='CASCADE'))
    status = Column('status', String(255))

    lldp_tlvs = relationship("LldpTlvs",
                             backref=backref("lldpagents", lazy="subquery"),
                             cascade="all")

    host = relationship("Hosts", lazy="joined", join_depth=1)
    port = relationship("Ports", lazy="joined", join_depth=1)


class PciDevices(Base):
    __tablename__ = 'pci_devices'

    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36))
    host_id = Column(Integer, ForeignKey('hosts.id', ondelete='CASCADE'))
    name = Column(String(255))
    pciaddr = Column(String(255))
    pclass_id = Column(String(6))
    pvendor_id = Column(String(4))
    pdevice_id = Column(String(4))
    pclass = Column(String(255))
    pvendor = Column(String(255))
    pdevice = Column(String(255))
    psvendor = Column(String(255))
    psdevice = Column(String(255))
    numa_node = Column(Integer)
    sriov_totalvfs = Column(Integer)
    sriov_numvfs = Column(Integer)
    sriov_vfs_pci_address = Column(String(1020))
    driver = Column(String(255))
    enabled = Column(Boolean)
    extra_info = Column(Text)

    host = relationship("Hosts", lazy="joined", join_depth=1)

    UniqueConstraint('pciaddr', 'host_id', name='u_pciaddrhost')


class LldpNeighbours(Base):
    __tablename__ = 'lldp_neighbours'

    id = Column('id', Integer, primary_key=True, nullable=False)
    uuid = Column('uuid', String(36))
    host_id = Column('host_id', Integer, ForeignKey('hosts.id',
                                                    ondelete='CASCADE'))
    port_id = Column('port_id', Integer, ForeignKey('ports.id',
                                                    ondelete='CASCADE'))
    msap = Column('msap', String(511))

    lldp_tlvs = relationship(
        "LldpTlvs",
        backref=backref("lldpneighbours", lazy="subquery"),
        cascade="all")

    host = relationship("Hosts", lazy="joined", join_depth=1)
    port = relationship("Ports", lazy="joined", join_depth=1)

    UniqueConstraint('msap', 'port_id', name='u_msap_port_id')


class LldpTlvs(Base):
    __tablename__ = 'lldp_tlvs'

    id = Column('id', Integer, primary_key=True, nullable=False)
    agent_id = Column('agent_id', Integer, ForeignKey('lldp_agents.id',
                      ondelete='CASCADE'), nullable=True)
    neighbour_id = Column('neighbour_id', Integer,
                          ForeignKey('lldp_neighbours.id', ondelete='CASCADE'),
                          nullable=True)
    type = Column('type', String(255))
    value = Column('value', String(255))

    lldp_agent = relationship("LldpAgents",
                              backref=backref("lldptlvs", lazy="subquery"),
                              cascade="all",
                              lazy="joined")

    lldp_neighbour = relationship(
        "LldpNeighbours",
        backref=backref("lldptlvs", lazy="subquery"),
        cascade="all",
        lazy="joined")

    UniqueConstraint('type', 'agent_id',
                     name='u_type@agent')

    UniqueConstraint('type', 'neighbour_id',
                     name='u_type@neighbour')


class SensorGroups(Base):
    __tablename__ = 'sensorgroups'

    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36))
    host_id = Column(Integer, ForeignKey('hosts.id', ondelete='CASCADE'))

    sensortype = Column(String(255))
    datatype = Column(String(255))  # polymorphic
    sensorgroupname = Column(String(255))
    path = Column(String(255))
    description = Column(String(255))

    state = Column(String(255))
    possible_states = Column(String(255))
    algorithm = Column(String(255))
    audit_interval_group = Column(Integer)
    record_ttl = Column(Integer)

    actions_minor_group = Column(String(255))
    actions_major_group = Column(String(255))
    actions_critical_group = Column(String(255))

    suppress = Column(Boolean, default=False)

    capabilities = Column(JSONEncodedDict)

    actions_critical_choices = Column(String(255))
    actions_major_choices = Column(String(255))
    actions_minor_choices = Column(String(255))

    host = relationship("Hosts", lazy="joined", join_depth=1)

    UniqueConstraint('sensorgroupname', 'path', 'host_id',
                     name='u_sensorgroupname_path_host_id')

    __mapper_args__ = {
        'polymorphic_identity': 'sensorgroup',
        'polymorphic_on': datatype
    }


class SensorGroupsCommon(object):
    @declared_attr
    def id(cls):
        return Column(Integer,
                      ForeignKey('sensorgroups.id', ondelete="CASCADE"),
                      primary_key=True, nullable=False)


class SensorGroupsDiscrete(SensorGroupsCommon, SensorGroups):
    __tablename__ = 'sensorgroups_discrete'

    __mapper_args__ = {
        'polymorphic_identity': 'discrete',
    }


class SensorGroupsAnalog(SensorGroupsCommon, SensorGroups):
    __tablename__ = 'sensorgroups_analog'

    unit_base_group = Column(String(255))
    unit_modifier_group = Column(String(255))
    unit_rate_group = Column(String(255))

    t_minor_lower_group = Column(String(255))
    t_minor_upper_group = Column(String(255))
    t_major_lower_group = Column(String(255))
    t_major_upper_group = Column(String(255))
    t_critical_lower_group = Column(String(255))
    t_critical_upper_group = Column(String(255))

    __mapper_args__ = {
        'polymorphic_identity': 'analog',
    }


class Sensors(Base):
    __tablename__ = 'sensors'

    id = Column(Integer, primary_key=True, nullable=False)
    uuid = Column(String(36))
    host_id = Column(Integer, ForeignKey('hosts.id', ondelete='CASCADE'))

    sensorgroup_id = Column(Integer,
                            ForeignKey('sensorgroups.id',
                                       ondelete='SET NULL'))
    sensortype = Column(String(255))  # "watchdog", "temperature".
    datatype = Column(String(255))  # "discrete" or "analog"

    sensorname = Column(String(255))
    path = Column(String(255))

    status = Column(String(255))
    state = Column(String(255))
    state_requested = Column(String(255))

    sensor_action_requested = Column(String(255))

    audit_interval = Column(Integer)
    algorithm = Column(String(255))
    actions_minor = Column(String(255))
    actions_major = Column(String(255))
    actions_critical = Column(String(255))

    suppress = Column(Boolean, default=False)

    capabilities = Column(JSONEncodedDict)

    host = relationship("Hosts", lazy="joined", join_depth=1)
    sensorgroup = relationship("SensorGroups", lazy="joined", join_depth=1)

    UniqueConstraint('sensorname', 'path', 'host_id',
                     name='u_sensorname_path_host_id')

    __mapper_args__ = {
        'polymorphic_identity': 'sensor',
        'polymorphic_on': datatype
        # with_polymorphic is only supported in sqlalchemy.orm >= 0.8
        # 'with_polymorphic': '*'
    }


class SensorsDiscrete(Sensors):
    __tablename__ = 'sensors_discrete'

    id = Column(Integer, ForeignKey('sensors.id'),
                primary_key=True, nullable=False)

    __mapper_args__ = {
        'polymorphic_identity': 'discrete'
    }


class SensorsAnalog(Sensors):
    __tablename__ = 'sensors_analog'

    id = Column(Integer, ForeignKey('sensors.id'),
                primary_key=True, nullable=False)

    unit_base = Column(String(255))
    unit_modifier = Column(String(255))
    unit_rate = Column(String(255))

    t_minor_lower = Column(String(255))
    t_minor_upper = Column(String(255))
    t_major_lower = Column(String(255))
    t_major_upper = Column(String(255))
    t_critical_lower = Column(String(255))
    t_critical_upper = Column(String(255))

    __mapper_args__ = {
        'polymorphic_identity': 'analog'
    }
