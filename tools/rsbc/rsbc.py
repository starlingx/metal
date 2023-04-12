#!/usr/bin/python3
###############################################################################
#
# Copyright (c) 2023 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

"""Redfish Info Query"""

import argparse
import datetime
import json
import os
import requests
import socket
import ssl
import sys
import time
import yaml

# Import Redfish Python Library
# Module: https://pypi.org/project/redfish/
import redfish

FEATURE_NAME = 'Redfish Secure Boot Controller'
VERSION_MAJOR = 1
VERSION_MINOR = 0

POWER_ON = 'On'
POWER_OFF = "Off"
POWER_RESET = "Reset"

# Parse command line arguments
# ----------------------------
parser = argparse.ArgumentParser(description=FEATURE_NAME, add_help=False)

parser.add_argument("--help", action='store_true',
                    help="Describes the tool and its usage")

parser.add_argument("--target", type=str, required=False,
                    help="One or more bmc host descriptor targets ;\n"
                         "type: comma delimited target list")

parser.add_argument("--debug", type=int, required=False, default=0,
                    help="Optional debug level ; 1..4")

parser.add_argument("--service", action='store_true',
                    help="Queries services on devices ;\n")

parser.add_argument("--config", type=str, required=False,
                    help="Configures the endpoint/target server based on \
                          specification of file ;\n"
                         "type: path to a file")

parser.add_argument("--query", action='store_true',
                    help="Queries state of Secure Boot on host")

parser.add_argument("--enable", action='store_true',
                    help="Enables Secure Boot in target server BIOS")

parser.add_argument("--disable", action='store_true',
                    help="Disables Secure Boot in target server BIOS")

parser.add_argument("--upload", type=str, required=False,
                    help="Uploads Secure Boot certificate to server BIOS")

parser.add_argument("--bmc_ip", type=str, required=False,
                    help="Provides IP address required to login to server")

parser.add_argument("--bmc_un", type=str, required=False,
                    help="Provides username required to login to server")

parser.add_argument("--bmc_pw", type=str, required=False,
                    help="Provides password required to login to server")

# get command line arguments
try:
    args = parser.parse_args()
except Exception as ex:
    sys.exit("\n\nError: %s. See --help for more info" % ex)

# Check if Usage is present
help_needed = args.help
if help_needed:
    message = "\nRedfish Secure Boot Tool Help:\n"
    message += "==============================\n"
    message += "Primary actions include:\n"
    message += "   --query: Returns the state of Secure Boot on list of "
    message += "single or list of servers\n"
    message += "   --service: Returns the Redfish Services supported on "
    message += "list of servers\n"
    message += "   --enable/--disable: Enables/Disables Secure Boot on "
    message += "single or list of servers\n"
    message += "   --upload <file_to_upload>: Uploads the certificate "
    message += "specified by the path to single or list of servers\n"
    message += "\n\n"
    message += "Additional flags include:\n"
    message += "   --config <file_to_use>: Specifies the list of target "
    message += "servers to use with the desired service\n"
    message += "   --debug : Specifies the debug level of this service\n"
    message += "\n\n"
    message += "Common use cases:\n"
    message += "   Query Redfish service versions:                    "
    message += "./rsbc.py --service --config ./service_servers.yaml"
    message += "\n   Query state of Secure Boot for target server(s):   "
    message += "./rsbc.py --query --config ./query_servers.yaml\n"
    message += "   Enable Secure Boot:                                "
    message += "./rsbc.py --enable --config ./sb_servers.yaml\n"
    message += "   Upload certificate to Secure Boot:                 "
    message += "./rsbc.py --upload ./certs/certificate.crt "
    message += "--config ./sb_servers.yaml\n"
    message += "\n\n"
    sys.stdout.write(message)
    sys.exit()

# get debug level
debug = args.debug

# target list ; assumes none or comma delimited list
targets = []
if args.target and args.target != 'None':
    targets = args.target.split(',')

# Checks if we are querying, or disabling or enabling or uploading
ENABLE = args.enable
DISABLE = args.disable
SERVICE = args.service
QUERY = args.query
UPLOAD = False
BMC_IP = args.bmc_ip
BMC_UN = args.bmc_un
BMC_PW = args.bmc_pw

if args.upload is not None:
    UPLOAD = True
    certificate = args.upload

if SERVICE or QUERY:
    f = open("output.txt", "w")

# get list of target servers from configuration file:
CONFIG_SWAP_FLAG = False
CONFIG_FILE = None
if args.config is not None:
    CONFIG_SWAP_FLAG = True
    CONFIG_FILE = args.config


def t():
    """
    Return current time for log functions
    """
    return datetime.datetime.now().replace(microsecond=0)


def qlog(string, n=0, SecureBoot=False):
    """
    Query Log Utility
    """
    if SecureBoot:
        array_of_strings = string
        sys.stdout.write("Server Name: %s || Secure Boot Status: %s\n" %
                         (array_of_strings[0], array_of_strings[1]))
    elif n == 0:
        sys.stdout.write("\n%s Query  : %s" % (t(), string))
    else:
        print("\n%s " % t(), end="")
        print("Query  : {: <15} {: <15} {: <20}".format(*string), end="")


def ilog(string):
    """
    Info Log Utility
    """

    if SERVICE or QUERY:
        f.write("\n%s Info  : %s" % (t(), string))
    else:
        sys.stdout.write("\n%s Info  : %s" % (t(), string))


def elog(string):
    """
    Error Log Utility
    """

    if SERVICE or QUERY:
        f.write("\n%s Error : %s" % (t(), string))
    else:
        sys.stdout.write("\n%s Error : %s" % (t(), string))


def alog(string):
    """
    Action Log Utility
    """

    if SERVICE or QUERY:
        f.write("\n%s Action: %s" % (t(), string))
    else:
        sys.stdout.write("\n%s Action: %s" % (t(), string))


def dlog1(string, level=1):
    """
    Debug Log - Level
    """

    if debug and level <= debug:
        if SERVICE or QUERY:
            f.write("\n%s Debug%d: %s" % (t(), level, string))
        else:
            sys.stdout.write("\n%s Debug%d: %s" % (t(), level, string))


def dlog2(string):
    """
    Debug Log - Level 2
    """

    dlog1(string, 2)


def dlog3(string):
    """
    Debug Log - Level 3
    """

    dlog1(string, 3)


def dlog4(string):
    """
    Debug Log - Level 4
    """

    dlog1(string, 4)


def slog(stage):
    """Execution Stage Log"""

    if SERVICE or QUERY:
        f.write("\n%s Info  : %s" % (t(), stage))
    else:
        sys.stdout.write("\n%s Stage : %s" % (t(), stage))


def rsbc_exit(code):
    """Exit not tied to object ; early fault handling"""
    if (SERVICE or QUERY) and code != 0:
        sys.stdout.write("\n")
        sys.stdout.write("Error: Please check the output file for summary")
    sys.stdout.write("\n\n")
    sys.exit(code)


