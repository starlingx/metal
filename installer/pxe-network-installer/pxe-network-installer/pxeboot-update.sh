#!/bin/bash
#
# Copyright (c) 2016-2017 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

#
# Using a specified template, generate a node-specific pxeboot.cfg file
# for BIOS and UEFI mode. This script logs to user.log
#
# Command example;
# /usr/sbin/pxeboot-update-18.03.sh  -i /var/pxeboot/pxelinux.cfg.files/pxe-controller-install-18.03
#    -o /var/pxeboot/pxelinux.cfg/01-08-00-27-3e-f8-05 -b sda -r sda -t -c ttyS0,115200
#

function usage {
    cat >&2 <<EOF
$0: This utility is used to generate a node-specific pxeboot.cfg file

Arguments:
    -i <input template> : Specify template to use
    -o <output file>    : Specify output filename
    -t                  : Use text install (optional)
    -g                  : Use graphical install (optional)
    -c <console>        : Specify serial console (optional)
    -b <boot device>    : Specify boot device
    -r <rootfs device>  : Specify rootfs device
    -u <tisnotify url>  : Base url for TIS install progress notification
    -s <mode>           : Specify Security Profile mode (optional)
    -T <tboot value>    : Specify whether or not to use tboot (optional)
    -k <kernel args>    : Specify any extra kernel boot arguments (optional)
    -l <base url>       : Specify installer base URL

EOF
}

declare text_install="inst.text"
declare base_url="http://pxecontroller:8080"

function generate_config {
    input=$1
    output=$2

    if [ ! -f "$input" ]; then
        logger --stderr -t $0 "Error: Input file $input does not exist"
        exit 1
    fi

    if [ ! -w $(dirname $output) ]; then
        logger --stderr -t $0 "Error: Destination directory $(dirname $output) not writeable"
        exit 1
    fi

    if [ -e $output -a ! -w $output ]; then
        logger --stderr -t $0 "Error: Destination file $output_file_efi exists and is not writeable"
        exit 1
    fi

    sed -e "s#xxxAPPEND_OPTIONSxxx#$APPEND_OPTIONS#;s#xxxBASE_URLxxx#$BASE_URL#g" $input > $output

    if [ $? -ne 0 -o ! -f $output ]; then
        logger --stderr -t $0 "Error: Failed to generate pxeboot file $output"
        exit 1
    fi
}

parms=$@
logger -t $0 " $parms"

while getopts "i:o:tgc:b:r:u:s:T:k:l:h" opt
do
    case $opt in
        i)
            input_file=$OPTARG
            input_file_efi=$(dirname $input_file)/efi-$(basename $input_file)
            ;;
        o)
            output_file=$OPTARG
            output_file_efi=$(dirname $output_file)/efi-$(basename $output_file)
            ;;
        t)
            text_install="inst.text"
            ;;
        g)
            # We currently do not support Graphics install with Centos. Enforce
            # the text install.
            # text_install="inst.graphical"
            text_install="inst.text"
            ;;
        c)
            console=$OPTARG
            ;;
        b)
            boot_device=$OPTARG
            ;;
        r)
            rootfs_device=$OPTARG
            ;;
        u)
            tisnotify=$OPTARG
            ;;
        s)
            security_profile=$OPTARG
            ;;
        T)
            tboot=$OPTARG
            ;;
        k)
            kernal_extra_args=$OPTARG
            ;;
        l)
            base_url=$OPTARG
            ;;
        h)
            usage
            exit 1
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

# Validate parameters
if [ -z "$input_file" \
        -o -z "$input_file_efi" \
        -o -z "$output_file" \
        -o -z "$output_file_efi" \
        -o -z "$boot_device" \
        -o -z "$rootfs_device" ]; then
    logger --stderr -t $0 "Error: One or more mandatory options not specified: $@"
    usage
    exit 1
fi

APPEND_OPTIONS="boot_device=$boot_device rootfs_device=$rootfs_device"

if [ -n "$text_install" ]; then
    APPEND_OPTIONS="$APPEND_OPTIONS $text_install"
fi

if [ -n "$console" ]; then
    APPEND_OPTIONS="$APPEND_OPTIONS console=$console"
fi

if [ -n "$tisnotify" ]; then
    APPEND_OPTIONS="$APPEND_OPTIONS tisnotify=$tisnotify"
fi

# We now require GPT partitions for all disks regardless of size
APPEND_OPTIONS="$APPEND_OPTIONS inst.gpt"

# Add k8s support for namespaces
APPEND_OPTIONS="$APPEND_OPTIONS user_namespace.enable=1"

if [ -n "$security_profile" ]; then
    APPEND_OPTIONS="$APPEND_OPTIONS security_profile=$security_profile"
fi

if [ -n "$kernal_extra_args" ]; then
    APPEND_OPTIONS="$APPEND_OPTIONS $kernal_extra_args"
fi

BASE_URL=$base_url

generate_config $input_file $output_file

# for extended security profile UEFI boot only,
# a tboot option will be passed to target boot option menu
if [ "$security_profile" == "extended" -a -n "$tboot" ]; then
    APPEND_OPTIONS="$APPEND_OPTIONS tboot=$tboot"
fi

generate_config $input_file_efi $output_file_efi

exit 0
