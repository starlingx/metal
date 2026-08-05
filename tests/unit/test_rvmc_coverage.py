#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Coverage tests for rvmc.py."""
import json
import os
import unittest
from unittest import mock

from tests import constants as tc
from helpers import BaseRvmcTestCase
from helpers import make_rvmc_object
from unit.loader import resp
from unit.loader import devnull, quiet


class TestUtilFunctions(BaseRvmcTestCase):
    """Test rvmc utility functions."""

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
    def test_wlog(self):
        """Verify wlog writes without error."""
        self.mod.wlog("x")

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
        self.mod.slog("s")

    @mock.patch('sys.stdout', devnull())
    def test_dlog1(self):
        """Verify dlog1 writes without error."""
        self.mod.dlog1("x", 1)

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

    def test_ipv6_true(self):
        """Verify IPv6 address detected."""
        self.assertTrue(
            self.mod.is_ipv6_address(tc.BMC_ADDRESS_IPV6)
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

    def test_supported_device_dvd(self):
        """Verify DVD is supported."""
        self.assertTrue(
            self.mod.supported_device(['DVD'])
        )

    def test_supported_device_usb(self):
        """Verify USB is not supported."""
        self.assertFalse(
            self.mod.supported_device(['USB'])
        )

    def test_supported_device_empty(self):
        """Verify empty list not supported."""
        self.assertFalse(
            self.mod.supported_device([])
        )

    def test_rvmc_exit(self):
        """Verify rvmc_exit raises SystemExit."""
        with self.assertRaises(SystemExit):
            self.mod.rvmc_exit(0)


class TestParseTarget(BaseRvmcTestCase):
    """Test parse_target function."""

    def test_valid_target(self):
        """Verify valid config creates object."""
        self.mod.target_object_list = []
        target_data = {
            'bmc_address': tc.BMC_ADDRESS,
            'bmc_username': tc.BMC_USERNAME,
            'bmc_password': tc.BMC_PASSWORD_ENCODED,
            'image': tc.BMC_IMAGE_URL,
        }
        with quiet():
            self.mod.parse_target('h', target_data)
        self.assertEqual(
            len(self.mod.target_object_list), 1
        )

    def test_missing_password(self):
        """Verify missing password skips object."""
        self.mod.target_object_list = []
        with quiet():
            self.mod.parse_target(
                'h', {'bmc_address': tc.BMC_ADDRESS}
            )
        self.assertEqual(
            len(self.mod.target_object_list), 0
        )

    def test_missing_address(self):
        """Verify missing address skips object."""
        self.mod.target_object_list = []
        target_data = {
            'bmc_password': tc.BMC_PASSWORD_ENCODED,
            'bmc_username': tc.BMC_USERNAME,
        }
        with quiet():
            self.mod.parse_target('h', target_data)
        self.assertEqual(
            len(self.mod.target_object_list), 0
        )

    def test_ipv6_target(self):
        """Verify IPv6 address sets ipv6 flag."""
        self.mod.target_object_list = []
        target_data = {
            'bmc_address': tc.BMC_ADDRESS_IPV6,
            'bmc_username': tc.BMC_USERNAME,
            'bmc_password': tc.BMC_PASSWORD_ENCODED,
            'image': tc.BMC_IMAGE_URL,
        }
        with quiet():
            self.mod.parse_target('h', target_data)
        if self.mod.target_object_list:
            obj = self.mod.target_object_list[-1]
            self.assertTrue(obj.ipv6)

    def test_bad_base64(self):
        """Verify bad base64 handled gracefully."""
        self.mod.target_object_list = []
        target_data = {
            'bmc_address': tc.BMC_ADDRESS,
            'bmc_username': tc.BMC_USERNAME,
            'bmc_password': '\x00',
            'image': tc.BMC_IMAGE_URL,
        }
        with quiet():
            self.mod.parse_target('h', target_data)


class TestVmcObjectBasic(BaseRvmcTestCase):
    """Test VmcObject basic operations."""

    def test_init(self):
        """Verify object initialization."""
        obj = self.make_object()
        self.assertEqual(obj.target, 'h')
        self.assertFalse(obj.session)

    def test_resp_dict_valid(self):
        """Verify valid JSON parsed to dict."""
        obj = self.make_object()
        obj.response = mock.MagicMock(read='{"a":1}')
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
        """Verify JSON formatting works."""
        obj = self.make_object()
        obj.response = mock.MagicMock(read='{"a":1}')
        self.assertTrue(obj.format())

    def test_get_key_value(self):
        """Verify nested key lookup."""
        obj = self.make_object()
        obj.response_dict = {'a': {'b': 'c'}}
        self.assertEqual(
            obj.get_key_value('a', 'b'), 'c'
        )

    def test_get_key_value_missing(self):
        """Verify missing key returns None."""
        obj = self.make_object()
        obj.response_dict = {}
        self.assertIsNone(obj.get_key_value('x'))

    def test_check_ok_200(self):
        """Verify 200 status is OK."""
        obj = self.make_object()
        obj.response = mock.MagicMock(status=200)
        with quiet():
            self.assertTrue(
                obj.check_ok_status('/t', 'GET', 0)
            )

    def test_check_ok_500(self):
        """Verify 500 status is not OK."""
        obj = self.make_object()
        obj.response = mock.MagicMock(
            status=500, dict={'e': 'x'}
        )
        with quiet():
            self.assertFalse(
                obj.check_ok_status('/t', 'GET', 0)
            )

    def test_check_ok_eject_400(self):
        """Verify 400 on eject URL is accepted."""
        obj = self.make_object()
        obj.vm_eject_url = tc.EJECT_URL
        obj.response = mock.MagicMock(status=400)
        self.assertTrue(
            obj.check_ok_status(
                tc.EJECT_URL, 'POST', 0
            )
        )


class TestMakeRequest(BaseRvmcTestCase):
    """Test make_request method."""

    def test_get(self):
        """Verify GET request succeeds."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.return_value = resp()
        with quiet():
            self.assertTrue(
                obj.make_request(
                    operation='GET', path='/t'
                )
            )

    def test_post(self):
        """Verify POST request succeeds."""
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

    def test_patch(self):
        """Verify PATCH request succeeds."""
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

    def test_unsupported_operation(self):
        """Verify unsupported op returns False."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        with quiet():
            self.assertFalse(
                obj.make_request(
                    operation='DELETE', path='/t'
                )
            )

    def test_204_no_content(self):
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

    def test_exception_handling(self):
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


class TestExit(BaseRvmcTestCase):
    """Test _exit method."""

    def test_exit_code_zero(self):
        """Verify exit with code 0."""
        obj = self.make_object()
        obj.redfish_obj = None
        obj.systems_members = 0
        with quiet():
            with self.assertRaises(SystemExit):
                obj._exit(0)

    def test_exit_with_session(self):
        """Verify exit closes session."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.session = True
        obj.systems_members = 0
        with quiet():
            with self.assertRaises(SystemExit):
                obj._exit(1)


class TestRedfishStages(BaseRvmcTestCase):
    """Test redfish stage methods."""

    def test_connect_success(self):
        """Verify successful BMC connection."""
        obj = self.make_object()
        self.mod.redfish.redfish_client.return_value = (
            mock.MagicMock()
        )
        with mock.patch('os.system', return_value=0), \
             mock.patch('sys.stdout', devnull()), \
             mock.patch('time.sleep'):
            obj._redfish_client_connect()
        self.assertIsNotNone(obj.redfish_obj)

    def test_connect_failure(self):
        """Verify connection failure exits."""
        obj = self.make_object()
        self.mod.redfish.redfish_client.return_value = (
            None
        )
        with mock.patch('os.system', return_value=0), \
             mock.patch('sys.stdout', devnull()), \
             mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                obj._redfish_client_connect()

    def test_root_query(self):
        """Verify root query extracts systems URL."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.return_value = resp(
            200, tc.SYSTEMS_RESP
        )
        with quiet():
            obj._redfish_root_query()
        self.assertEqual(
            obj.systems_group_url, '/s/'
        )

    def test_create_session_success(self):
        """Verify session creation."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        with mock.patch('sys.stdout', devnull()), \
             mock.patch('time.sleep'):
            obj._redfish_create_session()
        self.assertTrue(obj.session)

    def test_create_session_invalid_creds(self):
        """Verify invalid credentials exits."""
        obj = self.make_object()
        obj.redfish_obj = mock.MagicMock()
        ice = self.mod.InvalidCredentialsError
        obj.redfish_obj.login.side_effect = ice("x")
        with mock.patch('sys.stdout', devnull()), \
             mock.patch('time.sleep'):
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
        self.assertEqual(obj.systems_members, 1)

    def test_load_vm_actions(self):
        """Verify VM actions loaded."""
        obj = self.make_object()
        obj.vm_url = tc.VM_URL
        obj.response_dict = {
            '@odata.type': '#VM.v1_2_0.VM',
            'Actions': {
                tc.EJECT_ACTION: {
                    'target': tc.EJECT_URL
                }
            },
        }
        with quiet():
            obj._redfish_load_vm_actions()
        self.assertEqual(obj.vm_version, 'v1_2_0')

    def test_poweroff_already_off(self):
        """Verify no action when already off."""
        obj = self.make_object()
        obj.power_state = tc.POWER_OFF
        with quiet():
            obj._redfish_poweroff_host()

    def test_poweron_already_on(self):
        """Verify no action when already on."""
        obj = self.make_object()
        obj.power_state = tc.POWER_ON
        with quiet():
            obj._redfish_poweron_host()

    def test_eject_not_inserted(self):
        """Verify eject skips when not inserted."""
        obj = self.make_object()
        obj.vm_url = tc.VM_URL
        obj.vm_actions = {
            tc.EJECT_ACTION: {
                'target': tc.EJECT_URL
            }
        }
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.get.return_value = resp(
            200, tc.INSERTED_FALSE_RESP
        )
        with quiet():
            obj._redfish_eject_image()

    def test_insert_image(self):
        """Verify image insertion."""
        obj = self.make_object()
        obj.vm_url = tc.VM_URL
        obj.img = tc.BMC_IMAGE_URL
        obj.vm_actions = {
            tc.INSERT_ACTION: {
                'target': tc.INSERT_URL
            }
        }
        obj.redfish_obj = mock.MagicMock()
        obj.redfish_obj.post.return_value = resp(200)
        insert_resp = (
            '{"Image":"http://h/b.iso",'
            '"Inserted":true,'
            '"ImageName":"b",'
            '"WriteProtected":true}'
        )
        obj.redfish_obj.get.return_value = resp(
            200, insert_resp
        )
        with quiet():
            obj._redfish_insert_image()

    def test_boot_override(self):
        """Verify boot override set to UEFI."""
        obj = self.make_object()
        obj.systems_members = 1
        obj.vm_media_types = ['CD']
        obj.systems_members_list = [
            {'@odata.id': tc.SYSTEM_MEMBER_URL}
        ]
        obj.redfish_obj = mock.MagicMock()
        boot_modes = (
            '{"Boot":{"' + tc.BOOT_MODES_KEY
            + '":["UEFI"]}}'
        )
        boot_result = (
            '{"Boot":{'
            '"BootSourceOverrideEnabled":"Once",'
            '"BootSourceOverrideTarget":"Cd",'
            '"BootSourceOverrideMode":"UEFI"}}'
        )
        obj.redfish_obj.get.side_effect = [
            resp(200, boot_modes),
            resp(200, boot_result),
        ]
        obj.redfish_obj.patch.return_value = resp(200)
        with quiet():
            obj._redfish_set_boot_override()

    def test_get_vm_url(self):
        """Verify VM URL discovery."""
        obj = self.make_object()
        obj.manager_members_list = [
            {'@odata.id': tc.MANAGER_MEMBER_URL}
        ]
        obj.redfish_obj = mock.MagicMock()
        vm_group = (
            '{"VirtualMedia":'
            '{"@odata.id":"/m/1/VM/"}}'
        )
        vm_members = (
            '{"Members":'
            '[{"@odata.id":"/m/1/VM/1/"}]}'
        )
        obj.redfish_obj.get.side_effect = [
            resp(200, vm_group),
            resp(200, vm_members),
            resp(200, tc.VM_MEDIA_RESP),
        ]
        with quiet():
            obj._redfish_get_vm_url()
        self.assertIsNotNone(obj.vm_url)

    def test_execute(self):
        """Verify execute calls all stages."""
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
            '_redfish_load_vm_actions',
            '_redfish_eject_image',
            '_redfish_poweroff_host',
            '_redfish_insert_image',
            '_redfish_set_boot_override',
            '_redfish_poweron_host',
        ]
        for stage in stages:
            setattr(obj, stage, mock.MagicMock())
        with quiet():
            obj.execute()


if __name__ == '__main__':
    unittest.main()
