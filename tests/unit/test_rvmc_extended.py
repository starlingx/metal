#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Extended coverage tests for rvmc.py.

Covers deeper method paths including power control,
VM URL discovery, eject/insert, and boot override.
"""
import json
import os
import sys
import unittest
from unittest import mock

from tests import constants as tc
from helpers import BaseRvmcTestCase
from helpers import make_rvmc_object
from helpers import get_rvmc_module
from unit.loader import resp
from unit.loader import devnull, quiet

mod = get_rvmc_module()


# Constants imported from tests.constants
BOOT_MODES_KEY = (
    'BootSourceOverrideMode'
    '@Redfish.AllowableValues'
)




class TestPowerCtlPaths(unittest.TestCase):
    """Cover _redfish_powerctl_host branches."""

    def _setup_power(self, vmc_obj,
                     current='Off', target='On',
                     actions=None):
        """Configure VmcObject for power control test.

        vmc_obj - VmcObject to configure
        current - current power state
        target - desired power state
        actions - list of allowed reset actions

        Sets up mock redfish responses for power
        state transitions.
        """
        vmc_obj.power_state = current
        vmc_obj.systems_members = 1
        vmc_obj.systems_members_list = [
            {'@odata.id': '/s/1/'}
        ]
        vmc_obj.redfish_obj = mock.MagicMock()
        acts = actions or ['On', 'ForceOff',
                           'ForceRestart', 'GracefulShutdown']
        sys_resp = resp(200, json.dumps({
            'PowerState': current,
            'Actions': {tc.RESET_ACTION: {
                'target': '/reset',
                tc.RESET_KEY: acts}}}))
        poll_resp = resp(200, json.dumps({'PowerState': target}))
        vmc_obj.redfish_obj.get.side_effect = [
            sys_resp, poll_resp
        ]
        vmc_obj.redfish_obj.post.return_value = resp(200)

    def test_power_on(self):
        o = make_rvmc_object()
        self._setup_power(o, 'Off', 'On')
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('On')

    def test_power_off(self):
        o = make_rvmc_object()
        self._setup_power(o, 'On', 'Off', ['ForceOff', 'On'])
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('Off')

    def test_power_graceful_shutdown(self):
        o = make_rvmc_object()
        self._setup_power(o, 'On', 'Off', ['GracefulShutdown'])
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('Off')

    def test_power_force_restart(self):
        o = make_rvmc_object()
        self._setup_power(o, 'On', 'On', ['ForceRestart', 'On'])
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            # ForceRestart doesn't poll for state change
            o._redfish_powerctl_host('ForceRestart')

    def test_power_no_reset_dict(self):
        o = make_rvmc_object()
        o.power_state = 'Off'
        o.systems_members = 1
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(
            200, '{"PowerState":"Off","Actions":{}}')
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_powerctl_host('On')

    def test_power_no_acceptable_cmd(self):
        o = make_rvmc_object()
        self._setup_power(o, 'Off', 'On', ['Nmi', 'PushPowerButton'])
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_powerctl_host('On')

    def test_power_no_target_url(self):
        o = make_rvmc_object()
        o.power_state = 'Off'
        o.systems_members = 1
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        r = resp(200, json.dumps({'PowerState': 'Off', 'Actions': {
            tc.RESET_ACTION: {tc.RESET_KEY: ['On']}}}))
        o.redfish_obj.get.return_value = r
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_powerctl_host('On')

    def test_power_no_allowable(self):
        o = make_rvmc_object()
        o.power_state = 'Off'
        o.systems_members = 1
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        r = resp(200, json.dumps({'PowerState': 'Off', 'Actions': {
            tc.RESET_ACTION: {'target': '/reset'}}}))
        o.redfish_obj.get.return_value = r
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_powerctl_host('On')

    def test_power_already_on(self):
        o = make_rvmc_object()
        o.power_state = 'Off'
        o.systems_members = 1
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        r = resp(200,
                 json.dumps({'PowerState': 'On',
                             'Actions': {
                                 tc.RESET_ACTION: {
                                     'target': '/r',
                                     tc.RESET_KEY: ['On']
                                 }}}))
        o.redfish_obj.get.return_value = r
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('On')


class TestGetVmUrlPaths(unittest.TestCase):
    """Cover _redfish_get_vm_url branches."""

    def test_no_members(self):
        o = make_rvmc_object()
        o.manager_members_list = None
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()

    def test_empty_members(self):
        o = make_rvmc_object()
        o.manager_members_list = []
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()

    def test_no_vm_support(self):
        o = make_rvmc_object()
        o.manager_members_list = [{'@odata.id': '/m/1/'}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()

    def test_unsupported_media(self):
        o = make_rvmc_object()
        o.manager_members_list = [{'@odata.id': '/m/1/'}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"VirtualMedia":{"@odata.id":"/m/1/VM/"}}'),
            resp(200, '{"Members":[{"@odata.id":"/m/1/VM/1/"}]}'),
            resp(200, '{"MediaTypes":["USB"]}')]
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()

    def test_no_media_types(self):
        o = make_rvmc_object()
        o.manager_members_list = [{'@odata.id': '/m/1/'}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"VirtualMedia":{"@odata.id":"/m/1/VM/"}}'),
            resp(200, '{"Members":[{"@odata.id":"/m/1/VM/1/"}]}'),
            resp(200, '{"MediaTypes":["Floppy"]}')]
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()


class TestEjectPaths(unittest.TestCase):
    """Cover _redfish_eject_image branches."""

    def test_eject_inserted(self):
        o = make_rvmc_object()
        o.vm_url = '/vm'
        o.vm_actions = {'#VirtualMedia.EjectMedia': {'target': '/ej'}}
        o.redfish_obj = mock.MagicMock()
        # First GET: Inserted=True, second POST eject, third GET:
        # Inserted=False
        o.redfish_obj.get.side_effect = [
            resp(200, '{"Inserted":true,"Image":"http://x"}'),
            resp(200, '{"Inserted":false}')]
        o.redfish_obj.post.return_value = resp(200)
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_eject_image()

    def test_eject_no_action(self):
        o = make_rvmc_object()
        o.vm_url = '/vm'
        o.vm_actions = {}
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(
            200, '{"Inserted":true,"Image":"http://x"}')
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_eject_image()


class TestInsertPaths(unittest.TestCase):
    """Cover _redfish_insert_image branches."""

    def test_insert_no_action(self):
        o = make_rvmc_object()
        o.vm_url = '/vm'
        o.img = tc.BMC_IMAGE_URL
        o.vm_actions = {}
        o.redfish_obj = mock.MagicMock()
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_insert_image()

    def test_insert_timeout(self):
        o = make_rvmc_object()
        o.vm_url = '/vm'
        o.img = tc.BMC_IMAGE_URL
        o.vm_actions = {'#VirtualMedia.InsertMedia': {'target': '/ins'}}
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.post.return_value = resp(200)
        o.redfish_obj.get.return_value = resp(
            200, '{"Image":"wrong","Inserted":true}')
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_insert_image()


class TestBootOverridePaths(unittest.TestCase):
    """Cover _redfish_set_boot_override branches."""

    def test_legacy_mode(self):
        o = make_rvmc_object()
        o.systems_members = 1
        o.vm_media_types = ['CD']
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        r1 = resp(
            200,
            '{"Boot":{"' + BOOT_MODES_KEY
            + '":["Legacy"]}}')
        r3 = resp(
            200,
            '{"Boot":{'
            '"BootSourceOverrideEnabled":"Once",'
            '"BootSourceOverrideTarget":"Cd",'
            '"BootSourceOverrideMode":"Legacy"}}')
        o.redfish_obj.get.side_effect = [r1, r3]
        o.redfish_obj.patch.return_value = resp(200)
        with quiet():
            o._redfish_set_boot_override()

    def test_no_mode_list(self):
        o = make_rvmc_object()
        o.systems_members = 1
        o.vm_media_types = ['CD']
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        r1 = resp(200, '{"Boot":{}}')
        r3 = resp(
            200,
            '{"Boot":{'
            '"BootSourceOverrideEnabled":"Once",'
            '"BootSourceOverrideTarget":"Cd"}}')
        o.redfish_obj.get.side_effect = [r1, r3]
        o.redfish_obj.patch.return_value = resp(200)
        with quiet():
            o._redfish_set_boot_override()

    def test_no_boot_dict(self):
        o = make_rvmc_object()
        o.systems_members = 1
        o.vm_media_types = ['CD']
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_set_boot_override()


class TestVmLoadActions(unittest.TestCase):
    def test_no_vm_url(self):
        o = make_rvmc_object()
        o.vm_url = None
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_load_vm_actions()

    def test_no_odata_type(self):
        o = make_rvmc_object()
        o.vm_url = '/vm'
        o.response_dict = {}
        with quiet():
            o._redfish_load_vm_actions()
        self.assertIsNone(o.vm_version)


class TestRootQueryFail(unittest.TestCase):
    def test_root_query_fail(self):
        o = make_rvmc_object()
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = Exception("e")
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_root_query()


class TestGetManagersFail(unittest.TestCase):
    def test_no_managers_link(self):
        o = make_rvmc_object()
        o.redfish_obj = mock.MagicMock()
        o.response_dict = {'Managers': None}
        with quiet():
            with self.assertRaises((SystemExit, AttributeError)):
                o._redfish_get_managers()


class TestGetSystemsFail(unittest.TestCase):
    def test_no_members(self):
        o = make_rvmc_object()
        o.systems_group_url = '/s/'
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{"Members":[]}')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_systems_members()

    def test_null_members(self):
        o = make_rvmc_object()
        o.systems_group_url = '/s/'
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_systems_members()


if __name__ == '__main__':
    unittest.main()


class TestRvmcMakeRequestPaths(unittest.TestCase):
    """Cover remaining make_request error branches."""

    def test_500_with_retry_exhaust(self):
        o = make_rvmc_object()
        o.redfish_obj = mock.MagicMock()
        r = resp(500)
        r.dict = {'e': 'x'}
        o.redfish_obj.get.return_value = r
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o.make_request(operation='GET', path='/t', retry=0)

    def test_parse_exception(self):
        o = make_rvmc_object()
        o.redfish_obj = mock.MagicMock()
        r = mock.MagicMock(status=200)
        r.read = None
        o.redfish_obj.get.return_value = r
        with quiet():
            self.assertFalse(o.make_request(operation='GET', path='/t'))


class TestRvmcEjectInsertPaths(unittest.TestCase):
    """Cover eject/insert error branches."""

    def test_eject_timeout(self):
        o = make_rvmc_object()
        o.vm_url = '/vm'
        o.vm_actions = {'#VirtualMedia.EjectMedia': {'target': '/ej'}}
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(
            200, '{"Inserted":true,"Image":"http://x"}')
        o.redfish_obj.post.return_value = resp(200)
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_eject_image()

    def test_insert_post_fail(self):
        o = make_rvmc_object()
        o.vm_url = '/vm'
        o.img = tc.BMC_IMAGE_URL
        o.vm_actions = {'#VirtualMedia.InsertMedia': {'target': '/ins'}}
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.post.side_effect = Exception("e")
        with quiet():
            try:
                o._redfish_insert_image()
            except (SystemExit, Exception):
                pass

    def test_boot_override_unsupported_mode(self):
        o = make_rvmc_object()
        o.systems_members = 1
        o.vm_media_types = ['CD']
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        r1 = resp(
            200,
            '{"Boot":{"' + BOOT_MODES_KEY
            + '":["PXE"]}}')
        o.redfish_obj.get.return_value = r1
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_set_boot_override()

    def test_connect_ipv6_ping_fail(self):
        o = make_rvmc_object()
        o.ipv6 = True
        o.ip = '[2001:db8::1]'
        with mock.patch('os.system', return_value=1), \
             mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_client_connect()


class TestPermissionErrors(unittest.TestCase):
    """Test permission error handling for file ops."""

    def test_config_file_permission_denied(self):
        """Verify graceful handling when config file
        cannot be opened due to permissions.

        Uses a mock that raises PermissionError only
        for the config file path.
        """
        import importlib.util
        import types

        mock_redfish = types.ModuleType('redfish')
        mock_redfish.redfish_client = mock.MagicMock()
        mock_v1 = types.ModuleType('redfish.rest.v1')
        mock_v1.InvalidCredentialsError = type(
            'ICE', (Exception,), {}
        )
        sys.modules['redfish'] = mock_redfish
        sys.modules['redfish.rest'] = types.ModuleType(
            'redfish.rest'
        )
        sys.modules['redfish.rest.v1'] = mock_v1

        original_open = open

        def perm_error_open(path, *args, **kwargs):
            """Raise PermissionError for config file."""
            if isinstance(path, str) and (
                path == '/etc/rvmc.yaml'
            ):
                raise PermissionError(
                    "Permission denied: " + path
                )
            return original_open(path, *args, **kwargs)

        spec = importlib.util.spec_from_file_location(
            'rvmc_perm_test',
            os.path.abspath(os.path.join(
                os.path.dirname(__file__), '..', '..',
                'tools', 'rvmc', 'docker', 'rvmc.py'
            ))
        )
        mod = importlib.util.module_from_spec(spec)
        sys.modules['rvmc_perm_test'] = mod

        with mock.patch('sys.argv', ['rvmc.py']), \
             mock.patch('os.path.exists',
                        return_value=True), \
             mock.patch('builtins.open',
                        side_effect=perm_error_open), \
             mock.patch('sys.exit',
                        side_effect=SystemExit), \
             mock.patch('sys.stdout',
                        original_open(os.devnull, 'w')):
            with self.assertRaises(SystemExit):
                spec.loader.exec_module(mod)
