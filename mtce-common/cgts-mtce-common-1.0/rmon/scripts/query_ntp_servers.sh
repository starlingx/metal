#!/bin/bash
#
# Copyright (c) 2015-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# Return values;
# 0 - All NTP servers are reachable and one is selected
# 1 - No NTP servers are provisioned
# 2 - None of the NTP servers are reachable
# 3 - Some NTP servers reachable and one is selected
# 4 - Some NTP servers reachable but none is selected
#
# This script execute ntpq -np to determined which provisioned servers
# reachable. A server will be considered reachable only when the Tally Code
# is a * or a +. Also the controller node will not be considered a reachable
# server
#
# Here is an example of the ntpq command
#         remote           refid      st t when poll reach   delay   offset  jitter
#    ==============================================================================
#    +192.168.204.104 206.108.0.133    2 u  203 1024  377    0.226   -3.443   1.137
#    +97.107.129.217  200.98.196.212   2 u  904 1024  377   21.677    5.577   0.624
#     192.95.27.155   24.150.203.150   2 u  226 1024  377   15.867    0.381   1.124
#    -97.107.129.217  200.98.196.212   2 u  904 1024  377   21.677    5.577   0.624
#    *182.95.27.155   24.150.203.150   2 u  226 1024  377   15.867    0.381   1.124
#
#  The temporary file /tmp/ntpq_server_info" will look something like this.
#  - The first line contains all the external NTP servers configured (it excludes the
#  peer controller).
#  - The second line lists all the unreachable external NTP servers. It also excludes
#  the peer controller.
#
#       more /tmp/ntpq_server_info
#       10.10.10.42;10.10.10.43;10.10.10.44;
#       10.10.10.43;
#
#  This temporary file is re-created everytime the this script is run. It is used by 
#  caller of the script to get more detail regarding the NTP servers status.
#
#  This script will only be run on the controller nodes.
#
#  This script logs to user.log

NTP_OK=0
NTP_NOT_PROVISIONED=1
NTP_NONE_REACHABLE=2
NTP_SOME_REACHABLE=3
NTP_SOME_REACHABLE_NONE_SELECTED=4

# is it the ip address of a controller node
isController ()
{
   host=$1
   res=$(echo $(grep $host /etc/hosts) | grep controller)

   if [[ "$res" != "" ]] ; then
      logger -p info -t $0 "$host is a controller"
      return 1
   else
      return 0
   fi
}

# loop through all the ntpq servers listed as IPs and get the controller's
getControllerIP ()
{
    servers=$1
    # loop through all the ntpq servers
    while read line
    do
        server=$(echo $line | awk '{print $1;}')
        if [[ "$line" != " "* ]] ; then
           # if the first char is not a space then remove it e.g +159.203.31.244
           server=$(echo $server| cut -c 2-)
        fi

        res=$(echo $(grep $server /etc/hosts) | grep controller)
        if [[ "$res" != "" ]] ; then
           echo $server
           return
        fi
    done < <(echo "$servers")

    # should of found the controller exit script
    logger -p err -t $0 "Could not find the Controller's IP address"
    exit -1
}


# set up ouput file
ntpq_server_info="/tmp/ntpq_server_info"
rm -f $ntpq_server_info

# find out if there is any servers provisioned
server_count=$(cat /etc/ntp.conf | awk '{print $1;}' | grep -c  '^server')
bad_server_count=0

# exit if there is no servers provisioned
if [ $server_count -eq 0 ]; then
   logger -p info -t $0 "No NTP servers are provisioned (1)"
   exit $NTP_NOT_PROVISIONED
fi

# query the ntp servers with ntpq
ntpres="$(ntpq -pn)"

echo -e "\n Results from 'ntpq -pn' command \n $ntpres \n" | logger -p info -t $0

# keep for debugging
# the first argument is a filename and used instead of using ntpq
#if [ "$1" ]; then
#   ntpres=$(cat $1)
#fi

# keep for debugging hard code the filname istead of passing it as an argument
#ntpres=$(cat "/home/wrsroot/test")
#server_count=3
#

# remove the header lines
server_list=$( echo "$ntpres" | tail -n +3 )

# loop through and find non reachable servers
# a server is reachable with its prepended with a "*" or a "+"

SAVEIFS=$IFS
IFS=''

# list all provisioned servers and save the external ones in temp file
while read line
do
    server=$(echo $line | awk '{print $1;}')
    if [[ "$line" != " "* ]] ; then
       # if the first char is not a space then remove it e.g +159.203.31.244
       server=$(echo $server| cut -c 2-)
    fi

    # add provisioned ntp server to temp file if not the controller
    isController $server
    if [[ "$?" == 0 ]]; then
       echo -n $server";" >> $ntpq_server_info
    fi
done < <(echo "$server_list")

echo >> $ntpq_server_info

# remove the peer server (peer controller) from the server list
controller_host_ip=$(getControllerIP $server_list)
server_list=$(echo $server_list | grep -v $controller_host_ip)

# list all non reachable ntp servers and save in temp file
while read line
do
  if [[ "$line" != "*"* ]] && [[ "$line" != "+"* ]] ;then

    server=$(echo $line | awk '{print $1;}')
    if [[ "$line" != " "* ]] ; then
       # if the first char is not a space then remove it e.g +159.203.31.244
       server=$(echo $server| cut -c 2-)
    fi

    # add the non reachable external ntp servers to temp file
    ((bad_server_count++))
    echo -n $server";" >> $ntpq_server_info
  fi
done < <(echo "$server_list")
IFS=$SAVEIFS

#
logger -p info -t $0 Total number of provisioned servers $server_count
logger -p info -t $0 Total number of unreachable servers $bad_server_count
#

# check if there is a "*" which represent a selected server
# there should be only one but handling multiple
selected=$(echo "$server_list" | grep -c  '^*')

if [ "$bad_server_count" -eq 0 ];then
  if [ $selected -eq 0 ]; then
    logger -p info -t $0 "All external NTP servers are reachable but none is selected (4)"
    exit $NTP_SOME_REACHABLE_NONE_SELECTED
  else
    logger -p info -t $0 "All external NTP servers are reachable and one is selected (0)"
    exit $NTP_OK
  fi
fi

# it does not matter if the peer controller is the server selected, if all the
# external NTP servers are not reachable then we return NTP_NONE_REACHABLE
if [ "$bad_server_count" -eq "$server_count" ];then
  logger -p info -t $0 "None of the external NTP servers are reachable (2)"
  exit $NTP_NONE_REACHABLE
fi

if [ "$bad_server_count" -lt "$server_count" ];then
  if [ $selected -eq 0 ]; then
    # this will happen if the peer controller is the selected server
    logger -p info -t $0 "Some external NTP servers are reachable but none is selected (4)"
    exit $NTP_SOME_REACHABLE_NONE_SELECTED
  else
    logger -p info -t $0 "Some external NTP servers are not reachable and one selected (3)"
    exit $NTP_SOME_REACHABLE
  fi
fi

logger -p err -t $0 "Should not exit here"
exit -1