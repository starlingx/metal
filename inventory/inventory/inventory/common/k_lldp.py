#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# LLDP constants

LLDP_TLV_TYPE_CHASSIS_ID = 'chassis_id'
LLDP_TLV_TYPE_PORT_ID = 'port_identifier'
LLDP_TLV_TYPE_TTL = 'ttl'
LLDP_TLV_TYPE_SYSTEM_NAME = 'system_name'
LLDP_TLV_TYPE_SYSTEM_DESC = 'system_description'
LLDP_TLV_TYPE_SYSTEM_CAP = 'system_capabilities'
LLDP_TLV_TYPE_MGMT_ADDR = 'management_address'
LLDP_TLV_TYPE_PORT_DESC = 'port_description'
LLDP_TLV_TYPE_DOT1_LAG = 'dot1_lag'
LLDP_TLV_TYPE_DOT1_PORT_VID = 'dot1_port_vid'
LLDP_TLV_TYPE_DOT1_MGMT_VID = 'dot1_management_vid'
LLDP_TLV_TYPE_DOT1_PROTO_VIDS = 'dot1_proto_vids'
LLDP_TLV_TYPE_DOT1_PROTO_IDS = 'dot1_proto_ids'
LLDP_TLV_TYPE_DOT1_VLAN_NAMES = 'dot1_vlan_names'
LLDP_TLV_TYPE_DOT1_VID_DIGEST = 'dot1_vid_digest'
LLDP_TLV_TYPE_DOT3_MAC_STATUS = 'dot3_mac_status'
LLDP_TLV_TYPE_DOT3_MAX_FRAME = 'dot3_max_frame'
LLDP_TLV_TYPE_DOT3_POWER_MDI = 'dot3_power_mdi'
LLDP_TLV_VALID_LIST = [LLDP_TLV_TYPE_CHASSIS_ID, LLDP_TLV_TYPE_PORT_ID,
                       LLDP_TLV_TYPE_TTL, LLDP_TLV_TYPE_SYSTEM_NAME,
                       LLDP_TLV_TYPE_SYSTEM_DESC, LLDP_TLV_TYPE_SYSTEM_CAP,
                       LLDP_TLV_TYPE_MGMT_ADDR, LLDP_TLV_TYPE_PORT_DESC,
                       LLDP_TLV_TYPE_DOT1_LAG, LLDP_TLV_TYPE_DOT1_PORT_VID,
                       LLDP_TLV_TYPE_DOT1_VID_DIGEST,
                       LLDP_TLV_TYPE_DOT1_MGMT_VID,
                       LLDP_TLV_TYPE_DOT1_PROTO_VIDS,
                       LLDP_TLV_TYPE_DOT1_PROTO_IDS,
                       LLDP_TLV_TYPE_DOT1_VLAN_NAMES,
                       LLDP_TLV_TYPE_DOT1_VID_DIGEST,
                       LLDP_TLV_TYPE_DOT3_MAC_STATUS,
                       LLDP_TLV_TYPE_DOT3_MAX_FRAME,
                       LLDP_TLV_TYPE_DOT3_POWER_MDI]

LLDP_AGENT_STATE_REMOVED = 'removed'
LLDP_NEIGHBOUR_STATE_REMOVED = LLDP_AGENT_STATE_REMOVED
# LLDP_FULL_AUDIT_COUNT based on frequency of host_lldp_get_and_report()
LLDP_FULL_AUDIT_COUNT = 6
