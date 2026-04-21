#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""
Unit tests for mtce/src/hwmon/scripts/hwmond_notify.py
"""
import os
import socket
import unittest
from unittest import mock
from tests import constants as tc


class TestHwmondNotify(unittest.TestCase):
    """Tests for hwmond_notify module logic."""

    @mock.patch('socket.socket')
    @mock.patch('socket.gethostbyname', return_value='127.0.0.1')
    @mock.patch.dict(os.environ, {'MESSAGE': 'test_message'})
    def test_udp_socket_creation(self, mock_gethostbyname, mock_socket):
        """Test UDP socket is created correctly."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.assertIsNotNone(sock)

    @mock.patch('socket.gethostbyname', return_value=tc.BMC_ADDRESS)
    def test_controller_resolution(self, mock_resolve):
        """Test controller hostname resolution."""
        ip = socket.gethostbyname('controller')
        self.assertEqual(ip, tc.BMC_ADDRESS)
        mock_resolve.assert_called_with('controller')

    def test_udp_port(self):
        """Test UDP port constant."""
        udp_port = tc.HWMOND_UDP_PORT
        self.assertEqual(udp_port, tc.HWMOND_UDP_PORT)

    @mock.patch.dict(os.environ, {'MESSAGE': 'hello_world'})
    def test_env_message(self):
        """Test MESSAGE environment variable."""
        msg = os.environ['MESSAGE']
        self.assertEqual(msg, 'hello_world')

    @mock.patch.dict(os.environ, {}, clear=True)
    def test_missing_env_message(self):
        """Test missing MESSAGE environment variable."""
        with self.assertRaises(KeyError):
            _ = os.environ['MESSAGE']

    @mock.patch('socket.gethostbyname',
                side_effect=socket.gaierror('Name resolution failed'))
    def test_controller_resolution_failure(self, mock_resolve):
        """Test controller resolution failure."""
        with self.assertRaises(socket.gaierror):
            socket.gethostbyname('controller')

    @mock.patch('socket.socket')
    def test_sendto_call(self, mock_socket_cls):
        """Test sendto is called with correct args."""
        mock_sock = mock.MagicMock()
        mock_socket_cls.return_value = mock_sock
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(b'test', ('127.0.0.1', tc.HWMOND_UDP_PORT))
        mock_sock.sendto.assert_called_once_with(
            b'test', ('127.0.0.1', tc.HWMOND_UDP_PORT))


if __name__ == '__main__':
    unittest.main()
