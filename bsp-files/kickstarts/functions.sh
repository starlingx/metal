# This file defines functions that can be used in %pre and %post kickstart sections, by including:
# . /tmp/ks-functions.sh
#

cat <<END_FUNCTIONS >/tmp/ks-functions.sh
#
# Copyright (c) xxxYEARxxx Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# Get the FD used by subshells to log output
if [ -z "\$stdout" ]; then
    exec {stdout}>&1
fi

function wlog()
{
    local dt="\$(date "+%Y-%m-%d %H:%M:%S.%3N")"
    echo "\$dt - \$1" >&\${stdout}
}

function get_by_path()
{
    local disk=\$(cd /dev ; readlink -f \$1)
    for p in /dev/disk/by-path/*; do
        if [ "\$disk" = "\$(readlink -f \$p)" ]; then
            echo \$p
            return
        fi
    done
}

function get_disk()
{
    echo \$(cd /dev ; readlink -f \$1)
}

function report_pre_failure_with_msg()
{
    local msg=\$1
    echo -e '\n\nInstallation failed.\n'
    echo "\$msg"

    exit 1
}

function report_post_failure_with_msg()
{
    local msg=\$1
    cat <<EOF >> /etc/motd

Installation failed.
\$msg

EOF
    echo "\$msg" >/etc/platform/installation_failed

    echo -e '\n\nInstallation failed.\n'
    echo "\$msg"

    exit 1
}

function report_post_failure_with_logfile()
{
    local logfile=\$1
    cat <<EOF >> /etc/motd

Installation failed.
Please see \$logfile for details of failure

EOF
    echo \$logfile >/etc/platform/installation_failed

    echo -e '\n\nInstallation failed.\n'
    cat \$logfile

    exit 1
}

function get_http_port()
{
    echo \$(cat /proc/cmdline |xargs -n1 echo |grep '^inst.repo=' | sed -r 's#^[^/]*://[^/]*:([0-9]*)/.*#\1#')
}

function get_disk_dev()
{
    local disk
    # Detect HDD
    for blk_dev in vda vdb sda sdb dda ddb hda hdb; do
        if [ -d /sys/block/\$blk_dev ]; then
            disk=\$(ls -l /sys/block/\$blk_dev | grep -v usb | head -n1 | sed 's/^.*\([vsdh]d[a-z]\+\).*$/\1/');  
            if [ -n \$disk ]; then
                echo \$disk
                return
            fi
        fi
    done
    for blk_dev in nvme0n1 nvme1n1; do
        if [ -d /sys/block/\$blk_dev ]; then
            disk=\$(ls -l /sys/block/\$blk_dev | grep -v usb | head -n1 | sed 's/^.*\(nvme[01]n1\).*$/\1/');
            if [ -n \$disk ]; then
                echo \$disk
                return
            fi
        fi
    done
}

function exec_no_fds()
{
    # Close open FDs when executing commands that complain about leaked FDs.
    local fds=\$1
    local cmd=\$2
    local retries=\$3
    local interval=\$4
    local ret_code=0
    local ret_stdout=""
    for fd in \$fds
    do
        local cmd="\$cmd \$fd>&-"
    done
    if [ -z "\$retries" ]; then
        #wlog "Running command: '\$cmd'."
        eval "\$cmd"
    else
        ret_stdout=\$(exec_retry "\$retries" "\$interval" "\$cmd")
        ret_code=\$?
        echo "\${ret_stdout}"
        return \${ret_code}
    fi
}

function exec_retry()
{
    local retries=\$1
    local interval=\$2
    local cmd=\$3
    let -i retry_count=1
    local ret_code=0
    local ret_stdout=""
    cmd="\$cmd" # 2>&\$stdout"
    while [ \$retry_count -le \$retries ]; do
        #wlog "Running command: '\$cmd'."
        ret_stdout=\$(eval \$cmd)
        ret_code=\$?
        [ \$ret_code -eq 0 ] && break
        wlog "Error running command '\${cmd}'. Try \${retry_count} of \${retries} at \${interval}s."
        wlog "ret_code: \${ret_code}, stdout: '\${ret_stdout}'."
        sleep \$interval
        let retry_count++
    done
    echo "\${ret_stdout}"
    return \${ret_code}
}

END_FUNCTIONS

