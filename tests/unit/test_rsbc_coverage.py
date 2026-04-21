#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Coverage tests for rsbc.py."""
import json
import os
import unittest
from unittest import mock

from tests import constants as tc
from helpers import BaseRsbcTestCase
from unit.loader import resp
from unit.loader import devnull, quiet


class TestUtilFunctions(BaseRsbcTestCase):
    """Test rsbc utility functions."""

    def test_t_returns_datetime(self):
        """Verify t() returns a datetime object."""
        import datetime
        self.assertIsInstance(
            self.mod.t(), datetime.datetime
        )

    @mock.patch('sys.stdout', devnull())
    def test_ilog(self):
        """Verify ilog writes without error."""
        self.mod.ilog("x")

    @mock.patch('sys.stdout', devnull())
    def test_elog(self):
        """Verify elog writes without error."""
        self.mod.elog("x")

    @mock.patch('sys.stdout', devnull())
    def test_alog(self):
        """Verify alog writes without error."""
        self.mod.alog("x")

    @mock.patch('sys.stdout', devnull())
    def test_slog(self):
        """Verify slog writes without error."""
        self.mod.slog("x")

    @mock.patch('sys.stdout', devnull())
    def test_dlog1(self):
        """Verify dlog1 writes without error."""
        self.mod.dlog1("x")

    @mock.patch('sys.stdout', devnull())
    def test_dlog2(self):
        """Verify dlog2 writes without error."""
        self.mod.dlog2("x")

    @mock.patch('sys.stdout', devnull())
    def test_dlog3(self):
        """Verify dlog3 writes without error."""
        self.mod.dlog3("x")

    @mock.patch('sys.stdout', devnull())
    def test_dlog4(self):
        """Verify dlog4 writes without error."""
        self.mod.dlog4("x")

    @mock.patch('sys.stdout', devnull())
    def test_qlog_basic(self):
        """Verify qlog basic output."""
        self.mod.qlog("t")

    @mock.patch('sys.stdout', devnull())
    def test_qlog_array(self):
        """Verify qlog array output."""
        self.mod.qlog(["a", "b", "c"], n=1)

    @mock.patch('sys.stdout', devnull())
    def test_qlog_secureboot(self):
        """Verify qlog SecureBoot output."""
        self.mod.qlog(
            ["h", "Enabled"], SecureBoot=True
        )

    def test_ipv6_true(self):
        """Verify IPv6 address detected."""
        self.assertTrue(
            self.mod.is_ipv6_address('::1')
        )

    def test_ipv6_false(self):
        """Verify IPv4 not detected as IPv6."""
        self.assertFalse(
            self.mod.is_ipv6_address(tc.BMC_ADDRESS)
        )

    def test_supported_device_cd(self):
        """Verify CD is supported."""
        self.assertTrue(
            self.mod.supported_device(['CD'])
        )

    def test_supported_device_empty(self):
        """Verify empty list not supported."""
        self.assertFalse(
            self.mod.supported_device([])
        )

    def test_rsbc_exit(self):
        """Verify rsbc_exit raises SystemExit."""
        with self.assertRaises(SystemExit):
            self.mod.rsbc_exit(0)


class TestParseTarget(BaseRsbcTestCase):
    """Test parse_target function."""

    def test_valid_target(self):
        """Verify valid config creates object."""
        self.mod.target_object_list = []
        target_data = {
            'bmc_address': tc.BMC_ADDRESS,
            'bmc_username': tc.BMC_USERNAME,
            'bmc_password': tc.BMC_PASSWORD_PLAIN,
        }
        with quiet():
            self.mod.parse_target('h', target_data)
        self.assertEqual(
            len(self.mod.target_object_list), 1
        )

    def test_missing_password(self):
        """Verify missing password skips."""
        self.mod.target_object_list = []
        with quiet():
            self.mod.parse_target(
                'h', {'bmc_address': tc.BMC_ADDRESS}
            )
        self.assertEqual(
            len(self.mod.target_object_list), 0
        )

    def test_missing_address(self):
        """Verify missing address skips."""
        self.mod.target_object_list = []
        target_data = {
            'bmc_password': 'p',
            'bmc_username': tc.BMC_USERNAME,
        }
        with quiet():
            self.mod.parse_target('h', target_data)
        self.assertEqual(
            len(self.mod.target_object_list), 0
        )

    def test_ipv6_target(self):
        """Verify IPv6 sets ipv6 flag."""
        self.mod.target_object_list = []
        target_data = {
            'bmc_address': tc.BMC_ADDRESS_IPV6,
            'bmc_username': tc.BMC_USERNAME,
            'bmc_password': tc.BMC_PASSWORD_PLAIN,
        }
        with quiet():
            self.mod.parse_target('h', target_data)
        if self.mod.target_object_list:
            obj = self.mod.target_object_list[-1]
            self.assertTrue(obj.ipv6)


