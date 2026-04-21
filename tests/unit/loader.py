#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Module loader for rvmc and rsbc test coverage.

Loads rvmc.py and rsbc.py via importlib with mocked
external dependencies to enable coverage tracking
without triggering network calls or infinite loops.
"""
import importlib.util
import os
import sys
import types
from unittest import mock


def _safe_exit(code):
    """Exit handler that raises SystemExit.

    :param code: exit code to raise
    :type code: int
    :raises SystemExit: always raised to terminate loops
    """
    raise SystemExit(code)


def load_rvmc():
    """Load rvmc.py module with mocked redfish dependency.

    Mocks the redfish library and configures loop constants
    to prevent hangs during testing. The module is loaded
    via importlib so coverage.py can track it.

    :returns: loaded rvmc module object
    :rtype: module
    """
    base = os.path.dirname(__file__)
    path = os.path.abspath(
        os.path.join(base, '..', '..', 'tools',
                     'rvmc', 'docker', 'rvmc.py')
    )
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

    if 'rvmc' in sys.modules:
        del sys.modules['rvmc']
    spec = importlib.util.spec_from_file_location(
        'rvmc', path
    )
    mod = importlib.util.module_from_spec(spec)
    sys.modules['rvmc'] = mod

    with mock.patch('sys.argv', ['rvmc.py']), \
            mock.patch('os.path.exists', return_value=False), \
            mock.patch('sys.exit', side_effect=SystemExit), \
            mock.patch('sys.stdout', open(os.devnull, 'w')):
        try:
            spec.loader.exec_module(mod)
        except SystemExit:
            pass

    mod.rvmc_exit = _safe_exit
    mod.MAX_POLL_COUNT = 1
    mod.MAX_CONNECTION_ATTEMPTS = 1
    mod.MAX_SESSION_CREATION_ATTEMPTS = 1
    mod.MAX_HTTP_TRANSIENT_ERROR_RETRIES = 1
    mod.RETRY_DELAY_SECS = 0
    mod.DELAY_2_SECS = 0
    mod.CONNECTION_RETRY_INTERVAL = 0
    mod.SESSION_CREATION_RETRY_INTERVAL = 0
    mod.HTTP_REQUEST_RETRY_INTERVAL = 0
    return mod


def load_rsbc():
    """Load rsbc.py module with mocked dependencies.

    Mocks the redfish and requests libraries and configures
    loop constants to prevent hangs during testing.

    :returns: loaded rsbc module object
    :rtype: module
    """
    base = os.path.dirname(__file__)
    path = os.path.abspath(
        os.path.join(base, '..', '..', 'tools',
                     'rsbc', 'rsbc.py')
    )
    mock_redfish = types.ModuleType('redfish')
    mock_redfish.redfish_client = mock.MagicMock()
    sys.modules['redfish'] = mock_redfish
    mock_requests = types.ModuleType('requests')
    mock_requests.request = mock.MagicMock()
    sys.modules['requests'] = mock_requests

    if 'rsbc' in sys.modules:
        del sys.modules['rsbc']
    spec = importlib.util.spec_from_file_location(
        'rsbc', path
    )
    mod = importlib.util.module_from_spec(spec)
    sys.modules['rsbc'] = mod

    argv = [
        'rsbc.py', '--query',
        '--bmc_ip', '127.0.0.1',
        '--bmc_un', 'admin',
        '--bmc_pw', 'pass'
    ]
    with mock.patch('sys.argv', argv), \
            mock.patch('os.path.exists', return_value=False), \
            mock.patch('sys.exit', side_effect=SystemExit), \
            mock.patch('sys.stdout', open(os.devnull, 'w')):
        try:
            spec.loader.exec_module(mod)
        except SystemExit:
            pass

    mod.rsbc_exit = _safe_exit
    mod.MAX_POLL_COUNT = 1
    mod.RETRY_DELAY_SECS = 0
    mod.DELAY_2_SECS = 0
    return mod


def resp(status=200, read='{"k":"v"}'):
    """Create a mock HTTP response object.

    :param status: HTTP status code
    :type status: int
    :param read: response body as JSON string
    :type read: str
    :returns: mock response with status and read attrs
    :rtype: MagicMock
    """
    mock_resp = mock.MagicMock(status=status)
    mock_resp.read = read
    mock_resp.dict = {}
    return mock_resp


def devnull():
    """Open /dev/null for stdout suppression.

    :returns: writable file object to /dev/null
    :rtype: file
    """
    return open(os.devnull, 'w')


def quiet():
    """Return context manager suppressing stdout.

    :returns: mock.patch context manager for sys.stdout
    :rtype: context manager
    """
    return mock.patch('sys.stdout', open(os.devnull, 'w'))
