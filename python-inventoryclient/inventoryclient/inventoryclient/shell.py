#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

"""
Command-line interface for Inventory
"""

import argparse
import httplib2
import inventoryclient
from inventoryclient import client
from inventoryclient.common import utils
from inventoryclient import exc
import logging
from oslo_utils import importutils
import sys


class InventoryShell(object):

    def get_base_parser(self):
        parser = argparse.ArgumentParser(
            prog='inventory',
            description=__doc__.strip(),
            epilog='See "inventory help COMMAND" '
                   'for help on a specific command.',
            add_help=False,
            formatter_class=HelpFormatter,
        )

        # Global arguments
        parser.add_argument('-h', '--help',
                            action='store_true',
                            help=argparse.SUPPRESS,
                            )

        parser.add_argument('--version',
                            action='version',
                            version=inventoryclient.__version__)

        parser.add_argument('--debug',
                            default=bool(utils.env('INVENTORYCLIENT_DEBUG')),
                            action='store_true',
                            help='Defaults to env[INVENTORYCLIENT_DEBUG]')

        parser.add_argument('-v', '--verbose',
                            default=False, action="store_true",
                            help="Print more verbose output")

        parser.add_argument('--timeout',
                            default=600,
                            help='Number of seconds to wait for a response')

        parser.add_argument('--os-username',
                            default=utils.env('OS_USERNAME'),
                            help='Defaults to env[OS_USERNAME]')

        parser.add_argument('--os_username',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-password',
                            default=utils.env('OS_PASSWORD'),
                            help='Defaults to env[OS_PASSWORD]')

        parser.add_argument('--os_password',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-tenant-id',
                            default=utils.env('OS_TENANT_ID'),
                            help='Defaults to env[OS_TENANT_ID]')

        parser.add_argument('--os_tenant_id',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-tenant-name',
                            default=utils.env('OS_TENANT_NAME'),
                            help='Defaults to env[OS_TENANT_NAME]')

        parser.add_argument('--os_tenant_name',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-auth-url',
                            default=utils.env('OS_AUTH_URL'),
                            help='Defaults to env[OS_AUTH_URL]')

        parser.add_argument('--os_auth_url',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-region-name',
                            default=utils.env('OS_REGION_NAME'),
                            help='Defaults to env[OS_REGION_NAME]')

        parser.add_argument('--os_region_name',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-auth-token',
                            default=utils.env('OS_AUTH_TOKEN'),
                            help='Defaults to env[OS_AUTH_TOKEN]')

        parser.add_argument('--os_auth_token',
                            help=argparse.SUPPRESS)

        parser.add_argument('--inventory-url',
                            default=utils.env('INVENTORY_URL'),
                            help='Defaults to env[INVENTORY_URL]')

        parser.add_argument('--inventory_url',
                            help=argparse.SUPPRESS)

        parser.add_argument('--inventory-api-version',
                            default=utils.env(
                                'INVENTORY_API_VERSION', default='1'),
                            help='Defaults to env[INVENTORY_API_VERSION] '
                            'or 1')

        parser.add_argument('--inventory_api_version',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-service-type',
                            default=utils.env('OS_SERVICE_TYPE',
                                              default=client.SERVICE_TYPE),
                            help='Defaults to env[OS_SERVICE_TYPE]')

        parser.add_argument('--os_service_type',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-endpoint-type',
                            default=utils.env('OS_ENDPOINT_TYPE'),
                            help='Defaults to env[OS_ENDPOINT_TYPE]')

        parser.add_argument('--os_endpoint_type',
                            help=argparse.SUPPRESS)

        parser.add_argument('--os-user-domain-id',
                            default=utils.env('OS_USER_DOMAIN_ID'),
                            help='Defaults to env[OS_USER_DOMAIN_ID].')

        parser.add_argument('--os-user-domain-name',
                            default=utils.env('OS_USER_DOMAIN_NAME'),
                            help='Defaults to env[OS_USER_DOMAIN_NAME].')

        parser.add_argument('--os-project-id',
                            default=utils.env('OS_PROJECT_ID'),
                            help='Another way to specify tenant ID. '
                                 'This option is mutually exclusive with '
                                 ' --os-tenant-id. '
                                 'Defaults to env[OS_PROJECT_ID].')

        parser.add_argument('--os-project-name',
                            default=utils.env('OS_PROJECT_NAME'),
                            help='Another way to specify tenant name. '
                                 'This option is mutually exclusive with '
                                 ' --os-tenant-name. '
                                 'Defaults to env[OS_PROJECT_NAME].')

        parser.add_argument('--os-project-domain-id',
                            default=utils.env('OS_PROJECT_DOMAIN_ID'),
                            help='Defaults to env[OS_PROJECT_DOMAIN_ID].')

        parser.add_argument('--os-project-domain-name',
                            default=utils.env('OS_PROJECT_DOMAIN_NAME'),
                            help='Defaults to env[OS_PROJECT_DOMAIN_NAME].')

        return parser

    def get_subcommand_parser(self, version):
        parser = self.get_base_parser()

        self.subcommands = {}
        subparsers = parser.add_subparsers(metavar='<subcommand>')
        submodule = importutils.import_versioned_module('inventoryclient',
                                                        version, 'shell')
        submodule.enhance_parser(parser, subparsers, self.subcommands)
        utils.define_commands_from_module(subparsers, self, self.subcommands)
        self._add_bash_completion_subparser(subparsers)
        return parser

    def _add_bash_completion_subparser(self, subparsers):
        subparser = subparsers.add_parser(
            'bash_completion',
            add_help=False,
            formatter_class=HelpFormatter
        )
        self.subcommands['bash_completion'] = subparser
        subparser.set_defaults(func=self.do_bash_completion)

    def _setup_debugging(self, debug):
        if debug:
            logging.basicConfig(
                format="%(levelname)s (%(module)s:%(lineno)d) %(message)s",
                level=logging.DEBUG)

            httplib2.debuglevel = 1
        else:
            logging.basicConfig(format="%(levelname)s %(message)s",
                                level=logging.CRITICAL)

    def main(self, argv):
        # Parse args once to find version
        parser = self.get_base_parser()
        (options, args) = parser.parse_known_args(argv)
        self._setup_debugging(options.debug)

        # build available subcommands based on version
        api_version = options.inventory_api_version
        subcommand_parser = self.get_subcommand_parser(api_version)
        self.parser = subcommand_parser

        # Handle top-level --help/-h before attempting to parse
        # a command off the command line
        if options.help or not argv:
            self.do_help(options)
            return 0

        # Parse args again and call whatever callback was selected
        args = subcommand_parser.parse_args(argv)

        # Short-circuit and deal with help command right away.
        if args.func == self.do_help:
            self.do_help(args)
            return 0
        elif args.func == self.do_bash_completion:
            self.do_bash_completion(args)
            return 0

        if not (args.os_auth_token and args.inventory_url):
            if not args.os_username:
                raise exc.CommandError("You must provide a username via "
                                       "either --os-username or via "
                                       "env[OS_USERNAME]")

            if not args.os_password:
                raise exc.CommandError("You must provide a password via "
                                       "either --os-password or via "
                                       "env[OS_PASSWORD]")

            if not (args.os_project_id or args.os_project_name):
                raise exc.CommandError("You must provide a project name via "
                                       "either --os-project-name or via "
                                       "env[OS_PROJECT_NAME]")

            if not args.os_auth_url:
                raise exc.CommandError("You must provide an auth url via "
                                       "either --os-auth-url or via "
                                       "env[OS_AUTH_URL]")

            if not args.os_region_name:
                raise exc.CommandError("You must provide an region name via "
                                       "either --os-region-name or via "
                                       "env[OS_REGION_NAME]")

        client_args = (
            'os_auth_token', 'inventory_url', 'os_username', 'os_password',
            'os_auth_url', 'os_project_id', 'os_project_name', 'os_tenant_id',
            'os_tenant_name', 'os_region_name', 'os_user_domain_id',
            'os_user_domain_name', 'os_project_domain_id',
            'os_project_domain_name', 'os_service_type', 'os_endpoint_type',
            'timeout'
        )
        kwargs = {}
        for key in client_args:
            client_key = key.replace("os_", "", 1)
            kwargs[client_key] = getattr(args, key)

        client = inventoryclient.client.get_client(api_version, **kwargs)

        try:
            args.func(client, args)
        except exc.Unauthorized:
            raise exc.CommandError("Invalid Identity credentials.")

    def do_bash_completion(self, args):
        """Prints all of the commands and options to stdout.
        """
        commands = set()
        options = set()
        for sc_str, sc in self.subcommands.items():
            commands.add(sc_str)
            for option in list(sc._optionals._option_string_actions):
                options.add(option)

        commands.remove('bash_completion')
        print(' '.join(commands | options))

    @utils.arg('command', metavar='<subcommand>', nargs='?',
               help='Display help for <subcommand>')
    def do_help(self, args):
        """Display help about this program or one of its subcommands."""
        if getattr(args, 'command', None):
            if args.command in self.subcommands:
                self.subcommands[args.command].print_help()
            else:
                raise exc.CommandError("'%s' is not a valid subcommand" %
                                       args.command)
        else:
            self.parser.print_help()


class HelpFormatter(argparse.HelpFormatter):
    def start_section(self, heading):
        # Title-case the headings
        heading = '%s%s' % (heading[0].upper(), heading[1:])
        super(HelpFormatter, self).start_section(heading)


def main():
    try:
        InventoryShell().main(sys.argv[1:])

    except KeyboardInterrupt as e:
        print >> sys.stderr, ('caught: %r, aborting' % (e))
        sys.exit(0)

    except IOError as e:
        sys.exit(0)

    except Exception as e:
        print >> sys.stderr, e
        sys.exit(1)


if __name__ == "__main__":
    main()
