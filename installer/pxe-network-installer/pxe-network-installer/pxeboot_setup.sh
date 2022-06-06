#!/bin/bash
#
# Copyright (c) 2016-2022 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
############################################################################
#
# This script is used to create lab specific controller-0 install grub menus.
# from template files through the following variable replacements.
#
# xxxPXEBOOTxxx  : Replaces this string with the filesystem path between
#                  /pxeboot and where the kernel/initrd files are stored.
#
#        example : --pxeboot vlm-boards/some_system_name/feed
#
#                  Will then replace all instances in template files of ...
#
#                  'xxxPXEBOOTxxx' with 'vlm-boards/some_system_name/feed'
#
# xxxFEEDxxx : replace with pxeboot path to server's install feed
#
#        example : --feed umalab/some_server_name_feed
#
#                  Will then replace all instances in template files of ...
#
#                  'xxxFEEDxxx' with 'umalab/some_server_name_feed'
#
# xxxBASE_URLxxx : replace with url path to server's install feed
#
#        example : --url http://###.###.###.###
#
#                  Will then replace all instances in template files of ...
#
#                  xxxBASE_URLxxx with http://###.###.###.###
#
# Optional Settings
#
#  xxxINSTDEVxxx : set the install device ; defaults to /dev/sda if missing
#        example : --device /dev/sde
#
#   xxxSYSTEMxxx : specify the install system type ; default is AIO '2'
#        example : --system 2
#
#                 system type values
#
#                 0 - disk boot
#                 1 - controller
#                 2 - All-in-one (standard)
#                 3 - All-in-one (lowlatency)
#
# Refer to usage function for option details.
#
############################################################################

trap ctrl_c INT
function ctrl_c {
    echo "Exiting ..."
    exit 1
}

# Debian Legacy BIOS and UEFI controller-0 install grub file names constants
BIOS_PXEBOOT_GRUB_FILE='pxeboot.cfg.debian'
UEFI_PXEBOOT_GRUB_FILE='efi-pxeboot.cfg.debian'

# User specified path to the menu templates directory ; default empty
MENUS_PATH=""

# server specific values
BASE_URL=""         # the port based URL
FEED_PATH=""        # path to the feed directory ; the mounted iso
PXE_PATH=""         # offset path between /pxeboot and feed
GRUB_PATH=""        # where to put the created grub files

# Optional Settings with default
INSTDEV="/dev/sda"   # default install device to sda

TTY=0                # default of ttyS0

SYSTEM=""            # default to menu
BIOS_SYSTEM=menu.c32 # Display the install menu by default
UEFI_SYSTEM=menu.c32 # Display the install menu by default

# The index of this array maps to the system install types specified
# by the --system option. See below.
UEFI_SYSTEM_TYPES_str=('disk' \
                        'standard>serial' \
                        'standard>graphical' \
                        'aio>serial' \
                        'aio>graphical' \
                        'aio-lowlat>serial' \
                        'aio-lowlat>graphical')

BACKUP=false

echo ""

function usage {
    RC=0
    [ ! -z "${1}" ] && RC="${1}"
    echo ""
    echo "Usage: $0 [Arguments Options]"
    echo ""
    echo "Arguments:"
    echo ""
    echo " -i | --input   <input path>     : Path to ${BIOS_PXEBOOT_GRUB_FILE} and ${UEFI_PXEBOOT_GRUB_FILE} grub template files"
    echo " -o | --output  <output path>    : Path to created ${BIOS_PXEBOOT_GRUB_FILE} and ${UEFI_PXEBOOT_GRUB_FILE} grub files"
    echo " -p | --pxeboot <pxeboot path>   : Offset path between /pxeboot and bzImage/initrd"
    echo " -f | --feed    <feed path>      : Offset path between http server base and mounted iso"
    echo " -u | --url     <pxe server url> : The pxeboot server's URL"
    echo ""
    echo "Options:"
    echo ""
    echo " -h | --help                     : Print this help info"
    echo " -b | --backup                   : Create backup of updated grub files as .named files"
    echo " -d | --device <install device>  : Install device path ; default: /dev/sda"
    echo " -s | --system <system install>  : System install type ; default: 3"
    echo " -t | --tty <port>               : Override ttyS0 with port number 0..3 ; default 0 "
    echo ""
    echo "        0 = Disk Boot"
    echo "        1 = Controller Install - Serial Console"
    echo "        2 = Controller Install - Graphical Console"
    echo "        3 = All-in-one Install - Serial Console       (default)"
    echo "        4 = All-in-one Install - Graphical Console"
    echo "        5 = All-in-one (lowlatency) Install - Serial Console"
    echo "        6 = All-in-one (lowlatency) Install - Graphical Console"
    echo ""
    echo ""
    echo "Example:"
    echo ""
    echo "pxeboot_setup.sh -i /path/to/grub/template/dir"
    echo "                 -o /path/to/target/iso/mount"
    echo "                 -p pxeboot/offset/to/bzImage_initrd"
    echo "                 -f pxeboot/offset/to/target_feed"
    echo "                 -u http://###.###.###.###"
    echo "                 -d /dev/sde"
    echo "                 -s 5"

    exit "${RC}"
}

