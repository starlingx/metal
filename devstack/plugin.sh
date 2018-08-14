#!/bin/bash

# devstack/plugin.sh
# Triggers stx-metal specific functions to install and configure stx-metal

echo_summary "Metal devstack plugin.sh called: $1/$2"

# check for service enabled
if is_service_enabled stx-metal; then
    if [[ "$1" == "stack" && "$2" == "pre-install" ]]; then
        # Pre-install requirties
        echo_summary "Pre-requires of stx-metal"
    elif [[ "$1" == "stack" && "$2" == "install" ]]; then
        # Perform installation of source
        echo_summary "Install metal"
        # maintenance components should be installed in each node
        install_maintenance
        if is_service_enabled mtce-compute; then
            echo_summary "Install mtce compute specific services"
            install_mtce_compute
        fi
        if is_service_enabled mtce-control; then
            echo_summary "Install mtce control specific services"
            install_mtce_control
        fi
        if is_service_enabled mtce-storage; then
            echo_summary "Install mtce storage specific services"
            install_mtce_storage
        fi
    elif [[ "$1" == "stack" && "$2" == "post-config" ]]; then
        # Configure after the other layer 1 and 2 services have been configured
        echo_summary "Configure metal"
        configure_maintenance
    elif [[ "$1" == "stack" && "$2" == "extra" ]]; then
        # Initialize and start the metal services
        echo_summary "Initialize and start metal "
        if is_service_enabled mtce-control; then
            echo_summary "Start mtce control specific services"
            # Start Agents on controller node
            start_mtce_control
        fi
        if is_service_enabled mtce-compute; then
            echo_summary "Start mtce compute specific services"
            start_mtce_compute
        fi
        if is_service_enabled mtce-storage; then
            echo_summary "Start mtce storage specific services"
            start_mtce_storage
        fi
        # Start services on each node
        start_maintenance
    elif [[ "$1" == "stack" && "$2" == "test" ]]; then
        # do sanity test for metal
        echo_summary "do test"
    fi

    if [[ "$1" == "unstack" ]]; then
        # Shut down metal services
        echo_summary "Stop metal services"
        # Stop client services on each node
        stop_maintenance
        if is_service_enabled mtce-control; then
            echo_summary "Stop mtce control specific services"
            # Stop Agents on controller node
            stop_mtce_control
        fi
        if is_service_enabled mtce-compute; then
            echo_summary "Stop mtce compute specific services"
            stop_mtce_compute
        fi
        if is_service_enabled mtce-storage; then
            echo_summary "Stop mtce storage specific services"
            stop_mtce_storage
        fi
    fi

    if [[ "$1" == "clean" ]]; then
        cleanup_metal
    fi
fi
