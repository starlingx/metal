#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

"""SQLAlchemy backend."""

import eventlet
import threading

from oslo_config import cfg
from oslo_db import exception as db_exc
from oslo_db.sqlalchemy import enginefacade
from oslo_db.sqlalchemy import session as db_session
from oslo_db.sqlalchemy import utils as db_utils

from oslo_log import log
from oslo_utils import uuidutils

from sqlalchemy import inspect
from sqlalchemy import or_
from sqlalchemy.orm.exc import MultipleResultsFound
from sqlalchemy.orm.exc import NoResultFound

from inventory.common import exception
from inventory.common.i18n import _
from inventory.common import utils
from inventory.db import api
from inventory.db.sqlalchemy import models

LOG = log.getLogger(__name__)
CONF = cfg.CONF


_LOCK = threading.Lock()
_FACADE = None

context_manager = enginefacade.transaction_context()
context_manager.configure()


def _create_facade_lazily():
    global _LOCK
    with _LOCK:
        global _FACADE
        if _FACADE is None:
            _FACADE = db_session.EngineFacade(
                CONF.database.connection,
                **dict(CONF.database)
            )
        return _FACADE


def get_engine():
    facade = _create_facade_lazily()
    return facade.get_engine()


def get_session(**kwargs):
    facade = _create_facade_lazily()
    return facade.get_session(**kwargs)


def get_backend():
    """The backend is this module itself."""
    return Connection()


def _session_for_read():
    # _context = threading.local()
    _context = eventlet.greenthread.getcurrent()

    return enginefacade.reader.using(_context)


def _session_for_write():
    _context = eventlet.greenthread.getcurrent()

    return enginefacade.writer.using(_context)


def _paginate_query(model, limit=None, marker=None, sort_key=None,
                    sort_dir=None, query=None):
    if not query:
        query = model_query(model)

    if not sort_key:
        sort_keys = []
    elif not isinstance(sort_key, list):
        sort_keys = [sort_key]
    else:
        sort_keys = sort_key

    if 'id' not in sort_keys:
        sort_keys.append('id')
    query = db_utils.paginate_query(query, model, limit, sort_keys,
                                    marker=marker, sort_dir=sort_dir)
    return query.all()


def model_query(model, *args, **kwargs):
    """Query helper for simpler session usage.

    :param model: database model
    :param session: if present, the session to use
    """

    session = kwargs.get('session')
    if session:
        query = session.query(model, *args)
    else:
        with _session_for_read() as session:
            query = session.query(model, *args)
    return query


def add_identity_filter(query, value,
                        use_ifname=False,
                        use_ipaddress=False,
                        use_community=False,
                        use_key=False,
                        use_name=False,
                        use_cname=False,
                        use_secname=False,
                        use_sensorgroupname=False,
                        use_sensorname=False,
                        use_pciaddr=False):
    """Adds an identity filter to a query.

    Filters results by ID, if supplied value is a valid integer.
    Otherwise attempts to filter results by UUID.

    :param query: Initial query to add filter to.
    :param value: Value for filtering results by.
    :return: Modified query.
    """
    if utils.is_int_like(value):
        return query.filter_by(id=value)
    elif uuidutils.is_uuid_like(value):
        return query.filter_by(uuid=value)
    else:
        if use_ifname:
            return query.filter_by(ifname=value)
        elif use_ipaddress:
            return query.filter_by(ip_address=value)
        elif use_community:
            return query.filter_by(community=value)
        elif use_name:
            return query.filter_by(name=value)
        elif use_cname:
            return query.filter_by(cname=value)
        elif use_secname:
            return query.filter_by(secname=value)
        elif use_key:
            return query.filter_by(key=value)
        elif use_pciaddr:
            return query.filter_by(pciaddr=value)
        elif use_sensorgroupname:
            return query.filter_by(sensorgroupname=value)
        elif use_sensorname:
            return query.filter_by(sensorname=value)
        else:
            return query.filter_by(hostname=value)


def add_filter_by_many_identities(query, model, values):
    """Adds an identity filter to a query for values list.

    Filters results by ID, if supplied values contain a valid integer.
    Otherwise attempts to filter results by UUID.

    :param query: Initial query to add filter to.
    :param model: Model for filter.
    :param values: Values for filtering results by.
    :return: tuple (Modified query, filter field name).
    """
    if not values:
        raise exception.InvalidIdentity(identity=values)
    value = values[0]
    if utils.is_int_like(value):
        return query.filter(getattr(model, 'id').in_(values)), 'id'
    elif uuidutils.is_uuid_like(value):
        return query.filter(getattr(model, 'uuid').in_(values)), 'uuid'
    else:
        raise exception.InvalidIdentity(identity=value)


def add_node_filter_by_host(query, value):
    if utils.is_int_like(value):
        return query.filter_by(host_id=value)
    else:
        query = query.join(models.Hosts,
                           models.Nodes.host_id == models.Hosts.id)
        return query.filter(models.Hosts.uuid == value)


def add_filter_by_host_node(query, ihostid, inodeid):
    if utils.is_int_like(ihostid) and utils.is_int_like(inodeid):
        return query.filter_by(host_id=ihostid, node_id=inodeid)

    if utils.is_uuid_like(ihostid) and utils.is_uuid_like(inodeid):
        ihostq = model_query(models.Hosts).filter_by(uuid=ihostid).first()
        inodeq = model_query(models.Nodes).filter_by(uuid=inodeid).first()

        query = query.filter_by(host_id=ihostq.id,
                                node_id=inodeq.id)

        return query


def add_cpu_filter_by_host(query, value):
    if utils.is_int_like(value):
        return query.filter_by(host_id=value)
    else:
        query = query.join(models.Hosts,
                           models.Cpus.host_id == models.Hosts.id)
        return query.filter(models.Hosts.uuid == value)


def add_cpu_filter_by_host_node(query, ihostid, inodeid):
    if utils.is_int_like(ihostid) and utils.is_int_like(inodeid):
        return query.filter_by(host_id=ihostid, node_id=inodeid)

    # gives access to joined tables... nice to have unique col name
    if utils.is_uuid_like(ihostid) and utils.is_uuid_like(inodeid):
        query = query.join(models.Hosts,
                           models.Cpus.host_id == models.Hosts.id,
                           models.Nodes.host_id == models.Hosts.id)

        return query.filter(models.Hosts.uuid == ihostid,
                            models.Nodes.uuid == inodeid)

    LOG.error("cpu_filter_by_host_inode: No match for id int or ids uuid")


def add_cpu_filter_by_node(query, inodeid):
    if utils.is_int_like(inodeid):
        return query.filter_by(node_id=inodeid)
    else:
        query = query.join(models.Nodes,
                           models.Cpus.node_id == models.Nodes.id)
        return query.filter(models.Nodes.uuid == inodeid)


def add_memory_filter_by_host(query, value):
    if utils.is_int_like(value):
        return query.filter_by(host_id=value)
    else:
        query = query.join(models.Hosts,
                           models.Memorys.host_id == models.Hosts.id)
        return query.filter(models.Hosts.uuid == value)


def add_memory_filter_by_host_node(query, ihostid, inodeid):
    if utils.is_int_like(ihostid) and utils.is_int_like(inodeid):
        return query.filter_by(host_id=ihostid, node_id=inodeid)

    if utils.is_uuid_like(ihostid) and utils.is_uuid_like(inodeid):
        ihostq = model_query(models.Hosts).filter_by(uuid=ihostid).first()
        inodeq = model_query(models.Nodes).filter_by(uuid=inodeid).first()

        query = query.filter_by(host_id=ihostq.id,
                                node_id=inodeq.id)

        return query


def add_memory_filter_by_node(query, inodeid):
    if utils.is_int_like(inodeid):
        return query.filter_by(node_id=inodeid)
    else:
        query = query.join(models.Nodes,
                           models.Memorys.node_id == models.Nodes.id)
        return query.filter(models.Nodes.uuid == inodeid)


def add_device_filter_by_host(query, hostid):
    """Adds a device-specific ihost filter to a query.

    Filters results by host id if supplied value is an integer,
    otherwise attempts to filter results by host uuid.

    :param query: Initial query to add filter to.
    :param hostid: host id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(hostid):
        return query.filter_by(host_id=hostid)

    elif utils.is_uuid_like(hostid):
        query = query.join(models.Hosts)
        return query.filter(models.Hosts.uuid == hostid)


def add_port_filter_by_numa_node(query, nodeid):
    """Adds a port-specific numa node filter to a query.

    Filters results by numa node id if supplied nodeid is an integer,
    otherwise attempts to filter results by numa node uuid.

    :param query: Initial query to add filter to.
    :param nodeid: numa node id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(nodeid):
        return query.filter_by(node_id=nodeid)

    elif utils.is_uuid_like(nodeid):
        query = query.join(models.Nodes)
        return query.filter(models.Nodes.uuid == nodeid)

    LOG.debug("port_filter_by_numa_node: "
              "No match for supplied filter id (%s)" % str(nodeid))


def add_port_filter_by_host(query, hostid):
    """Adds a port-specific host filter to a query.

    Filters results by host id if supplied value is an integer,
    otherwise attempts to filter results by host uuid.

    :param query: Initial query to add filter to.
    :param hostid: host id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(hostid):
        # Should not need join due to polymorphic ports table
        # query = query.join(models.ports,
        #                    models.EthernetPorts.id == models.ports.id)
        #
        # Query of ethernet_ports table should return data from
        # corresponding ports table entry so should be able to
        # use filter_by() rather than filter()
        #
        return query.filter_by(host_id=hostid)

    elif utils.is_uuid_like(hostid):
        query = query.join(models.Hosts)
        return query.filter(models.Hosts.uuid == hostid)

    LOG.debug("port_filter_by_host: "
              "No match for supplied filter id (%s)" % str(hostid))


def add_lldp_filter_by_host(query, hostid):
    """Adds a lldp-specific ihost filter to a query.

    Filters results by host id if supplied value is an integer,
    otherwise attempts to filter results by host uuid.

    :param query: Initial query to add filter to.
    :param hostid: host id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(hostid):
        return query.filter_by(host_id=hostid)
    elif utils.is_uuid_like(hostid):
        query = query.join(models.Hosts)
        return query.filter(models.Hosts.uuid == hostid)

    LOG.debug("lldp_filter_by_host: "
              "No match for supplied filter id (%s)" % str(hostid))


