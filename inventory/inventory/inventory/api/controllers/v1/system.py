#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#


from oslo_log import log
import pecan
from pecan import rest
import six
from wsme import types as wtypes
import wsmeext.pecan as wsme_pecan

from inventory.api.controllers.v1 import base
from inventory.api.controllers.v1 import collection
from inventory.api.controllers.v1 import host
from inventory.api.controllers.v1 import link
from inventory.api.controllers.v1 import types
from inventory.api.controllers.v1 import utils as api_utils
from inventory.common import constants
from inventory.common import exception
from inventory.common import k_host
from inventory import objects

LOG = log.getLogger(__name__)

VALID_VSWITCH_TYPES = [constants.VSWITCH_TYPE_OVS_DPDK]


class System(base.APIBase):
    """API representation of a system.

    This class enforces type checking and value constraints, and converts
    between the internal object model and the API representation of
    a system.
    """

    uuid = types.uuid
    "The UUID of the system"

    name = wtypes.text
    "The name of the system"

    system_type = wtypes.text
    "The type of the system"

    system_mode = wtypes.text
    "The mode of the system"

    description = wtypes.text
    "The name of the system"

    contact = wtypes.text
    "The contact of the system"

    location = wtypes.text
    "The location of the system"

    services = int
    "The services of the system"

    software_version = wtypes.text
    "A textual description of the entity"

    timezone = wtypes.text
    "The timezone of the system"

    links = [link.Link]
    "A list containing a self link and associated system links"

    hosts = [link.Link]
    "Links to the collection of hosts contained in this system"

    capabilities = {wtypes.text: api_utils.ValidTypes(wtypes.text, bool,
                                                      six.integer_types)}
    "System defined capabilities"

    region_name = wtypes.text
    "The region name of the system"

    distributed_cloud_role = wtypes.text
    "The distributed cloud role of the system"

    service_project_name = wtypes.text
    "The service project name of the system"

    security_feature = wtypes.text
    "Kernel arguments associated with enabled spectre/meltdown fix features"

    def __init__(self, **kwargs):
        self.fields = objects.System.fields.keys()

        for k in self.fields:
            # Translate any special internal representation of data to its
            # customer facing form
            if k == 'security_feature':
                # look up which customer-facing-security-feature-string goes
                # with the kernel arguments tracked in sysinv
                kernel_args = kwargs.get(k)
                translated_string = kernel_args

                for user_string, args_string in \
                        constants.SYSTEM_SECURITY_FEATURE_SPECTRE_MELTDOWN_OPTS.iteritems():  # noqa
                    if args_string == kernel_args:
                        translated_string = user_string
                        break
                setattr(self, k, translated_string)
            else:
                # No translation required
                setattr(self, k, kwargs.get(k))

    @classmethod
    def convert_with_links(cls, rpc_system, expand=True):
        minimum_fields = ['id', 'uuid', 'name', 'system_type', 'system_mode',
                          'description', 'capabilities',
                          'contact', 'location', 'software_version',
                          'created_at', 'updated_at', 'timezone',
                          'region_name', 'service_project_name',
                          'distributed_cloud_role', 'security_feature']

        fields = minimum_fields if not expand else None

        iSystem = System.from_rpc_object(rpc_system, fields)

        iSystem.links = [link.Link.make_link('self', pecan.request.host_url,
                                             'systems', iSystem.uuid),
                         link.Link.make_link('bookmark',
                                             pecan.request.host_url,
                                             'systems', iSystem.uuid,
                                             bookmark=True)
                         ]

        if expand:
            iSystem.hosts = [
                link.Link.make_link('self',
                                    pecan.request.host_url,
                                    'systems',
                                    iSystem.uuid + "/hosts"),
                link.Link.make_link('bookmark',
                                    pecan.request.host_url,
                                    'systems',
                                    iSystem.uuid + "/hosts",
                                    bookmark=True)]

        return iSystem


