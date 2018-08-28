#!/bin/bash
# Copyright (c) 2015-2017 Wind River Systems, Inc.
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
    rm -rf $TMP_RPM
}
rollback() {
    rm -rf $FEED_DIR
}

error() {
    local parent_lineno="$1"
    local err_code="${2}"
    echo "Error executing import script at line: ${parent_lineno} with error code: ${err_code}"
    rollback
    exit "${err_code}"
}

trap 'error ${LINENO} $?' ERR
trap cleanup 0

SCRIPT_DIR=$(dirname $0)
ISO_DIR=$(dirname $SCRIPT_DIR)

source $SCRIPT_DIR/version
source /etc/build.info

FEED_DIR=/www/pages/feed/rel-$VERSION
CURRENT_FEED_DIR=/www/pages/feed/rel-$SW_VERSION
TMP_RPM=/tmp/cpio

rm -rf $TMP_RPM
rm -rf $FEED_DIR

mkdir -p $FEED_DIR

cp -rp $ISO_DIR/Packages $ISO_DIR/repodata $ISO_DIR/LiveOS $FEED_DIR/

cp -p $CURRENT_FEED_DIR/install_uuid $FEED_DIR/

if [ -d $ISO_DIR/patches ]; then
    mkdir -p /www/pages/updates/rel-${VERSION}
    cp -r ${ISO_DIR}/patches/Packages ${ISO_DIR}/patches/repodata /www/pages/updates/rel-${VERSION}/
    rsync -ac ${ISO_DIR}/patches/metadata/ /opt/patching/metadata/
    mkdir -p /opt/patching/packages/${VERSION}

    find /www/pages/updates/rel-${VERSION}/Packages -name '*.rpm' \
        | xargs --no-run-if-empty -I files cp --preserve=all files /opt/patching/packages/${VERSION}/
fi

