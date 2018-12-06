#
# Copyright (c) 2016-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
from inventory import objects
import json
from keystoneauth1.access import service_catalog as k_service_catalog
from oslo_log import log
from rest_api import rest_api_request

LOG = log.getLogger(__name__)


def _get_region(context):
    system = objects.System.get_one(context)
    return system.region_name


def _get_endpoint(context):
    # service_type, service_name, interface = \
    #     CONF.smapi.catalog_info.split(':')
    region_name = _get_region(context)
    sc = k_service_catalog.ServiceCatalogV2(context.service_catalog)
    service_parameters = {'service_type': 'smapi',
                          'service_name': 'smapi',
                          'interface': 'internalURL',
                          'region_name': region_name}
    endpoint = sc.url_for(**service_parameters)
    return endpoint


def swact_pre_check(context, hostname, timeout=30):
    """
    Sends a Swact Pre-Check command to SM.
    """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/servicenode/%s" % hostname

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    api_cmd_payload = dict()
    api_cmd_payload['origin'] = "inventory"
    api_cmd_payload['action'] = "swact-pre-check"
    api_cmd_payload['admin'] = "unknown"
    api_cmd_payload['oper'] = "unknown"
    api_cmd_payload['avail'] = ""

    response = rest_api_request(context, "PATCH", api_cmd, api_cmd_headers,
                                json.dumps(api_cmd_payload), timeout)

    return response


def lock_pre_check(context, hostname, timeout=30):
    """
        Sends a Lock Pre-Check command to SM.
        """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/servicenode/%s" % hostname

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    api_cmd_payload = dict()
    api_cmd_payload['origin'] = "inventory"
    api_cmd_payload['action'] = "lock-pre-check"
    api_cmd_payload['admin'] = "unknown"
    api_cmd_payload['oper'] = "unknown"
    api_cmd_payload['avail'] = ""

    response = rest_api_request(context, "PATCH", api_cmd, api_cmd_headers,
                                json.dumps(api_cmd_payload), timeout)

    return response


def service_list(context):
    """
    Sends a service list command to SM.
    """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/services"

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['Accept'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    response = rest_api_request(context, "GET", api_cmd, api_cmd_headers, None)

    return response


def service_show(context, hostname):
    """
    Sends a service show command to SM.
    """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/services/%s" % hostname

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['Accept'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    response = rest_api_request(context, "GET", api_cmd, api_cmd_headers, None)
    return response


def servicenode_list(context):
    """
    Sends a service list command to SM.
    """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/nodes"

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['Accept'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    response = rest_api_request(context, "GET", api_cmd, api_cmd_headers, None)

    return response


def servicenode_show(context, hostname):
    """
    Sends a service show command to SM.
    """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/nodes/%s" % hostname

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['Accept'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    response = rest_api_request(context, "GET", api_cmd, api_cmd_headers, None)

    return response


def sm_servicegroup_list(context):
    """
    Sends a service list command to SM.
    """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/sm_sda"

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['Accept'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    response = rest_api_request(context, "GET", api_cmd, api_cmd_headers, None)

    # rename the obsolete sm_sda to sm_servicegroups
    if isinstance(response, dict):
        if 'sm_sda' in response:
            response['sm_servicegroup'] = response.pop('sm_sda')

    return response


def sm_servicegroup_show(context, hostname):
    """
    Sends a service show command to SM.
    """
    api_cmd = _get_endpoint(context)
    api_cmd += "/v1/sm_sda/%s" % hostname

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['Accept'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    response = rest_api_request(context, "GET", api_cmd, api_cmd_headers, None)

    return response
