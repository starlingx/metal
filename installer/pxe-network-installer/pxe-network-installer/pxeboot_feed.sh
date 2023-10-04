#!/bin/bash
#############################################################################
# Copyright (c) 2022 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# This utility is used to create or update the following directories
#
#    /var/www/pages/feed/rel-xx.xx/pxeboot
#    /var/pxeboot/rel-xx.xx
#
# ... with the kernel, initrd and other images and signature files from /boot
#
# This utility is also used to sync the /var/www/pages/feed/rel-xx.xx/kickstart
# directory with the kickstart directory from /ostree.
#
#############################################################################
#
# chkconfig: 2345 98 2
#
### BEGIN INIT INFO
# Provides: pxeboot_feed
# Required-Start: $null
# Required-Stop: $null
# Default-Start: 3 5
# Default-Stop: 0 1 2 6
# Short-Description: StarlingX Installer Pxeboot Feed Refresh
### END INIT INFO

# Script has options 'debug' argument
debug=false
[ -n "${1}" ] && [ "${1}" == "debug" ] && debug=true

LOG_TAG=${LOG_TAG:-$(basename "${0}")}

# return code
RETVAL=1

#############################################################################
# Name     : ilog
# Purpose  : log info message
# Parmaeter: message to log
# Returns  : none
#############################################################################

function ilog {
    logger -t "${LOG_TAG}" "${@}"
}

#############################################################################
# Name     : dlog
# Purpose  : log and echo debug messages
# Parmaeter: message to log
# Returns  : none
#############################################################################

function dlog {
    if [ "${debug}" == true ] ; then
        logger -t "${LOG_TAG}" "${@}"
    fi
}

#############################################################################
# Name       : rsync_if_not_equal
# Purpose    : Speed up the refresh service.
# Assumptions: Reads are faster thyan writes
#              Equal case is more likely
# Parameters : $1 - src path/file
#              $2 - dst path/file
# Returns    : none
#############################################################################

function rsync_if_not_equal {
    local src_file="${1}"
    local dst_file="${2}"
    local need_rsync=false

    if [ -e "${src_file}" ] ; then
        if [ -e "${dst_file}" ] ; then
            src=( $(md5sum "${src_file}") )
            dst=( $(md5sum "${dst_file}") )
            if [ "${src[0]}" == "${dst[0]}" ] ; then
                dlog "bypass rsync ; ${src_file}" and "${dst_file} are equal"
            else
                need_rsync=true
            fi
        else
            need_rsync=true
        fi
    else
        ilog "Warning: '${src_file}' not found"
    fi

    if [ "${need_rsync}" = true ] ; then
        ilog "syncing ${src_file} to ${dst_file}"
        rsync "${src_file}" "${dst_file}"
    fi
}

#############################################################################

# Override release with what is found in platform.conf
rel=""
if [ -e "/etc/platform/platform.conf" ] ; then
    rel=$(grep sw_version < /etc/platform/platform.conf | cut -d '=' -f 2)
fi
if [ -z "${rel}" ] ; then
    rel="xxxSW_VERSIONxxx"
fi

# pxeboot objects path
feed="/var/www/pages/feed/rel-${rel}"
pxefeed="${feed}/pxeboot"
pxeboot="/var/pxeboot"

# ensure the deepest directories are created
if [ ! -d "${pxefeed}/EFI/BOOT" ] ; then
    mkdir -p "${pxefeed}/EFI/BOOT" > /dev/null 2>&1 || exit ${RETVAL}
fi
if [ ! -d "${pxeboot}rel-${rel}" ] ; then
    mkdir -p "${pxeboot}/rel-${rel}" > /dev/null 2>&1 || exit ${RETVAL}
fi
if [ ! -d "${pxeboot}/EFI/BOOT" ]  ; then
    mkdir -p "${pxeboot}/EFI/BOOT" > /dev/null 2>&1 || exit ${RETVAL}
fi

base_path="/boot/ostree"
declare -a file_list=()

if [ ! -d "${base_path}" ] ; then
    ilog "Error: base path '${base_path}' does not exist"
    exit ${RETVAL}
fi

