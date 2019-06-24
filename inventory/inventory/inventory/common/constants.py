#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# vim: tabstop=4 shiftwidth=4 softtabstop=4
# coding=utf-8
#

from inventory.common import k_host
import os
import tsconfig.tsconfig as tsc

INVENTORY_RUNNING_IN_LAB = '/etc/inventory/.running_in_lab'
INVENTORY_CONFIG_PATH = \
    os.path.join(tsc.PLATFORM_PATH, "inventory", tsc.SW_VERSION)

VIM_DEFAULT_TIMEOUT_IN_SECS = 5
VIM_DELETE_TIMEOUT_IN_SECS = 10
MTC_ADD_TIMEOUT_IN_SECS = 6
MTC_DELETE_TIMEOUT_IN_SECS = 10
MTC_DEFAULT_TIMEOUT_IN_SECS = 6
HWMON_DEFAULT_TIMEOUT_IN_SECS = 6
PATCH_DEFAULT_TIMEOUT_IN_SECS = 6

DB_SUPPRESS_STATUS = 1
DB_MGMT_AFFECTING = 2
DB_DEGRADE_AFFECTING = 3

# CPU functions
PLATFORM_FUNCTION = "Platform"
VSWITCH_FUNCTION = "Vswitch"
SHARED_FUNCTION = "Shared"
VM_FUNCTION = "VMs"
NO_FUNCTION = "None"

# Hugepage sizes in MiB
MIB_2M = 2
MIB_1G = 1024
Ki = 1024
NUM_4K_PER_MiB = 256

# Dynamic IO Resident Set Size(RSS) in MiB per socket
DISK_IO_RESIDENT_SET_SIZE_MIB = 2000
DISK_IO_RESIDENT_SET_SIZE_MIB_VBOX = 500

# Memory reserved for platform core in MiB per host
PLATFORM_CORE_MEMORY_RESERVED_MIB = 2000
PLATFORM_CORE_MEMORY_RESERVED_MIB_VBOX = 1100

# For combined node, memory reserved for controller in MiB
COMBINED_NODE_CONTROLLER_MEMORY_RESERVED_MIB = 10500
COMBINED_NODE_CONTROLLER_MEMORY_RESERVED_MIB_VBOX = 6000
COMBINED_NODE_CONTROLLER_MEMORY_RESERVED_MIB_XEOND = 7000

# Max number of physical cores in a xeon-d cpu
NUMBER_CORES_XEOND = 8

# Max number of computes that can be added to an AIO duplex system
AIO_DUPLEX_MAX_COMPUTES = 4

# Network overhead for DHCP or vrouter, assume 100 networks * 40 MB each
NETWORK_METADATA_OVERHEAD_MIB = 4000
NETWORK_METADATA_OVERHEAD_MIB_VBOX = 0

# Sensors
SENSOR_DATATYPE_VALID_LIST = ['discrete', 'analog']
HWMON_PORT = 2212

# Supported compute node vswitch types
VSWITCH_TYPE_OVS_DPDK = "ovs-dpdk"

# Partition default sizes
DEFAULT_IMAGE_STOR_SIZE = 10
DEFAULT_DOCKER_STOR_SIZE = 1
DEFAULT_DOCKER_DISTRIBUTION_STOR_SIZE = 1
DEFAULT_DATABASE_STOR_SIZE = 20
DEFAULT_IMG_CONVERSION_STOR_SIZE = 20
DEFAULT_SMALL_IMAGE_STOR_SIZE = 10
DEFAULT_SMALL_DATABASE_STOR_SIZE = 10
DEFAULT_SMALL_IMG_CONVERSION_STOR_SIZE = 10
DEFAULT_SMALL_BACKUP_STOR_SIZE = 30
DEFAULT_VIRTUAL_IMAGE_STOR_SIZE = 8
DEFAULT_VIRTUAL_DATABASE_STOR_SIZE = 5
DEFAULT_VIRTUAL_IMG_CONVERSION_STOR_SIZE = 8
DEFAULT_VIRTUAL_BACKUP_STOR_SIZE = 5
DEFAULT_EXTENSION_STOR_SIZE = 1
DEFAULT_PATCH_VAULT_STOR_SIZE = 8
DEFAULT_ETCD_STORE_SIZE = 1
DEFAULT_GNOCCHI_STOR_SIZE = 5

