#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


class BaseException(Exception):
    """An error occurred."""
    def __init__(self, message=None):
        self.message = message

    def __str__(self):
        return str(self.message) or self.__class__.__doc__


class AuthSystem(BaseException):
    """Could not obtain token and endpoint using provided credentials."""
    pass


class CommandError(BaseException):
    """Invalid usage of CLI."""


class InvalidEndpoint(BaseException):
    """The provided endpoint is invalid."""


class CommunicationError(BaseException):
    """Unable to communicate with server."""


class EndpointException(BaseException):
    pass


class ClientException(Exception):
    """DEPRECATED"""


class InvalidAttribute(ClientException):
    pass


class InvalidAttributeValue(ClientException):
    pass


class HTTPException(Exception):
    """Base exception for all HTTP-derived exceptions."""
    code = 'N/A'

    def __init__(self, details=None):
        self.details = details

    def __str__(self):
        return str(self.details) or "%s (HTTP %s)" % (self.__class__.__name__,
                                                      self.code)


class HTTPMultipleChoices(HTTPException):
    code = 300

    def __str__(self):
        self.details = "Requested version of INVENTORY API is not available."
        return "%s (HTTP %s) %s" % (self.__class__.__name__, self.code,
                                    self.details)


class Unauthorized(HTTPException):
    code = 401


class HTTPUnauthorized(Unauthorized):
    pass


class NotFound(HTTPException):
    """DEPRECATED."""
    code = 404


class HTTPNotFound(NotFound):
    pass


class HTTPMethodNotAllowed(HTTPException):
    code = 405


class HTTPInternalServerError(HTTPException):
    code = 500


class HTTPNotImplemented(HTTPException):
    code = 501


class HTTPBadGateway(HTTPException):
    code = 502
