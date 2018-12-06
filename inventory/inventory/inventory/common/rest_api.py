#
# Copyright (c) 2015-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
import json
import signal
import urllib2

from inventory.common.exception import OpenStackException
from inventory.common.exception import OpenStackRestAPIException

from oslo_log import log
LOG = log.getLogger(__name__)


def rest_api_request(context, method, api_cmd, api_cmd_headers=None,
                     api_cmd_payload=None, timeout=10):
    """
    Make a rest-api request
    Returns: response as a dictionary
    """

    LOG.info("%s cmd:%s hdr:%s payload:%s" % (method,
             api_cmd, api_cmd_headers, api_cmd_payload))

    if hasattr(context, 'auth_token'):
        token = context.auth_token
    else:
        token = None

    response = None
    try:
        request_info = urllib2.Request(api_cmd)
        request_info.get_method = lambda: method
        if token:
            request_info.add_header("X-Auth-Token", token)
        request_info.add_header("Accept", "application/json")

        if api_cmd_headers is not None:
            for header_type, header_value in api_cmd_headers.items():
                request_info.add_header(header_type, header_value)

        if api_cmd_payload is not None:
            request_info.add_data(api_cmd_payload)

        request = urllib2.urlopen(request_info, timeout=timeout)
        response = request.read()

        if response == "":
            response = json.loads("{}")
        else:
            response = json.loads(response)
        request.close()

        LOG.info("Response=%s" % response)

    except urllib2.HTTPError as e:
        LOG.warn("HTTP Error e.code=%s e=%s" % (e.code, e))
        if hasattr(e, 'msg') and e.msg:
            response = json.loads(e.msg)
        else:
            response = json.loads("{}")

        LOG.info("HTTPError response=%s" % (response))
        raise OpenStackRestAPIException(e.message, e.code, "%s" % e)
    except urllib2.URLError as e:
        LOG.warn("URLError Error e=%s" % (e))
        raise OpenStackException(e.message, "%s" % e)

    finally:
        signal.alarm(0)
        return response
