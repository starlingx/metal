#
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
"""Root conftest - adds source directories to sys.path."""
import os
import sys

_root = os.path.dirname(os.path.abspath(__file__))

for _path in [
    os.path.join(_root, 'tools', 'rvmc', 'docker'),
    os.path.join(_root, 'tools', 'rsbc'),
    os.path.join(_root, 'mtce', 'src', 'hwmon', 'scripts'),
]:
    if _path not in sys.path:
        sys.path.insert(0, _path)
