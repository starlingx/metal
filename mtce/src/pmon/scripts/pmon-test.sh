#!/bin/bash

#
# Copyright (c) 2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
#
###########################################################################
#
# This is a pmon test script that is not packaged into the load.
# It is used for feature regression testing.
#
# Test options are
#  - restart
#  - future ....
#
#
# Restart Test Example:
#
# process                       operation status [before pid]:[after pid]
#
# controller-0:~#  /home/wrsroot/pmon-test.sh restart
#
# acpid                           restart PASSED [651]:[19095]
# fsmond                          restart PASSED [8719]:[26343]
# guestServer                     restart PASSED [8710]:[29108]
# hbsClient                       restart PASSED [8729]:[31248]
# host_agent                      restart PASSED [13840]:[630]
# io-monitor-manager              restart PASSED [11398]:[3713]
# libvirtd                        restart PASSED [3841]:[7323]
# logmgmt                         restart PASSED [2443]:[10701]
# mtcClient                       restart PASSED [8735]:[13749]
# mtclogd                         restart PASSED [8681]:[15771]
# neutron-dhcp-agent              restart PASSED [10911]:[23967]
# neutron-metadata-agent          restart PASSED [11051]:[27210]
# neutron-sriov-nic-agent         restart PASSED [11092]:[30551]
# nova-compute                    restart PASSED [14776]:[1109]
# ntpd                            does not support restart
# ptp4l                           does not support restart
# phc2sys                         does not support restart
# sm-api                          restart PASSED [8896]:[8460]
# skipping 'sm' process
# sm-eru                          restart PASSED [8904]:[10993]
# sm-watchdog                     restart PASSED [8695]:[14621]
# skipping 'sshd' process
# sw-patch-agent                  restart PASSED [2740]:[17461]
# sw-patch-controller-daemon      restart PASSED [2558]:[21336]
# sysinv-agent                    restart PASSED [2757]:[25128]
# syslog-ng                       restart PASSED [684]:[28125]
# vswitch                        does not support restart

############################################################################
. /etc/nova/openrc

# Linux Standard Base (LSB) Error Codes
RETVAL=0
GENERIC_ERROR=1
INVALID_ARGS=2
UNSUPPORTED_FEATURE=3
NOT_INSTALLED=5
NOT_RUNNING=7

trap ctrl_c INT

function ctrl_c {
    echo "Exiting ..."
    exit 0
}

DEBUG=false

function dlog {
    if [ ${DEBUG} == true ] ; then
        echo "Debug: $1"
    fi
}

# defaults
restarts=1
debounce=10
startuptime=40
factor=10

printf "\n"