[ "${*}" == "" ] && usage

# Process input options
script=$(basename "$0")
OPTS=$(getopt -o bd:f:hi:o:p:s:t:u: \
                --long backup,device:,feed:,help,input:,output:,pxeboot:,system:,tty:,url: \
                -n "${script}" -- "$@")
if [ $? != 0 ]; then
    echo "Failed parsing options." >&2
    usage 1
fi
eval set -- "$OPTS"
while true; do
    case "$1" in

    -b|--backup)
        BACKUP=true
        shift
        ;;
    -d|--device)
        INSTDEV="${2}"
        shift 2
        ;;
    -f|--feed)
        FEED_PATH="${2}"
        shift 2
        ;;
    -h|--help)
        usage 0
        shift
        ;;
    -i|--input)
        MENUS_PATH="${2}"
        shift 2
        ;;
    -o|--output)
        GRUB_PATH="${2}"
        shift 2
        ;;
    -p|--pxeboot)
        PXE_PATH="${2}"
        shift 2
        ;;
    -t|--tty)
        if echo "${2}" | grep -q -x '[0-9]\+' && [ ${2} -ge 0 ] && [ ${2} -le 3 ]; then
            TTY="${2}"
        else
            echo "Error: ttySx where x=${2} is out of range [0..3]"
            usage 1
        fi
        shift 2
        ;;

    -s|--system)
        if echo "${2}" | grep -q -x '[0-9]\+' && [ ${2} -ge 0 ] && [ ${2} -le 6 ]; then
            SYSTEM=${2}
            # UEFI: Translate the incoming system type to a string that
            #       specifies the 'default' menu path to be taken.
            UEFI_SYSTEM="${UEFI_SYSTEM_TYPES_str[${2}]}"

            # BIOS menu uses the system type number directly.
            BIOS_SYSTEM="${2}"
        else
            echo "Error: system install type '${2}' is out of range [0..6]"
            usage 1
        fi
        shift 2
        ;;
    -u|--url)
        BASE_URL="${2}"
        shift 2
        ;;
    --)
        shift
        break
        ;;
    esac
done

if [ -z "${MENUS_PATH}" ] ; then
    echo "Error: Grub file templates dir path is required: -i <path to grub file template dir>"
    usage 1
elif [ ! -d "${MENUS_PATH}" ] ; then
    echo "Error: Grub file templates dir ${MENUS_PATH} is not present."
    usage 1
elif [ ! -e "${MENUS_PATH}/${BIOS_PXEBOOT_GRUB_FILE}" ] ; then
    echo "Error: BIOS grub template file ${MENUS_PATH}/${BIOS_PXEBOOT_GRUB_FILE} is not present."
    usage 1
elif [ ! -e "${MENUS_PATH}/${UEFI_PXEBOOT_GRUB_FILE}" ] ; then
    echo "Error: UEFI grub template file ${MENUS_PATH}/${UEFI_PXEBOOT_GRUB_FILE} is not present."
    usage 1
elif [ -z "${BASE_URL}" ]; then
    echo "Error: HTTP Base pxeboot server URL is required: -u <pxe server url>"
    usage 1
elif [ -z "${PXE_PATH}" ]; then
    echo "Error: PXE boot path is required: -u <pxeboot path>"
    usage 1
