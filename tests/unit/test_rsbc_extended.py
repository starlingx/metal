#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Extended coverage tests for rsbc.py.

Covers deeper method paths including power control,
secure boot, certificate upload, and VM operations.
"""
import json
import os
import sys
import tempfile
import unittest
from unittest import mock

from tests import constants as tc
from helpers import BaseRsbcTestCase
from helpers import sb_state_responses, sb_db_responses, sb_upload_responses
from helpers import make_rsbc_object
from helpers import get_rsbc_module
from unit.loader import resp
from unit.loader import devnull, quiet

mod = get_rsbc_module()


# Constants imported from tests.constants




class TestPowerCtl(unittest.TestCase):
    def _setup(self, vmc_obj,
               current='Off', target='On',
               actions=None):
        """Configure VmcObject for power control test.

        vmc_obj - VmcObject to configure
        current - current power state
        target - desired power state
        actions - list of allowed reset actions
        """
        vmc_obj.power_state = current
        vmc_obj.systems_members = 1
        vmc_obj.systems_members_list = [
            {'@odata.id': '/s/1/'}
        ]
        vmc_obj.redfish_obj = mock.MagicMock()
        allowed = actions or [
            'On', 'ForceOff', 'ForceRestart'
        ]
        reset_data = {
            'PowerState': current,
            'Actions': {tc.RESET_ACTION: {
                'target': '/r',
                tc.RESET_KEY: allowed,
            }}
        }
        sys_resp = resp(
            200, json.dumps(reset_data)
        )
        poll_resp = resp(
            200,
            json.dumps({'PowerState': target})
        )
        vmc_obj.redfish_obj.get.side_effect = [
            sys_resp, poll_resp
        ]
        vmc_obj.redfish_obj.post.return_value = (
            resp(200)
        )

    def test_on(self):
        o = make_rsbc_object()
        self._setup(o, 'Off', 'On')
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('On')

    def test_off(self):
        o = make_rsbc_object()
        self._setup(o, 'On', 'Off', ['ForceOff'])
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('Off')

    def test_reset(self):
        o = make_rsbc_object()
        self._setup(o, 'On', 'On', ['ForceRestart'])
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('Reset')

    def test_no_reset_dict(self):
        o = make_rsbc_object()
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

    def test_no_cmd(self):
        o = make_rsbc_object()
        self._setup(o, 'Off', 'On', ['Nmi'])
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_powerctl_host('On')

    def test_already(self):
        o = make_rsbc_object()
        o.power_state = 'Off'
        o.systems_members = 1
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        r = resp(200,
                 json.dumps({'PowerState': 'On',
                             'Actions': {tc.RESET_ACTION: {'target': '/r',
                             tc.RESET_KEY: ['On']
                             }}}))
        o.redfish_obj.get.return_value = r
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('On')


class TestGetVmUrl(unittest.TestCase):
    def test_no_members(self):
        o = make_rsbc_object()
        o.manager_members_list = None
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()

    def test_empty(self):
        o = make_rsbc_object()
        o.manager_members_list = []
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()

    def test_no_vm(self):
        o = make_rsbc_object()
        o.manager_members_list = [{'@odata.id': '/m/1/'}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_vm_url()

    def test_ok(self):
        o = make_rsbc_object()
        o.manager_members_list = [{'@odata.id': '/m/1/'}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"VirtualMedia":{"@odata.id":"/vm/"}}'),
            resp(200, '{"Members":[{"@odata.id":"/vm/1/"}]}'),
            resp(200, '{"MediaTypes":["CD"]}')]
        with quiet():
            o._redfish_get_vm_url()
        self.assertIsNotNone(o.vm_url)


class TestSecureBoot(unittest.TestCase):
    def test_query_disabled(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = sb_state_responses('false')
        with quiet():
            o._redfish_query_sb_state()

    def test_query_no_sb(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            o._redfish_query_sb_state()

    def test_get_certs(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
            resp(200, '{"SecureBootDatabases":{"@odata.id":"/sbd"}}'),
            resp(200, '{"Members":[{"@odata.id":"/sbd/db"}]}'),
            resp(200, '{}'),
            resp(200, '{"Members":[{"@odata.id":"/sbd/db/c/1"}]}'),
            resp(200, '{"CertificateString":"x"}')]
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('builtins.open', mock.mock_open()):
            result = o._redfish_get_secure_boot_certificates()

    def test_get_certs_fail(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            result = o._redfish_get_secure_boot_certificates()
            self.assertEqual(result, 1)

    def test_enable_sb(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        mod.ENABLE = True
        mod.DISABLE = False
        o.redfish_obj.get.side_effect = sb_state_responses('false')
        o.redfish_obj.patch.return_value = resp(200)
        with mock.patch('sys.stdout', devnull()), \
            mock.patch('time.sleep'), \
                mock.patch.object(o, '_redfish_powerctl_host'):
            o._redfish_enable_secure_boot()
        mod.ENABLE = False

    def test_enable_already(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        mod.ENABLE = True
        mod.DISABLE = False
        o.redfish_obj.get.side_effect = sb_state_responses('true')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_enable_secure_boot()
        mod.ENABLE = False

    def test_disable_sb(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        mod.ENABLE = False
        mod.DISABLE = True
        o.redfish_obj.get.side_effect = sb_state_responses('true')
        o.redfish_obj.patch.return_value = resp(200)
        with mock.patch('sys.stdout', devnull()), \
            mock.patch('time.sleep'), \
                mock.patch.object(o, '_redfish_powerctl_host'):
            o._redfish_enable_secure_boot()
        mod.DISABLE = False


class TestUploadCerts(unittest.TestCase):
    def test_upload_pem(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = sb_upload_responses()
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('builtins.open',
                        mock.mock_open(
                            read_data='CERT')), \
                mock.patch(
                    'requests.request',
                    return_value=mock.MagicMock(
                        status_code=200)), \
                mock.patch.object(o, '_redfish_powerctl_host'):
            with tempfile.NamedTemporaryFile(suffix='.pem') as f:
                o._redfish_upload_certificates(f.name)

    def test_upload_bad_ext(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = sb_upload_responses()
        with quiet():
            result = o._redfish_upload_certificates('/tmp/cert.txt')
            self.assertEqual(result, 1)


class TestExecuteFlows(unittest.TestCase):
    def test_service(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.session = True
        mod.SERVICE = True
        mod.QUERY = False
        mod.UPLOAD = False
        mod.ENABLE = False
        mod.DISABLE = False
        for m in [
            '_redfish_client_connect',
            '_redfish_root_query',
            '_redfish_create_session',
            '_redfish_get_managers',
            '_redfish_get_systems_members',
            '_redfish_get_vm_url',
            '_redfish_get_vm_version',
                '_redfish_get_secure_boot_version']:
            setattr(o, m, mock.MagicMock())
        with quiet():
            o.execute(0)
        mod.SERVICE = False

    def test_enable(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.session = True
        mod.ENABLE = True
        mod.SERVICE = False
        mod.QUERY = False
        mod.UPLOAD = False
        mod.DISABLE = False
        for m in [
            '_redfish_client_connect',
            '_redfish_root_query',
            '_redfish_create_session',
            '_redfish_get_managers',
            '_redfish_get_systems_members',
                '_redfish_enable_secure_boot']:
            setattr(o, m, mock.MagicMock())
        with quiet():
            o.execute(0)
        mod.ENABLE = False

    def test_disable(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.session = True
        mod.DISABLE = True
        mod.SERVICE = False
        mod.QUERY = False
        mod.UPLOAD = False
        mod.ENABLE = False
        for m in [
            '_redfish_client_connect',
            '_redfish_root_query',
            '_redfish_create_session',
            '_redfish_get_managers',
            '_redfish_get_systems_members',
                '_redfish_enable_secure_boot']:
            setattr(o, m, mock.MagicMock())
        with quiet():
            o.execute(0)
        mod.DISABLE = False

    def test_upload(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.session = True
        mod.UPLOAD = True
        mod.SERVICE = False
        mod.QUERY = False
        mod.ENABLE = False
        mod.DISABLE = False
        mod.certificate = '/tmp/c.pem'
        for m in [
            '_redfish_client_connect',
            '_redfish_root_query',
            '_redfish_create_session',
            '_redfish_get_managers',
            '_redfish_get_systems_members',
                '_redfish_upload_certificates']:
            setattr(o, m, mock.MagicMock())
        with quiet():
            o.execute(0)
        mod.UPLOAD = False

    def test_query(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.session = True
        mod.QUERY = True
        mod.SERVICE = False
        mod.UPLOAD = False
        mod.ENABLE = False
        mod.DISABLE = False
        for m in [
            '_redfish_client_connect',
            '_redfish_root_query',
            '_redfish_create_session',
            '_redfish_get_managers',
            '_redfish_get_systems_members',
            '_redfish_query_sb_state',
                '_redfish_get_secure_boot_certificates']:
            setattr(o, m, mock.MagicMock())
        with quiet():
            o.execute(0)
        mod.QUERY = False


class TestConnectPaths(unittest.TestCase):
    def test_connect_exc(self):
        o = make_rsbc_object()
        mod.redfish.redfish_client.side_effect = Exception("e")
        with mock.patch('os.system', return_value=0), \
             mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_client_connect()
        mod.redfish.redfish_client.side_effect = None

    def test_connect_ipv6(self):
        o = make_rsbc_object()
        o.ipv6 = True
        o.ip = '[::1]'
        mod.redfish.redfish_client.return_value = mock.MagicMock()
        with mock.patch('os.system', return_value=0), \
             mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_client_connect()

    def test_connect_ping_fail(self):
        o = make_rsbc_object()
        with mock.patch('os.system', return_value=1), \
             mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            with self.assertRaises(SystemExit):
                o._redfish_client_connect()


class TestManagersFail(unittest.TestCase):
    def test_no_link(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.response_dict = {'Managers': None}
        with quiet():
            with self.assertRaises((SystemExit, AttributeError)):
                o._redfish_get_managers()


class TestSystemsFail(unittest.TestCase):
    def test_empty(self):
        o = make_rsbc_object()
        o.systems_group_url = '/s/'
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{"Members":[]}')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_get_systems_members()


if __name__ == '__main__':
    unittest.main()


class TestRsbcDeepPaths(unittest.TestCase):
    """Cover remaining error branches in rsbc secure boot methods."""

    def test_enable_no_sb(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        mod.ENABLE = True
        mod.DISABLE = False
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            result = o._redfish_enable_secure_boot()
            self.assertEqual(result, 1)
        mod.ENABLE = False

    def test_enable_get_fail(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        mod.ENABLE = True
        mod.DISABLE = False
        o.redfish_obj.get.side_effect = Exception("e")
        with quiet():
            try:
                o._redfish_enable_secure_boot()
            except (SystemExit, Exception):
                pass
        mod.ENABLE = False

    def test_upload_der(self):
        import tempfile
        import ssl
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = sb_upload_responses()
        # Create a fake DER file
        with tempfile.NamedTemporaryFile(suffix='.der', delete=False) as f:
            f.write(b'\x00' * 10)
            der_path = f.name
        with mock.patch('sys.stdout', devnull()), \
             mock.patch('ssl.DER_cert_to_PEM_cert',
                        return_value='PEM_CERT'):
            with mock.patch(
                    'requests.request',
                    return_value=mock.MagicMock(
                        status_code=200)):
                with mock.patch.object(o, '_redfish_powerctl_host'):
                    result = o._redfish_upload_certificates(der_path)
        os.unlink(der_path)

    def test_upload_fail_response(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = sb_upload_responses()
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('builtins.open',
                        mock.mock_open(
                            read_data='CERT')), \
                mock.patch(
                    'requests.request',
                    return_value=mock.MagicMock(
                        status_code=500)):
            result = o._redfish_upload_certificates('/tmp/c.pem')
            self.assertEqual(result, 1)

    def test_query_sb_disabled(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = sb_state_responses('false')
        with quiet():
            o._redfish_query_sb_state()

    def test_get_certs_no_db(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
            resp(200, '{}')]
        with quiet():
            result = o._redfish_get_secure_boot_certificates()
            self.assertEqual(result, 1)

    def test_sb_version_no_type(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
            resp(200, '{}')]
        with quiet():
            o._redfish_get_secure_boot_version()

    def test_vm_version_no_url(self):
        o = make_rsbc_object()
        o.vm_url = None
        with quiet():
            o._redfish_get_vm_version()

    def test_disable_already(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        mod.ENABLE = False
        mod.DISABLE = True
        o.redfish_obj.get.side_effect = sb_state_responses('false')
        with quiet():
            with self.assertRaises(SystemExit):
                o._redfish_enable_secure_boot()
        mod.DISABLE = False

    def test_enable_patch_fail(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        mod.ENABLE = True
        mod.DISABLE = False
        o.redfish_obj.get.side_effect = sb_state_responses('false')
        o.redfish_obj.patch.side_effect = Exception("e")
        with quiet():
            try:
                o._redfish_enable_secure_boot()
            except (SystemExit, Exception):
                pass
        mod.ENABLE = False

    def test_upload_crt(self):
        import tempfile
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = sb_upload_responses()
        with tempfile.NamedTemporaryFile(suffix='.crt', delete=False) as f:
            f.write(b'\x00' * 10)
            crt_path = f.name
        with mock.patch('sys.stdout', devnull()), \
             mock.patch('ssl.DER_cert_to_PEM_cert',
                        return_value='PEM'):
            with mock.patch(
                    'requests.request',
                    return_value=mock.MagicMock(
                        status_code=204)):
                with mock.patch.object(o, '_redfish_powerctl_host'):
                    o._redfish_upload_certificates(crt_path)
        os.unlink(crt_path)

    def test_upload_no_sb(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.return_value = resp(200, '{}')
        with quiet():
            result = o._redfish_upload_certificates('/tmp/c.pem')
            self.assertEqual(result, 1)

    def test_upload_no_db(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
            resp(200, '{}')]
        with quiet():
            result = o._redfish_upload_certificates('/tmp/c.pem')
            self.assertEqual(result, 1)

    def test_upload_no_db_members(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
            resp(200, '{"SecureBootDatabases":{"@odata.id":"/sbd"}}'),
            resp(200, '{}')]
        with quiet():
            result = o._redfish_upload_certificates('/tmp/c.pem')
            self.assertEqual(result, 1)

    def test_get_certs_full(self):
        o = make_rsbc_object()
        o.systems_members_list = [{"@odata.id": "/s/1/"}]
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = [
            resp(200, '{"SecureBoot":{"@odata.id":"/sb"}}'),
            resp(200, '{"SecureBootDatabases":{"@odata.id":"/sbd"}}'),
            resp(200, '{"Members":[{"@odata.id":"/sbd/db"}]}'),
            resp(200, '{}'),
            resp(
                200,
                '{"Members":['
                '{"@odata.id":"/c/1"},'
                '{"@odata.id":"/c/2"}]}'),
            resp(200, '{"CertificateString":"cert1"}'),
            resp(200, '{"CertificateString":"cert2"}')]
        with mock.patch('sys.stdout', devnull()), \
             mock.patch('builtins.open',
                        mock.mock_open()):
            o._redfish_get_secure_boot_certificates()

    def test_enable_sb_no_sys_member(self):
        o = make_rsbc_object()
        o.systems_members_list = []
        mod.ENABLE = True
        mod.DISABLE = False
        with quiet():
            result = o._redfish_enable_secure_boot()
            self.assertEqual(result, 1)
        mod.ENABLE = False

    def test_powerctl_graceful_shutdown(self):
        o = make_rsbc_object()
        o.power_state = 'On'
        o.systems_members = 1
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        reset_data = {
            'PowerState': 'On',
            'Actions': {tc.RESET_ACTION: {
                'target': '/r',
                tc.RESET_KEY: ['GracefulShutdown']
            }}
        }
        r1 = resp(200, json.dumps(reset_data))
        r2 = resp(200, '{"PowerState":"Off"}')
        o.redfish_obj.get.side_effect = [r1, r2]
        o.redfish_obj.post.return_value = resp(200)
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('Off')

    def test_powerctl_graceful_restart(self):
        o = make_rsbc_object()
        o.power_state = 'On'
        o.systems_members = 1
        o.systems_members_list = [{'@odata.id': '/s/1/'}]
        o.redfish_obj = mock.MagicMock()
        reset_data = {
            'PowerState': 'On',
            'Actions': {tc.RESET_ACTION: {
                'target': '/r',
                tc.RESET_KEY: ['GracefulRestart']
            }}
        }
        r1 = resp(200, json.dumps(reset_data))
        o.redfish_obj.get.return_value = r1
        o.redfish_obj.post.return_value = resp(200)
        with mock.patch('sys.stdout', devnull()), \
                mock.patch('time.sleep'):
            o._redfish_powerctl_host('Reset')

    def test_resp_dict_no_read(self):
        o = make_rsbc_object()
        o.response = mock.MagicMock(read=None)
        with quiet():
            try:
                o.resp_dict()
            except (TypeError, SystemExit):
                pass

    def test_format_exception(self):
        o = make_rsbc_object()
        o.response = mock.MagicMock(read='{"a":1}')
        o.resp_dict()
        o.response_dict = mock.MagicMock()
        o.response_dict.get.side_effect = Exception("e")
        with quiet():
            try:
                o.format()
            except Exception:
                pass

    def test_exit_logout_fail(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.logout.side_effect = Exception("x")
        o.session = True
        o.systems_members = 0
        with quiet():
            with self.assertRaises(SystemExit):
                o._exit(1)

    def test_make_request_no_response(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        o.redfish_obj.get.side_effect = Exception("e")
        with quiet():
            self.assertFalse(o.make_request(operation='GET', path='/t'))

    def test_make_request_parse_fail(self):
        o = make_rsbc_object()
        o.redfish_obj = mock.MagicMock()
        r = mock.MagicMock(status=200)
        r.read = None
        o.redfish_obj.get.return_value = r
        with quiet():
            self.assertFalse(o.make_request(operation='GET', path='/t'))

    def test_check_ok_eject_400(self):
        o = make_rsbc_object()
        o.vm_eject_url = '/ej'
        o.response = mock.MagicMock(status=400)
        self.assertTrue(o.check_ok_status('/ej', 'POST', 0))

    def test_check_ok_403(self):
        o = make_rsbc_object()
        o.vm_eject_url = '/ej'
        o.response = mock.MagicMock(status=403)
        self.assertTrue(o.check_ok_status('/ej', 'POST', 0))

    def test_parse_target_exception(self):
        mod.target_object_list = []
        with quiet():
            mod.parse_target('h',
                             {'bmc_password': 'p',
                              'bmc_address': tc.BMC_ADDRESS,
                              'bmc_username': None})

    def test_check_ok_404(self):
        o = make_rsbc_object()
        o.vm_eject_url = '/ej'
        o.response = mock.MagicMock(status=404)
        self.assertTrue(o.check_ok_status('/ej', 'POST', 0))

    def test_check_ok_500_exc(self):
        o = make_rsbc_object()
        o.response = mock.MagicMock(status=500)
        o.response.dict = mock.PropertyMock(side_effect=Exception("x"))
        with quiet():
            try:
                o.check_ok_status('/t', 'GET', 0)
            except (SystemExit, Exception):
                pass

    def test_dlog1_with_debug(self):
        old_debug = mod.debug
        mod.debug = 3
        with quiet():
            mod.dlog1("test", 1)
        with quiet():
            mod.dlog1("test", 2)
        with quiet():
            mod.dlog1("test", 3)
        mod.debug = old_debug

    def test_ilog_service_mode(self):
        old = mod.SERVICE
        mod.SERVICE = True
        mod.f = devnull()
        with quiet():
            mod.ilog("test")
        with quiet():
            mod.elog("test")
        with quiet():
            mod.alog("test")
        with quiet():
            mod.slog("test")
        mod.SERVICE = old

    def test_dlog1_with_targets(self):
        old_debug = mod.debug
        old_targets = mod.targets
        mod.debug = 1
        mod.targets = ['t1']
        with quiet():
            mod.dlog1("Targets     : %s" % mod.targets)
        mod.debug = old_debug
        mod.targets = old_targets

    def test_parse_target_obj_none(self):
        """Cover VmcObject returning None."""
        mod.target_object_list = []
        with mock.patch('sys.stdout', devnull()), \
                mock.patch.object(mod, 'VmcObject', return_value=None):
            mod.parse_target('h',
                             {'bmc_address': tc.BMC_ADDRESS,
                              'bmc_username': 'a',
                              'bmc_password': 'p'})
        self.assertEqual(len(mod.target_object_list), 0)

    def test_rsbc_exit_nonzero_service(self):
        old = mod.SERVICE
        mod.SERVICE = True
        with quiet():
            with self.assertRaises(SystemExit):
                mod.rsbc_exit(1)
        mod.SERVICE = old
