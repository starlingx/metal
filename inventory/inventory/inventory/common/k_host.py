#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# Inventory Host Management Constants

# Administrative States
ADMIN_UNLOCKED = 'unlocked'
ADMIN_LOCKED = 'locked'

# Operational States
OPERATIONAL_ENABLED = 'enabled'
OPERATIONAL_DISABLED = 'disabled'

# Availability Status
AVAILABILITY_AVAILABLE = 'available'
AVAILABILITY_OFFLINE = 'offline'
AVAILABILITY_ONLINE = 'online'
AVAILABILITY_DEGRADED = 'degraded'

# Host Actions:
ACTION_UNLOCK = 'unlock'
ACTION_FORCE_UNLOCK = 'force-unlock'
ACTION_LOCK = 'lock'
ACTION_FORCE_LOCK = 'force-lock'
ACTION_REBOOT = 'reboot'
ACTION_RESET = 'reset'
ACTION_REINSTALL = 'reinstall'
ACTION_POWERON = 'power-on'
ACTION_POWEROFF = 'power-off'
ACTION_SWACT = 'swact'
ACTION_FORCE_SWACT = 'force-swact'
ACTION_SUBFUNCTION_CONFIG = 'subfunction_config'
ACTION_DELETE = 'delete'
ACTION_NONE = 'none'


ACTIONS_VIM = [ACTION_LOCK,
               ACTION_FORCE_LOCK]

# VIM services
VIM_SERVICES_ENABLED = 'services-enabled'
VIM_SERVICES_DISABLED = 'services-disabled'
VIM_SERVICES_DISABLE_EXTEND = 'services-disable-extend'
VIM_SERVICES_DISABLE_FAILED = 'services-disable-failed'
VIM_SERVICES_DELETE_FAILED = 'services-delete-failed'

ACTIONS_MTCE = [
    ACTION_REBOOT,
    ACTION_REINSTALL,
    ACTION_RESET,
    ACTION_POWERON,
    ACTION_POWEROFF,
    ACTION_SWACT,
    ACTION_UNLOCK,
    VIM_SERVICES_DISABLED,
    VIM_SERVICES_DISABLE_FAILED,
    ACTION_FORCE_SWACT]

ACTIONS_CONFIG = [ACTION_SUBFUNCTION_CONFIG]

# Personalities
CONTROLLER = 'controller'
STORAGE = 'storage'
COMPUTE = 'compute'

PERSONALITIES = [CONTROLLER, STORAGE, COMPUTE]

# Host names
LOCALHOST_HOSTNAME = 'localhost'

CONTROLLER_HOSTNAME = 'controller'
CONTROLLER_0_HOSTNAME = '%s-0' % CONTROLLER_HOSTNAME
CONTROLLER_1_HOSTNAME = '%s-1' % CONTROLLER_HOSTNAME

STORAGE_HOSTNAME = 'storage'
STORAGE_0_HOSTNAME = '%s-0' % STORAGE_HOSTNAME
STORAGE_1_HOSTNAME = '%s-1' % STORAGE_HOSTNAME
STORAGE_2_HOSTNAME = '%s-2' % STORAGE_HOSTNAME
# Other Storage Hostnames are built dynamically.

# SUBFUNCTION FEATURES
SUBFUNCTIONS = 'subfunctions'
LOWLATENCY = 'lowlatency'

LOCKING = 'Locking'
FORCE_LOCKING = "Force Locking"

# invprovision status
PROVISIONED = 'provisioned'
PROVISIONING = 'provisioning'
UNPROVISIONED = 'unprovisioned'

# Board Management Controller
BM_EXTERNAL = "External"
BM_TYPE_GENERIC = 'bmc'
BM_TYPE_NONE = 'none'

HOST_STOR_FUNCTION = 'stor_function'

# ihost config_status field values
CONFIG_STATUS_REINSTALL = "Reinstall required"

# when reinstall starts, mtc updates the db with task = 'Reinstalling'
TASK_REINSTALLING = "Reinstalling"
HOST_ACTION_STATE = "action_state"
HAS_REINSTALLING = "reinstalling"
HAS_REINSTALLED = "reinstalled"
