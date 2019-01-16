
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2016, 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# All Rights Reserved.
#

""" Inventory Ceph Utilities and helper functions."""

from __future__ import absolute_import

from cephclient import wrapper as ceph
from inventory.common import constants
from inventory.common import k_host
from oslo_log import log

LOG = log.getLogger(__name__)


class CephApiOperator(object):
    """Class to encapsulate Ceph operations for Inventory API
       Methods on object-based storage devices (OSDs).
    """

    def __init__(self):
        self._ceph_api = ceph.CephWrapper(
            endpoint='https://localhost:5001/')

    def ceph_status_ok(self, timeout=10):
        """
            returns rc bool. True if ceph ok, False otherwise
            :param timeout: ceph api timeout
        """
        rc = True

        try:
            response, body = self._ceph_api.status(body='json',
                                                   timeout=timeout)
            ceph_status = body['output']['health']['overall_status']
            if ceph_status != constants.CEPH_HEALTH_OK:
                LOG.warn("ceph status=%s " % ceph_status)
                rc = False
        except Exception as e:
            rc = False
            LOG.warn("ceph status exception: %s " % e)

        return rc

    def _osd_quorum_names(self, timeout=10):
        quorum_names = []
        try:
            response, body = self._ceph_api.quorum_status(body='json',
                                                          timeout=timeout)
            quorum_names = body['output']['quorum_names']
        except Exception as ex:
            LOG.exception(ex)
            return quorum_names

        return quorum_names

    def remove_osd_key(self, osdid):
        osdid_str = "osd." + str(osdid)
        # Remove the OSD authentication key
        response, body = self._ceph_api.auth_del(
            osdid_str, body='json')
        if not response.ok:
            LOG.error("Auth delete failed for OSD %s: %s",
                      osdid_str, response.reason)

    def osd_host_lookup(self, osd_id):
        response, body = self._ceph_api.osd_crush_tree(body='json')
        for i in range(0, len(body)):
            # there are 2 chassis lists - cache-tier and root-tier
            # that can be seen in the output of 'ceph osd crush tree':
            # [{"id": -2,"name": "cache-tier", "type": "root",
            # "type_id": 10, "items": [...]},
            # {"id": -1,"name": "storage-tier","type": "root",
            # "type_id": 10, "items": [...]}]
            chassis_list = body['output'][i]['items']
            for chassis in chassis_list:
                # extract storage list/per chassis
                storage_list = chassis['items']
                for storage in storage_list:
                    # extract osd list/per storage
                    storage_osd_list = storage['items']
                    for osd in storage_osd_list:
                        if osd['id'] == osd_id:
                            # return storage name where osd is located
                            return storage['name']
        return None

    def check_osds_down_up(self, hostname, upgrade):
        # check if osds from a storage are down/up
        response, body = self._ceph_api.osd_tree(body='json')
        osd_tree = body['output']['nodes']
        size = len(osd_tree)
        for i in range(1, size):
            if osd_tree[i]['type'] != "host":
                continue
            children_list = osd_tree[i]['children']
            children_num = len(children_list)
            # when we do a storage upgrade, storage node must be locked
            # and all the osds of that storage node must be down
            if (osd_tree[i]['name'] == hostname):
                for j in range(1, children_num + 1):
                    if (osd_tree[i + j]['type'] ==
                            constants.STOR_FUNCTION_OSD and
                       osd_tree[i + j]['status'] == "up"):
                        # at least one osd is not down
                        return False
                # all osds are up
                return True

    def host_crush_remove(self, hostname):
        # remove host from crushmap when system host-delete is executed
        response, body = self._ceph_api.osd_crush_remove(
            hostname, body='json')

    def host_osd_status(self, hostname):
        # should prevent locking of a host if HEALTH_BLOCK
        host_health = None
        try:
            response, body = self._ceph_api.pg_dump_stuck(body='json')
            pg_detail = len(body['output'])
        except Exception as e:
            LOG.exception(e)
            return host_health

        # osd_list is a list where I add
        # each osd from pg_detail whose hostname
        # is not equal with hostnamge given as parameter
        osd_list = []
        for x in range(pg_detail):
            # extract the osd and return the storage node
            osd = body['output'][x]['acting']
            # osd is a list with osd where a stuck/degraded PG
            # was replicated. If osd is empty, it means
            # PG is not replicated to any osd
            if not osd:
                continue
            osd_id = int(osd[0])
            if osd_id in osd_list:
                continue
            # potential future optimization to cache all the
            # osd to host lookups for the single call to host_osd_status().
            host_name = self.osd_host_lookup(osd_id)
            if (host_name is not None and
               host_name == hostname):
                # mark the selected storage node with HEALTH_BLOCK
                # we can't lock any storage node marked with HEALTH_BLOCK
                return constants.CEPH_HEALTH_BLOCK
            osd_list.append(osd_id)
        return constants.CEPH_HEALTH_OK

    def get_monitors_status(self, ihosts):
        # first check that the monitors are available in inventory
        num_active_monitors = 0
        num_inv_monitors = 0
        required_monitors = constants.MIN_STOR_MONITORS
        quorum_names = []
        inventory_monitor_names = []
        for ihost in ihosts:
            if ihost['personality'] == k_host.COMPUTE:
                continue
            capabilities = ihost['capabilities']
            if 'stor_function' in capabilities:
                host_action = ihost['host_action'] or ""
                locking = (host_action.startswith(k_host.ACTION_LOCK) or
                           host_action.startswith(k_host.ACTION_FORCE_LOCK))
                if (capabilities['stor_function'] ==
                        constants.STOR_FUNCTION_MONITOR and
                   ihost['administrative'] == k_host.ADMIN_UNLOCKED and
                   ihost['operational'] == k_host.OPERATIONAL_ENABLED and
                   not locking):
                    num_inv_monitors += 1
                    inventory_monitor_names.append(ihost['hostname'])

        LOG.info("Active ceph monitors in inventory = %s" %
                 str(inventory_monitor_names))

        # check that the cluster is actually operational.
        # if we can get the monitor quorum from ceph, then
        # the cluster is truly operational
        if num_inv_monitors >= required_monitors:
            try:
                quorum_names = self._osd_quorum_names()
            except Exception:
                # if the cluster is not responding to requests
                # set quorum_names to an empty list, indicating a problem
                quorum_names = []
                LOG.error("Ceph cluster not responding to requests.")

        LOG.info("Active ceph monitors in ceph cluster = %s" %
                 str(quorum_names))

        # There may be cases where a host is in an unlocked-available state,
        # but the monitor is down due to crashes or manual removal.
        # For such cases, we determine the list of active ceph monitors to be
        # the intersection of the inventory reported unlocked-available monitor
        # hosts and the monitors reported in the quorum via the ceph API.
        active_monitors = list(set(inventory_monitor_names) &
                               set(quorum_names))
        LOG.info("Active ceph monitors = %s" % str(active_monitors))

        num_active_monitors = len(active_monitors)

        return num_active_monitors, required_monitors, active_monitors
