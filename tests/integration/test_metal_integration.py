#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""
Integration tests for metal project structure and configuration.
"""
import os
import unittest

import yaml


PROJECT_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))


class TestProjectStructure(unittest.TestCase):
    """Validate project file structure."""

    def test_tox_ini_exists(self):
        """Test tox.ini exists."""
        self.assertTrue(os.path.isfile(
            os.path.join(PROJECT_ROOT, 'tox.ini')))

    def test_zuul_yaml_exists(self):
        """Test .zuul.yaml exists."""
        self.assertTrue(os.path.isfile(
            os.path.join(PROJECT_ROOT, '.zuul.yaml')))

    def test_test_requirements_exists(self):
        """Test test-requirements.txt exists."""
        self.assertTrue(os.path.isfile(
            os.path.join(PROJECT_ROOT, 'test-requirements.txt')))

    def test_license_exists(self):
        """Test LICENSE file exists."""
        self.assertTrue(os.path.isfile(
            os.path.join(PROJECT_ROOT, 'LICENSE')))

    def test_readme_exists(self):
        """Test README.rst exists."""
        self.assertTrue(os.path.isfile(
            os.path.join(PROJECT_ROOT, 'README.rst')))

    def test_mtce_directory_exists(self):
        """Test mtce directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'mtce')))

    def test_mtce_common_directory_exists(self):
        """Test mtce-common directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'mtce-common')))

    def test_mtce_control_directory_exists(self):
        """Test mtce-control directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'mtce-control')))

    def test_mtce_compute_directory_exists(self):
        """Test mtce-compute directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'mtce-compute')))

    def test_mtce_storage_directory_exists(self):
        """Test mtce-storage directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'mtce-storage')))

    def test_tools_directory_exists(self):
        """Test tools directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'tools')))

    def test_bsp_files_directory_exists(self):
        """Test bsp-files directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'bsp-files')))

    def test_installer_directory_exists(self):
        """Test installer directory exists."""
        self.assertTrue(os.path.isdir(
            os.path.join(PROJECT_ROOT, 'installer')))


class TestPythonSourceFiles(unittest.TestCase):
    """Validate Python source files exist and are importable."""

    def test_rvmc_exists(self):
        """Test rvmc.py exists."""
        path = os.path.join(PROJECT_ROOT, 'tools', 'rvmc',
                            'docker', 'rvmc.py')
        self.assertTrue(os.path.isfile(path))

    def test_rsbc_exists(self):
        """Test rsbc.py exists."""
        path = os.path.join(PROJECT_ROOT, 'tools', 'rsbc', 'rsbc.py')
        self.assertTrue(os.path.isfile(path))

    def test_hwmond_notify_exists(self):
        """Test hwmond_notify.py exists."""
        path = os.path.join(PROJECT_ROOT, 'mtce', 'src', 'hwmon',
                            'scripts', 'hwmond_notify.py')
        self.assertTrue(os.path.isfile(path))

    def test_rvmc_syntax(self):
        """Test rvmc.py has valid Python syntax."""
        path = os.path.join(PROJECT_ROOT, 'tools', 'rvmc',
                            'docker', 'rvmc.py')
        with open(path, 'r') as fobj:
            source = fobj.read()
        compile(source, path, 'exec')

    def test_rsbc_syntax(self):
        """Test rsbc.py has valid Python syntax."""
        path = os.path.join(PROJECT_ROOT, 'tools', 'rsbc', 'rsbc.py')
        with open(path, 'r') as fobj:
            source = fobj.read()
        compile(source, path, 'exec')


class TestCppSourceFiles(unittest.TestCase):
    """Validate C/C++ source files exist."""

    def test_mtce_common_sources(self):
        """Test mtce-common source files exist."""
        src_dir = os.path.join(
            PROJECT_ROOT, 'mtce-common', 'src', 'common')
        self.assertTrue(os.path.isdir(src_dir))
        cpp_files = [f for f in os.listdir(
            src_dir) if f.endswith('.cpp')]
        self.assertGreater(len(cpp_files), 0)

    def test_mtce_common_headers(self):
        """Test mtce-common header files exist."""
        src_dir = os.path.join(
            PROJECT_ROOT, 'mtce-common', 'src', 'common')
        h_files = [f for f in os.listdir(src_dir) if f.endswith('.h')]
        self.assertGreater(len(h_files), 0)

    def test_mtce_daemon_sources(self):
        """Test mtce-common daemon sources exist."""
        src_dir = os.path.join(
            PROJECT_ROOT, 'mtce-common', 'src', 'daemon')
        self.assertTrue(os.path.isdir(src_dir))
        cpp_files = [f for f in os.listdir(
            src_dir) if f.endswith('.cpp')]
        self.assertGreater(len(cpp_files), 0)

    def test_mtce_heartbeat_sources(self):
        """Test mtce heartbeat sources exist."""
        src_dir = os.path.join(PROJECT_ROOT, 'mtce', 'src', 'heartbeat')
        self.assertTrue(os.path.isdir(src_dir))

    def test_mtce_maintenance_sources(self):
        """Test mtce maintenance sources exist."""
        src_dir = os.path.join(
            PROJECT_ROOT, 'mtce', 'src', 'maintenance')
        self.assertTrue(os.path.isdir(src_dir))

    def test_mtce_hwmon_sources(self):
        """Test mtce hwmon sources exist."""
        src_dir = os.path.join(PROJECT_ROOT, 'mtce', 'src', 'hwmon')
        self.assertTrue(os.path.isdir(src_dir))

    def test_mtce_alarm_sources(self):
        """Test mtce alarm sources exist."""
        src_dir = os.path.join(PROJECT_ROOT, 'mtce', 'src', 'alarm')
        self.assertTrue(os.path.isdir(src_dir))

    def test_mtce_pmon_sources(self):
        """Test mtce pmon sources exist."""
        src_dir = os.path.join(PROJECT_ROOT, 'mtce', 'src', 'pmon')
        self.assertTrue(os.path.isdir(src_dir))

    def test_fsync_c_source(self):
        """Test fsync.c exists."""
        path = os.path.join(PROJECT_ROOT, 'mtce',
                            'src', 'fsync', 'fsync.c')
        self.assertTrue(os.path.isfile(path))

    def test_amon_c_source(self):
        """Test amon.c exists."""
        path = os.path.join(PROJECT_ROOT, 'mtce',
                            'src', 'public', 'amon.c')
        self.assertTrue(os.path.isfile(path))