def add_lldp_filter_by_port(query, portid):
    """Adds a lldp-specific port filter to a query.

    Filters results by port id if supplied value is an integer,
    otherwise attempts to filter results by port uuid.

    :param query: Initial query to add filter to.
    :param portid: port id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(portid):
        return query.filter_by(port_id=portid)
    elif utils.is_uuid_like(portid):
        query = query.join(models.Ports)
        return query.filter(models.Ports.uuid == portid)


def add_lldp_filter_by_agent(query, value):
    """Adds an lldp-specific filter to a query.

    Filters results by agent id if supplied value is an integer.
    Filters results by agent UUID if supplied value is a UUID.

    :param query: Initial query to add filter to.
    :param value: Value for filtering results by.
    :return: Modified query.
    """
    if utils.is_int_like(value):
        return query.filter(models.LldpAgents.id == value)
    elif uuidutils.is_uuid_like(value):
        return query.filter(models.LldpAgents.uuid == value)


def add_lldp_filter_by_neighbour(query, value):
    """Adds an lldp-specific filter to a query.

    Filters results by neighbour id if supplied value is an integer.
    Filters results by neighbour UUID if supplied value is a UUID.

    :param query: Initial query to add filter to.
    :param value: Value for filtering results by.
    :return: Modified query.
    """
    if utils.is_int_like(value):
        return query.filter(models.LldpNeighbours.id == value)
    elif uuidutils.is_uuid_like(value):
        return query.filter(models.LldpNeighbours.uuid == value)


def add_lldp_tlv_filter_by_neighbour(query, neighbourid):
    """Adds an lldp-specific filter to a query.

    Filters results by neighbour id if supplied value is an integer.
    Filters results by neighbour UUID if supplied value is a UUID.

    :param query: Initial query to add filter to.
    :param neighbourid: Value for filtering results by.
    :return: Modified query.
    """
    if utils.is_int_like(neighbourid):
        return query.filter_by(neighbour_id=neighbourid)
    elif uuidutils.is_uuid_like(neighbourid):
        query = query.join(
            models.LldpNeighbours,
            models.LldpTlvs.neighbour_id == models.LldpNeighbours.id)
        return query.filter(models.LldpNeighbours.uuid == neighbourid)


def add_lldp_tlv_filter_by_agent(query, agentid):
    """Adds an lldp-specific filter to a query.

    Filters results by agent id if supplied value is an integer.
    Filters results by agent UUID if supplied value is a UUID.

    :param query: Initial query to add filter to.
    :param agentid: Value for filtering results by.
    :return: Modified query.
    """
    if utils.is_int_like(agentid):
        return query.filter_by(agent_id=agentid)
    elif uuidutils.is_uuid_like(agentid):
        query = query.join(models.LldpAgents,
                           models.LldpTlvs.agent_id == models.LldpAgents.id)
        return query.filter(models.LldpAgents.uuid == agentid)


#
# SENSOR FILTERS
#
def add_sensorgroup_filter(query, value):
    """Adds a sensorgroup-specific filter to a query.

    Filters results by mac, if supplied value is a valid MAC
    address. Otherwise attempts to filter results by identity.

    :param query: Initial query to add filter to.
    :param value: Value for filtering results by.
    :return: Modified query.
    """
    if uuidutils.is_uuid_like(value):
        return query.filter(or_(models.SensorGroupsAnalog.uuid == value,
                                models.SensorGroupsDiscrete.uuid == value))
    elif utils.is_int_like(value):
        return query.filter(or_(models.SensorGroupsAnalog.id == value,
                                models.SensorGroupsDiscrete.id == value))
    else:
        return add_identity_filter(query, value, use_sensorgroupname=True)


def add_sensorgroup_filter_by_sensor(query, value):
    """Adds an sensorgroup-specific filter to a query.

    Filters results by sensor id if supplied value is an integer.
    Filters results by sensor UUID if supplied value is a UUID.
    Otherwise attempts to filter results by name

    :param query: Initial query to add filter to.
    :param value: Value for filtering results by.
    :return: Modified query.
    """
    query = query.join(models.Sensors)
    if utils.is_int_like(value):
        return query.filter(models.Sensors.id == value)
    elif uuidutils.is_uuid_like(value):
        return query.filter(models.Sensors.uuid == value)
    else:
        return query.filter(models.Sensors.name == value)


def add_sensorgroup_filter_by_host(query, value):
    """Adds an sensorgroup-specific filter to a query.

    Filters results by hostid, if supplied value is an integer.
    Otherwise attempts to filter results by UUID.

    :param query: Initial query to add filter to.
    :param value: Value for filtering results by.
    :return: Modified query.
    """
    if utils.is_int_like(value):
        return query.filter_by(host_id=value)
    else:
        query = query.join(models.Hosts,
                           models.SensorGroups.host_id == models.Hosts.id)
        return query.filter(models.Hosts.uuid == value)


def add_sensor_filter(query, value):
    """Adds a sensor-specific filter to a query.

    Filters results by identity.

    :param query: Initial query to add filter to.
    :param value: Value for filtering results by.
    :return: Modified query.
    """
    return add_identity_filter(query, value, use_sensorname=True)


def add_sensor_filter_by_host(query, hostid):
    """Adds a sensor-specific ihost filter to a query.

    Filters results by host id if supplied value is an integer,
    otherwise attempts to filter results by host uuid.

    :param query: Initial query to add filter to.
    :param hostid: host id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(hostid):
        return query.filter_by(host_id=hostid)
    elif utils.is_uuid_like(hostid):
        query = query.join(models.Hosts)
        return query.filter(models.Hosts.uuid == hostid)

    LOG.debug("sensor_filter_by_host: "
              "No match for supplied filter id (%s)" % str(hostid))