# Openstack Interface names
OS_INTERFACE_PUBLIC = 'public'
OS_INTERFACE_INTERNAL = 'internal'
OS_INTERFACE_ADMIN = 'admin'

# Default region one name
REGION_ONE_NAME = 'RegionOne'
# DC Region Must match VIRTUAL_MASTER_CLOUD in dcorch
SYSTEM_CONTROLLER_REGION = 'SystemController'

# Valid major numbers for disks:
#     https://www.kernel.org/doc/Documentation/admin-guide/devices.txt
#
#   3 block First MFM, RLL and IDE hard disk/CD-ROM interface
#   8 block SCSI disk devices (0-15)
#  65 block SCSI disk devices (16-31)
#  66 block SCSI disk devices (32-47)
#  67 block SCSI disk devices (48-63)
#  68 block SCSI disk devices (64-79)
#  69 block SCSI disk devices (80-95)
#  70 block SCSI disk devices (96-111)
#  71 block SCSI disk devices (112-127)
# 128 block SCSI disk devices (128-143)
# 129 block SCSI disk devices (144-159)
# 130 block SCSI disk devices (160-175)
# 131 block SCSI disk devices (176-191)
# 132 block SCSI disk devices (192-207)
# 133 block SCSI disk devices (208-223)
# 134 block SCSI disk devices (224-239)
# 135 block SCSI disk devices (240-255)
# 240-254 block    LOCAL/EXPERIMENTAL USE (253 == /dev/vdX)
# 259 block    Block Extended Major (NVMe - /dev/nvmeXn1)
VALID_MAJOR_LIST = ['3', '8', '65', '66', '67', '68', '69', '70', '71',
                    '128', '129', '130', '131', '132', '133', '134',
                    '135', '253', '259']
VENDOR_ID_LIO = 'LIO-ORG'

# Storage backends supported
SB_TYPE_FILE = 'file'
SB_TYPE_LVM = 'lvm'
SB_TYPE_CEPH = 'ceph'
SB_TYPE_CEPH_EXTERNAL = 'ceph-external'
SB_TYPE_EXTERNAL = 'external'

SB_SUPPORTED = [SB_TYPE_FILE,
                SB_TYPE_LVM,
                SB_TYPE_CEPH,
                SB_TYPE_CEPH_EXTERNAL,
                SB_TYPE_EXTERNAL]

# Storage backend default names
SB_DEFAULT_NAME_SUFFIX = "-store"
SB_DEFAULT_NAMES = {
    SB_TYPE_FILE: SB_TYPE_FILE + SB_DEFAULT_NAME_SUFFIX,
    SB_TYPE_LVM: SB_TYPE_LVM + SB_DEFAULT_NAME_SUFFIX,
    SB_TYPE_CEPH: SB_TYPE_CEPH + SB_DEFAULT_NAME_SUFFIX,
    SB_TYPE_CEPH_EXTERNAL: SB_TYPE_CEPH_EXTERNAL + SB_DEFAULT_NAME_SUFFIX,
    SB_TYPE_EXTERNAL: 'shared_services'
}

# Storage backends services
SB_SVC_CINDER = 'cinder'
SB_SVC_GLANCE = 'glance'
SB_SVC_NOVA = 'nova'
SB_SVC_SWIFT = 'swift'

SB_FILE_SVCS_SUPPORTED = [SB_SVC_GLANCE]
SB_LVM_SVCS_SUPPORTED = [SB_SVC_CINDER]
SB_CEPH_SVCS_SUPPORTED = [
    SB_SVC_GLANCE, SB_SVC_CINDER,
    SB_SVC_SWIFT, SB_SVC_NOVA]  # supported primary tier svc
SB_CEPH_EXTERNAL_SVCS_SUPPORTED = [SB_SVC_CINDER, SB_SVC_GLANCE, SB_SVC_NOVA]
SB_EXTERNAL_SVCS_SUPPORTED = [SB_SVC_CINDER, SB_SVC_GLANCE]