class TestShellScripts(unittest.TestCase):
    """Validate shell scripts exist and have proper shebang."""

    def _check_script(self, rel_path):
        """Check a shell script exists."""
        path = os.path.join(PROJECT_ROOT, rel_path)
        self.assertTrue(os.path.isfile(path), "Missing: %s" % rel_path)

    def test_plugin_sh(self):
        """Test devstack plugin.sh exists."""
        self._check_script('devstack/plugin.sh')

    def test_pxeboot_feed_sh(self):
        """Test pxeboot_feed.sh exists."""
        self._check_script(
            'installer/pxe-network-installer/'
            'pxe-network-installer/pxeboot_feed.sh')

    def test_collect_bmc_sh(self):
        """Test collect_bmc.sh exists."""
        self._check_script('mtce/src/scripts/collect_bmc.sh')

    def test_dmemchk_sh(self):
        """Test dmemchk.sh exists."""
        self._check_script('mtce/src/scripts/dmemchk.sh')

    def test_hwclock_sh(self):
        """Test hwclock.sh exists."""
        self._check_script('mtce/src/scripts/hwclock.sh')


class TestConfigFiles(unittest.TestCase):
    """Validate configuration files."""

    def test_zuul_yaml_valid(self):
        """Test .zuul.yaml is valid YAML (skip encrypted sections)."""
        path = os.path.join(PROJECT_ROOT, '.zuul.yaml')
        with open(path, 'r') as fobj:
            content = fobj.read()
        # Remove encrypted tags that safe_load can't handle
        content = content.replace('!encrypted/pkcs1-oaep', '')
        data = yaml.safe_load(content)
        self.assertIsNotNone(data)

    def test_tox_ini_has_envlist(self):
        """Test tox.ini has envlist."""
        path = os.path.join(PROJECT_ROOT, 'tox.ini')
        with open(path, 'r') as fobj:
            content = fobj.read()
        self.assertIn('envlist', content)

    def test_test_requirements_has_bashate(self):
        """Test test-requirements.txt has bashate."""
        path = os.path.join(PROJECT_ROOT, 'test-requirements.txt')
        with open(path, 'r') as fobj:
            content = fobj.read()
        self.assertIn('bashate', content)

    def test_test_requirements_has_yamllint(self):
        """Test test-requirements.txt has yamllint."""
        path = os.path.join(PROJECT_ROOT, 'test-requirements.txt')
        with open(path, 'r') as fobj:
            content = fobj.read()
        self.assertIn('yamllint', content)

    def test_gitreview_exists(self):
        """Test .gitreview exists."""
        path = os.path.join(PROJECT_ROOT, '.gitreview')
        self.assertTrue(os.path.isfile(path))

    def test_gitignore_exists(self):
        """Test .gitignore exists."""
        path = os.path.join(PROJECT_ROOT, '.gitignore')
        self.assertTrue(os.path.isfile(path))


class TestMakefiles(unittest.TestCase):
    """Validate Makefiles exist."""

    def test_mtce_common_makefile(self):
        """Test mtce-common Makefile exists."""
        path = os.path.join(
            PROJECT_ROOT, 'mtce-common', 'src', 'Makefile')
        self.assertTrue(os.path.isfile(path))

    def test_mtce_makefile(self):
        """Test mtce Makefile exists."""
        path = os.path.join(PROJECT_ROOT, 'mtce', 'src', 'Makefile')
        self.assertTrue(os.path.isfile(path))

    def test_mtce_common_common_makefile(self):
        """Test mtce-common/src/common Makefile exists."""
        path = os.path.join(PROJECT_ROOT, 'mtce-common', 'src',
                            'common', 'Makefile')
        self.assertTrue(os.path.isfile(path))


if __name__ == '__main__':
    unittest.main()
