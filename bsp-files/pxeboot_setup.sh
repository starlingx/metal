#!/bin/bash

OPTIND=1

BASE_URL=""
TFTP_DIR=""
WORKING_DIR=""
COPY_DIR=""
ISODIR=$(dirname `readlink -f $0`)

usage() {
    echo "Usage: $0 -u <http base URL> [-t <tftp pxeboot directory>] or [-w <working directory>]" 1>&2;
    exit 0;
}

while getopts ":u:t:w:" opt; do
    case "$opt" in
    u)
        BASE_URL=${OPTARG}
        ;;
    t)
        TFTP_DIR=${OPTARG}
        ;;
    w)
        WORKING_DIR=${OPTARG}
        ;;
    *)
        usage
        ;;
    esac
done

shift $((OPTIND-1))

if [ -z "$BASE_URL" ]; then
    echo "HTTP base URL is required: -u <http base URL>"
    exit 0
fi

if [ -z "$TFTP_DIR" ] && [ -z "$WORKING_DIR" ]; then
    echo "Either tftp pxeboot directory or working directory has to be specified:"
    echo "-t <tftp pxeboot directory> or -w <working directory>"
    exit 0
elif [ -n "$TFTP_DIR" ]; then
    if [ -n "$WORKING_DIR" ]; then
        echo "tftp pxeboot directory is supplied, working directory will be ignored."
    fi
    COPY_DIR=$TFTP_DIR
elif [ -n "$WORKING_DIR" ]; then
    COPY_DIR=$WORKING_DIR
fi

if [ ! -d ${COPY_DIR} ] ; then
    if [ -w "$(dirname $COPY_DIR)" ]; then
        echo "Create ${COPY_DIR}"
        mkdir ${COPY_DIR}
        chmod +w ${COPY_DIR}
        if [ $? -ne 0 ]; then
            echo "Can't create ${COPY_DIR}"
            exit 1
        fi
    else
        echo "$COPY_DIR parent directory is not writeable."
        exit 0
    fi
else
    echo "$COPY_DIR already exists"
    read -p "WARNING: Files in this folder will get overwritten, continue? [y/N] " confirm
    if [[ "${confirm}" != "y" ]]; then
        exit 1
    fi
fi

#Copy the vmlinuz and initrd files to the destination directory
cp ${ISODIR}/vmlinuz ${COPY_DIR}/
cp ${ISODIR}/initrd.img ${COPY_DIR}/

#Copy the contents of distribution to the destination directory
cp -r ${ISODIR}/* ${COPY_DIR}/

#Find the number of directories in the URL
dirpath=$(echo ${BASE_URL#"http://"})
DIRS=$(grep -o "/" <<< "$dirpath" | wc -l)

#Escape path for sed
BASE_URL="${BASE_URL//\//\\/}"

#Copy pxeboot files
mkdir -p ${COPY_DIR}/EFI/centos/x86_64-efi/
cp -Rf ${COPY_DIR}/pxeboot/* ${COPY_DIR}/

#Rename the UEFI grub config
mv ${COPY_DIR}/pxeboot_grub.cfg ${COPY_DIR}/grub.cfg

#Create a symlink of the UEFI grub config, the bootloader could be also looking
#for it under the EFI/ folder depending on if the PXE Server is configured with a
#TFTP Server or dnsmasq
ln -sf ../grub.cfg ${COPY_DIR}/EFI/grub.cfg

# Copy grubx64.efi from the EFI/BOOT dir to the EFI dir
cp -f ${ISODIR}/EFI/BOOT/grubx64.efi ${COPY_DIR}/EFI/

#Variable replacement
sed -i "s#xxxHTTP_URLxxx#${BASE_URL}#g;
        s#xxxHTTP_URL_PATCHESxxx#${BASE_URL}/patches#g;
        s#NUM_DIRS#${DIRS}#g" \
    ${COPY_DIR}/pxeboot.cfg \
    ${COPY_DIR}/grub.cfg \
    ${COPY_DIR}/pxeboot_controller.cfg \
    ${COPY_DIR}/pxeboot_smallsystem.cfg \
    ${COPY_DIR}/pxeboot_smallsystem_lowlatency.cfg

# Delete unnecessary files
rm -Rf ${COPY_DIR}/EFI/BOOT
rm -Rf ${COPY_DIR}/pxeboot

if [ -n "$TFTP_DIR" ]; then
    #Create pxelinux.cfg directory and default link
    if [ ! -d ${TFTP_DIR}/pxelinux.cfg ] ; then
        mkdir ${TFTP_DIR}/pxelinux.cfg
    fi
    chmod 755 ${TFTP_DIR}/pxelinux.cfg
    ln -sf ../pxeboot.cfg ${TFTP_DIR}/pxelinux.cfg/default
fi

echo "The setup is complete"