ilog("%s version %d.%d\n" % (FEATURE_NAME, VERSION_MAJOR, VERSION_MINOR))
dlog1("Debug       : %d" % debug)
if len(targets):
    dlog1("Targets     : %s" % (args.target))

# start with an empty object list
target_object_list = []

# Constants
# ---------
REDFISH_ROOT_PATH = '/redfish/v1'
PRIMARY_CONFIG_LABEL = 'virtual_media_iso'       # Primary Config label
SUPPORTED_VIRTUAL_MEDIA_DEVICES = ['CD', 'DVD']  # Maybe add USB to list

# headers for each request type
HDR_CONTENT_TYPE = "'Content-Type': 'application/json'"
HDR_ACCEPT = "'Accept': 'application/json'"

# they all happen to be the same right now
GET_HEADERS = {HDR_CONTENT_TYPE, HDR_ACCEPT}
POST_HEADERS = {HDR_CONTENT_TYPE, HDR_ACCEPT}
PATCH_HEADERS = {HDR_CONTENT_TYPE, HDR_ACCEPT}
UPLOAD_HEADERS = {"'Content-type': 'multipart/form-data'"}

# HTTP request types ; only 3 are required by this tool
POST = 'POST'
GET = 'GET'
PATCH = 'PATCH'
UPLOAD_POST = 'UPLOAD_POST'

# max number of polling retries while waiting for some long task to complete
MAX_POLL_COUNT = 200
# some servers timeout on inter comm gaps longer than 10 secs
RETRY_DELAY_SECS = 10
# 2 second delay constant
DELAY_2_SECS = 2


def is_ipv6_address(address):
    """
    Check IPv6 Address.

    :param address: the ip address to compare user name.
    :type address: str.
    :returns bool: True if address is an IPv6 address else False
    """

    try:
        socket.inet_pton(socket.AF_INET6, address)
        dlog3("Address     : %s is IPv6" % address)
    except socket.error:
        dlog3("Address     : %s is IPv4" % address)
        return False
    return True


def supported_device(devices):
    """
    Supported Device

    :param devices: list of devices
    :type : list
    :returns True if a device in devices list is in the
             SUPPORTED_VIRTUAL_MEDIA_DEVICES list.
             Otherwise False is returned.
    """

    for device in devices:
        if device in SUPPORTED_VIRTUAL_MEDIA_DEVICES:
            return True
    return False


def parse_target(target_name, target_dict):
    """
    Parse key value pairs in target and if successful create
    a vmcObject and add it to the target_object_list.

    :param target_name: arbitrary target label
    :type target_name: str.
    :param target_dict: dictionary of key value config file pairs
    :type target_dict: dictionary
    :returns nothing
    """

    dlog3("Parse Target: %s:%s" % (target_name, target_dict))

    pw = target_dict.get('bmc_password')
    if pw is None:
        elog("Failed get bmc password from config file")
        return

    address = target_dict.get('bmc_address')
    if address is None:
        elog("Failed to decode bmc password found in %s" % CONFIG_FILE)
        alog("Verify config file's bmc password is base64 encoded")
        return

    ####################################################################
    #
    # Add url encoding [] for ipv6 addresses only.
    #
    # Note: The imported redfish library produces a python exception
    #       on the session close if the ipv4 address has [] around it.
    #
    #       I debugged the issue and know what it is and how to fix it
    #       but requires an upstream change that is not worth doing.
    #
    # URL Encoding for IPv6 only is an easy solution.
    #
    ######################################################################
    if is_ipv6_address(address) is True:
        bmc_ipv6 = True
        address = '[' + address + ']'
    else:
        bmc_ipv6 = False

    # Create object and add it to the target object list
    try:
        vmc_obj = VmcObject(target_name,
                            address,
                            target_dict.get('bmc_username'),
                            pw)
        if vmc_obj:
            vmc_obj.ipv6 = bmc_ipv6
            target_object_list.append(vmc_obj)
        else:
            elog("Unable to create control object for target:%s ; "
                 "skipping ..." % target_dict)

    except Exception as ex:
        elog("Unable to parse configuration for '%s' (%s)"
             "in config file." % (target_dict, ex))
        alog("Check presence and spelling of configuration"
             " members under '%s' for target '%s'." %
             (PRIMARY_CONFIG_LABEL, target_dict))
    return