# Storage backend: Service specific backend nomenclature
CINDER_BACKEND_CEPH = SB_TYPE_CEPH
CINDER_BACKEND_CEPH_EXTERNAL = SB_TYPE_CEPH_EXTERNAL
CINDER_BACKEND_LVM = SB_TYPE_LVM
GLANCE_BACKEND_FILE = SB_TYPE_FILE
GLANCE_BACKEND_RBD = 'rbd'
GLANCE_BACKEND_HTTP = 'http'
GLANCE_BACKEND_GLANCE = 'glance'

# Storage Tiers: types (aligns with polymorphic backends)
SB_TIER_TYPE_CEPH = SB_TYPE_CEPH
SB_TIER_SUPPORTED = [SB_TIER_TYPE_CEPH]
SB_TIER_DEFAULT_NAMES = {
    SB_TIER_TYPE_CEPH: 'storage'  # maps to crushmap 'storage-tier' root
}
SB_TIER_CEPH_SECONDARY_SVCS = [SB_SVC_CINDER]  # supported secondary tier svcs

SB_TIER_STATUS_DEFINED = 'defined'
SB_TIER_STATUS_IN_USE = 'in-use'

# File name reserved for internal ceph cluster.
SB_TYPE_CEPH_CONF_FILENAME = "ceph.conf"

# Glance images path when it is file backended
GLANCE_IMAGE_PATH = tsc.CGCS_PATH + "/" + SB_SVC_GLANCE + "/images"

# Path for Ceph (internal and external) config files
CEPH_CONF_PATH = "/etc/ceph/"

# Requested storage backend API operations
SB_API_OP_CREATE = "create"
SB_API_OP_MODIFY = "modify"
SB_API_OP_DELETE = "delete"

# Storage backend state
SB_STATE_CONFIGURED = 'configured'
SB_STATE_CONFIGURING = 'configuring'
SB_STATE_CONFIG_ERR = 'configuration-failed'

# Storage backend tasks
SB_TASK_NONE = None
SB_TASK_APPLY_MANIFESTS = 'applying-manifests'
SB_TASK_APPLY_CONFIG_FILE = 'applying-config-file'
SB_TASK_RECONFIG_CONTROLLER = 'reconfig-controller'
SB_TASK_PROVISION_STORAGE = 'provision-storage'
SB_TASK_PROVISION_SERVICES = 'provision-services'
SB_TASK_RECONFIG_COMPUTE = 'reconfig-compute'
SB_TASK_RESIZE_CEPH_MON_LV = 'resize-ceph-mon-lv'
SB_TASK_ADD_OBJECT_GATEWAY = 'add-object-gateway'
SB_TASK_RESTORE = 'restore'

# Storage backend ceph-mon-lv size
SB_CEPH_MON_GIB = 20
SB_CEPH_MON_GIB_MIN = 20
SB_CEPH_MON_GIB_MAX = 40

SB_CONFIGURATION_TIMEOUT = 1200

# Storage: Minimum number of monitors
MIN_STOR_MONITORS = 2

# Suffix used in LVM volume name to indicate that the
# volume is actually a thin pool.  (And thin volumes will
# be created in the thin pool.)
LVM_POOL_SUFFIX = '-pool'

# File system names
FILESYSTEM_NAME_BACKUP = 'backup'
FILESYSTEM_NAME_CGCS = 'cgcs'
FILESYSTEM_DISPLAY_NAME_CGCS = 'glance'
FILESYSTEM_NAME_CINDER = 'cinder'
FILESYSTEM_NAME_DATABASE = 'database'
FILESYSTEM_NAME_IMG_CONVERSIONS = 'img-conversions'
FILESYSTEM_NAME_SCRATCH = 'scratch'
FILESYSTEM_NAME_DOCKER = 'docker'
FILESYSTEM_NAME_DOCKER_DISTRIBUTION = 'docker-distribution'
FILESYSTEM_NAME_EXTENSION = 'extension'
FILESYSTEM_NAME_ETCD = 'etcd'
FILESYSTEM_NAME_PATCH_VAULT = 'patch-vault'
FILESYSTEM_NAME_GNOCCHI = 'gnocchi'

