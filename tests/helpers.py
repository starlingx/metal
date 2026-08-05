#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Shared test helpers and base classes.

Provides reusable helper functions and a base test
class to eliminate duplication across test files.
"""
import json
import os
import unittest
from unittest import mock

from unit.loader import load_rvmc
from unit.loader import load_rsbc
from unit.loader import resp
from unit.loader import devnull
from tests import constants as tc


# Module singletons - loaded once
_rvmc_mod = None
_rsbc_mod = None


def get_rvmc_module():
    """Return cached rvmc module instance.

    Loads the module on first call and caches it
    for subsequent calls.

    Returns the loaded rvmc module.
    """
    global _rvmc_mod
    if _rvmc_mod is None:
        _rvmc_mod = load_rvmc()
    return _rvmc_mod


def get_rsbc_module():
    """Return cached rsbc module instance.

    Loads the module on first call and caches it
    for subsequent calls.

    Returns the loaded rsbc module.
    """
    global _rsbc_mod
    if _rsbc_mod is None:
        _rsbc_mod = load_rsbc()
    return _rsbc_mod


def make_rvmc_object():
    """Create a VmcObject from the rvmc module.

    Returns a VmcObject configured with test constants.
    """
    mod = get_rvmc_module()
    with mock.patch('sys.stdout', devnull()):
        return mod.VmcObject(
            'h', tc.BMC_ADDRESS, tc.BMC_USERNAME,
            tc.BMC_PASSWORD_ENCODED,
            tc.BMC_PASSWORD_PLAIN,
            tc.BMC_IMAGE_URL
        )


def make_rsbc_object():
    """Create a VmcObject from the rsbc module.

    Returns a VmcObject configured with test constants.
    """
    mod = get_rsbc_module()
    with mock.patch('sys.stdout', devnull()):
        return mod.VmcObject(
            'h', tc.BMC_ADDRESS,
            tc.BMC_USERNAME,
            tc.BMC_PASSWORD_PLAIN
        )


def make_reset_response(
        power_state, allowed_actions,
        target=tc.RESET_URL):
    """Create a mock response with reset actions.

    power_state - current power state string
    allowed_actions - list of allowed reset types
    target - reset target URL

    Returns a mock response object.
    """
    data = {
        'PowerState': power_state,
        'Actions': {
            tc.RESET_ACTION: {
                'target': target,
                tc.RESET_KEY: allowed_actions
            }
        }
    }
    return resp(200, json.dumps(data))


class BaseRvmcTestCase(unittest.TestCase):
    """Base test class for rvmc.py tests.

    Provides common setUp with module loading and
    object creation.
    """

    mod = None

    @classmethod
    def setUpClass(cls):
        """Load rvmc module once for all tests."""
        cls.mod = get_rvmc_module()

    def make_object(self):
        """Create a test VmcObject.

        Returns a VmcObject with test credentials.
        """
        return make_rvmc_object()

    def quiet(self):
        """Return context manager suppressing stdout.

        Returns mock.patch context manager for stdout.
        """
        return mock.patch('sys.stdout', devnull())


def is_ipv6(addr):
    """Check if addr is a valid IPv6 address.

    Returns True if addr is valid IPv6, False otherwise.
    """
    import socket as _socket
    try:
        _socket.inet_pton(_socket.AF_INET6, addr)
        return True
    except _socket.error:
        return False


class BaseRsbcTestCase(unittest.TestCase):
    """Base test class for rsbc.py tests.

    Provides common setUp with module loading and
    object creation.
    """

    mod = None

    @classmethod
    def setUpClass(cls):
        """Load rsbc module once for all tests."""
        cls.mod = get_rsbc_module()

    def make_object(self):
        """Create a test VmcObject.

        Returns a VmcObject with test credentials.
        """
        return make_rsbc_object()

    def quiet(self):
        """Return context manager suppressing stdout.

        Returns mock.patch context manager for stdout.
        """
        return mock.patch('sys.stdout', devnull())


# ── SecureBoot response helpers ──────────────────────────────────

SB_RESP = resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}')
SBD_RESP = resp(200, '{"SecureBootDatabases":{"@odata.id":"/sbd"}}')
SB_MEMBERS_RESP = resp(200, '{"Members":[{"@odata.id":"/sbd/db"}]}')
SB_EMPTY_RESP = resp(200, '{}')


def sb_state_responses(enable_val='false'):
    """Return SecureBoot query side_effect list.

    enable_val - 'true' or 'false' for SecureBootEnable
    Returns list of resp objects for get.side_effect.
    """
    return [
        resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
        resp(200, '{"SecureBootEnable":' + enable_val + '}'),
    ]


def sb_db_responses(cert_string='x'):
    """Return SecureBoot database query side_effect list.

    cert_string - certificate content
    Returns list of resp objects for get.side_effect.
    """
    return [
        resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
        resp(200, '{"SecureBootDatabases":{"@odata.id":"/sbd"}}'),
        resp(200, '{"Members":[{"@odata.id":"/sbd/db"}]}'),
        resp(200, '{}'),
        resp(200, '{"Members":[{"@odata.id":"/sbd/db/c/1"}]}'),
        resp(200, '{"CertificateString":"' + cert_string + '"}'),
    ]


def sb_upload_responses():
    """Return SecureBoot cert upload side_effect list.

    Returns list of resp objects for get.side_effect.
    """
    return [
        resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
        resp(200, '{"SecureBootDatabases":{"@odata.id":"/sbd"}}'),
        resp(200, '{"Members":[{"@odata.id":"/sbd/db"}]}'),
        resp(200, '{}'),
    ]