def add_sensor_filter_by_sensorgroup(query, sensorgroupid):
    """Adds a sensor-specific sensorgroup filter to a query.

    Filters results by sensorgroup id if supplied value is an integer,
    otherwise attempts to filter results by sensorgroup uuid.

    :param query: Initial query to add filter to.
    :param sensorgroupid: sensorgroup id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(sensorgroupid):
        return query.filter_by(sensorgroup_id=sensorgroupid)

    elif utils.is_uuid_like(sensorgroupid):
        query = query.join(models.SensorGroups,
                           models.Sensors.sensorgroup_id ==
                           models.SensorGroups.id)

        return query.filter(models.SensorGroups.uuid == sensorgroupid)

    LOG.warn("sensor_filter_by_sensorgroup: "
             "No match for supplied filter id (%s)" % str(sensorgroupid))


def add_sensor_filter_by_host_sensorgroup(query, hostid, sensorgroupid):
    """Adds a sensor-specific host and sensorgroup filter to a query.

    Filters results by host id and sensorgroup id if supplied hostid and
    sensorgroupid are integers, otherwise attempts to filter results by
    host uuid and sensorgroup uuid.

    :param query: Initial query to add filter to.
    :param hostid: host id or uuid to filter results by.
    :param sensorgroupid: sensorgroup id or uuid to filter results by.
    :return: Modified query.
    """
    if utils.is_int_like(hostid) and utils.is_int_like(sensorgroupid):
        return query.filter_by(host_id=hostid, sensorgroup_id=sensorgroupid)

    elif utils.is_uuid_like(hostid) and utils.is_uuid_like(sensorgroupid):
        query = query.join(models.Hosts,
                           models.SensorGroups)
        return query.filter(models.Hosts.uuid == hostid,
                            models.SensorGroups.uuid == sensorgroupid)

    LOG.debug("sensor_filter_by_host_isensorgroup: "
              "No match for supplied filter ids (%s, %s)"
              % (str(hostid), str(sensorgroupid)))


class Connection(api.Connection):
    """SQLAlchemy connection."""

    def __init__(self):
        pass

    def get_session(self, autocommit=True):
        return get_session(autocommit)

    def get_engine(self):
        return get_engine()

    def _system_get(self, system):
        query = model_query(models.Systems)
        query = add_identity_filter(query, system)
        try:
            result = query.one()
        except NoResultFound:
            raise exception.SystemNotFound(system=system)
        return result

    def system_create(self, values):
        if not values.get('uuid'):
            # The system uuid comes from systemconfig
            raise exception.SystemNotFound(system=values)
        system = models.Systems()
        system.update(values)
        with _session_for_write() as session:
            try:
                session.add(system)
                session.flush()
            except db_exc.DBDuplicateEntry:
                raise exception.SystemAlreadyExists(uuid=values['uuid'])
            return self._system_get(values['uuid'])

    def system_get(self, system):
        return self._system_get(system)

    def system_get_one(self):
        query = model_query(models.Systems)
        try:
            return query.one()
        except NoResultFound:
            raise exception.NotFound()

    def system_get_list(self, limit=None, marker=None,
                        sort_key=None, sort_dir=None):
        query = model_query(models.Systems)
        return _paginate_query(models.Systems, limit, marker,
                               sort_key, sort_dir, query)

    def system_update(self, system, values):
        with _session_for_write() as session:
            query = model_query(models.Systems, session=session)
            query = add_identity_filter(query, system)

            count = query.update(values, synchronize_session='fetch')
            if count != 1:
                raise exception.SystemNotFound(system=system)
            return query.one()

    def system_delete(self, system):
        with _session_for_write() as session:
            query = model_query(models.Systems, session=session)
        query = add_identity_filter(query, system)
        try:
            query.one()
        except NoResultFound:
            raise exception.SystemNotFound(system=system)
        query.delete()

    #
    # Hosts
    #

    def _add_hosts_filters(self, query, filters):
        if filters is None:
            filters = dict()
        supported_filters = {'hostname',
                             'invprovision',
                             'mgmt_mac',
                             'personality',
                             }
        unsupported_filters = set(filters).difference(supported_filters)
        if unsupported_filters:
            msg = _("SqlAlchemy API does not support "
                    "filtering by %s") % ', '.join(unsupported_filters)
            raise ValueError(msg)

        for field in supported_filters:
            if field in filters:
                query = query.filter_by(**{field: filters[field]})

        return query

    def _host_get(self, host):
        query = model_query(models.Hosts)
        if utils.is_uuid_like(host):
            host.strip()
        query = add_identity_filter(query, host)
        try:
            return query.one()
        except NoResultFound:
            raise exception.HostNotFound(host=host)

    def host_create(self, values):
        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()
        host = models.Hosts()
        host.update(values)
        with _session_for_write() as session:
            try:
                session.add(host)
                session.flush()
            except db_exc.DBDuplicateEntry:
                raise exception.HostAlreadyExists(uuid=values['uuid'])
            return self._host_get(values['uuid'])

    def host_get(self, host):
        return self._host_get(host)

    def host_get_list(self, filters=None, limit=None, marker=None,
                      sort_key=None, sort_dir=None):
        query = model_query(models.Hosts)
        query = self._add_hosts_filters(query, filters)
        return _paginate_query(models.Hosts, limit, marker,
                               sort_key, sort_dir, query)

    def host_get_by_filters_one(self, filters):
        query = model_query(models.Hosts)
        query = self._add_hosts_filters(query, filters)
        try:
            return query.one()
        except NoResultFound:
            raise exception.HostNotFound(host=filters)

    def host_get_by_hostname(self, hostname):
        query = model_query(models.Hosts)
        query = query.filter_by(hostname=hostname)
        try:
            return query.one()
        except NoResultFound:
            raise exception.HostNotFound(host=hostname)

    def host_get_by_personality(self, personality,
                                limit=None, marker=None,
                                sort_key=None, sort_dir=None):
        query = model_query(models.Hosts)
        query = query.filter_by(personality=personality)
        return _paginate_query(models.Hosts, limit, marker,
                               sort_key, sort_dir, query)

    def host_get_by_mgmt_mac(self, mgmt_mac):
        try:
            mgmt_mac = mgmt_mac.rstrip()
            mgmt_mac = utils.validate_and_normalize_mac(mgmt_mac)
        except exception.InventoryException:
            raise exception.HostNotFound(host=mgmt_mac)

        query = model_query(models.Hosts)
        query = query.filter_by(mgmt_mac=mgmt_mac)

        try:
            return query.one()
        except NoResultFound:
            raise exception.HostNotFound(host=mgmt_mac)

    def host_update(self, host, values, context=None):
        with _session_for_write() as session:
            query = model_query(models.Hosts, session=session)
            query = add_identity_filter(query, host)
            count = query.update(values, synchronize_session='fetch')
            if count != 1:
                raise exception.HostNotFound(host=host)
        return self._host_get(host)

    def host_destroy(self, host):
        with _session_for_write() as session:
            query = model_query(models.Hosts, session=session)
        query = add_identity_filter(query, host)
        try:
            query.one()
        except NoResultFound:
            raise exception.HostNotFound(host=host)
        query.delete()

    #
    # Ports
    #

    def _port_get(self, portid, hostid=None):
        query = model_query(models.Ports)

        if hostid:
            query = query.filter_by(host_id=hostid)

        query = add_identity_filter(query, portid, use_name=True)

        try:
            return query.one()
        except NoResultFound:
            raise exception.PortNotFound(port=portid)

    def port_get(self, portid, hostid=None):
        return self._port_get(portid, hostid)

    def port_get_list(self, limit=None, marker=None,
                      sort_key=None, sort_dir=None):
        return _paginate_query(models.Ports, limit, marker,
                               sort_key, sort_dir)

    def port_get_all(self, hostid=None, interfaceid=None):
        query = model_query(models.Ports, read_deleted="no")
        if hostid:
            query = query.filter_by(host_id=hostid)
        if interfaceid:
            query = query.filter_by(interface_id=interfaceid)
        return query.all()

    def port_get_by_host(self, host,
                         limit=None, marker=None,
                         sort_key=None, sort_dir=None):
        query = model_query(models.Ports)
        query = add_port_filter_by_host(query, host)
        return _paginate_query(models.Ports, limit, marker,
                               sort_key, sort_dir, query)

    def port_get_by_numa_node(self, node,
                              limit=None, marker=None,
                              sort_key=None, sort_dir=None):

        query = model_query(models.Ports)
        query = add_port_filter_by_numa_node(query, node)
        return _paginate_query(models.Ports, limit, marker,
                               sort_key, sort_dir, query)

    def _ethernet_port_get(self, portid, hostid=None):
        query = model_query(models.EthernetPorts)

        if hostid:
            query = query.filter_by(host_id=hostid)

        query = add_identity_filter(query, portid, use_name=True)

        try:
            return query.one()
        except NoResultFound:
            raise exception.PortNotFound(port=portid)

    def ethernet_port_create(self, hostid, values):
        if utils.is_int_like(hostid):
            host = self.host_get(int(hostid))
        elif utils.is_uuid_like(hostid):
            host = self.host_get(hostid.strip())
        elif isinstance(hostid, models.Hosts):
            host = hostid
        else:
            raise exception.HostNotFound(host=hostid)

        values['host_id'] = host['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        ethernet_port = models.EthernetPorts()
        ethernet_port.update(values)
        with _session_for_write() as session:
            try:
                session.add(ethernet_port)
                session.flush()
            except db_exc.DBDuplicateEntry:
                LOG.error("Failed to add port %s (uuid: %s), port with MAC "
                          "address %s on host %s already exists" %
                          (values['name'],
                           values['uuid'],
                           values['mac'],
                           values['host_id']))
                raise exception.MACAlreadyExists(mac=values['mac'],
                                                 host=values['host_id'])

            return self._ethernet_port_get(values['uuid'])

    def ethernet_port_get(self, portid, hostid=None):
        return self._ethernet_port_get(portid, hostid)

    def ethernet_port_get_by_mac(self, mac):
        query = model_query(models.EthernetPorts).filter_by(mac=mac)
        try:
            return query.one()
        except NoResultFound:
            raise exception.PortNotFound(port=mac)

    def ethernet_port_get_list(self, limit=None, marker=None,
                               sort_key=None, sort_dir=None):
        return _paginate_query(models.EthernetPorts, limit, marker,
                               sort_key, sort_dir)

    def ethernet_port_get_all(self, hostid=None):
        query = model_query(models.EthernetPorts, read_deleted="no")
        if hostid:
            query = query.filter_by(host_id=hostid)
        return query.all()

    def ethernet_port_get_by_host(self, host,
                                  limit=None, marker=None,
                                  sort_key=None, sort_dir=None):
        query = model_query(models.EthernetPorts)
        query = add_port_filter_by_host(query, host)
        return _paginate_query(models.EthernetPorts, limit, marker,
                               sort_key, sort_dir, query)

    def ethernet_port_get_by_numa_node(self, node,
                                       limit=None, marker=None,
                                       sort_key=None, sort_dir=None):
        query = model_query(models.EthernetPorts)
        query = add_port_filter_by_numa_node(query, node)
        return _paginate_query(models.EthernetPorts, limit, marker,
                               sort_key, sort_dir, query)

    def ethernet_port_update(self, portid, values):
        with _session_for_write() as session:
            # May need to reserve in multi controller system; ref sysinv
            query = model_query(models.EthernetPorts, read_deleted="no",
                                session=session)
            query = add_identity_filter(query, portid)

            try:
                result = query.one()
                for k, v in values.items():
                    setattr(result, k, v)
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for port %s" % portid)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for port %s" % portid)

            return query.one()

    def ethernet_port_destroy(self, portid):
        with _session_for_write() as session:
            # Delete port which should cascade to delete EthernetPort
            if uuidutils.is_uuid_like(portid):
                model_query(models.Ports, read_deleted="no",
                            session=session).\
                    filter_by(uuid=portid).\
                    delete()
            else:
                model_query(models.Ports, read_deleted="no",
                            session=session).\
                    filter_by(id=portid).\
                    delete()

    #
    # Nodes
    #

    def _node_get(self, node_id):
        query = model_query(models.Nodes)
        query = add_identity_filter(query, node_id)

        try:
            result = query.one()
        except NoResultFound:
            raise exception.NodeNotFound(node=node_id)

        return result

    def node_create(self, host_id, values):
        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()
        values['host_id'] = int(host_id)
        node = models.Nodes()
        node.update(values)
        with _session_for_write() as session:
            try:
                session.add(node)
                session.flush()
            except db_exc.DBDuplicateEntry:
                raise exception.NodeAlreadyExists(uuid=values['uuid'])

            return self._node_get(values['uuid'])

    def node_get_all(self, host_id=None):
        query = model_query(models.Nodes, read_deleted="no")
        if host_id:
            query = query.filter_by(host_id=host_id)
        return query.all()

    def node_get(self, node_id):
        return self._node_get(node_id)

    def node_get_list(self, limit=None, marker=None,
                      sort_key=None, sort_dir=None):
        return _paginate_query(models.Nodes, limit, marker,
                               sort_key, sort_dir)

    def node_get_by_host(self, host,
                         limit=None, marker=None,
                         sort_key=None, sort_dir=None):

        query = model_query(models.Nodes)
        query = add_node_filter_by_host(query, host)
        return _paginate_query(models.Nodes, limit, marker,
                               sort_key, sort_dir, query)

    def node_update(self, node_id, values):
        with _session_for_write() as session:
            # May need to reserve in multi controller system; ref sysinv
            query = model_query(models.Nodes, read_deleted="no",
                                session=session)
            query = add_identity_filter(query, node_id)

            count = query.update(values, synchronize_session='fetch')
            if count != 1:
                raise exception.NodeNotFound(node=node_id)
            return query.one()

    def node_destroy(self, node_id):
        with _session_for_write() as session:
            # Delete physically since it has unique columns
            if uuidutils.is_uuid_like(node_id):
                model_query(models.Nodes, read_deleted="no",
                            session=session).\
                    filter_by(uuid=node_id).\
                    delete()
            else:
                model_query(models.Nodes, read_deleted="no").\
                    filter_by(id=node_id).\
                    delete()

    #
    # Cpus
    #

    def _cpu_get(self, cpu_id, host_id=None):
        query = model_query(models.Cpus)

        if host_id:
            query = query.filter_by(host_id=host_id)

        query = add_identity_filter(query, cpu_id)

        try:
            result = query.one()
        except NoResultFound:
            raise exception.CPUNotFound(cpu=cpu_id)

        return result

    def cpu_create(self, host_id, values):

        if utils.is_int_like(host_id):
            values['host_id'] = int(host_id)
        else:
            # this is not necessary if already integer following not work
            host = self.host_get(host_id.strip())
            values['host_id'] = host['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        cpu = models.Cpus()
        cpu.update(values)

        with _session_for_write() as session:
            try:
                session.add(cpu)
                session.flush()
            except db_exc.DBDuplicateEntry:
                raise exception.CPUAlreadyExists(cpu=values['cpu'])
            return self._cpu_get(values['uuid'])

    def cpu_get_all(self, host_id=None, fornodeid=None):
        query = model_query(models.Cpus, read_deleted="no")
        if host_id:
            query = query.filter_by(host_id=host_id)
        if fornodeid:
            query = query.filter_by(fornodeid=fornodeid)
        return query.all()

    def cpu_get(self, cpu_id, host_id=None):
        return self._cpu_get(cpu_id, host_id)

    def cpu_get_list(self, limit=None, marker=None,
                     sort_key=None, sort_dir=None):
        return _paginate_query(models.Cpus, limit, marker,
                               sort_key, sort_dir)

    def cpu_get_by_host(self, host,
                        limit=None, marker=None,
                        sort_key=None, sort_dir=None):

        query = model_query(models.Cpus)
        query = add_cpu_filter_by_host(query, host)
        return _paginate_query(models.Cpus, limit, marker,
                               sort_key, sort_dir, query)

    def cpu_get_by_node(self, node,
                        limit=None, marker=None,
                        sort_key=None, sort_dir=None):

        query = model_query(models.Cpus)
        query = add_cpu_filter_by_node(query, node)
        return _paginate_query(models.Cpus, limit, marker,
                               sort_key, sort_dir, query)

    def cpu_get_by_host_node(self, host, node,
                             limit=None, marker=None,
                             sort_key=None, sort_dir=None):

        query = model_query(models.Cpus)
        query = add_cpu_filter_by_host_node(query, host, node)
        return _paginate_query(models.Cpus, limit, marker,
                               sort_key, sort_dir, query)

    def cpu_update(self, cpu_id, values, host_id=None):
        with _session_for_write() as session:
            # May need to reserve in multi controller system; ref sysinv
            query = model_query(models.Cpus, read_deleted="no",
                                session=session)
            if host_id:
                query = query.filter_by(host_id=host_id)

            query = add_identity_filter(query, cpu_id)

            count = query.update(values, synchronize_session='fetch')
            if count != 1:
                raise exception.CPUNotFound(cpu=cpu_id)
            return query.one()

    def cpu_destroy(self, cpu_id):
        with _session_for_write() as session:
            # Delete physically since it has unique columns
            if uuidutils.is_uuid_like(cpu_id):
                model_query(models.Cpus, read_deleted="no", session=session).\
                    filter_by(uuid=cpu_id).\
                    delete()
            else:
                model_query(models.Cpus, read_deleted="no").\
                    filter_by(id=cpu_id).\
                    delete()

    #
    # Memory
    #

    def _memory_get(self, memory_id, host_id=None):
        query = model_query(models.Memorys)

        if host_id:
            query = query.filter_by(host_id=host_id)

        query = add_identity_filter(query, memory_id)

        try:
            result = query.one()
        except NoResultFound:
            raise exception.MemoryNotFound(memory=memory_id)

        return result

    def memory_create(self, host_id, values):
        if utils.is_int_like(host_id):
            values['host_id'] = int(host_id)
        else:
            # this is not necessary if already integer following not work
            host = self.host_get(host_id.strip())
            values['host_id'] = host['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        values.pop('numa_node', None)

        memory = models.Memorys()
        memory.update(values)
        with _session_for_write() as session:
            try:
                session.add(memory)
                session.flush()
            except db_exc.DBDuplicateEntry:
                raise exception.MemoryAlreadyExists(uuid=values['uuid'])
            return self._memory_get(values['uuid'])

    def memory_get_all(self, host_id=None, fornodeid=None):
        query = model_query(models.Memorys, read_deleted="no")
        if host_id:
            query = query.filter_by(host_id=host_id)
        if fornodeid:
            query = query.filter_by(fornodeid=fornodeid)
        return query.all()

    def memory_get(self, memory_id, host_id=None):
        return self._memory_get(memory_id, host_id)

    def memory_get_list(self, limit=None, marker=None,
                        sort_key=None, sort_dir=None):
        return _paginate_query(models.Memorys, limit, marker,
                               sort_key, sort_dir)

    def memory_get_by_host(self, host,
                           limit=None, marker=None,
                           sort_key=None, sort_dir=None):

        query = model_query(models.Memorys)
        query = add_memory_filter_by_host(query, host)
        return _paginate_query(models.Memorys, limit, marker,
                               sort_key, sort_dir, query)

    def memory_get_by_node(self, node,
                           limit=None, marker=None,
                           sort_key=None, sort_dir=None):

        query = model_query(models.Memorys)
        query = add_memory_filter_by_node(query, node)
        return _paginate_query(models.Memorys, limit, marker,
                               sort_key, sort_dir, query)

    def memory_get_by_host_node(self, host, node,
                                limit=None, marker=None,
                                sort_key=None, sort_dir=None):

        query = model_query(models.Memorys)
        query = add_memory_filter_by_host_node(query, host, node)
        return _paginate_query(models.Memorys, limit, marker,
                               sort_key, sort_dir, query)

    def memory_update(self, memory_id, values, host_id=None):
        with _session_for_write() as session:
            # May need to reserve in multi controller system; ref sysinv
            query = model_query(models.Memorys, read_deleted="no",
                                session=session)
            if host_id:
                query = query.filter_by(host_id=host_id)

            query = add_identity_filter(query, memory_id)

            values.pop('numa_node', None)

            count = query.update(values, synchronize_session='fetch')
            if count != 1:
                raise exception.MemoryNotFound(memory=memory_id)
            return query.one()

    def memory_destroy(self, memory_id):
        with _session_for_write() as session:
            # Delete physically since it has unique columns
            if uuidutils.is_uuid_like(memory_id):
                model_query(models.Memorys, read_deleted="no",
                            session=session).\
                    filter_by(uuid=memory_id).\
                    delete()
            else:
                model_query(models.Memorys, read_deleted="no",
                            session=session).\
                    filter_by(id=memory_id).\
                    delete()

    #
    # PciDevices
    #

    def pci_device_create(self, hostid, values):

        if utils.is_int_like(hostid):
            host = self.host_get(int(hostid))
        elif utils.is_uuid_like(hostid):
            host = self.host_get(hostid.strip())
        elif isinstance(hostid, models.Hosts):
            host = hostid
        else:
            raise exception.HostNotFound(host=hostid)

        values['host_id'] = host['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        pci_device = models.PciDevices()
        pci_device.update(values)
        with _session_for_write() as session:
            try:
                session.add(pci_device)
                session.flush()
            except db_exc.DBDuplicateEntry:
                LOG.error("Failed to add pci device %s:%s (uuid: %s), "
                          "device with PCI address %s on host %s "
                          "already exists" %
                          (values['vendor'],
                           values['device'],
                           values['uuid'],
                           values['pciaddr'],
                           values['host_id']))
                raise exception.PCIAddrAlreadyExists(pciaddr=values['pciaddr'],
                                                     host=values['host_id'])

    def pci_device_get_all(self, hostid=None):
        query = model_query(models.PciDevices, read_deleted="no")
        if hostid:
            query = query.filter_by(host_id=hostid)
        return query.all()

    def pci_device_get(self, deviceid, hostid=None):
        query = model_query(models.PciDevices)
        if hostid:
            query = query.filter_by(host_id=hostid)
        query = add_identity_filter(query, deviceid, use_pciaddr=True)
        try:
            result = query.one()
        except NoResultFound:
            raise exception.PCIDeviceNotFound(pcidevice_id=deviceid)

        return result

    def pci_device_get_list(self, limit=None, marker=None,
                            sort_key=None, sort_dir=None):
        return _paginate_query(models.PciDevices, limit, marker,
                               sort_key, sort_dir)

    def pci_device_get_by_host(self, host, limit=None, marker=None,
                               sort_key=None, sort_dir=None):
        query = model_query(models.PciDevices)
        query = add_device_filter_by_host(query, host)
        return _paginate_query(models.PciDevices, limit, marker,
                               sort_key, sort_dir, query)

    def pci_device_update(self, device_id, values, host_id=None):
        with _session_for_write() as session:
            query = model_query(models.PciDevices, read_deleted="no",
                                session=session)

            if host_id:
                query = query.filter_by(host_id=host_id)

            try:
                query = add_identity_filter(query, device_id)
                result = query.one()
                for k, v in values.items():
                    setattr(result, k, v)
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for device %s" % device_id)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for device %s" % device_id)

            return query.one()

    def pci_device_destroy(self, device_id):
        with _session_for_write() as session:
            if uuidutils.is_uuid_like(device_id):
                model_query(models.PciDevices, read_deleted="no",
                            session=session).\
                    filter_by(uuid=device_id).\
                    delete()
            else:
                model_query(models.PciDevices, read_deleted="no",
                            session=session).\
                    filter_by(id=device_id).\
                    delete()

    #
    # LLDP
    #

    def _lldp_agent_get(self, agentid, hostid=None):
        query = model_query(models.LldpAgents)

        if hostid:
            query = query.filter_by(host_id=hostid)

        query = add_lldp_filter_by_agent(query, agentid)

        try:
            return query.one()
        except NoResultFound:
            raise exception.LldpAgentNotFound(agent=agentid)

    def lldp_agent_create(self, portid, hostid, values):
        host = self.host_get(hostid)
        port = self.port_get(portid)

        values['host_id'] = host['id']
        values['port_id'] = port['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        lldp_agent = models.LldpAgents()
        lldp_agent.update(values)
        with _session_for_write() as session:
            try:
                session.add(lldp_agent)
                session.flush()
            except db_exc.DBDuplicateEntry:
                LOG.error("Failed to add lldp agent %s, on host %s:"
                          "already exists" %
                          (values['uuid'],
                           values['host_id']))
                raise exception.LLDPAgentExists(uuid=values['uuid'],
                                                host=values['host_id'])
            return self._lldp_agent_get(values['uuid'])

    def lldp_agent_get(self, agentid, hostid=None):
        return self._lldp_agent_get(agentid, hostid)

    def lldp_agent_get_list(self, limit=None, marker=None,
                            sort_key=None, sort_dir=None):
        return _paginate_query(models.LldpAgents, limit, marker,
                               sort_key, sort_dir)

    def lldp_agent_get_all(self, hostid=None, portid=None):
        query = model_query(models.LldpAgents, read_deleted="no")
        if hostid:
            query = query.filter_by(host_id=hostid)
        if portid:
            query = query.filter_by(port_id=portid)
        return query.all()

    def lldp_agent_get_by_host(self, host,
                               limit=None, marker=None,
                               sort_key=None, sort_dir=None):
        query = model_query(models.LldpAgents)
        query = add_lldp_filter_by_host(query, host)
        return _paginate_query(models.LldpAgents, limit, marker,
                               sort_key, sort_dir, query)

    def lldp_agent_get_by_port(self, port):
        query = model_query(models.LldpAgents)
        query = add_lldp_filter_by_port(query, port)
        try:
            return query.one()
        except NoResultFound:
            raise exception.InvalidParameterValue(
                err="No entry found for agent on port %s" % port)
        except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for agent on port %s" % port)

    def lldp_agent_update(self, uuid, values):
        with _session_for_write():
            query = model_query(models.LldpAgents, read_deleted="no")

            try:
                query = add_lldp_filter_by_agent(query, uuid)
                result = query.one()
                for k, v in values.items():
                    setattr(result, k, v)
                return result
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for agent %s" % uuid)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for agent %s" % uuid)

    def lldp_agent_destroy(self, agentid):

        with _session_for_write():
            query = model_query(models.LldpAgents, read_deleted="no")
            query = add_lldp_filter_by_agent(query, agentid)

            try:
                query.delete()
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for agent %s" % agentid)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for agent %s" % agentid)

    def _lldp_neighbour_get(self, neighbourid, hostid=None):
        query = model_query(models.LldpNeighbours)

        if hostid:
            query = query.filter_by(host_id=hostid)

        query = add_lldp_filter_by_neighbour(query, neighbourid)

        try:
            return query.one()
        except NoResultFound:
            raise exception.LldpNeighbourNotFound(neighbour=neighbourid)

    def lldp_neighbour_create(self, portid, hostid, values):
        if utils.is_int_like(hostid):
            host = self.host_get(int(hostid))
        elif utils.is_uuid_like(hostid):
            host = self.host_get(hostid.strip())
        elif isinstance(hostid, models.Hosts):
            host = hostid
        else:
            raise exception.HostNotFound(host=hostid)
        if utils.is_int_like(portid):
            port = self.port_get(int(portid))
        elif utils.is_uuid_like(portid):
            port = self.port_get(portid.strip())
        elif isinstance(portid, models.port):
            port = portid
        else:
            raise exception.PortNotFound(port=portid)

        values['host_id'] = host['id']
        values['port_id'] = port['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        lldp_neighbour = models.LldpNeighbours()
        lldp_neighbour.update(values)
        with _session_for_write() as session:
            try:
                session.add(lldp_neighbour)
                session.flush()
            except db_exc.DBDuplicateEntry:
                LOG.error("Failed to add lldp neighbour %s, on port %s:. "
                          "Already exists with msap %s" %
                          (values['uuid'],
                           values['port_id'],
                           values['msap']))
                raise exception.LLDPNeighbourExists(uuid=values['uuid'])

            return self._lldp_neighbour_get(values['uuid'])

    def lldp_neighbour_get(self, neighbourid, hostid=None):
        return self._lldp_neighbour_get(neighbourid, hostid)

    def lldp_neighbour_get_list(self, limit=None, marker=None,
                                sort_key=None, sort_dir=None):
        return _paginate_query(models.LldpNeighbours, limit, marker,
                               sort_key, sort_dir)

    def lldp_neighbour_get_by_host(self, host,
                                   limit=None, marker=None,
                                   sort_key=None, sort_dir=None):
        query = model_query(models.LldpNeighbours)
        query = add_port_filter_by_host(query, host)
        return _paginate_query(models.LldpNeighbours, limit, marker,
                               sort_key, sort_dir, query)

    def lldp_neighbour_get_by_port(self, port,
                                   limit=None, marker=None,
                                   sort_key=None, sort_dir=None):
        query = model_query(models.LldpNeighbours)
        query = add_lldp_filter_by_port(query, port)
        return _paginate_query(models.LldpNeighbours, limit, marker,
                               sort_key, sort_dir, query)

    def lldp_neighbour_update(self, uuid, values):
        with _session_for_write():
            query = model_query(models.LldpNeighbours, read_deleted="no")

            try:
                query = add_lldp_filter_by_neighbour(query, uuid)
                result = query.one()
                for k, v in values.items():
                    setattr(result, k, v)
                return result
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for uuid %s" % uuid)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for uuid %s" % uuid)

    def lldp_neighbour_destroy(self, neighbourid):
        with _session_for_write():
            query = model_query(models.LldpNeighbours, read_deleted="no")
            query = add_lldp_filter_by_neighbour(query, neighbourid)
            try:
                query.delete()
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for neighbour %s" % neighbourid)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for neighbour %s" %
                    neighbourid)

    def _lldp_tlv_get(self, type, agentid=None, neighbourid=None,
                      session=None):
        if not agentid and not neighbourid:
            raise exception.InvalidParameterValue(
                err="agent id and neighbour id not specified")

        query = model_query(models.LldpTlvs, session=session)

        if agentid:
            query = query.filter_by(agent_id=agentid)

        if neighbourid:
            query = query.filter_by(neighbour_id=neighbourid)

        query = query.filter_by(type=type)

        try:
            return query.one()
        except NoResultFound:
            raise exception.LldpTlvNotFound(type=type)
        except MultipleResultsFound:
            raise exception.InvalidParameterValue(
                err="Multiple entries found")

    def lldp_tlv_create(self, values, agentid=None, neighbourid=None):
        if not agentid and not neighbourid:
            raise exception.InvalidParameterValue(
                err="agent id and neighbour id not specified")

        if agentid:
            if utils.is_int_like(agentid):
                agent = self.lldp_agent_get(int(agentid))
            elif utils.is_uuid_like(agentid):
                agent = self.lldp_agent_get(agentid.strip())
            elif isinstance(agentid, models.lldp_agents):
                agent = agentid
            else:
                raise exception.LldpAgentNotFound(agent=agentid)

        if neighbourid:
            if utils.is_int_like(neighbourid):
                neighbour = self.lldp_neighbour_get(int(neighbourid))
            elif utils.is_uuid_like(neighbourid):
                neighbour = self.lldp_neighbour_get(neighbourid.strip())
            elif isinstance(neighbourid, models.lldp_neighbours):
                neighbour = neighbourid
            else:
                raise exception.LldpNeighbourNotFound(neighbour=neighbourid)

        if agentid:
            values['agent_id'] = agent['id']

        if neighbourid:
            values['neighbour_id'] = neighbour['id']

        lldp_tlv = models.LldpTlvs()
        lldp_tlv.update(values)
        with _session_for_write() as session:
            try:
                session.add(lldp_tlv)
                session.flush()
            except db_exc.DBDuplicateEntry:
                LOG.error("Failed to add lldp tlv %s"
                          "already exists" % (values['type']))
                raise exception.LLDPTlvExists(uuid=values['id'])
            return self._lldp_tlv_get(values['type'],
                                      agentid=values.get('agent_id'),
                                      neighbourid=values.get('neighbour_id'))

    def lldp_tlv_create_bulk(self, values, agentid=None, neighbourid=None):
        if not agentid and not neighbourid:
            raise exception.InvalidParameterValue(
                err="agent id and neighbour id not specified")

        if agentid:
            if utils.is_int_like(agentid):
                agent = self.lldp_agent_get(int(agentid))
            elif utils.is_uuid_like(agentid):
                agent = self.lldp_agent_get(agentid.strip())
            elif isinstance(agentid, models.lldp_agents):
                agent = agentid
            else:
                raise exception.LldpAgentNotFound(agent=agentid)

        if neighbourid:
            if utils.is_int_like(neighbourid):
                neighbour = self.lldp_neighbour_get(int(neighbourid))
            elif utils.is_uuid_like(neighbourid):
                neighbour = self.lldp_neighbour_get(neighbourid.strip())
            elif isinstance(neighbourid, models.lldp_neighbours):
                neighbour = neighbourid
            else:
                raise exception.LldpNeighbourNotFound(neighbour=neighbourid)

        tlvs = []
        with _session_for_write() as session:
            for entry in values:
                lldp_tlv = models.LldpTlvs()
                if agentid:
                    entry['agent_id'] = agent['id']

                if neighbourid:
                    entry['neighbour_id'] = neighbour['id']

                lldp_tlv.update(entry)
                session.add(lldp_tlv)

                lldp_tlv = self._lldp_tlv_get(
                    entry['type'],
                    agentid=entry.get('agent_id'),
                    neighbourid=entry.get('neighbour_id'),
                    session=session)

                tlvs.append(lldp_tlv)

        return tlvs

    def lldp_tlv_get(self, type, agentid=None, neighbourid=None):
        return self._lldp_tlv_get(type, agentid, neighbourid)

    def lldp_tlv_get_by_id(self, id, agentid=None, neighbourid=None):
        query = model_query(models.LldpTlvs)

        query = query.filter_by(id=id)
        try:
            result = query.one()
        except NoResultFound:
            raise exception.LldpTlvNotFound(id=id)
        except MultipleResultsFound:
            raise exception.InvalidParameterValue(
                err="Multiple entries found")

        return result

    def lldp_tlv_get_list(self, limit=None, marker=None,
                          sort_key=None, sort_dir=None):
        return _paginate_query(models.LldpTlvs, limit, marker,
                               sort_key, sort_dir)

    def lldp_tlv_get_all(self, agentid=None, neighbourid=None):
        query = model_query(models.LldpTlvs, read_deleted="no")
        if agentid:
            query = query.filter_by(agent_id=agentid)
        if neighbourid:
            query = query.filter_by(neighbour_id=neighbourid)
        return query.all()

    def lldp_tlv_get_by_agent(self, agent,
                              limit=None, marker=None,
                              sort_key=None, sort_dir=None):
        query = model_query(models.LldpTlvs)
        query = add_lldp_tlv_filter_by_agent(query, agent)
        return _paginate_query(models.LldpTlvs, limit, marker,
                               sort_key, sort_dir, query)

    def lldp_tlv_get_by_neighbour(self, neighbour,
                                  limit=None, marker=None,
                                  sort_key=None, sort_dir=None):

        query = model_query(models.LldpTlvs)
        query = add_lldp_tlv_filter_by_neighbour(query, neighbour)
        return _paginate_query(models.LldpTlvs, limit, marker,
                               sort_key, sort_dir, query)

    def lldp_tlv_update(self, values, agentid=None, neighbourid=None):
        if not agentid and not neighbourid:
                raise exception.InvalidParameterValue(
                    err="agent id and neighbour id not specified")

        with _session_for_write():
            query = model_query(models.LldpTlvs, read_deleted="no")

            if agentid:
                query = add_lldp_tlv_filter_by_agent(query, agentid)

            if neighbourid:
                query = add_lldp_tlv_filter_by_neighbour(query,
                                                         neighbourid)

            query = query.filter_by(type=values['type'])

            try:
                result = query.one()
                for k, v in values.items():
                    setattr(result, k, v)
                return result
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for tlv")
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found")

    def lldp_tlv_update_bulk(self, values, agentid=None, neighbourid=None):
        results = []

        if not agentid and not neighbourid:
            raise exception.InvalidParameterValue(
                err="agent id and neighbour id not specified")

        with _session_for_write() as session:
            for entry in values:
                query = model_query(models.LldpTlvs, read_deleted="no")

                if agentid:
                    query = query.filter_by(agent_id=agentid)

                if neighbourid:
                    query = query.filter_by(neighbour_id=neighbourid)

                query = query.filter_by(type=entry['type'])

                try:
                    result = query.one()
                    result.update(entry)
                    session.merge(result)
                except NoResultFound:
                    raise exception.InvalidParameterValue(
                        err="No entry found for tlv")
                except MultipleResultsFound:
                    raise exception.InvalidParameterValue(
                        err="Multiple entries found")

                results.append(result)
        return results

    def lldp_tlv_destroy(self, id):
        with _session_for_write():
            model_query(models.LldpTlvs, read_deleted="no").\
                filter_by(id=id).\
                delete()

    #
    # SENSORS
    #

    def _sensor_analog_create(self, hostid, values):
        if utils.is_int_like(hostid):
            host = self.host_get(int(hostid))
        elif utils.is_uuid_like(hostid):
            host = self.host_get(hostid.strip())
        elif isinstance(hostid, models.Hosts):
            host = hostid
        else:
            raise exception.HostNotFound(host=hostid)

        values['host_id'] = host['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        sensor_analog = models.SensorsAnalog()
        sensor_analog.update(values)

        with _session_for_write() as session:
            try:
                session.add(sensor_analog)
                session.flush()
            except db_exc.DBDuplicateEntry:
                exception.SensorAlreadyExists(uuid=values['uuid'])
            return self._sensor_analog_get(values['uuid'])

    def _sensor_analog_get(self, sensorid, hostid=None):
        query = model_query(models.SensorsAnalog)

        if hostid:
            query = query.filter_by(host_id=hostid)

        query = add_sensor_filter(query, sensorid)

        try:
            result = query.one()
        except NoResultFound:
            raise exception.SensorNotFound(sensor=sensorid)

        return result

    def _sensor_analog_get_list(self, limit=None, marker=None,
                                sort_key=None, sort_dir=None):
        return _paginate_query(models.SensorsAnalog, limit, marker,
                               sort_key, sort_dir)

    def _sensor_analog_get_all(self, hostid=None, sensorgroupid=None):
        query = model_query(models.SensorsAnalog, read_deleted="no")
        if hostid:
            query = query.filter_by(host_id=hostid)
        if sensorgroupid:
            query = query.filter_by(sensorgroup_id=hostid)
        return query.all()

    def _sensor_analog_get_by_host(self, host,
                                   limit=None, marker=None,
                                   sort_key=None, sort_dir=None):
        query = model_query(models.SensorsAnalog)
        query = add_port_filter_by_host(query, host)
        return _paginate_query(models.SensorsAnalog, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_analog_get_by_sensorgroup(self, sensorgroup,
                                          limit=None, marker=None,
                                          sort_key=None, sort_dir=None):

        query = model_query(models.SensorsAnalog)
        query = add_sensor_filter_by_sensorgroup(query, sensorgroup)
        return _paginate_query(models.SensorsAnalog, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_analog_get_by_host_sensorgroup(self, host, sensorgroup,
                                               limit=None, marker=None,
                                               sort_key=None, sort_dir=None):
        query = model_query(models.SensorsAnalog)
        query = add_sensor_filter_by_host_sensorgroup(query,
                                                      host,
                                                      sensorgroup)
        return _paginate_query(models.SensorsAnalog, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_analog_update(self, sensorid, values, hostid=None):
        with _session_for_write():
            # May need to reserve in multi controller system; ref sysinv
            query = model_query(models.SensorsAnalog, read_deleted="no")

            if hostid:
                query = query.filter_by(host_id=hostid)

            try:
                query = add_sensor_filter(query, sensorid)
                result = query.one()
                for k, v in values.items():
                    setattr(result, k, v)
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for port %s" % sensorid)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for port %s" % sensorid)

            return query.one()

    def _sensor_analog_destroy(self, sensorid):
        with _session_for_write():
            # Delete port which should cascade to delete SensorsAnalog
            if uuidutils.is_uuid_like(sensorid):
                model_query(models.Sensors, read_deleted="no").\
                    filter_by(uuid=sensorid).\
                    delete()
            else:
                model_query(models.Sensors, read_deleted="no").\
                    filter_by(id=sensorid).\
                    delete()

    def sensor_analog_create(self, hostid, values):
        return self._sensor_analog_create(hostid, values)

    def sensor_analog_get(self, sensorid, hostid=None):
        return self._sensor_analog_get(sensorid, hostid)

    def sensor_analog_get_list(self, limit=None, marker=None,
                               sort_key=None, sort_dir=None):
        return self._sensor_analog_get_list(limit, marker, sort_key, sort_dir)

    def sensor_analog_get_all(self, hostid=None, sensorgroupid=None):
        return self._sensor_analog_get_all(hostid, sensorgroupid)

    def sensor_analog_get_by_host(self, host,
                                  limit=None, marker=None,
                                  sort_key=None, sort_dir=None):
        return self._sensor_analog_get_by_host(host, limit, marker,
                                               sort_key, sort_dir)

    def sensor_analog_get_by_sensorgroup(self, sensorgroup,
                                         limit=None, marker=None,
                                         sort_key=None, sort_dir=None):
        return self._sensor_analog_get_by_sensorgroup(sensorgroup,
                                                      limit, marker,
                                                      sort_key, sort_dir)

    def sensor_analog_get_by_host_sensorgroup(self, host, sensorgroup,
                                              limit=None, marker=None,
                                              sort_key=None, sort_dir=None):
        return self._sensor_analog_get_by_host_sensorgroup(host, sensorgroup,
                                                           limit, marker,
                                                           sort_key, sort_dir)

    def sensor_analog_update(self, sensorid, values, hostid=None):
        return self._sensor_analog_update(sensorid, values, hostid)

    def sensor_analog_destroy(self, sensorid):
        return self._sensor_analog_destroy(sensorid)

    def _sensor_discrete_create(self, hostid, values):
        if utils.is_int_like(hostid):
            host = self.host_get(int(hostid))
        elif utils.is_uuid_like(hostid):
            host = self.host_get(hostid.strip())
        elif isinstance(hostid, models.Hosts):
            host = hostid
        else:
            raise exception.HostNotFound(host=hostid)

        values['host_id'] = host['id']

        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()

        sensor_discrete = models.SensorsDiscrete()
        sensor_discrete.update(values)
        with _session_for_write() as session:
            try:
                session.add(sensor_discrete)
                session.flush()
            except db_exc.DBDuplicateEntry:
                raise exception.SensorAlreadyExists(uuid=values['uuid'])
            return self._sensor_discrete_get(values['uuid'])

    def _sensor_discrete_get(self, sensorid, hostid=None):
        query = model_query(models.SensorsDiscrete)

        if hostid:
            query = query.filter_by(host_id=hostid)

        query = add_sensor_filter(query, sensorid)

        try:
            result = query.one()
        except NoResultFound:
            raise exception.SensorNotFound(sensor=sensorid)

        return result

    def _sensor_discrete_get_list(self, limit=None, marker=None,
                                  sort_key=None, sort_dir=None):
        return _paginate_query(models.SensorsDiscrete, limit, marker,
                               sort_key, sort_dir)

    def _sensor_discrete_get_all(self, hostid=None, sensorgroupid=None):
        query = model_query(models.SensorsDiscrete, read_deleted="no")
        if hostid:
            query = query.filter_by(host_id=hostid)
        if sensorgroupid:
            query = query.filter_by(sensorgroup_id=hostid)
        return query.all()

    def _sensor_discrete_get_by_host(self, host,
                                     limit=None, marker=None,
                                     sort_key=None, sort_dir=None):
        query = model_query(models.SensorsDiscrete)
        query = add_port_filter_by_host(query, host)
        return _paginate_query(models.SensorsDiscrete, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_discrete_get_by_sensorgroup(self, sensorgroup,
                                            limit=None, marker=None,
                                            sort_key=None, sort_dir=None):

        query = model_query(models.SensorsDiscrete)
        query = add_sensor_filter_by_sensorgroup(query, sensorgroup)
        return _paginate_query(models.SensorsDiscrete, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_discrete_get_by_host_sensorgroup(self, host, sensorgroup,
                                                 limit=None, marker=None,
                                                 sort_key=None, sort_dir=None):
        query = model_query(models.SensorsDiscrete)
        query = add_sensor_filter_by_host_sensorgroup(query,
                                                      host,
                                                      sensorgroup)
        return _paginate_query(models.SensorsDiscrete, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_discrete_update(self, sensorid, values, hostid=None):
        with _session_for_write():
            # May need to reserve in multi controller system; ref sysinv
            query = model_query(models.SensorsDiscrete, read_deleted="no")

            if hostid:
                query = query.filter_by(host_id=hostid)

            try:
                query = add_sensor_filter(query, sensorid)
                result = query.one()
                for k, v in values.items():
                    setattr(result, k, v)
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for port %s" % sensorid)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for port %s" % sensorid)

            return query.one()

    def _sensor_discrete_destroy(self, sensorid):
        with _session_for_write():
            # Delete port which should cascade to delete SensorsDiscrete
            if uuidutils.is_uuid_like(sensorid):
                model_query(models.Sensors, read_deleted="no").\
                    filter_by(uuid=sensorid).\
                    delete()
            else:
                model_query(models.Sensors, read_deleted="no").\
                    filter_by(id=sensorid).\
                    delete()

    def sensor_discrete_create(self, hostid, values):
        return self._sensor_discrete_create(hostid, values)

    def sensor_discrete_get(self, sensorid, hostid=None):
        return self._sensor_discrete_get(sensorid, hostid)

    def sensor_discrete_get_list(self, limit=None, marker=None,
                                 sort_key=None, sort_dir=None):
        return self._sensor_discrete_get_list(
            limit, marker, sort_key, sort_dir)

    def sensor_discrete_get_all(self, hostid=None, sensorgroupid=None):
        return self._sensor_discrete_get_all(hostid, sensorgroupid)

    def sensor_discrete_get_by_host(self, host,
                                    limit=None, marker=None,
                                    sort_key=None, sort_dir=None):
        return self._sensor_discrete_get_by_host(host, limit, marker,
                                                 sort_key, sort_dir)

    def sensor_discrete_get_by_sensorgroup(self, sensorgroup,
                                           limit=None, marker=None,
                                           sort_key=None, sort_dir=None):
        return self._sensor_discrete_get_by_sensorgroup(
            sensorgroup, limit, marker, sort_key, sort_dir)

    def sensor_discrete_get_by_host_sensorgroup(self, host, sensorgroup,
                                                limit=None, marker=None,
                                                sort_key=None, sort_dir=None):
        return self._sensor_discrete_get_by_host_sensorgroup(
            host, sensorgroup, limit, marker, sort_key, sort_dir)

    def sensor_discrete_update(self, sensorid, values, hostid=None):
        return self._sensor_discrete_update(sensorid, values, hostid)

    def sensor_discrete_destroy(self, sensorid):
        return self._sensor_discrete_destroy(sensorid)

    def _sensor_get(self, cls, sensor_id, ihost=None, obj=None):
        session = None
        if obj:
            session = inspect(obj).session
        query = model_query(cls, session=session)
        query = add_sensor_filter(query, sensor_id)
        if ihost:
            query = add_sensor_filter_by_host(query, ihost)

        try:
            result = query.one()
        except NoResultFound:
            raise exception.InvalidParameterValue(
                err="No entry found for sensor %s" % sensor_id)
        except MultipleResultsFound:
            raise exception.InvalidParameterValue(
                err="Multiple entries found for sensor %s" % sensor_id)

        return result

    def _sensor_create(self, obj, host_id, values):
        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()
        values['host_id'] = int(host_id)

        if 'sensor_profile' in values:
            values.pop('sensor_profile')

        # The id is null for ae sensors with more than one member
        # sensor
        temp_id = obj.id
        obj.update(values)
        if obj.id is None:
            obj.id = temp_id

        with _session_for_write() as session:
            try:
                session.add(obj)
                session.flush()
            except db_exc.DBDuplicateEntry:
                LOG.error("Failed to add sensor %s (uuid: %s), an sensor "
                          "with name %s already exists on host %s" %
                          (values['sensorname'],
                           values['uuid'],
                           values['sensorname'],
                           values['host_id']))
                raise exception.SensorAlreadyExists(uuid=values['uuid'])
            return self._sensor_get(type(obj), values['uuid'])

    def sensor_create(self, hostid, values):
        if values['datatype'] == 'discrete':
            sensor = models.SensorsDiscrete()
        elif values['datatype'] == 'analog':
            sensor = models.SensorsAnalog()
        else:
            sensor = models.SensorsAnalog()
            LOG.error("default SensorsAnalog due to datatype=%s" %
                      values['datatype'])

        return self._sensor_create(sensor, hostid, values)

    def sensor_get(self, sensorid, hostid=None):
        return self._sensor_get(models.Sensors, sensorid, hostid)

    def sensor_get_list(self, limit=None, marker=None,
                        sort_key=None, sort_dir=None):
        model_query(models.Sensors)
        return _paginate_query(models.Sensors, limit, marker,
                               sort_key, sort_dir)

    def sensor_get_all(self, host_id=None, sensorgroupid=None):
        query = model_query(models.Sensors, read_deleted="no")

        if host_id:
            query = query.filter_by(host_id=host_id)
        if sensorgroupid:
            query = query.filter_by(sensorgroup_id=sensorgroupid)
        return query.all()

    def sensor_get_by_host(self, ihost,
                           limit=None, marker=None,
                           sort_key=None, sort_dir=None):
        query = model_query(models.Sensors)
        query = add_sensor_filter_by_host(query, ihost)
        return _paginate_query(models.Sensors, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_get_by_sensorgroup(self, cls, sensorgroup,
                                   limit=None, marker=None,
                                   sort_key=None, sort_dir=None):
        query = model_query(cls)
        query = add_sensor_filter_by_sensorgroup(query, sensorgroup)
        return _paginate_query(cls, limit, marker, sort_key, sort_dir, query)

    def sensor_get_by_sensorgroup(self, sensorgroup,
                                  limit=None, marker=None,
                                  sort_key=None, sort_dir=None):
        query = model_query(models.Sensors)
        query = add_sensor_filter_by_sensorgroup(query, sensorgroup)
        return _paginate_query(models.Sensors, limit, marker,
                               sort_key, sort_dir, query)

    def sensor_get_by_host_sensorgroup(self, ihost, sensorgroup,
                                       limit=None, marker=None,
                                       sort_key=None, sort_dir=None):
        query = model_query(models.Sensors)
        query = add_sensor_filter_by_host(query, ihost)
        query = add_sensor_filter_by_sensorgroup(query, sensorgroup)
        return _paginate_query(models.Sensors, limit, marker,
                               sort_key, sort_dir, query)

    def _sensor_update(self, cls, sensor_id, values):
        with _session_for_write():
            query = model_query(models.Sensors)
            query = add_sensor_filter(query, sensor_id)
            try:
                result = query.one()
                # obj = self._sensor_get(models.Sensors, sensor_id)
                for k, v in values.items():
                    if v == 'none':
                        v = None
                    setattr(result, k, v)
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for sensor %s" % sensor_id)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for sensor %s" % sensor_id)

            return query.one()

    def sensor_update(self, sensor_id, values):
        with _session_for_write():
            query = model_query(models.Sensors, read_deleted="no")
            query = add_sensor_filter(query, sensor_id)
            try:
                result = query.one()
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for sensor %s" % sensor_id)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for sensor %s" % sensor_id)

            if result.datatype == 'discrete':
                return self._sensor_update(models.SensorsDiscrete,
                                           sensor_id, values)
            elif result.datatype == 'analog':
                return self._sensor_update(models.SensorsAnalog,
                                           sensor_id, values)
            else:
                return self._sensor_update(models.SensorsAnalog,
                                           sensor_id, values)

    def _sensor_destroy(self, cls, sensor_id):
        with _session_for_write():
            # Delete sensor which should cascade to delete derived sensors
            if uuidutils.is_uuid_like(sensor_id):
                model_query(cls, read_deleted="no").\
                    filter_by(uuid=sensor_id).\
                    delete()
            else:
                model_query(cls, read_deleted="no").\
                    filter_by(id=sensor_id).\
                    delete()

    def sensor_destroy(self, sensor_id):
        return self._sensor_destroy(models.Sensors, sensor_id)

    # SENSOR GROUPS
    def sensorgroup_create(self, host_id, values):
        if values['datatype'] == 'discrete':
            sensorgroup = models.SensorGroupsDiscrete()
        elif values['datatype'] == 'analog':
            sensorgroup = models.SensorGroupsAnalog()
        else:
            LOG.error("default SensorsAnalog due to datatype=%s" %
                      values['datatype'])

            sensorgroup = models.SensorGroupsAnalog
        return self._sensorgroup_create(sensorgroup, host_id, values)

    def _sensorgroup_get(self, cls, sensorgroup_id, ihost=None, obj=None):
        query = model_query(cls)
        query = add_sensorgroup_filter(query, sensorgroup_id)
        if ihost:
            query = add_sensorgroup_filter_by_host(query, ihost)

        try:
            result = query.one()
        except NoResultFound:
            raise exception.InvalidParameterValue(
                err="No entry found for sensorgroup %s" % sensorgroup_id)
        except MultipleResultsFound:
            raise exception.InvalidParameterValue(
                err="Multiple entries found for sensorgroup %s" %
                sensorgroup_id)

        return result

    def sensorgroup_get(self, sensorgroup_id, ihost=None):
        return self._sensorgroup_get(models.SensorGroups,
                                     sensorgroup_id,
                                     ihost)

    def sensorgroup_get_list(self, limit=None, marker=None,
                             sort_key=None, sort_dir=None):
        query = model_query(models.SensorGroups)
        return _paginate_query(models.SensorGroupsAnalog, limit, marker,
                               sort_key, sort_dir, query)

    def sensorgroup_get_by_host_sensor(self, ihost, sensor,
                                       limit=None, marker=None,
                                       sort_key=None, sort_dir=None):
        query = model_query(models.SensorGroups)
        query = add_sensorgroup_filter_by_host(query, ihost)
        query = add_sensorgroup_filter_by_sensor(query, sensor)
        try:
            result = query.one()
        except NoResultFound:
            raise exception.InvalidParameterValue(
                err="No entry found for host %s port %s" % (ihost, sensor))
        except MultipleResultsFound:
            raise exception.InvalidParameterValue(
                err="Multiple entries found for host %s port %s" %
                    (ihost, sensor))

        return result

    def sensorgroup_get_by_host(self, ihost,
                                limit=None, marker=None,
                                sort_key=None, sort_dir=None):
        query = model_query(models.SensorGroups)
        query = add_sensorgroup_filter_by_host(query, ihost)
        return _paginate_query(models.SensorGroups, limit, marker,
                               sort_key, sort_dir, query)

    def sensorgroup_update(self, sensorgroup_id, values):
        with _session_for_write():
            query = model_query(models.SensorGroups, read_deleted="no")
            query = add_sensorgroup_filter(query, sensorgroup_id)
            try:
                result = query.one()
            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for sensorgroup %s" % sensorgroup_id)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for sensorgroup %s" %
                        sensorgroup_id)

            if result.datatype == 'discrete':
                return self._sensorgroup_update(models.SensorGroupsDiscrete,
                                                sensorgroup_id,
                                                values)
            elif result.datatype == 'analog':
                return self._sensorgroup_update(models.SensorGroupsAnalog,
                                                sensorgroup_id,
                                                values)
            else:
                return self._sensorgroup_update(models.SensorGroupsAnalog,
                                                sensorgroup_id,
                                                values)

    def sensorgroup_propagate(self, sensorgroup_id, values):
        query = model_query(models.SensorGroups, read_deleted="no")
        query = add_sensorgroup_filter(query, sensorgroup_id)
        try:
            result = query.one()
        except NoResultFound:
            raise exception.InvalidParameterValue(
                err="No entry found for sensorgroup %s" % sensorgroup_id)
        except MultipleResultsFound:
            raise exception.InvalidParameterValue(
                err="Multiple entries found for sensorgroup %s" %
                    sensorgroup_id)

        sensors = self._sensor_get_by_sensorgroup(models.Sensors,
                                                  result.uuid)
        for sensor in sensors:
            LOG.info("sensorgroup update propagate sensor=%s val=%s" %
                     (sensor.sensorname, values))
            self._sensor_update(models.Sensors, sensor.uuid, values)

    def _sensorgroup_create(self, obj, host_id, values):
        if not values.get('uuid'):
            values['uuid'] = uuidutils.generate_uuid()
        values['host_id'] = int(host_id)

        if 'sensorgroup_profile' in values:
            values.pop('sensorgroup_profile')

        temp_id = obj.id
        obj.update(values)
        if obj.id is None:
            obj.id = temp_id
        with _session_for_write() as session:
            try:
                session.add(obj)
                session.flush()
            except db_exc.DBDuplicateEntry:
                LOG.error("Failed to add sensorgroup %s (uuid: %s), a "
                          "sensorgroup with name %s already exists on host %s"
                          % (values['sensorgroupname'],
                             values['uuid'],
                             values['sensorgroupname'],
                             values['host_id']))
                raise exception.SensorGroupAlreadyExists(uuid=values['uuid'])
            return self._sensorgroup_get(type(obj), values['uuid'])

    def _sensorgroup_get_all(self, cls, host_id=None):
        query = model_query(cls, read_deleted="no")
        if utils.is_int_like(host_id):
            query = query.filter_by(host_id=host_id)
        return query.all()

    def _sensorgroup_get_list(self, cls, limit=None, marker=None,
                              sort_key=None, sort_dir=None):
        return _paginate_query(cls, limit, marker, sort_key, sort_dir)

    def _sensorgroup_get_by_host_sensor(self, cls, ihost, sensor,
                                        limit=None, marker=None,
                                        sort_key=None, sort_dir=None):

        query = model_query(cls).join(models.Sensors)
        query = add_sensorgroup_filter_by_host(query, ihost)
        query = add_sensorgroup_filter_by_sensor(query, sensor)
        return _paginate_query(cls, limit, marker, sort_key, sort_dir, query)

    def _sensorgroup_get_by_host(self, cls, ihost,
                                 limit=None, marker=None,
                                 sort_key=None, sort_dir=None):

        query = model_query(cls)
        query = add_sensorgroup_filter_by_host(query, ihost)
        return _paginate_query(cls, limit, marker, sort_key, sort_dir, query)

    def _sensorgroup_update(self, cls, sensorgroup_id, values):
        with _session_for_write() as session:
            # query = model_query(models.SensorGroups, read_deleted="no")
            query = model_query(cls, read_deleted="no")
            try:
                query = add_sensorgroup_filter(query, sensorgroup_id)
                result = query.one()

                # obj = self._sensorgroup_get(models.SensorGroups,
                obj = self._sensorgroup_get(cls, sensorgroup_id)

                for k, v in values.items():
                    if k == 'algorithm' and v == 'none':
                        v = None
                    if k == 'actions_critical_choices' and v == 'none':
                        v = None
                    if k == 'actions_major_choices' and v == 'none':
                        v = None
                    if k == 'actions_minor_choices' and v == 'none':
                        v = None
                    setattr(result, k, v)

            except NoResultFound:
                raise exception.InvalidParameterValue(
                    err="No entry found for sensorgroup %s" % sensorgroup_id)
            except MultipleResultsFound:
                raise exception.InvalidParameterValue(
                    err="Multiple entries found for sensorgroup %s" %
                    sensorgroup_id)
            try:
                session.add(obj)
                session.flush()
            except db_exc.DBDuplicateEntry:
                raise exception.SensorGroupAlreadyExists(uuid=values['uuid'])
            return query.one()

    def _sensorgroup_destroy(self, cls, sensorgroup_id):
        with _session_for_write():
            # Delete sensorgroup which should cascade to
            # delete derived sensorgroups
            if uuidutils.is_uuid_like(sensorgroup_id):
                model_query(cls, read_deleted="no").\
                    filter_by(uuid=sensorgroup_id).\
                    delete()
            else:
                model_query(cls, read_deleted="no").\
                    filter_by(id=sensorgroup_id).\
                    delete()

    def sensorgroup_destroy(self, sensorgroup_id):
        return self._sensorgroup_destroy(models.SensorGroups, sensorgroup_id)

    def sensorgroup_analog_create(self, host_id, values):
        sensorgroup = models.SensorGroupsAnalog()
        return self._sensorgroup_create(sensorgroup, host_id, values)

    def sensorgroup_analog_get_all(self, host_id=None):
        return self._sensorgroup_get_all(models.SensorGroupsAnalog, host_id)

    def sensorgroup_analog_get(self, sensorgroup_id):
        return self._sensorgroup_get(models.SensorGroupsAnalog,
                                     sensorgroup_id)

    def sensorgroup_analog_get_list(self, limit=None, marker=None,
                                    sort_key=None, sort_dir=None):
        return self._sensorgroup_get_list(models.SensorGroupsAnalog,
                                          limit, marker,
                                          sort_key, sort_dir)

    def sensorgroup_analog_get_by_host(self, ihost,
                                       limit=None, marker=None,
                                       sort_key=None, sort_dir=None):
        return self._sensorgroup_get_by_host(models.SensorGroupsAnalog,
                                             ihost,
                                             limit, marker,
                                             sort_key, sort_dir)

    def sensorgroup_analog_update(self, sensorgroup_id, values):
        return self._sensorgroup_update(models.SensorGroupsAnalog,
                                        sensorgroup_id,
                                        values)

    def sensorgroup_analog_destroy(self, sensorgroup_id):
        return self._sensorgroup_destroy(models.SensorGroupsAnalog,
                                         sensorgroup_id)

    def sensorgroup_discrete_create(self, host_id, values):
        sensorgroup = models.SensorGroupsDiscrete()
        return self._sensorgroup_create(sensorgroup, host_id, values)

    def sensorgroup_discrete_get_all(self, host_id=None):
        return self._sensorgroup_get_all(models.SensorGroupsDiscrete, host_id)

    def sensorgroup_discrete_get(self, sensorgroup_id):
        return self._sensorgroup_get(models.SensorGroupsDiscrete,
                                     sensorgroup_id)

    def sensorgroup_discrete_get_list(self, limit=None, marker=None,
                                      sort_key=None, sort_dir=None):
        return self._sensorgroup_get_list(models.SensorGroupsDiscrete,
                                          limit, marker,
                                          sort_key, sort_dir)

    def sensorgroup_discrete_get_by_host(self, ihost,
                                         limit=None, marker=None,
                                         sort_key=None, sort_dir=None):
        return self._sensorgroup_get_by_host(models.SensorGroupsDiscrete,
                                             ihost,
                                             limit, marker, sort_key, sort_dir)

    def sensorgroup_discrete_update(self, sensorgroup_id, values):
        return self._sensorgroup_update(models.SensorGroupsDiscrete,
                                        sensorgroup_id, values)

    def sensorgroup_discrete_destroy(self, sensorgroup_id):
        return self._sensorgroup_destroy(models.SensorGroupsDiscrete,
                                         sensorgroup_id)
