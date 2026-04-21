#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Shared test constants for metal project tests.

Centralizes test data values used across multiple
test files to avoid duplication.
"""

# BMC test credentials
BMC_ADDRESS = '10.0.0.1'
BMC_ADDRESS_IPV6 = '2001:db8::1'
BMC_USERNAME = 'admin'
BMC_PASSWORD_ENCODED = 'cGFzc3dvcmQ='
BMC_PASSWORD_PLAIN = 'password'
BMC_IMAGE_URL = 'http://h/b.iso'
BMC_IMAGE_URL_FULL = 'http://192.168.1.1:8080/bootimage.iso'
BMC_IPV4_TEST_ADDR = '192.168.1.1'

# Redfish paths and keys
REDFISH_ROOT = '/redfish/v1'
RESET_ACTION = '#ComputerSystem.Reset'
RESET_KEY = 'ResetType@Redfish.AllowableValues'
BOOT_MODES_KEY = (
    'BootSourceOverrideMode'
    '@Redfish.AllowableValues'
)
EJECT_ACTION = '#VirtualMedia.EjectMedia'
INSERT_ACTION = '#VirtualMedia.InsertMedia'

# Redfish URL patterns
SYSTEMS_URL = '/redfish/v1/Systems/'
MANAGERS_URL = '/redfish/v1/Managers/'
SYSTEM_MEMBER_URL = '/s/1/'
MANAGER_MEMBER_URL = '/m/1/'
VM_URL = '/vm/1'
EJECT_URL = '/ej'
INSERT_URL = '/ins'
RESET_URL = '/reset'
SECURE_BOOT_URL = '/sb'

# Common JSON response templates
SYSTEMS_RESP = (
    '{"Systems":{"@odata.id":"/s/"}}'
)
MEMBERS_RESP = (
    '{"Members":[{"@odata.id":"/s/1/"}]}'
)
MANAGER_MEMBERS_RESP = (
    '{"Members":[{"@odata.id":"/m/1/"}]}'
)
VM_MEDIA_RESP = '{"MediaTypes":["CD"]}'
INSERTED_FALSE_RESP = '{"Inserted":false}'
SB_ENABLED_RESP = '{"SecureBootEnable":true}'
SB_DISABLED_RESP = '{"SecureBootEnable":false}'
SB_REF_RESP = (
    '{"SecureBoot":{"@odata.id":"/sb"}}'
)

# Power states
POWER_ON = 'On'
POWER_OFF = 'Off'

# UDP port for hwmond_notify
HWMOND_UDP_PORT = 2188

# Config file templates
SINGLE_TARGET_CONFIG = (
    "bmc_address: 10.0.0.1\n"
    "bmc_username: admin\n"
    "bmc_password: cGFzc3dvcmQ=\n"
    "image: http://h/b.iso\n"
)
MULTI_TARGET_CONFIG = (
    "virtual_media_iso:\n"
    "  t1:\n"
    "    bmc_address: 10.0.0.1\n"
    "    bmc_username: admin\n"
    "    bmc_password: cGFzc3dvcmQ=\n"
    "    image: http://h/b.iso\n"
)
