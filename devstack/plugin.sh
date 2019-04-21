#!/bin/bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright (C) 2019 Intel Corporation
#

# devstack/plugin.sh
# Triggers stx-metal specific functions to install and configure stx-metal

echo_summary "Metal devstack plugin.sh called: $1/$2"

# check for service enabled
if is_service_enabled metal; then
    if [[ "$1" == "stack" && "$2" == "pre-install" ]]; then
        # Pre-install requirties
        echo_summary "Pre-requires of stx-metal"
    elif [[ "$1" == "stack" && "$2" == "install" ]]; then
        # Perform installation of source
        echo_summary "Install stx-metal"
        # maintenance components should be installed in each node
        install_metal
    elif [[ "$1" == "stack" && "$2" == "post-config" ]]; then
        # Configure after the other layer 1 and 2 services have been configured
        echo_summary "Configure metal"
        configure_metal
    elif [[ "$1" == "stack" && "$2" == "extra" ]]; then
        # Initialize and start the metal services
        echo_summary "Initialize and start metal "
        # Start services on each node
        start_metal
    elif [[ "$1" == "stack" && "$2" == "test" ]]; then
        # do sanity test for metal
        echo_summary "do test"
    fi

    if [[ "$1" == "unstack" ]]; then
        # Shut down metal services
        echo_summary "Stop metal services"
        # Stop client services on each node
        stop_metal
    fi

    if [[ "$1" == "clean" ]]; then
        cleanup_metal
    fi
fi