class VmcObject(object):
    """
    Virtual Media Controller Class Object. One for each BMC
    """

    def __init__(self,
                 hostname,
                 address,
                 username,
                 password):

        self.target = hostname
        self.uri = "https://" + address
        self.url = REDFISH_ROOT_PATH
        self.un = username.rstrip()
        self.ip = address.rstrip()
        self.pw = password.rstrip()
        self.ipv6 = False
        self.redfish_obj = None     # redfish client connection object
        self.session = False        # True when session for this BMC is created

        self.response = None        # holds response from last http request
        self.response_json = None   # json formatted version of above response
        self.response_dict = None   # dictionary version of aboe response

        # redfish root query response
        self.root_query_info = None  # json version of the full root query

        # Managers Info
        self.managers_group_url = None
        self.manager_members_list = []

        # Virtual Media Info
        self.vm_url = None
        self.vm_eject_url = None
        self.vm_group_url = None
        self.vm_group = None
        self.vm_label = None
        self.vm_version = None
        self.vm_actions = {}
        self.vm_members_array = []
        self.vm_media_types = []

        # systems info
        self.systems_group_url = None
        self.sys_mem_url = None
        self.systems_members_list = []
        self.systems_members = 0
        self.power_state = None

        # secure boot info
        self.sb_url = None
        self.db_cert_url = None
        self.sb_db_url = None

        # boot control info
        self.boot_control_dict = {}

        # systems reset info
        self.reset_command_url = None
        self.reset_action_dict = {}

        # parsed target object info
        if self.target is not None:
            dlog1("Target      : %s" % self.target)
        dlog1("BMC IP      : %s" % self.ip)
        dlog1("Username    : %s" % self.un)
        dlog1("Password    : %s" % self.pw)

    def make_request(self, operation=None, path=None, payload=None):
        """
        Issue a Redfish http request,
        Check response,
        Convert response to dictionary format
        Convert response to json format

        :param operation: HTTP GET, POST or PATCH operation
        :type operation: str.
        :param path: url to perform request to
        :type path: str
        :param payload: POST or PATCH payload data
        :type payload: dictionary
        :returns True if request succeeded (200,202(accepted),204(no content)
        """

        self.response = None
        if path is not None:
            url = path
        else:
            url = self.url

        before_request_time = datetime.datetime.now().replace(microsecond=0)
        try:
            dlog3("Request     : %s %s" % (operation, url))
            if operation == GET:
                dlog3("Headers     : %s : %s" % (operation, GET_HEADERS))
                self.response = self.redfish_obj.get(url, headers=GET_HEADERS)

            elif operation == POST:
                dlog3("Headers     : %s : %s" % (operation, POST_HEADERS))
                dlog3("Payload     : %s" % payload)
                self.response = self.redfish_obj.post(url,
                                                      body=payload,
                                                      headers=POST_HEADERS)
            elif operation == PATCH:
                dlog3("Headers     : %s : %s" % (operation, PATCH_HEADERS))
                dlog3("Payload     : %s" % payload)
                self.response = self.redfish_obj.patch(url,
                                                       body=payload,
                                                       headers=PATCH_HEADERS)
            elif operation == UPLOAD_POST:
                dlog3("Headers     : %s : %s" % (operation, UPLOAD_HEADERS))
                dlog3("Payload     : %s" % payload)
                self.response = self.redfish_obj.post(url,
                                                      body=payload,
                                                      headers=UPLOAD_HEADERS)
            else:
                elog("Unsupported operation: %s" % operation)
                return False

        except Exception as ex:
            elog("Failed operation on '%s' (%s)" % (url, ex))

        if self.response is not None:
            after_request_time = datetime.datetime.now().replace(microsecond=0)
            delta = after_request_time - before_request_time
            # if we got a response, check its status
            if self.check_ok_status(url, operation, delta.seconds) is False:
                self._exit(1)

            # handle 204 success with no content ; clear last response
            if self.response.status == 204:
                self.response = ""
                return True
            try:
                if self.resp_dict() is True:
                    if self.format() is True:
                        dlog4("Response:\n%s\n" % self.response_json)
                        return True
                    else:
                        elog("Failed to parse BMC %s response '%s'" %
                             (operation, url))

            except Exception as ex:
                elog("Failed to parse BMC %s response '%s' (%s)" %
                     (operation, url, ex))

            elog("Response:\n%s\n" % self.response)
        else:
            elog("No response from %s:%s" % (operation, url))
        return False

    def resp_dict(self):
        """
        Create Response Dictionary
        """

        if self.response.read:
            self.response_dict = None
            try:
                self.response_dict = json.loads(self.response.read)
                return True
            except Exception as ex:
                elog("Got exception key valuing response ; (%s)" % ex)
                elog("Response: " % self.response.read)
        else:
            elog("No response from last command")
        return False

    def format(self):
        """
        Format Response as Json
        """

        self.response_json = None
        try:
            if self.resp_dict() is True:
                self.response_json = json.dumps(self.response_dict,
                                                indent=4,
                                                sort_keys=True)
                return True
            else:
                return False

        except Exception as ex:
            elog("Got exception formatting response ; (%s)\n" % ex)
            return False

    def get_key_value(self, key1, key2=None):
        """
        Get key1 value if no key2 is specified.
        Get key2 value from key1 value if key2 is specified.

        :param : key1 value is returned if no key2 is provided.
        :type : str.
        :param : key2 value is optional but if provided its value is returned
        :type : str
        :returns key1 value or key2 value if key2 is specified
        """

        value1 = self.response_dict.get(key1)
        if key2 is None:
            return value1
        return value1.get(key2)

    def check_ok_status(self, function, operation, seconds):
        """
        Status

        :param function: description of operation
        :type : str
        :param operation: http GET, POST or PATCH
        :type : str
        :returns True if response status is OK. Otherwise False.
        """

        # Accept applicable 400 series error from an Eject Request POST.
        # This error is dealt with by the eject handler.
        if self.response.status in [400, 403, 404] and \
                function == self.vm_eject_url and \
                operation == POST:
            return True

        if self.response.status not in [200, 202, 204]:
            try:
                elog("HTTP Status : %d ; %s %s failed after %i seconds\n%s\n" %
                     (self.response.status,
                      operation, function, seconds,
                      json.dumps(self.response.dict,
                                 indent=4, sort_keys=True)))
                return False
            except Exception as ex:
                elog("check status exception ; %s" % ex)

        dlog2("HTTP Status : %s %s Ok (%d) (took %i seconds)" %
              (operation, function, self.response.status, seconds))
        return True

    def _exit(self, code):
        """
        Exit the tool but not before closing an open Redfish
        client connection.

        :param code: the exit code
        :type code: int
        """

        if self.redfish_obj is not None and self.session is True:
            try:
                self.redfish_obj.logout()
                self.redfish_obj = None
                self.session = False
                dlog1("Session     : Closed")

            except Exception as ex:
                elog("Session close failed ; %s" % ex)
                alog("Check BMC username and password in config file")

        if code:
            elog("\n-------------------------------------------\n")

            # If exit with reason code then print that reason code and dump
            # the redfish query data that was learned up to that point
            elog("Code : %s" % code)

            # Other info
            ilog("IPv6      : %s" % self.ipv6)

            # Root Query Info
            ilog("Root Query: %s" % self.root_query_info)

            # Managers Info
            ilog("Manager URL: %s" % self.managers_group_url)
            ilog("Manager Members List: %s" % self.manager_members_list)

            # Systems Info
            ilog("Systems Group URL: %s" % self.systems_group_url)
            ilog("Systems Member URL: %s" % self.sys_mem_url)
            ilog("Systems Members: %d" % self.systems_members)
            ilog("Systems Members List: %s" % self.systems_members_list)

            ilog("Power State: %s" % self.power_state)
            ilog("Reset Actions: %s" % self.reset_action_dict)
            ilog("Reset Command URL: %s" % self.reset_command_url)
            ilog("Boot Control Dict: %s" % self.boot_control_dict)

            ilog("VM Members Array: %s" % self.vm_members_array)
            ilog("VM Group URL: %s" % self.vm_group_url)
            ilog("VM Group: %s" % self.vm_group)
            ilog("VM URL: %s" % self.vm_url)
            ilog("VM Label: %s" % self.vm_label)
            ilog("VM Version: %s" % self.vm_version)
            ilog("VM Actions: %s" % self.vm_actions)
            ilog("VM Media Types: %s" % self.vm_media_types)

            ilog("Last Response raw: %s" % self.response)
            ilog("Last Response json: %s" % self.response_json)

        rsbc_exit(code)

    ###########################################################################
    #
    #     P R I V A T E    S T A G E    M E M B E R    F U N C T I O N S
    #
    ###########################################################################

    ###########################################################################
    # Redfish Client Connect
    ###########################################################################
    def _redfish_client_connect(self):
        """
        Connect to target Redfish service.
        """

        stage = 'Redfish Client Connection'
        slog(stage)

        # Verify ping response
        ping_ok = False
        ping_count = 0
        MAX_PING_COUNT = 10
        while ping_count < MAX_PING_COUNT and ping_ok is False:
            response = 0
            if self.ipv6 is True:
                response = os.system("ping -6 -c 1 " +
                                     self.ip[1:-1] + " > /dev/null 2>&1")
            else:
                response = os.system("ping -c 1 " +
                                     self.ip + " > /dev/null 2>&1")

            if response == 0:
                ping_ok = True
            else:
                ping_count = ping_count + 1
                ilog("BMC Ping     : retry (%i of %i)" %
                     (ping_count, MAX_PING_COUNT))
                time.sleep(2)

        if ping_ok is False:
            elog("Unable to ping '%s' (%i)" % (self.ip, ping_count))
            alog("Check BMC ip address is pingable")
            self._exit(1)
        else:
            ilog("BMC Ping Ok : %s (%i)" % (self.ip, ping_count))

        # try to connect
        connect_error = False
        try:
            # One time Redfish Client Object Create
            self.redfish_obj = \
                redfish.redfish_client(base_url=self.uri,
                                       username=self.un,
                                       password=self.pw,
                                       default_prefix=REDFISH_ROOT_PATH)
            if self.redfish_obj is None:
                connect_error = True
                elog("Unable to establish %s to BMC at %s" %
                     (stage, self.uri))
        except Exception as ex:
            connect_error = True
            elog("Unable to establish %s to BMC at %s (%s)" %
                 (stage, self.uri, ex))

        if connect_error is True:
            alog("Check BMC ip address is pingable and supports Redfish")
            self._exit(1)

    ###########################################################################
    # Redfish Root Query
    ###########################################################################
    def _redfish_root_query(self):
        """
        Redfish Root Query
        """

        stage = 'Root Query'
        slog(stage)

        if self.make_request(operation=GET, path=None) is False:
            elog("Failed %s GET request")
            self._exit(1)

        if self.response_json:
            self.root_query_info = self.response_json

        # extract the systems get url needed to learn reset
        # actions for the eventual reset.
        #
        # "Systems": { "@odata.id": "/redfish/v1/Systems/" },
        #
        # See Reset section below ; following iso insertion where
        # systems_group_url is used.
        self.systems_group_url = self.get_key_value('Systems', '@odata.id')

    ###########################################################################
    # Create Redfish Communication Session
    ###########################################################################
    def _redfish_create_session(self):
        """
        Create Redfish Communication Session
        """

        stage = 'Create Communication Session'
        slog(stage)

        try:
            self.redfish_obj.login(auth="session")
            dlog1("Session     : Open")
            self.session = True

        except Exception as ex:
            elog("Failed to Create session ; %s" % ex)
            self._exit(1)

    ###########################################################################
    # Query Redfish Managers
    ###########################################################################
    def _redfish_get_managers(self):
        """
        Query Redfish Managers
        """

        stage = 'Get Managers'
        slog(stage)

        # Virtual Media support is located through the
        # Managers link of the root query response.
        #
        # This section learns that Managers URL Link from the
        # Root Query Result:
        #
        # Expecting something like this ...
        #
        # {
        #    ...
        #    "Managers":
        #    {
        #        "@odata.id": "/redfish/v1/Managers/"
        #    },
        #    ...
        # }

        # Get Managers Link from the last Get response currently
        # in self.response_json
        self.managers_group_url = self.get_key_value('Managers', '@odata.id')
        if self.managers_group_url is None:
            elog("Failed to learn BMC RedFish Managers link")
            self._exit(1)

        # Managers Query (/redfish/v1/Managers/)
        if self.make_request(operation=GET,
                             path=self.managers_group_url) is False:
            elog("Failed GET Managers from %s" % self.managers_group_url)
            self._exit(1)

        # Look for the Managers 'Members' URL Link list from the Managers Query
        #
        # Expect something like this ...
        #
        # {
        #    ...
        #    "Members":
        #    [
        #         { "@odata.id": "/redfish/v1/Managers/1/" }
        #    ],
        #   ...
        # }
        # Support multiple Managers in the list

        self.manager_members_list = self.get_key_value('Members')

    ######################################################################
    # Get Systems Members
    ######################################################################
    def _redfish_get_systems_members(self):
        """
        Get Systems Members
        """

        stage = 'Get Systems'
        slog(stage)

        # Query Systems Group URL for list of Systems Members
        if self.make_request(operation=GET,
                             path=self.systems_group_url) is False:
            elog("Unable to %s Members from %s" %
                 (stage, self.systems_group_url))
            self._exit(1)

        self.systems_members_list = self.get_key_value('Members')
        dlog3("Systems Members List: %s" % self.systems_members_list)
        if self.systems_members_list is None:
            elog("Systems Members URL GET Response\n%s" % self.response_json)
            self._exit(1)

        self.systems_members = len(self.systems_members_list)
        if self.systems_members == 0:
            elog("BMC not publishing any System Members:\n%s" %
                 self.response_json)
            self._exit(1)

    ######################################################################
    # Power On or Off Host
    ######################################################################
    def _redfish_powerctl_host(self, state):
        """
        Power On or Off the Host
        """
        stage = 'Power ' + state + ' Host'
        slog(stage)

        if self.power_state == state:
            # already in required state
            return

        # Walk the Systems Members list looking for Action support.
        #
        #  "Members": [ { "@odata.id": "/redfish/v1/Systems/1/" } ],
        #
        # Loop over Systems Members List looking for Reset Actions Dictionary
        info = 'Redfish Systems Actions Member'
        self.sys_mem_url = None
        for member in range(self.systems_members):
            systems_member = self.systems_members_list[member]
            if systems_member:
                self.sys_mem_url = systems_member.get('@odata.id')
            if self.sys_mem_url is None:
                elog("Unable to get %s URL:\n%s\n" %
                     (info, self.response_json))
                self._exit(1)

            if self.make_request(operation=GET,
                                 path=self.sys_mem_url) is False:
                elog("Unable to get %s from %s" %
                     (info, self.sys_mem_url))
                self._exit(1)

            # Look for Reset Actions Dictionary
            self.reset_action_dict = \
                self.get_key_value('Actions', '#ComputerSystem.Reset')
            if self.reset_action_dict is None:
                # try other URL
                self.sys_mem_url = None
                continue
            else:
                # Got the Reset Actions Dictionary

                # get powerState
                self.power_state = self.get_key_value('PowerState')

                # Ensure we don't issue current state command
                if state in [POWER_OFF, POWER_ON]:
                    # This is a Power ON or Off command
                    if self.power_state == state:
                        dlog2("Power already %s" % state)
                        # ... AND we are already in that state then
                        # we are done. Issuing a power command while
                        # in the same state will error out.
                        # So don't do it.
                        return
                break

        info = 'Systems Reset Action Dictionary'
        if self.reset_action_dict is None:
            elog("BMC not publishing %s:\n%s\n" %
                 (info, self.response_json))
            self._exit(1)

        ##############################################################
        # Reset Actions Dictionary. This is what we are looking for  #
        ##############################################################
        #
        # Look for Reset Actions label
        #
        # "Actions":
        # {
        #   "#ComputerSystem.Reset":
        #   {
        #     "ResetType@Redfish.AllowableValues": [
        #       "On",
        #       "ForceOff",
        #       "ForceRestart",
        #       "Nmi",
        #       "PushPowerButton"
        #     ],
        #     "target":"/redfish/v1/Systems/1/Actions/ComputerSystem.Reset/"
        #   }
        # }
        #
        # Need to get 2 pieces of information out of the Actions output
        #
        #  1. the Redfish Systems Reset Action Target
        #  2. the Redfish Systems Reset Action List
        #
        ###############################################################

        info = 'Systems Reset Action Target'
        self.reset_command_url = self.reset_action_dict.get('target')
        if self.reset_command_url is None:
            elog("Unable to get Reset Command URL (members:%d)\n%s" %
                 (self.systems_members, self.reset_action_dict))
            self._exit(1)

        # With the reset target url in hand, all that is needed now
        # is the reset command this target supports
        #
        # The reset command list looks like this.
        #
        #        "ResetType@Redfish.AllowableValues": [
        #            "On",
        #            "ForceOff",
        #            "ForceRestart",
        #            "Nmi",
        #            "PushPowerButton"
        #        ],
        #
        # Some targets support GracefulRestart and/or ForceRestart

        info = 'Allowable Reset Actions'
        reset_command_list = \
            self.reset_action_dict.get('ResetType@Redfish.AllowableValues')
        if reset_command_list is None:
            elog("BMC is not publishing any %s" % info)
            self._exit(1)

        dlog3("ResetActions: %s" % reset_command_list)

        # load the appropriate acceptable command list
        if state == POWER_OFF:
            acceptable_commands = ['ForceOff', 'GracefulShutdown']
        elif state == POWER_ON:
            acceptable_commands = ['ForceOn', 'On']
        else:
            acceptable_commands = ['ForceRestart', 'GracefulRestart']

        # Look for the best command for the power state requested.
        command = None
        for acceptable_command in acceptable_commands:
            for reset_command in reset_command_list:
                if reset_command == acceptable_command:
                    command = reset_command
                    break
            else:
                continue
            break

        if command is None:
            elog("Failed to find acceptable Power %s command in:\n%s" %
                 (state, reset_command_list))
            self._exit(1)

        # All that is left to do is POST the reset command
        # to the reset_command_url.
        payload = {'ResetType': command}
        if self.make_request(operation=POST,
                             payload=payload,
                             path=self.reset_command_url) is False:
            elog("Failed to Power %s Host" % state)
            self._exit(1)

        if state not in [POWER_OFF, POWER_ON]:
            # no need to refresh power state if
            # this was not a power command
            return

        # poll for requested power state.
        poll_count = 0
        MAX_STATE_POLL_COUNT = 60  # some servers take longer than 10 seconds
        while poll_count < MAX_STATE_POLL_COUNT and self.power_state != state:
            time.sleep(1)
            poll_count = poll_count + 1

            # get systems info
            if self.make_request(operation=GET,
                                 path=self.sys_mem_url) is False:
                elog("Failed to Get System State (%i of %i)" %
                     (poll_count, MAX_STATE_POLL_COUNT))
            else:
                # get powerState
                self.power_state = self.get_key_value('PowerState')
                if self.power_state != state:
                    dlog1("waiting for power %s (%s) (%d)" %
                          (state, self.power_state, poll_count))
        if self.power_state != state:
            elog("Failed to Set System Power State to %s (%s)" %
                 (self.power_state, self.sys_mem_url))
            self._exit(1)
        else:
            ilog("%s verified (%d)" % (stage, poll_count))

    ######################################################################
    # Get CD/DVD Virtual Media URL
    ######################################################################
    def _redfish_get_vm_url(self):

        """
        Get CD/DVD Virtual Media URL from one of the Manager Members list
        """

        stage = 'Get CD/DVD Virtual Media'
        slog(stage)

        if self.manager_members_list is None:
            elog("Unable to index Managers Members from %s" %
                 self.managers_group_url)
            self._exit(1)

        members = len(self.manager_members_list)
        if members == 0:
            elog("BMC is not publishing any redfish Manager Members")
            self._exit(1)

        # Issue a Get from each 'Manager Member URL Link looking
        # for supported virtual devices.
        for member in range(members):
            member_url = None
            this_member = self.manager_members_list[member]
            if this_member:
                member_url = this_member.get('@odata.id')
            if member_url is None:
                continue
            if self.make_request(operation=GET, path=member_url) is False:
                elog("Unable to get Manager Member from %s" % member_url)
                self._exit(1)

            ########################################################
            #                Query Virtual Media                   #
            ########################################################
            # Look for Virtual Media Support by this Manager Member
            #
            # Expect something like this ...
            #
            # {
            #    ...
            #    "VirtualMedia":
            #    {
            #        "@odata.id": "/redfish/v1/Managers/1/VirtualMedia/"
            #    }
            #    ...
            # }
            self.vm_group_url = None
            self.vm_group = self.get_key_value('VirtualMedia')
            if self.vm_group is None:
                if (member + 1) == members:
                    elog("Virtual Media not supported by target BMC")
                    self._exit(1)
                else:
                    dlog3("Virtual Media not supported by member %d" % member)
                    continue
            else:
                try:
                    self.vm_group_url = self.vm_group.get('@odata.id')
                except Exception:
                    elog("Unable to get Virtual Media Group from %s" %
                         self.vm_group_url)
                    self._exit(1)

            # Query this member's Virtual Media Service Group
            if self.make_request(
                    operation=GET, path=self.vm_group_url) is False:
                elog("Failed to GET Virtual Media Service group from %s" %
                     self.vm_group_url)
                continue

            # Look for Virtual Media Device URL Links
            #
            # Expect something like this ...
            #
            # {
            #   ...
            #   "Members":
            #   [
            #       { "@odata.id": "/redfish/v1/Managers/1/VirtualMedia/1/" },
            #       { "@odata.id": "/redfish/v1/Managers/1/VirtualMedia/2/" }
            #   ],
            #    ...
            # }
            self.vm_members_array = []
            try:
                self.vm_members_array = self.get_key_value('Members')
                vm_members = len(self.vm_members_array)
            except Exception:
                vm_members = 0

            if vm_members == 0:
                elog("No Virtual Media members found at %s" %
                     self.vm_group_url)
                self._exit(1)

            # Loop over each member's URL looking for the CD or DVD device
            # Consider trying the USB device as well if BMC supports that.
            for vm_member in range(vm_members):

                # Look for Virtual Media Device URL
                this_member = self.vm_members_array[vm_member]
                if this_member:
                    self.vm_url = this_member.get('@odata.id')

                if self.make_request(operation=GET, path=self.vm_url) is False:
                    elog("Failed to GET Virtual Media Service group from %s" %
                         self.vm_group_url)
                    continue

                # Query Virtual Media Device Type looking for supported device
                self.vm_media_types = self.get_key_value('MediaTypes')
                if self.vm_media_types is None:
                    dlog3("No Virtual MediaTypes found at %s ; "
                          "trying other members" % self.vm_url)
                    break

                dlog4("Virtual Media Service:\n%s" % self.response_json)

                if supported_device(self.vm_media_types) is True:
                    dlog3("Supported Virtual Media found at %s ; %s" %
                          (self.vm_url, self.vm_media_types))
                    break
                else:
                    dlog3("Virtual Media %s does not support CD/DVD ; "
                          "trying other members" % self.vm_url)
                    self.vm_url = None

            if self.vm_url is None:
                elog("Failed to find CD or DVD Virtual media type")
                self._exit(1)

    ######################################################################
    # Get Virtual Media Version
    ######################################################################
    def _redfish_get_vm_version(self):
        """
        Gets Virtual Media Version
        """

        stage = 'Check the version of the virtual media service'
        slog(stage)

        if self.vm_url is None:
            elog("Failed to find CD or DVD Virtual media type")
            return

        # Extract Virtual Media Version and Insert/Eject Actions
        #
        # Looks something like this. First half of odata.type is the VM label
        #
        # {
        #   ...
        #   "@odata.type": "#VirtualMedia.v1_2_0.VirtualMedia",
        #   "Actions": {
        #   "#VirtualMedia.EjectMedia":
        #   {
        #     "target" :
        #     ".../Managers/1/VirtualMedia/2/Actions/VirtualMedia.EjectMedia/"
        #   },
        #   "#VirtualMedia.InsertMedia":
        #   {
        #     "target":
        #     ".../Managers/1/VirtualMedia/2/Actions/VirtualMedia.InsertMedia/"
        #   }
        #   ...
        # },

        vm_data_type = self.get_key_value('@odata.type')
        if vm_data_type:
            self.vm_label = vm_data_type.split('.')[0]
            self.vm_version = vm_data_type.split('.')[1]

        output_array = [self.vm_label[1:], self.vm_version, self.target]
        qlog(output_array, 1)

    ######################################################################
    # Get Secure Boot Version
    ######################################################################
    def _redfish_get_secure_boot_version(self):
        """
        Gets Secure Boot Version
        """

        stage = 'Check if there is a Secure Boot Service available'
        slog(stage)

        # Retrieving SecureBoot URI
        self.sys_mem_url = self.systems_members_list[0]["@odata.id"]
        # Might not be systems embedded url. Just first member of list

        # Retrieving redfish/v1/Systems/ members list info
        sys_mem = self.sys_mem_url
        if self.make_request(operation=GET, path=sys_mem) is False:
            elog("Failed %s GET request")
            return

        if self.response_json:
            self.response_dict = json.loads(self.response.read)
            secure_boot_dict = self.response_dict

        try:
            self.sb_url = secure_boot_dict["SecureBoot"]["@odata.id"]
        except Exception as ex:
            elog("Unable to retrieve SB resource: %s" % ex)
            return

        # Retrieving redfish/v1/Systems/System.Embedded.1/SecureBoot info
        if self.make_request(operation=GET, path=self.sb_url) is False:
            elog("Failed %s GET request")
            return

        if self.response_json is None:
            qlog("Unable to retrieve Secure Boot URL")
            return

        # Retrieving Secure Boot Version
        secure_boot_type = self.get_key_value("@odata.type")
        if secure_boot_type is None:
            qlog("Unable to retrieve Secure Boot Version Information")
            return

        secure_boot_version = secure_boot_type.split('.')[1]
        output_array = ["Secure Boot", secure_boot_version, self.target]
        qlog(output_array, 1)

    ######################################################################
    # Get Secure Boot State
    ######################################################################
    def _redfish_query_sb_state(self):
        """
        Gets Secure Boot State
        """

        stage = 'Check and output Secure Boot State'
        slog(stage)

        # Get SecureBoot URI
        self.sys_mem_url = self.systems_members_list[0]["@odata.id"]
        # may not be systems embedded url. Just first memebr of members list.

        if self.make_request(operation=GET, path=self.sys_mem_url) is False:
            elog("Failed %s GET request")
            return

        if self.response_json:
            self.response_dict = json.loads(self.response.read)
            secure_boot_dict = self.response_dict

        # Should be "SecureBoot":{"@odata.id":"~/System.Embedded.1/SecureBoot"}
        try:
            self.sb_url = secure_boot_dict["SecureBoot"]["@odata.id"]
        except Exception as ex:
            elog("Error: %s" % ex)
            elog("Secure Boot is not supported on this device")
            return

        # Get SB Status
        if self.make_request(operation=GET, path=self.sb_url) is False:
            elog("Failed %s GET request")
            return

        try:
            self.response_dict = json.loads(self.response.read)
            status = self.response_dict["SecureBootEnable"]
            if status:
                qlog([str(self.target), "Enabled"], SecureBoot=True)
                ilog("Secure Boot is Enabled")
            else:
                qlog([str(self.target), "Disabled"], SecureBoot=True)
                ilog("Secure Boot is Disabled")

        except Exception as ex:
            elog("Error: %s" % ex)
            elog("Unable to get Secure Boot Status")
            self._exit(1)

    ######################################################################
    # Get Secure Boot Certificates
    ######################################################################
    def _redfish_get_secure_boot_certificates(self):
        """
        Gets Secure Boot Certificates
        """

        stage = 'Query and output Secure Boot certificates'
        slog(stage)

        # Get SecureBoot URI
        self.sys_mem_url = self.systems_members_list[0]["@odata.id"]
        # May not be systems embedded url. Just first member of members list.

        if self.make_request(operation=GET, path=self.sys_mem_url) is False:
            elog("Failed %s GET request")
            return 1

        if self.response_json:
            self.response_dict = json.loads(self.response.read)
            secure_boot_dict = self.response_dict

        try:
            self.sb_url = secure_boot_dict["SecureBoot"]["@odata.id"]
        except Exception as ex:
            elog("Unable to retrieve SB resource: %s" % ex)
            return 1

        # Get DB Certificates URL
        if self.make_request(operation=GET, path=self.sb_url) is False:
            elog("Failed %s GET request")
            return 1

        try:
            response_dict = json.loads(self.response.read)
            sb_database = response_dict["SecureBootDatabases"]["@odata.id"]
        except Exception as ex:
            elog("Unable to retrieve SB Databases URL: %s" % ex)
            return 1

        # Get DB Certificate URL
        if self.make_request(operation=GET, path=sb_database) is False:
            elog("Failed %s GET request")
            return 1

        try:
            self.response_dict = json.loads(self.response.read)
            self.sb_db_url = self.response_dict["Members"][0]["@odata.id"]
        except Exception as ex:
            elog("Could not retrieve DB Certificates URL: %s" % ex)
            return 1

        # Get a list of DB Certificates
        if self.make_request(operation=GET, path=self.sb_db_url) is False:
            elog("Failed to retrieve SecureBootDatabases/db URL")
            return 1

        self.db_cert_url = self.sb_db_url + "/Certificates"

        if self.make_request(operation=GET, path=self.db_cert_url) is False:
            elog("Failed to retrieve db/Certificates URL")
            return 1

        try:
            # DB_certificates is a list of ALL DB certificates
            self.response_dict = json.loads(self.response.read)
            members_dict = self.response_dict["Members"]
        except Exception as ex:
            elog("Could not retrieve Certificate Members: %s" % ex)
            return 1

        DB_certificates = []
        for cert_member in members_dict:
            cert = cert_member["@odata.id"]
            try:
                self.make_request(operation=GET, path=cert)
                cert_info = json.loads(self.response.read)
                DB_certificates.append(cert_info)
            except Exception as ex:
                elog("Could not retrieve certificate: %s" % ex)
                return 1

        curr_date = str(datetime.datetime.now())[0:10]
        curr_time = str(datetime.datetime.now())[11:16]
        file_name = self.target + "_" + curr_date + "_" + curr_time + ".txt"
        cert_file = open(file_name, "w")

        for cert in DB_certificates:
            cert_file.write(json.dumps(cert))
            cert_file.write("\n\n")
        cert_file.close()
        return 0

    ######################################################################
    # Enable Secure Boot
    ######################################################################
    def _redfish_enable_secure_boot(self):
        """
        Enables Secure Boot
        """

        stage = 'Enables/Disables secure boot'
        slog(stage)

        # Retrieving SecureBoot URI
        try:
            self.sys_mem_url = self.systems_members_list[0]["@odata.id"]
        except Exception as ex:
            elog("Error: Could not access systems member URL: %s" % ex)
            return 1

        # Retrieving redfish/v1/Systems/System.Embedded.1/ info
        if self.make_request(operation=GET, path=self.sys_mem_url) is False:
            elog("Failed %s GET request" % self.sys_mem_url)
            return 1

        if self.response_json:
            self.response_dict = json.loads(self.response.read)
            secure_boot_dict = self.response_dict
            try:
                self.sb_url = secure_boot_dict["SecureBoot"]["@odata.id"]
            except Exception as ex:
                print("Secure Boot is not supported: %s" % ex)
                return 1

        # Retrieving redfish/v1/Systems/System.Embedded.1/SecureBoot info
        if self.make_request(operation=GET, path=self.sb_url) is False:
            elog("Failed %s GET request")
            return 1

        if self.response_json:
            self.response_dict = json.loads(self.response.read)
            secure_boot_info = self.response_dict

        if secure_boot_info is None:
            ilog("Unable to retrieve SB URL")
            return 1

        # Check whether secure boot is enabled or not
        current_device_state = self.get_key_value("SecureBootEnable")

        # Check to see if server is already in desired state
        if ENABLE:
            if current_device_state is True:
                ilog("Device is already in the desired state")
                rsbc_exit(0)
            payload = {"SecureBootEnable": True}
        elif DISABLE:
            if current_device_state is False:
                rsbc_exit(0)
            payload = {"SecureBootEnable": False}

        # Makes request and create action
        if self.make_request(operation=PATCH,
                             path=self.sb_url,
                             payload=payload) is False:
            if ENABLE:
                elog("Unable to Enable Secure Boot")
            elif DISABLE:
                elog("Unable to Disable Secure Boot")
            return 1

        # Action succeeded - Restart Host
        # Note: This make take several minutes

        self._redfish_powerctl_host(POWER_RESET)

    ######################################################################
    # Upload Certificates
    ######################################################################
    def _redfish_upload_certificates(self, path):
        """
        Uploads certificates for RedFish Secure Boot
        """

        stage = 'Uploading a certificate'
        slog(stage)

        # Get SecureBoot URI
        try:
            # may not be systems embedded url. Just first of the members list.
            self.sys_mem_url = self.systems_members_list[0]["@odata.id"]
        except Exception as ex:
            elog("Key Error: %s. Could not get systems member URL" % ex)
            return 1

        if self.make_request(operation=GET, path=self.sys_mem_url) is False:
            elog("Failed %s GET request")
            return 1

        if self.response_json:
            self.response_dict = json.loads(self.response.read)
            secure_boot_dict = self.response_dict

            try:
                self.sb_url = secure_boot_dict["SecureBoot"]["@odata.id"]
            except Exception as ex:
                elog("Unable to retrieve SB resource: %s" % ex)
                return 1

        # Get Certificates URL
        if self.make_request(operation=GET, path=self.sb_url) is False:
            elog("Failed %s GET Certificates URL")
            return 1

        try:
            response_dict = json.loads(self.response.read)
            sb_db = response_dict["SecureBootDatabases"]["@odata.id"]
        except Exception as ex:
            elog("Unable to retrieve SB Database resource: %s" % ex)
            return 1

        # Get DB Certificate URL
        if self.make_request(operation=GET, path=sb_db) is False:
            elog("Failed to get DB Certificate URL: %s" % sb_db)
            return
        try:
            self.response_dict = json.loads(self.response.read)
            self.sb_db_url = self.response_dict["Members"][0]["@odata.id"]
            sys.stdout.write(self.sb_db_url)
        except Exception as ex:
            elog("Unable to retrieve DB Certificates URL: %s" % ex)
            return 1

        # Get a list of existing certificates
        if self.make_request(operation=GET, path=self.sb_db_url) is False:
            elog("Failed %s GET request")
            return 1

        try:
            # DB_certificates is a list of ALL DB certificates
            self.response_dict = json.loads(self.response.read)
        except Exception as ex:
            elog("Unable to load DB certificates from JSON to Dict: %s" % ex)
            return 1

        # Open the Public Key Certificate
        if path.endswith(".pem"):
            try:
                cert = open(path, "r").read()
            except Exception as ex:
                elog("Unable to open certificate path %s\n" % ex)
                return 1
        elif path.endswith(".der") or path.endswith(".crt"):
            try:
                cert_dem = open(path, "rb").read()
                cert = ssl.DER_cert_to_PEM_cert(cert_dem)
            except Exception as ex:
                elog("Unable to open certificate path %s\n" % ex)
                return 1
        else:
            return 1

        # Upload the Certificate
        payload_dictionary = {"CertificateString": cert,
                              "CertificateType": "PEM"
                              }
        sys.stdout.write(str(cert))
        url = self.uri
        url += self.sb_db_url
        url += "/Certificates"
        url = str(url)

        headers = {'Content-Type': 'application/json',
                   'Authorization': 'Basic c3lzYWRtaW46TGk2OW51eCo='
                   }

        payload = json.dumps(payload_dictionary)

        try:
            response = requests.request("POST",
                                        url,
                                        headers=headers,
                                        data=payload,
                                        verify=False)

            if response.status_code == 204 or response.status_code == 200:
                # Action succeeded - Restart Host
                # Note: This make take several minutes
                self._redfish_powerctl_host(POWER_RESET)
            else:
                elog("Response code is %s\n" % response.status_code)
                return 1
        except Exception as ex:
            elog("Could not upload certificate: %s" % ex)
            return 1

        ilog("Completed Certicate Upload!")

    ######################################################################
    # Power Off Host
    ######################################################################
    def _redfish_poweroff_host(self):
        """
        Power Off the Host
        """

        self._redfish_powerctl_host(POWER_OFF)

    ######################################################################
    # Power On Host
    ######################################################################
    def _redfish_poweron_host(self):
        """
        Power On or Off the Host
        """

        self._redfish_powerctl_host(POWER_ON)

    ######################################################################
    # Execute function
    ######################################################################
    def execute(self, num_of_times_executed):
        """Redfish Info Query"""

        self._redfish_client_connect()
        self._redfish_root_query()
        self._redfish_create_session()
        if UPLOAD:
            self._redfish_get_managers()
            self._redfish_get_systems_members()
            result = self._redfish_upload_certificates(certificate)
            if result == 1:
                elog("Upload Failed\n")
                sys.stdout.write("\nCommon errors:\n")
                sys.stdout.write("SB Custom Mode must be enabled in BIOS\n")
                sys.stdout.write("Certificate must have extension:\n")
                sys.stdout.write("  .crt .dem .pem")
                return
            ilog("Done Upload")
            ilog("Please wait 5 mins before executing further commands")
        elif SERVICE:
            if num_of_times_executed == 0:
                query_headers = ["Service", "Version", "Server Name"]
                underlines = ["----------", "------------", "-----------"]
                qlog(query_headers, 1)
                qlog(underlines, 1)

            self._redfish_get_managers()
            self._redfish_get_systems_members()
            self._redfish_get_vm_url()
            self._redfish_get_vm_version()
            self._redfish_get_secure_boot_version()
            ilog("Done Query")
        elif ENABLE:
            self._redfish_get_managers()
            self._redfish_get_systems_members()
            result = self._redfish_enable_secure_boot()
            if result == 1:
                sys.stdout.write("Enable Operation Failed\n")
                return
            ilog("Done Enable")
            ilog("Please wait 5 mins before executing further commands")
        elif DISABLE:
            self._redfish_get_managers()
            self._redfish_get_systems_members()
            result = self._redfish_enable_secure_boot()
            if result == 1:
                sys.stdout.write("Disable Operation Failed\n")
                return
            ilog("Done Disable")
            ilog("Please wait 5 mins before executing further commands")
        elif QUERY:
            self._redfish_get_managers()
            self._redfish_get_systems_members()
            self._redfish_query_sb_state()
            result = self._redfish_get_secure_boot_certificates()
            if result == 1:
                sys.stdout.write("Unable to retrieve SB Certificates\n")
                sys.stdout.write("Check output.txt for details\n")
                return
            ilog("Done Query Secure Boot")

        if self.redfish_obj is not None and self.session is True:
            self.redfish_obj.logout()
            dlog1("Session     : Closed")


