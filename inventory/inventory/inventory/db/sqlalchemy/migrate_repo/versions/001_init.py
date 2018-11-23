#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from sqlalchemy import Column, MetaData, String, Table, UniqueConstraint
from sqlalchemy import Boolean, Integer, Enum, Text, ForeignKey, DateTime

ENGINE = 'InnoDB'
CHARSET = 'utf8'


def upgrade(migrate_engine):
    meta = MetaData()
    meta.bind = migrate_engine

    systems = Table(
        'systems',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),

        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True, index=True),

        Column('system_type', String(255)),
        Column('system_mode', String(255)),

        Column('name', String(255), unique=True),
        Column('contact', String(255)),
        Column('location', String(255)),

        Column('description', String(255), unique=True),
        Column('timezone', String(255)),
        Column('region_name', Text),
        Column('services', Integer, default=72),
        Column('service_project_name', Text),
        Column('distributed_cloud_role', String(255)),
        Column('security_profile', String(255)),
        Column('security_feature', String(255)),
        Column('software_version', String(255)),
        Column('capabilities', Text),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    # Hosts Enum definitions
    recordtype_enum = Enum('standard',
                           'reserve1',
                           'reserve2',
                           name='recordtype_enum')

    personality_enum = Enum('controller',
                            'compute',
                            'storage',
                            'reserve1',
                            'reserve2',
                            name='personality_enum')

    admin_enum = Enum('locked',
                      'unlocked',
                      'reserve1',
                      'reserve2',
                      name='admin_enum')

    operational_enum = Enum('disabled',
                            'enabled',
                            'reserve1',
                            'reserve2',
                            name='operational_enum')

    availability_enum = Enum('available',
                             'intest',
                             'degraded',
                             'failed',
                             'power-off',
                             'offline',
                             'offduty',
                             'online',
                             'dependency',
                             'not-installed',
                             'reserve1',
                             'reserve2',
                             name='availability_enum')

    action_enum = Enum('none',
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
                       name='action_enum')

    hosts = Table(
        'hosts',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),

        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),

        Column('hostname', String(255), unique=True, index=True),
        Column('recordtype', recordtype_enum, default="standard"),
        Column('reserved', Boolean),

        Column('invprovision', String(64), default="unprovisioned"),

        Column('mgmt_mac', String(255), unique=True),
        Column('mgmt_ip', String(255), unique=True),

        # Board Management database members
        Column('bm_ip', String(255)),
        Column('bm_mac', String(255)),
        Column('bm_type', String(255)),
        Column('bm_username', String(255)),

        Column('personality', personality_enum),
        Column('subfunctions', String(255)),
        Column('subfunction_oper', String(255)),
        Column('subfunction_avail', String(255)),

        Column('serialid', String(255)),
        Column('location', Text),
        Column('administrative', admin_enum, default="locked"),
        Column('operational', operational_enum, default="disabled"),
        Column('availability', availability_enum, default="offline"),
        Column('action', action_enum, default="none"),
        Column('host_action', String(255)),
        Column('action_state', String(255)),
        Column('mtce_info', String(255)),
        Column('install_state', String(255)),
        Column('install_state_info', String(255)),
        Column('vim_progress_status', String(255)),
        Column('task', String(64)),
        Column('uptime', Integer),
        Column('capabilities', Text),

        Column('boot_device', String(255)),
        Column('rootfs_device', String(255)),
        Column('install_output', String(255)),
        Column('console', String(255)),
        Column('tboot', String(64)),
        Column('ttys_dcd', Boolean),
        Column('iscsi_initiator_name', String(64)),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    nodes = Table(
        'nodes',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),

        # numaNode from /sys/devices/system/node/nodeX/cpulist or cpumap
        Column('numa_node', Integer),
        Column('capabilities', Text),

        Column('host_id', Integer,
               ForeignKey('hosts.id', ondelete='CASCADE')),
        UniqueConstraint('numa_node', 'host_id', name='u_hostnuma'),
        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    cpus = Table(
        'cpus',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),
        Column('cpu', Integer),
        Column('core', Integer),
        Column('thread', Integer),
        Column('cpu_family', String(255)),
        Column('cpu_model', String(255)),
        Column('capabilities', Text),

        Column('host_id', Integer,
               ForeignKey('hosts.id', ondelete='CASCADE')),
        Column('node_id', Integer,
               ForeignKey('nodes.id', ondelete='CASCADE')),
        UniqueConstraint('cpu', 'host_id', name='u_hostcpu'),
        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    memorys = Table(
        'memorys',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer,
               primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),

        # per NUMA: /sys/devices/system/node/node<x>/meminfo
        Column('memtotal_mib', Integer),
        Column('memavail_mib', Integer),
        Column('platform_reserved_mib', Integer),

        Column('hugepages_configured', Boolean),  # if hugepages_configured

        Column('vswitch_hugepages_size_mib', Integer),
        Column('vswitch_hugepages_reqd', Integer),
        Column('vswitch_hugepages_nr', Integer),
        Column('vswitch_hugepages_avail', Integer),

        Column('vm_hugepages_nr_2M', Integer),
        Column('vm_hugepages_nr_1G', Integer),
        Column('vm_hugepages_use_1G', Boolean),
        Column('vm_hugepages_possible_2M', Integer),
        Column('vm_hugepages_possible_1G', Integer),

        Column('vm_hugepages_nr_2M_pending', Integer),  # To be removed
        Column('vm_hugepages_nr_1G_pending', Integer),  # To be removed
        Column('vm_hugepages_avail_2M', Integer),
        Column('vm_hugepages_avail_1G', Integer),

        Column('vm_hugepages_nr_4K', Integer),

        Column('node_memtotal_mib', Integer),

        Column('capabilities', Text),

        # psql requires unique FK
        Column('host_id', Integer,
               ForeignKey('hosts.id', ondelete='CASCADE')),
        Column('node_id', Integer, ForeignKey('nodes.id')),
        UniqueConstraint('host_id', 'node_id', name='u_hostnode'),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    ports = Table(
        'ports',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),

        Column('host_id', Integer, ForeignKey('hosts.id',
                                              ondelete='CASCADE')),
        Column('node_id', Integer, ForeignKey('nodes.id',
                                              ondelete='SET NULL')),

        Column('type', String(255)),
        Column('name', String(255)),
        Column('namedisplay', String(255)),
        Column('pciaddr', String(255)),
        Column('dev_id', Integer),
        Column('sriov_totalvfs', Integer),
        Column('sriov_numvfs', Integer),
        Column('sriov_vfs_pci_address', String(1020)),
        Column('driver', String(255)),

        Column('pclass', String(255)),
        Column('pvendor', String(255)),
        Column('pdevice', String(255)),
        Column('psvendor', String(255)),
        Column('psdevice', String(255)),
        Column('dpdksupport', Boolean, default=False),
        Column('numa_node', Integer),
        Column('capabilities', Text),

        UniqueConstraint('pciaddr', 'dev_id', 'host_id',
                         name='u_pciaddr_dev_host_id'),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    ethernet_ports = Table(
        'ethernet_ports',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, ForeignKey('ports.id', ondelete="CASCADE"),
               primary_key=True, nullable=False),

        Column('mac', String(255)),
        Column('mtu', Integer),
        Column('speed', Integer),
        Column('link_mode', String(255)),
        Column('duplex', String(255)),
        Column('autoneg', String(255)),
        Column('bootp', String(255)),
        Column('capabilities', Text),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    pci_devices = Table(
        'pci_devices',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),

        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(255), unique=True, index=True),
        Column('host_id', Integer, ForeignKey('hosts.id',
                                              ondelete='CASCADE')),
        Column('name', String(255)),
        Column('pciaddr', String(255)),
        Column('pclass_id', String(6)),
        Column('pvendor_id', String(4)),
        Column('pdevice_id', String(4)),
        Column('pclass', String(255)),
        Column('pvendor', String(255)),
        Column('pdevice', String(255)),
        Column('psvendor', String(255)),
        Column('psdevice', String(255)),
        Column('numa_node', Integer),
        Column('driver', String(255)),
        Column('sriov_totalvfs', Integer),
        Column('sriov_numvfs', Integer),
        Column('sriov_vfs_pci_address', String(1020)),
        Column('enabled', Boolean),
        Column('extra_info', Text),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    lldp_agents = Table(
        'lldp_agents',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),
        Column('host_id', Integer, ForeignKey('hosts.id',
                                              ondelete='CASCADE')),
        Column('port_id', Integer, ForeignKey('ports.id',
                                              ondelete='CASCADE')),
        Column('status', String(255)),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    lldp_neighbours = Table(
        'lldp_neighbours',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),
        Column('host_id', Integer, ForeignKey('hosts.id',
                                              ondelete='CASCADE')),
        Column('port_id', Integer, ForeignKey('ports.id',
                                              ondelete='CASCADE')),

        Column('msap', String(511), nullable=False),

        UniqueConstraint('msap', 'port_id',
                         name='u_msap_port_id'),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    lldp_tlvs = Table(
        'lldp_tlvs',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),
        Column('agent_id', Integer,
               ForeignKey('lldp_agents.id', ondelete="CASCADE"),
               nullable=True),
        Column('neighbour_id', Integer,
               ForeignKey('lldp_neighbours.id', ondelete="CASCADE"),
               nullable=True),
        Column('type', String(255)),
        Column('value', String(255)),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    sensorgroups = Table(
        'sensorgroups',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),

        Column('uuid', String(36), unique=True),
        Column('host_id', Integer,
               ForeignKey('hosts.id', ondelete='CASCADE')),

        Column('sensorgroupname', String(255)),
        Column('path', String(255)),
        Column('datatype', String(255)),  # polymorphic 'analog'/'discrete
        Column('sensortype', String(255)),
        Column('description', String(255)),
        Column('state', String(255)),  # enabled or disabled
        Column('possible_states', String(255)),
        Column('audit_interval_group', Integer),
        Column('record_ttl', Integer),

        Column('algorithm', String(255)),
        Column('actions_critical_choices', String(255)),
        Column('actions_major_choices', String(255)),
        Column('actions_minor_choices', String(255)),
        Column('actions_minor_group', String(255)),
        Column('actions_major_group', String(255)),
        Column('actions_critical_group', String(255)),

        Column('suppress', Boolean),  # True, disables the action

        Column('capabilities', Text),

        UniqueConstraint('sensorgroupname', 'path', 'host_id',
                         name='u_sensorgroupname_path_hostid'),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    # polymorphic on datatype 'discrete'
    sensorgroups_discrete = Table(
        'sensorgroups_discrete',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer,
               ForeignKey('sensorgroups.id', ondelete="CASCADE"),
               primary_key=True, nullable=False),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    # polymorphic on datatype 'analog'
    sensorgroups_analog = Table(
        'sensorgroups_analog',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer,
               ForeignKey('sensorgroups.id', ondelete="CASCADE"),
               primary_key=True, nullable=False),

        Column('unit_base_group', String(255)),  # revolutions
        Column('unit_modifier_group', String(255)),  # 100
        Column('unit_rate_group', String(255)),  # minute

        Column('t_minor_lower_group', String(255)),
        Column('t_minor_upper_group', String(255)),
        Column('t_major_lower_group', String(255)),
        Column('t_major_upper_group', String(255)),
        Column('t_critical_lower_group', String(255)),
        Column('t_critical_upper_group', String(255)),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    sensors = Table(
        'sensors',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer, primary_key=True, nullable=False),
        Column('uuid', String(36), unique=True),

        Column('host_id', Integer,
               ForeignKey('hosts.id', ondelete='CASCADE')),

        Column('sensorgroup_id', Integer,
               ForeignKey('sensorgroups.id', ondelete='SET NULL')),

        Column('sensorname', String(255)),
        Column('path', String(255)),

        Column('datatype', String(255)),  # polymorphic on datatype
        Column('sensortype', String(255)),

        Column('status', String(255)),  # ok, minor, major, critical, disabled
        Column('state', String(255)),  # enabled, disabled
        Column('state_requested', String(255)),

        Column('sensor_action_requested', String(255)),

        Column('audit_interval', Integer),
        Column('algorithm', String(255)),
        Column('actions_minor', String(255)),
        Column('actions_major', String(255)),
        Column('actions_critical', String(255)),

        Column('suppress', Boolean),  # True, disables the action

        Column('capabilities', Text),

        UniqueConstraint('sensorname', 'path', 'host_id',
                         name='u_sensorname_path_host_id'),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    # discrete sensor
    sensors_discrete = Table(
        'sensors_discrete',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer,
               ForeignKey('sensors.id', ondelete="CASCADE"),
               primary_key=True, nullable=False),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    # analog sensor
    sensors_analog = Table(
        'sensors_analog',
        meta,
        Column('created_at', DateTime),
        Column('updated_at', DateTime),
        Column('deleted_at', DateTime),
        Column('id', Integer,
               ForeignKey('sensors.id', ondelete="CASCADE"),
               primary_key=True, nullable=False),

        Column('unit_base', String(255)),  # revolutions
        Column('unit_modifier', String(255)),  # 10^2
        Column('unit_rate', String(255)),  # minute

        Column('t_minor_lower', String(255)),
        Column('t_minor_upper', String(255)),
        Column('t_major_lower', String(255)),
        Column('t_major_upper', String(255)),
        Column('t_critical_lower', String(255)),
        Column('t_critical_upper', String(255)),

        mysql_engine=ENGINE,
        mysql_charset=CHARSET,
    )

    # TODO(sc) disks
    tables = (
        systems,
        hosts,
        nodes,
        cpus,
        memorys,
        pci_devices,
        ports,
        ethernet_ports,
        lldp_agents,
        lldp_neighbours,
        lldp_tlvs,
        sensorgroups,
        sensorgroups_discrete,
        sensorgroups_analog,
        sensors,
        sensors_discrete,
        sensors_analog,
    )

    for index, table in enumerate(tables):
        try:
            table.create()
        except Exception:
            # If an error occurs, drop all tables created so far to return
            # to the previously existing state.
            meta.drop_all(tables=tables[:index])
            raise