FILESYSTEM_LV_DICT = {
    FILESYSTEM_NAME_CGCS: 'cgcs-lv',
    FILESYSTEM_NAME_BACKUP: 'backup-lv',
    FILESYSTEM_NAME_SCRATCH: 'scratch-lv',
    FILESYSTEM_NAME_DOCKER: 'docker-lv',
    FILESYSTEM_NAME_DOCKER_DISTRIBUTION: 'dockerdistribution-lv',
    FILESYSTEM_NAME_IMG_CONVERSIONS: 'img-conversions-lv',
    FILESYSTEM_NAME_DATABASE: 'pgsql-lv',
    FILESYSTEM_NAME_EXTENSION: 'extension-lv',
    FILESYSTEM_NAME_ETCD: 'etcd-lv',
    FILESYSTEM_NAME_PATCH_VAULT: 'patch-vault-lv',
    FILESYSTEM_NAME_GNOCCHI: 'gnocchi-lv'
}

SUPPORTED_LOGICAL_VOLUME_LIST = FILESYSTEM_LV_DICT.values()

SUPPORTED_FILEYSTEM_LIST = [
    FILESYSTEM_NAME_BACKUP,
    FILESYSTEM_NAME_CGCS,
    FILESYSTEM_NAME_CINDER,
    FILESYSTEM_NAME_DATABASE,
    FILESYSTEM_NAME_EXTENSION,
    FILESYSTEM_NAME_IMG_CONVERSIONS,
    FILESYSTEM_NAME_SCRATCH,
    FILESYSTEM_NAME_DOCKER,
    FILESYSTEM_NAME_DOCKER_DISTRIBUTION,
    FILESYSTEM_NAME_PATCH_VAULT,
    FILESYSTEM_NAME_ETCD,
    FILESYSTEM_NAME_GNOCCHI
]

SUPPORTED_REPLICATED_FILEYSTEM_LIST = [
    FILESYSTEM_NAME_CGCS,
    FILESYSTEM_NAME_DATABASE,
    FILESYSTEM_NAME_EXTENSION,
    FILESYSTEM_NAME_PATCH_VAULT,
    FILESYSTEM_NAME_ETCD,
    FILESYSTEM_NAME_DOCKER_DISTRIBUTION,
]

# Storage: Volume Group Types
LVG_NOVA_LOCAL = 'nova-local'
LVG_CGTS_VG = 'cgts-vg'
LVG_CINDER_VOLUMES = 'cinder-volumes'
LVG_ALLOWED_VGS = [LVG_NOVA_LOCAL, LVG_CGTS_VG, LVG_CINDER_VOLUMES]

# Cinder LVM Parameters
CINDER_LVM_MINIMUM_DEVICE_SIZE_GIB = 5  # GiB
CINDER_LVM_DRBD_RESOURCE = 'drbd-cinder'
CINDER_LVM_DRBD_WAIT_PEER_RETRY = 5
CINDER_LVM_DRBD_WAIT_PEER_SLEEP = 2
CINDER_LVM_POOL_LV = LVG_CINDER_VOLUMES + LVM_POOL_SUFFIX
CINDER_LVM_POOL_META_LV = CINDER_LVM_POOL_LV + "_tmeta"
CINDER_RESIZE_FAILURE = "cinder-resize-failure"
CINDER_DRBD_DEVICE = '/dev/drbd4'

CINDER_LVM_TYPE_THIN = 'thin'
CINDER_LVM_TYPE_THICK = 'thick'

# Storage: Volume Group Parameter Types
LVG_CINDER_PARAM_LVM_TYPE = 'lvm_type'

# Storage: Volume Group Parameter: Cinder: LVM provisioing
LVG_CINDER_LVM_TYPE_THIN = 'thin'
LVG_CINDER_LVM_TYPE_THICK = 'thick'

# Controller audit requests (force updates from agents)
DISK_AUDIT_REQUEST = "audit_disk"
LVG_AUDIT_REQUEST = "audit_lvg"
PV_AUDIT_REQUEST = "audit_pv"
PARTITION_AUDIT_REQUEST = "audit_partition"
CONTROLLER_AUDIT_REQUESTS = [DISK_AUDIT_REQUEST,
                             LVG_AUDIT_REQUEST,
                             PV_AUDIT_REQUEST,
                             PARTITION_AUDIT_REQUEST]

# IP families
IPV4_FAMILY = 4
IPV6_FAMILY = 6
IP_FAMILIES = {IPV4_FAMILY: "IPv4",
               IPV6_FAMILY: "IPv6"}

