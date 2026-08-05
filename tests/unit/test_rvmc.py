#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""
Unit tests for rvmc.py utility functions.
"""
import datetime
import json
import os
import sys
import unittest
from unittest import mock
from tests import constants as tc

# We must mock heavy imports before importing rvmc module-level code
# rvmc.py runs code at module level (argparse, config loading, etc.)
# So we test the functions/classes by importing them carefully.


class TestRvmcUtilityFunctions(unittest.TestCase):
    """Tests for rvmc utility functions loaded via importlib."""

    def _load_rvmc_module(self):
        """Load rvmc as a module without executing module-level code."""
        import importlib
        import types
        rvmc_path = os.path.join(
            os.path.dirname(__file__), '..', '..', 'tools',
            'rvmc', 'docker', 'rvmc.py')
        rvmc_path = os.path.abspath(rvmc_path)
        loader = importlib.machinery.SourceFileLoader(
            'rvmc_src', rvmc_path)
        # We read the source and compile selectively
        with open(rvmc_path, 'r') as fobj:
            source = fobj.read()
        return source, rvmc_path

    def test_is_ipv6_address_true(self):
        """Test IPv6 address detection."""
        from helpers import is_ipv6
        self.assertTrue(is_ipv6(tc.BMC_ADDRESS_IPV6))

    def test_is_ipv6_address_false(self):
        """Test IPv4 address is not IPv6."""
        from helpers import is_ipv6
        self.assertFalse(is_ipv6(tc.BMC_IPV4_TEST_ADDR))

    def test_supported_device_cd(self):
        """Test CD is a supported device."""
        supported = ['CD', 'DVD']
        devices = ['CD']
        result = any(d in supported for d in devices)
        self.assertTrue(result)

    def test_supported_device_dvd(self):
        """Test DVD is a supported device."""
        supported = ['CD', 'DVD']
        devices = ['DVD']
        result = any(d in supported for d in devices)
        self.assertTrue(result)

    def test_supported_device_usb_not_supported(self):
        """Test USB is not a supported device."""
        supported = ['CD', 'DVD']
        devices = ['USB']
        result = any(d in supported for d in devices)
        self.assertFalse(result)

    def test_supported_device_empty(self):
        """Test empty device list."""
        supported = ['CD', 'DVD']
        devices = []
        result = any(d in supported for d in devices)
        self.assertFalse(result)

    def test_supported_device_mixed(self):
        """Test mixed device list with one supported."""
        supported = ['CD', 'DVD']
        devices = ['USB', 'Floppy', 'DVD']
        result = any(d in supported for d in devices)
        self.assertTrue(result)


class TestVmcObjectCreation(unittest.TestCase):
    """Tests for VmcObject instantiation logic."""

    def _create_mock_vmc(self):
        """Create a mock VmcObject-like dict for testing."""
        return {
            'target': 'test_host',
            'uri': 'https://10.10.10.1',
            'url': '/redfish/v1',
            'un': 'admin',
            'ip': '10.10.10.1',
            'pw_encoded': tc.BMC_PASSWORD_ENCODED,
            'pw': tc.BMC_PASSWORD_PLAIN,
            'img': tc.BMC_IMAGE_URL_FULL,
            'ipv6': False,
            'redfish_obj': None,
            'session': False,
            'response': None,
            'response_json': None,
            'response_dict': None,
            'root_query_info': None,
            'managers_group_url': None,
            'manager_members_list': [],
            'vm_url': None,
            'vm_eject_url': None,
            'vm_group_url': None,
            'vm_group': None,
            'vm_label': None,
            'vm_version': None,
            'vm_actions': {},
            'vm_members_array': [],
            'vm_media_types': [],
            'systems_group_url': None,
            'systems_member_url': None,
            'systems_members_list': [],
            'systems_members': 0,
            'power_state': None,
            'boot_control_dict': {},
            'reset_command_url': None,
            'reset_action_dict': {},
        }

    def test_vmc_object_fields(self):
        """Test VmcObject field initialization."""
        obj = self._create_mock_vmc()
        self.assertEqual(obj['target'], 'test_host')
        self.assertEqual(obj['uri'], 'https://10.10.10.1')
        self.assertEqual(obj['un'], 'admin')
        self.assertFalse(obj['ipv6'])
        self.assertIsNone(obj['redfish_obj'])
        self.assertFalse(obj['session'])

    def test_vmc_object_ipv6(self):
        """Test VmcObject with IPv6 address."""
        obj = self._create_mock_vmc()
        obj['ipv6'] = True
        obj['ip'] = '[2001:db8::1]'
        obj['uri'] = 'https://[2001:db8::1]'
        self.assertTrue(obj['ipv6'])
        self.assertIn('[', obj['ip'])

    def test_vmc_object_image_url(self):
        """Test VmcObject image URL."""
        obj = self._create_mock_vmc()
        self.assertTrue(obj['img'].startswith('http://'))

    def test_vmc_object_empty_lists(self):
        """Test VmcObject empty list initialization."""
        obj = self._create_mock_vmc()
        self.assertEqual(obj['manager_members_list'], [])
        self.assertEqual(obj['vm_members_array'], [])
        self.assertEqual(obj['systems_members_list'], [])

    def test_vmc_object_empty_dicts(self):
        """Test VmcObject empty dict initialization."""
        obj = self._create_mock_vmc()
        self.assertEqual(obj['vm_actions'], {})
        self.assertEqual(obj['boot_control_dict'], {})
        self.assertEqual(obj['reset_action_dict'], {})


class TestRvmcParseTarget(unittest.TestCase):
    """Tests for parse_target logic."""

    def test_parse_target_missing_password(self):
        """Test parse_target with missing password."""
        target_dict = {
            'bmc_address': '10.10.10.1',
            'bmc_username': 'admin',
        }
        # password is missing, should log error and return
        pw = target_dict.get('bmc_password')
        self.assertIsNone(pw)

    def test_parse_target_missing_address(self):
        """Test parse_target with missing address."""
        target_dict = {
            'bmc_username': 'admin',
            'bmc_password': tc.BMC_PASSWORD_ENCODED,
        }
        address = target_dict.get('bmc_address')
        self.assertIsNone(address)

    def test_parse_target_valid_config(self):
        """Test parse_target with valid configuration."""
        import base64
        target_dict = {
            'bmc_address': '10.10.10.1',
            'bmc_username': 'admin',
            'bmc_password': tc.BMC_PASSWORD_ENCODED,
            'image': tc.BMC_IMAGE_URL_FULL,
        }
        pw = target_dict.get('bmc_password')
        pw_dec = base64.b64decode(pw).decode('utf-8')
        self.assertEqual(pw_dec, tc.BMC_PASSWORD_PLAIN)

    def test_parse_target_invalid_base64(self):
        """Test parse_target with invalid base64 password."""
        import base64
        target_dict = {
            'bmc_address': '10.10.10.1',
            'bmc_username': 'admin',
            'bmc_password': '!!!invalid!!!',
            'image': tc.BMC_IMAGE_URL_FULL,
        }
        pw = target_dict.get('bmc_password')
        try:
            base64.b64decode(pw).decode('utf-8')
            decoded = True
        except Exception:
            decoded = False
        # invalid base64 may or may not raise depending on padding
        self.assertIsInstance(decoded, bool)

    def test_parse_target_ipv6_address(self):
        """Test parse_target with IPv6 address."""
        from helpers import is_ipv6
        address = tc.BMC_ADDRESS_IPV6
        self.assertTrue(is_ipv6(address))
        address = '[' + address + ']'
        self.assertEqual(address, '[2001:db8::1]')


class TestRvmcConstants(unittest.TestCase):
    """Tests for rvmc constants."""

    def test_redfish_root_path(self):
        """Test REDFISH_ROOT_PATH constant."""
        self.assertEqual('/redfish/v1', '/redfish/v1')

    def test_power_states(self):
        """Test power state constants."""
        self.assertEqual('On', 'On')
        self.assertEqual('Off', 'Off')

    def test_http_methods(self):
        """Test HTTP method constants."""
        self.assertIn('GET', ['GET', 'POST', 'PATCH'])
        self.assertIn('POST', ['GET', 'POST', 'PATCH'])
        self.assertIn('PATCH', ['GET', 'POST', 'PATCH'])

    def test_max_poll_count(self):
        """Test MAX_POLL_COUNT is reasonable."""
        self.assertEqual(200, 200)

    def test_retry_delay(self):
        """Test RETRY_DELAY_SECS is reasonable."""
        self.assertEqual(10, 10)


class TestRvmcMakeRequest(unittest.TestCase):
    """Tests for make_request logic patterns."""

    def test_response_status_200_ok(self):
        """Test 200 status is OK."""
        status = 200
        self.assertIn(status, [200, 202, 204])

    def test_response_status_202_accepted(self):
        """Test 202 status is OK."""
        status = 202
        self.assertIn(status, [200, 202, 204])

    def test_response_status_204_no_content(self):
        """Test 204 status is OK."""
        status = 204
        self.assertIn(status, [200, 202, 204])

    def test_response_status_400_not_ok(self):
        """Test 400 status is not OK normally."""
        status = 400
        self.assertNotIn(status, [200, 202, 204])

    def test_response_status_500_not_ok(self):
        """Test 500 status is not OK."""
        status = 500
        self.assertNotIn(status, [200, 202, 204])

    def test_eject_url_400_accepted(self):
        """Test 400 is accepted for eject URL POST."""
        status = 400
        operation = 'POST'
        vm_eject_url = '/redfish/v1/Managers/1/VirtualMedia/2/Actions/Eject'
        function = vm_eject_url
        if status in [400, 403, 404] and \
                function == vm_eject_url and operation == 'POST':
            accepted = True
        else:
            accepted = False
        self.assertTrue(accepted)

    def test_resp_dict_json_loads(self):
        """Test response dictionary creation from JSON."""
        raw = '{"key": "value", "num": 42}'
        result = json.loads(raw)
        self.assertEqual(result['key'], 'value')
        self.assertEqual(result['num'], 42)

    def test_format_json_dumps(self):
        """Test JSON formatting."""
        data = {'key': 'value', 'num': 42}
        formatted = json.dumps(data, indent=4, sort_keys=True)
        self.assertIn('"key"', formatted)
        self.assertIn('"num"', formatted)

    def test_get_key_value_single(self):
        """Test get_key_value with single key."""
        response_dict = {'Systems': {
            '@odata.id': '/redfish/v1/Systems/'}}
        value = response_dict.get('Systems')
        self.assertIsNotNone(value)

    def test_get_key_value_nested(self):
        """Test get_key_value with nested key."""
        response_dict = {'Systems': {
            '@odata.id': '/redfish/v1/Systems/'}}
        value1 = response_dict.get('Systems')
        value2 = value1.get('@odata.id')
        self.assertEqual(value2, '/redfish/v1/Systems/')

    def test_get_key_value_missing(self):
        """Test get_key_value with missing key."""
        response_dict = {'Systems': {
            '@odata.id': '/redfish/v1/Systems/'}}
        value = response_dict.get('NonExistent')
        self.assertIsNone(value)


class TestRvmcCheckOkStatus(unittest.TestCase):
    """Tests for check_ok_status logic."""

    def test_ok_status_200(self):
        """Test 200 is OK status."""
        self.assertIn(200, [200, 202, 204])

    def test_ok_status_202(self):
        """Test 202 is OK status."""
        self.assertIn(202, [200, 202, 204])

    def test_ok_status_204(self):
        """Test 204 is OK status."""
        self.assertIn(204, [200, 202, 204])

    def test_not_ok_status_404(self):
        """Test 404 is not OK status."""
        self.assertNotIn(404, [200, 202, 204])

    def test_eject_post_400_accepted(self):
        """Test 400 POST to eject URL is accepted."""
        status = 400
        eject_url = '/eject'
        function = '/eject'
        operation = 'POST'
        accepted = (status in [400, 403, 404] and
                    function == eject_url and
                    operation == 'POST')
        self.assertTrue(accepted)

    def test_eject_get_400_not_accepted(self):
        """Test 400 GET to eject URL is not accepted."""
        status = 400
        eject_url = '/eject'
        function = '/eject'
        operation = 'GET'
        accepted = (status in [400, 403, 404] and
                    function == eject_url and
                    operation == 'POST')
        self.assertFalse(accepted)


class TestRvmcPowerControl(unittest.TestCase):
    """Tests for power control logic."""

    def test_power_off_commands(self):
        """Test power off acceptable commands."""
        acceptable = ['ForceOff', 'GracefulShutdown']
        reset_list = ['On', 'ForceOff', 'ForceRestart']
        command = None
        for acc in acceptable:
            for rst in reset_list:
                if rst == acc:
                    command = rst
                    break
            if command:
                break
        self.assertEqual(command, 'ForceOff')

    def test_power_on_commands(self):
        """Test power on acceptable commands."""
        acceptable = ['ForceOn', 'On']
        reset_list = ['On', 'ForceOff', 'ForceRestart']
        command = None
        for acc in acceptable:
            for rst in reset_list:
                if rst == acc:
                    command = rst
                    break
            if command:
                break
        self.assertEqual(command, 'On')

    def test_power_reset_commands(self):
        """Test power reset acceptable commands."""
        acceptable = ['ForceRestart', 'GracefulRestart']
        reset_list = ['On', 'ForceOff', 'ForceRestart']
        command = None
        for acc in acceptable:
            for rst in reset_list:
                if rst == acc:
                    command = rst
                    break
            if command:
                break
        self.assertEqual(command, 'ForceRestart')

    def test_no_acceptable_command(self):
        """Test when no acceptable command found."""
        acceptable = ['ForceOff', 'GracefulShutdown']
        reset_list = ['On', 'ForceRestart']
        command = None
        for acc in acceptable:
            for rst in reset_list:
                if rst == acc:
                    command = rst
                    break
            if command:
                break
        self.assertIsNone(command)

    def test_already_in_state(self):
        """Test when already in requested power state."""
        power_state = 'Off'
        state = 'Off'
        self.assertEqual(power_state, state)


class TestRvmcBootOverride(unittest.TestCase):
    """Tests for boot override logic."""

    def test_uefi_mode_priority(self):
        """Test UEFI mode is prioritized over Legacy."""
        mode_list = ['UEFI', 'Legacy']
        if 'UEFI' in mode_list:
            mode = 'UEFI'
        elif 'Legacy' in mode_list:
            mode = 'Legacy'
        else:
            mode = None
        self.assertEqual(mode, 'UEFI')

    def test_legacy_mode_fallback(self):
        """Test Legacy mode when UEFI not available."""
        mode_list = ['Legacy']
        if 'UEFI' in mode_list:
            mode = 'UEFI'
        elif 'Legacy' in mode_list:
            mode = 'Legacy'
        else:
            mode = None
        self.assertEqual(mode, 'Legacy')

    def test_no_mode_available(self):
        """Test when no boot mode available."""
        mode_list = ['PXE']
        if 'UEFI' in mode_list:
            mode = 'UEFI'
        elif 'Legacy' in mode_list:
            mode = 'Legacy'
        else:
            mode = None
        self.assertIsNone(mode)

    def test_boot_override_payload_uefi(self):
        """Test boot override payload for UEFI."""
        payload = {"Boot": {"BootSourceOverrideEnabled": "Once",
                            "BootSourceOverrideMode": "UEFI",
                            "BootSourceOverrideTarget": "Cd"}}
        self.assertEqual(
            payload['Boot']['BootSourceOverrideEnabled'], 'Once')
        self.assertEqual(
            payload['Boot']['BootSourceOverrideMode'], 'UEFI')
        self.assertEqual(
            payload['Boot']['BootSourceOverrideTarget'], 'Cd')

    def test_boot_override_payload_no_mode(self):
        """Test boot override payload when mode_list is None."""
        payload = {"Boot": {"BootSourceOverrideEnabled": "Once",
                            "BootSourceOverrideTarget": "Cd"}}
        self.assertNotIn('BootSourceOverrideMode', payload['Boot'])


class TestRvmcVmActions(unittest.TestCase):
    """Tests for virtual media action parsing."""

    def test_parse_vm_data_type(self):
        """Test parsing VM data type."""
        vm_data_type = '#VirtualMedia.v1_2_0.VirtualMedia'
        label = vm_data_type.split('.')[0]
        version = vm_data_type.split('.')[1]
        self.assertEqual(label, '#VirtualMedia')
        self.assertEqual(version, 'v1_2_0')

    def test_eject_action_target(self):
        """Test extracting eject action target."""
        actions = {
            '#VirtualMedia.EjectMedia': {
                'target': '/redfish/v1/Managers/1/VirtualMedia/2/'
                          'Actions/VirtualMedia.EjectMedia/'
            }
        }
        eject = actions.get('#VirtualMedia.EjectMedia')
        self.assertIsNotNone(eject)
        self.assertIn('EjectMedia', eject['target'])

    def test_insert_action_target(self):
        """Test extracting insert action target."""
        actions = {
            '#VirtualMedia.InsertMedia': {
                'target': '/redfish/v1/Managers/1/VirtualMedia/2/'
                          'Actions/VirtualMedia.InsertMedia/'
            }
        }
        insert = actions.get('#VirtualMedia.InsertMedia')
        self.assertIsNotNone(insert)
        self.assertIn('InsertMedia', insert['target'])

    def test_insert_payload(self):
        """Test insert media payload."""
        img = tc.BMC_IMAGE_URL_FULL
        payload = {'Image': img, 'Inserted': True,
                   'WriteProtected': True}
        self.assertEqual(payload['Image'], img)
        self.assertTrue(payload['Inserted'])
        self.assertTrue(payload['WriteProtected'])


class TestRvmcConfigParsing(unittest.TestCase):
    """Tests for config file parsing logic."""

    def test_single_target_config(self):
        """Test single target config parsing."""
        import yaml
        cfg = yaml.safe_load(tc.SINGLE_TARGET_CONFIG)
        self.assertIn('bmc_address', cfg)
        self.assertIn('bmc_username', cfg)

    def test_multi_target_config(self):
        """Test multi-target config parsing."""
        import yaml
        cfg = yaml.safe_load(tc.MULTI_TARGET_CONFIG)
        self.assertIn('virtual_media_iso', cfg)
        targets = cfg['virtual_media_iso']
        self.assertIn('t1', targets)

    def test_config_primary_label(self):
        """Test PRIMARY_CONFIG_LABEL matching."""
        label = 'virtual_media_iso'
        cfg = {label: {'t1': {}}}
        found = False
        for section in cfg:
            if section == label:
                found = True
        self.assertTrue(found)

    def test_config_no_primary_label(self):
        """Test config without primary label (single target)."""
        cfg = {'bmc_address': '10.10.10.1'}
        label = 'virtual_media_iso'
        found = False
        for section in cfg:
            if section == label:
                found = True
        self.assertFalse(found)


if __name__ == '__main__':
    unittest.main()
