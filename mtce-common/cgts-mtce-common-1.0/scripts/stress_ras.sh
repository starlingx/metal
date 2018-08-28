#!/bin/bash
#
# Copyright (c) 2013-2014, 2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

#
# This test script soaks the mtcAgent and hbsAgent start and stop operations.
# Start Condition: Agents are already running

primary_resource="sysinv-api"
proc="Platform RA Soak:"
count=0
delay_list="1 2 3 4 5 6 7 8 9 10 11 12 13 14 15"


while true
do

    logger "$proc Stopping Platform Resource Agents ----------"
    crm resource stop $primary_resource
    for delay in $delay_list
    do
        sleep 1
        echo -n "."
    done
    echo ""

    status=`crm resource status`
    for service in "sysinv-api" "sysinv-conductor" "sysinv-agent" "mtcAgent" "hbsAgent"
    do
        status_tmp=`echo "$status" | grep $service | cut -f2 -d')'`
        if [ "$status_tmp" != " Stopped " ] ; then
                echo "$proc ($count) Stop  $service Failed <$status_tmp>"
                sleep 5
                crm resource status
                exit 0
        else
                echo "$proc ($count) Stop  O.K. for $service"
        fi
    done
    logger "$proc Stop O.K. -------------------------------"

    logger "$proc Starting Platform Resource Agents ----------"
    crm resource start $primary_resource
    for delay in $delay_list
    do
        sleep 1
        echo -n "."
    done
    echo ""

    status=`crm resource status`
    for service in "sysinv-api" "sysinv-conductor" "sysinv-agent" "mtcAgent" "hbsAgent"
    do
        status_tmp=`echo "$status" | grep $service | cut -f2 -d')'`
        if [ "$status_tmp" != " Started " ] ; then
                echo "$proc ($count) Start $service Failed <$status_tmp>"
                sleep 5
                crm resource status
                exit 0
        else
                echo "$proc ($count) Start O.K. for $service"
        fi
    done
    logger "$proc Start O.K. ------------------------------"

    count=`expr $count + 1`
    mtc=`cat /var/run/mtcAgent.pid`
    hbs=`cat /var/run/hbsAgent.pid`
    echo "$mtc:`pidof mtcAgent`  <:> $hbs:`pidof hbsAgent`"
done
