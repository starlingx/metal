#!/bin/bash
##############################################################################
#
# Copyright (c) 2025 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
##############################################################################
#
# Name     : delayed_sysrq_reboot.sh
#
# Purpose  : Backup reboot mechanism that triggers a SYSRQ forced reset
#            after a specified delay.
#
# Usage    : Used as a backup to force a reset if systemd shutdown stalls for
#            too long or fails to reboot.
#
#            This script is typically launched by the mtcAgent and/or mtcClient
#            via `systemd-run` as an isolated transient service.
#
# Arguement: Accepts a single argument that specifies the delay before SYSRQ
#
# Usage    : delayed_sysrq_reboot.sh <delay_seconds>
#
##############################################################################

LOGGER_TAG=$(basename "$0")

# log to both console and syslog
function ilog {
    echo "$@"
    logger -t "${LOGGER_TAG}" "$@"
}

# Check if an argument is provided
if [ $# -ne 1 ]; then
    ilog "Usage: $0 <seconds_to_delay>"
    exit 1
fi

DELAY="$1"

# Ensure it's a non-negative integer between 1 and 86400 (24h)
if ! [[ "$DELAY" =~ ^[0-9]+$ ]] || [ "$DELAY" -le 0 ] || [ "$DELAY" -gt 300 ]; then
    ilog "Error: delay must be a positive integer between 1 and 300 seconds"
    exit 1
fi

# Check if script is run as root (required for /proc/sysrq-trigger)
if [ "$EUID" -ne 0 ]; then
    ilog "Error: script must be run as root"
    exit 1
fi

# Check for sysrq file
if [ ! -w "/proc/sysrq-trigger" ]; then
    ilog "Error: /proc/sysrq-trigger is not writable"
    exit 1
fi

ilog "Delaying for $DELAY seconds before issuing SysRq reboot ..."
sleep "$DELAY"

# ensure sysrq is enabled (bitmask 1 = reboot allowed)
if [ -f "/proc/sys/kernel/sysrq" ]; then
    SYSRQ_STATE=$(cat /proc/sys/kernel/sysrq)
    if [ "$SYSRQ_STATE" -eq 0 ]; then
        ilog "SysRq is disabled; enabling"
        echo 1 > /proc/sys/kernel/sysrq
    fi
fi

ilog "Triggering forced reboot via /proc/sysrq-trigger"
echo b > /proc/sysrq-trigger

# Should not get here unless reboot fails
ilog "Warning: reboot trigger failed or was blocked"

exit 1