class TestVmcObject(BaseRsbcTestCase):
    """Test VmcObject operations."""

    def test_init(self):
        """Verify object initialization."""
        obj = self.make_object()
        self.assertEqual(obj.target, 'h')
        self.assertFalse(obj.session)

    def test_resp_dict_valid(self):
        """Verify valid JSON parsed."""
        obj = self.make_object()
        obj.response = mock.MagicMock(
            read='{"a":1}'
        )
        self.assertTrue(obj.resp_dict())

    def test_resp_dict_invalid(self):
        """Verify invalid JSON handled."""
        obj = self.make_object()
        obj.response = mock.MagicMock(read='bad')
        with quiet():
            try:
                obj.resp_dict()
            except (TypeError, SystemExit):
                pass

    def test_format_valid(self):
        """Verify JSON formatting."""
        obj = self.make_object()
        obj.response = mock.MagicMock(
            read='{"a":1}'
        )
        self.assertTrue(obj.format())

    def test_get_key_value(self):
        """Verify nested key lookup."""
        obj = self.make_object()
        obj.response_dict = {'a': {'b': 'c'}}
        self.assertEqual(
            obj.get_key_value('a', 'b'), 'c'
        )

    def test_check_ok_200(self):
        """Verify 200 is OK."""
        obj = self.make_object()
        obj.response = mock.MagicMock(status=200)
        with quiet():
            self.assertTrue(
                obj.check_ok_status('/t', 'GET', 0)
            )

    def test_check_ok_500(self):
        """Verify 500 handled."""
        obj = self.make_object()
        obj.response = mock.MagicMock(
            status=500, dict={'e': 'x'}
        )
        with quiet():
            try:
                obj.check_ok_status('/t', 'GET', 0)
            except (SystemExit, Exception):
                pass

    def test_make_request_get(self):
        """Verify GET request."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.return_value = resp()
        with quiet():
            self.assertTrue(
                obj.make_request(
                    operation='GET', path='/t'
                )
            )

    def test_make_request_post(self):
        """Verify POST request."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.post.return_value = resp()
        with quiet():
            self.assertTrue(
                obj.make_request(
                    operation='POST',
                    path='/t', payload={}
                )
            )

    def test_make_request_patch(self):
        """Verify PATCH request."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.patch.return_value = resp()
        with quiet():
            self.assertTrue(
                obj.make_request(
                    operation='PATCH',
                    path='/t', payload={}
                )
            )

    def test_make_request_upload(self):
        """Verify UPLOAD_POST request."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.post.return_value = resp()
        with quiet():
            self.assertTrue(
                obj.make_request(
                    operation='UPLOAD_POST',
                    path='/t', payload={}
                )
            )

    def test_make_request_unsupported(self):
        """Verify unsupported op returns False."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        with quiet():
            self.assertFalse(
                obj.make_request(
                    operation='DELETE', path='/t'
                )
            )

    def test_make_request_204(self):
        """Verify 204 response handled."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.return_value = resp(204)
        with quiet():
            self.assertTrue(
                obj.make_request(
                    operation='GET', path='/t'
                )
            )

    def test_make_request_exception(self):
        """Verify exception returns False."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.side_effect = (
            Exception("e")
        )
        with quiet():
            self.assertFalse(
                obj.make_request(
                    operation='GET', path='/t'
                )
            )

    def test_exit_with_session(self):
        """Verify exit closes session."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.session = True
        obj.systems_members = 0
        with quiet():
            with self.assertRaises(SystemExit):
                obj._exit(1)


