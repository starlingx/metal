#! /bin/bash

#
# Copyright (c) 2015 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

echo "Running guestAgent guest services command testhead"

if [ -z $1 ] ; then
    echo "Error: must supply a host name as first arguement"
    echo "Syntax: $0 compute-1"
    exit 1
fi

echo "Args: $1 $2 $3"

banner="-----------------------------------------------------------"

hostname=$1
hostuuid=`system host-show $hostname | grep uuid | cut -f 15 -d ' '`
#hostuuid=`system host-show $hostname | grep uuid`

echo "hostname: $hostname"
echo "hostuuid: $hostuuid"
echo "Emulating VIM guest services commands against $hostname"

count=1

echo $banner
echo "$count Create Host Services"
echo $banner
curl -i -X POST -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/$hostuuid

count=$((count + 1))

echo $banner
echo "$count Query Host Services"
echo $banner
curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/$hostuuid

count=$((count + 1))

echo $banner
echo "$count Enable Host Services"
echo $banner
curl -i -X PUT -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/$hostuuid/enable -d '{"hostname": "compute-1", "uuid" : "010e7741-1173-4a3b-88fa-c4e5905500ca"}'

count=$((count + 1))

echo $banner
echo "$count Create Guest Service: Instance 1"
echo $banner
curl -i -X POST -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104400 -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df104400", "channel" : "cgts-instance000001", "services" : ["heartbeat"]}'

count=$((count + 1))

echo $banner
echo "$count Create Guest Service: Instance 2"
echo $banner
curl -i -X POST -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104401 -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df104401", "channel" : "cgts-instance000002", "services" : ["heartbeat"]}'

count=$((count + 1))

echo $banner
echo "$count Query Guest Services: Instance 2:"
echo $banner
curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104401

count=$((count + 1))

echo $banner
echo "$count Query Guest Services: Instance 1:"
echo $banner
curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104400

count=$((count + 1))

echo $banner
echo "$count Enable Guest Service: Instance 2"
echo $banner
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104401 -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df104401", "channel" : "cgts-instance000002", "services" : [{"service":"heartbeat" , "state":"enabled"}]}'

count=$((count + 1))

echo $banner
echo "$count Query Guest Services: Instance 2:"
echo $banner
curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104401

count=$((count + 1))

echo $banner
echo "$count Disable Guest Service: Instance 2"
echo $banner
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104401 -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df104401", "channel" : "cgts-instance000002", "services" : [{"service":"heartbeat" , "state":"disabled"}]}'

count=$((count + 1))

echo $banner
echo "$count Query Guest Services: Instance 1:"
echo $banner
curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104401

count=$((count + 1))

exit 0

echo $banner
echo "$count Delete Guest Service: Instance 2"
echo $banner
curl -i -X DELETE -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104401

count=$((count + 1))

echo $banner
echo "$count Query Host Services"
echo $banner
curl -i -X GET -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/$hostuuid

count=$((count + 1))

echo $banner
echo "$count Disable Host Services"
echo $banner
curl -i -X PUT -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/$hostuuid/disable -d '{"hostname": "compute-1", "uuid" : "010e7741-1173-4a3b-88fa-c4e5905500ca"}'

count=$((count + 1))

echo $banner
echo "$count Delete Host Services"
echo $banner
curl -i -X DELETE -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/hosts/$hostuuid  

count=$((count + 1))

echo $banner
echo "$count Enable Guest Service: Instance 1"
echo $banner
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104400 -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df104400", "channel" : "cgts-instance000001", "services" : [{"service":"heartbeat" , "state":"enabled"}]}'

count=$((count + 1))

echo $banner
echo "$count Disable Guest Service: Instance 1"
echo $banner
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104400 -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df104400", "channel" : "cgts-instance000001", "services" : [{"service":"heartbeat" , "state":"disabled"}]}'

count=$((count + 1))

echo $banner
echo "$count Enable Guest Service: Instance 1 - Change Channel"
echo $banner
curl -i -X PATCH -H 'Content-Type: application/json' -H 'Accept: application/json' -H 'User-Agent: vim/1.0' http://localhost:2410/v1/instances/8d80875b-fa73-4ccb-bce3-1cd4df104400 -d '{"hostname": "compute-1", "uuid" : "8d80875b-fa73-4ccb-bce3-1cd4df104400", "channel" : "cgts-instance000003", "services" : [{"service":"heartbeat" , "state":"enabled"}]}'

echo $banner
echo $banner

exit 0
