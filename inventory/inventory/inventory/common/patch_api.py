#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from inventory.common import constants
from keystoneauth1.access import service_catalog as k_service_catalog
from oslo_log import log
from rest_api import rest_api_request

LOG = log.getLogger(__name__)


def _get_endpoint(context, region_name):
    # service_type, service_name, interface = \
    #     CONF.patching.catalog_info.split(':')
    sc = k_service_catalog.ServiceCatalogV2(context.service_catalog)
    service_parameters = {'service_type': 'patching',
                          'service_name': 'patching',
                          'interface': 'internalURL',
                          'region_name': region_name}
    endpoint = sc.url_for(**service_parameters)
    return endpoint


def patch_query(context, region_name,
                timeout=constants.PATCH_DEFAULT_TIMEOUT_IN_SECS):
    """
    Request the list of patches known to the patch service
    """

    api_cmd = _get_endpoint(context, region_name)
    api_cmd += "/v1/query/"

    return rest_api_request(context, "GET", api_cmd, timeout=timeout)


def patch_query_hosts(context, region_name,
                      timeout=constants.PATCH_DEFAULT_TIMEOUT_IN_SECS):
    """
    Request the patch state for all hosts known to the patch service
    """

    api_cmd = _get_endpoint(context, region_name)
    api_cmd += "/v1/query_hosts/"

    return rest_api_request(context, "GET", api_cmd, timeout=timeout)


def patch_drop_host(context, hostname, region_name,
                    timeout=constants.PATCH_DEFAULT_TIMEOUT_IN_SECS):
    """
    Notify the patch service to drop the specified host
    """

    api_cmd = _get_endpoint(context, region_name)
    api_cmd += "/v1/drop_host/%s" % hostname

    return rest_api_request(context, "POST", api_cmd, timeout=timeout)