# Loop over all the files in pmon.d dir and include pmond itself
FILES=/etc/pmon.d/*
#for file in "pmond" ${FILES}
for file in ${FILES}
do
    if [ "${file}" == "pmond" ] ; then
        process=${file}
        pidfile="/var/run/pmond.pid"
    else
        restarts=`cat ${file} | grep ^restarts | cut -f2 -d'=' | cut -f2 -d' '`
        process=`cat ${file} | grep ^process | cut -f2 -d'=' | cut -f2 -d' '`
    fi


    printf "%-30s %s - " "${process}" "${1}"

    # Avoid testing certain processes
    # ceph - pmond does not support
    if [ "$process" == "ceph" -o "$process" == "vswitch" ] ; then
        echo "${process} ${1} is not supported ... skipping"
        continue
    fi

    if [ "$process" == "sshd" -a "${TERM}" == "xterm" ] ; then
        echo "${process} ${1} not supported in xterm mode ; need to run on console"
        continue
    fi


    debounce=`cat ${file} | grep ^debounce | cut -f2 -d'=' | cut -f2 -d' '`
    if [ -z "${debounce}" ] ; then
        debounce=10
    fi

    startuptime=`cat ${file} | grep ^startuptime | cut -f2 -d'=' | cut -f2 -d' '`
    if [ -z "${startuptime}" ] ; then
        startuptime=10
    fi

    pidfile=`cat ${file} | grep ^pidfile | cut -f2 -d'=' | cut -f2 -d' '`

    dlog "supports $restarts restarts debounce:$debounce startuptime:$startuptime pidfile:$pidfile"

    if [ -z "$restarts" -o -z "$process" -o -z "$pidfile" -o -z "$debounce" -o -z "$startuptime" ] ; then
        printf "FAILED to parse ${file} - $restarts:restarts debounce:$debounce startuptime:$startuptime pidfile:$pidfile"
        continue
    fi

    if [ "$1" == "restart" ] ; then
        if [ ! -f ${pidfile} ] ; then
            printf "${1} FAILED ... pifdile missing (${pidfile})\n"
            if [ ${process} == "pmond" ] ; then
                exit ${GENERIC_ERROR}
            else
                continue
            fi
        fi
        pid1=`head -1 ${pidfile}`
        kill -0 ${pid1}
        if [ $? -eq 0 ] ; then
            pmon-restart ${process}
        else
            echo "FAILED - process not Running"
        fi
    elif [ "$1" == "stop" ] ; then
        debounce=1
        startuptime=1
        factor=1
         # Not all processes can be stopped
        if [ "$process" == "pmond" -o "$process" == "sm" ] ; then
            echo "${process} stop not supported ... skipping"
            continue
        elif [ "$process" == "hbsClient" -a ! -e "/var/run/.node_locked" ] ; then
            echo "${process} refusing to stop of heartbeat client on inservice host"
            continue
        else
            pmon-stop ${process}
        fi
    elif [ "${1}" == "start" ] ; then
        if [ "$process" == "pmond" -o "$process" == "sm" ] ; then
            echo "${process} stop not supported ... skipping"
            continue
        else
            factor=1
            pmon-start ${process}
            sleep 1
            s=`tail -2 /var/log/pmond.log | grep "$process process is not in the stopped state"`
            if [ ! -z "${s}" ] ; then
                echo "FAILED not in stopped state"
                continue
            fi
        fi
    elif [ "${1}" == "kill" ] ; then
        kill -9 ${pid1}
        sleep 2
    elif [ "${1}" == "alarm" ] ; then
        printf "action not yet supported"
        continue
    elif [ "${1}" == "critical" ] ; then
        printf "action not yet supported"
        continue
    else
        printf "\n\nError:\nInvalid operation '${1}' specified\n"
        printf "... must be restart, kill, alarm, or critical\n"
        printf "\n"
        exit ${UNSUPPORTED_FEATURE}
    fi

    sleeptime=$((debounce + startuptime + factor))
    sleep $sleeptime
    if [ "$1" == "stop" ] ; then
        if [ -e "${pidfile}" ] ; then
            # some processes are auto restarted by systemd
            if [ "${process}" != "mtcClient" -a "${process}" != "syslog-ng" ] ; then
                echo "FAILED - pidfile still present"
            fi
        else
            kill -0 ${pid1} 2> /dev/null
            if [ $? -eq 0 ] ; then
                echo "FAILED - process is still running ($pid1)"
            else
                pid2=`/usr/sbin/pidof ${process}`
                if [ -z ${pid2} ] ; then
                    echo "PASSED - process is stopped"
                else
                    echo "FAILED - process is running again ($pid2)"
                fi
            fi
        fi
    else
        pid2=`head -1 ${pidfile}`
        if [ "$pid1" != "$pid2" ] ; then
            kill -0 ${pid2}
            if [ $? -eq 0 ] ; then
                sleep 10
                pid3=`head -1 ${pidfile}`
                if [ "$pid2" != "$pid3" ] ; then
                    echo "FAILED - pid changed"
                else
                    printf "PASSED [%5d]:[%5d]\n" "${pid1}" "${pid2}"
                fi
            else
                echo "FAILED - no process"
            fi
        else
            echo "FAILED - process not stopped"
        fi
    fi
done
exit ${RETVAL}