file_list=( $(find "${base_path}" -name 'initramfs*') )
file_list+=( $(find "${base_path}" -name 'vmlinuz*') )
dlog "${file_list[*]}"
for f in "${file_list[@]}" ; do
    path_file1=""
    filename=$(basename "${f}")
    dlog "File: ${filename} ... ${f}"
    if [ "${filename}" == "initramfs.sig" ] ; then
        path_file1="${pxeboot}/rel-${rel}/initrd.sig"
        path_file2="${pxefeed}/initrd.sig"
    elif [ "${filename}" == "initramfs" ] ; then
        path_file1="${pxeboot}/rel-${rel}/initrd"
        path_file2="${pxefeed}/initrd"
    elif [ "${filename}" == "vmlinuz.sig" ] ; then
        path_file1="${pxeboot}/rel-${rel}/bzImage.sig"
        path_file2="${pxefeed}/bzImage.sig"
    elif [ "${filename}" == "vmlinuz" ] ; then
        path_file1="${pxeboot}/rel-${rel}/bzImage"
        path_file2="${pxefeed}/bzImage"
    elif [[ "${filename}" == *"rt-amd64.sig"* ]] ; then
        path_file1="${pxeboot}/rel-${rel}/bzImage-rt.sig"
        path_file2="${pxefeed}/bzImage-rt.sig"
    elif [[ "${filename}" == *"rt-amd64"* ]] ; then
        path_file1="${pxeboot}/rel-${rel}/bzImage-rt"
        path_file2="${pxefeed}/bzImage-rt"
    elif [[ "${filename}" == *"amd64.sig"* ]] ; then
        path_file1="${pxeboot}/rel-${rel}/bzImage-std.sig"
        path_file2="${pxefeed}/bzImage-std.sig"
    elif [[ "${filename}" == *"amd64"* ]] ; then
        path_file1="${pxeboot}/rel-${rel}/bzImage-std"
        path_file2="${pxefeed}/bzImage-std"
    else
        ilog "ignoring unknown file: ${f}"
        continue
    fi

    rsync_if_not_equal "${f}" "${path_file1}"
    rsync_if_not_equal "${f}" "${path_file2}"
done


# Other image files
file1="LockDown.efi.sig"
file2="LockDown.efi"
file3="bootx64.efi"
file4="grub.cfg.sig"
file5="grubx64.efi"
file6="mmx64.efi"

file_list=( $(find "${base_path}" \
    -name "${file1}" -o \
    -name "${file2}" -o \
    -name "${file3}" -o \
    -name "${file4}" -o \
    -name "${file5}" -o \
    -name "${file6}") )

dlog "${file_list[*]}"

for f in "${file_list[@]}" ; do
    filename=$(basename "${f}")
    dlog "File: ${filename} ... ${f}"

    path_file="EFI/BOOT/${filename}"
    path_file1=""

    if [[ "${filename}" == *"${file1}"* || \
        "${filename}" == *"${file2}"* || \
        "${filename}" == *"${file3}"* || \
        "${filename}" == *"${file4}"* || \
        "${filename}" == *"${file5}"* || \
        "${filename}" == *"${file6}"* ]] ; then
        path_file1="${pxeboot}/${path_file}"
        path_file2="${pxefeed}/${path_file}"
    else
        ilog "ignoring unknown file: ${f}"
        continue
    fi

    rsync_if_not_equal "${f}" "${path_file1}"
    rsync_if_not_equal "${f}" "${path_file2}"
done

# rsync efi.img file
rsync_if_not_equal "${pxeboot}/efi.img" "${feed}/efi.img"

# Refresh the kickstarts feed
kickstarts_feed="${feed}/kickstart"
kickstarts_deploy="/ostree/1""${kickstarts_feed}"

if [ ! -d "${kickstarts_deploy}" ] ; then
    ilog "Error: deploy path '${kickstarts_deploy}' does not exist"
    exit ${RETVAL}
fi

ilog "syncing ${kickstarts_deploy} to ${kickstarts_feed}"
rsync -a --delete "${kickstarts_deploy}/" "${kickstarts_feed}"

RETVAL=0
exit ${RETVAL}
