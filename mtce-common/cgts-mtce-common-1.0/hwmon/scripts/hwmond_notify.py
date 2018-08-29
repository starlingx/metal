#
# Copyright (c) 2015 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
from __future__ import print_function
import socket
import os

UDP_IP = socket.gethostbyname('controller')
UDP_PORT = 2188
ENV_MESSAGE = os.environ["MESSAGE"]

print ("UDP target IP:", UDP_IP)
print ("UDP target port:", UDP_PORT)
print ("message:", ENV_MESSAGE)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(ENV_MESSAGE, (UDP_IP, UDP_PORT))
