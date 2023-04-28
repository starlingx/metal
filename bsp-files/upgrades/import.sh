#!/bin/bash
# Copyright (c) 2015-2022 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# This script is run during the load-import command
# It is used to copy the required files from the iso to the
# controller.


set -e

exec 2>>/var/log/load-import.log
set -x
echo "$(date): Starting execution: $0 $@"

cleanup() {
    rm -rf ${TMP_RPM}
}
rollback() {
    rm -rf ${FEED_DIR}
}

error() {
    local parent_lineno="$1"
    local err_code="${2}"
    echo "Error executing import script at line: ${parent_lineno} with error code: ${err_code}"
    rollback
    exit "${err_code}"
}

trap 'error ${LINENO} $?' ERR

SCRIPT_DIR=$(dirname $0)
ISO_DIR=$(dirname $SCRIPT_DIR)

source $SCRIPT_DIR/version
source /etc/build.info

FEED_DIR=/var/www/pages/feed/rel-${VERSION}

# Feed directory is different in 21.12 vs. 22.06
CURRENT_FEED_DIR=/var/www/pages/feed/rel-${SW_VERSION}
if [ ${SW_VERSION} == "21.12" ]; then
    CURRENT_FEED_DIR=/www/pages/feed/rel-${SW_VERSION}
    FEED_DIR=/www/pages/feed/rel-${VERSION}
fi

rm -rf ${FEED_DIR}
mkdir -p ${FEED_DIR}

# copy pxeboot, kickstart, ostree_repo to feed directory
echo "Copy kickstart to ${FEED_DIR}"
cp -rp ${ISO_DIR}/kickstart ${FEED_DIR}/
echo "Copy pxeboot to ${FEED_DIR}"
cp -rp ${ISO_DIR}/pxeboot ${FEED_DIR}/
echo "Copy ostree_repo to ${FEED_DIR}"
cp -rp ${ISO_DIR}/ostree_repo ${FEED_DIR}/

echo "Copy install_uuid to ${FEED_DIR}"
cp ${CURRENT_FEED_DIR}/install_uuid ${FEED_DIR}/

mkdir ${FEED_DIR}/upgrades
echo "Copy pxeboot-update-${VERSION}.sh to ${FEED_DIR}/upgrades"
cp ${ISO_DIR}/upgrades/pxeboot-update-${VERSION}.sh ${FEED_DIR}/upgrades/

echo "Copy efi.img to ${FEED_DIR}"
cp ${ISO_DIR}/efi.img ${FEED_DIR}/
# for upgrade from 22.06 to Debian 22.12, patch during upgrade is not supported

echo "Copy pxeboot-update-${SW_VERSION}.sh to ${CURRENT_FEED_DIR}/upgrades"
mkdir -p ${CURRENT_FEED_DIR}/upgrades/
# In stx 8.0, the pxeboot-update-22.12.sh is in /usr/sbin
# In stx 9.0, the pxeboot-update-23.09.sh is in /etc due to the ostree managed
# /usr directory.
cp  -rp /usr/sbin/pxeboot-update-22.12.sh ${CURRENT_FEED_DIR}/upgrades/ 2>/dev/null || \
cp  -rp /etc/pxeboot-update-${SW_VERSION}.sh ${CURRENT_FEED_DIR}/upgrades/

# The pxelinux.cfg.files directory is from the current release feed in Debian.
echo "Copy pxelinux.cfg.files directory to ${CURRENT_FEED_DIR}"
mkdir -p ${CURRENT_FEED_DIR}/pxeboot/pxelinux.cfg.files/
find /var/pxeboot/pxelinux.cfg.files -type f ! -name "*${VERSION}" \
-exec cp -p {} ${CURRENT_FEED_DIR}/pxeboot/pxelinux.cfg.files/ \;

echo 'import has completed'
