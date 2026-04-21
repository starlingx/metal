#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Coverage test for hwmond_notify.py."""
import os
import sys
import unittest
from unittest import mock
from unit.loader import devnull


class TestHwmondNotifyImport(unittest.TestCase):
    """Import hwmond_notify.py with mocked socket and env."""
    @mock.patch('socket.gethostbyname', return_value='127.0.0.1')
    @mock.patch('socket.socket')
    @mock.patch.dict(os.environ, {'MESSAGE': 'test_msg'})
    @mock.patch('sys.stdout', devnull())
    def test_import(self, mock_sock_cls, mock_resolve):
        """Import the module - covers all 11 statements."""
        import importlib.util
        path = os.path.abspath(
            os.path.join(
                os.path.dirname(__file__),
                '..',
                '..',
                'mtce',
                'src',
                'hwmon',
                'scripts',
                'hwmond_notify.py'))
        spec = importlib.util.spec_from_file_location(
            'hwmond_notify_test', path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        mock_resolve.assert_called_with('controller')
        mock_sock_cls.return_value.sendto.assert_called_once()


if __name__ == '__main__':
    unittest.main()
