#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
import os
import subprocess

from controllerconfig import backup_restore
from fm_api import fm_api

from inventory.common import ceph
from inventory.common import constants
from inventory.common.fm import fmclient
from inventory.common.i18n import _
from inventory.common import k_host
from inventory.common import patch_api
from inventory.common import vim_api
from oslo_log import log

import cgcs_patch.constants as patch_constants

LOG = log.getLogger(__name__)


class Health(object):

    SUCCESS_MSG = _('OK')
    FAIL_MSG = _('Fail')

    def __init__(self, context, dbapi):
        self._context = context
        self._dbapi = dbapi
        self._ceph = ceph.CephApiOperator()

    def _check_hosts_provisioned(self, hosts):
        """Checks that each host is provisioned"""
        provisioned_hosts = []
        unprovisioned_hosts = 0
        for host in hosts:
            if host['invprovision'] != k_host.PROVISIONED or \
                    host['hostname'] is None:
                unprovisioned_hosts = unprovisioned_hosts + 1
            else:
                provisioned_hosts.append(host)

        return unprovisioned_hosts, provisioned_hosts

    def _check_hosts_enabled(self, hosts):
        """Checks that each host is enabled and unlocked"""
        offline_host_list = []
        for host in hosts:
            if host['administrative'] != k_host.ADMIN_UNLOCKED or \
                    host['operational'] != k_host.OPERATIONAL_ENABLED:
                offline_host_list.append(host.hostname)

        success = not offline_host_list
        return success, offline_host_list

    def _check_hosts_config(self, hosts):
        """Checks that the applied and target config match for each host"""
        config_host_list = []
        for host in hosts:
            if (host.config_target and
                    host.config_applied != host.config_target):
                config_host_list.append(host.hostname)

        success = not config_host_list
        return success, config_host_list

    def _check_patch_current(self, hosts):
        """Checks that each host is patch current"""
        system = self._dbapi.isystem_get_one()
        response = patch_api.patch_query_hosts(context=self._context,
                                               region_name=system.region_name)
        patch_hosts = response['data']
        not_patch_current_hosts = []
        hostnames = []
        for host in hosts:
            hostnames.append(host['hostname'])

        for host in patch_hosts:
            # There may be instances where the patching db returns
            # hosts that have been recently deleted. We will continue if a host
            # is the patching db but not inventory
            try:
                hostnames.remove(host['hostname'])
            except ValueError:
                LOG.info('Host %s found in patching but not in inventory. '
                         'Continuing' % host['hostname'])
            else:
                if not host['patch_current']:
                    not_patch_current_hosts.append(host['hostname'])

        success = not not_patch_current_hosts and not hostnames
        return success, not_patch_current_hosts, hostnames

    def _check_alarms(self, context, force=False):
        """Checks that no alarms are active"""
        db_alarms = fmclient(context).alarm.list(include_suppress=True)

        success = True
        allowed = 0
        affecting = 0
        # Only fail if we find alarms past their affecting threshold
        for db_alarm in db_alarms:
            if isinstance(db_alarm, tuple):
                alarm = db_alarm[0]
                mgmt_affecting = db_alarm[constants.DB_MGMT_AFFECTING]
            else:
                alarm = db_alarm
                mgmt_affecting = db_alarm.mgmt_affecting
            if fm_api.FaultAPIs.alarm_allowed(alarm.severity, mgmt_affecting):
                allowed += 1
                if not force:
                    success = False
            else:
                affecting += 1
                success = False

        return success, allowed, affecting

    def get_alarms_degrade(self, context,
                           alarm_ignore_list=[], entity_instance_id_filter=""):
        """Return all the alarms that cause the degrade"""
        db_alarms = fmclient(context).alarm.list(include_suppress=True)
        degrade_alarms = []

        for db_alarm in db_alarms:
            if isinstance(db_alarm, tuple):
                alarm = db_alarm[0]
                degrade_affecting = db_alarm[constants.DB_DEGRADE_AFFECTING]
            else:
                alarm = db_alarm
                degrade_affecting = db_alarm.degrade_affecting
            # Ignore alarms that are part of the ignore list sent as parameter
            # and also filter the alarms bases on entity instance id.
            # If multiple alarms with the same ID exist, we only return the ID
            # one time.
            if not fm_api.FaultAPIs.alarm_allowed(
                    alarm.severity, degrade_affecting):
                if (entity_instance_id_filter in alarm.entity_instance_id and
                        alarm.alarm_id not in alarm_ignore_list and
                        alarm.alarm_id not in degrade_alarms):
                    degrade_alarms.append(alarm.alarm_id)
        return degrade_alarms

    def _check_ceph(self):
        """Checks the ceph health status"""
        return self._ceph.ceph_status_ok()

    def _check_license(self, version):
        """Validates the current license is valid for the specified version"""
        check_binary = "/usr/bin/sm-license-check"
        license_file = '/etc/platform/.license'
        system = self._dbapi.isystem_get_one()
        system_type = system.system_type
        system_mode = system.system_mode

        with open(os.devnull, "w") as fnull:
            try:
                subprocess.check_call([check_binary, license_file, version,
                                       system_type, system_mode],
                                      stdout=fnull, stderr=fnull)
            except subprocess.CalledProcessError:
                return False

        return True

    def _check_required_patches(self, patch_list):
        """Validates that each patch provided is applied on the system"""
        system = self._dbapi.isystem_get_one()
        response = patch_api.patch_query(context=self._context,
                                         region_name=system.region_name,
                                         timeout=60)
        query_patches = response['pd']
        applied_patches = []
        for patch_key in query_patches:
            patch = query_patches[patch_key]
            patchstate = patch.get('patchstate', None)
            if patchstate == patch_constants.APPLIED or \
                    patchstate == patch_constants.COMMITTED:
                applied_patches.append(patch_key)

        missing_patches = []
        for required_patch in patch_list:
            if required_patch not in applied_patches:
                missing_patches.append(required_patch)

        success = not missing_patches
        return success, missing_patches

    def _check_running_instances(self, host):
        """Checks that no instances are running on the host"""

        vim_resp = vim_api.vim_host_get_instances(
            self._context,
            host['uuid'],
            host['hostname'])
        running_instances = vim_resp['instances']

        success = running_instances == 0
        return success, running_instances

    def _check_simplex_available_space(self):
        """Ensures there is free space for the backup"""
        try:
            backup_restore.check_size("/opt/backups", True)
        except backup_restore.BackupFail:
            return False

        return True

    def get_system_health(self, context, force=False):
        """Returns the general health of the system"""
        # Checks the following:
        # All hosts are provisioned
        # All hosts are patch current
        # All hosts are unlocked/enabled
        # All hosts having matching configs
        # No management affecting alarms
        # For ceph systems: The storage cluster is healthy

        hosts = self._dbapi.ihost_get_list()
        output = _('System Health:\n')
        health_ok = True

        unprovisioned_hosts, provisioned_hosts = \
            self._check_hosts_provisioned(hosts)
        success = unprovisioned_hosts == 0
        output += (_('All hosts are provisioned: [%s]\n')
                   % (Health.SUCCESS_MSG if success else Health.FAIL_MSG))
        if not success:
            output += _('%s Unprovisioned hosts\n') % unprovisioned_hosts
            # Set the hosts to the provisioned_hosts. This will allow the other
            # checks to continue
            hosts = provisioned_hosts

        health_ok = health_ok and success

        success, error_hosts = self._check_hosts_enabled(hosts)
        output += _('All hosts are unlocked/enabled: [%s]\n') \
            % (Health.SUCCESS_MSG if success else Health.FAIL_MSG)
        if not success:
            output += _('Locked or disabled hosts: %s\n') \
                % ', '.join(error_hosts)

        health_ok = health_ok and success

        success, error_hosts = self._check_hosts_config(hosts)
        output += _('All hosts have current configurations: [%s]\n') \
            % (Health.SUCCESS_MSG if success else Health.FAIL_MSG)
        if not success:
            output += _('Hosts with out of date configurations: %s\n') \
                % ', '.join(error_hosts)

        health_ok = health_ok and success

        success, error_hosts, missing_hosts = self._check_patch_current(hosts)
        output += _('All hosts are patch current: [%s]\n') \
            % (Health.SUCCESS_MSG if success else Health.FAIL_MSG)
        if not success:
            if error_hosts:
                output += _('Hosts not patch current: %s\n') \
                    % ', '.join(error_hosts)
            if missing_hosts:
                output += _('Hosts without patch data: %s\n') \
                    % ', '.join(missing_hosts)

        health_ok = health_ok and success

        # if StorageBackendConfig.has_backend(
        #         self._dbapi,
        #         constants.CINDER_BACKEND_CEPH):
        #     success = self._check_ceph()
        #     output += _('Ceph Storage Healthy: [%s]\n') \
        #         % (Health.SUCCESS_MSG if success else Health.FAIL_MSG)
        # health_ok = health_ok and success

        success, allowed, affecting = self._check_alarms(context, force)
        output += _('No alarms: [%s]\n') \
            % (Health.SUCCESS_MSG if success else Health.FAIL_MSG)
        if not success:
            output += _('[{}] alarms found, [{}] of which are management '
                        'affecting\n').format(allowed + affecting, affecting)

        health_ok = health_ok and success

        return health_ok, output