# Interface definitions
NETWORK_TYPE_NONE = 'none'
NETWORK_TYPE_INFRA = 'infra'
NETWORK_TYPE_MGMT = 'mgmt'
NETWORK_TYPE_OAM = 'oam'
NETWORK_TYPE_BM = 'bm'
NETWORK_TYPE_MULTICAST = 'multicast'
NETWORK_TYPE_DATA = 'data'
NETWORK_TYPE_SYSTEM_CONTROLLER = 'system-controller'

NETWORK_TYPE_PCI_PASSTHROUGH = 'pci-passthrough'
NETWORK_TYPE_PCI_SRIOV = 'pci-sriov'
NETWORK_TYPE_PXEBOOT = 'pxeboot'

PLATFORM_NETWORK_TYPES = [NETWORK_TYPE_PXEBOOT,
                          NETWORK_TYPE_MGMT,
                          NETWORK_TYPE_INFRA,
                          NETWORK_TYPE_OAM]

PCI_NETWORK_TYPES = [NETWORK_TYPE_PCI_PASSTHROUGH,
                     NETWORK_TYPE_PCI_SRIOV]

INTERFACE_TYPE_ETHERNET = 'ethernet'
INTERFACE_TYPE_VLAN = 'vlan'
INTERFACE_TYPE_AE = 'ae'
INTERFACE_TYPE_VIRTUAL = 'virtual'

INTERFACE_CLASS_NONE = 'none'
INTERFACE_CLASS_PLATFORM = 'platform'
INTERFACE_CLASS_DATA = 'data'
INTERFACE_CLASS_PCI_PASSTHROUGH = 'pci-passthrough'
INTERFACE_CLASS_PCI_SRIOV = 'pci-sriov'

SM_MULTICAST_MGMT_IP_NAME = "sm-mgmt-ip"
MTCE_MULTICAST_MGMT_IP_NAME = "mtce-mgmt-ip"
PATCH_CONTROLLER_MULTICAST_MGMT_IP_NAME = "patch-controller-mgmt-ip"
PATCH_AGENT_MULTICAST_MGMT_IP_NAME = "patch-agent-mgmt-ip"
SYSTEM_CONTROLLER_GATEWAY_IP_NAME = "system-controller-gateway-ip"

ADDRESS_FORMAT_ARGS = (k_host.CONTROLLER_HOSTNAME,
                       NETWORK_TYPE_MGMT)
MGMT_CINDER_IP_NAME = "%s-cinder-%s" % ADDRESS_FORMAT_ARGS

ETHERNET_NULL_MAC = '00:00:00:00:00:00'

DEFAULT_MTU = 1500

# Stor function types
STOR_FUNCTION_CINDER = 'cinder'
STOR_FUNCTION_OSD = 'osd'
STOR_FUNCTION_MONITOR = 'monitor'
STOR_FUNCTION_JOURNAL = 'journal'

# Disk types and names.
DEVICE_TYPE_HDD = 'HDD'
DEVICE_TYPE_SSD = 'SSD'
DEVICE_TYPE_NVME = 'NVME'
DEVICE_TYPE_UNDETERMINED = 'Undetermined'
DEVICE_TYPE_NA = 'N/A'
DEVICE_NAME_NVME = 'nvme'

# Disk model types.
DEVICE_MODEL_UNKNOWN = 'Unknown'

# Journal operations.
ACTION_CREATE_JOURNAL = "create"
ACTION_UPDATE_JOURNAL = "update"

# Load constants
MNT_DIR = '/tmp/mnt'

ACTIVE_LOAD_STATE = 'active'
IMPORTING_LOAD_STATE = 'importing'
IMPORTED_LOAD_STATE = 'imported'
ERROR_LOAD_STATE = 'error'
DELETING_LOAD_STATE = 'deleting'

DELETE_LOAD_SCRIPT = '/etc/inventory/upgrades/delete_load.sh'

# Ceph
CEPH_HEALTH_OK = 'HEALTH_OK'
CEPH_HEALTH_BLOCK = 'HEALTH_BLOCK'