class SystemCollection(collection.Collection):
    """API representation of a collection of systems."""

    systems = [System]
    "A list containing system objects"

    def __init__(self, **kwargs):
        self._type = 'systems'

    @classmethod
    def convert_with_links(cls, systems, limit, url=None,
                           expand=False, **kwargs):
        collection = SystemCollection()
        collection.systems = [System.convert_with_links(ch, expand)
                              for ch in systems]

        collection.next = collection.get_next(limit, url=url, **kwargs)
        return collection


LOCK_NAME = 'SystemController'


class SystemController(rest.RestController):
    """REST controller for system."""

    hosts = host.HostController(from_system=True)
    "Expose hosts as a sub-element of system"

    _custom_actions = {
        'detail': ['GET'],
    }

    def __init__(self):
        self._bm_region = None

    def _bm_region_get(self):
        # only supported region type is BM_EXTERNAL
        if not self._bm_region:
            self._bm_region = k_host.BM_EXTERNAL
        return self._bm_region

    def _get_system_collection(self, marker, limit, sort_key, sort_dir,
                               expand=False, resource_url=None):
        limit = api_utils.validate_limit(limit)
        sort_dir = api_utils.validate_sort_dir(sort_dir)
        marker_obj = None
        if marker:
            marker_obj = objects.System.get_by_uuid(pecan.request.context,
                                                    marker)
        system = pecan.request.dbapi.system_get_list(limit, marker_obj,
                                                     sort_key=sort_key,
                                                     sort_dir=sort_dir)
        for i in system:
            i.capabilities['bm_region'] = self._bm_region_get()

        return SystemCollection.convert_with_links(system, limit,
                                                   url=resource_url,
                                                   expand=expand,
                                                   sort_key=sort_key,
                                                   sort_dir=sort_dir)

    @wsme_pecan.wsexpose(SystemCollection, types.uuid,
                         int, wtypes.text, wtypes.text)
    def get_all(self, marker=None, limit=None, sort_key='id', sort_dir='asc'):
        """Retrieve a list of systems.

        :param marker: pagination marker for large data sets.
        :param limit: maximum number of resources to return in a single result.
        :param sort_key: column to sort results by. Default: id.
        :param sort_dir: direction to sort. "asc" or "desc". Default: asc.
        """
        return self._get_system_collection(marker, limit, sort_key, sort_dir)

    @wsme_pecan.wsexpose(SystemCollection, types.uuid, int,
                         wtypes.text, wtypes.text)
    def detail(self, marker=None, limit=None, sort_key='id', sort_dir='asc'):
        """Retrieve a list of system with detail.

        :param marker: pagination marker for large data sets.
        :param limit: maximum number of resources to return in a single result.
        :param sort_key: column to sort results by. Default: id.
        :param sort_dir: direction to sort. "asc" or "desc". Default: asc.
        """
        # /detail should only work agaist collections
        parent = pecan.request.path.split('/')[:-1][-1]
        if parent != "system":
            raise exception.HTTPNotFound

        expand = True
        resource_url = '/'.join(['system', 'detail'])
        return self._get_system_collection(marker, limit, sort_key, sort_dir,
                                           expand, resource_url)

    @wsme_pecan.wsexpose(System, types.uuid)
    def get_one(self, system_uuid):
        """Retrieve information about the given system.

        :param system_uuid: UUID of a system.
        """
        rpc_system = objects.System.get_by_uuid(pecan.request.context,
                                                system_uuid)

        rpc_system.capabilities['bm_region'] = self._bm_region_get()
        return System.convert_with_links(rpc_system)

    @wsme_pecan.wsexpose(System, body=System)
    def post(self, system):
        """Create a new system."""
        raise exception.OperationNotPermitted

    @wsme_pecan.wsexpose(None, types.uuid, status_code=204)
    def delete(self, system_uuid):
        """Delete a system.

        :param system_uuid: UUID of a system.
        """
        raise exception.OperationNotPermitted
