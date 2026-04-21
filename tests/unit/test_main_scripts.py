#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Tests for module-level script execution paths."""
import importlib.util
import os
import sys
import tempfile
import types
import unittest
from unittest import mock
from tests import constants as tc

from unit.loader import devnull

RVMC = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..',
    'tools', 'rvmc', 'docker', 'rvmc.py'))
RSBC = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..',
    'tools', 'rsbc', 'rsbc.py'))


def _setup_mock_modules():
    """Set up mock redfish and requests modules.

    Installs mock versions of redfish, redfish.rest,
    redfish.rest.v1, and requests into sys.modules.
    """
    mock_redfish = types.ModuleType('redfish')
    mock_redfish.redfish_client = mock.MagicMock()
    mock_rest_v1 = types.ModuleType('redfish.rest.v1')
    mock_rest_v1.InvalidCredentialsError = type(
        'InvalidCredentialsError', (Exception,), {}
    )
    sys.modules['redfish'] = mock_redfish
    sys.modules['redfish.rest'] = types.ModuleType(
        'redfish.rest'
    )
    sys.modules['redfish.rest.v1'] = mock_rest_v1
    mock_requests = types.ModuleType('requests')
    mock_requests.request = mock.MagicMock()
    sys.modules['requests'] = mock_requests


def _load_module(file_path, argv,
                 config_data=None, exists=False):
    """Load a Python module with mocked side effects.

    file_path - absolute path to the .py file
    argv - sys.argv to use during module load
    config_data - YAML config content or None
    exists - return value for os.path.exists mock

    Returns the loaded module object.
    """
    _setup_mock_modules()
    module_key = 'mod_' + str(id(argv))
    if module_key in sys.modules:
        del sys.modules[module_key]
    spec = importlib.util.spec_from_file_location(
        module_key, file_path
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_key] = module

    config_file = None
    if config_data and exists:
        config_file = tempfile.NamedTemporaryFile(
            mode='w', suffix='.yaml', delete=False
        )
        config_file.write(config_data)
        config_file.close()

    def exists_side_effect(path):
        """Return True if config, else default."""
        return True if config_file else exists

    original_open = open

    def patched_open(filepath, *args, **kwargs):
        """Redirect config file opens to temp file."""
        if config_file and isinstance(filepath, str):
            is_yaml = filepath.endswith('.yaml')
            is_config = (
                filepath == '/etc/rvmc.yaml'
                or 'config' in filepath.lower()
            )
            not_source = (
                'rvmc' not in filepath
                and 'rsbc' not in filepath
            )
            if (is_yaml and not_source) or is_config:
                return original_open(
                    config_file.name, *args, **kwargs
                )
        return original_open(filepath, *args, **kwargs)

    with mock.patch('sys.argv', argv), \
         mock.patch('os.path.exists',
                    side_effect=exists_side_effect), \
         mock.patch('sys.exit',
                    side_effect=SystemExit), \
         mock.patch('os.system', return_value=1), \
         mock.patch('sys.stdout', devnull()), \
         mock.patch('time.sleep'):
        if config_file:
            with mock.patch('builtins.open',
                            side_effect=patched_open):
                try:
                    spec.loader.exec_module(module)
                except SystemExit:
                    pass
        else:
            with mock.patch('builtins.open',
                            mock.mock_open(read_data='')):
                try:
                    spec.loader.exec_module(module)
                except SystemExit:
                    pass

    if config_file:
        os.unlink(config_file.name)
    return module