# See http://ceph.com/pgcalc/. We set it to more than 100 because pool usage
# varies greatly in Titanium Cloud and we want to avoid running too low on PGs
CEPH_TARGET_PGS_PER_OSD = 200
CEPH_REPLICATION_FACTOR_DEFAULT = 2
CEPH_REPLICATION_FACTOR_SUPPORTED = [2, 3]
CEPH_MIN_REPLICATION_FACTOR_SUPPORTED = [1, 2]
CEPH_REPLICATION_MAP_DEFAULT = {
    # replication: min_replication
    2: 1,
    3: 2
}
# ceph osd pool size
CEPH_BACKEND_REPLICATION_CAP = 'replication'
# ceph osd pool min size
CEPH_BACKEND_MIN_REPLICATION_CAP = 'min_replication'
CEPH_BACKEND_CAP_DEFAULT = {
    CEPH_BACKEND_REPLICATION_CAP:
        str(CEPH_REPLICATION_FACTOR_DEFAULT),
    CEPH_BACKEND_MIN_REPLICATION_CAP:
        str(CEPH_REPLICATION_MAP_DEFAULT[CEPH_REPLICATION_FACTOR_DEFAULT])
}

# Service Parameter
SERVICE_TYPE_IDENTITY = 'identity'
SERVICE_TYPE_KEYSTONE = 'keystone'
SERVICE_TYPE_IMAGE = 'image'
SERVICE_TYPE_VOLUME = 'volume'
SERVICE_TYPE_NETWORK = 'network'
SERVICE_TYPE_HORIZON = "horizon"
SERVICE_TYPE_CEPH = 'ceph'
SERVICE_TYPE_CINDER = 'cinder'
SERVICE_TYPE_MURANO = 'murano'
SERVICE_TYPE_MAGNUM = 'magnum'
SERVICE_TYPE_PLATFORM = 'configuration'
SERVICE_TYPE_NOVA = 'nova'
SERVICE_TYPE_SWIFT = 'swift'
SERVICE_TYPE_IRONIC = 'ironic'
SERVICE_TYPE_PANKO = 'panko'
SERVICE_TYPE_AODH = 'aodh'
SERVICE_TYPE_GLANCE = 'glance'
SERVICE_TYPE_BARBICAN = 'barbican'

# TIS part number, CPE = combined load, STD = standard load
TIS_STD_BUILD = 'Standard'
TIS_AIO_BUILD = 'All-in-one'

# sysadmin password aging.
# Setting aging to max defined value qualifies
# as "never" on certain Linux distros including WRL
SYSADMIN_PASSWORD_NO_AGING = 99999

# Partition table size in bytes.
PARTITION_TABLE_SIZE = 2097152

# States that describe the states of a partition.

# Partition is ready for being used.
PARTITION_READY_STATUS = 0
# Partition is used by a PV.
PARTITION_IN_USE_STATUS = 1
# An in-service request to create the partition has been sent.
PARTITION_CREATE_IN_SVC_STATUS = 2
# An unlock request to create the partition has been sent.
PARTITION_CREATE_ON_UNLOCK_STATUS = 3
# A request to delete the partition has been sent.
PARTITION_DELETING_STATUS = 4
# A request to modify the partition has been sent.
PARTITION_MODIFYING_STATUS = 5
# The partition has been deleted.
PARTITION_DELETED_STATUS = 6
# The creation of the partition has encountered a known error.
PARTITION_ERROR_STATUS = 10
# Partition creation failed due to an internal error, check packstack logs.
PARTITION_ERROR_STATUS_INTERNAL = 11
# Partition was not created because disk does not have a GPT.
PARTITION_ERROR_STATUS_GPT = 12

PARTITION_STATUS_MSG = {
    PARTITION_IN_USE_STATUS: "In-Use",
    PARTITION_CREATE_IN_SVC_STATUS: "Creating",
    PARTITION_CREATE_ON_UNLOCK_STATUS: "Creating (on unlock)",
    PARTITION_DELETING_STATUS: "Deleting",
    PARTITION_MODIFYING_STATUS: "Modifying",
    PARTITION_READY_STATUS: "Ready",
    PARTITION_DELETED_STATUS: "Deleted",
    PARTITION_ERROR_STATUS: "Error",
    PARTITION_ERROR_STATUS_INTERNAL: "Error: Internal script error.",
    PARTITION_ERROR_STATUS_GPT: "Error:Missing GPT Table."}

