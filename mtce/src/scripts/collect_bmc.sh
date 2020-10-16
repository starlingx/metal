#! /bin/bash
#
# Copyright (c) 2020 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


# Loads Up Utilities and Commands Variables

source /usr/local/sbin/collect_parms
source /usr/local/sbin/collect_utils

SERVICE="bmc"
LOGFILE="${extradir}/${SERVICE}.info"


CRASHDIR="/var/crash"

echo    "${hostname}: BMC Info ..........: ${LOGFILE}"

COMMAND="echo BMC  Date: `ipmitool sel time get`"
delimiter ${LOGFILE} "${COMMAND}"
${COMMAND} >> ${LOGFILE} 2>>${COLLECT_ERROR_LOG}

COMMAND="echo 'Host Date: `date`'"
delimiter ${LOGFILE} "${COMMAND}"
${COMMAND} >> ${LOGFILE} 2>>${COLLECT_ERROR_LOG}

COMMAND="ipmitool sel"
delimiter ${LOGFILE} "${COMMAND}"
${COMMAND} >> ${LOGFILE} 2>>${COLLECT_ERROR_LOG}

COMMAND="ipmitool sel list"
delimiter ${LOGFILE} "${COMMAND}"
${COMMAND} >> ${LOGFILE} 2>>${COLLECT_ERROR_LOG}

COMMAND="ipmitool sensor list"
delimiter ${LOGFILE} "${COMMAND}"
${COMMAND} >> ${LOGFILE} 2>>${COLLECT_ERROR_LOG}

exit 0

