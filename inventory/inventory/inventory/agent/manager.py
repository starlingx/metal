# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


""" Perform activity related to local inventory.

A single instance of :py:class:`inventory.agent.manager.AgentManager` is
created within the *inventory-agent* process, and is responsible for
performing all actions for this host managed by inventory .

On start, collect and post inventory.

Commands (from conductors) are received via RPC calls.

"""

import errno
import fcntl
import os
import oslo_messaging as messaging
import socket
import subprocess
import time

from futurist import periodics
from oslo_config import cfg
from oslo_log import log

# from inventory.agent import partition
from inventory.agent import base_manager
from inventory.agent.lldp import plugin as lldp_plugin
from inventory.agent import node
from inventory.agent import pci
from inventory.common import constants
from inventory.common import context as mycontext
from inventory.common import exception
from inventory.common.i18n import _
from inventory.common import k_host
from inventory.common import k_lldp
from inventory.common import utils
from inventory.conductor import rpcapi as conductor_rpcapi
import tsconfig.tsconfig as tsc

MANAGER_TOPIC = 'inventory.agent_manager'

LOG = log.getLogger(__name__)

agent_opts = [
    cfg.StrOpt('api_url',
               default=None,
               help=('Url of Inventory API service. If not set Inventory can '
                     'get current value from Keystone service catalog.')),
    cfg.IntOpt('audit_interval',
               default=60,
               help='Maximum time since the last check-in of a agent'),
]

CONF = cfg.CONF
CONF.register_opts(agent_opts, 'agent')

MAXSLEEP = 300  # 5 minutes

INVENTORY_READY_FLAG = os.path.join(tsc.VOLATILE_PATH, ".inventory_ready")


FIRST_BOOT_FLAG = os.path.join(
    tsc.PLATFORM_CONF_PATH, ".first_boot")