PARTITION_STATUS_OK_TO_DELETE = [
    PARTITION_READY_STATUS,
    PARTITION_CREATE_ON_UNLOCK_STATUS,
    PARTITION_ERROR_STATUS,
    PARTITION_ERROR_STATUS_INTERNAL,
    PARTITION_ERROR_STATUS_GPT]

PARTITION_STATUS_SEND_DELETE_RPC = [
    PARTITION_READY_STATUS,
    PARTITION_ERROR_STATUS,
    PARTITION_ERROR_STATUS_INTERNAL]

PARTITION_CMD_CREATE = "create"
PARTITION_CMD_DELETE = "delete"
PARTITION_CMD_MODIFY = "modify"

# User creatable, system managed,  GUID partitions types.
PARTITION_USER_MANAGED_GUID_PREFIX = "ba5eba11-0000-1111-2222-"
USER_PARTITION_PHYSICAL_VOLUME = \
    PARTITION_USER_MANAGED_GUID_PREFIX + "000000000001"
LINUX_LVM_PARTITION = "e6d6d379-f507-44c2-a23c-238f2a3df928"

# Partition name for those partitions deignated for PV use.
PARTITION_NAME_PV = "LVM Physical Volume"

# Partition table types.
PARTITION_TABLE_GPT = "gpt"
PARTITION_TABLE_MSDOS = "msdos"

# Optional services
ALL_OPTIONAL_SERVICES = [SERVICE_TYPE_CINDER, SERVICE_TYPE_MURANO,
                         SERVICE_TYPE_MAGNUM, SERVICE_TYPE_SWIFT,
                         SERVICE_TYPE_IRONIC]

# System mode
SYSTEM_MODE_DUPLEX = "duplex"
SYSTEM_MODE_SIMPLEX = "simplex"
SYSTEM_MODE_DUPLEX_DIRECT = "duplex-direct"

# System Security Profiles
SYSTEM_SECURITY_PROFILE_STANDARD = "standard"
SYSTEM_SECURITY_PROFILE_EXTENDED = "extended"

# Install states
INSTALL_STATE_PRE_INSTALL = "preinstall"
INSTALL_STATE_INSTALLING = "installing"
INSTALL_STATE_POST_INSTALL = "postinstall"
INSTALL_STATE_FAILED = "failed"
INSTALL_STATE_INSTALLED = "installed"
INSTALL_STATE_BOOTING = "booting"
INSTALL_STATE_COMPLETED = "completed"

tox_work_dir = os.environ.get("TOX_WORK_DIR")
if tox_work_dir:
    INVENTORY_LOCK_PATH = tox_work_dir
else:
    INVENTORY_LOCK_PATH = os.path.join(tsc.VOLATILE_PATH, "inventory")

NETWORK_CONFIG_LOCK_FILE = os.path.join(
    tsc.VOLATILE_PATH, "apply_network_config.lock")

INVENTORY_USERNAME = "inventory"
INVENTORY_GRPNAME = "inventory"

# License file
LICENSE_FILE = ".license"

# Cinder lvm config complete file.
NODE_CINDER_LVM_CONFIG_COMPLETE_FILE = \
    os.path.join(tsc.PLATFORM_CONF_PATH, '.node_cinder_lvm_config_complete')
INITIAL_CINDER_LVM_CONFIG_COMPLETE_FILE = \
    os.path.join(tsc.CONFIG_PATH, '.initial_cinder_lvm_config_complete')

DISK_WIPE_IN_PROGRESS_FLAG = \
    os.path.join(tsc.PLATFORM_CONF_PATH, '.disk_wipe_in_progress')
DISK_WIPE_COMPLETE_TIMEOUT = 5  # wait for a disk to finish wiping.

# Clone label set in DB
CLONE_ISO_MAC = 'CLONEISOMAC_'
CLONE_ISO_DISK_SID = 'CLONEISODISKSID_'

# kernel options for various security feature selections
SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_V1 = 'spectre_meltdown_v1'
SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_V1_OPTS = 'nopti nospectre_v2'
SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_ALL = 'spectre_meltdown_all'
SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_ALL_OPTS = ''
SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_OPTS = {
    SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_V1:
        SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_V1_OPTS,
    SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_ALL:
        SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_ALL_OPTS
}


SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_DEFAULT_OPTS = \
    SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_V1_OPTS
