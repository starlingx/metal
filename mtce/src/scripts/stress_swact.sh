#!/bin/bash
#
# Copyright (c) 2013-2014 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

#
# This test script soaks the mtcAgent and hbsAgent start and stop operations.
# Start Condition: Agents are already running

primary_resource="svr_vip"
proc="Openstack Swact Soak:"
count=0
delay_list="1 2 3 4 5 6 7 8 9 10 11 12 13 14 15"


while true
do

    logger "$proc Swacting to Controller 1 ----------"
    crm resource move $primary_resource controller-1
    for delay in $delay_list
    do
        sleep 10
        echo -n "."
        crm status
    done

    logger "$proc Swacting to Controller 0 ----------"
    crm resource move $primary_resource controller-0
    for delay in $delay_list
    do
        sleep 10
        echo -n "."
        crm status
    done

done