##############################################################################
#
# Load BMC target info from Config File.
# For each BMC target create target object through parse_target.
#     Add each created target object to target_object_list.
# Insert BMC iso for each object in target_object_list through self.execute
#
##############################################################################

# Find, Open and Read callers config file
# ---------------------------------------
cfg = None

if CONFIG_FILE is not None and os.path.exists(CONFIG_FILE):
    try:
        with open(CONFIG_FILE, 'r') as yaml_config:
            dlog1("Config File : %s" % CONFIG_FILE)
            cfg = yaml.safe_load(yaml_config)
            dlog3("Config Data : %s" % cfg)
    except Exception as ex:
        elog("Unable to open specified config file: %s (%s)" %
             (CONFIG_FILE, ex))
        alog("Check config file access and permissions.\n\n")
        rsbc_exit(1)

    # Parse the config file
    # ----------------------
    found = False  # assume nothing is found to start
    # sys.stdout.write("INSIDE CONFIG VERSION\n\n")
    # loop over all the sections looking for the primary config label
    for section in cfg:
        if section == PRIMARY_CONFIG_LABEL:
            # ... once found then loop over all the targets
            dlog2("VM Iso Label: %s" % cfg[section])
            found = True
            if targets:
                dlog2("Using specified target(s): %s" % targets)
            else:
                for target in cfg[section]:
                    targets.append(target)

            dlog1("Targets     : %s" % targets)
            for target in targets:
                try:
                    parse_target(target, cfg[section][target])
                except Exception as ex:
                    elog("Failed to parse info from '%s' target %s" %
                         (target, ex))
                    alog("Verify %s file has %s target and such target "
                         "is properly formatted" %
                         (CONFIG_FILE, target))
                    continue

    # 'found' would still be false if the config file is for a single target
    if found is False:
        dlog3("Try single")
        parse_target(None, cfg)

    # This is if the --config flag is unused, but bmc_ip bmc_un and bmc_pw are
