#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""
Shared test fixtures for metal project tests.
"""
import os
import sys
import tempfile

import pytest


@pytest.fixture
def project_root():
    """Return the project root directory."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


@pytest.fixture
def temp_dir():
    """Provide a temporary directory for test artifacts."""
    with tempfile.TemporaryDirectory() as tmpdir:
        yield tmpdir


@pytest.fixture
def sample_xml_groups(temp_dir):
    """Create a sample groups XML file for platform_comps tests."""
    content = ('<?xml version="1.0" encoding="UTF-8"?>\n'
               '<comps>\n</comps>\n')
    path = os.path.join(temp_dir, 'groups.xml')
    with open(path, 'w') as fobj:
        fobj.write(content)
    return path


@pytest.fixture
def sample_filter_file(temp_dir):
    """Create a sample filter file."""
    path = os.path.join(temp_dir, 'filter_out_from_controller')
    with open(path, 'w') as fobj:
        fobj.write("pkg_to_filter\nanother_filtered\n")
    return temp_dir


@pytest.fixture
def sample_pkglist(temp_dir):
    """Create a sample package list file."""
    path = os.path.join(temp_dir, 'pkglist.txt')
    with open(path, 'w') as fobj:
        fobj.write("pkg_a\npkg_b\npkg_to_filter\npkg_c\n")
    return path


@pytest.fixture
def rvmc_config_file(temp_dir):
    """Create a sample rvmc YAML config file."""
    from tests import constants as tc
    content = (
        "bmc_address: 10.10.10.1\n"
        "bmc_username: " + tc.BMC_USERNAME + "\n"
        "bmc_password: " + tc.BMC_PASSWORD_ENCODED + "\n"
        "image: " + tc.BMC_IMAGE_URL_FULL + "\n"
    )
    path = os.path.join(temp_dir, 'rvmc.yaml')
    with open(path, 'w') as fobj:
        fobj.write(content)
    return path


@pytest.fixture
def rvmc_multi_config(temp_dir):
    """Create a multi-target rvmc YAML config file."""
    from tests import constants as tc
    content = (
        "virtual_media_iso:\n"
        "    target1:\n"
        "        bmc_address: 10.10.10.1\n"
        "        bmc_username: " + tc.BMC_USERNAME + "\n"
        "        bmc_password: " + tc.BMC_PASSWORD_ENCODED + "\n"
        "        image: " + tc.BMC_IMAGE_URL_FULL + "\n"
        "    target2:\n"
        "        bmc_address: 10.10.10.2\n"
        "        bmc_username: " + tc.BMC_USERNAME + "\n"
        "        bmc_password: " + tc.BMC_PASSWORD_ENCODED + "\n"
        "        image: " + tc.BMC_IMAGE_URL_FULL + "\n"
    )
    path = os.path.join(temp_dir, 'rvmc_multi.yaml')
    with open(path, 'w') as fobj:
        fobj.write(content)
    return path
