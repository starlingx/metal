#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

from inventory.api.middleware import auth_token
from inventory.api.middleware import parsable_error
# from inventory.api.middleware import json_ext


ParsableErrorMiddleware = parsable_error.ParsableErrorMiddleware
AuthTokenMiddleware = auth_token.AuthTokenMiddleware
# JsonExtensionMiddleware = json_ext.JsonExtensionMiddleware

__all__ = ('ParsableErrorMiddleware',
           'AuthTokenMiddleware')

# 'JsonExtensionMiddleware')