elif [ -z "${FEED_PATH}" ]; then
    echo "Error: Target specific feed path is required: -f <feed path>"
    usage 1
elif [ -z "${GRUB_PATH}" ]; then
    echo "Error: Target specific grub path is required: -o <output path>"
    usage 1
elif [ ! -d "${GRUB_PATH}" ]; then
    echo "Error: Target specific grub path is missing: ${GRUB_PATH}"
    usage 1
fi

cmd_file=""
if [ "${BACKUP}" = true ] ; then
    # Save the command used to create these menus
    # Needs to be done before the escape sequence below.
    if [ -d "${GRUB_PATH}" ] ; then
        cmd_file=${GRUB_PATH}/pxeboot_setup_cmd.sh
        echo "${0} \\" > "${cmd_file}"
        echo "-i ${MENUS_PATH} \\" >> "${cmd_file}"
        echo "-o ${GRUB_PATH} \\" >> "${cmd_file}"
        echo "-p ${PXE_PATH} \\" >> "${cmd_file}"
        echo "-f ${FEED_PATH} \\" >> "${cmd_file}"
        echo "-u ${BASE_URL} \\" >> "${cmd_file}"
        if [ "${SYSTEM}" != "" ] ; then
            echo "-d ${INSTDEV} \\" >> "${cmd_file}"
            echo "-s ${SYSTEM}" >> "${cmd_file}"
        else
            echo "-d ${INSTDEV}" >> "${cmd_file}"
        fi
    fi
    chmod 777 "${cmd_file}"
fi

#Escape paths for sed
PXE_PATH="${PXE_PATH//\//\\/}"
BASE_URL="${BASE_URL//\//\\/}"
FEED_PATH="${FEED_PATH//\//\\/}"

#Variable replacement
cp "${MENUS_PATH}"/"${BIOS_PXEBOOT_GRUB_FILE}" "${GRUB_PATH}"
sed -i "s#xxxBASE_URLxxx#${BASE_URL}#g;
        s#xxxPXEBOOTxxx#${PXE_PATH}#g;
        s#xxxFEEDxxx#${FEED_PATH}#g;
        s#xxxINSTDEVxxx#${INSTDEV}#g;
        s#xxxSYSTEMxxx#${BIOS_SYSTEM}#g;
        s#ttyS0#ttyS${TTY}#g" \
    "${GRUB_PATH}/${BIOS_PXEBOOT_GRUB_FILE}"
cp "${MENUS_PATH}"/"${UEFI_PXEBOOT_GRUB_FILE}" "${GRUB_PATH}"
sed -i "s#xxxBASE_URLxxx#${BASE_URL}#g;
        s#xxxPXEBOOTxxx#${PXE_PATH}#g;
        s#xxxFEEDxxx#${FEED_PATH}#g;
        s#xxxINSTDEVxxx#${INSTDEV}#g;
        s#xxxSYSTEMxxx#${UEFI_SYSTEM}#g;
        s#ttyS0#ttyS${TTY}#g" \
    "${GRUB_PATH}/${UEFI_PXEBOOT_GRUB_FILE}"

# leave these grub files as read/write for owner and group, read only world
chmod 664 "${GRUB_PATH}/${BIOS_PXEBOOT_GRUB_FILE}"
chmod 664 "${GRUB_PATH}/${UEFI_PXEBOOT_GRUB_FILE}"

if [ "${BACKUP}" = true ] ; then
    # Save a backup of these created files
    if [ -e "${GRUB_PATH}"/"${BIOS_PXEBOOT_GRUB_FILE}" ] ; then
        cp -f "${GRUB_PATH}"/"${BIOS_PXEBOOT_GRUB_FILE}" "${GRUB_PATH}/.${BIOS_PXEBOOT_GRUB_FILE}"
        cp -f "${GRUB_PATH}"/"${UEFI_PXEBOOT_GRUB_FILE}" "${GRUB_PATH}/.${UEFI_PXEBOOT_GRUB_FILE}"
    fi
fi

tmp="The setup is complete"
if [ "${cmd_file}" != "" ] ; then
    echo "${tmp} ; cmd saved to ${cmd_file}"
else
    echo "${tmp}"
fi
exit 0