class TestRvmcMain(unittest.TestCase):
    """Test rvmc.py module-level execution."""

    def test_no_config(self):
        """Test load with no config file."""
        result = _load_module(RVMC, ['rvmc.py'], exists=False)
        self.assertIsNotNone(result)

    def test_with_target(self):
        """Test load with target and debug args."""
        result = _load_module(
            RVMC,
            ['rvmc.py', '--target', 't1', '--debug', '1'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_single_config(self):
        """Test load with single-target config."""
        cfg = tc.SINGLE_TARGET_CONFIG
        result = _load_module(
            RVMC, ['rvmc.py'],
            config_data=cfg, exists=True
        )
        self.assertIsNotNone(result)

    def test_multi_config(self):
        """Test load with multi-target config."""
        cfg = tc.MULTI_TARGET_CONFIG
        result = _load_module(
            RVMC, ['rvmc.py'],
            config_data=cfg, exists=True
        )
        self.assertIsNotNone(result)

    def test_multi_with_target(self):
        """Test load with multi config and target arg."""
        cfg = tc.MULTI_TARGET_CONFIG
        result = _load_module(
            RVMC, ['rvmc.py', '--target', 't1'],
            config_data=cfg, exists=True
        )
        self.assertIsNotNone(result)

    def test_bad_config(self):
        """Test load with malformed config."""
        result = _load_module(
            RVMC, ['rvmc.py'],
            config_data='bad: [yaml', exists=True
        )
        self.assertIsNotNone(result)


class TestRsbcMain(unittest.TestCase):
    """Test rsbc.py module-level execution."""

    def test_no_config_no_ip(self):
        """Test load with no config or IP."""
        result = _load_module(
            RSBC, ['rsbc.py', '--query'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_with_ip(self):
        """Test load with BMC IP credentials."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--query',
             '--bmc_ip', tc.BMC_ADDRESS,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_help(self):
        """Test load with help flag."""
        result = _load_module(
            RSBC, ['rsbc.py', '--help'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_enable(self):
        """Test load with enable flag."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--enable',
             '--bmc_ip', tc.BMC_ADDRESS,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_disable(self):
        """Test load with disable flag."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--disable',
             '--bmc_ip', tc.BMC_ADDRESS,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_service(self):
        """Test load with service flag."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--service',
             '--bmc_ip', tc.BMC_ADDRESS,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_upload(self):
        """Test load with upload flag."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--upload', '/tmp/c.pem',
             '--bmc_ip', tc.BMC_ADDRESS,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_ipv6(self):
        """Test load with IPv6 BMC address."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--query',
             '--bmc_ip', tc.BMC_ADDRESS_IPV6,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_with_config(self):
        """Test load with multi-target config file."""
        cfg = (
            "virtual_media_iso:\n"
            "  t1:\n"
            "    bmc_address: 10.0.0.1\n"
            "    bmc_username: admin\n"
            "    bmc_password: pass\n"
        )
        result = _load_module(
            RSBC,
            ['rsbc.py', '--query',
             '--config', '/tmp/c.yaml'],
            config_data=cfg, exists=True
        )
        self.assertIsNotNone(result)

    def test_single_config(self):
        """Test load with single-target config."""
        cfg = (
            "bmc_address: 10.0.0.1\n"
            "bmc_username: admin\n"
            "bmc_password: pass\n"
        )
        result = _load_module(
            RSBC,
            ['rsbc.py', '--query',
             '--config', '/tmp/c.yaml'],
            config_data=cfg, exists=True
        )
        self.assertIsNotNone(result)

    def test_debug(self):
        """Test load with debug level."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--query', '--debug', '3',
             '--bmc_ip', tc.BMC_ADDRESS,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_bad_args(self):
        """Test load with unknown arguments."""
        result = _load_module(
            RSBC, ['rsbc.py', '--bad_unknown_arg'],
            exists=False
        )
        self.assertIsNotNone(result)

    def test_target_flag(self):
        """Test load with target flag."""
        result = _load_module(
            RSBC,
            ['rsbc.py', '--query',
             '--target', 't1,t2',
             '--bmc_ip', tc.BMC_ADDRESS,
             '--bmc_un', 'a', '--bmc_pw', 'p'],
            exists=False
        )
        self.assertIsNotNone(result)


if __name__ == '__main__':
    unittest.main()
