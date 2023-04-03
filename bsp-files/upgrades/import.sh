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

# Centos will be temporarily supported for testing. The import code
# for Centos 22.12 will be removed soon. No need for any fancy way
# to detect if it is a Debian iso, just check existance of ostree_repo
# TODO: remove the "else" clause
if [ -d ${ISO_DIR}/ostree_repo ]; then
    # it is a Debian iso.

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
    # only copy the patch meta is enough
    if [ -d ${ISO_DIR}/patches ]; then
        rsync -ac ${ISO_DIR}/patches/ /opt/patching/metadata/committed/
        # copy patch metadata to feed, so to be picked up by kickstart to copy to release N+1
        mkdir ${FEED_DIR}/patches -p
        cp -a ${ISO_DIR}/patches/* ${FEED_DIR}/patches/
    fi
else
    # this is a Centos iso
    trap cleanup 0

    TMP_RPM=/tmp/cpio

    rm -rf ${TMP_RPM}

    cp -rp ${ISO_DIR}/Packages ${ISO_DIR}/repodata ${ISO_DIR}/LiveOS ${FEED_DIR}/

    cp -p ${CURRENT_FEED_DIR}/install_uuid ${FEED_DIR}/

    if [ -d ${ISO_DIR}/patches ]; then
        mkdir -p /var/www/pages/updates/rel-${VERSION}
        cp -r ${ISO_DIR}/patches/Packages ${ISO_DIR}/patches/repodata /var/www/pages/updates/rel-${VERSION}/
        rsync -ac ${ISO_DIR}/patches/metadata/ /opt/patching/metadata/
        mkdir -p /opt/patching/packages/${VERSION}

        find /var/www/pages/updates/rel-${VERSION}/Packages -name '*.rpm' \
            | xargs --no-run-if-empty -I files cp --preserve=all files /opt/patching/packages/${VERSION}/
    fi

    # copy package checksum if it exists

    PKG_FILE="package_checksums"
    PKG_FILE_LOC=/usr/local/share/pkg-list

    if [ -f ${ISO_DIR}/${PKG_FILE} ]; then

        DEST_PKG_FILE="${VERSION}_packages_list.txt"
        if [ ! -d ${PKG_FILE_LOC} ]; then
            mkdir -p ${PKG_FILE_LOC}
        fi

        cp ${ISO_DIR}/${PKG_FILE} ${PKG_FILE_LOC}/${DEST_PKG_FILE}
        cp ${ISO_DIR}/${PKG_FILE} ${FEED_DIR}/${PKG_FILE}
    fi
fi

echo 'import has completed'
