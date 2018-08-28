#!/bin/bash
#
# Copyright (c) 2015-2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

source "/etc/init.d/log_functions.sh"

# is it a compute subfunction on a CPE system
isCompute ()
{
    [ -f /etc/platform/platform.conf ] || return 0
    res=$(grep "subfunction" /etc/platform/platform.conf | grep "controller,compute" | wc -l)

    if [ "$res" -eq 0 ] ; then
        return 0
    else
        return 1
    fi
}

# only reload rmon if it is a CPE system
isCompute

if [[ "$?" -eq 0 ]]; then
    log "Cannot run on a non CPE system."
    exit 0
fi

if [ ! -f /var/run/.compute_config_complete ]; then
    log "Cannot run prior to compute configuration complete."
    exit 0
fi

#################################################################################################
# Temporarily switch this to a process kill instead of reload due to a problem found
# in the rmon config reload handling. A clone Jira was created to track the fix that will migrate
# this back to a reload.
#################################################################################################
# rc=`pkill -hup rmond`
# log "rmond config reload (rc=$rc)"

/usr/local/sbin/pmon-restart rmond
logger "requesting graceful rmon restart in goenabled test on cpe"

exit 0