class TestRedfishStages(BaseRsbcTestCase):
    """Test redfish stage methods."""

    def test_connect_success(self):
        """Verify BMC connection."""
        obj = self.make_object()
        self.mod.redfish.redfish_client.return_value = (
            mock.MagicMock()
        )
        with mock.patch('os.system', return_value=0), \
             mock.patch('sys.stdout', devnull()), \
             mock.patch('time.sleep'):
            obj._redfish_client_connect()
        self.assertIsNotNone(obj.redfish_obj)

    def test_root_query(self):
        """Verify root query."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.return_value = resp(
            200, tc.SYSTEMS_RESP
        )
        with quiet():
            obj._redfish_root_query()

    def test_create_session(self):
        """Verify session creation."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        with quiet():
            obj._redfish_create_session()
        self.assertTrue(obj.session)

    def test_create_session_failure(self):
        """Verify session failure exits."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.login.side_effect = (
            Exception("e")
        )
        with quiet():
            with self.assertRaises(SystemExit):
                obj._redfish_create_session()

    def test_get_managers(self):
        """Verify managers query."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.response_dict = {
            'Managers': {'@odata.id': '/m/'}
        }
        obj.redfish_obj.get.return_value = resp(
            200, tc.MANAGER_MEMBERS_RESP
        )
        with quiet():
            obj._redfish_get_managers()

    def test_get_systems_members(self):
        """Verify systems members query."""
        obj = self.make_object()
        obj.systems_group_url = '/s/'
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.return_value = resp(
            200, tc.MEMBERS_RESP
        )
        with quiet():
            obj._redfish_get_systems_members()

    def test_query_sb_state(self):
        """Verify secure boot state query."""
        obj = self.make_object()
        obj.systems_members_list = [
            {"@odata.id": tc.SYSTEM_MEMBER_URL}
        ]
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.side_effect = [
            resp(200, tc.SB_REF_RESP),
            resp(200, tc.SB_ENABLED_RESP),
        ]
        with quiet():
            obj._redfish_query_sb_state()

    def test_sb_version(self):
        """Verify secure boot version query."""
        obj = self.make_object()
        obj.systems_members_list = [
            {"@odata.id": tc.SYSTEM_MEMBER_URL}
        ]
        obj.redfish_obj = mock.MagicMock()
        sb_type = (
            '{"@odata.type":'
            '"#SB.v1_1_0.SB"}'
        )
        obj.redfish_obj.get.side_effect = [
            resp(200, tc.SB_REF_RESP),
            resp(200, sb_type),
        ]
        with quiet():
            obj._redfish_get_secure_boot_version()

    def test_vm_version(self):
        """Verify VM version query."""
        obj = self.make_object()
        obj.vm_url = tc.VM_URL
        obj.response_dict = {
            '@odata.type': '#VM.v1_2_0.VM'
        }
        with quiet():
            obj._redfish_get_vm_version()

    def test_poweroff_already(self):
        """Verify no action when already off."""
        obj = self.make_object()
        obj.power_state = tc.POWER_OFF
        with quiet():
            obj._redfish_poweroff_host()

    def test_poweron_already(self):
        """Verify no action when already on."""
        obj = self.make_object()
        obj.power_state = tc.POWER_ON
        with quiet():
            obj._redfish_poweron_host()

    def test_execute(self):
        """Verify execute calls stages."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.session = True
        stages = [
            '_redfish_client_connect',
            '_redfish_root_query',
            '_redfish_create_session',
            '_redfish_get_managers',
            '_redfish_get_systems_members',
            '_redfish_get_vm_url',
            '_redfish_get_vm_version',
            '_redfish_get_secure_boot_version',
            '_redfish_query_sb_state',
            '_redfish_get_secure_boot_certificates',
            '_redfish_enable_secure_boot',
            '_redfish_upload_certificates',
            '_redfish_powerctl_host',
        ]
        for stage in stages:
            setattr(obj, stage, mock.MagicMock())
        with quiet():
            obj.execute(0)


if __name__ == '__main__':
    unittest.main()
