#! /bin/bash
#
# Copyright (c) 2015-2017 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

### CONFIG_START
#
# Timeout:  300
#
### CONFIG_END

# Platform paths and flags
. /usr/bin/tsconfig

# Linux Standard Base (LSB) Error Codes
SUCCESS=0
GENERIC_ERROR=1

NOVA_GOENABLED_TAG=${NOVA_GOENABLED_TAG:-"NOVA_GOENABLED"}

function log
{
    logger -p local1.info -t ${NOVA_GOENABLED_TAG} $@
}

NOVA_RUN="/var/run/nova"
NOVA_INIT_FAILED="${NOVA_RUN}/.nova_init_failed"
NOVA_COMPUTE_ENABLED="${NOVA_RUN}/.nova_compute_enabled"
NOVA_ADVANCE_ENABLED="/var/run/.nova_timer_advance_enabled"

case "$1" in
    start)
        if [ -e ${VOLATILE_COMPUTE_CONFIG_COMPLETE} ] && [ ! -e ${VOLATILE_DISABLE_COMPUTE_SERVICES} ]
        then
            log "Start"

            if [ -e ${NOVA_INIT_FAILED} ]
            then
                log "Nova-Init check FAILED"
                exit ${GENERIC_ERROR}
            fi

            log "Nova-Init check PASSED"

            while :
            do
                if [ -e ${NOVA_ADVANCE_ENABLED} ]
                then
                    log "Nova setup timer advance PASSED"
                    break
                fi

                sleep 1
            done

            while :
            do
                if [ -e ${NOVA_COMPUTE_ENABLED} ]
                then
                    log "Nova-Compute service enabled PASSED"
                    break
                fi

                sleep 1
            done

            log "Finished"
        fi
        ;;

    stop)
        ;;

    *)
        ;;
esac

exit ${SUCCESS}
