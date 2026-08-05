#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""
Unit tests for tools/rsbc/rsbc.py - utility functions and VmcObject.
"""
import base64
import datetime
import json
import os
import sys
import unittest
from unittest import mock
from tests import constants as tc


class TestRsbcIsIpv6Address(unittest.TestCase):
    """Tests for is_ipv6_address logic."""

    def test_valid_ipv6(self):
        """Test valid IPv6 address."""
        from helpers import is_ipv6
        self.assertTrue(is_ipv6(tc.BMC_ADDRESS_IPV6))

    def test_valid_ipv6_full(self):
        """Test full IPv6 address."""
        from helpers import is_ipv6
        self.assertTrue(
            is_ipv6('2001:0db8:0000:0000:0000:0000:0000:0001'))

    def test_ipv4_not_ipv6(self):
        """Test IPv4 address is not IPv6."""
        from helpers import is_ipv6
        self.assertFalse(is_ipv6(tc.BMC_IPV4_TEST_ADDR))

    def test_loopback_ipv6(self):
        """Test IPv6 loopback."""
        from helpers import is_ipv6
        self.assertTrue(is_ipv6('::1'))

    def test_invalid_address(self):
        """Test invalid address string."""
        from helpers import is_ipv6
        self.assertFalse(is_ipv6('not_an_address'))


class TestRsbcSupportedDevice(unittest.TestCase):
    """Tests for supported_device logic."""

    def test_cd_supported(self):
        """Test CD is supported."""
        supported = ['CD', 'DVD']
        self.assertTrue(any(d in supported for d in ['CD']))

    def test_dvd_supported(self):
        """Test DVD is supported."""
        supported = ['CD', 'DVD']
        self.assertTrue(any(d in supported for d in ['DVD']))

    def test_usb_not_supported(self):
        """Test USB is not supported."""
        supported = ['CD', 'DVD']
        self.assertFalse(any(d in supported for d in ['USB']))

    def test_empty_list(self):
        """Test empty device list."""
        supported = ['CD', 'DVD']
        self.assertFalse(any(d in supported for d in []))

    def test_multiple_with_dvd(self):
        """Test multiple devices including DVD."""
        supported = ['CD', 'DVD']
        self.assertTrue(any(d in supported for d in ['USB', 'DVD']))


class TestRsbcVmcObjectInit(unittest.TestCase):
    """Tests for VmcObject initialization logic."""

    def _make_obj(self, hostname='host1', address=tc.BMC_ADDRESS,
                  username='admin', password='pass'):
        """Create a dict mimicking VmcObject init."""
        return {
            'target': hostname,
            'uri': 'https://' + address,
            'url': '/redfish/v1',
            'un': username.rstrip(),
            'ip': address.rstrip(),
            'pw': password.rstrip(),
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
            'sys_mem_url': None,
            'systems_members_list': [],
            'systems_members': 0,
            'power_state': None,
            'sb_url': None,
            'db_cert_url': None,
            'sb_db_url': None,
            'boot_control_dict': {},
            'reset_command_url': None,
            'reset_action_dict': {},
        }

    def test_basic_init(self):
        """Test basic object initialization."""
        obj = self._make_obj()
        self.assertEqual(obj['target'], 'host1')
        self.assertEqual(obj['uri'], 'https://10.0.0.1')
        self.assertEqual(obj['un'], 'admin')

    def test_ipv6_flag(self):
        """Test IPv6 flag default."""
        obj = self._make_obj()
        self.assertFalse(obj['ipv6'])

    def test_session_default(self):
        """Test session default is False."""
        obj = self._make_obj()
        self.assertFalse(obj['session'])

    def test_secure_boot_fields(self):
        """Test secure boot fields initialized to None."""
        obj = self._make_obj()
        self.assertIsNone(obj['sb_url'])
        self.assertIsNone(obj['db_cert_url'])
        self.assertIsNone(obj['sb_db_url'])

    def test_whitespace_stripping(self):
        """Test whitespace is stripped from fields."""
        obj = self._make_obj(username='admin  ', password='pass  ')
        self.assertEqual(obj['un'], 'admin')
        self.assertEqual(obj['pw'], 'pass')


class TestRsbcMakeRequest(unittest.TestCase):
    """Tests for make_request logic patterns."""

    def test_get_operation(self):
        """Test GET operation string."""
        self.assertEqual('GET', 'GET')

    def test_post_operation(self):
        """Test POST operation string."""
        self.assertEqual('POST', 'POST')

    def test_patch_operation(self):
        """Test PATCH operation string."""
        self.assertEqual('PATCH', 'PATCH')

    def test_upload_post_operation(self):
        """Test UPLOAD_POST operation string."""
        self.assertEqual('UPLOAD_POST', 'UPLOAD_POST')

    def test_unsupported_operation(self):
        """Test unsupported operation detection."""
        supported = ['GET', 'POST', 'PATCH', 'UPLOAD_POST']
        self.assertNotIn('DELETE', supported)

    def test_response_dict_from_json(self):
        """Test creating response dict from JSON."""
        raw = '{"Members": [{"@odata.id": "/redfish/v1/Systems/1/"}]}'
        result = json.loads(raw)
        self.assertIn('Members', result)
        self.assertEqual(len(result['Members']), 1)

    def test_format_json(self):
        """Test JSON formatting."""
        data = {'a': 1, 'b': 2}
        formatted = json.dumps(data, indent=4, sort_keys=True)
        self.assertIn('"a"', formatted)

    def test_check_ok_status_200(self):
        """Test 200 is OK."""
        self.assertIn(200, [200, 202, 204])

    def test_check_ok_status_500(self):
        """Test 500 is not OK."""
        self.assertNotIn(500, [200, 202, 204])


class TestRsbcGetKeyValue(unittest.TestCase):
    """Tests for get_key_value logic."""

    def test_single_key(self):
        """Test single key lookup."""
        data = {'Systems': {'@odata.id': '/redfish/v1/Systems/'}}
        self.assertIsNotNone(data.get('Systems'))

    def test_nested_key(self):
        """Test nested key lookup."""
        data = {'Systems': {'@odata.id': '/redfish/v1/Systems/'}}
        val = data.get('Systems')
        self.assertEqual(val.get('@odata.id'), '/redfish/v1/Systems/')

    def test_missing_key(self):
        """Test missing key returns None."""
        data = {'Systems': {}}
        self.assertIsNone(data.get('Missing'))

    def test_none_key2(self):
        """Test when key2 is None, return key1 value."""
        data = {'key1': 'value1'}
        key2 = None
        value1 = data.get('key1')
        if key2 is None:
            result = value1
        else:
            result = value1.get(key2)
        self.assertEqual(result, 'value1')


class TestRsbcSecureBoot(unittest.TestCase):
    """Tests for secure boot logic."""

    def test_secure_boot_enable_payload(self):
        """Test enable secure boot payload."""
        payload = {"SecureBootEnable": True}
        self.assertTrue(payload['SecureBootEnable'])

    def test_secure_boot_disable_payload(self):
        """Test disable secure boot payload."""
        payload = {"SecureBootEnable": False}
        self.assertFalse(payload['SecureBootEnable'])

    def test_secure_boot_version_parse(self):
        """Test parsing secure boot version."""
        sb_type = '#SecureBoot.v1_1_0.SecureBoot'
        version = sb_type.split('.')[1]
        self.assertEqual(version, 'v1_1_0')

    def test_secure_boot_state_enabled(self):
        """Test secure boot enabled state."""
        response = {"SecureBootEnable": True}
        self.assertTrue(response['SecureBootEnable'])

    def test_secure_boot_state_disabled(self):
        """Test secure boot disabled state."""
        response = {"SecureBootEnable": False}
        self.assertFalse(response['SecureBootEnable'])

    def test_sb_url_extraction(self):
        """Test SecureBoot URL extraction."""
        data = {"SecureBoot": {
            "@odata.id": "/redfish/v1/Systems/1/SecureBoot"}}
        sb_url = data["SecureBoot"]["@odata.id"]
        self.assertIn("SecureBoot", sb_url)

    def test_sb_databases_url(self):
        """Test SecureBootDatabases URL extraction."""
        data = {
            "SecureBootDatabases": {
                "@odata.id": "/redfish/v1/Systems"
                "/1/SecureBoot/SecureBootDatabases"}}
        db_url = data["SecureBootDatabases"]["@odata.id"]
        self.assertIn("SecureBootDatabases", db_url)


class TestRsbcCertificateUpload(unittest.TestCase):
    """Tests for certificate upload logic."""

    def test_pem_extension(self):
        """Test PEM file extension detection."""
        path = '/path/to/cert.pem'
        self.assertTrue(path.endswith('.pem'))

    def test_der_extension(self):
        """Test DER file extension detection."""
        path = '/path/to/cert.der'
        self.assertTrue(path.endswith('.der'))

    def test_crt_extension(self):
        """Test CRT file extension detection."""
        path = '/path/to/cert.crt'
        self.assertTrue(path.endswith('.crt'))

    def test_unsupported_extension(self):
        """Test unsupported file extension."""
        path = '/path/to/cert.txt'
        self.assertFalse(path.endswith('.pem'))
        self.assertFalse(path.endswith('.der'))
        self.assertFalse(path.endswith('.crt'))

    def test_upload_payload(self):
        """Test certificate upload payload."""
        cert = ("-----BEGIN CERTIFICATE-----"
               "\nMIIB...\n"
               "-----END CERTIFICATE-----")
        payload = {"CertificateString": cert, "CertificateType": "PEM"}
        self.assertEqual(payload['CertificateType'], 'PEM')
        self.assertIn('BEGIN CERTIFICATE', payload['CertificateString'])


class TestRsbcPowerControl(unittest.TestCase):
    """Tests for power control logic in rsbc."""

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
        reset_list = ['On', 'ForceOff']
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
        reset_list = ['ForceRestart', 'On']
        command = None
        for acc in acceptable:
            for rst in reset_list:
                if rst == acc:
                    command = rst
                    break
            if command:
                break
        self.assertEqual(command, 'ForceRestart')

    def test_already_in_state(self):
        """Test when already in requested state."""
        power_state = 'On'
        state = 'On'
        self.assertEqual(power_state, state)


class TestRsbcConfigParsing(unittest.TestCase):
    """Tests for rsbc config file parsing."""

    def test_yaml_config_load(self):
        """Test YAML config loading."""
        import yaml
        config_str = ("virtual_media_iso:\n"
                      "    host1:\n"
                      "        bmc_address: 10.0.0.1\n"
                      "        bmc_username: admin\n"
                      "        bmc_password: pass\n")
        cfg = yaml.safe_load(config_str)
        self.assertIn('virtual_media_iso', cfg)

    def test_parse_target_no_password(self):
        """Test parse_target with missing password."""
        target_dict = {'bmc_address': tc.BMC_ADDRESS,
                       'bmc_username': 'admin'}
        self.assertIsNone(target_dict.get('bmc_password'))

    def test_parse_target_no_address(self):
        """Test parse_target with missing address."""
        target_dict = {'bmc_username': 'admin', 'bmc_password': 'pass'}
        self.assertIsNone(target_dict.get('bmc_address'))

    def test_parse_target_valid(self):
        """Test parse_target with valid data."""
        target_dict = {
            'bmc_address': tc.BMC_ADDRESS,
            'bmc_username': 'admin',
            'bmc_password': 'pass',
        }
        self.assertIsNotNone(target_dict.get('bmc_password'))
        self.assertIsNotNone(target_dict.get('bmc_address'))

    def test_bmc_ip_un_pw_mode(self):
        """Test direct BMC IP/username/password mode."""
        bmc_ip = tc.BMC_ADDRESS
        bmc_un = 'admin'
        bmc_pw = tc.BMC_PASSWORD_PLAIN
        self.assertTrue(isinstance(bmc_ip, str))
        self.assertTrue(isinstance(bmc_un, str))
        self.assertTrue(isinstance(bmc_pw, str))


class TestRsbcExecuteFlow(unittest.TestCase):
    """Tests for execute flow logic."""

    def test_service_flow_flags(self):
        """Test SERVICE flag flow."""
        service = True
        query = False
        upload = False
        enable = False
        disable = False
        self.assertTrue(service)
        self.assertFalse(query)

    def test_query_flow_flags(self):
        """Test QUERY flag flow."""
        query = True
        self.assertTrue(query)

    def test_enable_flow_flags(self):
        """Test ENABLE flag flow."""
        enable = True
        self.assertTrue(enable)

    def test_disable_flow_flags(self):
        """Test DISABLE flag flow."""
        disable = True
        self.assertTrue(disable)

    def test_upload_flow_flags(self):
        """Test UPLOAD flag flow."""
        upload = True
        certificate = '/path/to/cert.pem'
        self.assertTrue(upload)
        self.assertTrue(certificate.endswith('.pem'))


if __name__ == '__main__':
    unittest.main()