class AgentManager(base_manager.BaseAgentManager):
    """Inventory Agent service main class."""

    # Must be in sync with rpcapi.AgentAPI's
    RPC_API_VERSION = '1.0'

    target = messaging.Target(version=RPC_API_VERSION)

    def __init__(self, host, topic):
        super(AgentManager, self).__init__(host, topic)

        self._report_to_conductor = False
        self._report_to_conductor_iplatform_avail_flag = False
        self._ipci_operator = pci.PCIOperator()
        self._inode_operator = node.NodeOperator()
        self._lldp_operator = lldp_plugin.InventoryLldpPlugin()
        self._ihost_personality = None
        self._ihost_uuid = ""
        self._agent_throttle = 0
        self._subfunctions = None
        self._subfunctions_configured = False
        self._notify_subfunctions_alarm_clear = False
        self._notify_subfunctions_alarm_raise = False
        self._first_grub_update = False

    @property
    def report_to_conductor_required(self):
        return self._report_to_conductor

    @report_to_conductor_required.setter
    def report_to_conductor_required(self, val):
        if not isinstance(val, bool):
            raise ValueError("report_to_conductor_required not bool %s" %
                             val)
        self._report_to_conductor = val

    def start(self):
        # Do not collect inventory and report to conductor at startup in
        # order to eliminate two inventory reports
        # (one from here and one from audit) being sent to the conductor

        super(AgentManager, self).start()

        if os.path.isfile('/etc/inventory/inventory.conf'):
            LOG.info("inventory-agent started, "
                     "inventory to be reported by audit")
        else:
            LOG.info("No config file for inventory-agent found.")

        if tsc.system_mode == constants.SYSTEM_MODE_SIMPLEX:
            utils.touch(INVENTORY_READY_FLAG)

    def init_host(self, admin_context=None):
        super(AgentManager, self).init_host(admin_context)
        if os.path.isfile('/etc/inventory/inventory.conf'):
            LOG.info(_("inventory-agent started, "
                       "system config to be reported by audit"))
        else:
            LOG.info(_("No config file for inventory-agent found."))

        if tsc.system_mode == constants.SYSTEM_MODE_SIMPLEX:
            utils.touch(INVENTORY_READY_FLAG)

    def del_host(self, deregister=True):
        return

    def periodic_tasks(self, context, raise_on_error=False):
        """Periodic tasks are run at pre-specified intervals. """
        return self.run_periodic_tasks(context,
                                       raise_on_error=raise_on_error)

    def _report_to_conductor_iplatform_avail(self):
        utils.touch(INVENTORY_READY_FLAG)
        time.sleep(1)  # give time for conductor to process
        self._report_to_conductor_iplatform_avail_flag = True

    def _update_ttys_dcd_status(self, context, host_id):
        # Retrieve the serial line carrier detect flag
        ttys_dcd = None
        rpcapi = conductor_rpcapi.ConductorAPI(
            topic=conductor_rpcapi.MANAGER_TOPIC)
        try:
            ttys_dcd = rpcapi.get_host_ttys_dcd(context, host_id)
        except exception.InventoryException:
            LOG.exception("Inventory Agent exception getting host ttys_dcd.")
            pass
        if ttys_dcd is not None:
            self._config_ttys_login(ttys_dcd)
        else:
            LOG.debug("ttys_dcd is not configured")

    @staticmethod
    def _get_active_device():
        # the list of currently configured console devices,
        # like 'tty1 ttyS0' or just 'ttyS0'
        # The last entry in the file is the active device connected
        # to /dev/console.
        active_device = 'ttyS0'
        try:
            cmd = 'cat /sys/class/tty/console/active | grep ttyS'
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
            output = proc.stdout.read().strip()
            proc.communicate()[0]
            if proc.returncode != 0:
                LOG.info("Cannot find the current configured serial device, "
                         "return default %s" % active_device)
                return active_device
            # if more than one devices are found, take the last entry
            if ' ' in output:
                devs = output.split(' ')
                active_device = devs[len(devs) - 1]
            else:
                active_device = output
        except subprocess.CalledProcessError as e:
            LOG.error("Failed to execute (%s) (%d)", cmd, e.returncode)
        except OSError as e:
            LOG.error("Failed to execute (%s) OS error (%d)", cmd, e.errno)

        return active_device

    @staticmethod
    def _is_local_flag_disabled(device):
        """
        :param device:
        :return: boolean: True if the local flag is disabled 'i.e. -clocal is
                          set'. This means the serial data carrier detect
                          signal is significant
        """
        try:
            # uses -o for only-matching and -e for a pattern beginning with a
            # hyphen (-), the following command returns 0 if the local flag
            # is disabled
            cmd = 'stty -a -F /dev/%s | grep -o -e -clocal' % device
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
            proc.communicate()[0]
            return proc.returncode == 0
        except subprocess.CalledProcessError as e:
            LOG.error("Failed to execute (%s) (%d)", cmd, e.returncode)
            return False
        except OSError as e:
            LOG.error("Failed to execute (%s) OS error (%d)", cmd, e.errno)
            return False

    def _config_ttys_login(self, ttys_dcd):
        # agetty is now enabled by systemd
        # we only need to disable the local flag to enable carrier detection
        # and enable the local flag when the feature is turned off
        toggle_flag = None
        active_device = self._get_active_device()
        local_flag_disabled = self._is_local_flag_disabled(active_device)
        if str(ttys_dcd) in ['True', 'true']:
            LOG.info("ttys_dcd is enabled")
            # check if the local flag is disabled
            if not local_flag_disabled:
                LOG.info("Disable (%s) local line" % active_device)
                toggle_flag = 'stty -clocal -F /dev/%s' % active_device
        else:
            if local_flag_disabled:
                # enable local flag to ignore the carrier detection
                LOG.info("Enable local flag for device :%s" % active_device)
                toggle_flag = 'stty clocal -F /dev/%s' % active_device

        if toggle_flag:
            try:
                subprocess.Popen(toggle_flag, stdout=subprocess.PIPE,
                                 shell=True)
                # restart serial-getty
                restart_cmd = ('systemctl restart serial-getty@%s.service'
                               % active_device)
                subprocess.check_call(restart_cmd, shell=True)
            except subprocess.CalledProcessError as e:
                LOG.error("subprocess error: (%d)", e.returncode)

    def _force_grub_update(self):
        """Force update the grub on the first AIO controller after the initial
           config is completed
        """
        if (not self._first_grub_update and
                os.path.isfile(tsc.INITIAL_CONFIG_COMPLETE_FLAG)):
            self._first_grub_update = True
            return True
        return False

    def host_lldp_get_and_report(self, context, rpcapi, host_uuid):
        neighbour_dict_array = []
        agent_dict_array = []
        neighbours = []
        agents = []

        try:
            neighbours = self._lldp_operator.lldp_neighbours_list()
        except Exception as e:
            LOG.error("Failed to get LLDP neighbours: %s", str(e))

        for neighbour in neighbours:
            neighbour_dict = {
                'name_or_uuid': neighbour.key.portname,
                'msap': neighbour.msap,
                'state': neighbour.state,
                k_lldp.LLDP_TLV_TYPE_CHASSIS_ID: neighbour.key.chassisid,
                k_lldp.LLDP_TLV_TYPE_PORT_ID: neighbour.key.portid,
                k_lldp.LLDP_TLV_TYPE_TTL: neighbour.ttl,
                k_lldp.LLDP_TLV_TYPE_SYSTEM_NAME: neighbour.system_name,
                k_lldp.LLDP_TLV_TYPE_SYSTEM_DESC: neighbour.system_desc,
                k_lldp.LLDP_TLV_TYPE_SYSTEM_CAP: neighbour.capabilities,
                k_lldp.LLDP_TLV_TYPE_MGMT_ADDR: neighbour.mgmt_addr,
                k_lldp.LLDP_TLV_TYPE_PORT_DESC: neighbour.port_desc,
                k_lldp.LLDP_TLV_TYPE_DOT1_LAG: neighbour.dot1_lag,
                k_lldp.LLDP_TLV_TYPE_DOT1_PORT_VID: neighbour.dot1_port_vid,
                k_lldp.LLDP_TLV_TYPE_DOT1_VID_DIGEST:
                    neighbour.dot1_vid_digest,
                k_lldp.LLDP_TLV_TYPE_DOT1_MGMT_VID: neighbour.dot1_mgmt_vid,
                k_lldp.LLDP_TLV_TYPE_DOT1_PROTO_VIDS:
                    neighbour.dot1_proto_vids,
                k_lldp.LLDP_TLV_TYPE_DOT1_PROTO_IDS:
                    neighbour.dot1_proto_ids,
                k_lldp.LLDP_TLV_TYPE_DOT1_VLAN_NAMES:
                    neighbour.dot1_vlan_names,
                k_lldp.LLDP_TLV_TYPE_DOT3_MAC_STATUS:
                    neighbour.dot3_mac_status,
                k_lldp.LLDP_TLV_TYPE_DOT3_MAX_FRAME:
                    neighbour.dot3_max_frame,
                k_lldp.LLDP_TLV_TYPE_DOT3_POWER_MDI:
                    neighbour.dot3_power_mdi,
            }
            neighbour_dict_array.append(neighbour_dict)

        if neighbour_dict_array:
            try:
                rpcapi.lldp_neighbour_update_by_host(context,
                                                     host_uuid,
                                                     neighbour_dict_array)
            except exception.InventoryException:
                LOG.exception("Inventory Agent exception updating "
                              "lldp neighbours.")
                self._lldp_operator.lldp_neighbours_clear()
                pass

        try:
            agents = self._lldp_operator.lldp_agents_list()
        except Exception as e:
            LOG.error("Failed to get LLDP agents: %s", str(e))

        for agent in agents:
            agent_dict = {
                'name_or_uuid': agent.key.portname,
                'state': agent.state,
                'status': agent.status,
                k_lldp.LLDP_TLV_TYPE_CHASSIS_ID: agent.key.chassisid,
                k_lldp.LLDP_TLV_TYPE_PORT_ID: agent.key.portid,
                k_lldp.LLDP_TLV_TYPE_TTL: agent.ttl,
                k_lldp.LLDP_TLV_TYPE_SYSTEM_NAME: agent.system_name,
                k_lldp.LLDP_TLV_TYPE_SYSTEM_DESC: agent.system_desc,
                k_lldp.LLDP_TLV_TYPE_SYSTEM_CAP: agent.capabilities,
                k_lldp.LLDP_TLV_TYPE_MGMT_ADDR: agent.mgmt_addr,
                k_lldp.LLDP_TLV_TYPE_PORT_DESC: agent.port_desc,
                k_lldp.LLDP_TLV_TYPE_DOT1_LAG: agent.dot1_lag,
                k_lldp.LLDP_TLV_TYPE_DOT1_VLAN_NAMES: agent.dot1_vlan_names,
                k_lldp.LLDP_TLV_TYPE_DOT3_MAX_FRAME: agent.dot3_max_frame,
            }
            agent_dict_array.append(agent_dict)

        if agent_dict_array:
            try:
                rpcapi.lldp_agent_update_by_host(context,
                                                 host_uuid,
                                                 agent_dict_array)
            except exception.InventoryException:
                LOG.exception("Inventory Agent exception updating "
                              "lldp agents.")
                self._lldp_operator.lldp_agents_clear()
                pass

    def synchronized_network_config(func):
        """Synchronization decorator to acquire and release
           network_config_lock.
        """

        def wrap(self, *args, **kwargs):
            try:
                # Get lock to avoid conflict with apply_network_config.sh
                lockfd = self._acquire_network_config_lock()
                return func(self, *args, **kwargs)
            finally:
                self._release_network_config_lock(lockfd)

        return wrap

    @synchronized_network_config
    def _lldp_enable_and_report(self, context, rpcapi, host_uuid):
        """Temporarily enable interfaces and get lldp neighbor information.
           This method should only be called before
           INITIAL_CONFIG_COMPLETE_FLAG is set.
        """
        links_down = []
        try:
            # Turn on interfaces, so that lldpd can show all neighbors
            for interface in self._ipci_operator.pci_get_net_names():
                flag = self._ipci_operator.pci_get_net_flags(interface)
                # If administrative state is down, bring it up momentarily
                if not (flag & pci.IFF_UP):
                    subprocess.call(['ip', 'link', 'set', interface, 'up'])
                    links_down.append(interface)
                    LOG.info('interface %s enabled to receive LLDP PDUs' %
                             interface)
            self._lldp_operator.lldp_update()

            # delay maximum 30 seconds for lldpd to receive LLDP PDU
            timeout = 0
            link_wait_for_lldp = True
            while timeout < 30 and link_wait_for_lldp and links_down:
                time.sleep(5)
                timeout = timeout + 5
                link_wait_for_lldp = False

                for link in links_down:
                    if not self._lldp_operator.lldp_has_neighbour(link):
                        link_wait_for_lldp = True
                        break
            self.host_lldp_get_and_report(context, rpcapi, host_uuid)
        except Exception as e:
            LOG.exception(e)
            pass
        finally:
            # restore interface administrative state
            for interface in links_down:
                subprocess.call(['ip', 'link', 'set', interface, 'down'])
                LOG.info('interface %s disabled after querying LLDP neighbors'
                         % interface)

    def platform_update_by_host(self, rpcapi, context, host_uuid, msg_dict):
        """Update host platform information.
           If this is the first boot (kickstart), then also update the Host
           Action State to reinstalled, and remove the flag.
        """
        if os.path.exists(FIRST_BOOT_FLAG):
            msg_dict.update({k_host.HOST_ACTION_STATE:
                             k_host.HAS_REINSTALLED})

        try:
            rpcapi.platform_update_by_host(context,
                                           host_uuid,
                                           msg_dict)
            if os.path.exists(FIRST_BOOT_FLAG):
                os.remove(FIRST_BOOT_FLAG)
                LOG.info("Removed %s" % FIRST_BOOT_FLAG)
        except exception.InventoryException:
            LOG.warn("platform_update_by_host exception "
                     "host_uuid=%s msg_dict=%s." %
                     (host_uuid, msg_dict))
            pass

        LOG.info("Inventory Agent platform update by host: %s" % msg_dict)

    def _acquire_network_config_lock(self):
        """Synchronization with apply_network_config.sh

        This method is to acquire the lock to avoid
        conflict with execution of apply_network_config.sh
        during puppet manifest application.

        :returns: fd of the lock, if successful. 0 on error.
        """
        lock_file_fd = os.open(
            constants.NETWORK_CONFIG_LOCK_FILE, os.O_CREAT | os.O_RDONLY)
        count = 1
        delay = 5
        max_count = 5
        while count <= max_count:
            try:
                fcntl.flock(lock_file_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return lock_file_fd
            except IOError as e:
                # raise on unrelated IOErrors
                if e.errno != errno.EAGAIN:
                    raise
                else:
                    LOG.info("Could not acquire lock({}): {} ({}/{}), "
                             "will retry".format(lock_file_fd, str(e),
                                                 count, max_count))
                    time.sleep(delay)
                    count += 1
        LOG.error("Failed to acquire lock (fd={})".format(lock_file_fd))
        return 0

    def _release_network_config_lock(self, lockfd):
        """Release the lock guarding apply_network_config.sh """
        if lockfd:
            fcntl.flock(lockfd, fcntl.LOCK_UN)
            os.close(lockfd)

    def ihost_inv_get_and_report(self, icontext):
        """Collect data for an ihost.

        This method allows an ihost data to be collected.

        :param:   icontext: an admin context
        :returns: updated ihost object, including all fields.
        """

        rpcapi = conductor_rpcapi.ConductorAPI(
            topic=conductor_rpcapi.MANAGER_TOPIC)

        ihost = None

        # find list of network related inics for this ihost
        inics = self._ipci_operator.inics_get()

        # create an array of ports for each net entry of the NIC device
        iports = []
        for inic in inics:
            lockfd = 0
            try:
                # Get lock to avoid conflict with apply_network_config.sh
                lockfd = self._acquire_network_config_lock()
                pci_net_array = \
                    self._ipci_operator.pci_get_net_attrs(inic.pciaddr)
            finally:
                self._release_network_config_lock(lockfd)
            for net in pci_net_array:
                iports.append(pci.Port(inic, **net))

        # find list of pci devices for this host
        pci_devices = self._ipci_operator.pci_devices_get()

        # create an array of pci_devs for each net entry of the device
        pci_devs = []
        for pci_dev in pci_devices:
            pci_dev_array = \
                self._ipci_operator.pci_get_device_attrs(pci_dev.pciaddr)
            for dev in pci_dev_array:
                pci_devs.append(pci.PCIDevice(pci_dev, **dev))

        # create a list of MAC addresses that will be used to identify the
        # inventoried host (one of the MACs should be the management MAC)
        host_macs = [port.mac for port in iports if port.mac]

        # get my ihost record which should be avail since booted

        LOG.debug('Inventory Agent iports={}, host_macs={}'.format(
            iports, host_macs))

        slept = 0
        while slept < MAXSLEEP:
            # wait for controller to come up first may be a DOR
            try:
                ihost = rpcapi.get_host_by_macs(icontext, host_macs)
            except messaging.MessagingTimeout:
                LOG.info("get_host_by_macs Messaging Timeout.")
            except Exception as ex:
                LOG.warn("Conductor RPC get_host_by_macs exception "
                         "response %s" % ex)

            if not ihost:
                hostname = socket.gethostname()
                if hostname != k_host.LOCALHOST_HOSTNAME:
                    try:
                        ihost = rpcapi.get_host_by_hostname(icontext,
                                                            hostname)
                    except messaging.MessagingTimeout:
                        LOG.info("get_host_by_hostname Messaging Timeout.")
                        return  # wait for next audit cycle
                    except Exception as ex:
                        LOG.warn("Conductor RPC get_host_by_hostname "
                                 "exception response %s" % ex)

            if ihost and ihost.get('personality'):
                self.report_to_conductor_required = True
                self._ihost_uuid = ihost['uuid']
                self._ihost_personality = ihost['personality']

                if os.path.isfile(tsc.PLATFORM_CONF_FILE):
                    # read the platform config file and check for UUID
                    found = False
                    with open(tsc.PLATFORM_CONF_FILE, "r") as fd:
                        for line in fd:
                            if line.find("UUID=") == 0:
                                found = True
                    if not found:
                        # the UUID is not found, append it
                        with open(tsc.PLATFORM_CONF_FILE, "a") as fd:
                            fd.write("UUID=" + self._ihost_uuid + "\n")

                # Report host install status
                msg_dict = {}
                self.platform_update_by_host(rpcapi,
                                             icontext,
                                             self._ihost_uuid,
                                             msg_dict)
                LOG.info("Agent found matching ihost: %s" % ihost['uuid'])
                break

            time.sleep(30)
            slept += 30

        if not self.report_to_conductor_required:
            # let the audit take care of it instead
            LOG.info("Inventory no matching ihost found... await Audit")
            return

        subfunctions = self.subfunctions_get()
        try:
            rpcapi.subfunctions_update_by_host(icontext,
                                               ihost['uuid'],
                                               subfunctions)
        except exception.InventoryException:
            LOG.exception("Inventory Agent exception updating "
                          "subfunctions conductor.")
            pass

        # post to inventory db by ihost['uuid']
        iport_dict_array = []
        for port in iports:
            inic_dict = {'pciaddr': port.ipci.pciaddr,
                         'pclass': port.ipci.pclass,
                         'pvendor': port.ipci.pvendor,
                         'pdevice': port.ipci.pdevice,
                         'prevision': port.ipci.prevision,
                         'psvendor': port.ipci.psvendor,
                         'psdevice': port.ipci.psdevice,
                         'pname': port.name,
                         'numa_node': port.numa_node,
                         'sriov_totalvfs': port.sriov_totalvfs,
                         'sriov_numvfs': port.sriov_numvfs,
                         'sriov_vfs_pci_address': port.sriov_vfs_pci_address,
                         'driver': port.driver,
                         'mac': port.mac,
                         'mtu': port.mtu,
                         'speed': port.speed,
                         'link_mode': port.link_mode,
                         'dev_id': port.dev_id,
                         'dpdksupport': port.dpdksupport}

            LOG.debug('Inventory Agent inic {}'.format(inic_dict))

            iport_dict_array.append(inic_dict)
        try:
            # may get duplicate key if already sent on earlier init
            rpcapi.port_update_by_host(icontext,
                                       ihost['uuid'],
                                       iport_dict_array)
        except messaging.MessagingTimeout:
            LOG.info("pci_device_update_by_host Messaging Timeout.")
            self.report_to_conductor_required = False
            return  # wait for next audit cycle

        # post to inventory db by ihost['uuid']
        pci_device_dict_array = []
        for dev in pci_devs:
            pci_dev_dict = {'name': dev.name,
                            'pciaddr': dev.pci.pciaddr,
                            'pclass_id': dev.pclass_id,
                            'pvendor_id': dev.pvendor_id,
                            'pdevice_id': dev.pdevice_id,
                            'pclass': dev.pci.pclass,
                            'pvendor': dev.pci.pvendor,
                            'pdevice': dev.pci.pdevice,
                            'prevision': dev.pci.prevision,
                            'psvendor': dev.pci.psvendor,
                            'psdevice': dev.pci.psdevice,
                            'numa_node': dev.numa_node,
                            'sriov_totalvfs': dev.sriov_totalvfs,
                            'sriov_numvfs': dev.sriov_numvfs,
                            'sriov_vfs_pci_address': dev.sriov_vfs_pci_address,
                            'driver': dev.driver,
                            'enabled': dev.enabled,
                            'extra_info': dev.extra_info}
            LOG.debug('Inventory Agent dev {}'.format(pci_dev_dict))

            pci_device_dict_array.append(pci_dev_dict)
        try:
            # may get duplicate key if already sent on earlier init
            rpcapi.pci_device_update_by_host(icontext,
                                             ihost['uuid'],
                                             pci_device_dict_array)
        except messaging.MessagingTimeout:
            LOG.info("pci_device_update_by_host Messaging Timeout.")
            self.report_to_conductor_required = True

        # Find list of numa_nodes and cpus for this ihost
        inumas, icpus = self._inode_operator.inodes_get_inumas_icpus()

        try:
            # may get duplicate key if already sent on earlier init
            rpcapi.numas_update_by_host(icontext,
                                        ihost['uuid'],
                                        inumas)
        except messaging.RemoteError as e:
            LOG.error("numas_update_by_host RemoteError exc_type=%s" %
                      e.exc_type)
        except messaging.MessagingTimeout:
            LOG.info("pci_device_update_by_host Messaging Timeout.")
            self.report_to_conductor_required = True
        except Exception as e:
            LOG.exception("Inventory Agent exception updating inuma e=%s." % e)
            pass

        force_grub_update = self._force_grub_update()
        try:
            # may get duplicate key if already sent on earlier init
            rpcapi.cpus_update_by_host(icontext,
                                       ihost['uuid'],
                                       icpus,
                                       force_grub_update)
        except messaging.RemoteError as e:
            LOG.error("cpus_update_by_host RemoteError exc_type=%s" %
                      e.exc_type)
        except messaging.MessagingTimeout:
            LOG.info("cpus_update_by_host Messaging Timeout.")
            self.report_to_conductor_required = True
        except Exception as e:
            LOG.exception("Inventory exception updating cpus e=%s." % e)
            self.report_to_conductor_required = True
            pass
        except exception.InventoryException:
            LOG.exception("Inventory exception updating cpus conductor.")
            pass

        imemory = self._inode_operator.inodes_get_imemory()
        if imemory:
            try:
                # may get duplicate key if already sent on earlier init
                rpcapi.memory_update_by_host(icontext,
                                             ihost['uuid'],
                                             imemory)
            except messaging.MessagingTimeout:
                LOG.info("memory_update_by_host Messaging Timeout.")
            except messaging.RemoteError as e:
                LOG.error("memory_update_by_host RemoteError exc_type=%s" %
                          e.exc_type)
            except exception.InventoryException:
                LOG.exception("Inventory Agent exception updating imemory "
                              "conductor.")

        if self._ihost_uuid and \
                os.path.isfile(tsc.INITIAL_CONFIG_COMPLETE_FLAG):
            if not self._report_to_conductor_iplatform_avail_flag:
                # and not self._wait_for_nova_lvg()
                imsg_dict = {'availability': k_host.AVAILABILITY_AVAILABLE}

                iscsi_initiator_name = self.get_host_iscsi_initiator_name()
                if iscsi_initiator_name is not None:
                    imsg_dict.update({'iscsi_initiator_name':
                                      iscsi_initiator_name})

                # Before setting the host to AVAILABILITY_AVAILABLE make
                # sure that nova_local aggregates are correctly set
                self.platform_update_by_host(rpcapi,
                                             icontext,
                                             self._ihost_uuid,
                                             imsg_dict)

                self._report_to_conductor_iplatform_avail()

    def subfunctions_get(self):
        """returns subfunctions on this host.
        """

        self._subfunctions = ','.join(tsc.subfunctions)

        return self._subfunctions

    @staticmethod
    def subfunctions_list_get():
        """returns list of subfunctions on this host.
        """
        subfunctions = ','.join(tsc.subfunctions)
        subfunctions_list = subfunctions.split(',')

        return subfunctions_list

    def subfunctions_configured(self, subfunctions_list):
        """Determines whether subfunctions configuration is completed.
           return: Bool whether subfunctions configuration is completed.
        """
        if (k_host.CONTROLLER in subfunctions_list and
                k_host.COMPUTE in subfunctions_list):
            if not os.path.exists(tsc.INITIAL_COMPUTE_CONFIG_COMPLETE):
                self._subfunctions_configured = False
                return False

        self._subfunctions_configured = True
        return True

    @staticmethod
    def _wait_for_nova_lvg(icontext, rpcapi, ihost_uuid, nova_lvgs=None):
        """See if we wait for a provisioned nova-local volume group

        This method queries the conductor to see if we are provisioning
        a nova-local volume group on this boot cycle. This check is used
        to delay sending the platform availability to the conductor.

        :param:   icontext: an admin context
        :param:   rpcapi: conductor rpc api
        :param:   ihost_uuid: an admin context
        :returns: True if we are provisioning false otherwise
        """

        return True
        LOG.info("TODO _wait_for_nova_lvg from systemconfig")

    def _is_config_complete(self):
        """Check if this node has completed config

        This method queries node's config flag file to see if it has
        complete config.
        :return: True if the complete flag file exists false otherwise
        """
        if not os.path.isfile(tsc.INITIAL_CONFIG_COMPLETE_FLAG):
            return False
        subfunctions = self.subfunctions_list_get()
        if k_host.CONTROLLER in subfunctions:
            if not os.path.isfile(tsc.INITIAL_CONTROLLER_CONFIG_COMPLETE):
                return False
        if k_host.COMPUTE in subfunctions:
            if not os.path.isfile(tsc.INITIAL_COMPUTE_CONFIG_COMPLETE):
                return False
        if k_host.STORAGE in subfunctions:
            if not os.path.isfile(tsc.INITIAL_STORAGE_CONFIG_COMPLETE):
                return False
        return True

    @periodics.periodic(spacing=CONF.agent.audit_interval,
                        run_immediately=True)
    def _agent_audit(self, context):
        # periodically, perform inventory audit
        self.agent_audit(context, host_uuid=self._ihost_uuid,
                         force_updates=None)

    def agent_audit(self, context,
                    host_uuid, force_updates, cinder_device=None):
        # perform inventory audit
        if self._ihost_uuid != host_uuid:
            # The function call is not for this host agent
            return

        icontext = mycontext.get_admin_context()
        rpcapi = conductor_rpcapi.ConductorAPI(
            topic=conductor_rpcapi.MANAGER_TOPIC)

        if not self.report_to_conductor_required:
            LOG.info("Inventory Agent audit running inv_get_and_report.")
            self.ihost_inv_get_and_report(icontext)

        if self._ihost_uuid and os.path.isfile(
                tsc.INITIAL_CONFIG_COMPLETE_FLAG):
            if (not self._report_to_conductor_iplatform_avail_flag and
                    not self._wait_for_nova_lvg(
                        icontext, rpcapi, self._ihost_uuid)):
                imsg_dict = {'availability': k_host.AVAILABILITY_AVAILABLE}

                iscsi_initiator_name = self.get_host_iscsi_initiator_name()
                if iscsi_initiator_name is not None:
                    imsg_dict.update({'iscsi_initiator_name':
                                      iscsi_initiator_name})

                # Before setting the host to AVAILABILITY_AVAILABLE make
                # sure that nova_local aggregates are correctly set
                self.platform_update_by_host(rpcapi,
                                             icontext,
                                             self._ihost_uuid,
                                             imsg_dict)

                self._report_to_conductor_iplatform_avail()

            if (self._ihost_personality == k_host.CONTROLLER and
                    not self._notify_subfunctions_alarm_clear):

                subfunctions_list = self.subfunctions_list_get()
                if ((k_host.CONTROLLER in subfunctions_list) and
                        (k_host.COMPUTE in subfunctions_list)):
                    if self.subfunctions_configured(subfunctions_list) and \
                            not self._wait_for_nova_lvg(
                                icontext, rpcapi, self._ihost_uuid):

                        ihost_notify_dict = {'subfunctions_configured': True}
                        rpcapi.notify_subfunctions_config(icontext,
                                                          self._ihost_uuid,
                                                          ihost_notify_dict)
                        self._notify_subfunctions_alarm_clear = True
                    else:
                        if not self._notify_subfunctions_alarm_raise:
                            ihost_notify_dict = {'subfunctions_configured':
                                                 False}
                            rpcapi.notify_subfunctions_config(
                                icontext, self._ihost_uuid, ihost_notify_dict)
                            self._notify_subfunctions_alarm_raise = True
                else:
                    self._notify_subfunctions_alarm_clear = True

        if self._ihost_uuid:
            LOG.debug("Inventory Agent Audit running.")

            if force_updates:
                LOG.debug("Inventory Agent Audit force updates: (%s)" %
                          (', '.join(force_updates)))

            self._update_ttys_dcd_status(icontext, self._ihost_uuid)
            if self._agent_throttle > 5:
                # throttle updates
                self._agent_throttle = 0
                imemory = self._inode_operator.inodes_get_imemory()
                rpcapi.memory_update_by_host(icontext,
                                             self._ihost_uuid,
                                             imemory)
                if self._is_config_complete():
                    self.host_lldp_get_and_report(
                        icontext, rpcapi, self._ihost_uuid)
                else:
                    self._lldp_enable_and_report(
                        icontext, rpcapi, self._ihost_uuid)
            self._agent_throttle += 1

            if os.path.isfile(tsc.PLATFORM_CONF_FILE):
                # read the platform config file and check for UUID
                if 'UUID' not in open(tsc.PLATFORM_CONF_FILE).read():
                    # the UUID is not in found, append it
                    with open(tsc.PLATFORM_CONF_FILE, "a") as fd:
                        fd.write("UUID=" + self._ihost_uuid)

    def configure_lldp_systemname(self, context, systemname):
        """Configure the systemname into the lldp agent with the supplied data.

        :param context: an admin context.
        :param systemname: the systemname
        """

        # TODO(sc): This becomes an inventory-api call from
        # via systemconfig: configure_isystemname
        rpcapi = conductor_rpcapi.ConductorAPI(
            topic=conductor_rpcapi.MANAGER_TOPIC)
        # Update the lldp agent
        self._lldp_operator.lldp_update_systemname(systemname)
        # Trigger an audit to ensure the db is up to date
        self.host_lldp_get_and_report(context, rpcapi, self._ihost_uuid)

    def configure_ttys_dcd(self, context, uuid, ttys_dcd):
        """Configure the getty on the serial device.

        :param context: an admin context.
        :param uuid: the host uuid
        :param ttys_dcd: the flag to enable/disable dcd
        """

        LOG.debug("AgentManager.configure_ttys_dcd: %s %s" % (uuid, ttys_dcd))
        if self._ihost_uuid and self._ihost_uuid == uuid:
            LOG.debug("AgentManager configure getty on serial console")
            self._config_ttys_login(ttys_dcd)
        return

    def execute_command(self, context, host_uuid, command):
        """Execute a command on behalf of inventory-conductor

        :param context: request context
        :param host_uuid: the host uuid
        :param command: the command to execute
        """

        LOG.debug("AgentManager.execute_command: (%s)" % command)
        if self._ihost_uuid and self._ihost_uuid == host_uuid:
            LOG.info("AgentManager execute_command: (%s)" % command)
            with open(os.devnull, "w") as fnull:
                try:
                    subprocess.check_call(command, stdout=fnull, stderr=fnull)
                except subprocess.CalledProcessError as e:
                    LOG.error("Failed to execute (%s) (%d)",
                              command, e.returncode)
                except OSError as e:
                    LOG.error("Failed to execute (%s), OS error:(%d)",
                              command, e.errno)

                LOG.info("(%s) executed.", command)

    def get_host_iscsi_initiator_name(self):
        iscsi_initiator_name = None
        try:
            stdout, __ = utils.execute('cat', '/etc/iscsi/initiatorname.iscsi',
                                       run_as_root=True)
            if stdout:
                stdout = stdout.strip()
                iscsi_initiator_name = stdout.split('=')[-1]
            LOG.info("iscsi initiator name = %s" % iscsi_initiator_name)
        except Exception:
            LOG.error("Failed retrieving iscsi initiator name")

        return iscsi_initiator_name

    def update_host_memory(self, context, host_uuid):
        """update the host memory

        :param context: an admin context
        :param host_uuid: ihost uuid unique id
        :return: None
        """
        if self._ihost_uuid and self._ihost_uuid == host_uuid:
            rpcapi = conductor_rpcapi.ConductorAPI(
                topic=conductor_rpcapi.MANAGER_TOPIC)
            memory = self._inode_operator.inodes_get_imemory()
            rpcapi.memory_update_by_host(context,
                                         self._ihost_uuid,
                                         memory,
                                         force_update=True)
