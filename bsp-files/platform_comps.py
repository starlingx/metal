#!/usr/bin/env python

"""
Copyright (c) 2018 Wind River Systems, Inc.

SPDX-License-Identifier: Apache-2.0

"""
from __future__ import print_function
import getopt
import os
import subprocess
import sys
import xml.etree.ElementTree as ElementTree


def usage():
    print("Usage: %s --groups <groups.xml> --pkgdir <pkgdir>"
          % os.path.basename(sys.argv[0]))
    exit(1)


def add_text_tag_to_xml(parent,
                        name,
                        text):
    """
    Utility function for adding a text tag to an XML object
    :param parent: Parent element
    :param name: Element name
    :param text: Text value
    :return:The created element
    """
    tag = ElementTree.SubElement(parent, name)
    tag.text = text
    tag.tail = '\n    '
    return tag


def add_group(comps, personality, rpmlist=None, filter_dir=None, filter=None):
    """
    Add a software group to the comps.xml
    :param comps: comps element
    :param personality: Personality of node for group
    :param rpmlist: List of all rpms in the base load
    :param filter_dir: Path to filter files
    :param filter: Name of filter file to use
    """

    if rpmlist is not None:
        # Define a base platform group
        groupname = "platform-%s" % personality
        desc = "Platform packages for %s" % personality
    else:
        # Define an empty patch group
        groupname = "updates-%s" % personality
        desc = "Patches for %s" % personality

    group = ElementTree.SubElement(comps, 'group')
    group.tail = '\n'

    add_text_tag_to_xml(group, 'id', groupname)
    add_text_tag_to_xml(group, 'default', "false")
    add_text_tag_to_xml(group, 'uservisible', "true")
    add_text_tag_to_xml(group, 'display_order', "1024")
    add_text_tag_to_xml(group, 'name', groupname)
    add_text_tag_to_xml(group, 'description', desc)

    package_element = ElementTree.SubElement(group,
                                             'packagelist')
    package_element.tail = '\n    '

    if rpmlist is not None:
        # Read the filter file
        f = open(os.path.join(filter_dir, filter), 'r')
        filtered = f.read().split()
        f.close()

        for pkg in sorted(rpmlist):
            if pkg not in filtered:
                tag = ElementTree.SubElement(package_element,
                                             'packagereq',
                                             type="mandatory")
                tag.text = pkg
                tag.tail = '\n        '


def main():
    try:
        opts, remainder = getopt.getopt(sys.argv[1:],
                                        '',
                                        ['pkgdir=',
                                         'groups='])
    except getopt.GetoptError:
        usage()

    pkgdir = None
    groups_file = None

    # Filters are colocated with this script
    filter_dir = os.path.dirname(sys.argv[0])

    for opt, arg in opts:
        if opt == "--pkgdir":
            pkgdir = arg
        elif opt == "--groups":
            groups_file = arg

    if pkgdir is None or groups_file is None:
        usage()

    # Get the pkglist
    cmd = "find %s -name '*.rpm' \
    | xargs rpm -qp --queryformat '%%{NAME}\n'" % pkgdir
    rpmlist = subprocess.check_output(cmd, shell=True).split()

    tree = ElementTree.parse(groups_file)
    comps = tree.getroot()
    comps.tail = '\n'

    add_group(comps, 'controller', rpmlist,
              filter_dir, 'filter_out_from_controller')
    add_group(comps, 'controller-worker', rpmlist,
              filter_dir, 'filter_out_from_smallsystem')
    add_group(comps, 'controller-worker-lowlatency', rpmlist,
              filter_dir, 'filter_out_from_smallsystem_lowlatency')
    add_group(comps, 'worker', rpmlist, filter_dir, 'filter_out_from_worker')
    add_group(comps, 'worker-lowlatency', rpmlist,
              filter_dir, 'filter_out_from_worker_lowlatency')
    add_group(comps, 'storage', rpmlist, filter_dir, 'filter_out_from_storage')

    add_group(comps, 'controller')
    add_group(comps, 'controller-worker')
    add_group(comps, 'controller-worker-lowlatency')
    add_group(comps, 'worker')
    add_group(comps, 'worker-lowlatency')
    add_group(comps, 'storage')

    tree.write(groups_file, encoding="UTF-8")


if __name__ == "__main__":
    main()
