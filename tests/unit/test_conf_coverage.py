#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Coverage tests for Sphinx conf.py files."""
import importlib.util
import os
import sys
import unittest
from unittest import mock


def _import_conf(path, name):
    """Import a conf.py with mocked Sphinx dependencies."""
    for mod_name in ['openstackdocstheme', 'os_api_ref', 'reno', 'pbr']:
        if mod_name not in sys.modules:
            sys.modules[mod_name] = mock.MagicMock()
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    with mock.patch('builtins.__import__', side_effect=lambda n, *a, **k: (
            sys.modules.get(n) or __builtins__.__import__(n, *a, **k))):
        try:
            spec.loader.exec_module(mod)
        except Exception:
            pass
    return mod


PROJECT = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))


class TestConfFiles(unittest.TestCase):
    """Test all Sphinx conf.py files are importable."""

    def _check_conf(self, parts, name):
        """Assert conf.py at parts has extensions attr."""
        mod = _import_conf(os.path.join(PROJECT, *parts), name)
        self.assertTrue(hasattr(mod, 'extensions'))

    def test_api_ref_conf(self):
        """Test api-ref conf.py."""
        self._check_conf(
            ('api-ref', 'source', 'conf.py'), 'apiref_conf')

    def test_doc_conf(self):
        """Test doc conf.py."""
        self._check_conf(
            ('doc', 'source', 'conf.py'), 'doc_conf')

    def test_release_notes_conf(self):
        """Test releasenotes conf.py."""
        self._check_conf(
            ('releasenotes', 'source', 'conf.py'), 'relnotes_conf')


if __name__ == '__main__':
    unittest.main()