elif (isinstance(BMC_IP, str) and isinstance(BMC_UN, str) and
        isinstance(BMC_PW, str)):
    # sys.stdout.write("INSIDE IP/PW VERSION\n\n")
    target_name = BMC_IP
    address = BMC_IP
    username = BMC_UN
    pw = BMC_PW

    if is_ipv6_address(address) is True:
        bmc_ipv6 = True
        address = '[' + address + ']'
    else:
        bmc_ipv6 = False

    # Create object and add it to the target object list
    vmc_obj = VmcObject(target_name,
                        address,
                        username,
                        pw)
    if vmc_obj:
        vmc_obj.ipv6 = bmc_ipv6
        target_object_list.append(vmc_obj)
    else:
        elog("Unable to create control object for target")
else:
    elog("No config file or ip/pw present")
    alog("Please provide a config file or the ip address and password\n\n")
    rsbc_exit(1)

if len(target_object_list):
    # Load the Iso for all loaded objects
    count = 0
    for targetObj in target_object_list:
        if targetObj.target is not None:
            ilog("BMC Target  : %s" % targetObj.target)
        if debug == 0:
            ilog("BMC IP Addr : %s" % targetObj.ip)
        targetObj.execute(count)
        ilog("%s is finished executing\n" % targetObj.target)
        count += 1
else:
    elog("Operation aborted ; no valid bmc information found")
    if CONFIG_FILE and cfg:
        ilog("Config File :\n%s" % cfg)
    rsbc_exit(1)

rsbc_exit(0)
