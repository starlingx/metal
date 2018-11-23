#
# Copyright (c) 2015-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
from inventory.common import constants
from inventory.common import k_host
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
    #     CONF.nfv.catalog_info.split(':')
    region_name = _get_region(context)
    sc = k_service_catalog.ServiceCatalogV2(context.service_catalog)
    service_parameters = {'service_type': 'nfv',
                          'service_name': 'vim',
                          'interface': 'internalURL',
                          'region_name': region_name}
    endpoint = sc.url_for(**service_parameters)
    LOG.info("NFV endpoint=%s" % endpoint)
    return endpoint


def vim_host_add(context, uuid, hostname, subfunctions,
                 admininistrative, operational, availability,
                 subfunction_oper, subfunction_avail,
                 timeout=constants.VIM_DEFAULT_TIMEOUT_IN_SECS):
    """
    Requests VIM to add a host.
    """
    LOG.info("vim_host_add hostname=%s, subfunctions=%s "
             "%s-%s-%s  subfunction_oper=%s subfunction_avail=%s" %
             (hostname, subfunctions, admininistrative, operational,
              availability, subfunction_oper, subfunction_avail))

    api_cmd = _get_endpoint(context)
    api_cmd += "/nfvi-plugins/v1/hosts/"

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    api_cmd_payload = dict()
    api_cmd_payload['uuid'] = uuid
    api_cmd_payload['hostname'] = hostname
    api_cmd_payload['subfunctions'] = subfunctions
    api_cmd_payload['administrative'] = admininistrative
    api_cmd_payload['operational'] = operational
    api_cmd_payload['availability'] = availability
    api_cmd_payload['subfunction_oper'] = subfunction_oper
    api_cmd_payload['subfunction_avail'] = subfunction_avail

    LOG.warn("vim_host_add api_cmd=%s headers=%s payload=%s" %
             (api_cmd, api_cmd_headers, api_cmd_payload))

    response = rest_api_request(context, "POST", api_cmd, api_cmd_headers,
                                json.dumps(api_cmd_payload), timeout)
    return response


def vim_host_action(context, uuid, hostname, action,
                    timeout=constants.VIM_DEFAULT_TIMEOUT_IN_SECS):
    """
    Request VIM to perform host action.
    """

    response = None
    _valid_actions = [k_host.ACTION_UNLOCK,
                      k_host.ACTION_LOCK,
                      k_host.ACTION_FORCE_LOCK]

    if action not in _valid_actions:
        LOG.error("Unrecognized vim_host_action=%s" % action)
        return response

    LOG.warn("vim_host_action hostname=%s, action=%s" % (hostname, action))

    api_cmd = _get_endpoint(context)
    api_cmd += "/nfvi-plugins/v1/hosts/%s" % uuid

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    api_cmd_payload = dict()
    api_cmd_payload['uuid'] = uuid
    api_cmd_payload['hostname'] = hostname
    api_cmd_payload['action'] = action

    LOG.warn("vim_host_action hostname=%s, action=%s  api_cmd=%s "
             "headers=%s payload=%s" %
             (hostname, action, api_cmd, api_cmd_headers, api_cmd_payload))

    response = rest_api_request(context, "PATCH", api_cmd, api_cmd_headers,
                                json.dumps(api_cmd_payload), timeout)
    return response


def vim_host_delete(context, uuid, hostname,
                    timeout=constants.VIM_DEFAULT_TIMEOUT_IN_SECS):
    """
    Asks VIM to delete a host
    """

    api_cmd = _get_endpoint(context)
    api_cmd += "/nfvi-plugins/v1/hosts/%s" % uuid

    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    api_cmd_payload = dict()
    api_cmd_payload['uuid'] = uuid
    api_cmd_payload['hostname'] = hostname
    api_cmd_payload['action'] = 'delete'

    response = rest_api_request(context, "DELETE", api_cmd,
                                api_cmd_headers,
                                json.dumps(api_cmd_payload),
                                timeout=timeout)
    return response


def vim_host_get_instances(context, uuid, hostname,
                           timeout=constants.VIM_DEFAULT_TIMEOUT_IN_SECS):
    """
    Returns instance counts for a given host
    """

    response = None

    api_cmd = _get_endpoint(context)
    api_cmd += "/nfvi-plugins/v1/hosts"
    api_cmd_headers = dict()
    api_cmd_headers['Content-type'] = "application/json"
    api_cmd_headers['User-Agent'] = "inventory/1.0"

    api_cmd_payload = dict()
    api_cmd_payload['uuid'] = uuid
    api_cmd_payload['hostname'] = hostname

    response = rest_api_request(context, "GET", api_cmd, api_cmd_headers,
                                json.dumps(api_cmd_payload), timeout)
    return response
