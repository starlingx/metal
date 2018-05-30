#!/bin/bash
#
# Copyright (c) 2017 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# virtualization support "goenabled" check.
PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/local/bin

NAME=$(basename $0)

function LOG()
{
    logger "$NAME: $*"
}

if [ ${UID} -ne 0 ]; then
    LOG "Error: Need sudo/root permission."
    exit 1
fi

# Evaluate various virtualization related facts.
product_name=$(dmidecode -s system-product-name 2>/dev/null)
OPTS="product_name:${product_name}, "
is_virtual=$(/usr/bin/facter is_virtual 2>/dev/null)
OPTS+="is_virtual:${is_virtual}, "
host_type=$(/usr/bin/facter virtual 2>/dev/null)
OPTS+="host_type:${host_type}, "
cpu_has_vmx=$(grep -w -q vmx /proc/cpuinfo && echo "true" || echo "false")
OPTS+="cpu_has_vmx:${cpu_has_vmx}, "
dev_kvm_exists=$([[ -e /dev/kvm ]] && echo "true" || echo "false")
OPTS+="dev_kvm_exists:${dev_kvm_exists}, "
nested_virt=$(cat /sys/module/kvm_intel/parameters/nested 2>/dev/null || echo "false")
OPTS+="nested_virt:${nested_virt}, "
hardware_virt_supported=$(virt-host-validate qemu 2>/dev/null | grep -q -w -e FAIL && echo "false" || echo "true")
OPTS+="hardware_virt_supported:${hardware_virt_supported}"
REASONS=$(virt-host-validate qemu 2>/dev/null | grep -w -e FAIL)

# Check that virtualization is supported on hardware. It is sufficient just to
# check the output of virt-host-validate.  Additional facts are gathered for
# information.
# Notes:
# - virt-host-validate checks that /dev/kvm exists, and 'vmx' CPU flag present
# - it is also possible to check whether VT-x is enabled in BIOS by reading
#   Intel MSR register, but checking /dev/kvm is sufficient
# - 'vmx' cpu flag indicates whether virtualization can be supported
# - on emulated systems such as VirtualBox or QEMU, vmx is not required
# - if vmx is enabled on QEMU, it can also support nested virtualization

if [ "${host_type}" = "physical" ] && [ "${hardware_virt_supported}" == "false" ]
then
    LOG "Virtualization is not supported: ${OPTS}.  Failing goenabled check."
    LOG "Failure reasons:"$'\n'"${REASONS}"
    exit 1
fi

LOG "Virtualization is supported: ${OPTS}."
exit 0
